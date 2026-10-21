// Unit tests for wekde::TTYSwitchMonitor — the suspend/wake signal bridge that
// drives the renderer's pause-on-suspend behaviour. The D-Bus connect in the
// ctor depends on a system bus that isn't reachable from the test harness, but
// the slot itself is plain Qt code: it serialises bool changes through the
// `ttySwitch(bool)` signal and dedupes same-state calls.

#include "TTYSwitchMonitor.hpp"
#include <QSignalSpy>
#include <QtTest/QtTest>

using wekde::TTYSwitchMonitor;

class TestTTYSwitchMonitor : public QObject {
    Q_OBJECT

private slots:
    void initialState_isAwake();
    void handlePrepareForSleep_trueFlipsAndEmits();
    void handlePrepareForSleep_falseFlipsBackAndEmits();
    void handlePrepareForSleep_sameStateIsIdempotent();
    void handlePrepareForSleep_emitsCarryNewValue();
    void sleepingProperty_reflectsLastDispatchedState();
    void pmfConnect_signatureCompilesWithMatchingSlot();
    // Ctor-injection seam mirrors the F-7 ScreenSaverMonitor pattern. The
    // distrobox harness lacks dbus-launch/dbus-run-session, so the wireUp
    // code path is otherwise unreachable under ctest. These two cases
    // pin the overload signature and prove the ctor doesn't crash on
    // either bus state — the slot-direct path remains exercised by the
    // existing handlePrepareForSleep_* cases.
    void ctor_injectable_disconnectedBus_doesNotCrash();
    void ctor_injectable_systemBus_doesNotCrash();
    void handleSessionActiveChanged_falseTriggersPause();
    void handleSessionActiveChanged_trueRestoresAfterFalse();
    void handleSessionActiveChanged_sameStateIsIdempotent();
    void sessionActiveAndPrepareForSleep_combineWithOrSemantics();
    void sessionPropertiesChangedConnect_signatureCompilesWithMatchingSlot();
    void handleSessionPropertiesChanged_filtersInterfaceAndExtractsActive();
    // we-manager resume-reload patch: a resume, told apart from a VT switch.
    void aWakeAfterPrepareForSleepIsAResumeWithItsKindAndTime();
    void aWakeWithoutASleepAndAVtSwitchAreNoResume();
    void theSuspendedTimeCatchesASleepLogindDidNotAnnounce();
    void bothSourcesForOneSleepAreOneResume();
    void aSleepOnlyTheClockSawSoonAfterLogindsIsASecondResume();
    void anAbortedSuspendIsNamedSo();
    void aPrepareForSleepHandledAfterTheWakeIsOneResumeOfTheWholeSleep();
};

namespace
{
// The clocks a test drives: suspended time, suspend count.
struct Clocks {
    qint64 suspended { 0 };
    qint64 count { 5 };
    void   attach(TTYSwitchMonitor& m) {
        m.setClocksForTest([this] { return suspended; }, [this] { return count; });
    }
};
} // namespace

