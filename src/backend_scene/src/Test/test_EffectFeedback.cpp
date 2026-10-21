#include <doctest.h>

// Effect feedback (we-manager fluid-targets patch, SceneQuality::feedback):
// what the parser and the render graph make of effects that carry a picture
// over between frames once the setting is on, followed over several frames
// with the same moves the renderer makes after each frame; and the pointer's
// last position and the frame time the shader value updater supplies.  The
// setting off is test_EffectFeedbackDefaults.cpp's.

#include "EffectFeedbackFixtures.hpp"

#include "Scene/SceneCamera.h"
#include "SceneQuality.hpp"
#include "VulkanRender/FeedbackMoves.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>

using namespace wek::test::effect_feedback;
using namespace wallpaper;

namespace
{

struct Parsed {
    std::shared_ptr<Scene>           scene;
    std::unique_ptr<rg::RenderGraph> rgraph;
    std::string                      text;
};

Parsed parsed(bool feedback, const std::vector<int>& layers) {
    WPSceneParser parser;
    SceneQuality  q;
    q.feedback = feedback;
    parser.SetQuality(q);
    Parsed p;
    p.scene  = parse(parser, layers);
    p.rgraph = sceneToRenderGraph(*p.scene);
    p.text   = dump(*p.scene, *p.rgraph);
    return p;
}

// The renderer's slots, without Vulkan: each render target name has a slot,
// each slot an image (a number), and what was last drawn into an image is
// remembered as "frame/pass".
struct FakeSlot {
    std::optional<int> image;
    std::uint32_t      generation { 0 };
};

struct Frames {
    Scene&                          scene;
    rg::RenderGraph&                rgraph;
    std::map<std::string, FakeSlot> slots;
    std::map<int, std::string>      drawn; // image -> what holds it
    int                             frame { 0 };

    struct Draw {
        std::vector<std::string> sampled; // per texture: what the image held
        std::vector<int>         sampled_images;
        int                      target_image { -1 };
    };
    // Per frame, per node: what its pass sampled and drew into.
    std::vector<std::map<SceneNode*, Draw>> history;

    FakeSlot& slot(const std::string& name) {
        auto it = slots.find(name);
        if (it == slots.end()) {
            it               = slots.emplace(name, FakeSlot {}).first;
            it->second.image = (int)slots.size();
            drawn[*it->second.image] = "empty";
        }
        return it->second;
    }

    void run() {
        history.emplace_back();
        for (auto id : rgraph.topologicalOrder()) {
            auto* pass = rgraph.getPass(id);
            if (auto* csp = dynamic_cast<vulkan::CustomShaderPass*>(pass)) {
                Draw d;
                for (const auto& tex : csp->desc().textures) {
                    if (tex.empty()) {
                        d.sampled.emplace_back();
                        d.sampled_images.push_back(-1);
                        continue;
                    }
                    const int image = *slot(tex).image;
                    d.sampled.push_back(drawn.at(image));
                    d.sampled_images.push_back(image);
                }
                d.target_image    = *slot(csp->desc().output).image;
                drawn[d.target_image] =
                    std::to_string(frame) + "/" + std::to_string(csp->desc().node->ID()) + "/" +
                    std::to_string(history.back().size());
                history.back()[csp->desc().node] = d;
            } else if (auto* cp = dynamic_cast<vulkan::CopyPass*>(pass)) {
                drawn[*slot(cp->desc().dst).image] = drawn.at(*slot(cp->desc().src).image);
            }
        }
        // What VulkanRender does once a frame's passes are recorded.
        std::vector<std::optional<int>> scratch;
        for (const auto& named : scene.feedbackMoves) {
            std::vector<std::pair<FakeSlot*, FakeSlot*>> moves;
            for (const auto& [taker, giver] : named) moves.emplace_back(&slot(taker), &slot(giver));
            REQUIRE(vulkan::MovesArePermutation<FakeSlot>(moves));
            vulkan::MoveSlotImages<FakeSlot, std::optional<int>>(moves, scratch);
        }
        frame++;
    }
};

// The nodes of a layer's effect, in pass order.
std::vector<SceneNode*> nodesOf(Scene& scene, int layer, usize effect) {
    std::vector<SceneNode*> nodes;
    for (const auto& n : scene.nodeEffectLayerMap.at(layer)->GetEffect(effect)->nodes)
        nodes.push_back(n.sceneNode.get());
    return nodes;
}

// The lines of a dump's [effects] section that belong to one layer.
std::vector<std::string> layerBlock(const std::string& text, int layer) {
    std::vector<std::string> out;
    bool                     in = false;
    for (const auto& l : linesOf(text)) {
        if (l.rfind("layer ", 0) == 0) in = l.rfind("layer " + std::to_string(layer) + ":", 0) == 0;
        else if (l.rfind("[", 0) == 0) in = false;
        if (in) out.push_back(l);
    }
    return out;
}

std::vector<std::string> section(const std::string& text, const std::string& name) {
    std::vector<std::string> out;
    bool                     in = false;
    for (const auto& l : linesOf(text)) {
        if (l.rfind("[", 0) == 0) {
            in = l == "[" + name + "]";
            continue;
        }
        if (in) out.push_back(l);
    }
    return out;
}

} // namespace

