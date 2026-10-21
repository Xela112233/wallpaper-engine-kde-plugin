// SPDX-License-Identifier: GPL-2.0-only
// Stop on low video memory (we-manager vram patch): the process-wide
// monitor behind every VramSampler.  See VramSampler.hpp.
#include "VramSampler.hpp"
#include "VramPolicy.hpp"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QSet>
#include <QtDebug>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <dlfcn.h>

namespace wekde
{
namespace
{

constexpr int    kSampleMs = 1000;
constexpr int    kRetryMs  = 60'000;   // a driver that did not answer is asked again this often
constexpr int    kExitWaitMs = 100;    // at exit, how long to wait for the worker to close Vulkan
constexpr double kMiB      = 1024.0 * 1024.0;

using Uuid = std::array<std::uint8_t, VK_UUID_SIZE>;

struct Snapshot {
    bool       running { false };
    bool       available { false };
    QString    reason { QStringLiteral("not started: no screen has VramStop on") };
    QString    device;
    QString    deviceType;
    VramSample sample;
    bool       stopWanted { false };
    bool       tight { false };
    quint64    footprint { 0 };
    quint64    estimate { 0 };
    qint64     backoffMs { 0 };
};

QString typeName(VkPhysicalDeviceType t) {
    switch (t) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return QStringLiteral("discrete");
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return QStringLiteral("integrated (its memory is system memory)");
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return QStringLiteral("virtual");
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return QStringLiteral("cpu");
    default: return QStringLiteral("other");
    }
}

QString vkName(VkResult r) {
    switch (r) {
    case VK_SUCCESS: return QStringLiteral("VK_SUCCESS");
    case VK_INCOMPLETE: return QStringLiteral("VK_INCOMPLETE");
    case VK_ERROR_OUT_OF_HOST_MEMORY: return QStringLiteral("VK_ERROR_OUT_OF_HOST_MEMORY");
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return QStringLiteral("VK_ERROR_OUT_OF_DEVICE_MEMORY");
    case VK_ERROR_INITIALIZATION_FAILED: return QStringLiteral("VK_ERROR_INITIALIZATION_FAILED");
    case VK_ERROR_LAYER_NOT_PRESENT: return QStringLiteral("VK_ERROR_LAYER_NOT_PRESENT");
    case VK_ERROR_EXTENSION_NOT_PRESENT: return QStringLiteral("VK_ERROR_EXTENSION_NOT_PRESENT");
    case VK_ERROR_INCOMPATIBLE_DRIVER: return QStringLiteral("VK_ERROR_INCOMPATIBLE_DRIVER");
    default: return QStringLiteral("VkResult %1").arg(static_cast<int>(r));
    }
}

class Monitor {
public:
    // Never destroyed: its worker may be inside a driver call when the
    // process exits, and must not find the object gone.
    static Monitor& get() {
        static Monitor* m = new Monitor;
        return *m;
    }

    // GUI thread.  Neither waits for the worker: it is told, and goes idle
    // (its Vulkan instance closed) or wakes up on its own thread.
    void addUser(VramSampler* s) {
        m_users.insert(s);
        if (m_users.size() == 1) setActive(true);
    }
    void removeUser(VramSampler* s) {
        if (! m_users.remove(s)) return;
        if (m_users.isEmpty()) setActive(false);
    }

    // GUI thread: one screen's scene estimate (VramSampler::estimateMiB);
    // the worker reads the sum over the screens at each sample.
    void setEstimate(VramSampler* s, std::uint64_t bytes) {
        if (bytes)
            m_estimates.insert(s, bytes);
        else
            m_estimates.remove(s);
        std::uint64_t sum = 0;
        for (auto v : std::as_const(m_estimates)) sum += v;
        m_estimateSum.store(sum);
    }

    Snapshot snapshot() const {
        QMutexLocker lock(&m_snapMx);
        return m_snap;
    }