void TestTTYSwitchMonitor::aWakeAfterPrepareForSleepIsAResumeWithItsKindAndTime() {
    TTYSwitchMonitor mon(QDBusConnection(QStringLiteral("none")), nullptr);
    Clocks           c;
    c.attach(mon);
    QSignalSpy spy(&mon, &TTYSwitchMonitor::resumedFromSleep);
    mon.handlePrepareForSleep(true);
    c.suspended += 2 * 3600 * 1000;                   // two hours in suspend
    c.count += 1;
    mon.handlePrepareForSleep(false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("suspend"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("logind"));
    QCOMPARE(spy.at(0).at(2).toLongLong(), 2LL * 3600 * 1000);
    QCOMPARE(mon.isSleeping(), false);                // the pause chain hears the wake as before
    // A hibernation: the suspend count stays.
    mon.handlePrepareForSleep(true);
    c.suspended += 600'000;
    mon.handlePrepareForSleep(false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("hibernation"));
    // The count cannot be read: "sleep".
    c.count = -1;
    mon.handlePrepareForSleep(true);
    c.suspended += 60'000;
    mon.handlePrepareForSleep(false);
    QCOMPARE(spy.at(2).at(0).toString(), QStringLiteral("sleep"));
}

void TestTTYSwitchMonitor::aWakeWithoutASleepAndAVtSwitchAreNoResume() {
    TTYSwitchMonitor mon(QDBusConnection(QStringLiteral("none")), nullptr);
    Clocks           c;
    c.attach(mon);
    QSignalSpy spy(&mon, &TTYSwitchMonitor::resumedFromSleep);
    QSignalSpy tty(&mon, &TTYSwitchMonitor::ttySwitch);
    mon.handlePrepareForSleep(false);                 // made mid-sleep, or a repeated signal
    mon.handleSessionActiveChanged(false);            // Ctrl+Alt+F3 away
    mon.handleSessionActiveChanged(true);             // and back
    mon.checkSuspendedTime();                         // the machine never slept
    QCOMPARE(spy.count(), 0);
    QCOMPARE(tty.count(), 2);
}

void TestTTYSwitchMonitor::theSuspendedTimeCatchesASleepLogindDidNotAnnounce() {
    TTYSwitchMonitor mon(QDBusConnection(QStringLiteral("none")), nullptr);
    Clocks           c;
    c.attach(mon);
    QSignalSpy spy(&mon, &TTYSwitchMonitor::resumedFromSleep);
    c.suspended += 1500;                              // below kMinAsleepMs: not a sleep
    mon.checkSuspendedTime();
    QCOMPARE(spy.count(), 0);
    c.suspended += 45 * 60'000;
    c.count += 1;
    mon.checkSuspendedTime();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("suspend"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("clock"));
    QCOMPARE(spy.at(0).at(2).toLongLong(), 45LL * 60'000);
    mon.checkSuspendedTime();                         // the same sleep is not seen twice
    QCOMPARE(spy.count(), 1);
}

void TestTTYSwitchMonitor::bothSourcesForOneSleepAreOneResume() {
    TTYSwitchMonitor mon(QDBusConnection(QStringLiteral("none")), nullptr);
    Clocks           c;
    c.attach(mon);
    QSignalSpy spy(&mon, &TTYSwitchMonitor::resumedFromSleep);
    // logind first: the clock's next look sees nothing more.
    mon.handlePrepareForSleep(true);
    c.suspended += 600'000;
    mon.handlePrepareForSleep(false);
    mon.checkSuspendedTime();
    QCOMPARE(spy.count(), 1);
    // The clock first, logind's wake 20 s later: one resume.
    mon.handlePrepareForSleep(true);
    c.suspended += 600'000;
    mon.checkSuspendedTime();
    mon.handlePrepareForSleep(false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(1).toString(), QStringLiteral("clock"));
    // Another sleep a little later is a resume of its own.
    mon.handlePrepareForSleep(true);
    c.suspended += 600'000;
    mon.handlePrepareForSleep(false);
    QCOMPARE(spy.count(), 3);
}

// The review's case: logind announces a wake, and 30 s later a sleep logind
// did not announce is seen by the clock.  Two sleeps, two resumes.
void TestTTYSwitchMonitor::aSleepOnlyTheClockSawSoonAfterLogindsIsASecondResume() {
    TTYSwitchMonitor mon(QDBusConnection(QStringLiteral("none")), nullptr);
    Clocks           c;
    c.attach(mon);
    QSignalSpy spy(&mon, &TTYSwitchMonitor::resumedFromSleep);
    mon.handlePrepareForSleep(true);
    c.suspended += 600'000;
    c.count += 1;
    mon.handlePrepareForSleep(false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("logind"));
    mon.checkSuspendedTime();                         // 15 s on: logind's sleep is not seen again
    QCOMPARE(spy.count(), 1);
    c.suspended += 300'000;                           // 30 s after the wake, a sleep logind missed
    c.count += 1;
    mon.checkSuspendedTime();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("suspend"));
    QCOMPARE(spy.at(1).at(1).toString(), QStringLiteral("clock"));
    QCOMPARE(spy.at(1).at(2).toLongLong(), 300'000LL);
    // And the other way round: the clock's sleep, then logind's own one.
    mon.handlePrepareForSleep(true);
    c.suspended += 120'000;
    c.count += 1;
    mon.handlePrepareForSleep(false);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(2).at(1).toString(), QStringLiteral("logind"));
    QCOMPARE(spy.at(2).at(2).toLongLong(), 120'000LL);
}

// A suspend the kernel refused, or one a wake event cut short: logind sends
// the wake, but the machine spent (almost) no time suspended and the suspend
// count did not rise.  Not a hibernation.
void TestTTYSwitchMonitor::anAbortedSuspendIsNamedSo() {
    TTYSwitchMonitor mon(QDBusConnection(QStringLiteral("none")), nullptr);
    Clocks           c;
    c.attach(mon);
    QSignalSpy spy(&mon, &TTYSwitchMonitor::resumedFromSleep);
    mon.handlePrepareForSleep(true);
    mon.handlePrepareForSleep(false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("aborted"));
    QCOMPARE(spy.at(0).at(2).toLongLong(), 0LL);
    mon.handlePrepareForSleep(true);
    c.suspended += 800;                               // cut short under a second in
    c.count = -1;                                     // and with the count unreadable
    mon.handlePrepareForSleep(false);
    QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("aborted"));
    // A short suspend that did happen is still a suspend.
    c.count = 7;
    mon.checkSuspendedTime();                         // the count readable again at a look
    mon.handlePrepareForSleep(true);
    c.suspended += 900;
    c.count = 8;
    mon.handlePrepareForSleep(false);
    QCOMPARE(spy.at(2).at(0).toString(), QStringLiteral("suspend"));
}

// systemd 256 and later freeze the user session for a sleep: a GUI thread
// that had not handled PrepareForSleep(true) before the freeze gets the true
// and the false back to back after the wake.  An hour's suspend, in either
// order against the clock's look, is one resume of an hour, a suspend.
void TestTTYSwitchMonitor::aPrepareForSleepHandledAfterTheWakeIsOneResumeOfTheWholeSleep() {
    {
        TTYSwitchMonitor mon(QDBusConnection(QStringLiteral("none")), nullptr);
        Clocks           c;
        c.attach(mon);
        mon.checkSuspendedTime();                     // a look before the sleep
        QSignalSpy spy(&mon, &TTYSwitchMonitor::resumedFromSleep);
        c.suspended += 3'600'000;                     // asleep, the true still queued
        c.count += 1;
        mon.handlePrepareForSleep(true);              // after the wake
        mon.handlePrepareForSleep(false);
        mon.checkSuspendedTime();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("suspend"));
        QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("logind"));
        QCOMPARE(spy.at(0).at(2).toLongLong(), 3'600'000LL);
    }
    {
        TTYSwitchMonitor mon(QDBusConnection(QStringLiteral("none")), nullptr);
        Clocks           c;
        c.attach(mon);
        QSignalSpy spy(&mon, &TTYSwitchMonitor::resumedFromSleep);
        c.suspended += 3'600'000;
        c.count += 1;
        mon.checkSuspendedTime();                     // the clock's look comes first
        mon.handlePrepareForSleep(true);
        mon.handlePrepareForSleep(false);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("suspend"));
        QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("clock"));
        QCOMPARE(spy.at(0).at(2).toLongLong(), 3'600'000LL);
        // The next sleep, announced in time, is a resume of its own.
        mon.handlePrepareForSleep(true);
        c.suspended += 600'000;
        c.count += 1;
        mon.handlePrepareForSleep(false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(1).toString(), QStringLiteral("logind"));
        QCOMPARE(spy.at(1).at(2).toLongLong(), 600'000LL);
    }
}

