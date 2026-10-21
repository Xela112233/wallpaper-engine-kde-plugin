#pragma once

// Fixture scene for the effect feedback tests (we-manager fluid-targets
// patch): layers whose effects carry a picture over between frames in each
// way Wallpaper Engine's effect files can say it, next to layers whose
// effects do not, and a text dump of what the parser and the render graph
// make of it.  This file uses nothing the patch adds, so the same dump can be
// written by the tree without the patch (test_EffectFeedbackDefaults.cpp).

#include "RenderGraphTestHelpers.hpp"
#include "SceneParseFixtures.hpp"

#include "Audio/SoundManager.h"
#include "RenderGraph/RenderGraph.hpp"
#include "Scene/Scene.h"
#include "Scene/SceneImageEffectLayer.h"
#include "Scene/SceneMaterial.h"
#include "Scene/SceneMesh.h"
#include "Scene/SceneNode.h"
#include "SpecTexs.hpp"
#include "VulkanRender/CopyPass.hpp"
#include "VulkanRender/CustomShaderPass.hpp"
#include "VulkanRender/SceneToRenderGraph.hpp"
#include "WPSceneParser.hpp"
#include "WPShaderValueUpdater.hpp"
#include "WPUserProperties.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace wek::test::effect_feedback
{

using namespace wallpaper;

// A fragment shader that declares two samplers: the parser clears every
// texture slot of a material that its shader does not declare, and with it
// the targets an effect binds.
inline constexpr const char* kTwoSamplerFrag = R"GLSL(
uniform sampler2D g_Texture0;
uniform sampler2D g_Texture1;
void main() {
    gl_FragColor = vec4(1.0);
}
)GLSL";
inline constexpr const char* kTwoSamplerMaterial = R"({
    "passes": [ { "shader": "_fx", "blending": "normal", "textures": [] } ]
})";

// One pass that only passes the layer on.
inline constexpr const char* kTintEffect = R"({
    "name": "tint",
    "passes": [ { "material": "materials/_fx.json" } ]
})";

// Two targets and Wallpaper Engine's swap: the first pass samples A (what the
// frame before left there) and draws B, the swap exchanges them, the second
// pass shows A.
inline constexpr const char* kTrailEffect = R"({
    "name": "trail",
    "fbos": [ { "name": "_rt_TrailA", "scale": 1, "format": "rgba8888" },
              { "name": "_rt_TrailB", "scale": 1, "format": "rgba8888" } ],
    "passes": [
        { "material": "materials/_fx.json", "target": "_rt_TrailB",
          "bind": [ { "name": "previous", "index": 0 }, { "name": "_rt_TrailA", "index": 1 } ] },
        { "command": "swap", "source": "_rt_TrailA", "target": "_rt_TrailB" },
        { "material": "materials/_fx.json",
          "bind": [ { "name": "previous", "index": 0 }, { "name": "_rt_TrailA", "index": 1 } ] }
    ]
})";

// One target that a pass both samples and draws into, at half size, float.
inline constexpr const char* kStateEffect = R"({
    "name": "state",
    "fbos": [ { "name": "_rt_State", "scale": 2, "format": "rg1616f" } ],
    "passes": [
        { "material": "materials/_fx.json", "target": "_rt_State",
          "bind": [ { "name": "previous", "index": 0 }, { "name": "_rt_State", "index": 1 } ] },
        { "material": "materials/_fx.json",
          "bind": [ { "name": "previous", "index": 0 }, { "name": "_rt_State", "index": 1 } ] }
    ]
})";

// Carried over by a copy placed after the last pass: Keep is sampled first,
// Work is drawn and shown, then Work is copied onto Keep for the next frame.
inline constexpr const char* kCopyBackEffect = R"({
    "name": "copyback",
    "fbos": [ { "name": "_rt_Keep", "scale": 2, "format": "rgba8888" },
              { "name": "_rt_Work", "scale": 2, "format": "rgba8888" } ],
    "passes": [
        { "material": "materials/_fx.json", "target": "_rt_Work",
          "bind": [ { "name": "previous", "index": 0 }, { "name": "_rt_Keep", "index": 1 } ] },
        { "material": "materials/_fx.json",
          "bind": [ { "name": "previous", "index": 0 }, { "name": "_rt_Work", "index": 1 } ] },
        { "command": "copy", "source": "_rt_Work", "target": "_rt_Keep" }
    ]
})";

