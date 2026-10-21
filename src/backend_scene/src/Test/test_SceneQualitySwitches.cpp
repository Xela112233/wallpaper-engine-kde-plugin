#include <doctest.h>

// What each scene quality switch of the we-manager scene-quality patch takes
// away from a parsed scene and its render graph, and that it takes nothing
// else: the dump of SceneQualityFixtures.hpp with a switch set, compared line
// by line with the dump without it.  (test_SceneQualityDefaults.cpp compares
// the dump without any switch with the tree before the patch.)

#include "SceneQualityFixtures.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

using namespace wek::test::scene_quality;
using wallpaper::SceneQuality;

namespace
{

struct Difference {
    std::vector<std::string> gone;  // lines only the first dump has
    std::vector<std::string> added; // lines only the second dump has
};

// Lines as a multiset: the dumps list equal pass lines more than once.
Difference difference(const std::string& before, const std::string& after) {
    Difference d;
    d.gone  = linesOf(before);
    d.added = linesOf(after);
    for (auto it = d.gone.begin(); it != d.gone.end();) {
        auto hit = std::find(d.added.begin(), d.added.end(), *it);
        if (hit == d.added.end()) {
            ++it;
        } else {
            d.added.erase(hit);
            it = d.gone.erase(it);
        }
    }
    return d;
}

bool has(const std::string& text, std::string_view needle) { return text.find(needle) != std::string::npos; }

int count(const std::vector<std::string>& lines, std::string_view needle) {
    return (int)std::count_if(lines.begin(), lines.end(), [needle](const std::string& l) {
        return has(l, needle);
    });
}

SceneQuality without(SceneQuality q = {}) { return q; }

SceneQuality volumetricsOff() {
    SceneQuality q;
    q.volumetrics = SceneQuality::Volumetrics::Off;
    return q;
}

SceneQuality reflectionsOff() {
    SceneQuality q;
    q.reflections = SceneQuality::Reflections::Off;
    return q;
}

SceneQuality flatMsaaOff() {
    SceneQuality q;
    q.flatMsaa = SceneQuality::FlatMsaa::Off;
    return q;
}

// The dump minus its first line, which is the MSAA request.
std::string afterFirstLine(const std::string& text) { return text.substr(text.find('\n') + 1); }

std::string firstLine(const std::string& text) { return text.substr(0, text.find('\n')); }

// g_Texture0Resolution of the node with this id, as LoadMaterial left it.
std::vector<float> texture0Resolution(wallpaper::Scene& scene, wallpaper::i32 id) {
    std::vector<wallpaper::SceneNode*> todo { scene.sceneGraph.get() };
    while (! todo.empty()) {
        auto* node = todo.back();
        todo.pop_back();
        if (node->ID() == id && node->Mesh() != nullptr && node->Mesh()->Material() != nullptr) {
            const auto& values = node->Mesh()->Material()->customShader.constValues;
            const auto  it     = values.find("g_Texture0Resolution");
            if (it == values.end()) return {};
            std::vector<float> out;
            for (size_t i = 0; i < it->second.size(); i++) out.push_back(it->second[i]);
            return out;
        }
        for (auto& child : node->GetChildren()) todo.push_back(child.get());
    }
    return {};
}

} // namespace

