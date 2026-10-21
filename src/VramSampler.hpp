// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Stop on low video memory (we-manager vram patch): the GPU's free memory,
// sampled once a second for the whole process, and VramPolicy's verdict.
//
// One monitor per process (VramSampler.cpp) runs while any VramSampler in it
// is enabled: a worker thread with a Vulkan instance of its own, opened
// with dlopen (no link to libvulkan, so a missing loader is only "not
// available").  It reads VK_EXT_memory_budget on the GPU Plasma draws with:
// the one a scene renderer last picked (SceneBackend.cpp reports it through
// wekde_vram_scene_device), else the discrete GPU, else the only one.  It
// samples whether or not a scene runs, so it also sees memory come back
// while the wallpapers are stopped, and for video and web wallpapers.
//
// QML (VramWatch.qml) makes one VramSampler per screen; they all show the
// process's one monitor.  Nothing here touches a wallpaper: main.qml
// stops and loads them.

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <cstdint>

namespace wekde
{

class VramSampler : public QObject {
    Q_OBJECT
    // Sample while true (main.xml VramStop on for this screen).
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // The driver answered: budget numbers are coming.  When false, `reason`
    // names the call that failed, and nothing is ever stopped.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString reason READ reason NOTIFY changed)
    Q_PROPERTY(QString device READ device NOTIFY changed)
    // VramPolicy: stop the wallpapers now; false again when they may load.
    Q_PROPERTY(bool stopWanted READ stopWanted NOTIFY changed)
    // Too little room for two wallpapers at once (no handoff overlap).
    Q_PROPERTY(bool tight READ tight NOTIFY changed)
    Q_PROPERTY(double freeMiB READ freeMiB NOTIFY changed)
    Q_PROPERTY(double heapMiB READ heapMiB NOTIFY changed)
    // What this screen's scene says it holds (SceneViewer.vramMiB after its
    // first frame; texture-quality patch): summed over the screens, the
    // footprint's estimate (VramPolicy::setEstimate).  0: none (a video or
    // web wallpaper, or no scene drawn yet).
    Q_PROPERTY(double estimateMiB READ estimateMiB WRITE setEstimateMiB NOTIFY estimateChanged)

public:
    explicit VramSampler(QObject* parent = nullptr);
    ~VramSampler() override;

    bool    enabled() const { return m_enabled; }
    void    setEnabled(bool on);
    bool    available() const;
    QString reason() const;
    QString device() const;
    bool    stopWanted() const;
    bool    tight() const;
    double  freeMiB() const;
    double  heapMiB() const;
    double  estimateMiB() const { return static_cast<double>(m_estimateBytes) / (1024.0 * 1024.0); }
    void    setEstimateMiB(double mib);

    // The monitor's state as JSON (WekControl.VramStatus): available,
    // reason, device, deviceType, usage, budget, heap, free, footprint,
    // backoffMs, stopWanted, tight, running (MiB for the sizes).
    Q_INVOKABLE QString     statusJson() const;
    static QString          processStatusJson();
    static QVariantMap      processStatus();
    // What ~QCoreApplication runs (qAddPostRoutine, installed with the
    // monitor's worker): the worker goes idle and posts nothing more, and
    // its Vulkan instance is given up to 100 ms to close.  Public for the tests.
    static void             shutdownForExit();

signals:
    void enabledChanged();
    void changed();
    void estimateChanged();

private:
    bool          m_enabled { false };
    std::uint64_t m_estimateBytes { 0 };
};

} // namespace wekde

// SceneBackend.cpp (the scene renderer's QML bridge) calls this, when it is
// linked, with the device UUID it renders on.
extern "C" void wekde_vram_scene_device(const unsigned char* uuid, int size);
