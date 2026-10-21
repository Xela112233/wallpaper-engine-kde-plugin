// SceneScript input fixes: input.cursorLeftDown, live engine.isPortrait(),
// rotated hit tests with event.localPosition, and click propagation
// (disablepropagation).  Header-only pieces, so these run without a scene:
// the orientation and layer-proxy JavaScript is the production source from
// SceneScriptShimsJs.hpp, the hit test and event helpers the production
// headers SceneBackend.cpp calls.
#include <doctest.h>
#include <QJSEngine>
#include <QJSValue>
#include <QString>
#include <vector>

#include "SceneCursorEvent.h"
#include "SceneCursorHitTest.h"
#include "SceneScriptShimsJs.hpp"

using scenebackend::cursorHitOnLayer;
using scenebackend::CursorParallax;
using scenebackend::hitTestLayerProxy;
using scenebackend::LayerCursorHit;
using scenebackend::PressHit;
using scenebackend::pressPropagation;

namespace
{

// engine + Vec classes + input, as production installs them (setupEngineGlobals).
struct InputEnv {
    QJSEngine engine;
    QJSValue  input;
    InputEnv() {
        engine.globalObject().setProperty("engine", engine.newObject());
        engine.evaluate(wek::qml_helper::kVecClassesJs);
        engine.evaluate(wek::qml_helper::kInputAndLocalStorageJs);
        // The getter buildCursorTargets installs.
        engine.evaluate("Object.defineProperty(engine, 'cursorLeftDown', {\n"
                        "  get: function() { return input.cursorLeftDown; },\n"
                        "  enumerable: true\n"
                        "});\n");
        input = engine.globalObject().property("input");
    }
    bool eval(const char* js) { return engine.evaluate(js).toBool(); }
};

// The production layer proxy over a given _layerInitStates literal, with an
// optional fake __sceneBridge.getLayerWorldTransform.
struct ProxyEnv {
    QJSEngine engine;
    explicit ProxyEnv(const char* initStates, const char* bridge = nullptr) {
        engine.globalObject().setProperty("engine", engine.newObject());
        engine.evaluate(wek::qml_helper::kVecClassesJs);
        if (bridge) engine.evaluate(bridge);
        engine.evaluate(QString("var _layerInitStates = %1;\n"
                                "var _sceneOrtho = [1920, 1080];\n")
                            .arg(initStates));
        engine.evaluate(wek::qml_helper::kMaterialProxyJs);
        engine.evaluate(wek::qml_helper::kHierarchyProxyJs);
        engine.evaluate(wek::qml_helper::kLayerProxyJs);
    }
    QJSValue layer(const char* name) {
        return engine.evaluate(QString("thisScene.getLayer('%1')").arg(name));
    }
};

} // namespace

TEST_SUITE("SceneScript orientation follows the wallpaper's size") {
    TEST_CASE("isPortrait and isLandscape read the size production writes") {
        QJSEngine engine;
        engine.globalObject().setProperty("engine", engine.newObject());
        QJSValue view = engine.evaluate(wek::qml_helper::kOrientationJs);
        REQUIRE(view.isObject());
        view.setProperty("x", 1920);
        view.setProperty("y", 1080);
        CHECK(engine.evaluate("engine.isLandscape()").toBool());
        CHECK_FALSE(engine.evaluate("engine.isPortrait()").toBool());
    }

    TEST_CASE("a resize to a portrait monitor turns isPortrait on without a reload") {
        // A landscape scene (design canvas 1920x1080) on a monitor turned to
        // portrait: the item becomes 1080x1920 and the same functions answer.
        QJSEngine engine;
        engine.globalObject().setProperty("engine", engine.newObject());
        QJSValue view = engine.evaluate(wek::qml_helper::kOrientationJs);
        view.setProperty("x", 1920);
        view.setProperty("y", 1080);
        QJSValue isPortrait = engine.evaluate("engine.isPortrait");
        CHECK_FALSE(isPortrait.call().toBool());
        view.setProperty("x", 1080);
        view.setProperty("y", 1920);
        CHECK(isPortrait.call().toBool());
        CHECK_FALSE(engine.evaluate("engine.isLandscape()").toBool());
    }

    TEST_CASE("a square view counts as landscape, as before") {
        QJSEngine engine;
        engine.globalObject().setProperty("engine", engine.newObject());
        QJSValue view = engine.evaluate(wek::qml_helper::kOrientationJs);
        view.setProperty("x", 1080);
        view.setProperty("y", 1080);
        CHECK(engine.evaluate("engine.isLandscape()").toBool());
        CHECK_FALSE(engine.evaluate("engine.isPortrait()").toBool());
    }
}