    // Any thread (the renderer's).
    void setSceneDevice(const std::uint8_t* uuid, int size) {
        if (! uuid || size != VK_UUID_SIZE) return;
        QMutexLocker lock(&m_snapMx);
        std::memcpy(m_sceneUuid.data(), uuid, VK_UUID_SIZE);
        m_haveSceneUuid = true;
    }

private:
    Monitor() = default;

    // The worker is started once, detached, and then only woken or sent to
    // idle: turning VramStop off never joins a thread on the GUI thread
    // (vkCreateInstance or a sample can take hundreds of ms on some drivers,
    // without bound under a GPU hang).
    void setActive(bool on) {
        std::lock_guard<std::mutex> lock(m_mx);
        if (m_exiting) return;               // the application is going: never again
        m_active = on;
        ++m_gen;
        if (! on) {
            QMutexLocker snap(&m_snapMx);
            m_snap.running    = false;
            m_snap.stopWanted = false;
            m_snap.tight      = false;
        }
        if (on && ! m_started) {
            m_started = true;
            // Before the QApplication goes, the worker goes idle (exitRoutine).
            if (QCoreApplication::instance()) qAddPostRoutine(&Monitor::exitRoutine);
            std::thread([this] { run(); }).detach();
        }
        m_cv.notify_all();
    }

public:
    // Run by ~QCoreApplication (qAddPostRoutine), and by VramSampler::
    // shutdownForExit: plasmashell may destroy its application while a
    // screen still has VramStop on (its wallpaper items not torn down
    // first).  The worker is turned off under m_mx, which it holds while it
    // checks m_active and posts to the application, so nothing is posted to
    // an application being destroyed; then up to kExitWaitMs for it to close
    // its Vulkan instance before the driver's own teardown runs, and no
    // longer, so an exit never hangs on a stuck driver call.
    static void exitRoutine() {
        Monitor&                     m = get();
        std::unique_lock<std::mutex> lock(m.m_mx);
        m.m_exiting = true;
        m.m_active  = false;
        ++m.m_gen;
        {
            QMutexLocker snap(&m.m_snapMx);
            m.m_snap.running    = false;
            m.m_snap.stopWanted = false;
            m.m_snap.tight      = false;
        }
        m.m_cv.notify_all();
        if (m.m_started)
            m.m_cv.wait_for(lock, std::chrono::milliseconds(kExitWaitMs), [&m] { return m.m_idle; });
    }

private:

    // The worker: idle until a screen turns VramStop on, then open Vulkan
    // and sample once a second until the last one turns it off.
    void run() {
        while (true) {
            std::uint64_t gen = 0;
            {
                std::unique_lock<std::mutex> lock(m_mx);
                m_idle = true;               // Vulkan closed (exitRoutine waits for this)
                m_cv.notify_all();
                m_cv.wait(lock, [this] { return m_active; });
                m_idle = false;
                gen    = m_gen;
            }
            sampleWhileActive(gen);
            closeVulkan();
        }
    }

    void sampleWhileActive(std::uint64_t gen) {
        VramPolicy policy;                   // a fresh one each time it is turned on
        bool       wasAvailable = true;      // so the first failure is logged
        while (true) {
            Snapshot   s = snapshot();
            QString    why;
            VramSample sample;
            const bool ok = (m_instance != VK_NULL_HANDLE || openVulkan(why)) && pickDevice(s, why)
                            && takeSample(sample, why);
            if (ok) {
                policy.setEstimate(m_estimateSum.load());
                const bool changed = policy.feed(sample);
                s.available        = true;
                s.reason.clear();
                s.sample     = sample;
                s.stopWanted = policy.stopWanted();
                s.tight      = policy.tight();
                s.footprint  = policy.footprint();
                s.estimate   = policy.estimate();
                s.backoffMs  = policy.backoffMs();
                if (changed) logChange(s, policy);
            } else {
                if (wasAvailable) qWarning().noquote() << "[WEK] vram: video memory cannot be read, so VramStop "
                                                          "stops nothing:" << why;
                s.available  = false;
                s.reason     = why;
                s.stopWanted = false;
                s.tight      = false;
                closeVulkan();
            }
            wasAvailable = ok;
            s.running    = true;
            std::unique_lock<std::mutex> lock(m_mx);
            if (! m_active || m_gen != gen) return;     // turned off meanwhile: publish nothing
            {
                QMutexLocker snap(&m_snapMx);
                m_snap = s;
            }
            notify();
            if (m_cv.wait_for(lock, std::chrono::milliseconds(ok ? kSampleMs : kRetryMs),
                              [this, gen] { return ! m_active || m_gen != gen; }))
                return;
        }
    }

