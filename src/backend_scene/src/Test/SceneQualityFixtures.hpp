#pragma once

// Fixture scenes and a text dump of what WPSceneParser::Parse and
// sceneToRenderGraph make of them, for the scene quality switches
// (we-manager scene-quality patch, SceneQuality.hpp).
//
// The dump names what a switch can change: the MSAA request, the cameras,
// the lights, the volumetric, reflection-blur and bloom chains, the scene
// graph, the render graph's passes in order with their output, camera and
// textures, and the render targets.  Everything in it is read through names
// the tree had before the patch, so this header and
// test_SceneQualityDefaults.cpp also build in that tree: its dumps are the
// files under tests/fixtures/scene_quality, and the patched tree must give
// the same text with every switch at its default.
//
// Names that hold a node's address (an effect camera, its ping-pong targets)
// are rewritten to @1, @2, ... in the order the scene graph and the pass
// list first name them, so two runs and two builds give the same text.

#include <doctest.h>

#include "SceneParseFixtures.hpp"

#include "Audio/SoundManager.h"
#include "RenderGraph/RenderGraph.hpp"
#include "Scene/Scene.h"
#include "Scene/SceneCamera.h"
#include "Scene/SceneLight.hpp"
#include "Scene/SceneMaterial.h"
#include "Scene/SceneMesh.h"
#include "Scene/SceneNode.h"
#include "SceneQuality.hpp"
#include "VulkanRender/CustomShaderPass.hpp"
#include "VulkanRender/SceneToRenderGraph.hpp"
#include "WPSceneParser.hpp"
#include "WPUserProperties.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wek::test::scene_quality
{

using namespace wallpaper;

struct Fixture {
    const char*                                      name;
    const char*                                      scene_json;
    std::vector<std::pair<std::string, std::string>> assets;
};

// A fragment shader that declares the sampler: LoadMaterial drops a texture
// its shader has no slot for.
inline constexpr const char* kMirrorFrag = R"GLSL(
uniform sampler2D g_Texture0;
void main() {
    gl_FragColor = vec4(1.0);
}
)GLSL";

// A material whose first texture is the planar reflection target, as a
// reflecting floor's material names it.
inline constexpr const char* kReflectingMaterialJson = R"({
    "passes": [{
        "shader": "_mirror",
        "blending": "translucent",
        "textures": ["_rt_Reflection"]
    }]
})";

inline constexpr const char* kReflectingImageJson = R"({
    "material": "materials/floor.json",
    "width": 512,
    "height": 256
})";

// Two camera paths of two keyframes each: what ParseCamera needs to make the
// reflected camera.
inline constexpr const char* kCameraPathsJson = R"({
    "paths": [
        { "duration": 10.0, "transforms": [
            { "timestamp": 0.0, "eye": "0 100 1000", "center": "0 0 0", "up": "0 1 0" },
            { "timestamp": 10.0, "eye": "300 100 900", "center": "0 0 0", "up": "0 1 0" } ] },
        { "duration": 5.0, "transforms": [
            { "timestamp": 0.0, "eye": "300 100 900", "center": "0 0 0", "up": "0 1 0" },
            { "timestamp": 5.0, "eye": "0 100 1000", "center": "0 0 0", "up": "0 1 0" } ] }
    ]
})";

inline constexpr const char* kParticleJson = R"({
    "emitter": [{}], "renderer": [{ "name": "sprite" }],
    "maxcount": 16, "starttime": 0,
    "material": "materials/_plain.json"
})";

// The same, drawn in perspective (flags: bit 2).
inline constexpr const char* kPerspectiveParticleJson = R"({
    "emitter": [{}], "renderer": [{ "name": "sprite" }],
    "maxcount": 16, "starttime": 0, "flags": 4,
    "material": "materials/_plain.json"
})";