TEST_SUITE("input.cursorLeftDown") {
    TEST_CASE("a polling script sees the button down for the whole drag") {
        InputEnv env;
        env.engine.evaluate("var seen = []; function tick() { seen.push(input.cursorLeftDown); }");
        env.engine.evaluate("tick();");
        scenebackend::setCursorLeftDown(env.input, true); // mousePressEvent
        env.engine.evaluate("tick();");                   // a tick while dragging
        env.engine.evaluate("tick();");
        CHECK(env.eval("engine.cursorLeftDown"));
        scenebackend::setCursorLeftDown(env.input, false); // mouseReleaseEvent
        env.engine.evaluate("tick();");
        CHECK(env.engine.evaluate("seen.join(',')").toString() == "false,true,true,false");
        CHECK_FALSE(env.eval("engine.cursorLeftDown"));
    }

    TEST_CASE("a lost press (ungrab) lets go of the button") {
        InputEnv env;
        scenebackend::setCursorLeftDown(env.input, true);
        scenebackend::setCursorLeftDown(env.input, false); // mouseUngrabEvent
        CHECK_FALSE(env.eval("input.cursorLeftDown"));
    }

    TEST_CASE("before the engine exists nothing is written") {
        QJSValue none;
        scenebackend::setCursorLeftDown(none, true);
        CHECK_FALSE(none.isObject());
    }
}

