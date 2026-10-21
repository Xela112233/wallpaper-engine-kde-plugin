#include "TTYSwitchMonitor.hpp"
#include <QCoreApplication>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QFile>

#include <algorithm>

#include <time.h>

using namespace wekde;

namespace
{
qint64 clockMs(clockid_t id) {
    timespec ts {};
    if (clock_gettime(id, &ts) != 0) return 0;
    return static_cast<qint64>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}
// The time the machine has been suspended since boot: CLOCK_BOOTTIME counts
// it, CLOCK_MONOTONIC does not.
qint64 suspendedMsNow() { return clockMs(CLOCK_BOOTTIME) - clockMs(CLOCK_MONOTONIC); }
// The kernel's count of suspends that worked (to RAM or idle; a hibernation
// is not one), -1 when it cannot be read.
qint64 suspendCountNow() {
    QFile f(QStringLiteral("/sys/power/suspend_stats/success"));
    if (! f.open(QIODevice::ReadOnly)) return -1;
    bool         ok = false;
    const qint64 n  = f.readAll().trimmed().toLongLong(&ok);
    return ok ? n : -1;
}
} // namespace

void TTYSwitchMonitor::startClock() {
    m_suspendedMs   = suspendedMsNow;
    m_suspendCount  = suspendCountNow;
    m_lastSuspended = m_suspendedMs();
    m_toldSuspended = m_lastSuspended;
    m_lastCount     = m_suspendCount();
    m_poll.setInterval(kPollMs);
    connect(&m_poll, &QTimer::timeout, this, &TTYSwitchMonitor::checkSuspendedTime);
    m_poll.start();
}

void TTYSwitchMonitor::setClocksForTest(std::function<qint64()> suspendedMs, std::function<qint64()> suspendCount) {
    m_suspendedMs   = std::move(suspendedMs);
    m_suspendCount  = std::move(suspendCount);
    m_lastSuspended = m_suspendedMs();
    m_toldSuspended = m_lastSuspended;
    m_lastCount     = m_suspendCount();
}

void TTYSwitchMonitor::checkSuspendedTime() {
    const qint64 now = m_suspendedMs();
    if (now - m_lastSuspended > kMinAsleepMs) noteResume(QStringLiteral("clock"));
    m_lastSuspended = now;
    if (! m_suspending) m_lastCount = m_suspendCount();
}

// A resume, from either source: once per sleep, with the time asleep and its
// kind.
void TTYSwitchMonitor::noteResume(const QString& how) {
    const bool   logind       = how == QStringLiteral("logind");
    const qint64 nowSuspended = m_suspendedMs();
    const qint64 since        = logind ? m_sleptAtSuspended : m_lastSuspended;
    const qint64 asleep       = std::max<qint64>(0, nowSuspended - since);
    const qint64 before       = logind ? m_sleptAtCount : m_lastCount;
    const qint64 count        = m_suspendCount();
    // logind's wake for a sleep the clock has told already: the clock saw the
    // suspended time rise after this sleep's PrepareForSleep(true), or (that
    // signal handled late, after the clock's look) the last resume was the
    // clock's and nothing has risen since.
    const bool told = logind
                      && (m_toldSuspended > m_sleptAtSuspended
                          || (m_toldHow == QStringLiteral("clock") && m_toldSuspended == nowSuspended));
    m_lastSuspended = nowSuspended;   // the clock's next look starts from here
    m_lastCount     = count;
    m_toldSuspended = nowSuspended;
    m_toldHow       = told ? QString() : how;   // a clock resume pairs with one logind wake at most
    if (told) return;
    const QString kind = (before >= 0 && count > before) ? QStringLiteral("suspend")
                         : asleep < kMinAsleepMs             ? QStringLiteral("aborted")
                         : (before < 0 || count < 0)         ? QStringLiteral("sleep")
                                                             : QStringLiteral("hibernation");
    emit resumedFromSleep(kind, how, asleep);
}