// The shaders the built-in chains load by name (volumetric lighting, the
// reflection blur, the LDR bloom): the trivial pair under each name.
inline std::vector<std::pair<std::string, std::string>> chainShaders() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const char* name : { "volumetricsback", "volumetricsfront", "blur_k3", "passthrough",
                              "downsample_quarter_bloom", "downsample_eighth_blur_v", "blur_h_bloom",
                              "combine" }) {
        out.emplace_back(std::string("/shaders/") + name + ".vert", kTrivialVert);
        out.emplace_back(std::string("/shaders/") + name + ".frag", kTrivialFrag);
    }
    return out;
}

inline std::vector<std::pair<std::string, std::string>>
withChainShaders(std::vector<std::pair<std::string, std::string>> extra) {
    auto out = chainShaders();
    out.emplace_back("/shaders/_mirror.vert", kTrivialVert);
    out.emplace_back("/shaders/_mirror.frag", kMirrorFrag);
    for (auto& e : extra) out.push_back(std::move(e));
    return out;
}

// A flat 2D scene: two image layers, one of them with an effect.
inline Fixture flat2d() {
    return { "flat_2d",
             R"JSON(
{
  "camera": { "eye": "0 0 1", "center": "0 0 0", "up": "0 1 0" },
  "general": { "clearcolor": "0 0 0",
               "orthogonalprojection": { "width": 1280, "height": 720 } },
  "objects": [
    { "id": 10, "name": "back", "image": "models/_plain.json",
      "origin": "640 360 0", "scale": "1 1 1", "angles": "0 0 0", "visible": true },
    { "id": 11, "name": "front", "image": "models/_plain.json",
      "origin": "300 200 0", "scale": "1 1 1", "angles": "0 0 0", "visible": true,
      "effects": [ { "id": 12, "name": "tint", "visible": true, "file": "effects/tint.json" } ] }
  ]
}
)JSON",
             withChainShaders({ { "/effects/tint.json", kEffectFileJson } }) };
}

// A 2D scene that is not flat: one particle system is drawn in perspective.
inline Fixture perspectiveParticles2d() {
    return { "perspective_particles_2d",
             R"JSON(
{
  "camera": { "eye": "0 0 1", "center": "0 0 0", "up": "0 1 0" },
  "general": { "clearcolor": "0 0 0",
               "orthogonalprojection": { "width": 1280, "height": 720 } },
  "objects": [
    { "id": 20, "name": "back", "image": "models/_plain.json",
      "origin": "640 360 0", "scale": "1 1 1", "angles": "0 0 0", "visible": true },
    { "id": 21, "name": "sparks", "particle": "particle/sparks.json",
      "origin": "640 360 0", "scale": "1 1 1", "angles": "0 0 0", "visible": true }
  ]
}
)JSON",
             withChainShaders({ { "/particle/sparks.json", kPerspectiveParticleJson } }) };
}

// A 2D scene with one volumetric point light, one plain light and bloom.
inline Fixture volumetric2d() {
    return { "volumetric_2d",
             R"JSON(
{
  "camera": { "eye": "0 0 1", "center": "0 0 0", "up": "0 1 0" },
  "general": { "clearcolor": "0 0 0", "bloom": true,
               "orthogonalprojection": { "width": 1280, "height": 720 } },
  "objects": [
    { "id": 30, "name": "back", "image": "models/_plain.json",
      "origin": "640 360 0", "scale": "1 1 1", "angles": "0 0 0", "visible": true },
    { "id": 31, "name": "fog", "origin": "640 360 100", "scale": "1 1 1", "angles": "0 0 0",
      "light": "lpoint", "color": "1 1 1", "radius": 300.0, "intensity": 1.0,
      "castvolumetrics": true, "density": 5.0, "visible": true },
    { "id": 32, "name": "lamp", "origin": "100 100 100", "scale": "1 1 1", "angles": "0 0 0",
      "light": "lpoint", "color": "1 1 1", "radius": 100.0, "intensity": 1.0, "visible": true },
    { "id": 33, "name": "dust", "particle": "particle/dust.json",
      "origin": "640 360 0", "scale": "1 1 1", "angles": "0 0 0", "visible": true }
  ]
}
)JSON",
             withChainShaders({ { "/particle/dust.json", kParticleJson } }) };
}

