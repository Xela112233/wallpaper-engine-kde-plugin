// SPDX-License-Identifier: GPL-2.0-only
// Unit tests for wekde::VramPolicy (stop on low video memory, we-manager
// vram patch): when the wallpapers stop for want of video memory, when they
// may load again, and the back-off after a stop that comes straight back.

#include <QtTest>

#include "VramPolicy.hpp"

using namespace wekde;

namespace
{
constexpr std::uint64_t MiB  = VramPolicy::MiB;
constexpr std::uint64_t Heap = 8192 * MiB;          // an 8 GiB card: floor 327 MiB, margin 682 MiB

// One sample a second from `from` for `seconds`, with `freeMiB` free and
// `usageMiB` used by this process.
bool run(VramPolicy& p, std::int64_t& t, int seconds, std::uint64_t usageMiB, std::uint64_t freeMiB) {
    bool changed = false;
    for (int i = 0; i < seconds; ++i, t += 1000)
        changed |= p.feed({ t, usageMiB * MiB, (usageMiB + freeMiB) * MiB, Heap });
    return changed;
}
} // namespace

class TestVramPolicy : public QObject {
    Q_OBJECT

private slots:
    void aShortDipDoesNotStop();
    void lowHeadroomForThreeSecondsStops();
    void theRatioDoesNotMatterOnlyWhatIsFree();
    void aReloadWaitsForTheFootprintAndTheMargin();
    void theFootprintIsWhatTheStopGaveBack();
    void aStopRightAfterAReloadBacksOffAndDoubles();
    void theBackoffStartsOverAfterAQuietSpell();
    void tightWhenTwoWallpapersWouldNotFit();
    void anEmptySampleIsIgnored();
    void aWallpaperTheUsageCannotSeeIsMeasuredByTheHeadroom();
    void aClockJumpNeitherCutsNorStretchesAWait();
    void theDeviceIsTheScenesThenTheOutputsThenTheDiscreteOne();
    void softwareAndPreVulkan11DevicesAreNeverPicked();
};

void TestVramPolicy::aShortDipDoesNotStop() {
    VramPolicy   p;
    std::int64_t t = 0;
    QVERIFY(! run(p, t, 5, 300, 4000));
    QVERIFY(! run(p, t, 2, 300, 100));               // two seconds below the floor
    QVERIFY(! run(p, t, 3, 300, 4000));
    QVERIFY(! p.stopWanted());
}

void TestVramPolicy::lowHeadroomForThreeSecondsStops() {
    VramPolicy   p;
    std::int64_t t = 0;
    run(p, t, 3, 300, 4000);
    QVERIFY(run(p, t, 4, 300, 100));                 // the 4th second below the floor is 3 s on
    QVERIFY(p.stopWanted());
    QCOMPARE(p.backoffMs(), 0);
}

void TestVramPolicy::theRatioDoesNotMatterOnlyWhatIsFree() {
    // 150 MiB of ours and 50 MiB free is a 75 % "usage" ratio and a full card.
    VramPolicy   p;
    std::int64_t t = 0;
    run(p, t, 4, 150, 50);
    QVERIFY(p.stopWanted());
    // A big wallpaper on an empty card is 95 % of our budget only if little is free: here plenty is.
    VramPolicy q;
    t = 0;
    run(q, t, 10, 6000, 1500);
    QVERIFY(! q.stopWanted());
}

void TestVramPolicy::aReloadWaitsForTheFootprintAndTheMargin() {
    VramPolicy   p;
    std::int64_t t = 0;
    run(p, t, 4, 400, 100);                          // stop: 400 MiB of ours
    QVERIFY(p.stopWanted());
    run(p, t, 6, 100, 400);                          // the stop gave 300 MiB back
    QCOMPARE(p.footprint(), 300 * MiB);
    const std::uint64_t need = p.required(Heap) / MiB;   // 300 * 1.25 + 682
    QVERIFY(need > 1000 && need < 1100);
    QVERIFY(! run(p, t, 30, 100, need - 10));        // just short: never
    QVERIFY(p.stopWanted());
    QVERIFY(! run(p, t, 9, 100, need + 10));         // enough, for 9 s
    QVERIFY(run(p, t, 2, 100, need + 10));           // the 10th second
    QVERIFY(! p.stopWanted());
}