TTYSwitchMonitor::TTYSwitchMonitor(QQuickItem* parent)
    : QQuickItem(parent), m_sleeping(false), m_suspending(false), m_sessionActive(true) {
    startClock();
    // Pause-on-suspend is a polish feature — degrading to "no TTY/suspend
    // pause" is fine in toolbox / sandbox / minimal environments. Don't
    // qFatal; that takes down plasmashell with us.
    QDBusConnection systemBus = QDBusConnection::systemBus();
    if (! systemBus.isConnected()) {
        qWarning("wekde::TTYSwitchMonitor: system D-Bus unavailable; "
                 "pause-on-suspend disabled");
        return;
    }
    wireUp(systemBus);
}

TTYSwitchMonitor::TTYSwitchMonitor(QDBusConnection bus, QQuickItem* parent)
    : QQuickItem(parent), m_sleeping(false), m_suspending(false), m_sessionActive(true) {
    startClock();
    if (bus.isConnected()) wireUp(bus);
}

void TTYSwitchMonitor::wireUp(QDBusConnection bus) {
    bool connected = bus.connect("org.freedesktop.login1",
                                 "/org/freedesktop/login1",
                                 "org.freedesktop.login1.Manager",
                                 "PrepareForSleep",
                                 this,
                                 SLOT(handlePrepareForSleep(bool)));
    if (! connected) {
        qWarning("wekde::TTYSwitchMonitor: failed to connect to "
                 "PrepareForSleep signal; pause-on-suspend disabled");
    }

    // Resolve our own logind session so we can watch its Active property for
    // VT switches.
    resolveSessionByPid(bus);
}

void TTYSwitchMonitor::resolveSessionByPid(QDBusConnection bus) {
    // GetSessionByPID's D-Bus signature is (u)->(o) -- a plain uint32 in, an
    // object path out. QCoreApplication::applicationPid() returns qint64;
    // marshaling that directly sends signature 'x', which logind
    // hard-rejects ("Invalid arguments 'x' ... expecting 'u'"). Narrow and
    // wrap explicitly.
    QDBusMessage getSession = QDBusMessage::createMethodCall("org.freedesktop.login1",
                                                             "/org/freedesktop/login1",
                                                             "org.freedesktop.login1.Manager",
                                                             "GetSessionByPID");
    getSession << QVariant::fromValue<quint32>(
        static_cast<quint32>(QCoreApplication::applicationPid()));
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(getSession), this);
    connect(watcher,
            &QDBusPendingCallWatcher::finished,
            this,
            // `bus` (a QDBusConnection) is captured by value, and the body
            // below calls its non-const connect() -- a plain lambda's
            // operator() is const, so that wouldn't compile without
            // `mutable`. This is safe despite looking like it mutates a
            // throwaway copy: QDBusConnection is a thin handle around a
            // shared, refcounted d-pointer, so connect() on any copy
            // registers against the one real bus connection underneath.
            [this, bus](QDBusPendingCallWatcher* w) mutable {
                w->deleteLater();
                QDBusPendingReply<QDBusObjectPath> reply = *w;
                if (reply.isValid()) {
                    subscribeToSessionActive(bus, reply.value().path());
                    return;
                }
                qWarning("wekde::TTYSwitchMonitor: GetSessionByPID failed (%s); "
                         "trying XDG_SESSION_ID instead",
                         qUtf8Printable(reply.error().message()));
                resolveSessionByXdgSessionId(bus);
            });
}

