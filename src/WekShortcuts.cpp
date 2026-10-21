#include "WekShortcuts.hpp"

#include <KActionCollection>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <QAction>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDebug>
#include <QProcess>
#include <QStandardPaths>
#include <QString>

#ifdef WEK_HAS_KIO
#    include <KIO/CommandLauncherJob>
#endif

namespace wekde
{

namespace
{

// Send an async D-Bus method call to the WekControl service (the C++ adapter
// over the QML PlaylistController).  Async because the action's triggered()
// signal lives on the UI thread and we don't want to block on a method
// response.  Errors are silent: the call is addressed by bus name, so
// whichever plasmoid owns the surface answers it no matter how many screens
// are up; with no wallpaper plasmoid running there is no owner and the call
// goes nowhere.
void invokeDBusMethod(const QString& method) {
    auto msg =
        QDBusMessage::createMethodCall(QStringLiteral("com.github.captsilver.WallpaperEngine"),
                                       QStringLiteral("/WallpaperEngine"),
                                       QStringLiteral("com.github.captsilver.WallpaperEngine"),
                                       method);
    QDBusConnection::sessionBus().asyncCall(msg);
}

// The wallpaper library is we-manager's window when we-manager is installed
// (we-manager patch). Starting we-manager-gui raises the window already
// running: a second launch asks the first to show itself and hands it its
// XDG activation token, which KIO::CommandLauncherJob gets from KWin, so the
// window may take the focus on Wayland. Without KIO at build time it is
// started with QProcess and no token (KWin may then only flash its task bar
// entry). Without we-manager, the hint as before: no clean Plasma 6 API opens
// the containment-owned configuration dialog.
void openWallpaperLibrary() {
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("we-manager-gui"));
    if (exe.isEmpty()) {
        qInfo("wek-shortcuts: 'Open Library' shortcut triggered; user "
              "should right-click desktop -> Configure Desktop and "
              "Wallpaper... (we-manager-gui is not on PATH)");
        return;
    }
#ifdef WEK_HAS_KIO
    auto* job = new KIO::CommandLauncherJob(exe, QStringList {});
    // we-manager's desktop file, without .desktop: the activation token and
    // the startup notification are for that application.
    job->setDesktopName(QStringLiteral("io.github.wemanager"));
    QObject::connect(job, &KJob::result, job, [exe](KJob* j) {
        if (j->error())
            qWarning().noquote() << "wek-shortcuts: KIO::CommandLauncherJob(" + exe
                                        + ") failed:" << j->errorString();
    });
    job->start();
#else
    if (! QProcess::startDetached(exe, QStringList {}))
        qWarning().noquote() << "wek-shortcuts: QProcess::startDetached(" + exe + ") failed";
#endif
}

} // namespace

WekShortcuts::WekShortcuts(QObject* parent)
    : QObject(parent), m_actions(new KActionCollection(this, QStringLiteral("wallpaper_engine"))) {
    m_actions->setComponentDisplayName(i18nc("@title shortcuts category", "Wallpaper Engine"));

    // Helper: create the action, default-unbind via setGlobalShortcut({}),
    // and wire its triggered() to the async D-Bus call.
    auto registerAction =
        [this](const QString& id, const QString& label, const QString& dbusMember) -> QAction* {
        auto* action = m_actions->addAction(id);
        action->setText(label);
        // A stock QAction facility (not a test-only hook) so tests can read
        // back the D-Bus member the trigger lambda captured, below.
        action->setData(dbusMember);
        // Default-unbound -- KDE convention.  User binds via System Settings.
        KGlobalAccel::self()->setGlobalShortcut(action, QList<QKeySequence> {});
        QObject::connect(action, &QAction::triggered, this, [dbusMember]() {
            invokeDBusMethod(dbusMember);
        });
        return action;
    };

    registerAction(QStringLiteral("next_wallpaper"),
                   i18nc("@action shortcut label", "Next wallpaper in playlist"),
                   QStringLiteral("Next"));

    registerAction(QStringLiteral("previous_wallpaper"),
                   i18nc("@action shortcut label", "Previous wallpaper in playlist"),
                   QStringLiteral("Previous"));

    registerAction(QStringLiteral("toggle_pause"),
                   i18nc("@action shortcut label", "Pause / resume wallpaper"),
                   QStringLiteral("Toggle"));

    registerAction(QStringLiteral("toggle_mute"),
                   i18nc("@action shortcut label", "Toggle wallpaper audio mute"),
                   QStringLiteral("ToggleMute"));

    registerAction(QStringLiteral("reload_wallpaper"),
                   i18nc("@action shortcut label", "Reload current wallpaper"),
                   QStringLiteral("Reload"));

    // open_library opens we-manager (openWallpaperLibrary above), or, without
    // it, logs a hint rather than D-Bus dispatch: no clean Plasma 6 API exists
    // to programmatically open a containment-owned dialog.  Revisit when
    // Plasma 6.x adds an openWallpaperConfig signal.
    auto* openLib = m_actions->addAction(QStringLiteral("open_library"));
    openLib->setText(
        i18nc("@action shortcut label", "Open wallpaper library (configuration dialog)"));
    KGlobalAccel::self()->setGlobalShortcut(openLib, QList<QKeySequence> {});
    QObject::connect(openLib, &QAction::triggered, this, []() { openWallpaperLibrary(); });
}

} // namespace wekde
