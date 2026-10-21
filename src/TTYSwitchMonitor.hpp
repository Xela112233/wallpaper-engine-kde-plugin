#pragma once
#include <QQuickItem>
#include <QDBusConnection>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <functional>

namespace wekde
{

// Listens for org.freedesktop.login1.Manager.PrepareForSleep(bool), and
// separately watches our own logind session's Active property for VT
// switches (Ctrl+Alt+Fn away/back, or a display manager's fast user
// switch) -- there is no PrepareForSleep-shaped broadcast for those, so
// Active is the only signal logind offers. The two sources are
// independent and OR'd together into a single ttySwitch(bool) signal.
// The renderer uses this via main.qml's pause-on-suspend chain (alongside
// ScreenSaverMonitor's pause-on-lock, the focus-window pause, and the
// battery-discharge pause).
//
// A resume from sleep (we-manager resume-reload patch) is told apart from a
// VT switch and given its own signal, resumedFromSleep(kind, how, asleepMs):
// * how "logind": PrepareForSleep(false) after a PrepareForSleep(true);
// * how "clock": the time the machine spent suspended (CLOCK_BOOTTIME minus
//   CLOCK_MONOTONIC, which only a sleep moves; NTP and the timezone do not)
//   rose by more than kMinAsleepMs since the last look, every kPollMs: a
//   sleep logind did not announce, or a system bus this item could not reach.
// One sleep seen by both counts once: logind's wake is the clock's when the
// clock has already told a rise of the suspended time since the sleep began
// (the clock's last look before PrepareForSleep(true), which a frozen session
// may hand over only after the wake), or when the last resume was the
// clock's and nothing has risen since; logind's wake moves the clock's last
// look forward, so the clock tells only a later rise (another sleep, however
// soon after).  kind is "suspend" when the kernel's suspend count
// (/sys/power/suspend_stats/success) rose across it; "aborted" when it did
// not and the machine spent less than kMinAsleepMs suspended (a suspend or
// hibernation the kernel refused or a wake event cut short: logind still
// sends the wake); "hibernation" when it did not rise otherwise; "sleep"
// when the count cannot be read.  asleepMs comes from the suspended time.
class TTYSwitchMonitor : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool sleeping READ isSleeping NOTIFY ttySwitch)

public:
    TTYSwitchMonitor(QQuickItem* parent = nullptr);
    // Injectable-bus overload for tests: keeps the slot reachable without
    // a live system D-Bus.  Production constructs through the default
    // ctor; tests pass either QDBusConnection::systemBus() (if a real bus
    // is reachable) or a synthetic disconnected QDBusConnection so the
    // wireUp branch can be code-pathed without dbus-launch/dbus-run-
    // session (which are absent from the Bazzite Fedora toolbox).
    // Mirrors the ScreenSaverMonitor pattern.
    TTYSwitchMonitor(QDBusConnection bus, QQuickItem* parent);

    bool isSleeping() const { return m_sleeping; }

    static constexpr int    kPollMs      = 15000;
    static constexpr qint64 kMinAsleepMs = 2000;

    // Tests: where the suspended time (ms) and the kernel's suspend count
    // (-1: unreadable) come from.
    void setClocksForTest(std::function<qint64()> suspendedMs, std::function<qint64()> suspendCount);

signals:
    void ttySwitch(bool sleep);
    void resumedFromSleep(const QString& kind, const QString& how, qint64 asleepMs);

public slots:
    void handlePrepareForSleep(bool sleep);
    void handleSessionActiveChanged(bool active);
    void handleSessionPropertiesChanged(const QString&     interfaceName,
                                        const QVariantMap& changedProperties,
                                        const QStringList& invalidatedProperties);
    // Every kPollMs: the suspended time's look (public for the tests).
    void checkSuspendedTime();

private:
    void wireUp(QDBusConnection bus);
    void resolveSessionByPid(QDBusConnection bus);
    void resolveSessionByXdgSessionId(QDBusConnection bus);
    void subscribeToSessionActive(QDBusConnection bus, const QString& sessionPath);
    void updateSleeping();
    void startClock();
    void noteResume(const QString& how);

    std::function<qint64()> m_suspendedMs;
    std::function<qint64()> m_suspendCount;
    QTimer                  m_poll;
    qint64                  m_lastSuspended { 0 };   // the suspended time at the last look
    qint64                  m_lastCount { -1 };      // the suspend count at the last look
    qint64                  m_sleptAtSuspended { 0 }; // at PrepareForSleep(true)
    qint64                  m_sleptAtCount { -1 };
    qint64                  m_toldSuspended { 0 };   // the suspended time up to which a resume was emitted
    QString                 m_toldHow;               // who emitted it ("" once logind's wake paired with it)

    bool m_sleeping;
    bool m_suspending;    // from PrepareForSleep
    bool m_sessionActive; // from our logind session's Active property
};

} // namespace wekde