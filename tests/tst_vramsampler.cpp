// SPDX-License-Identifier: GPL-2.0-only
// Unit tests for wekde::VramSampler (stop on low video memory, we-manager
// vram patch) without a GPU: when no Vulkan driver answers, the sampler says
// so, names the call, and never asks for a stop.  The numbers from a real
// driver are not testable here.

#include <QtTest>

#include "VramSampler.hpp"

using namespace wekde;

class TestVramSampler : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // No driver at all, whatever this machine has: the loader finds none.
        qputenv("VK_ICD_FILENAMES", "/nonexistent/no-driver.json");
        qputenv("VK_DRIVER_FILES", "/nonexistent/no-driver.json");
    }
    void offUntilEnabled();
    void noDriverIsNotAvailableAndNamesTheCall();
    void everyScreenSeesTheOneMonitor();
    void offReturnsAtOnceAndOnAgainWakesTheWorker();
    // Last: after it the monitor is gone for this process, as at an exit.
    void theApplicationGoingStopsThePostsAndDoesNotHang();
};

void TestVramSampler::offUntilEnabled() {
    VramSampler s;
    QVERIFY(! s.enabled());
    QVERIFY(! s.available());
    QVERIFY(! s.stopWanted());
    QCOMPARE(s.reason(), QStringLiteral("VramStop is off on this screen"));
}

void TestVramSampler::noDriverIsNotAvailableAndNamesTheCall() {
    VramSampler s;
    QSignalSpy  spy(&s, &VramSampler::changed);
    s.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(VramSampler::processStatus().value(QStringLiteral("running")).toBool(), 5000);
    QVERIFY(! s.available());
    QVERIFY(! s.stopWanted());
    QVERIFY(! s.tight());
    const QString why = s.reason();
    QVERIFY2(why.contains(QStringLiteral("vkCreateInstance")) || why.contains(QStringLiteral("dlopen"))
                 || why.contains(QStringLiteral("no GPU")) || why.contains(QStringLiteral("llvmpipe")),
             qPrintable(why));
    QVERIFY(spy.count() >= 1);
    const QJsonObject o = QJsonDocument::fromJson(s.statusJson().toUtf8()).object();
    QCOMPARE(o.value(QStringLiteral("available")).toBool(true), false);
    QCOMPARE(o.value(QStringLiteral("reason")).toString(), why);
    QCOMPARE(o.value(QStringLiteral("stopWanted")).toBool(true), false);
    s.setEnabled(false);
    QCOMPARE(VramSampler::processStatus().value(QStringLiteral("running")).toBool(), false);
}

void TestVramSampler::everyScreenSeesTheOneMonitor() {
    VramSampler a, b;
    a.setEnabled(true);
    b.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(VramSampler::processStatus().value(QStringLiteral("running")).toBool(), 5000);
    QCOMPARE(a.reason(), b.reason());
    a.setEnabled(false);
    QVERIFY2(VramSampler::processStatus().value(QStringLiteral("running")).toBool(),
             "one screen turning VramStop off must not stop the others' monitor");
    b.setEnabled(false);
    QVERIFY(! VramSampler::processStatus().value(QStringLiteral("running")).toBool());
}

// Turning VramStop off never waits for the worker (it may be inside a slow
// driver call): it is told and goes idle; turning it on again wakes it.
void TestVramSampler::offReturnsAtOnceAndOnAgainWakesTheWorker() {
    for (int round = 0; round < 3; ++round) {
        VramSampler s;
        s.setEnabled(true);
        QElapsedTimer timer;
        timer.start();
        s.setEnabled(false);                         // at once, also while the worker is mid-sample
        QVERIFY2(timer.elapsed() < 50, qPrintable(QStringLiteral("off took %1 ms").arg(timer.elapsed())));
        QVERIFY(! VramSampler::processStatus().value(QStringLiteral("running")).toBool());
        QTest::qWait(30);
        QVERIFY2(! VramSampler::processStatus().value(QStringLiteral("running")).toBool(),
                 "a sample taken before the off must not be published after it");
        s.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(VramSampler::processStatus().value(QStringLiteral("running")).toBool(), 5000);
        s.setEnabled(false);
    }
}

// plasmashell destroying its application while a screen still has VramStop
// on: the routine ~QCoreApplication runs turns the worker off under the lock
// it posts under, so no change is posted after it, it returns within about
// 100 ms, and nothing starts again.
void TestVramSampler::theApplicationGoingStopsThePostsAndDoesNotHang() {
    VramSampler s;
    QSignalSpy  spy(&s, &VramSampler::changed);
    s.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(VramSampler::processStatus().value(QStringLiteral("running")).toBool(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 2, 5000);              // the worker posts its samples
    QElapsedTimer timer;
    timer.start();
    VramSampler::shutdownForExit();
    QVERIFY2(timer.elapsed() < 500, qPrintable(QStringLiteral("the exit routine took %1 ms").arg(timer.elapsed())));
    QCoreApplication::processEvents();                              // whatever was posted before it
    const int before = spy.count();
    QTest::qWait(2500);                                             // more than two sample periods
    QCOMPARE(spy.count(), before);
    QVERIFY(! VramSampler::processStatus().value(QStringLiteral("running")).toBool());
    // The screens going one by one, and another one turning VramStop on,
    // would wake the worker, which samples at once and posts: the
    // application is going, so nothing starts.
    s.setEnabled(false);
    VramSampler other;
    QSignalSpy  otherSpy(&other, &VramSampler::changed);
    other.setEnabled(true);
    QCOMPARE(otherSpy.count(), 1);                                  // setEnabled's own, nothing from the worker
    QTest::qWait(1500);
    QCOMPARE(otherSpy.count(), 1);
    QVERIFY(! VramSampler::processStatus().value(QStringLiteral("running")).toBool());
}

QTEST_GUILESS_MAIN(TestVramSampler)
#include "tst_vramsampler.moc"