void TestTTYSwitchMonitor::initialState_isAwake() {
    TTYSwitchMonitor mon;
    QCOMPARE(mon.isSleeping(), false);
}

void TestTTYSwitchMonitor::handlePrepareForSleep_trueFlipsAndEmits() {
    TTYSwitchMonitor mon;
    QSignalSpy       spy(&mon, &TTYSwitchMonitor::ttySwitch);
    mon.handlePrepareForSleep(true);
    QCOMPARE(mon.isSleeping(), true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
}

void TestTTYSwitchMonitor::handlePrepareForSleep_falseFlipsBackAndEmits() {
    TTYSwitchMonitor mon;
    QSignalSpy       spy(&mon, &TTYSwitchMonitor::ttySwitch);
    mon.handlePrepareForSleep(true);  // sleep
    mon.handlePrepareForSleep(false); // wake
    QCOMPARE(mon.isSleeping(), false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
}

void TestTTYSwitchMonitor::handlePrepareForSleep_sameStateIsIdempotent() {
    // The dedupe guard in updateSleeping() prevents duplicate emits when
    // systemd repeats PrepareForSleep — pause-on-suspend would otherwise
    // toggle the render state twice per real event.
    TTYSwitchMonitor mon;
    QSignalSpy       spy(&mon, &TTYSwitchMonitor::ttySwitch);
    mon.handlePrepareForSleep(false); // already false → no emit
    QCOMPARE(spy.count(), 0);
    mon.handlePrepareForSleep(true); // false→true → emit
    mon.handlePrepareForSleep(true); // true→true  → no emit
    mon.handlePrepareForSleep(true); // true→true  → no emit
    QCOMPARE(spy.count(), 1);
    QCOMPARE(mon.isSleeping(), true);
}

void TestTTYSwitchMonitor::handlePrepareForSleep_emitsCarryNewValue() {
    // Each emit must carry the NEW state — a regression that mis-routed the
    // arg (e.g., passing m_sleeping before the assignment) would still emit
    // the right count but break consumers reading the bool.
    TTYSwitchMonitor mon;
    QSignalSpy       spy(&mon, &TTYSwitchMonitor::ttySwitch);
    mon.handlePrepareForSleep(true);
    mon.handlePrepareForSleep(false);
    mon.handlePrepareForSleep(true);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
    QCOMPARE(spy.at(2).at(0).toBool(), true);
}

void TestTTYSwitchMonitor::sleepingProperty_reflectsLastDispatchedState() {
    // Q_PROPERTY(bool sleeping READ isSleeping NOTIFY ttySwitch). The
    // signal name doubles as the NOTIFY for the property, so QML bindings
    // re-evaluate on every state flip.
    TTYSwitchMonitor mon;
    QSignalSpy       spy(&mon, &TTYSwitchMonitor::ttySwitch);
    mon.handlePrepareForSleep(true);
    QCOMPARE(mon.property("sleeping").toBool(), true);
    mon.handlePrepareForSleep(false);
    QCOMPARE(mon.property("sleeping").toBool(), false);
    QCOMPARE(spy.count(), 2);
}

void TestTTYSwitchMonitor::pmfConnect_signatureCompilesWithMatchingSlot() {
    // Compile-time pin on handlePrepareForSleep's signature. The production
    // QDBusConnection::connect in TTYSwitchMonitor.cpp uses the legacy
    // SLOT(handlePrepareForSleep(bool)) string macro — Qt6's QDBusConnection
    // does not (as of 6.10.3) expose a pointer-to-member-function overload,
    // so the connect-site itself can't be made compile-checked. This test
    // case takes the next-best step: it forms a member pointer with the
    // exact `void (bool)` shape the SLOT macro string baked in. If a future
    // refactor renames the slot, changes its parameter type/arity, or drops
    // it entirely, this assignment fails to COMPILE in the test build —
    // surfacing the drift at gate time rather than at runtime via a silently-
    // returning-false connect() and a journal qWarning.
    using SlotPtr = void (TTYSwitchMonitor::*)(bool);
    SlotPtr p     = &TTYSwitchMonitor::handlePrepareForSleep;
    // QVERIFY keeps the compiler from optimising the pointer assignment
    // away. The *compile-time* check is the contract; this runtime
    // assertion is just liveness so the case shows up in QtTest output.
    QVERIFY(p != nullptr);
}

void TestTTYSwitchMonitor::ctor_injectable_disconnectedBus_doesNotCrash() {
    // Pass a default-constructed QDBusConnection (isConnected()==false).
    // The wireUp branch must early-out; the slot-direct path is still
    // exercised. Mirrors the F-7 ScreenSaverMonitor injection seam.
    QDBusConnection bus("wek-test-not-a-real-bus");
    QVERIFY(! bus.isConnected());
    TTYSwitchMonitor mon(bus, /*parent=*/nullptr);
    QSignalSpy       spy(&mon, &TTYSwitchMonitor::ttySwitch);
    mon.handlePrepareForSleep(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(mon.isSleeping(), true);
}

void TestTTYSwitchMonitor::ctor_injectable_systemBus_doesNotCrash() {
    // Probe the real system bus once. On distrobox it's unavailable; on a
    // host with login1 it's reachable. Either way, the ctor must succeed;
    // the wireUp branch is taken only on the connected path. This case
    // pins "the overload exists" + "ctor doesn't crash regardless of
    // bus availability".
    QDBusConnection  bus = QDBusConnection::systemBus();
    TTYSwitchMonitor mon(bus, /*parent=*/nullptr);
    QVERIFY(true); // ctor returned without crashing
}

void TestTTYSwitchMonitor::handleSessionActiveChanged_falseTriggersPause() {
    TTYSwitchMonitor mon;
    QSignalSpy       spy(&mon, &TTYSwitchMonitor::ttySwitch);
    mon.handleSessionActiveChanged(false); // simulated VT switch away
    QCOMPARE(mon.isSleeping(), true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
}

void TestTTYSwitchMonitor::handleSessionActiveChanged_trueRestoresAfterFalse() {
    TTYSwitchMonitor mon;
    QSignalSpy       spy(&mon, &TTYSwitchMonitor::ttySwitch);
    mon.handleSessionActiveChanged(false); // switch away
    mon.handleSessionActiveChanged(true);  // switch back
    QCOMPARE(mon.isSleeping(), false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
}

void TestTTYSwitchMonitor::handleSessionActiveChanged_sameStateIsIdempotent() {
    // m_sessionActive defaults to true (assume foregrounded until told otherwise);
    // calling true again must not emit.
    TTYSwitchMonitor mon;
    QSignalSpy       spy(&mon, &TTYSwitchMonitor::ttySwitch);
    mon.handleSessionActiveChanged(true); // already active -> no emit
    QCOMPARE(spy.count(), 0);
}

void TestTTYSwitchMonitor::sessionActiveAndPrepareForSleep_combineWithOrSemantics() {
    // The one case that actually exercises the merge -- fails under a naive
    // implementation that writes m_sleeping directly from either handler
    // instead of OR-ing two independent flags.
    TTYSwitchMonitor mon;
    QSignalSpy       spy(&mon, &TTYSwitchMonitor::ttySwitch);
    mon.handleSessionActiveChanged(false); // VT switch away -> pause (emit #1, true)
    mon.handlePrepareForSleep(true);       // also suspending -> still paused, no new edge
    QCOMPARE(mon.isSleeping(), true);
    QCOMPARE(spy.count(), 1);
    mon.handleSessionActiveChanged(true); // back on our VT, but system still asleep -> still paused
    QCOMPARE(mon.isSleeping(), true);
    QCOMPARE(spy.count(), 1);
    mon.handlePrepareForSleep(
        false); // actual wake, and our VT is active -> resume (emit #2, false)
    QCOMPARE(mon.isSleeping(), false);
    QCOMPARE(spy.count(), 2);
}

void TestTTYSwitchMonitor::sessionPropertiesChangedConnect_signatureCompilesWithMatchingSlot() {
    // Compile-time pin, same rationale as pmfConnect_signatureCompilesWithMatchingSlot
    // above: QDBusConnection::connect's SLOT() string isn't compile-checked at the
    // connect site, so pin the member-pointer shape instead.
    using SlotPtr =
        void (TTYSwitchMonitor::*)(const QString&, const QVariantMap&, const QStringList&);
    SlotPtr p = &TTYSwitchMonitor::handleSessionPropertiesChanged;
    QVERIFY(p != nullptr);
}

void TestTTYSwitchMonitor::handleSessionPropertiesChanged_filtersInterfaceAndExtractsActive() {
    TTYSwitchMonitor mon;
    QSignalSpy       spy(&mon, &TTYSwitchMonitor::ttySwitch);
    // wrong interface: must not touch sleeping state
    mon.handleSessionPropertiesChanged(
        "org.freedesktop.login1.Manager", { { "SomeProp", true } }, {});
    QCOMPARE(spy.count(), 0);
    // right interface, Active=false: must pause
    mon.handleSessionPropertiesChanged(
        "org.freedesktop.login1.Session", { { "Active", false } }, {});
    QCOMPARE(spy.count(), 1);
    QCOMPARE(mon.isSleeping(), true);
    // right interface, unrelated changed property: must not re-fire
    mon.handleSessionPropertiesChanged(
        "org.freedesktop.login1.Session", { { "IdleHint", true } }, {});
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TestTTYSwitchMonitor)
#include "tst_ttyswitchmonitor.moc"