    // Called with m_mx held, after the check that the worker is still on:
    // exitRoutine turns it off under the same mutex.
    void notify() {
        QCoreApplication* app = QCoreApplication::instance();
        if (! app || m_exiting) return;
        QMetaObject::invokeMethod(
            app,
            [this] {
                for (VramSampler* u : std::as_const(m_users)) emit u->changed();
            },
            Qt::QueuedConnection);
    }

    void logChange(const Snapshot& s, const VramPolicy& p) {
        const auto& v   = s.sample;
        const auto  mib = [](std::uint64_t b) { return QString::number(static_cast<double>(b) / kMiB, 'f', 0); };
        if (s.stopWanted)
            qInfo().noquote() << QStringLiteral("[WEK] vram: stopping the wallpapers with VramStop on: %1 MiB free "
                                                "(below %2), this process uses %3 MiB, budget %4 MiB, heap %5 MiB on "
                                                "%6; back-off %7 s")
                                     .arg(mib(v.headroom()), mib(VramPolicy::floor(v.heapSize)), mib(v.usage),
                                          mib(v.budget), mib(v.heapSize), s.device)
                                     .arg(p.backoffMs() / 1000);
        else
            qInfo().noquote() << QStringLiteral("[WEK] vram: the wallpapers may load again: %1 MiB free (at least %2: "
                                                "their footprint %3 MiB and the margin) on %4")
                                     .arg(mib(v.headroom()), mib(p.required(v.heapSize)), mib(p.footprint()),
                                          s.device);
    }

