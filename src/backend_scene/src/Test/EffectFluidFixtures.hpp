#pragma once

// Fixture scene for the fluid simulation tests (we-manager fluid-effect
// patch): a stand-in for Wallpaper Engine's fluid simulation effect, on a
// layer with a size of its own, on a full-screen layer, and on a layer whose
// scene entry sets the effect's lighting, next to a layer with a plain
// effect.  This file uses nothing the patch adds (nor the fluid-targets
// patch), so the same dump can be written by a tree without either
// (test_EffectFluidDefaults.cpp).
//
// The stand-in has the shape of effects/fluidsimulation/effect.json as far
// as it is known here (NOTES.md D48, unit 7b: second-hand, from an open
// reimplementation that reads the shipped file): the nine targets with their
// names, formats, "fit" and "scale", the twenty entries of the pass list in
// their order with the targets they draw into, the conditions on the normal
// pass, on its target and on two binds of the combine pass, the two swaps at
// the end, and the two functions.  The binds of the other passes are what a
// fluid solver of that pass order needs, the start colours are the test's
// own, and one shader stands for all of Wallpaper Engine's.

#include "EffectFeedbackFixtures.hpp"

namespace wek::test::effect_fluid
{

using namespace wallpaper;
using effect_feedback::dump;
using effect_feedback::firstDifference;
using effect_feedback::linesOf;

// Five samplers, as the combine pass binds up to slot 4, and the two combos
// the effect file's conditions name.
inline constexpr const char* kFluidFrag = R"GLSL(
// [COMBO] {"material":"Lighting","combo":"LIGHTING","type":"options","default":0}
// [COMBO] {"material":"Rendering","combo":"RENDERING","type":"options","default":0}
uniform sampler2D g_Texture0;
uniform sampler2D g_Texture1;
uniform sampler2D g_Texture2;
uniform sampler2D g_Texture3;
uniform sampler2D g_Texture4;
void main() {
    gl_FragColor = vec4(1.0);
}
)GLSL";
inline constexpr const char* kFluidMaterial = R"({
    "passes": [ { "shader": "_fluid", "blending": "normal", "textures": [] } ]
})";

