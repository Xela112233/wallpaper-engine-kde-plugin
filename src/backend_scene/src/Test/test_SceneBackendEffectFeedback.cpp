#include <doctest.h>

// Effect feedback (we-manager fluid-targets patch): the step between the QML
// and the renderer.  Scene.qml puts feedback: 1 into SceneViewer.sceneQuality
// when the setting is on (tst_main_effect_feedback.qml); here a real
// SceneObject with its SceneWallpaper is given such a map, and the renderer
// says which quality switches it then holds ("Scene quality: ...", the line
// SceneWallpaper logs when they change).  No Vulkan device: the scene is
// never loaded.

#include "SceneBackend.hpp"
#include "Utils/Logging.h"

#include <QByteArray>
#include <QThread>
#include <QVariantMap>

#include <mutex>
#include <string>
#include <vector>

namespace
{

// The renderer logs from its own thread.
struct QualityLines {
    static std::mutex& lock() {
        static std::mutex m;
        return m;
    }
    static std::vector<std::string>& lines() {
        static std::vector<std::string> v;
        return v;
    }
    static void sink(int, const char* msg) {
        const std::string     line(msg);
        const std::lock_guard g(lock());
        if (line.find("Scene quality:") != std::string::npos) lines().push_back(line);
    }
    QualityLines() {
        {
            const std::lock_guard g(lock());
            lines().clear();
        }
        wallpaper_log_test::setSink(&sink);
    }
    ~QualityLines() { wallpaper_log_test::setSink(nullptr); }

    // The next line, waited for up to three seconds; empty when none came.
    std::string next() {
        for (int i = 0; i < 300; i++) {
            {
                const std::lock_guard g(lock());
                if (taken < lines().size()) return lines()[taken++];
            }
            QThread::msleep(10);
        }
        return {};
    }
    std::size_t taken { 0 };
};

// scenescript_tests runs with WEKDE_TEST_NO_SCENE set, which makes a
// SceneObject without a renderer; this case wants the renderer.
struct WithScene {
    QByteArray saved { qgetenv("WEKDE_TEST_NO_SCENE") };
    WithScene() { qunsetenv("WEKDE_TEST_NO_SCENE"); }
    ~WithScene() { qputenv("WEKDE_TEST_NO_SCENE", saved); }
};

bool endsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

} // namespace

TEST_SUITE("SceneBackend.effectFeedback") {
    TEST_CASE("a quality map that says feedback reaches the renderer as feedback, and one that "
              "does not leaves the text it always sent") {
        QualityLines log;
        WithScene    env;
        scenebackend::SceneObject obj;

        // Without the key: the text of before the patch.
        obj.setSceneQuality({ { "textures", 1 } });
        std::string line = log.next();
        REQUIRE(line != "");
        CHECK(endsWith(line, "Scene quality: textures=1;targetWidth=0;targetHeight=0"));

        // The map Scene.qml makes with the setting on.
        obj.setSceneQuality({ { "textures", 1 }, { "feedback", 1 } });
        line = log.next();
        REQUIRE(line != "");
        CHECK(endsWith(line, "Scene quality: textures=1;targetWidth=0;targetHeight=0;feedback=1"));

        // And off again.
        obj.setSceneQuality({ { "textures", 1 } });
        line = log.next();
        REQUIRE(line != "");
        CHECK(endsWith(line, "Scene quality: textures=1;targetWidth=0;targetHeight=0"));
    }
}
