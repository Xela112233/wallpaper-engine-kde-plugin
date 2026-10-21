#include <doctest.h>

// Fluid simulation (we-manager fluid-effect patch): the steps between the QML
// and the renderer that a SceneObject makes.  A quality map that says fluid
// reaches the renderer as fluid (and one that does not leaves the text it
// sent before); a script's IEffect.executeMaterialFunction(name) reaches the
// renderer's side only for a scene loaded with the setting on, and without
// it an effect has no such method, as before the patch.

#include "IPropertyDispatchSink.hpp"
#include "SceneBackend.hpp"
#include "Utils/Logging.h"

#include <QByteArray>
#include <QThread>
#include <QVariantMap>

#include <mutex>
#include <string>
#include <tuple>
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
// SceneObject without a renderer; the first case wants the renderer.
struct WithScene {
    QByteArray saved { qgetenv("WEKDE_TEST_NO_SCENE") };
    WithScene() { qunsetenv("WEKDE_TEST_NO_SCENE"); }
    ~WithScene() { qputenv("WEKDE_TEST_NO_SCENE", saved); }
};

bool endsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// Records the effect functions that reach the renderer's side; nothing else
// is driven here.
struct FunctionSink : scenebackend::IPropertyDispatchSink {
    std::vector<std::tuple<int32_t, int32_t, std::string>> functions;
    void runEffectFunction(int32_t nodeId, int32_t effectIndex, std::string name) override {
        functions.emplace_back(nodeId, effectIndex, std::move(name));
    }

    void updateNodeTransform(int32_t, const std::string&, float, float, float) override {}
    void updateNodeAlpha(int32_t, float) override {}
    void updateNodeVisible(int32_t, bool) override {}
    void updateText(int32_t, const std::string&) override {}
    void updateTextPointsize(int32_t, float) override {}
    void updateColor(int32_t, float, float, float) override {}
    void updateParticleRate(int32_t, float) override {}
    void applyLayerBatch(const std::vector<wallpaper::SceneWallpaper::LayerBatchUpdate>&) override {
    }
    void updateEffectVisible(int32_t, int32_t, bool) override {}
    void updateMaterialValue(int32_t, std::string, std::vector<float>) override {}
    void updateEffectMaterialValue(int32_t, int32_t, std::string, std::vector<float>) override {}
    void setLayerSpriteFrame(int32_t, bool, int32_t) override {}
    void updateTextStyle(int32_t, std::string, std::string, std::string) override {}
    void updateSoundVolume(int32_t, float) override {}
    void updateClearColor(float, float, float) override {}
    void updateBloomStrength(float) override {}
    void updateBloomThreshold(float) override {}
    void updateCameraFov(float) override {}
    void updateCameraLookAt(float, float, float, float, float, float, float, float,
                            float) override {}
    void updateAmbientColor(float, float, float) override {}
    void updateSkylightColor(float, float, float) override {}
    void updateLightColor(int32_t, float, float, float) override {}
    void updateLightRadius(int32_t, float) override {}
    void updateLightIntensity(int32_t, float) override {}
    void updateLightPosition(int32_t, float, float, float) override {}
    void updateSystemTexture(std::string, std::shared_ptr<wallpaper::Image>) override {}
};

// A layer "water" (id 7) with two effects and a property script on it that,
// each tick, runs what the test put into _probe.run on effect 1.
struct Rig {
    std::unique_ptr<scenebackend::SceneObject> obj { std::make_unique<scenebackend::SceneObject>() };
    FunctionSink*                              sink { nullptr };

