// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Stop on low video memory (we-manager vram patch): when to stop the
// wallpapers and when they may load again, from samples of the GPU's memory
// as VK_EXT_memory_budget gives them (VramSampler.cpp takes them).  No Qt,
// no Vulkan: tests/tst_vrampolicy.cpp drives it with made-up samples.
//
// A sample is this process's device-local usage, its budget (that usage
// plus what the driver says is free for it, other processes' use taken off)
// and the device-local heap size.  What counts is the headroom, budget minus
// usage: the memory a game can still have.  A ratio would not do: a 150 MiB
// wallpaper with 50 MiB left on an 8 GiB card reads 75 %.
//
// * stopWanted turns on when the headroom stays below floor() for kOnMs.
// * The wallpapers' footprint is what their stop gave back, kFootprintMs
//   after it: the larger of the drop in this process's usage and the rise in
//   the headroom.  The headroom counts memory the driver does not put on this
//   process (radeonsi's GL contexts for mpv, Chromium's GPU process for web
//   wallpapers, possibly NVIDIA's), so a wallpaper the usage cannot see is
//   still measured.  kDefaultFootprint only when both are under kMinFreed.
// * stopWanted turns off once the headroom has stayed at least
//   footprint * 1.25 + margin() for kOffMs, and the back-off has passed.
// * Back-off: a stop within kRelapseMs of the last reload waits
//   kBackoffFirstMs, then twice as long each time, at most kBackoffMaxMs; a
//   stop after kResetMs without one starts over.
// * Time is the caller's monotonic milliseconds (VramSampler: steady_clock).
//   A step between two samples outside 0..kMaxStepMs (2 s: a clock that
//   jumped, a worker that stalled) counts as kMaxStepMs, so no wait is cut
//   short or stretched by it.
//
// VramDevice and chooseDevice(): which GPU to watch, also without Vulkan
// types, for the tests.

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace wekde
{

struct VramSample {
    std::int64_t  tMs { 0 };
    std::uint64_t usage { 0 };
    std::uint64_t budget { 0 };
    std::uint64_t heapSize { 0 };
    std::uint64_t headroom() const { return budget > usage ? budget - usage : 0; }
};

class VramPolicy {
public:
    static constexpr std::uint64_t MiB               = 1024ull * 1024ull;
    static constexpr std::int64_t  kOnMs             = 3'000;
    static constexpr std::int64_t  kOffMs            = 10'000;
    static constexpr std::int64_t  kFootprintMs      = 5'000;
    static constexpr std::uint64_t kDefaultFootprint = 512 * MiB;
    static constexpr std::uint64_t kMinFreed         = 32 * MiB;
    static constexpr std::int64_t  kRelapseMs        = 120'000;
    static constexpr std::int64_t  kBackoffFirstMs   = 60'000;
    static constexpr std::int64_t  kBackoffMaxMs     = 15 * 60'000;
    static constexpr std::int64_t  kResetMs          = 10 * 60'000;
    static constexpr std::int64_t  kMaxStepMs        = 2'000;   // two samples' worth
    static constexpr std::int64_t  kUnset            = std::numeric_limits<std::int64_t>::min();

    // Too little free: below this for kOnMs is a stop.
    static std::uint64_t floor(std::uint64_t heap) { return std::max<std::uint64_t>(256 * MiB, heap / 25); }
    // Kept free on top of the footprint before a reload.
    static std::uint64_t margin(std::uint64_t heap) { return std::max<std::uint64_t>(512 * MiB, heap / 12); }

    // Feed one sample; returns true when stopWanted changed.
    bool feed(const VramSample& s) {
        if (s.budget == 0 || s.heapSize == 0) return false;          // the driver said nothing usable
        if (m_haveLast) {
            const std::int64_t step = s.tMs - m_last.tMs;
            const std::int64_t keep = std::clamp<std::int64_t>(step, 0, kMaxStepMs);
            if (step != keep) shift(step - keep);
        }
        m_last     = s;
        m_haveLast = true;
        if (! m_stop) {
            if (s.headroom() >= floor(s.heapSize)) {
                m_lowSince = kUnset;
                return false;
            }
            if (m_lowSince == kUnset) m_lowSince = s.tMs;
            if (s.tMs - m_lowSince < kOnMs) return false;
            startStop(s);
            return true;
        }
        if (! m_footprintDone && s.tMs - m_stopAt >= kFootprintMs) {
            const std::uint64_t freed  = m_usageAtStop > s.usage ? m_usageAtStop - s.usage : 0;
            const std::uint64_t gained = s.headroom() > m_headroomAtStop ? s.headroom() - m_headroomAtStop : 0;
            const std::uint64_t seen   = std::max(freed, gained);
            m_footprint                = seen >= kMinFreed ? seen : kDefaultFootprint;
            m_footprintDone            = true;
        }
        if (! m_footprintDone || s.headroom() < required(s.heapSize)) {
            m_highSince = kUnset;
            return false;
        }
        if (m_highSince == kUnset) m_highSince = s.tMs;
        if (s.tMs - m_highSince < kOffMs || s.tMs - m_stopAt < m_backoffMs) return false;
        m_stop       = false;
        m_lowSince   = kUnset;
        m_highSince  = kUnset;
        m_reloadedAt = s.tMs;
        return true;
    }

    bool          stopWanted() const { return m_stop; }
    std::uint64_t footprint() const { return m_footprint; }
    std::int64_t  backoffMs() const { return m_backoffMs; }
    // Free memory a reload waits for (footprint() * 1.25 + margin).
    std::uint64_t required(std::uint64_t heap) const { return m_footprint + m_footprint / 4 + margin(heap); }
    // Too little room to keep two wallpapers alive at once (a handoff):
    // a stop is wanted, or the headroom is below one more footprint and the floor.
    bool tight() const {
        if (m_stop) return true;
        if (! m_haveLast) return false;
        return m_last.headroom() < m_footprint + floor(m_last.heapSize);
    }
    const VramSample& last() const { return m_last; }

private:
    void startStop(const VramSample& s) {
        if (m_reloadedAt != kUnset && s.tMs - m_reloadedAt <= kRelapseMs)
            m_backoffMs = m_backoffMs ? std::min(m_backoffMs * 2, kBackoffMaxMs) : kBackoffFirstMs;
        else if (m_reloadedAt == kUnset || s.tMs - m_reloadedAt >= kResetMs)
            m_backoffMs = 0;
        m_stop          = true;
        m_stopAt         = s.tMs;
        m_usageAtStop    = s.usage;
        m_headroomAtStop = s.headroom();
        m_footprintDone = false;
        m_highSince     = kUnset;
    }

    // Move every remembered time by `d` (a jump in the caller's clock).
    void shift(std::int64_t d) {
        if (m_lowSince != kUnset) m_lowSince += d;
        if (m_highSince != kUnset) m_highSince += d;
        if (m_reloadedAt != kUnset) m_reloadedAt += d;
        m_stopAt += d;
    }

    VramSample    m_last;
    bool          m_haveLast { false };
    bool          m_stop { false };
    std::int64_t  m_lowSince { kUnset };
    std::int64_t  m_highSince { kUnset };
    std::int64_t  m_stopAt { 0 };
    std::int64_t  m_reloadedAt { kUnset };
    std::int64_t  m_backoffMs { 0 };
    std::uint64_t m_usageAtStop { 0 };
    std::uint64_t m_headroomAtStop { 0 };
    std::uint64_t m_footprint { kDefaultFootprint };
    bool          m_footprintDone { true };
};

// One GPU as Vulkan describes it, without Vulkan's types.
struct VramDevice {
    std::array<std::uint8_t, 16> uuid {};
    std::uint32_t                apiVersion { 0 };
    int                          type { 0 };            // VkPhysicalDeviceType
    bool                         havePci { false };     // VK_EXT_pci_bus_info answered
    std::uint32_t                pciDomain { 0 }, pciBus { 0 }, pciDevice { 0 }, pciFunction { 0 };
};
struct PciAddress {
    std::uint32_t domain { 0 }, bus { 0 }, device { 0 }, function { 0 };
};

// The GPU to watch, an index into `devs`, or -1: the one a scene renders on
// (`sceneUuid`, when one has said), else the one that drives the outputs
// (`bootVga`, sysfs boot_vga: on an Optimus laptop the integrated GPU, so
// the discrete one is not kept awake by a query a second), else a discrete
// one, else the first.  Software devices (type CPU) and devices below
// Vulkan 1.1 (no vkGetPhysicalDeviceMemoryProperties2) are never picked.
inline int chooseDevice(const std::vector<VramDevice>& devs, const std::uint8_t* sceneUuid,
                        const std::vector<PciAddress>& bootVga) {
    constexpr int           kTypeDiscrete = 2;
    constexpr int           kTypeCpu      = 4;
    constexpr std::uint32_t kApi11        = (1u << 22) | (1u << 12);
    auto usable = [&](const VramDevice& d) { return d.type != kTypeCpu && d.apiVersion >= kApi11; };
    if (sceneUuid)
        for (std::size_t i = 0; i < devs.size(); ++i)
            if (usable(devs[i]) && std::equal(devs[i].uuid.begin(), devs[i].uuid.end(), sceneUuid))
                return static_cast<int>(i);
    for (const PciAddress& a : bootVga)
        for (std::size_t i = 0; i < devs.size(); ++i) {
            const VramDevice& d = devs[i];
            if (usable(d) && d.havePci && d.pciDomain == a.domain && d.pciBus == a.bus && d.pciDevice == a.device
                && d.pciFunction == a.function)
                return static_cast<int>(i);
        }
    for (std::size_t i = 0; i < devs.size(); ++i)
        if (usable(devs[i]) && devs[i].type == kTypeDiscrete) return static_cast<int>(i);
    for (std::size_t i = 0; i < devs.size(); ++i)
        if (usable(devs[i])) return static_cast<int>(i);
    return -1;
}

} // namespace wekde