inline constexpr const char* kFluidEffect = R"({
    "name": "fluidsimulation",
    "fbos": [
        { "name": "_rt_SmokeVelocity1", "format": "rg1616f", "fit": 256, "unique": true, "clear": "0 0 0 0" },
        { "name": "_rt_SmokeVelocity2", "format": "rg1616f", "fit": 256, "unique": true, "clear": "0 0 0 0" },
        { "name": "_rt_SmokePressure1", "format": "r16f", "fit": 256, "unique": true, "clear": "0.5 0 0 0" },
        { "name": "_rt_SmokePressure2", "format": "r16f", "fit": 256, "unique": true, "clear": "0.5 0 0 0" },
        { "name": "_rt_SmokeDivergence", "format": "r16f", "fit": 256, "unique": true, "clear": "0 0 0 0" },
        { "name": "_rt_SmokeCurl", "format": "r16f", "fit": 256, "unique": true, "clear": "0 0 0 0" },
        { "name": "_rt_SmokeDye1", "format": "rgba_backbuffer", "scale": 2, "unique": true, "clear": "0 0 0 1" },
        { "name": "_rt_SmokeDye2", "format": "rgba_backbuffer", "scale": 2, "unique": true, "clear": "0 0 0 1" },
        { "name": "_rt_SmokeNormal", "format": "rgba_backbuffer", "scale": 2, "unique": true,
          "conditions": [ { "LIGHTING": 1 } ] }
    ],
    "passes": [
        { "material": "materials/_fluid.json", "target": "_rt_SmokeCurl",
          "bind": [ { "name": "_rt_SmokeVelocity1", "index": 0 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokeVelocity2",
          "bind": [ { "name": "_rt_SmokeVelocity1", "index": 0 }, { "name": "_rt_SmokeCurl", "index": 1 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokeDivergence",
          "bind": [ { "name": "_rt_SmokeVelocity2", "index": 0 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokePressure2",
          "bind": [ { "name": "_rt_SmokePressure1", "index": 0 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokePressure1",
          "bind": [ { "name": "_rt_SmokePressure2", "index": 0 }, { "name": "_rt_SmokeDivergence", "index": 1 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokePressure2",
          "bind": [ { "name": "_rt_SmokePressure1", "index": 0 }, { "name": "_rt_SmokeDivergence", "index": 1 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokePressure1",
          "bind": [ { "name": "_rt_SmokePressure2", "index": 0 }, { "name": "_rt_SmokeDivergence", "index": 1 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokePressure2",
          "bind": [ { "name": "_rt_SmokePressure1", "index": 0 }, { "name": "_rt_SmokeDivergence", "index": 1 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokePressure1",
          "bind": [ { "name": "_rt_SmokePressure2", "index": 0 }, { "name": "_rt_SmokeDivergence", "index": 1 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokePressure2",
          "bind": [ { "name": "_rt_SmokePressure1", "index": 0 }, { "name": "_rt_SmokeDivergence", "index": 1 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokePressure1",
          "bind": [ { "name": "_rt_SmokePressure2", "index": 0 }, { "name": "_rt_SmokeDivergence", "index": 1 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokePressure2",
          "bind": [ { "name": "_rt_SmokePressure1", "index": 0 }, { "name": "_rt_SmokeDivergence", "index": 1 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokePressure1",
          "bind": [ { "name": "_rt_SmokePressure2", "index": 0 }, { "name": "_rt_SmokeDivergence", "index": 1 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokeVelocity1",
          "bind": [ { "name": "_rt_SmokePressure1", "index": 0 }, { "name": "_rt_SmokeVelocity2", "index": 1 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokeVelocity2",
          "bind": [ { "name": "_rt_SmokeVelocity1", "index": 0 }, { "name": "_rt_SmokeVelocity1", "index": 1 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokeDye2",
          "bind": [ { "name": "_rt_SmokeVelocity2", "index": 0 }, { "name": "_rt_SmokeDye1", "index": 1 },
                    { "name": "previous", "index": 2 } ] },
        { "material": "materials/_fluid.json", "target": "_rt_SmokeNormal",
          "conditions": [ { "LIGHTING": 1 } ],
          "bind": [ { "name": "_rt_SmokeDye2", "index": 0 } ] },
        { "material": "materials/_fluid.json",
          "bind": [ { "name": "previous", "index": 0 }, { "name": "_rt_SmokeDye2", "index": 1 },
                    { "name": "_rt_SmokeNormal", "index": 2, "conditions": [ { "LIGHTING": 1 } ] },
                    { "name": "_rt_SmokeVelocity2", "index": 4,
                      "conditions": { "RENDERING": { "op": "ge", "value": 3 } } } ] },
        { "command": "swap", "source": "_rt_SmokeVelocity1", "target": "_rt_SmokeVelocity2" },
        { "command": "swap", "source": "_rt_SmokeDye1", "target": "_rt_SmokeDye2" }
    ],
    "functions": {
        "clearVelocity": { "action": "clear", "fbos": [ "_rt_SmokeVelocity1", "_rt_SmokeVelocity2" ] },
        "clearDye": { "action": "clear", "fbos": [ "_rt_SmokeDye1", "_rt_SmokeDye2" ] }
    }
})";

// The entries of the effect's pass list, by what they do.
inline constexpr int kCurl = 0, kVorticity = 1, kDivergence = 2, kPressureClear = 3, kPressureFirst = 4,
                     kPressureLast = 12, kGradient = 13, kAdvection = 14, kAdvectionDye = 15, kNormal = 16,
                     kCombine = 17, kFluidEntries = 20;

// An image wider than the simulation's grid, so "fit" shows.
inline constexpr const char* kWideImageJson = R"({
    "material": "materials/_plain.json",
    "width": 640,
    "height": 360
})";

// A full-screen image, as a layer that covers the wallpaper is.
inline constexpr const char* kFullImageJson = R"({
    "material": "materials/_plain.json",
    "fullscreen": true
})";

// The fixture's layers, by object id (the ids effect_feedback::dump walks).
// kOdd's effect file is the fluid's with entries in "functions" that are not
// of the shape known (oddFluidEffect below).
// kLit's scene entry has one pass entry per line of the effect's pass list,
// swaps included, and sets LIGHTING on the normal and the combine pass and
// RENDERING 3 on the combine pass.
inline constexpr int kPlain = 10, kFluid = 20, kFull = 30, kLit = 40, kOdd = 50;

inline std::string layerJson(int id) {
    std::string effects;
    const char* model = "models/_plain.json";
    switch (id) {
    case kPlain: effects = R"({ "id": 11, "name": "tint", "visible": true, "file": "effects/tint.json" })"; break;
    case kFluid:
        model   = "models/_wide.json";
        effects = R"({ "id": 21, "name": "fluidsimulation", "visible": true, "file": "effects/fluid.json" })";
        break;
    case kFull:
        model   = "models/_full.json";
        effects = R"({ "id": 31, "name": "fluidsimulation", "visible": true, "file": "effects/fluid.json" })";
        break;
    case kOdd:
        model   = "models/_wide.json";
        effects = R"({ "id": 51, "name": "fluidsimulation", "visible": true, "file": "effects/fluid_odd.json" })";
        break;
    case kLit: {
        model = "models/_wide.json";
        std::string passes;
        for (int i = 0; i < kFluidEntries; i++) {
            passes += i == 0 ? "" : ", ";
            passes += i == kNormal    ? R"({ "combos": { "LIGHTING": 1 } })"
                      : i == kCombine ? R"({ "combos": { "LIGHTING": 1, "RENDERING": 3 } })"
                                      : "{}";
        }
        effects = R"({ "id": 41, "name": "fluidsimulation", "visible": true, "file": "effects/fluid.json",
                       "passes": [ )" + passes + " ] }";
        break;
    }
    }
    return R"({ "id": )" + std::to_string(id) + R"(, "name": "layer)" + std::to_string(id) +
           R"(", "image": ")" + model + R"(", "origin": "640 360 0", "scale": "1 1 1",
               "angles": "0 0 0", "visible": true, "effects": [ )" +
           effects + " ] }";
}

inline std::string sceneJson(const std::vector<int>& layers) {
    std::string objects;
    for (int id : layers) objects += (objects.empty() ? "" : ",\n") + layerJson(id);
    return R"({ "camera": { "eye": "0 0 1", "center": "0 0 0", "up": "0 1 0" },
  "general": { "clearcolor": "0 0 0", "orthogonalprojection": { "width": 1280, "height": 720 } },
  "objects": [
)" + objects + "\n] }";
}

// The stand-in's file with two entries more in "functions": one whose action
// is a number, one with an action other than a clear.
inline std::string oddFluidEffect() {
    std::string       text(kFluidEffect);
    const std::string at = R"("functions": {)";
    const auto        i  = text.find(at);
    REQUIRE(i != std::string::npos);
    text.insert(i + at.size(), R"(
        "broken": { "action": 1, "fbos": [ "_rt_SmokeDye1" ] },
        "resize": { "action": "resize" },)");
    return text;
}

inline std::unique_ptr<fs::VFS> mount() {
    return makeAssetsVfsWith({ { "/shaders/_fx.vert", kTrivialVert },
                               { "/shaders/_fx.frag", effect_feedback::kTwoSamplerFrag },
                               { "/materials/_fx.json", effect_feedback::kTwoSamplerMaterial },
                               { "/shaders/_fluid.vert", kTrivialVert },
                               { "/shaders/_fluid.frag", kFluidFrag },
                               { "/materials/_fluid.json", kFluidMaterial },
                               { "/models/_wide.json", kWideImageJson },
                               { "/models/_full.json", kFullImageJson },
                               { "/effects/tint.json", effect_feedback::kTintEffect },
                               { "/effects/fluid.json", kFluidEffect },
                               { "/effects/fluid_odd.json", oddFluidEffect() } });
}

inline std::shared_ptr<Scene> parse(WPSceneParser& parser, const std::vector<int>& layers) {
    ensureGlslangInit();
    auto                vfs = mount();
    audio::SoundManager sm;
    WPUserProperties    props {};
    auto scene = parser.Parse("effect_fluid", sceneJson(layers), *vfs, sm, props);
    REQUIRE(scene != nullptr);
    return scene;
}

} // namespace wek::test::effect_fluid
