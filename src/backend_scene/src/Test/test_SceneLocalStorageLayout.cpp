#include <doctest.h>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include "LocalStorageLayout.hpp"

// Where SceneScript's localStorage lives with LocalStoragePerWallpaper on
// (we-manager localstorage patch): one folder per wallpaper, one file for the
// wallpaper on every screen and one per screen, and no name that leaves the
// folder.  Header-only on purpose (Qt Core and LocalStorageLayout.hpp), so
// we-manager's tests build it without the renderer; what SceneObject reads
// and writes there is test_SceneLocalStoragePerWallpaper.cpp's.

namespace
{

using namespace wek::qml_helper;

void touch(const QString& path, const QDateTime& when) {
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("{}");
    f.close();
    REQUIRE(f.open(QIODevice::ReadWrite));
    REQUIRE(f.setFileTime(when, QFileDevice::FileModificationTime));
}

} // namespace

TEST_SUITE("SceneScript localStorage per-wallpaper layout") {
    TEST_CASE("a wallpaper has one global file and one file per screen in its own folder") {
        const QString root = QStringLiteral("/cfg/wekde/localstorage");
        CHECK(PerWallpaperStoragePath(root, "3662790108", true, "DP-1") ==
              "/cfg/wekde/localstorage/3662790108/global.json");
        // The global file does not depend on the screen.
        CHECK(PerWallpaperStoragePath(root, "3662790108", true, "HDMI-A-1") ==
              PerWallpaperStoragePath(root, "3662790108", true, "DP-1"));
        CHECK(PerWallpaperStoragePath(root, "3662790108", false, "DP-1") ==
              "/cfg/wekde/localstorage/3662790108/screen-DP-1.json");
        CHECK(PerWallpaperStoragePath(root, "3662790108", false, "HDMI-A-1") ==
              "/cfg/wekde/localstorage/3662790108/screen-HDMI-A-1.json");
        // Another wallpaper shares nothing with it, a project folder's name included.
        CHECK(PerWallpaperStoragePath(root, "dino_run", true, "DP-1") ==
              "/cfg/wekde/localstorage/dino_run/global.json");
        // A plugin that does not know its screen still has one screen file.
        CHECK(PerWallpaperStoragePath(root, "dino_run", false, "") ==
              "/cfg/wekde/localstorage/dino_run/screen.json");
    }

    TEST_CASE("no folder or screen name leaves the wallpaper's folder") {
        const QString root = QStringLiteral("/cfg/wekde/localstorage");
        // A folder name that is not one plain path component gives no file at
        // all (the values then stay in memory).
        for (const char* bad : { "", ".", "..", "a/b", "../x", "new\nline" }) {
            CHECK(PerWallpaperStoragePath(root, QString::fromUtf8(bad), true, "DP-1").isEmpty());
            CHECK(PerWallpaperStoragePath(root, QString::fromUtf8(bad), false, "DP-1").isEmpty());
        }
        CHECK(PerWallpaperStoragePath(root, QString(201, QLatin1Char('a')), true, "").isEmpty());
        CHECK(PerWallpaperStoragePath(QString(), "111", true, "").isEmpty());
        // A screen name is encoded, never a path: the file stays in the folder.
        for (const char* odd : { "../../etc/passwd", "a/b", "..", "DP 1", "x.json/../y" }) {
            const QString p = PerWallpaperStoragePath(root, "111", false, QString::fromUtf8(odd));
            const QFileInfo fi(p);
            CHECK(fi.path() == "/cfg/wekde/localstorage/111");
            CHECK(fi.fileName().startsWith("screen-"));
            CHECK(fi.fileName().endsWith(".json"));
            CHECK_FALSE(fi.fileName().contains('/'));
        }
        CHECK(ScreenStorageFileName("a/b") == "screen-a%2Fb.json");
        // A very long name is cut, not a longer path.
        CHECK(ScreenStorageFileName(QString(500, QLatin1Char('x'))).size() <= 7 + 120 + 5);
    }

    TEST_CASE("a wallpaper's clear token is what its folder's cleared file holds") {
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        const QString root = tmp.path();
        REQUIRE(QDir(root).mkpath("111"));
        // Never cleared: nothing, and the same for a folder name that gets no file.
        CHECK(StorageClearToken(root, "111").isEmpty());
        CHECK(StorageClearToken(root, "..").isEmpty());
        QFile f(root + "/111/cleared");
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("18f3a2c9d1e-0a1b2c3d\n");
        f.close();
        CHECK(StorageClearToken(root, "111") == "18f3a2c9d1e-0a1b2c3d");
        // Another clear is another token; another wallpaper has its own.
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("18f3a2c9d1f-99999999\n");
        f.close();
        CHECK(StorageClearToken(root, "111") == "18f3a2c9d1f-99999999");
        CHECK(StorageClearToken(root, "222").isEmpty());
    }

    TEST_CASE("a wallpaper keeps the files of sixteen screens, the newest ones") {
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        const QString   dir  = tmp.path();
        const QDateTime base = QDateTime::currentDateTimeUtc().addDays(-1);
        for (int i = 0; i < 20; ++i) {
            touch(dir + QStringLiteral("/screen-S%1.json").arg(i), base.addSecs(i * 60));
        }
        touch(dir + "/global.json", base.addSecs(-3600)); // older than all of them
        // The file just written is the oldest by its time and is kept all the same.
        const int removed = PruneScreenStorageFiles(dir, "screen-S0.json");
        CHECK(removed == 4);
        CHECK(QFile::exists(dir + "/global.json"));
        CHECK(QFile::exists(dir + "/screen-S0.json"));
        for (int i = 1; i <= 4; ++i) CHECK_FALSE(QFile::exists(dir + QStringLiteral("/screen-S%1.json").arg(i)));
        for (int i = 5; i < 20; ++i) CHECK(QFile::exists(dir + QStringLiteral("/screen-S%1.json").arg(i)));
        // Within the limit nothing goes.
        CHECK(PruneScreenStorageFiles(dir, "screen-S0.json") == 0);
    }
}
