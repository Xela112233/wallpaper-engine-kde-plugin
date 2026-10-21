#include "EffectsWatcher.hpp"

#include <QDBusArgument>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QSet>
#include <QTimer>
#include <QVariant>

using namespace wekde;

namespace
{
// The process's one poller: asks KWin while any watcher watches, and hands
// the answer to every one of them.
class Poller : public QObject {
public:
    static Poller& instance() {
        static Poller* p = new Poller(); // lives as long as plasmashell
        return *p;
    }

    void add(EffectsWatcher* w) {
        if (m_watchers.contains(w)) return;
        m_watchers.insert(w);
        // Another screen is watching already: its answer is current.
        if (m_fresh) w->apply(m_active, m_available, m_reason);
        if (! m_timer.isActive()) {
            m_timer.start();
            poll();
        }
    }
    void remove(EffectsWatcher* w) {
        m_watchers.remove(w);
        if (m_watchers.isEmpty()) {
            m_timer.stop();
            m_fresh = false;
        }
    }
    void setBus(const QDBusConnection& bus, const QString& service) {
        m_bus     = bus;
        m_service = service;
        m_fresh   = false;
    }
    int calls() const { return m_calls; }

private:
    Poller(): m_bus(QDBusConnection::sessionBus()), m_service(QStringLiteral("org.kde.KWin")) {
        m_timer.setInterval(EffectsWatcher::kPollMs);
        connect(&m_timer, &QTimer::timeout, this, &Poller::poll);
    }

    void poll() {
        if (m_inFlight) return;   // a slow KWin: never two calls at once
        ++m_calls;
        const QString service = m_service;
        // A dead connection's asyncCall gives a call that never finishes, which
        // would hold m_inFlight for good: it is a failed call, named, and the
        // next tick asks again.
        if (! m_bus.isConnected()) {
            settle(false, false,
                   QStringLiteral("%1 /Effects org.kde.kwin.Effects.activeEffects failed (the session bus is not "
                                  "connected)").arg(service));
            return;
        }
        QDBusMessage msg = QDBusMessage::createMethodCall(service, QStringLiteral("/Effects"),
                                                          QStringLiteral("org.freedesktop.DBus.Properties"),
                                                          QStringLiteral("Get"));
        msg << QStringLiteral("org.kde.kwin.Effects") << QStringLiteral("activeEffects");
        m_inFlight = true;
        auto* call = new QDBusPendingCallWatcher(m_bus.asyncCall(msg, EffectsWatcher::kCallTimeoutMs), this);
        connect(call, &QDBusPendingCallWatcher::finished, this, [this, service](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            m_inFlight = false;
            QDBusPendingReply<QVariant> reply = *w;
            if (reply.isError()) {
                settle(false, false, QStringLiteral("%1 /Effects org.kde.kwin.Effects.activeEffects failed (%2: %3)")
                                         .arg(service, reply.error().name(), reply.error().message()));
                return;
            }
            const QVariant    v    = reply.value();
            const QStringList list = v.canConvert<QDBusArgument>() ? qdbus_cast<QStringList>(v.value<QDBusArgument>())
                                                                    : v.toStringList();
            settle(EffectsWatcher::listShowsOverview(list), true, QString());
        });
    }

    // One answer (or failure): logged once per change of `available`, then
    // handed to every watcher still watching.
    void settle(bool active, bool available, const QString& reason) {
        if (available != m_available) {
            if (available)
                qInfo().noquote() << "[WEK] overview: KWin's Effects interface answers again; Overview"
                                     " unpauses the wallpaper";
            else
                qWarning().noquote()
                    << QStringLiteral("[WEK] overview: %1; Overview does not unpause the wallpaper until it "
                                      "answers").arg(reason);
        }
        m_active    = active;
        m_available = available;
        m_reason    = reason;
        if (m_watchers.isEmpty()) return;   // nobody watches any more
        m_fresh = true;
        // A watcher's signal handler may delete another watcher (its
        // destructor takes it out of m_watchers): go over a copy, and skip
        // whatever has left the set meanwhile.
        const auto watchers = m_watchers;
        for (EffectsWatcher* x : watchers)
            if (m_watchers.contains(x)) x->apply(m_active, m_available, m_reason);
    }

    QDBusConnection        m_bus;
    QString                m_service;
    QTimer                 m_timer;
    QSet<EffectsWatcher*>  m_watchers;
    bool                   m_inFlight { false };
    bool                   m_fresh { false };   // m_active etc. come from the poll that is running
    bool                   m_active { false };
    bool                   m_available { true };
    QString                m_reason;
    int                    m_calls { 0 };
};
} // namespace

EffectsWatcher::EffectsWatcher(QObject* parent): QObject(parent) {}

EffectsWatcher::~EffectsWatcher() {
    if (m_watching) Poller::instance().remove(this);
}

void EffectsWatcher::setWatching(bool on) {
    if (on == m_watching) return;
    m_watching = on;
    if (on) {
        Poller::instance().add(this);
    } else {
        Poller::instance().remove(this);
        // Not asked any more: no stale "Overview shows".
        if (m_overviewActive) {
            m_overviewActive = false;
            emit overviewActiveChanged();
        }
    }
    emit watchingChanged();
}

void EffectsWatcher::apply(bool overviewActive, bool available, const QString& reason) {
    if (! m_watching) return;
    if (available != m_available || reason != m_reason) {
        m_available = available;
        m_reason    = reason;
        emit availableChanged();
    }
    if (overviewActive != m_overviewActive) {
        m_overviewActive = overviewActive;
        emit overviewActiveChanged();
    }
}

bool EffectsWatcher::listShowsOverview(const QStringList& activeEffects) {
    return activeEffects.contains(QStringLiteral("overview")) || activeEffects.contains(QStringLiteral("windowview"));
}

void EffectsWatcher::setBusForTest(const QDBusConnection& bus, const QString& service) {
    Poller::instance().setBus(bus, service);
}

int EffectsWatcher::callsForTest() { return Poller::instance().calls(); }
