#include <doctest.h>

#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "LocalStorageLayout.hpp"
#include "SceneBackend.hpp"
#include "SceneScriptShimsJs.hpp"
#include "Utils/Logging.h"
#include "test_scratch.hpp"

// SceneScript localStorage per wallpaper and per screen (the KDE plugin's
// LocalStoragePerWallpaper, we-manager localstorage patch), through the
// script API a wallpaper uses (the production `localStorage` shim over the
// production bridge) and the files it leaves.
//
// The first case is the default: with nothing set, the scripts' values go to
// the two files under the renderer's cache that this renderer has always
// written, with its old limits, and nothing appears anywhere else.  The rest
// turn the switch on.

namespace
{

namespace fs = std::filesystem;

// Fresh XDG_CACHE_HOME and XDG_CONFIG_HOME for one case.
struct Sandbox {
    std::string root;
    QByteArray  savedCache, savedConfig;
    bool        hadCache { false }, hadConfig { false };

    explicit Sandbox(const char* tag) {
        static int counter = 0;
        root = wallpaper::test::ScratchDir("ls_wallpaper", (int)::getpid()) + "/" + tag + "_" +
               std::to_string(counter++);
        fs::remove_all(root);
        fs::create_directories(cache());
        fs::create_directories(config());
        hadCache    = qEnvironmentVariableIsSet("XDG_CACHE_HOME");
        savedCache  = qgetenv("XDG_CACHE_HOME");
        hadConfig   = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
        savedConfig = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CACHE_HOME", QByteArray::fromStdString(cache()));
        qputenv("XDG_CONFIG_HOME", QByteArray::fromStdString(config()));
    }
    ~Sandbox() {
        if (hadCache)
            qputenv("XDG_CACHE_HOME", savedCache);
        else
            qunsetenv("XDG_CACHE_HOME");
        if (hadConfig)
            qputenv("XDG_CONFIG_HOME", savedConfig);
        else
            qunsetenv("XDG_CONFIG_HOME");
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    std::string cache() const { return root + "/cache"; }
    std::string config() const { return root + "/config"; }
    // The wallpaper's folder with the switch on.
    std::string folder(const std::string& id) const {
        return config() + "/wekde/localstorage/" + id;
    }

    // Every file under the sandbox, relative to it, sorted.
    std::vector<std::string> files() const {
        std::vector<std::string> out;
        for (const auto& e : fs::recursive_directory_iterator(root)) {
            if (e.is_regular_file()) out.push_back(fs::relative(e.path(), root).string());
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    void write(const std::string& path, const std::string& content) const {
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream f(path, std::ios::trunc | std::ios::binary);
        f << content;
    }

    // What we-manager's Clear saved wallpaper data does to a wallpaper's
    // folder (we_manager/saveddata.py): a new token in <folder>/cleared,
    // then every data file goes, then another token.  `between` runs after
    // the first token, before the files go.  The reload it asks for
    // afterwards is the case's own business.
    void clearFromOutside(const std::string&           id,
                          const std::function<void()>& between = {}) const {
        static int n = 0;
        write(folder(id) + "/cleared", "token-" + std::to_string(++n) + "\n");
        if (between) between();
        for (const auto& e : fs::directory_iterator(folder(id))) {
            if (e.path().filename() != "cleared") fs::remove(e.path());
        }
        write(folder(id) + "/cleared", "token-" + std::to_string(++n) + "\n");
    }

    // The data files under the sandbox: files() without the clear marks.
    std::vector<std::string> dataFiles() const {
        std::vector<std::string> out;
        for (const std::string& f : files()) {
            if (fs::path(f).filename() != "cleared") out.push_back(f);
        }
        return out;
    }
};

QJsonObject readJson(const std::string& path) {
    QFile f(QString::fromStdString(path));
    if (! f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool isJsonObjectFile(const std::string& path) {
    QFile f(QString::fromStdString(path));
    if (! f.open(QIODevice::ReadOnly)) return false;
    return QJsonDocument::fromJson(f.readAll()).isObject();
}

struct LogCapture {
    static inline std::string text;
    LogCapture() {
        text = {};
        wallpaper_log_test::setSink([](int, const char* msg) {
            text += msg;
            text += '\n';
        });
    }
    ~LogCapture() { wallpaper_log_test::setSink(nullptr); }
    static bool mentions(const char* needle) { return text.find(needle) != std::string::npos; }
};

const char* const kWallpaper = "/tmp/wek-nonexistent/3662790108/scene.pkg";

// A scene's script engine with the production localStorage global, for the
// wallpaper in `source` on `screen`; `perWallpaper` is the switch.  With
// neither given it is what a library gets from QML that sets nothing.
std::unique_ptr<scenebackend::SceneObject> engine(const char* source = kWallpaper) {
    auto obj = std::make_unique<scenebackend::SceneObject>();
    obj->setSource(QUrl::fromLocalFile(source));
    obj->installScriptApiGlobalsForTesting();
    obj->debugEvalJs(wek::qml_helper::kInputAndLocalStorageJs);
    return obj;
}
std::unique_ptr<scenebackend::SceneObject> engineOn(const char* screen,
                                                    const char* source = kWallpaper) {
    auto obj = engine(source);
    obj->setStorageScreen(screen);
    obj->setStoragePerWallpaper(true);
    return obj;
}

// Runs script text; the value of its last expression as a string.
std::string js(scenebackend::SceneObject& obj, const char* code) {
    return obj.debugEvalJs(QString::fromUtf8(code)).toStdString();
}

// A string value of `kib` KiB under `key`, set through the script API;
// "ok", or the exception's text.
std::string setBlob(scenebackend::SceneObject& obj, const char* key, int kib,
                    const char* location = "localStorage.LOCATION_SCREEN") {
    const QString code =
        QStringLiteral("(function(){ try { localStorage.set('%1', 'x'.repeat(%2 * 1024), %3); "
                       "return 'ok'; } catch (e) { return String(e); } })()")
            .arg(QString::fromUtf8(key))
            .arg(kib)
            .arg(QString::fromUtf8(location));
    return obj.debugEvalJs(code).toStdString();
}

} // namespace

TEST_SUITE("SceneScript localStorage per wallpaper") {
    TEST_CASE("off, scripts save to the renderer's two cache files with their old limits") {
        Sandbox sb("off");
        {
            auto obj = engine();
            CHECK_FALSE(obj->storagePerWallpaper());
            CHECK(js(*obj, "localStorage.set('score', 7, localStorage.LOCATION_GLOBAL);"
                           "localStorage.set('pos', {x: 1, y: 2});"
                           "String(localStorage.get('score', localStorage.LOCATION_GLOBAL)) + ',' +"
                           "localStorage.get('pos').y + ',' + String(localStorage.get('score'))") ==
                  "7,2,undefined");
            // 200 KiB in one value: far over the per-wallpaper cap, well
            // within the cache files' 1 MiB a value.
            CHECK(setBlob(*obj, "big", 200) == "ok");
            // The screen alone moves nothing while the switch is off.
            obj->setStorageScreen("DP-1");
            CHECK(js(*obj, "localStorage.get('pos').x") == "1");
            obj->flushLocalStorageForTesting();
            CHECK(sb.files() ==
                  std::vector<std::string> { "cache/wescene-renderer/3662790108/localstorage.json",
                                             "cache/wescene-renderer/localstorage_global.json" });
            const QJsonObject global =
                readJson(sb.cache() + "/wescene-renderer/localstorage_global.json");
            CHECK(global.size() == 1);
            CHECK(global.value("score").toInt() == 7);
            const QJsonObject scene =
                readJson(sb.cache() + "/wescene-renderer/3662790108/localstorage.json");
            CHECK(scene.size() == 2);
            CHECK(scene.value("pos").toObject().value("y").toInt() == 2);
            CHECK(scene.value("big").toString().size() == 200 * 1024);
        }
        // The next engine of the wallpaper reads all of it back, the 200 KiB
        // value included: the cache files are read up to their old limit.
        auto again = engine();
        CHECK(js(*again, "localStorage.get('big').length + ',' + localStorage.get('pos').y") ==
              "204800,2");
        again->flushLocalStorageForTesting();
        CHECK(fs::file_size(sb.cache() + "/wescene-renderer/3662790108/localstorage.json") >
              200 * 1024);
        // Another wallpaper reads the first one's global value: the cache's
        // global file is every wallpaper's.
        auto other = engine("/tmp/wek-nonexistent/222/scene.pkg");
        CHECK(js(*other, "String(localStorage.get('score', localStorage.LOCATION_GLOBAL))") == "7");
        CHECK(js(*other, "String(localStorage.get('pos'))") == "undefined");
    }

    TEST_CASE("on, get, set, delete and clear work in both locations and land in the wallpaper's "
              "folder") {
        Sandbox sb("api");
        auto    obj = engineOn("DP-1");
        CHECK(js(*obj, "String(localStorage.get('nothing')) + ',' +"
                       "String(localStorage.get('nothing', localStorage.LOCATION_GLOBAL))") ==
              "undefined,undefined");
        // Without a location a value is the screen's; the two do not mix.
        CHECK(js(*obj, "localStorage.set('n', 42);"
                       "localStorage.set('s', 'text', localStorage.LOCATION_SCREEN);"
                       "localStorage.set('flag', true, localStorage.LOCATION_GLOBAL);"
                       "localStorage.set('list', [1, 'two', {three: 3}], localStorage.LOCATION_GLOBAL);"
                       "[localStorage.get('n') + 1, localStorage.get('s'),"
                       " localStorage.get('flag', localStorage.LOCATION_GLOBAL) === true,"
                       " localStorage.get('list', localStorage.LOCATION_GLOBAL)[2].three,"
                       " String(localStorage.get('n', localStorage.LOCATION_GLOBAL)),"
                       " String(localStorage.get('flag'))].join()") ==
              "43,text,true,3,undefined,undefined");
        obj->flushLocalStorageForTesting();
        CHECK(sb.files() ==
              std::vector<std::string> { "config/wekde/localstorage/3662790108/global.json",
                                         "config/wekde/localstorage/3662790108/screen-DP-1.json" });
        CHECK(readJson(sb.folder("3662790108") + "/screen-DP-1.json").value("n").toInt() == 42);
        CHECK(readJson(sb.folder("3662790108") + "/global.json").value("flag").toBool());

        // delete takes one key from one location; clear empties one location.
        CHECK(js(*obj, "localStorage.delete('n');"
                       "localStorage.delete('flag');" // the screen has no 'flag': nothing happens
                       "[String(localStorage.get('n')), localStorage.get('s'),"
                       " localStorage.get('flag', localStorage.LOCATION_GLOBAL)].join()") ==
              "undefined,text,true");
        CHECK(js(*obj, "localStorage.clear(localStorage.LOCATION_GLOBAL);"
                       "[String(localStorage.get('flag', localStorage.LOCATION_GLOBAL)),"
                       " String(localStorage.get('list', localStorage.LOCATION_GLOBAL)),"
                       " localStorage.get('s')].join()") == "undefined,undefined,text");
        obj->flushLocalStorageForTesting();
        const QJsonObject screen = readJson(sb.folder("3662790108") + "/screen-DP-1.json");
        CHECK(screen.size() == 1);
        CHECK(screen.value("s").toString() == "text");
        CHECK(readJson(sb.folder("3662790108") + "/global.json").isEmpty());
        CHECK(isJsonObjectFile(sb.folder("3662790108") + "/global.json"));
    }

    TEST_CASE("on, values are there again in the next engine") {
        Sandbox sb("persist");
        {
            auto obj = engineOn("DP-1");
            js(*obj, "localStorage.set('highscore', 1234, localStorage.LOCATION_GLOBAL);"
                     "localStorage.set('layout', {icons: [3, 1, 2]});");
            // No flush here: the engine going away (a reload, plasmashell
            // stopping) writes what is pending.
        }
        auto obj = engineOn("DP-1");
        CHECK(js(*obj, "localStorage.get('highscore', localStorage.LOCATION_GLOBAL) + ':' +"
                       "localStorage.get('layout').icons.join('')") == "1234:312");
    }

    TEST_CASE("on, two screens keep their own screen values and share the wallpaper's global ones") {
        Sandbox sb("screens");
        {
            auto left  = engineOn("DP-1");
            auto right = engineOn("HDMI-A-1");
            js(*left, "localStorage.set('pos', 'left');"
                      "localStorage.set('highscore', 10, localStorage.LOCATION_GLOBAL);");
            js(*right, "localStorage.set('pos', 'right');"
                       "localStorage.set('plays', 3, localStorage.LOCATION_GLOBAL);");
            left->flushLocalStorageForTesting();
            right->flushLocalStorageForTesting();
        }
        CHECK(sb.files() ==
              std::vector<std::string> { "config/wekde/localstorage/3662790108/global.json",
                                         "config/wekde/localstorage/3662790108/screen-DP-1.json",
                                         "config/wekde/localstorage/3662790108/screen-HDMI-A-1.json" });
        auto left  = engineOn("DP-1");
        auto right = engineOn("HDMI-A-1");
        const char* read = "[localStorage.get('pos'),"
                           " localStorage.get('highscore', localStorage.LOCATION_GLOBAL),"
                           " localStorage.get('plays', localStorage.LOCATION_GLOBAL)].join()";
        CHECK(js(*left, read) == "left,10,3");
        CHECK(js(*right, read) == "right,10,3");
        // Another wallpaper on the same screen sees none of it.
        auto other = engineOn("DP-1", "/tmp/wek-nonexistent/222/scene.pkg");
        CHECK(js(*other, "String(localStorage.get('pos')) + ',' +"
                         "String(localStorage.get('highscore', localStorage.LOCATION_GLOBAL))") ==
              "undefined,undefined");
    }

    TEST_CASE("on, a location holds 100 KiB and no more") {
        Sandbox sb("cap");
        auto    obj = engineOn("DP-1");
        CHECK(setBlob(*obj, "a", 60) == "ok");
        // The same key again: the size that counts is the size afterwards.
        CHECK(setBlob(*obj, "a", 70) == "ok");
        // A second value that does not fit is refused, and the script is told.
        const std::string refused = setBlob(*obj, "b", 40);
        CHECK(refused.find("QuotaExceededError") != std::string::npos);
        CHECK(js(*obj, "String(localStorage.get('b')) + ',' + localStorage.get('a').length") ==
              "undefined,71680");
        // The wallpaper's global location has its own 100 KiB.
        CHECK(setBlob(*obj, "g", 90, "localStorage.LOCATION_GLOBAL") == "ok");
        CHECK(setBlob(*obj, "h", 20, "localStorage.LOCATION_GLOBAL").find("QuotaExceededError") !=
              std::string::npos);
        obj->flushLocalStorageForTesting();
        for (const std::string& f : sb.files()) {
            CHECK(fs::file_size(sb.root + "/" + f) <=
                  (std::uintmax_t)wek::qml_helper::kMaxLsWallpaperFileBytes);
        }
        CHECK(sb.files().size() == 2);
    }

    TEST_CASE("on, two screens writing the global file at once cannot push it over the cap") {
        Sandbox sb("capmerge");
        auto    left  = engineOn("DP-1");
        auto    right = engineOn("HDMI-A-1");
        CHECK(setBlob(*left, "a", 60, "localStorage.LOCATION_GLOBAL") == "ok");
        CHECK(setBlob(*right, "b", 60, "localStorage.LOCATION_GLOBAL") == "ok");
        left->flushLocalStorageForTesting();
        LogCapture log;
        right->flushLocalStorageForTesting();
        const std::string global = sb.folder("3662790108") + "/global.json";
        CHECK(fs::file_size(global) <= (std::uintmax_t)wek::qml_helper::kMaxLsWallpaperFileBytes);
        CHECK(readJson(global).contains("a"));
        CHECK_FALSE(readJson(global).contains("b"));
        CHECK(LogCapture::mentions("localStorage"));
        // The screen that lost sees what is saved, and is not left retrying.
        CHECK_FALSE(right->localStorageDirtyForTesting(true));
        CHECK(js(*right, "String(localStorage.get('b', localStorage.LOCATION_GLOBAL)) + ',' +"
                         "localStorage.get('a', localStorage.LOCATION_GLOBAL).length") ==
              "undefined,61440");
    }

    TEST_CASE("on, a corrupt, truncated or oversized file does not stop the scene and is replaced") {
        Sandbox           sb("corrupt");
        const std::string dir = sb.folder("3662790108");
        sb.write(dir + "/global.json", R"({"highscore":12)"); // cut off mid-write
        sb.write(dir + "/screen-DP-1.json", std::string("\x00\xff garbage", 10));
        // A valid file over what this renderer writes there (one copied in
        // by hand): not read, so it cannot leave a scope no set fits into.
        sb.write(dir + "/screen-HDMI-A-1.json",
                 "{\"k\":\"" + std::string(150 * 1024, 'x') + "\"}");
        LogCapture log;
        auto       obj   = engineOn("DP-1");
        auto       other = engineOn("HDMI-A-1");
        CHECK(js(*obj, "String(localStorage.get('highscore', localStorage.LOCATION_GLOBAL)) + ',' +"
                       "String(localStorage.get('anything'))") == "undefined,undefined");
        CHECK(js(*other, "String(localStorage.get('k'))") == "undefined");
        CHECK(LogCapture::mentions("localStorage"));
        CHECK(js(*obj, "localStorage.set('pos', 5); localStorage.get('pos')") == "5");
        CHECK(setBlob(*other, "fits", 1) == "ok");
        obj->flushLocalStorageForTesting();
        other->flushLocalStorageForTesting();
        CHECK(isJsonObjectFile(dir + "/global.json"));
        CHECK(readJson(dir + "/global.json").isEmpty());
        CHECK(readJson(dir + "/screen-DP-1.json").value("pos").toInt() == 5);
        CHECK(readJson(dir + "/screen-HDMI-A-1.json").size() == 1);
        CHECK(fs::file_size(dir + "/screen-HDMI-A-1.json") < 2 * 1024);
    }

    TEST_CASE("on, a clear from outside holds against the engine that was running") {
        Sandbox           sb("cleared");
        const std::string dir = sb.folder("3662790108");
        {
            auto obj = engineOn("DP-1");
            js(*obj, "localStorage.set('highscore', 99, localStorage.LOCATION_GLOBAL);"
                     "localStorage.set('pos', 1); localStorage.set('layout', 'mine');");
            obj->flushLocalStorageForTesting();
            REQUIRE(fs::exists(dir + "/global.json"));
            REQUIRE(fs::exists(dir + "/screen-DP-1.json"));

            // we-manager clears, then has the wallpaper reloaded.  Until the
            // new scene has drawn, the old engine lives on: it still holds
            // the values and saves them again and again, and is destroyed in
            // the end.
            sb.clearFromOutside("3662790108");
            LogCapture log;
            for (int i = 0; i < 3; ++i) {
                js(*obj, "localStorage.set('highscore', 100, localStorage.LOCATION_GLOBAL);"
                         "localStorage.set('pos', 2);");
                obj->flushLocalStorageForTesting();
                CHECK(sb.dataFiles().empty());
                CHECK_FALSE(obj->localStorageDirtyForTesting(true));
            }
            CHECK(LogCapture::mentions("was cleared while its scene ran"));
            // What it held when the clear came is gone for its script too.
            CHECK(js(*obj, "localStorage.set('pos', 4); String(localStorage.get('layout'))") ==
                  "undefined");
        }
        CHECK(sb.dataFiles().empty());
        auto obj = engineOn("DP-1");
        CHECK(js(*obj, "String(localStorage.get('highscore', localStorage.LOCATION_GLOBAL)) + ',' +"
                       "String(localStorage.get('pos'))") == "undefined,undefined");
        // The scene loaded after the clear saves as always.
        js(*obj, "localStorage.set('pos', 3);");
        obj->flushLocalStorageForTesting();
        CHECK(readJson(dir + "/screen-DP-1.json").value("pos").toInt() == 3);
    }

    TEST_CASE("on, two screens: a clear holds when one screen's new scene saves before the "
              "other screen's old scene goes") {
        Sandbox           sb("cleared2");
        const std::string dir      = sb.folder("3662790108");
        auto              oldLeft  = engineOn("DP-1");
        auto              oldRight = engineOn("HDMI-A-1");
        js(*oldLeft, "localStorage.set('highscore', 99, localStorage.LOCATION_GLOBAL);"
                     "localStorage.set('pos', 'left');");
        oldLeft->flushLocalStorageForTesting();
        js(*oldRight, "localStorage.set('pos', 'right');");
        oldRight->flushLocalStorageForTesting();
        REQUIRE(js(*oldRight, "String(localStorage.get('highscore', localStorage.LOCATION_GLOBAL))") ==
                "99");

        // Clear, then ReloadWallpaper for DP-1, then for HDMI-A-1.  DP-1's new
        // scene is up first and saves a value of its own to the wallpaper's
        // global file, which is there again when HDMI-A-1's old scene saves
        // the old high score once more (in its destroy handler, say) and goes.
        sb.clearFromOutside("3662790108");
        oldLeft.reset();
        auto newLeft = engineOn("DP-1");
        js(*newLeft, "localStorage.set('plays', 1, localStorage.LOCATION_GLOBAL);"
                     "localStorage.set('pos', 'new left');");
        newLeft->flushLocalStorageForTesting();
        js(*oldRight, "localStorage.set('highscore', 99, localStorage.LOCATION_GLOBAL);"
                      "localStorage.set('pos', 'right');");
        oldRight.reset();

        const QJsonObject global = readJson(dir + "/global.json");
        CHECK(global.size() == 1);
        CHECK(global.value("plays").toInt() == 1);
        CHECK_FALSE(fs::exists(dir + "/screen-HDMI-A-1.json"));
        auto newRight = engineOn("HDMI-A-1");
        CHECK(js(*newRight, "String(localStorage.get('highscore', localStorage.LOCATION_GLOBAL)) + ',' +"
                            "localStorage.get('plays', localStorage.LOCATION_GLOBAL) + ',' +"
                            "String(localStorage.get('pos'))") == "undefined,1,undefined");
        // The new scenes keep saving, on both screens.
        js(*newRight, "localStorage.set('pos', 'new right');");
        newRight->flushLocalStorageForTesting();
        CHECK(readJson(dir + "/screen-HDMI-A-1.json").value("pos").toString() == "new right");
        CHECK(readJson(dir + "/screen-DP-1.json").value("pos").toString() == "new left");
    }

    TEST_CASE("on, a clear holds for a location the old scene had no file for, and on one "
              "screen when the new scene saves first") {
        Sandbox           sb("cleared3");
        const std::string dir = sb.folder("3662790108");
        auto              old = engineOn("DP-1");
        js(*old, "localStorage.set('pos', 'old');"); // the screen's file only: no global.json
        old->flushLocalStorageForTesting();
        REQUIRE_FALSE(fs::exists(dir + "/global.json"));

        sb.clearFromOutside("3662790108");
        auto fresh = engineOn("DP-1");
        js(*fresh, "localStorage.set('visits', 1);");
        fresh->flushLocalStorageForTesting();
        // The old scene's destroy handler: its first global value ever, and
        // its screen value again.
        js(*old, "localStorage.set('highscore', 99, localStorage.LOCATION_GLOBAL);"
                 "localStorage.set('pos', 'old');");
        old.reset();

        CHECK_FALSE(fs::exists(dir + "/global.json"));
        const QJsonObject screen = readJson(dir + "/screen-DP-1.json");
        CHECK(screen.size() == 1);
        CHECK(screen.value("visits").toInt() == 1);
    }

    TEST_CASE("on, a clear holds against a scene that first reads its data while it is being "
              "cleared") {
        Sandbox           sb("cleared4");
        const std::string dir = sb.folder("3662790108");
        {
            auto first = engineOn("DP-1");
            js(*first, "localStorage.set('highscore', 99, localStorage.LOCATION_GLOBAL);");
        }
        // A scene that is up but has not touched localStorage yet.  Its first
        // look comes when the clear has written its token and not yet deleted
        // the files: it holds the old value, and saves it again as it goes.
        auto late = engineOn("DP-1");
        sb.clearFromOutside("3662790108", [&late] {
            CHECK(js(*late, "String(localStorage.get('highscore', localStorage.LOCATION_GLOBAL))") ==
                  "99");
        });
        js(*late, "localStorage.set('highscore', 99, localStorage.LOCATION_GLOBAL);");
        late.reset();
        CHECK(sb.dataFiles().empty());
    }

    TEST_CASE("the switch changes where a running scene saves, and copies nothing") {
        Sandbox sb("switch");
        auto    obj = engineOn("DP-1");
        js(*obj, "localStorage.set('pos', 'per wallpaper');");
        // Off: what was pending is written where it belonged, and the cache
        // files, empty so far, are what the script sees.
        obj->setStorageScreen("");
        obj->setStoragePerWallpaper(false);
        CHECK(readJson(sb.folder("3662790108") + "/screen-DP-1.json").value("pos").toString() ==
              "per wallpaper");
        CHECK(js(*obj, "String(localStorage.get('pos'))") == "undefined");
        js(*obj, "localStorage.set('pos', 'cache');");
        // On again: the cache file gets its value, the wallpaper's own is back.
        obj->setStorageScreen("DP-1");
        obj->setStoragePerWallpaper(true);
        CHECK(readJson(sb.cache() + "/wescene-renderer/3662790108/localstorage.json")
                  .value("pos")
                  .toString() == "cache");
        CHECK(js(*obj, "localStorage.get('pos')") == "per wallpaper");
        // Another screen's values are another file.
        obj->setStorageScreen("HDMI-A-1");
        CHECK(js(*obj, "String(localStorage.get('pos'))") == "undefined");
    }
}