// The copy back again, with both targets declared float, and with only the
// one that is kept declared float (the copy then joins two formats).  Not in
// the default dump: these two are for the setting on.
inline constexpr const char* kFloatCopyBackEffect = R"({
    "name": "copyfloat",
    "fbos": [ { "name": "_rt_Keep", "scale": 2, "format": "rgba16161616f" },
              { "name": "_rt_Work", "scale": 2, "format": "rgba16161616f" } ],
    "passes": [
        { "material": "materials/_fx.json", "target": "_rt_Work",
          "bind": [ { "name": "previous", "index": 0 }, { "name": "_rt_Keep", "index": 1 } ] },
        { "material": "materials/_fx.json",
          "bind": [ { "name": "previous", "index": 0 }, { "name": "_rt_Work", "index": 1 } ] },
        { "command": "copy", "source": "_rt_Work", "target": "_rt_Keep" }
    ]
})";
inline constexpr const char* kMixedCopyBackEffect = R"({
    "name": "copymixed",
    "fbos": [ { "name": "_rt_Keep", "scale": 2, "format": "rgba16161616f" },
              { "name": "_rt_Work", "scale": 2, "format": "rgba8888" } ],
    "passes": [
        { "material": "materials/_fx.json", "target": "_rt_Work",
          "bind": [ { "name": "previous", "index": 0 }, { "name": "_rt_Keep", "index": 1 } ] },
        { "material": "materials/_fx.json",
          "bind": [ { "name": "previous", "index": 0 }, { "name": "_rt_Work", "index": 1 } ] },
        { "command": "copy", "source": "_rt_Work", "target": "_rt_Keep" }
    ]
})";

// Not feedback: both targets are drawn before they are sampled, and a swap
// and a copy sit in between.
inline constexpr const char* kBlurEffect = R"({
    "name": "blur",
    "fbos": [ { "name": "_rt_BlurA", "scale": 2, "format": "rgba8888" },
              { "name": "_rt_BlurB", "scale": 2, "format": "rgba8888" } ],
    "passes": [
        { "material": "materials/_fx.json", "target": "_rt_BlurA",
          "bind": [ { "name": "previous", "index": 0 } ] },
        { "material": "materials/_fx.json", "target": "_rt_BlurB",
          "bind": [ { "name": "_rt_BlurA", "index": 0 } ] },
        { "command": "swap", "source": "_rt_BlurA", "target": "_rt_BlurB" },
        { "material": "materials/_fx.json",
          "bind": [ { "name": "previous", "index": 0 }, { "name": "_rt_BlurB", "index": 1 } ] }
    ]
})";

// The fixture's layers, by object id.
inline constexpr int kPlain = 10, kTrail = 20, kState = 30, kCopyBack = 40, kBlur = 50;
inline constexpr int kFloatCopyBack = 60, kMixedCopyBack = 70;
inline constexpr int kAllLayers[] = { kPlain, kTrail,         kState,        kCopyBack,
                                      kBlur,  kFloatCopyBack, kMixedCopyBack };

