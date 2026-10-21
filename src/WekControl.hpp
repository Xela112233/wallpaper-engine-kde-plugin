#pragma once
#include <QDBusConnection>
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

namespace wekde
{

// D-Bus session-bus surface for headless control of the wallpaper engine.
// Service:   com.github.captsilver.WallpaperEngine
// Object:    /WallpaperEngine
// Interface: com.github.captsilver.WallpaperEngine
//
// All methods forward to the running QML PlaylistController via
// QMetaObject::invokeMethod; the C++ side is a thin adapter. See
// PlaylistController.qml for the QML-side export layer that this class
// invokes.
//
// Bus/object ownership is first-instance-wins: on multi-monitor, every
// wallpaper plasmoid lives in the same plasmashell process on one
// session-bus connection, so whichever WekControl exports /WallpaperEngine
// first owns the surface; the losers log and go silent (see registerOn()).
// But every instance -- winner and loser alike -- registers its own
// screen's PlaylistController into a process-wide registry the moment
// setPlaylistController() is called, and the write actions (Next,
// Previous, Pause, Resume, Toggle, Mute, Unmute, ToggleMute,
// ActivatePlaylist, Reload) broadcast to every registered controller, not
// just the exporting instance's own screen. A D-Bus Pause therefore pauses
// every screen, even though only one instance answers the D-Bus call.
//
// CurrentWorkshopId/CurrentPlaylistId/CurrentItemIndex/IsPaused and the
// WallpaperChanged signal are the one place this stays single-screen: they
// answer for the exporting instance's own screen only. There's no way to
// return "every screen's answer" from a single scalar value or an
// unlabeled signal without a wider API change, so this is a deliberate
// scope line, not an oversight.
class WekControl : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.github.captsilver.WallpaperEngine")

public:
    // Default constructor: lazy-registers on the session bus at construction.
    // Used by QML element instantiation. Silently no-ops on multi-monitor
    // secondaries (the object path is already exported) or environments
    // without a session bus.
    explicit WekControl(QObject* parent = nullptr);

    // Hands the bus name back so the next plasmoid can take over after a
    // screen unplug, wallpaper-plugin switch or containment re-create.
    ~WekControl() override;

    // Factory: registers on the session bus immediately. Returns nullptr if
    // another instance in this process already exports the object path
    // (multi-monitor case), another process owns the name, or the bus is
    // unavailable.
    static WekControl* registerOnSessionBus(QObject* parent = nullptr);

    // Test-only overload — accept an injected QDBusConnection so tests can
    // bring up a private peer-to-peer connection without dbus-launch.  Per
    // feedback_distrobox_dbus_launch_missing — neither dbus-launch nor
    // dbus-run-session ship in the Bazzite Fedora toolbox.
    static WekControl* registerOnConnection(QDBusConnection bus, QObject* parent = nullptr);

    // True if the bus registration succeeded — and therefore whether this
    // instance is the one that must release the name.  False on
    // multi-monitor secondaries, or when no session bus is available.
    bool isRegistered() const { return m_registered; }

    // Set by the QML root once PlaylistController.qml is created. Calls
    // before this is set silently no-op (the slot bodies check m_controller).
    Q_INVOKABLE void setPlaylistController(QObject* controller);

    // Bridge for QML-side WallpaperChanged emission — when the QML
    // PlaylistController detects a workshop-id transition, it calls this to
    // fire the D-Bus signal.
    Q_INVOKABLE void emitWallpaperChanged(const QString& workshopId, const QString& playlistId);

    // StopScope "all screens": this screen stopped (true) or loaded again
    // (false).  Calls remoteStop(source, stopped) on every OTHER screen's
    // controller in this process; `source` names this instance.  A stop still
    // announced when this instance goes (its screen was removed) is withdrawn
    // then, so the other screens do not stay stopped.
    Q_INVOKABLE void broadcastStop(bool stopped);