TEST_SUITE("Cursor hit-test on rotated layers, with localPosition") {
    // A 200x20 bar centred at (100, 100), turned 90 degrees: it now stands
    // upright, 20 wide and 200 tall.
    static const char* kRotatedBar =
        "({ _state: { size: {x:200, y:20},"
        "  worldFrame: { ox:100, oy:100, a:0, b:1, c:-1, d:0 } } })";

    TEST_CASE("a turned layer is hit where it is drawn, not where it would be unturned") {
        QJSEngine engine;
        QJSValue  bar = engine.evaluate(kRotatedBar);
        CHECK(hitTestLayerProxy(bar, 100.0f, 180.0f));       // along the upright bar
        CHECK_FALSE(hitTestLayerProxy(bar, 180.0f, 100.0f)); // where the unturned bar was
    }

    TEST_CASE("localPosition is the point in the layer's own coordinates") {
        QJSEngine      engine;
        QJSValue       bar = engine.evaluate(kRotatedBar);
        LayerCursorHit hit = cursorHitOnLayer(bar, 100.0f, 150.0f);
        CHECK(hit.inside);
        REQUIRE(hit.hasLocal);
        // 50 up the screen is 50 along the bar's own x axis.
        CHECK(hit.localX == doctest::Approx(50.0f));
        CHECK(hit.localY == doctest::Approx(0.0f));
    }

    TEST_CASE("scale and mirroring are undone too") {
        QJSEngine engine;
        QJSValue  scaled = engine.evaluate(
            "({ _state: { size: {x:100, y:100}, worldFrame: { ox:0, oy:0, a:2, b:0, c:0, d:0.5 } } })");
        LayerCursorHit hit = cursorHitOnLayer(scaled, 90.0f, 20.0f);
        CHECK(hit.inside);
        CHECK(hit.localX == doctest::Approx(45.0f));
        CHECK(hit.localY == doctest::Approx(40.0f));
        QJSValue mirrored = engine.evaluate(
            "({ _state: { size: {x:100, y:100}, worldFrame: { ox:0, oy:0, a:-1, b:0, c:0, d:1 } } })");
        hit = cursorHitOnLayer(mirrored, 30.0f, 10.0f);
        CHECK(hit.inside);
        CHECK(hit.localX == doctest::Approx(-30.0f));
        CHECK(hit.localY == doctest::Approx(10.0f));
    }

    TEST_CASE("without a frame the axis-aligned test and its local point stay as before") {
        QJSEngine engine;
        QJSValue  proxy = engine.evaluate(
            "({ _state: { origin:{x:100,y:200,z:0}, scale:{x:-2,y:1,z:1}, size:{x:80,y:40} } })");
        LayerCursorHit hit = cursorHitOnLayer(proxy, 160.0f, 210.0f);
        CHECK(hit.inside); // |160-100| < 80*2/2
        CHECK(hit.localX == doctest::Approx(30.0f));
        CHECK(hit.localY == doctest::Approx(10.0f));
        CHECK_FALSE(hitTestLayerProxy(proxy, 181.0f, 200.0f));
    }

    TEST_CASE("solid=false keeps the local point but is never inside") {
        QJSEngine engine;
        QJSValue  proxy = engine.evaluate(
            "({ _state: { solid:false, size:{x:100,y:100},"
            "  worldFrame: { ox:0, oy:0, a:1, b:0, c:0, d:1 } } })");
        LayerCursorHit hit = cursorHitOnLayer(proxy, 10.0f, 0.0f);
        CHECK_FALSE(hit.inside);
        CHECK(hit.hasLocal);
        CHECK(hit.localX == doctest::Approx(10.0f));
    }

    TEST_CASE("a zero scale has no local point and no hit") {
        QJSEngine engine;
        QJSValue  proxy = engine.evaluate(
            "({ _state: { size:{x:100,y:100}, worldFrame: { ox:0, oy:0, a:0, b:0, c:0, d:1 } } })");
        LayerCursorHit hit = cursorHitOnLayer(proxy, 0.0f, 0.0f);
        CHECK_FALSE(hit.inside);
        CHECK_FALSE(hit.hasLocal);
    }

    TEST_CASE("parallax moves the turned layer's centre as before") {
        QJSEngine      engine;
        QJSValue       proxy = engine.evaluate(
            "({ _state: { size:{x:100,y:100}, parallaxDepth:{x:1,y:1},"
            "  worldFrame: { ox:100, oy:100, a:1, b:0, c:0, d:1 } } })");
        CursorParallax para;
        para.enable         = true;
        para.amount         = 1.0f;
        para.mouseInfluence = 0.0f;
        para.camX           = 0.0f;
        para.camY           = 0.0f;
        // The centre moves from (100, 100) to (200, 200).
        CHECK(hitTestLayerProxy(proxy, 200.0f, 200.0f, para));
        CHECK_FALSE(hitTestLayerProxy(proxy, 100.0f, 100.0f, para));
    }

    TEST_CASE("the production proxy turns with the transform baked at load") {
        // wa is the world matrix's x and y axes (xx, xy, yx, yy), baked by
        // WPSceneParser next to wo and ws; here a quarter turn.
        ProxyEnv env("{ 'bar': { o:[100,100,0], s:[1,1,1], a:[0,0,90], sz:[200,20], v:true,"
                     "  wo:[100,100,0], ws:[1,1,1], wa:[0,1,-1,0] } }");
        QJSValue bar = env.layer("bar");
        REQUIRE(bar.isObject());
        CHECK(hitTestLayerProxy(bar, 100.0f, 180.0f));
        CHECK_FALSE(hitTestLayerProxy(bar, 180.0f, 100.0f));
    }

    TEST_CASE("the production proxy prefers the renderer's live world matrix") {
        // A script turned the layer after load: the bridge's matrix (column
        // major, as SceneObject::getLayerWorldTransform returns it) wins over
        // the unturned transform baked at load.
        ProxyEnv env("{ 'bar': { o:[100,100,0], s:[1,1,1], a:[0,0,0], sz:[200,20], v:true,"
                     "  wo:[100,100,0], ws:[1,1,1], wa:[1,0,0,1] } }",
                     "var __sceneBridge = { getLayerWorldTransform: function(name) {\n"
                     "  return [0,1,0,0, -1,0,0,0, 0,0,1,0, 100,100,0,1]; } };\n");
        QJSValue bar = env.layer("bar");
        CHECK(hitTestLayerProxy(bar, 100.0f, 180.0f));
        CHECK_FALSE(hitTestLayerProxy(bar, 180.0f, 100.0f));
    }

    TEST_CASE("init states without wa still hit by the baked origin and scale") {
        ProxyEnv env("{ 'bg': { o:[100,200,0], s:[1,1,1], a:[0,0,0], sz:[80,40], v:true,"
                     "  wo:[100,200,0], ws:[1,1,1] } }");
        QJSValue bg = env.layer("bg");
        CHECK(hitTestLayerProxy(bg, 139.0f, 219.0f));
        CHECK_FALSE(hitTestLayerProxy(bg, 141.0f, 200.0f));
    }
}