// A 3D scene with camera paths (so planar reflections), a reflecting floor,
// a plain layer, a layer with an effect, a volumetric light and bloom.
inline Fixture reflective3d() {
    return { "reflective_3d",
             R"JSON(
{
  "camera": { "eye": "0 100 1000", "center": "0 0 0", "up": "0 1 0",
              "paths": [ "scripts/campaths.json" ] },
  "general": { "clearcolor": "0 0 0", "bloom": true, "camerafade": true,
               "nearz": 1.0, "farz": 5000.0, "fov": 50.0 },
  "objects": [
    { "id": 40, "name": "wall", "image": "models/_plain.json",
      "origin": "0 200 -200", "scale": "1 1 1", "angles": "0 0 0", "visible": true },
    { "id": 41, "name": "floor", "image": "models/floor.json",
      "origin": "0 0 0", "scale": "1 1 1", "angles": "0 0 0", "visible": true },
    { "id": 42, "name": "sign", "image": "models/_plain.json",
      "origin": "200 100 0", "scale": "1 1 1", "angles": "0 0 0", "visible": true,
      "effects": [ { "id": 43, "name": "tint", "visible": true, "file": "effects/tint.json" } ] },
    { "id": 44, "name": "fog", "origin": "0 100 0", "scale": "1 1 1", "angles": "0 0 0",
      "light": "lpoint", "color": "1 1 1", "radius": 300.0, "intensity": 1.0,
      "castvolumetrics": true, "density": 5.0, "visible": true }
  ]
}
)JSON",
             withChainShaders({ { "/effects/tint.json", kEffectFileJson },
                                { "/scripts/campaths.json", kCameraPathsJson },
                                { "/models/floor.json", kReflectingImageJson },
                                { "/materials/floor.json", kReflectingMaterialJson } }) };
}

// A 3D scene without camera paths: the reflection targets are registered,
// but nothing draws into them.
inline Fixture still3d() {
    return { "still_3d",
             R"JSON(
{
  "camera": { "eye": "0 100 1000", "center": "0 0 0", "up": "0 1 0" },
  "general": { "clearcolor": "0 0 0" },
  "objects": [
    { "id": 50, "name": "wall", "image": "models/_plain.json",
      "origin": "0 200 -200", "scale": "1 1 1", "angles": "0 0 0", "visible": true },
    { "id": 51, "name": "floor", "image": "models/floor.json",
      "origin": "0 0 0", "scale": "1 1 1", "angles": "0 0 0", "visible": true }
  ]
}
)JSON",
             withChainShaders({ { "/models/floor.json", kReflectingImageJson },
                                { "/materials/floor.json", kReflectingMaterialJson } }) };
}

inline std::vector<Fixture> allFixtures() {
    return { flat2d(), perspectiveParticles2d(), volumetric2d(), reflective3d(), still3d() };
}

// (WPScene::FromJson reads "general" only when the scene has a "camera"
// block, so the 2D fixtures carry one too.)

// The MsaaMode values the settings page offered before the patch.
inline constexpr int kOldMsaaModes[] = { 0, 1, 2, 4 };

inline std::unique_ptr<fs::VFS> mount(const Fixture& f) {
    auto vfs   = std::make_unique<fs::VFS>();
    auto memfs = std::make_unique<MemFs>();
    memfs->add("/shaders/_t.vert", kTrivialVert);
    memfs->add("/shaders/_t.frag", kTrivialFrag);
    memfs->add("/models/_plain.json", kPlainImageJson);
    memfs->add("/materials/_plain.json", kPlainMaterialJson);
    for (const auto& [path, content] : f.assets) memfs->add(path, content);
    REQUIRE(vfs->Mount("/assets", std::move(memfs)));
    return vfs;
}