TEST_SUITE("SceneQuality.switches") {
    TEST_CASE("volumetric lighting off: the light's chain, its passes and its three targets go, "
              "and nothing else") {
        // MsaaMode 1: the renderer draws the volumetric chain only without
        // multisampling (emitVolumetricChain), so that is where its passes are.
        for (const auto& f : { volumetric2d(), reflective3d() }) {
            CAPTURE(std::string(f.name));
            const auto d = difference(dump(f, 1, without()), dump(f, 1, volumetricsOff()));
            REQUIRE(d.added.size() == 1);
            CHECK(d.added[0] == "volumetrics enabled=0 lights=0 quality=2 blur=0 combine=0");
            // the summary line, five passes (back, front, two blurs, combine), three targets
            CHECK(d.gone.size() == 9);
            CHECK(count(d.gone, "volumetrics enabled=1 lights=1") == 1);
            CHECK(count(d.gone, "pass volumetrics_") == 5);
            CHECK(count(d.gone, "target _rt_volumetrics") == 3);
        }
        // With multisampling the passes were never there; the chain's nodes
        // and targets still go.
        const auto f = volumetric2d();
        const auto d = difference(dump(f, 0, without()), dump(f, 0, volumetricsOff()));
        CHECK(d.added.size() == 1);
        CHECK(d.gone.size() == 4);
        CHECK(count(d.gone, "target _rt_volumetrics") == 3);
        // A scene without a volumetric light is not touched.
        CHECK(dump(flat2d(), 1, volumetricsOff()) == dump(flat2d(), 1, without()));
    }

    TEST_CASE("reflections off: no reflected camera, no reflection passes, no blur chain, no "
              "targets, and nothing else") {
        const auto f = reflective3d();
        for (int mode : { 0, 1 }) {
            CAPTURE(mode);
            const auto d = difference(dump(f, mode, without()), dump(f, mode, reflectionsOff()));
            // the blur summary, and the 4x4 stand-in the mirror samples
            REQUIRE(d.added.size() == 2);
            CHECK(count(d.added, "reflectionBlur nodes=0 outputs=-") == 1);
            CHECK(count(d.added, "target _rt_Reflection 4x4 ") == 1);
            // the camera, the blur summary, one reflection pass per drawn
            // layer that is not the mirror (the wall; the sign draws through
            // its effect camera), two blur passes, the two scene-sized targets
            CHECK(d.gone.size() == 7);
            CHECK(count(d.gone, "camera reflected_perspective") == 1);
            CHECK(count(d.gone, "reflectionBlur nodes=2") == 1);
            CHECK(count(d.gone, "pass refl_") == 1);
            CHECK(count(d.gone, "pass blur_k3") == 2);
            CHECK(count(d.gone, "target _rt_Reflection 1920x1080 ") == 1);
            CHECK(count(d.gone, "target _rt_Reflection_blur 1920x1080 ") == 1);
        }
        // The camera paths still drive the scene's own camera, and the bloom
        // and volumetric chains are as before.
        const std::string off = dump(f, 1, reflectionsOff());
        CHECK(has(off, "camera global perspective fov=50 aspect=1.778 near=1 far=5000 paths=1 fade=1"));
        CHECK(has(off, "bloom enabled=1 nodes=4"));
        CHECK(has(off, "pass volumetrics_combine"));
        // The mirror is still drawn and still names its texture.
        CHECK(has(off, "pass _mirror type=0 output=_rt_default camera=- textures=_rt_Reflection"));
    }

    TEST_CASE("reflections off: a scene without the reflected camera only swaps the two unused "
              "scene-sized targets for the stand-in") {
        for (const auto& f : { flat2d(), still3d(), volumetric2d() }) {
            CAPTURE(std::string(f.name));
            const auto d = difference(dump(f, 0, without()), dump(f, 0, reflectionsOff()));
            CHECK(d.added.size() == 1);
            CHECK(count(d.added, "target _rt_Reflection 4x4 ") == 1);
            CHECK(d.gone.size() == 2);
            CHECK(count(d.gone, "target _rt_Reflection") == 2);
        }
    }

    TEST_CASE("reflections off: the mirror samples a registered target that nothing writes, not "
              "the opaque texture for empty slots") {
        // A slot whose name is no render target gets the 1x1 placeholder,
        // which is opaque (0.5, 0.5, 1, 1): the mirror would show that colour.
        // A registered target larger than 1x1 starts transparent black
        // (TextureCache's GenInitialClearColor) and adds nothing.
        const auto f   = reflective3d();
        auto       off = parse(f, 0, reflectionsOff());
        REQUIRE(off != nullptr);
        REQUIRE(off->renderTargets.count("_rt_Reflection") == 1);
        const auto& rt = off->renderTargets.at("_rt_Reflection");
        CHECK(rt.width == 4);
        CHECK(rt.height == 4);
        CHECK(off->renderTargets.count("_rt_Reflection_blur") == 0);
        // The mirror's slot is resolved as a render target: its resolution
        // uniform is the target's, not the zeros of a missing texture.
        const auto res = texture0Resolution(*off, 41);
        REQUIRE(res.size() == 4);
        CHECK(res[0] == doctest::Approx(4.0f));
        CHECK(res[1] == doctest::Approx(4.0f));
        // Nothing writes it: no pass of the graph has it as its output.
        const std::string text = dump(*off);
        CHECK(text.find("output=_rt_Reflection") == std::string::npos);
        CHECK(has(text, "pass _mirror type=0 output=_rt_default camera=- textures=_rt_Reflection"));
    }

    TEST_CASE("no anti-aliasing on 2D scenes: a flat 2D scene parses as with anti-aliasing Off, "
              "whatever the setting") {
        for (const auto& f : { flat2d(), volumetric2d() }) {
            for (int mode : { 0, 2, 4, 8 }) {
                CAPTURE(std::string(f.name));
                CAPTURE(mode);
                const std::string text = dump(f, mode, flatMsaaOff());
                CHECK(has(firstLine(text), "msaa requested=1 samples=1"));
                // Everything after the request is what MsaaMode 1 gives: also
                // the volumetric passes, which the renderer draws only
                // without multisampling.
                CHECK(afterFirstLine(text) == afterFirstLine(dump(f, 1, without())));
            }
        }
    }

    TEST_CASE("no anti-aliasing on 2D scenes: a 3D scene, and a 2D one with a system drawn in "
              "perspective, keep their anti-aliasing") {
        for (const auto& f : { perspectiveParticles2d(), reflective3d(), still3d() }) {
            for (int mode : { 0, 4 }) {
                CAPTURE(std::string(f.name));
                CAPTURE(mode);
                CHECK(dump(f, mode, flatMsaaOff()) == dump(f, mode, without()));
            }
        }
    }

    TEST_CASE("no anti-aliasing on 2D scenes: WEKDE_MSAA still pins the count") {
        ::setenv("WEKDE_MSAA", "4", 1);
        const std::string pinned = firstLine(dump(flat2d(), 0, flatMsaaOff()));
        ::unsetenv("WEKDE_MSAA");
        CHECK(pinned == "msaa requested=4 samples=4 autoScale=0");
    }

    TEST_CASE("anti-aliasing 8x is a pinned count, and a value nobody defined is still automatic") {
        CHECK(firstLine(dump(reflective3d(), 8, without())) == "msaa requested=8 samples=8 autoScale=0");
        CHECK(firstLine(dump(flat2d(), 8, without())) == "msaa requested=8 samples=8 autoScale=0");
        CHECK(firstLine(dump(flat2d(), 16, without())) == "msaa requested=4 samples=4 autoScale=1");
        CHECK(firstLine(dump(flat2d(), 3, without())) == firstLine(dump(flat2d(), 0, without())));
    }
}