TEST_SUITE("EffectFeedback.scene") {
    TEST_CASE("on, an effect's first pass samples what the frame before drew, and no pass samples "
              "the image it draws into") {
        auto   p = parsed(true, { kPlain, kTrail, kState, kCopyBack, kBlur });
        // WEK_EFFECT_FEEDBACK_SHOW=1 prints what the scene becomes with the
        // setting on, in the form of tests/fixtures/effect_feedback/default.txt.
        if (std::getenv("WEK_EFFECT_FEEDBACK_SHOW") != nullptr) std::puts(p.text.c_str());
        Frames f { *p.scene, *p.rgraph };
        for (int i = 0; i < 5; i++) f.run();

        struct Case {
            int   layer;
            usize effect;
        };
        for (const Case& c : { Case { kTrail, 0 }, Case { kState, 1 }, Case { kCopyBack, 0 } }) {
            CAPTURE(c.layer);
            const auto nodes = nodesOf(*p.scene, c.layer, c.effect);
            REQUIRE(nodes.size() == 2u);
            for (int frame = 0; frame < 5; frame++) {
                CAPTURE(frame);
                const auto& first = f.history[(usize)frame].at(nodes[0]);
                const auto& show  = f.history[(usize)frame].at(nodes[1]);
                REQUIRE(first.sampled.size() == 2u);
                REQUIRE(show.sampled.size() == 2u);
                // Texture 1 of the first pass is the target carried over.
                if (frame == 0) {
                    CHECK(first.sampled[1] == "empty");
                } else {
                    const std::string before = std::to_string(frame - 1) + "/" +
                                               std::to_string(nodes[0]->ID()) + "/";
                    CHECK(first.sampled[1].rfind(before, 0) == 0);
                }
                // The second pass shows what the first just drew.
                const std::string now =
                    std::to_string(frame) + "/" + std::to_string(nodes[0]->ID()) + "/";
                CHECK(show.sampled[1].rfind(now, 0) == 0);
                for (const auto& d : { first, show })
                    for (int image : d.sampled_images) CHECK(image != d.target_image);
            }
        }
    }

    TEST_CASE("on, a target that changes places is read from one image and drawn into the other, "
              "the other way round in the next frame") {
        auto   p = parsed(true, { kTrail, kState, kCopyBack });
        Frames f { *p.scene, *p.rgraph };
        for (int i = 0; i < 4; i++) f.run();
        for (auto [layer, effect] : { std::pair<int, usize> { kTrail, 0 }, { kState, 1 } }) {
            CAPTURE(layer);
            auto* first = nodesOf(*p.scene, layer, effect)[0];
            const auto& f0 = f.history[0].at(first);
            const auto& f1 = f.history[1].at(first);
            const auto& f2 = f.history[2].at(first);
            CHECK(f0.sampled_images[1] != f0.target_image);
            CHECK(f1.sampled_images[1] == f0.target_image);
            CHECK(f1.target_image == f0.sampled_images[1]);
            CHECK(f2.sampled_images[1] == f0.sampled_images[1]);
            CHECK(f2.target_image == f0.target_image);
        }
        // The copy-back effect has nothing to exchange: it samples and draws
        // the same two images every frame, and its copy runs after its last
        // pass.
        auto* first = nodesOf(*p.scene, kCopyBack, 0)[0];
        CHECK(f.history[1].at(first).sampled_images[1] == f.history[0].at(first).sampled_images[1]);
        CHECK(f.history[1].at(first).target_image == f.history[0].at(first).target_image);
        CHECK(p.scene->feedbackMoves.size() == 2u);
    }

    TEST_CASE("off, the same effects do not carry over: the first pass never sees what it drew") {
        // The renderer the patch leaves alone, followed the same way (one
        // image per name, which is the kindest reading of it): the swap is a
        // copy from source to target, the wrong way round for this effect,
        // and the copy after the last pass never runs.
        auto   p = parsed(false, { kTrail, kCopyBack });
        Frames f { *p.scene, *p.rgraph };
        for (int i = 0; i < 3; i++) f.run();
        CHECK(p.scene->feedbackMoves.empty());
        CHECK(p.scene->feedbackTargets.empty());
        for (int layer : { kTrail, kCopyBack }) {
            CAPTURE(layer);
            auto* first = nodesOf(*p.scene, layer, 0)[0];
            CHECK(f.history[2].at(first).sampled[1] == "empty");
        }
    }

    TEST_CASE("on, effects that carry nothing over are built as with the setting off") {
        // A scene without a feedback effect: the chains, the passes and the
        // render targets are the same; only the two globals are looked for
        // and, by a shader that declares them, written.
        {
            const auto on  = parsed(true, { kPlain, kBlur });
            const auto off = parsed(false, { kPlain, kBlur });
            for (const char* name : { "effects", "passes", "targets" }) {
                CAPTURE(name);
                CHECK(section(on.text, name) == section(off.text, name));
            }
            CHECK(on.scene->feedbackTargets.empty());
            CHECK(on.scene->feedbackMoves.empty());
            auto without = [](std::string line) {
                for (const char* name : { ", g_PointerPositionLast", ", g_Frametime" }) {
                    const auto at = line.find(name);
                    REQUIRE(at != std::string::npos);
                    line.erase(at, std::string(name).size());
                }
                return line;
            };
            const auto u_on = section(on.text, "uniforms"), u_off = section(off.text, "uniforms");
            REQUIRE(u_on.size() == 2u);
            REQUIRE(u_off.size() == 2u);
            CHECK(without(u_on[0]) == u_off[0]);
            CHECK(without(u_on[1]) == u_off[1]);
        }

        // Next to feedback effects: the other layers' chains, and the plain
        // effect that shares a layer with a feedback effect, are the same.
        const auto on  = parsed(true, { kPlain, kTrail, kState, kCopyBack, kBlur });
        const auto off = parsed(false, { kPlain, kTrail, kState, kCopyBack, kBlur });
        CHECK(layerBlock(on.text, kPlain) == layerBlock(off.text, kPlain));
        CHECK(layerBlock(on.text, kBlur) == layerBlock(off.text, kBlur));
        const auto state_on = layerBlock(on.text, kState), state_off = layerBlock(off.text, kState);
        REQUIRE(state_on.size() >= 3u);
        REQUIRE(state_off.size() >= 3u);
        for (usize i = 0; i < 3; i++) CHECK(state_on[i] == state_off[i]); // layer, effect tint, its node

        // And every render target the setting off registers for them is
        // still registered, unchanged.
        const auto        targets_on = section(on.text, "targets");
        std::set<std::string> have(targets_on.begin(), targets_on.end());
        for (const auto& t : section(off.text, "targets")) {
            if (t.find("Trail") != std::string::npos || t.find("State") != std::string::npos ||
                t.find("Keep") != std::string::npos)
                continue;
            CAPTURE(t);
            CHECK(have.count(t) == 1);
        }
        // No target more in all: the twin of the target a pass samples and
        // draws takes the place of the half-size copy made for it before.
        CHECK(targets_on.size() == section(off.text, "targets").size());
    }

    TEST_CASE("on, a copy joins two targets of one format where the effect declares them alike, and "
              "is a blit where it does not") {
        // The copy back with both targets declared float: both are float, the
        // one that is not kept too, so the copy is a copy.  With only the kept
        // one float the copy joins two formats, which vkCmdCopyImage must not
        // be given: CopyPass blits then.
        auto p = parsed(true, { kFloatCopyBack, kMixedCopyBack });
        std::map<std::string, bool> blits; // by the copy's destination: Keep of which layer
        for (auto id : p.rgraph->topologicalOrder()) {
            auto* cp = dynamic_cast<vulkan::CopyPass*>(p.rgraph->getPass(id));
            if (cp == nullptr) continue;
            const auto&    src = p.scene->renderTargets.at(cp->desc().src);
            const auto&    dst = p.scene->renderTargets.at(cp->desc().dst);
            vulkan::RtSlot a, b;
            a.key.format = src.format;
            b.key.format = dst.format;
            CHECK(dst.format == TextureFormat::RGBA16F); // Keep, in both effects
            CHECK(cp->desc().feedback);                  // a feedback effect's command
            CHECK(vulkan::CopyConverts(cp->desc().feedback, &a, &b) == (src.format != dst.format));
            blits[cp->desc().dst] = vulkan::CopyConverts(cp->desc().feedback, &a, &b);
        }
        REQUIRE(blits.size() == 2u);
        int copies = 0, as_blit = 0;
        for (const auto& [name, blit] : blits) (blit ? as_blit : copies)++;
        CHECK(copies == 1);  // copyfloat: Work is float as declared
        CHECK(as_blit == 1); // copymixed: Work is 8-bit as declared
        // Both still carry over.
        Frames f { *p.scene, *p.rgraph };
        for (int i = 0; i < 3; i++) f.run();
        for (int layer : { kFloatCopyBack, kMixedCopyBack }) {
            CAPTURE(layer);
            auto* first = nodesOf(*p.scene, layer, 0)[0];
            CHECK(f.history[2].at(first).sampled[1].rfind("1/" + std::to_string(first->ID()) + "/", 0) == 0);
        }
        // Off, every target is 8-bit as before, and no copy is one that may
        // convert: given two formats all the same, CopyPass would still copy,
        // as before the patch.  (The swap of the blur and the half-size copy
        // of the pass that samples its own target are copies here too.)
        auto off = parsed(false, { kPlain, kTrail, kState, kCopyBack, kBlur, kFloatCopyBack, kMixedCopyBack });
        for (const auto& [name, rt] : off.scene->renderTargets) CHECK(rt.format == TextureFormat::RGBA8);
        vulkan::RtSlot half, eight;
        half.key.format  = TextureFormat::RGBA16F;
        eight.key.format = TextureFormat::RGBA8;
        int off_copies = 0;
        for (auto id : off.rgraph->topologicalOrder()) {
            auto* cp = dynamic_cast<vulkan::CopyPass*>(off.rgraph->getPass(id));
            if (cp == nullptr) continue;
            off_copies++;
            CHECK_FALSE(cp->desc().feedback);
            CHECK_FALSE(vulkan::CopyConverts(cp->desc().feedback, &half, &eight));
        }
        CHECK(off_copies >= 3);
        // On, a copy of an effect that carries nothing over is not one either.
        auto on_plain = parsed(true, { kBlur });
        for (auto id : on_plain.rgraph->topologicalOrder()) {
            if (auto* cp = dynamic_cast<vulkan::CopyPass*>(on_plain.rgraph->getPass(id)))
                CHECK_FALSE(cp->desc().feedback);
        }
    }

    TEST_CASE("on, the targets carried over are kept and float where the effect asks; the rest "
              "stay reusable") {
        auto p = parsed(true, { kTrail, kState, kCopyBack, kBlur });
        int  kept = 0;
        for (const auto& [name, rt] : p.scene->renderTargets) {
            CAPTURE(name);
            const bool keeps = p.scene->feedbackTargets.count(name) != 0;
            kept += keeps ? 1 : 0;
            if (name.find("Trail") != std::string::npos || name.find("State") != std::string::npos ||
                name.find("Keep") != std::string::npos) {
                CHECK(keeps);
                CHECK_FALSE(rt.allowReuse);
            } else {
                CHECK_FALSE(keeps);
            }
            if (name.find("Work") != std::string::npos || name.find("Blur") != std::string::npos)
                CHECK(rt.allowReuse);
            CHECK(rt.format == (name.find("State") != std::string::npos ? TextureFormat::RGBA16F
                                                                         : TextureFormat::RGBA8));
        }
        CHECK(kept == 5); // TrailA, TrailB, State and its twin, Keep
    }
}