inline std::string layerJson(int id) {
    const char* effects = "";
    switch (id) {
    case kPlain: effects = R"({ "id": 11, "name": "tint", "visible": true, "file": "effects/tint.json" })"; break;
    case kTrail: effects = R"({ "id": 21, "name": "trail", "visible": true, "file": "effects/trail.json" })"; break;
    case kState:
        effects = R"({ "id": 31, "name": "tint", "visible": true, "file": "effects/tint.json" },
                     { "id": 32, "name": "state", "visible": true, "file": "effects/state.json" })";
        break;
    case kCopyBack:
        effects = R"({ "id": 41, "name": "copyback", "visible": true, "file": "effects/copyback.json" })";
        break;
    case kBlur: effects = R"({ "id": 51, "name": "blur", "visible": true, "file": "effects/blur.json" })"; break;
    case kFloatCopyBack:
        effects = R"({ "id": 61, "name": "copyfloat", "visible": true, "file": "effects/copyfloat.json" })";
        break;
    case kMixedCopyBack:
        effects = R"({ "id": 71, "name": "copymixed", "visible": true, "file": "effects/copymixed.json" })";
        break;
    }
    return R"({ "id": )" + std::to_string(id) + R"(, "name": "layer)" + std::to_string(id) +
           R"(", "image": "models/_plain.json", "origin": "640 360 0", "scale": "1 1 1",
               "angles": "0 0 0", "visible": true, "effects": [ )" +
           effects + " ] }";
}

// A 2D scene with the given layers.  (WPScene::FromJson reads "general" only
// when the scene has a "camera" block.)
inline std::string sceneJson(const std::vector<int>& layers) {
    std::string objects;
    for (int id : layers) objects += (objects.empty() ? "" : ",\n") + layerJson(id);
    return R"({ "camera": { "eye": "0 0 1", "center": "0 0 0", "up": "0 1 0" },
  "general": { "clearcolor": "0 0 0", "orthogonalprojection": { "width": 1280, "height": 720 } },
  "objects": [
)" + objects + "\n] }";
}

inline std::unique_ptr<fs::VFS> mount() {
    return makeAssetsVfsWith({ { "/shaders/_fx.vert", kTrivialVert },
                               { "/shaders/_fx.frag", kTwoSamplerFrag },
                               { "/materials/_fx.json", kTwoSamplerMaterial },
                               { "/effects/tint.json", kTintEffect },
                               { "/effects/trail.json", kTrailEffect },
                               { "/effects/state.json", kStateEffect },
                               { "/effects/copyback.json", kCopyBackEffect },
                               { "/effects/copyfloat.json", kFloatCopyBackEffect },
                               { "/effects/copymixed.json", kMixedCopyBackEffect },
                               { "/effects/blur.json", kBlurEffect } });
}

// Parse the fixture's layers with a parser the caller has set up (or left
// alone).
inline std::shared_ptr<Scene> parse(WPSceneParser& parser, const std::vector<int>& layers) {
    ensureGlslangInit();
    auto                vfs = mount();
    audio::SoundManager sm;
    WPUserProperties    props {};
    auto scene = parser.Parse("effect_feedback", sceneJson(layers), *vfs, sm, props);
    REQUIRE(scene != nullptr);
    return scene;
}

namespace detail
{

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Runs of six or more digits are addresses (the parser names cameras and
// targets after them); the fixture's ids and sizes have at most four.
// Each becomes @N, N counting the addresses in the order the dump meets them.
inline std::string withoutAddresses(std::string_view text, std::vector<std::string>& seen) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        if (! isDigit(text[i])) {
            out += text[i++];
            continue;
        }
        size_t j = i;
        while (j < text.size() && isDigit(text[j])) j++;
        const std::string run(text.substr(i, j - i));
        if (j - i >= 6) {
            auto it = std::find(seen.begin(), seen.end(), run);
            if (it == seen.end()) it = seen.insert(seen.end(), run);
            out += "@" + std::to_string(it - seen.begin() + 1);
        } else {
            out += run;
        }
        i = j;
    }
    return out;
}

inline std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : ", ") + (s.empty() ? std::string("-") : s);
    return out;
}

} // namespace detail

// The uniform names a node's shader is asked about and, for a shader that
// declares every one of them, the names written in a frame.
struct UniformTable {
    std::vector<std::string> asked, written;
};
inline UniformTable uniformTable(Scene& scene, SceneNode* node) {
    UniformTable t;
    scene.shaderValueUpdater->InitUniforms(node, [&t](std::string_view name) {
        t.asked.emplace_back(name);
        return true;
    });
    sprite_map_t sprites;
    scene.shaderValueUpdater->UpdateUniforms(
        node, sprites, [&t](std::string_view name, const ShaderValue&) {
            t.written.emplace_back(name);
        });
    return t;
}