    // -- Vulkan, on the worker thread only ----------------------------------
    bool openVulkan(QString& why) {
        if (! m_lib) m_lib = ::dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (! m_lib) {
            const char* e = ::dlerror();
            why = QStringLiteral("dlopen(libvulkan.so.1) failed: %1").arg(QString::fromLocal8Bit(e ? e : "?"));
            return false;
        }
        auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(::dlsym(m_lib, "vkGetInstanceProcAddr"));
        if (! gipa) {
            why = QStringLiteral("libvulkan.so.1 has no vkGetInstanceProcAddr");
            return false;
        }
        auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE, "vkCreateInstance"));
        if (! create) {
            why = QStringLiteral("vkGetInstanceProcAddr(vkCreateInstance) gave nothing");
            return false;
        }
        VkApplicationInfo app {};
        app.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "wallpaper-engine-kde-plugin vram monitor";
        app.apiVersion       = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ci {};
        ci.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;
        const VkResult r    = create(&ci, nullptr, &m_instance);
        if (r != VK_SUCCESS) {
            m_instance = VK_NULL_HANDLE;
            why        = QStringLiteral("vkCreateInstance (Vulkan 1.1) returned %1: no Vulkan driver for this GPU")
                      .arg(vkName(r));
            return false;
        }
        m_destroy  = reinterpret_cast<PFN_vkDestroyInstance>(gipa(m_instance, "vkDestroyInstance"));
        m_enum     = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(m_instance, "vkEnumeratePhysicalDevices"));
        m_props    = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            gipa(m_instance, "vkGetPhysicalDeviceProperties"));
        m_props2   = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            gipa(m_instance, "vkGetPhysicalDeviceProperties2"));
        m_mem2     = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(
            gipa(m_instance, "vkGetPhysicalDeviceMemoryProperties2"));
        m_devExts  = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
            gipa(m_instance, "vkEnumerateDeviceExtensionProperties"));
        if (! m_destroy || ! m_enum || ! m_props || ! m_props2 || ! m_mem2 || ! m_devExts) {
            why = QStringLiteral("the Vulkan loader lacks a Vulkan 1.1 entry point "
                                 "(vkGetPhysicalDeviceMemoryProperties2 or another)");
            closeVulkan();
            return false;
        }
        m_device = VK_NULL_HANDLE;
        return true;
    }

    void closeVulkan() {
        if (m_instance != VK_NULL_HANDLE && m_destroy) m_destroy(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        m_device   = VK_NULL_HANDLE;
    }

    bool hasExtension(VkPhysicalDevice d, const char* name) {
        uint32_t n = 0;
        if (m_devExts(d, nullptr, &n, nullptr) != VK_SUCCESS) return false;
        std::vector<VkExtensionProperties> exts(n);
        if (m_devExts(d, nullptr, &n, exts.data()) != VK_SUCCESS) return false;
        for (const auto& e : exts)
            if (std::strcmp(e.extensionName, name) == 0) return true;
        return false;
    }

    // The PCI addresses of the GPUs that drive the outputs (sysfs boot_vga).
    static std::vector<PciAddress> bootVgaDevices() {
        std::vector<PciAddress> out;
        const QDir drm(QStringLiteral("/sys/class/drm"));
        for (const QString& card : drm.entryList({ QStringLiteral("card*") }, QDir::Dirs | QDir::System)) {
            if (card.contains(QLatin1Char('-'))) continue;                 // card0-DP-1: a connector
            QFile f(drm.filePath(card + QStringLiteral("/device/boot_vga")));
            if (! f.open(QIODevice::ReadOnly) || f.readAll().trimmed() != "1") continue;
            // device -> ../../../0000:03:00.0
            const QString pci = QFileInfo(drm.filePath(card + QStringLiteral("/device"))).canonicalFilePath()
                                    .section(QLatin1Char('/'), -1);
            unsigned dom = 0, bus = 0, dev = 0, fn = 0;
            if (std::sscanf(pci.toLatin1().constData(), "%x:%x:%x.%x", &dom, &bus, &dev, &fn) == 4)
                out.push_back(PciAddress { dom, bus, dev, fn });
        }
        return out;
    }

    // The GPU to watch (VramPolicy.hpp chooseDevice): the one a scene
    // renders on, else the one that drives the outputs, else the discrete
    // one.  Checked every sample (cheap): a scene that reports its device
    // later moves the watch there.
    bool pickDevice(Snapshot& s, QString& why) {
        Uuid scene {};
        bool haveScene = false;
        {
            QMutexLocker lock(&m_snapMx);
            scene     = m_sceneUuid;
            haveScene = m_haveSceneUuid;
        }
        if (m_device != VK_NULL_HANDLE && (! haveScene || scene == m_deviceUuid)) return true;
        uint32_t n = 0;
        VkResult r = m_enum(m_instance, &n, nullptr);
        if (r != VK_SUCCESS || n == 0) {
            why = r != VK_SUCCESS ? QStringLiteral("vkEnumeratePhysicalDevices returned %1").arg(vkName(r))
                                  : QStringLiteral("vkEnumeratePhysicalDevices found no GPU");
            return false;
        }
        std::vector<VkPhysicalDevice> devs(n);
        m_enum(m_instance, &n, devs.data());
        std::vector<VramDevice> descs;
        std::vector<QString>    names;
        for (VkPhysicalDevice d : devs) {
            // Vulkan 1.0's call first: a device below 1.1 has no
            // vkGetPhysicalDeviceProperties2 to ask (chooseDevice skips it).
            VkPhysicalDeviceProperties p1 {};
            m_props(d, &p1);
            VramDevice v;
            v.apiVersion = p1.apiVersion;
            v.type       = static_cast<int>(p1.deviceType);
            if (p1.apiVersion >= VK_API_VERSION_1_1 && p1.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) {
                VkPhysicalDeviceIDProperties id {};
                id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
                VkPhysicalDevicePCIBusInfoPropertiesEXT pci {};
                pci.sType     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;
                v.havePci     = hasExtension(d, VK_EXT_PCI_BUS_INFO_EXTENSION_NAME);
                if (v.havePci) id.pNext = &pci;
                VkPhysicalDeviceProperties2 p2 {};
                p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
                p2.pNext = &id;
                m_props2(d, &p2);
                std::memcpy(v.uuid.data(), id.deviceUUID, VK_UUID_SIZE);
                v.pciDomain   = pci.pciDomain;
                v.pciBus      = pci.pciBus;
                v.pciDevice   = pci.pciDevice;
                v.pciFunction = pci.pciFunction;
            }
            descs.push_back(v);
            names.push_back(QString::fromUtf8(p1.deviceName));
        }
        const std::vector<PciAddress> bootVga = bootVgaDevices();
        const int i = chooseDevice(descs, haveScene ? scene.data() : nullptr, bootVga);
        if (i < 0) {
            why = QStringLiteral("no GPU with Vulkan 1.1 answers (only a software driver such as llvmpipe, or "
                                 "devices below Vulkan 1.1): no video memory to watch");
            return false;
        }
        if (! hasExtension(devs[i], VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) {
            why = QStringLiteral("%1 has no VK_EXT_memory_budget, so its free memory cannot be read").arg(names[i]);
            m_device = VK_NULL_HANDLE;
            return false;
        }
        const auto type = static_cast<VkPhysicalDeviceType>(descs[i].type);
        const bool atScene = haveScene && descs[i].uuid == scene;
        if (m_device != devs[i])
            qInfo().noquote() << "[WEK] vram: watching" << names[i] << "(" + typeName(type) + ")"
                              << (atScene ? "the GPU the scenes render on"
                                          : (bootVga.empty() ? "until a scene says which GPU it renders on"
                                                             : "the GPU that drives the outputs, until a scene says "
                                                               "which GPU it renders on"));
        m_device     = devs[i];
        m_deviceUuid = descs[i].uuid;
        s.device     = names[i];
        s.deviceType = typeName(type);
        return true;
    }

    bool takeSample(VramSample& out, QString& why) {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget {};
        budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
        VkPhysicalDeviceMemoryProperties2 mem {};
        mem.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        mem.pNext = &budget;
        m_mem2(m_device, &mem);
        const auto& props = mem.memoryProperties;
        out               = VramSample {};
        // Monotonic: a wall clock set back or forward (NTP after a resume)
        // would stretch or cut VramPolicy's waits.
        out.tMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
        for (uint32_t i = 0; i < props.memoryHeapCount && i < VK_MAX_MEMORY_HEAPS; ++i) {
            if (! (props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) continue;
            out.usage += budget.heapUsage[i];
            out.budget += budget.heapBudget[i];
            out.heapSize += props.memoryHeaps[i].size;
        }
        if (out.budget == 0 || out.heapSize == 0) {
            why = QStringLiteral("vkGetPhysicalDeviceMemoryProperties2 gave no device-local budget");
            return false;
        }
        return true;
    }

    QSet<VramSampler*>      m_users;     // GUI thread only
    QHash<VramSampler*, std::uint64_t> m_estimates;   // GUI thread only
    std::atomic<std::uint64_t>         m_estimateSum { 0 };
    std::mutex              m_mx;
    std::condition_variable m_cv;
    bool                    m_active { false };
    bool                    m_started { false };
    bool                    m_idle { true };      // the worker waits, its Vulkan instance closed
    bool                    m_exiting { false };  // exitRoutine ran: nothing more is started or posted
    std::uint64_t           m_gen { 0 };
    mutable QMutex          m_snapMx;
    Snapshot                m_snap;
    Uuid                    m_sceneUuid {};
    bool                    m_haveSceneUuid { false };

    void*                                     m_lib { nullptr };
    VkInstance                                m_instance { VK_NULL_HANDLE };
    VkPhysicalDevice                          m_device { VK_NULL_HANDLE };
    Uuid                                      m_deviceUuid {};
    PFN_vkDestroyInstance                     m_destroy { nullptr };
    PFN_vkEnumeratePhysicalDevices            m_enum { nullptr };
    PFN_vkGetPhysicalDeviceProperties         m_props { nullptr };
    PFN_vkGetPhysicalDeviceProperties2        m_props2 { nullptr };
    PFN_vkGetPhysicalDeviceMemoryProperties2  m_mem2 { nullptr };
    PFN_vkEnumerateDeviceExtensionProperties  m_devExts { nullptr };
};

} // namespace

VramSampler::VramSampler(QObject* parent): QObject(parent) {}

VramSampler::~VramSampler() {
    if (m_enabled) Monitor::get().removeUser(this);
    Monitor::get().setEstimate(this, 0);
}

void VramSampler::setEnabled(bool on) {
    if (on == m_enabled) return;
    m_enabled = on;
    if (on) {
        Monitor::get().addUser(this);
    } else {
        Monitor::get().removeUser(this);
    }
    Monitor::get().setEstimate(this, on ? m_estimateBytes : 0);
    emit enabledChanged();
    emit changed();
}

bool    VramSampler::available() const { return m_enabled && Monitor::get().snapshot().available; }
QString VramSampler::reason() const {
    return m_enabled ? Monitor::get().snapshot().reason : QStringLiteral("VramStop is off on this screen");
}
QString VramSampler::device() const { return Monitor::get().snapshot().device; }
bool    VramSampler::stopWanted() const { return m_enabled && Monitor::get().snapshot().stopWanted; }
bool    VramSampler::tight() const { return m_enabled && Monitor::get().snapshot().tight; }
double  VramSampler::freeMiB() const { return Monitor::get().snapshot().sample.headroom() / kMiB; }
double  VramSampler::heapMiB() const { return Monitor::get().snapshot().sample.heapSize / kMiB; }

QVariantMap VramSampler::processStatus() {
    const Snapshot s = Monitor::get().snapshot();
    const auto     mib = [](std::uint64_t b) { return qRound64(static_cast<double>(b) / kMiB); };
    return {
        { QStringLiteral("running"), s.running },
        { QStringLiteral("available"), s.available },
        { QStringLiteral("reason"), s.reason },
        { QStringLiteral("device"), s.device },
        { QStringLiteral("deviceType"), s.deviceType },
        { QStringLiteral("usageMiB"), mib(s.sample.usage) },
        { QStringLiteral("budgetMiB"), mib(s.sample.budget) },
        { QStringLiteral("heapMiB"), mib(s.sample.heapSize) },
        { QStringLiteral("freeMiB"), mib(s.sample.headroom()) },
        { QStringLiteral("footprintMiB"), mib(s.footprint) },
        { QStringLiteral("estimateMiB"), mib(s.estimate) },
        { QStringLiteral("backoffMs"), s.backoffMs },
        { QStringLiteral("stopWanted"), s.stopWanted },
        { QStringLiteral("tight"), s.tight },
    };
}

QString VramSampler::processStatusJson() {
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(processStatus())).toJson(QJsonDocument::Compact));
}

QString VramSampler::statusJson() const { return processStatusJson(); }

void VramSampler::setEstimateMiB(double mib) {
    const std::uint64_t bytes = mib > 0 ? static_cast<std::uint64_t>(mib * kMiB) : 0;
    if (bytes == m_estimateBytes) return;
    m_estimateBytes = bytes;
    Monitor::get().setEstimate(this, m_enabled ? bytes : 0);
    emit estimateChanged();
}

void VramSampler::shutdownForExit() { Monitor::exitRoutine(); }

} // namespace wekde

extern "C" void wekde_vram_scene_device(const unsigned char* uuid, int size) {
    wekde::Monitor::get().setSceneDevice(uuid, size);
}