namespace
{

struct PointerRig {
    Scene                      scene;
    std::shared_ptr<SceneNode> node { std::make_shared<SceneNode>() };
    WPShaderValueUpdater       updater { &scene };

    struct Frame {
        std::array<float, 2> pos { 0, 0 }, last { 0, 0 };
        float                frametime { -1.0f };
        bool                 has_last { false }, has_frametime { false };
        std::array<float, 2> velocity() const {
            return { (pos[0] - last[0]) / frametime, (pos[1] - last[1]) / frametime };
        }
    };

    explicit PointerRig(bool feedback, float delay = 0.0f) {
        auto cam = std::make_shared<SceneCamera>(1920, 1080, 0.01f, 1000.0f);
        cam->SetDirectLookAt(
            Eigen::Vector3d(0, 0, 1), Eigen::Vector3d(0, 0, 0), Eigen::Vector3d(0, 1, 0));
        scene.cameras["__test__"] = cam;
        scene.activeCamera        = cam.get();
        // The updater writes nothing for a node without a mesh and a material.
        auto          mesh = std::make_shared<SceneMesh>();
        SceneMaterial mat;
        mat.name = "pointer";
        mesh->AddMaterial(std::move(mat));
        node->AddMesh(mesh);
        scene.sceneGraph->AppendChild(node);
        WPCameraParallax p;
        p.delay = delay;
        updater.SetCameraParallax(p);
        updater.SetPointerFeedback(feedback);
        updater.InitUniforms(node.get(), [this](std::string_view name) {
            asked.emplace_back(name);
            return true;
        });
    }