    // efn: what the scene's layer data says of the layer's functions (as
    // JSON), or empty for a scene loaded without the setting.
    Rig(bool fluid, const char* efn) {
        auto s = std::make_unique<FunctionSink>();
        sink   = s.get();
        obj->setDispatchSinkForTesting(std::move(s));
        if (fluid) obj->setSceneQuality({ { "textures", 0 }, { "feedback", 1 }, { "fluid", 1 } });
        obj->layerProxyForTesting(7, "water", { "tint", "fluidsimulation" });
        if (efn[0]) obj->debugEvalJs(QStringLiteral("_layerInitStates['water'].efn = %1;").arg(QLatin1String(efn)));
        obj->seedPropertyScriptForTesting(
            7,
            scenebackend::SceneObject::TestScriptKind::Alpha,
            "alpha",
            "(function(v){\n"
            "  var e = thisScene.getLayer('water').getEffect(1);\n"
            "  _probe.kind = typeof e.executeMaterialFunction;\n"
            "  for (var i = 0; i < _probe.run.length; i++) e.executeMaterialFunction(_probe.run[i]);\n"
            "  _probe.run = [];\n"
            "  return v;\n"
            "})");
        obj->debugEvalJs("var _probe = { kind: '', run: [] };");
    }
    void tick(const char* run) {
        obj->debugEvalJs(QStringLiteral("_probe.run = %1;").arg(QLatin1String(run)));
        obj->evaluatePropertyScriptsForTesting();
    }
    std::string kind() { return obj->debugEvalJs("_probe.kind").toStdString(); }
};

} // namespace

TEST_SUITE("SceneBackend.effectFluid") {
    TEST_CASE("a quality map that says fluid reaches the renderer as fluid, and one that does not "
              "leaves the text it sent before") {
        QualityLines log;
        WithScene    env;
        scenebackend::SceneObject obj;

        obj.setSceneQuality({ { "textures", 1 } });
        std::string line = log.next();
        REQUIRE(line != "");
        CHECK(endsWith(line, "Scene quality: textures=1;targetWidth=0;targetHeight=0"));

        // The map Scene.qml makes with FluidSimulation on.
        obj.setSceneQuality({ { "textures", 1 }, { "feedback", 1 }, { "fluid", 1 } });
        line = log.next();
        REQUIRE(line != "");
        CHECK(endsWith(line, "Scene quality: textures=1;targetWidth=0;targetHeight=0;feedback=1;fluid=1"));

        // EffectFeedback alone: the text of the fluid-targets patch.
        obj.setSceneQuality({ { "textures", 1 }, { "feedback", 1 } });
        line = log.next();
        REQUIRE(line != "");
        CHECK(endsWith(line, "Scene quality: textures=1;targetWidth=0;targetHeight=0;feedback=1"));
    }

    TEST_CASE("on, a script's executeMaterialFunction reaches the renderer with the layer, the "
              "effect and the name, once, and only for a function the effect has") {
        Rig r(true, "[[], ['clearDye', 'clearVelocity']]");
        r.tick("['clearDye']");
        CHECK(r.kind() == "function");
        REQUIRE(r.sink->functions.size() == 1);
        CHECK(r.sink->functions[0] == std::make_tuple(7, 1, std::string("clearDye")));

        r.tick("[]");
        CHECK(r.sink->functions.size() == 1);

        // A name the effect does not have is dropped without an error; two
        // in one tick both arrive, in order.
        r.tick("['noSuchFunction', 'clearVelocity', 'clearDye']");
        REQUIRE(r.sink->functions.size() == 3);
        CHECK(std::get<2>(r.sink->functions[1]) == "clearVelocity");
        CHECK(std::get<2>(r.sink->functions[2]) == "clearDye");
    }

    TEST_CASE("without the setting an effect has no such method, as before, and nothing reaches "
              "the renderer") {
        Rig off(false, "");
        off.tick("[]");
        CHECK(off.kind() == "undefined");
        CHECK(off.sink->functions.empty());

        // A scene whose layer data carries no functions (no effect of the
        // layer has any), with the setting on: the same.
        Rig none(true, "");
        none.tick("[]");
        CHECK(none.kind() == "undefined");
        CHECK(none.sink->functions.empty());
    }
}