// Parse a fixture as SceneWallpaper does: the MSAA setting and the quality
// switches are given to the parser before Parse().
inline std::shared_ptr<Scene> parse(const Fixture& f, int msaa_mode, const SceneQuality& quality) {
    ensureGlslangInit();
    auto                vfs = mount(f);
    audio::SoundManager sm;
    WPUserProperties    props {};
    WPSceneParser       parser;
    parser.SetMsaaMode(msaa_mode);
    parser.SetQuality(quality);
    return parser.Parse(f.name, f.scene_json, *vfs, sm, props);
}

namespace detail
{

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Runs of six or more digits are addresses (getAddr's decimal form); the
// fixtures' ids and sizes have at most four.
inline constexpr size_t kAddressDigits = 6;

inline void collectAddresses(std::string_view text, std::vector<std::string>& seen) {
    for (size_t i = 0; i < text.size();) {
        if (! isDigit(text[i])) {
            i++;
            continue;
        }
        size_t j = i;
        while (j < text.size() && isDigit(text[j])) j++;
        if (j - i >= kAddressDigits) {
            std::string a(text.substr(i, j - i));
            if (std::find(seen.begin(), seen.end(), a) == seen.end()) seen.push_back(a);
        }
        i = j;
    }
}

inline std::string withoutAddresses(std::string_view text, const std::vector<std::string>& seen) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        if (! isDigit(text[i])) {
            out += text[i++];
            continue;
        }
        size_t j = i;
        while (j < text.size() && isDigit(text[j])) j++;
        const std::string run(text.substr(i, j - i));
        if (j - i >= kAddressDigits) {
            const auto it = std::find(seen.begin(), seen.end(), run);
            out += it == seen.end() ? std::string("@?")
                                    : "@" + std::to_string(it - seen.begin() + 1);
        } else {
            out += run;
        }
        i = j;
    }
    return out;
}

inline std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.4g", v);
    return buf;
}

inline std::string joined(const std::vector<std::string>& parts) {
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : ",") + (p.empty() ? std::string("-") : p);
    return out.empty() ? "-" : out;
}

inline void dumpNode(SceneNode* node, int depth, std::string& out) {
    out += std::string((size_t)depth * 2, ' ') + "node id=" + std::to_string(node->ID()) +
           " camera=" + (node->Camera().empty() ? "-" : node->Camera()) +
           " visible=" + std::to_string((int)node->IsVisible());
    if (auto* mesh = node->Mesh(); mesh != nullptr && mesh->Material() != nullptr)
        out += " material=" + mesh->Material()->name + " textures=" + joined(mesh->Material()->textures);
    out += "\n";
    for (const auto& child : node->GetChildren()) dumpNode(child.get(), depth + 1, out);
}

} // namespace detail