// The layers' effect chains, the render graph's passes in the order they run,
// the render targets, and one effect node's uniform table, as text.
inline std::string dump(Scene& scene, rg::RenderGraph& rgraph) {
    std::vector<std::string> seen;
    std::string              out;
    auto                     line = [&](const std::string& s) {
        out += detail::withoutAddresses(s, seen) + "\n";
    };

    line("[effects]");
    SceneNode* first_effect_node = nullptr;
    for (i32 id : kAllLayers) {
        if (scene.nodeEffectLayerMap.count(id) == 0) continue;
        auto* layer = scene.nodeEffectLayerMap.at(id);
        line("layer " + std::to_string(id) + ": " + layer->FirstTarget() + " " +
             layer->SecondTarget());
        for (usize e = 0; e < layer->EffectCount(); e++) {
            auto& eff = layer->GetEffect(e);
            line("  effect " + eff->name + (eff->visible ? "" : " (hidden)"));
            for (const auto& c : eff->commands)
                line(std::string("    ") +
                     (c.cmd == SceneImageEffect::CmdType::Swap ? "swap " : "copy ") + c.src +
                     " -> " + c.dst + " after " + std::to_string(c.afterpos));
            for (const auto& n : eff->nodes) {
                if (first_effect_node == nullptr) first_effect_node = n.sceneNode.get();
                line("    node -> " + n.output + " <- " +
                     detail::join(n.sceneNode->Mesh()->Material()->textures));
            }
        }
    }

    line("[passes]");
    for (auto id : rgraph.topologicalOrder()) {
        auto* pass = rgraph.getPass(id);
        if (auto* csp = dynamic_cast<vulkan::CustomShaderPass*>(pass)) {
            line("draw -> " + csp->desc().output + " <- " + detail::join(csp->desc().textures));
        } else if (auto* cp = dynamic_cast<vulkan::CopyPass*>(pass)) {
            line("copy -> " + cp->desc().dst + " <- " + cp->desc().src);
        } else {
            line("other");
        }
    }

    line("[targets]");
    std::vector<std::string> targets;
    for (const auto& [name, rt] : scene.renderTargets) {
        char buf[256];
        std::snprintf(buf,
                      sizeof buf,
                      " %dx%d reuse=%d depth=%d mip=%d format=%s bind=%d/%s/%d/%.3f",
                      rt.width,
                      rt.height,
                      (int)rt.allowReuse,
                      (int)rt.withDepth,
                      (int)rt.has_mipmap,
                      ToString(rt.format).c_str(),
                      (int)rt.bind.enable,
                      rt.bind.name.c_str(),
                      (int)rt.bind.screen,
                      rt.bind.scale);
        targets.push_back(detail::withoutAddresses(name + buf, seen));
    }
    std::sort(targets.begin(), targets.end());
    for (const auto& t : targets) out += t + "\n";

    line("[uniforms]");
    REQUIRE(first_effect_node != nullptr);
    const auto table = uniformTable(scene, first_effect_node);
    line("asked: " + detail::join(table.asked));
    line("written: " + detail::join(table.written));
    return out;
}

inline std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> lines;
    size_t                   start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            if (start < text.size()) lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

// "line N: got ..., expected ...", or empty when equal.
inline std::string firstDifference(const std::string& got, const std::string& want) {
    const auto a = linesOf(got);
    const auto b = linesOf(want);
    for (size_t i = 0; i < std::max(a.size(), b.size()); i++) {
        const std::string x = i < a.size() ? a[i] : "(nothing)";
        const std::string y = i < b.size() ? b[i] : "(nothing)";
        if (x != y) return "line " + std::to_string(i + 1) + ": got '" + x + "', expected '" + y + "'";
    }
    return {};
}

} // namespace wek::test::effect_feedback