    // One frame as SceneWallpaper runs it: FrameBegin, the pointer the item
    // last reported, the frame's ticks, then the draws.
    Frame frame(std::optional<std::array<double, 2>> pointer, std::vector<double> ticks) {
        updater.FrameBegin();
        if (pointer) input = *pointer;
        updater.MouseInput(input[0], input[1]);
        for (double t : ticks) scene.PassFrameTime(t);
        Frame        f;
        sprite_map_t sprites;
        updater.UpdateUniforms(node.get(), sprites, [&f](std::string_view name, const ShaderValue& v) {
            if (name == G_POINTERPOSITION) f.pos = { v[0], v[1] };
            if (name == G_POINTERPOSITIONLAST) {
                f.last     = { v[0], v[1] };
                f.has_last = true;
            }
            if (name == G_FRAMETIME) {
                f.frametime     = v[0];
                f.has_frametime = true;
            }
        });
        return f;
    }

    std::array<double, 2>    input { 0.5, 0.5 };
    std::vector<std::string> asked;
};

} // namespace

TEST_SUITE("EffectFeedback.pointer") {
    TEST_CASE("the last position is where the pointer was a frame ago, and the frame time is this "
              "frame's") {
        PointerRig r(true);
        const double dt = 1.0 / 60.0;

        // The first frame: nothing has moved.
        auto f = r.frame({ { 0.2, 0.3 } }, { dt });
        REQUIRE(f.has_last);
        REQUIRE(f.has_frametime);
        CHECK(f.pos == f.last);
        CHECK(f.frametime == doctest::Approx(dt));

        // The pointer reaches the cursor from the middle, where it starts.
        f = r.frame({}, { dt });
        CHECK(f.pos[0] == doctest::Approx(0.2f));
        CHECK(f.pos[1] == doctest::Approx(0.3f));
        CHECK(f.last[0] == doctest::Approx(0.5f));
        CHECK(f.last[1] == doctest::Approx(0.5f));

        // No movement: no difference, velocity zero.
        f = r.frame({}, { dt });
        CHECK(f.pos == f.last);
        CHECK(f.velocity()[0] == 0.0f);
        CHECK(f.velocity()[1] == 0.0f);

        // A move shows in the frame after the item reported it, once.
        f = r.frame({ { 0.25, 0.3 } }, { dt });
        CHECK(f.pos == f.last);
        f = r.frame({}, { 0.02 });
        CHECK(f.pos[0] == doctest::Approx(0.25f));
        CHECK(f.last[0] == doctest::Approx(0.2f));
        CHECK(f.frametime == doctest::Approx(0.02));
        CHECK(f.velocity()[0] == doctest::Approx(2.5).epsilon(0.001));
        CHECK(f.velocity()[1] == doctest::Approx(0.0));
        f = r.frame({}, { dt });
        CHECK(f.pos == f.last);

        // A jump (the cursor came back somewhere else) is one large step.
        r.frame({ { 0.9, 0.9 } }, { dt });
        f = r.frame({}, { dt });
        CHECK(f.pos[0] - f.last[0] == doctest::Approx(0.65f));
        CHECK(f.pos[1] - f.last[1] == doctest::Approx(0.6f));

        // The cursor left: no more reports, the pointer stays, velocity zero.
        for (int i = 0; i < 3; i++) {
            f = r.frame({}, { dt });
            CHECK(f.pos == f.last);
            CHECK(f.pos[0] == doctest::Approx(0.9f));
        }

        // A frame drawn in two ticks, and one with the clock standing.
        f = r.frame({}, { 0.016, 0.016 });
        CHECK(f.frametime == doctest::Approx(0.032));
        f = r.frame({}, {});
        CHECK(f.frametime == 0.0f);
    }

    TEST_CASE("with the scene's pointer delay, the last position is still last frame's "
              "g_PointerPosition") {
        PointerRig r(true, 0.5f);
        r.scene.frameTime = 0.1;
        auto before = r.frame({ { 0.9, 0.1 } }, { 0.1 });
        bool moved  = false;
        for (int i = 0; i < 6; i++) {
            const auto f = r.frame({}, { 0.1 });
            CHECK(f.last == before.pos);
            moved  = moved || f.pos != f.last;
            before = f;
        }
        CHECK(moved);
    }

    TEST_CASE("off, the two names are not looked for and not written, even for a shader that "
              "declares them") {
        PointerRig r(false);
        for (const auto& name : r.asked) {
            CHECK(name != G_POINTERPOSITIONLAST);
            CHECK(name != G_FRAMETIME);
        }
        r.frame({ { 0.2, 0.3 } }, { 0.016 });
        const auto f = r.frame({}, { 0.016 });
        CHECK_FALSE(f.has_last);
        CHECK_FALSE(f.has_frametime);
        CHECK(f.pos[0] == doctest::Approx(0.2f)); // g_PointerPosition itself is as before

        PointerRig on(true);
        CHECK(std::count(on.asked.begin(), on.asked.end(), std::string(G_POINTERPOSITIONLAST)) == 1);
        CHECK(std::count(on.asked.begin(), on.asked.end(), std::string(G_FRAMETIME)) == 1);
    }
}