// The text described at the top of this file.  Builds the render graph, as
// SceneWallpaper does right after the parse.
inline std::string dump(Scene& scene) {
    using namespace detail;
    std::string graph;
    dumpNode(scene.sceneGraph.get(), 0, graph);

    auto        rgraph = wallpaper::sceneToRenderGraph(scene);
    std::string passes;
    for (auto id : rgraph->topologicalOrder()) {
        auto* pn = rgraph->getPassNode(id);
        if (pn == nullptr) continue;
        passes += "pass " + std::string(pn->name()) + " type=" + std::to_string((int)pn->type());
        if (auto* p = dynamic_cast<vulkan::CustomShaderPass*>(rgraph->getPass(id)); p != nullptr) {
            const auto& d = p->desc();
            passes += " output=" + d.output + " camera=" +
                      (d.camera_override.empty() ? std::string("-") : d.camera_override) +
                      " textures=" + joined(d.textures) +
                      " reflectionDepth=" + std::to_string((int)d.useReflectionDepth);
        }
        passes += "\n";
    }

    std::vector<std::string> addresses;
    collectAddresses(graph, addresses);
    collectAddresses(passes, addresses);
    const auto clean = [&addresses](std::string_view s) {
        return withoutAddresses(s, addresses);
    };

    std::string out;
    out += "msaa requested=" + std::to_string(scene.msaaRequested) + " samples=" +
           std::to_string(scene.msaaSamples) + " autoScale=" + std::to_string((int)scene.msaaAutoScale) + "\n";
    out += "ortho " + std::to_string(scene.ortho[0]) + "x" + std::to_string(scene.ortho[1]) +
           " hdr=" + std::to_string((int)scene.hdrContent) + " skybox=" + std::to_string((int)scene.has_skybox) + "\n";

    std::vector<std::string> lines;
    for (const auto& [name, cam] : scene.cameras) {
        lines.push_back("camera " + clean(name) +
                        (cam->IsPerspective() ? " perspective fov=" + num(cam->Fov()) + " aspect=" + num(cam->Aspect())
                                              : " ortho " + num(cam->Width()) + "x" + num(cam->Height())) +
                        " near=" + num(cam->NearClip()) + " far=" + num(cam->FarClip()) +
                        " paths=" + std::to_string((int)cam->HasPaths()) +
                        " fade=" + std::to_string((int)cam->IsFadeEnabled()) +
                        " reflectY0=" + std::to_string((int)cam->IsReflectY0()) +
                        " effect=" + std::to_string((int)cam->HasImgEffect()) +
                        (scene.activeCamera == cam.get() ? " active" : ""));
    }
    std::sort(lines.begin(), lines.end());
    for (const auto& l : lines) out += l + "\n";

    lines.clear();
    for (const auto& [name, followers] : scene.linkedCameras) {
        std::vector<std::string> f;
        for (const auto& n : followers) f.push_back(clean(n));
        std::sort(f.begin(), f.end());
        lines.push_back("linked " + clean(name) + " <- " + joined(f));
    }
    std::sort(lines.begin(), lines.end());
    for (const auto& l : lines) out += l + "\n";

    for (const auto& light : scene.lights) {
        if (! light) continue;
        out += "light kind=" + std::to_string((int)light->kind()) + " radius=" + num(light->radius()) +
               " castsVolumetrics=" + std::to_string((int)light->castsVolumetrics()) + "\n";
    }

    const auto& vol = scene.volumetricsConfig;
    out += "volumetrics enabled=" + std::to_string((int)vol.enabled) + " lights=" +
           std::to_string(vol.per_light.size()) + " quality=" + std::to_string(vol.quality) +
           " blur=" + std::to_string((int)(vol.blur_h_node != nullptr) + (int)(vol.blur_v_node != nullptr)) +
           " combine=" + std::to_string((int)(vol.combine_node != nullptr)) + "\n";
    out += "reflectionBlur nodes=" + std::to_string(scene.reflectionBlurConfig.nodes.size()) + " outputs=" +
           joined(scene.reflectionBlurConfig.outputs) + "\n";
    out += "bloom enabled=" + std::to_string((int)scene.bloomConfig.enabled) + " nodes=" +
           std::to_string(scene.bloomConfig.nodes.size()) + " outputs=" + joined(scene.bloomConfig.outputs) + "\n";

    out += clean(graph);
    out += clean(passes);

    // After the graph: sceneToRenderGraph registers its copy targets.
    lines.clear();
    for (const auto& [name, rt] : scene.renderTargets) {
        lines.push_back("target " + clean(name) + " " + std::to_string(rt.width) + "x" + std::to_string(rt.height) +
                        " format=" + std::to_string((int)rt.format) + " mipmap=" + std::to_string((int)rt.has_mipmap) +
                        " reuse=" + std::to_string((int)rt.allowReuse) +
                        (rt.bind.enable ? " bind=" + (rt.bind.name.empty() ? std::string("screen") : clean(rt.bind.name)) +
                                              "*" + num(rt.bind.scale)
                                        : std::string()));
    }
    std::sort(lines.begin(), lines.end());
    for (const auto& l : lines) out += l + "\n";
    return out;
}

inline std::string dump(const Fixture& f, int msaa_mode, const SceneQuality& quality) {
    auto scene = parse(f, msaa_mode, quality);
    REQUIRE(scene != nullptr);
    return dump(*scene);
}

inline std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> out;
    size_t                   start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        out.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

} // namespace wek::test::scene_quality