    // main.qml, when keyboard mode ends on its screen however it ends (Esc,
    // a click elsewhere, another wallpaper) or could not start: the next
    // ToggleKeyboard starts it again instead of ending it.
    Q_INVOKABLE void keyboardModeEnded();

    // What ReloadAudioConfig runs: the plugin installs the scene renderer's
    // AudioBus::Reconfigure at start (plugin.cpp), tests a fake.  Kept out
    // of this class so it builds without the renderer.
    static void setAudioConfigReloader(std::function<void()> reload);
    // What AudioCaptureStatus answers (the plugin: AudioBus::CaptureStatus,
    // "pending" while a reload runs).
    static void setAudioCaptureStatusReader(std::function<QString()> read);

    // Where the "Save wallpaper screenshot" shortcut writes:
    // <Pictures>/Wallpapers/wallpaper-<yyyyMMdd-HHmmss>.png (each screen
    // then adds "-<screen>" before the extension).
    static QString defaultScreenshotPath(const QDateTime& when);

public slots:
    // Playlist navigation
    Q_NOREPLY void Next();
    Q_NOREPLY void Previous();
    Q_NOREPLY void Pause();
    Q_NOREPLY void Resume();
    Q_NOREPLY void Toggle();

    // Audio
    Q_NOREPLY void Mute();
    Q_NOREPLY void Unmute();
    Q_NOREPLY void ToggleMute();

    // Activation
    Q_NOREPLY void ActivatePlaylist(const QString& id);
    Q_NOREPLY void Reload();

    // Keyboard mode for web wallpapers (we-manager patch; main.qml).  Off
    // everywhere when a screen has it; else on for one screen only: the one
    // under the pointer, else the one whose desktop window is active, else
    // the first that can take it (KeyboardInput on, a web wallpaper).
    Q_NOREPLY void ToggleKeyboard();

    // Re-read ~/.config/wekde/audio.json (we-manager's Settings > Audio
    // writes it): the capture source, sensitivity, noise floor, attack and
    // decay of the process-wide audio analyzer.  Every screen shares it.
    // Returns false when no reloader is installed.  The reload runs off the
    // GUI thread; AudioCaptureStatus says how it went.
    bool ReloadAudioConfig();
    // The process-wide capture: "off" (no wallpaper wants system audio),
    // "on", "fallback" (the source audio.json names is absent; the default
    // output's monitor meanwhile), "failed" (nothing could be opened), or
    // "pending" while a ReloadAudioConfig still runs; "" when the plugin
    // installed no reader.
    QString AudioCaptureStatus();

    // A still of the wallpaper, without icons or windows, written to `path`
    // (PNG, or what its extension names): from the screen named `screen`
    // (QScreen::name, the output's connector), or from every screen when
    // `screen` is empty, each with "-<screen>" before the extension.  The
    // file appears asynchronously: a scene writes the frame it renders next,
    // video and web wallpapers Qt's grab of the backend item.  It answers at
    // once (an empty reply), so a caller waiting for one never times out.
    void Screenshot(const QString& path, const QString& screen);
    // Every screen, to defaultScreenshotPath(now); returns that path (before
    // the screens' names are added).  The "Save wallpaper screenshot"
    // shortcut calls it.
    QString SaveScreenshots();

    // Query (sync, returns a value — D-Bus method, not signal/property)
    QString CurrentWorkshopId() const;
    QString CurrentPlaylistId() const;
    int     CurrentItemIndex() const;
    bool    IsPaused() const;

signals:
    // Broadcast on every wallpaper change. Third-party panel widgets
    // subscribe to this to update "now playing" displays.
    void WallpaperChanged(const QString& workshopId, const QString& playlistId);

private:
    // Private constructor for the static factories.
    WekControl(QObject* parent, QDBusConnection bus);

    bool registerOn(QDBusConnection& bus);

    QPointer<QObject> m_controller;
    QDBusConnection   m_bus;
    bool              m_registered = false;
    bool              m_stopAnnounced = false;
};

} // namespace wekde
