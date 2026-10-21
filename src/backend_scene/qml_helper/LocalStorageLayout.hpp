#pragma once

// Where SceneScript's localStorage lives while the KDE plugin's
// LocalStoragePerWallpaper is on (we-manager localstorage patch), and how
// much of it there may be.
//
// Off (the default), SceneBackend.cpp keeps the two files it always kept
// under the renderer's cache, with the caps of LocalStorageQuota.hpp, and
// nothing here is used:
//   <cache>/wescene-renderer/localstorage_global.json     every wallpaper's LOCATION_GLOBAL
//   <cache>/wescene-renderer/<folder>/localstorage.json   one wallpaper's LOCATION_SCREEN, every screen's
//
// On, the scopes are Wallpaper Engine's (ILocalStorage: "up to 100 KB of
// data per wallpaper", LOCATION_GLOBAL "shared across all screens",
// LOCATION_SCREEN "specific to the screen", the default):
//   <config>/wekde/localstorage/<folder>/global.json          one wallpaper's LOCATION_GLOBAL
//   <config>/wekde/localstorage/<folder>/screen-<screen>.json  its LOCATION_SCREEN on one screen
// <folder> is the name of the wallpaper's own folder (its workshop id, or a
// project folder's name), as before; <screen> is what the plugin says its
// screen is (the output's connector, QScreen::name).  One folder per
// wallpaper, so we-manager can list a wallpaper's saved data and delete it.
//
// A script chooses neither part of the path: the folder comes from the
// source the user applied and the screen from the plugin, and both are
// checked here before they become a path.  The folder is the one the scene's
// file is in, as in the cache layout, so an item whose project.json puts its
// scene in a subfolder is filed under that subfolder's name.
//
// Header-only and Qt Core only, like LocalStorageQuota.hpp, so it is tested
// without the renderer.

#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QStandardPaths>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtCore/qglobal.h>

namespace wek::qml_helper
{

// One location's file (global.json, or one screen's) holds at most this much
// compact JSON: Wallpaper Engine's 100 KB per wallpaper, counted per file so
// a wallpaper written to Wallpaper Engine's limit fits on every screen.
inline constexpr qsizetype kMaxLsWallpaperFileBytes = 100 * 1024;
// A file larger than the cap is not read at all (it is not ours: we never
// write past the cap); it is replaced at the next write.
inline constexpr qsizetype kMaxLsWallpaperPreloadBytes = kMaxLsWallpaperFileBytes;
// A wallpaper keeps the files of this many screens; the least recently
// written go when a new screen's file is made.  With global.json that bounds
// a wallpaper's folder at 17 files of 100 KiB.
inline constexpr int kMaxLsScreenFiles = 16;

inline constexpr QLatin1String kLsGlobalFileName { "global.json" };
// <folder>/cleared: the mark of the last clear from outside.  we-manager's
// Clear saved wallpaper data writes a new token into it, deletes the
// folder's data files, writes another token, and then has the wallpaper
// loaded again.  A scene reads the token when it reads its scopes and again
// around every write: a scene that finds another token than the one it
// started with was running when its data was cleared, and saves nothing more
// (SceneBackend.cpp).  The first token stops a scene that held the values
// before the clear, the second one a scene that read the files while they
// were being deleted.  The file stays in the folder; it is a few bytes.
inline constexpr QLatin1String kLsClearedFileName { "cleared" };

// <config>/wekde/localstorage, next to the plugin's other state
// (wekde/wallpaper, wekde/loading, wekde/audio.json).
inline QString PerWallpaperStorageRoot() {
    const QString cfg = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (cfg.isEmpty()) return {};
    return cfg + QStringLiteral("/wekde/localstorage");
}

// Whether a wallpaper folder's name can be one directory under the root: a
// single path component that is not "." or "..", without control characters,
// of at most 200 bytes.
inline bool ValidStorageFolder(const QString& folder) {
    if (folder.isEmpty() || folder == QLatin1String(".") || folder == QLatin1String("..")) return false;
    if (folder.toUtf8().size() > 200) return false;
    for (const QChar c : folder) {
        if (c == QLatin1Char('/') || c.unicode() < 0x20 || c.unicode() == 0x7f) return false;
    }
    return true;
}

// The file of one screen's LOCATION_SCREEN values: "screen-<screen>.json",
// with every byte of the screen's name outside A-Z a-z 0-9 - . _ ~
// percent-encoded, so "DP-1" stays readable and no name leaves the folder.
// A plugin that does not know its screen gets "screen.json".
inline QString ScreenStorageFileName(const QString& screen) {
    if (screen.isEmpty()) return QStringLiteral("screen.json");
    const QByteArray enc = QUrl::toPercentEncoding(screen).left(120);
    return QStringLiteral("screen-") + QString::fromLatin1(enc) + QStringLiteral(".json");
}

// The file of one location, or "" when the wallpaper has no usable folder
// name (the values then live in memory for the session).
inline QString PerWallpaperStoragePath(const QString& root, const QString& folder, bool global,
                                       const QString& screen) {
    if (root.isEmpty() || ! ValidStorageFolder(folder)) return {};
    return root + QLatin1Char('/') + folder + QLatin1Char('/') +
           (global ? QString(kLsGlobalFileName) : ScreenStorageFileName(screen));
}

// The wallpaper's clear token: the first 64 bytes of <folder>/cleared, or
// nothing when there is no such file (never cleared) or no usable folder.
inline QByteArray StorageClearToken(const QString& root, const QString& folder) {
    if (root.isEmpty() || ! ValidStorageFolder(folder)) return {};
    QFile f(root + QLatin1Char('/') + folder + QLatin1Char('/') + QString(kLsClearedFileName));
    if (! f.open(QIODevice::ReadOnly)) return {};
    return f.read(64).trimmed();
}

// What the flush would write for this scope, in bytes.
inline qsizetype SerializedScopeSize(const QJsonObject& scope) {
    return QJsonDocument(scope).toJson(QJsonDocument::Compact).size();
}

// Keeps the `maxFiles` most recently written screen files of a wallpaper's
// folder and removes the rest; `keep` (a file name) is never removed.
// Returns how many went.
inline int PruneScreenStorageFiles(const QString& dir, const QString& keep,
                                   int maxFiles = kMaxLsScreenFiles) {
    QDir                d(dir);
    const QFileInfoList files =
        d.entryInfoList({ QStringLiteral("screen.json"), QStringLiteral("screen-*.json") },
                        QDir::Files | QDir::NoSymLinks,
                        QDir::Time); // newest first
    int kept    = 0;
    int removed = 0;
    for (const QFileInfo& fi : files) {
        if (fi.fileName() == keep || kept < maxFiles - 1) {
            // one place is always the keeper's
            if (fi.fileName() != keep) ++kept;
            continue;
        }
        if (QFile::remove(fi.absoluteFilePath())) ++removed;
    }
    return removed;
}

} // namespace wek::qml_helper
