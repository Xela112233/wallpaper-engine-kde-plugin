#pragma once
#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QStringList>

namespace wekde
{

// Whether KWin shows Overview (in KWin 6 also the Desktop Grid) or Window
// View (Present Windows), for the we-manager unpause-overview patch: while
// one does, the wallpaper's window pause is lifted (WindowModel.qml).
//
// KWin gives clients no signal for it.  The one source is its Effects D-Bus
// object: org.kde.KWin /Effects org.kde.kwin.Effects, property activeEffects
// (as, read only, no change signal), the loaded effects whose isActive() is
// true; for these scene effects that is "running, with views, and the screen
// not locked".  So the process polls it, every kPollMs, with an async call
// (KWin is the compositor: plasmashell's GUI thread never waits on it), never
// two at once, and only while some screen's watcher is `watching`: while a
// window pauses that screen's wallpaper.  One poller serves every screen.
// A failed call makes `available` false, with `reason` naming the call and
// the error, and one journal line; it keeps asking, so a restarted KWin is
// picked up again.
class EffectsWatcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY watchingChanged)
    Q_PROPERTY(bool overviewActive READ overviewActive NOTIFY overviewActiveChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString reason READ reason NOTIFY availableChanged)

public:
    // How often KWin is asked while a screen watches.  Half a second: the
    // poll runs while a fullscreen or maximised window pauses the wallpaper
    // (mostly a game), against up to that much lag when Overview opens.
    static constexpr int kPollMs        = 500;
    static constexpr int kCallTimeoutMs = 1000;

    explicit EffectsWatcher(QObject* parent = nullptr);
    ~EffectsWatcher() override;

    bool    watching() const { return m_watching; }
    void    setWatching(bool on);
    bool    overviewActive() const { return m_overviewActive; }
    bool    available() const { return m_available; }
    QString reason() const { return m_reason; }

    // Whether an activeEffects list means Overview or Window View shows.
    static bool listShowsOverview(const QStringList& activeEffects);

    // Tests: the bus and service the poller asks (by default the session
    // bus and org.kde.KWin), and how many calls it has sent.
    static void setBusForTest(const QDBusConnection& bus, const QString& service);
    static int  callsForTest();

    // The poller's answer, handed to every watching instance.
    void apply(bool overviewActive, bool available, const QString& reason);

signals:
    void watchingChanged();
    void overviewActiveChanged();
    void availableChanged();

private:
    bool    m_watching { false };
    bool    m_overviewActive { false };
    bool    m_available { true };
    QString m_reason;
};

} // namespace wekde