void TestVramPolicy::theFootprintIsWhatTheStopGaveBack() {
    // A driver that does not count the whole process: nothing seems freed,
    // so the reload waits for the default footprint.
    VramPolicy   p;
    std::int64_t t = 0;
    run(p, t, 4, 400, 100);
    run(p, t, 6, 400, 100);
    QCOMPARE(p.footprint(), VramPolicy::kDefaultFootprint);
}

void TestVramPolicy::aStopRightAfterAReloadBacksOffAndDoubles() {
    VramPolicy   p;
    std::int64_t t = 0;
    auto cycle = [&](int quietSeconds) {
        run(p, t, 4, 400, 100);                      // stop
        run(p, t, 6, 100, 400);
        const std::uint64_t need = p.required(Heap) / MiB + 10;
        int waited = 0;
        while (p.stopWanted() && waited < 2000) {
            run(p, t, 1, 100, need);
            ++waited;
        }
        run(p, t, quietSeconds, 400, 4000);          // reloaded; memory fine for a while
        return waited;
    };
    QCOMPARE(cycle(30), 11);                         // the first: the 10 s hold only (11 samples a second apart)
    QCOMPARE(p.backoffMs(), 0);
    const int second = cycle(30);                    // back within 2 min of the reload
    QCOMPARE(p.backoffMs(), VramPolicy::kBackoffFirstMs);
    QCOMPARE(second, 54);                            // the back-off runs from the stop: 60 s after it
    cycle(30);
    QCOMPARE(p.backoffMs(), 2 * VramPolicy::kBackoffFirstMs);
    for (int i = 0; i < 6; ++i) cycle(30);
    QCOMPARE(p.backoffMs(), VramPolicy::kBackoffMaxMs);
}

void TestVramPolicy::theBackoffStartsOverAfterAQuietSpell() {
    VramPolicy   p;
    std::int64_t t = 0;
    for (int i = 0; i < 3; ++i) {
        run(p, t, 4, 400, 100);
        run(p, t, 6, 100, 400);
        while (p.stopWanted()) run(p, t, 1, 100, p.required(Heap) / MiB + 10);
        run(p, t, 20, 400, 4000);
    }
    QVERIFY(p.backoffMs() > 0);
    run(p, t, 11 * 60, 400, 4000);                   // eleven quiet minutes
    run(p, t, 4, 400, 100);
    QVERIFY(p.stopWanted());
    QCOMPARE(p.backoffMs(), 0);
}

void TestVramPolicy::tightWhenTwoWallpapersWouldNotFit() {
    VramPolicy   p;
    std::int64_t t = 0;
    run(p, t, 2, 300, 4000);
    QVERIFY(! p.tight());
    run(p, t, 1, 300, 600);                          // above the floor, below footprint (512) + floor (327)
    QVERIFY(! p.stopWanted());
    QVERIFY(p.tight());
}

void TestVramPolicy::anEmptySampleIsIgnored() {
    VramPolicy p;
    QVERIFY(! p.feed({ 0, 0, 0, 0 }));
    QVERIFY(! p.feed({ 5000, 100 * MiB, 0, Heap }));
    QVERIFY(! p.stopWanted());
    QVERIFY(! p.tight());
}

// The review's probe: an 8 GiB card, a game that leaves 200 MiB free, and
// a 1.5 GiB wallpaper whose memory the driver does not put on this process
// (mpv's GL context, a web wallpaper's GPU process): the usage never moves.
// Its stop frees 1.5 GiB of headroom, which is the footprint, so it waits
// for the game instead of reloading into the shortage every few minutes.
void TestVramPolicy::aWallpaperTheUsageCannotSeeIsMeasuredByTheHeadroom() {
    VramPolicy          p;
    const std::uint64_t seen = 100, unseen = 1536, heap = 8192;
    const std::uint64_t game = heap - 200 - unseen - seen;
    bool                loaded = true;
    int                 stops = 0, reloads = 0;
    for (std::int64_t t = 0; t < 3600 * 1000; t += 1000) {
        const std::uint64_t free = heap - game - seen - (loaded ? unseen : 0);
        p.feed({ t, seen * MiB, (seen + free) * MiB, heap * MiB });
        if (p.stopWanted() && loaded) {             // main.qml unloads at once
            loaded = false;
            ++stops;
        } else if (! p.stopWanted() && ! loaded) {
            loaded = true;
            ++reloads;
        }
    }
    QCOMPARE(stops, 1);
    QVERIFY2(reloads <= 1, qPrintable(QStringLiteral("%1 reloads into the shortage in an hour").arg(reloads)));
    QCOMPARE(reloads, 0);
    QCOMPARE(p.footprint(), unseen * MiB);
    // The game ends: everything is free again, and it loads.
    std::int64_t t = 3600 * 1000;
    run(p, t, 15, seen, heap - seen - 200);
    QVERIFY(! p.stopWanted());
}