TEST_SUITE("Cursor events carry localPosition") {
    TEST_CASE("localPosition is a Vec3 next to worldPosition and screenPosition") {
        QJSEngine engine;
        engine.evaluate(wek::qml_helper::kVecClassesJs);
        QJSValue ev = scenebackend::makeCursorEvent(&engine, 300.0f, 400.0f, 10.0f, 20.0f);
        CHECK(ev.property("localPosition").isUndefined());
        scenebackend::setCursorEventLocal(&engine, ev, 12.5f, -3.0f);
        engine.globalObject().setProperty("ev", ev);
        CHECK(engine.evaluate("ev.localPosition.x").toNumber() == doctest::Approx(12.5));
        CHECK(engine.evaluate("ev.localPosition.y").toNumber() == doctest::Approx(-3.0));
        CHECK(engine.evaluate("ev.localPosition.z").toNumber() == doctest::Approx(0.0));
        // A Vec3, so drag scripts can do vector arithmetic on it.
        CHECK(engine.evaluate("ev.localPosition.add(new Vec3(1,1,0)).x").toNumber() ==
              doctest::Approx(13.5));
        CHECK(engine.evaluate("ev.worldPosition.x").toNumber() == doctest::Approx(300.0));
    }
}

TEST_SUITE("Click propagation (disablepropagation)") {
    TEST_CASE("a press goes from the top down and stops at the first flagged layer") {
        // draw order: background 0, panel 1 (flagged), button 2.
        std::vector<PressHit> hits = { { 0, 0, false }, { 1, 1, true }, { 2, 2, false } };
        auto                  reached = pressPropagation(hits);
        REQUIRE(reached.size() == 2);
        CHECK(reached[0].index == 2); // the button on top first
        CHECK(reached[1].index == 1); // the flagged panel takes it; the background does not
    }

    TEST_CASE("without a flag every layer under the cursor gets it, topmost first") {
        std::vector<PressHit> hits    = { { 0, 3, false }, { 1, 7, false }, { 2, 5, false } };
        auto                  reached = pressPropagation(hits);
        REQUIRE(reached.size() == 3);
        CHECK(reached[0].index == 1);
        CHECK(reached[1].index == 2);
        CHECK(reached[2].index == 0);
    }

    TEST_CASE("a flagged top layer keeps the press to itself") {
        std::vector<PressHit> hits    = { { 0, 0, false }, { 1, 9, true } };
        auto                  reached = pressPropagation(hits);
        REQUIRE(reached.size() == 1);
        CHECK(reached[0].index == 1);
    }

    TEST_CASE("equal orders keep the caller's order; unknown order sorts last") {
        // Two shader-value targets of one layer share its order.
        std::vector<PressHit> hits    = { { 0, -1, false }, { 1, 4, false }, { 2, 4, false } };
        auto                  reached = pressPropagation(hits);
        REQUIRE(reached.size() == 3);
        CHECK(reached[0].index == 1);
        CHECK(reached[1].index == 2);
        CHECK(reached[2].index == 0);
    }

    TEST_CASE("nothing under the cursor reaches nothing") {
        CHECK(pressPropagation({}).empty());
    }

    TEST_CASE("the proxy carries the flag and the draw order from the init states") {
        ProxyEnv env("{ 'button': { o:[0,0,0], s:[1,1,1], a:[0,0,0], sz:[10,10], v:true,"
                     "  dp:true, ro:5 },"
                     "  'bg': { o:[0,0,0], s:[1,1,1], a:[0,0,0], sz:[10,10], v:true, ro:0 },"
                     "  'old': { o:[0,0,0], s:[1,1,1], a:[0,0,0], sz:[10,10], v:true } }");
        CHECK(env.engine.evaluate("thisScene.getLayer('button')._state.disablePropagation")
                  .toBool());
        CHECK(env.engine.evaluate("thisScene.getLayer('button')._state.drawOrder").toInt() == 5);
        CHECK_FALSE(
            env.engine.evaluate("thisScene.getLayer('bg')._state.disablePropagation").toBool());
        CHECK(env.engine.evaluate("thisScene.getLayer('bg')._state.drawOrder").toInt() == 0);
        // Init states written before this change have neither key.
        CHECK(env.engine.evaluate("thisScene.getLayer('old')._state.drawOrder").toInt() == -1);
    }
}