void TTYSwitchMonitor::resolveSessionByXdgSessionId(QDBusConnection bus) {
    // GetSessionByPID resolves a PID to a session by matching a
    // session-N.scope component in that process's cgroup path. Under a
    // systemd-managed Plasma session (Fedora/Bazzite's mainline setup, this
    // project's primary distribution target) plasmashell instead runs as a
    // user service -- plasma-plasmashell.service, under user@<uid>.service's
    // own tree -- which never has that scope component, so the lookup fails
    // unconditionally there, not just occasionally. XDG_SESSION_ID is set by
    // pam_systemd at login and inherited into the systemd user manager's
    // environment, and from there into every unit it starts, so it survives
    // the indirection that breaks the cgroup-based lookup.
    const QByteArray sessionId = qgetenv("XDG_SESSION_ID");
    if (sessionId.isEmpty()) {
        qWarning("wekde::TTYSwitchMonitor: XDG_SESSION_ID not set either; "
                 "pause-on-VT-switch disabled");
        return;
    }
    QDBusMessage getSession = QDBusMessage::createMethodCall("org.freedesktop.login1",
                                                             "/org/freedesktop/login1",
                                                             "org.freedesktop.login1.Manager",
                                                             "GetSession");
    getSession << QString::fromUtf8(sessionId);
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(getSession), this);
    connect(watcher,
            &QDBusPendingCallWatcher::finished,
            this,
            // Same mutable-capture reasoning as resolveSessionByPid's watcher.
            [this, bus](QDBusPendingCallWatcher* w) mutable {
                w->deleteLater();
                QDBusPendingReply<QDBusObjectPath> reply = *w;
                if (! reply.isValid()) {
                    qWarning("wekde::TTYSwitchMonitor: GetSession(XDG_SESSION_ID) failed "
                             "(%s); pause-on-VT-switch disabled",
                             qUtf8Printable(reply.error().message()));
                    return;
                }
                subscribeToSessionActive(bus, reply.value().path());
            });
}

void TTYSwitchMonitor::subscribeToSessionActive(QDBusConnection bus, const QString& sessionPath) {
    bool subscribed =
        bus.connect("org.freedesktop.login1",
                    sessionPath,
                    "org.freedesktop.DBus.Properties",
                    "PropertiesChanged",
                    this,
                    SLOT(handleSessionPropertiesChanged(QString, QVariantMap, QStringList)));
    if (! subscribed) {
        qWarning("wekde::TTYSwitchMonitor: failed to subscribe to session "
                 "PropertiesChanged; pause-on-VT-switch disabled");
    }
}

void TTYSwitchMonitor::handlePrepareForSleep(bool sleep) {
    const bool wasSuspending = m_suspending;
    m_suspending             = sleep;
    if (sleep && ! wasSuspending) {
        // The sleep starts where the clock looked last, not where this signal
        // is handled: systemd (256 on) freezes the user session for a sleep,
        // and a GUI thread that had not got to PrepareForSleep(true) before
        // the freeze handles it after the wake, with the time asleep already
        // counted.  Nothing sleeps between two looks otherwise.
        m_sleptAtSuspended = m_lastSuspended;
        m_sleptAtCount     = m_lastCount;
    }
    updateSleeping();
    // After the pause chain hears the wake: a false with no true before it
    // (an item made mid-sleep, a repeated signal) is no resume.
    if (! sleep && wasSuspending) noteResume(QStringLiteral("logind"));
}

void TTYSwitchMonitor::handleSessionActiveChanged(bool active) {
    m_sessionActive = active;
    updateSleeping();
}

void TTYSwitchMonitor::handleSessionPropertiesChanged(
    const QString& interfaceName, const QVariantMap& changedProperties,
    const QStringList& /*invalidatedProperties*/) {
    if (interfaceName != "org.freedesktop.login1.Session") return;
    const auto it = changedProperties.constFind("Active");
    if (it == changedProperties.constEnd()) return;
    handleSessionActiveChanged(it->toBool());
}

void TTYSwitchMonitor::updateSleeping() {
    const bool sleeping = m_suspending || ! m_sessionActive;
    if (m_sleeping != sleeping) {
        m_sleeping = sleeping;
        emit ttySwitch(sleeping);
    }
}