// The caller's clock (steady_clock in VramSampler) jumping anyway, or a
// worker stalled for a while: a step outside 0..2 s counts as 2 s.
void TestVramPolicy::aClockJumpNeitherCutsNorStretchesAWait() {
    VramPolicy   p;
    std::int64_t t = 0;
    run(p, t, 2, 300, 100);                          // two seconds short of memory
    t -= 3'600'000;                                  // set back an hour
    QVERIFY(! run(p, t, 2, 300, 100));               // the sample at the jump counts as no time passed ...
    QVERIFY(run(p, t, 1, 300, 100));                 // ... so it stops one sample late, not an hour late
    QVERIFY(p.stopWanted());
    VramPolicy   q;
    std::int64_t u = 0;
    run(q, u, 1, 300, 100);
    u += 3'600'000;                                  // forward an hour
    QVERIFY(! run(q, u, 1, 300, 100));               // counts as 2 s: two low samples are no 3 s ...
    VramPolicy   r;
    std::int64_t v = 0;
    run(r, v, 1, 300, 4000);
    v += 3'600'000;
    QVERIFY(! run(r, v, 1, 300, 100));               // ... nor is a first low sample after a jump
    QVERIFY(! r.stopWanted());
}

void TestVramPolicy::theDeviceIsTheScenesThenTheOutputsThenTheDiscreteOne() {
    constexpr std::uint32_t api13 = (1u << 22) | (3u << 12);
    VramDevice igpu, dgpu;
    igpu.type = 1;                                   // integrated
    igpu.apiVersion = api13;
    igpu.uuid.fill(0x11);
    igpu.havePci = true;
    igpu.pciBus  = 0x06;
    dgpu.type = 2;                                   // discrete
    dgpu.apiVersion = api13;
    dgpu.uuid.fill(0x22);
    dgpu.havePci = true;
    dgpu.pciBus  = 0x01;
    const std::vector<VramDevice> devs { dgpu, igpu };
    // An Optimus laptop: the integrated GPU drives the panel (boot_vga), so
    // the discrete one is left asleep until a scene says it renders there.
    QCOMPARE(chooseDevice(devs, nullptr, { PciAddress { 0, 0x06, 0, 0 } }), 1);
    QCOMPARE(chooseDevice(devs, dgpu.uuid.data(), { PciAddress { 0, 0x06, 0, 0 } }), 0);
    // A desktop whose monitors are on the discrete card.
    QCOMPARE(chooseDevice(devs, nullptr, { PciAddress { 0, 0x01, 0, 0 } }), 0);
    // No boot_vga and no PCI information: the discrete one.
    VramDevice a = igpu, b = dgpu;
    a.havePci = b.havePci = false;
    QCOMPARE(chooseDevice({ a, b }, nullptr, {}), 1);
    QCOMPARE(chooseDevice({ a }, nullptr, {}), 0);
}

void TestVramPolicy::softwareAndPreVulkan11DevicesAreNeverPicked() {
    VramDevice llvmpipe, old;
    llvmpipe.type       = 4;                         // CPU
    llvmpipe.apiVersion = (1u << 22) | (3u << 12);
    old.type            = 2;
    old.apiVersion      = (1u << 22);                // Vulkan 1.0
    QCOMPARE(chooseDevice({ llvmpipe, old }, nullptr, {}), -1);
    QCOMPARE(chooseDevice({ llvmpipe, old }, old.uuid.data(), {}), -1);
}

QTEST_GUILESS_MAIN(TestVramPolicy)
#include "tst_vrampolicy.moc"
