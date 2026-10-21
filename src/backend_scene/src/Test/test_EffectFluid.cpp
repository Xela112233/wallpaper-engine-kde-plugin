#include <doctest.h>

// Fluid simulation (we-manager fluid-effect patch, SceneQuality::fluid): what
// the parser and the render graph make of the stand-in for Wallpaper Engine's
// fluid effect (EffectFluidFixtures.hpp) once the setting is on, followed
// over several frames with the moves the renderer makes after each; the
// parts of the effect file that only this patch reads; and the cursor and
// button its shaders are given.  The setting off, and effect feedback alone,
// are test_EffectFluidDefaults.cpp's.

#include "EffectFluidFixtures.hpp"

#include "EffectFluid.hpp"
#include "SceneQuality.hpp"
#include "VulkanRender/FeedbackMoves.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <set>

using namespace wek::test::effect_fluid;
using namespace wallpaper;

namespace
{

struct Parsed {
    std::shared_ptr<Scene>           scene;
    std::unique_ptr<rg::RenderGraph> rgraph;
    std::string                      text;
};

// quality: the text the plugin sends ("fluid=1"), or empty for no setting.
Parsed parsed(const std::string& quality, const std::vector<int>& layers) {
    WPSceneParser parser;
    if (! quality.empty()) parser.SetQuality(SceneQuality::FromString(quality));
    Parsed p;
    p.scene  = parse(parser, layers);
    p.rgraph = sceneToRenderGraph(*p.scene);
    p.text   = dump(*p.scene, *p.rgraph);
    return p;
}

// The nodes of a layer's first effect, in pass order.
std::vector<SceneNode*> nodesOf(Scene& scene, int layer) {
    std::vector<SceneNode*> nodes;
    for (const auto& n : scene.nodeEffectLayerMap.at(layer)->GetEffect(0)->nodes)
        nodes.push_back(n.sceneNode.get());
    return nodes;
}

// The renderer's slots without Vulkan: each render target name has a slot,
// each slot an image (a number), and what was last drawn into an image is
// remembered as "<frame>/<place of the pass in its effect>".
struct FakeSlot {
    std::optional<int> image;
    std::uint32_t      generation { 0 };
};

struct Frames {
    Scene&                          scene;
    rg::RenderGraph&                rgraph;
    std::map<SceneNode*, int>       place;
    std::map<std::string, FakeSlot> slots;
    std::map<int, std::string>      drawn;
    int                             frame { 0 };
    // Per frame, per place: what each texture slot of the pass held.
    std::vector<std::map<int, std::vector<std::string>>> sampled;

    Frames(Parsed& p, int layer): scene(*p.scene), rgraph(*p.rgraph) {
        int i = 0;
        for (auto* n : nodesOf(scene, layer)) place[n] = i++;
    }

    FakeSlot& slot(const std::string& name) {
        auto it = slots.find(name);
        if (it == slots.end()) {
            it                       = slots.emplace(name, FakeSlot {}).first;
            it->second.image         = (int)slots.size();
            drawn[*it->second.image] = "empty";
        }
        return it->second;
    }

    void run() {
        sampled.emplace_back();
        for (auto id : rgraph.topologicalOrder()) {
            auto* pass = rgraph.getPass(id);
            if (auto* csp = dynamic_cast<vulkan::CustomShaderPass*>(pass)) {
                std::vector<std::string> held;
                for (const auto& tex : csp->desc().textures)
                    held.push_back(tex.empty() ? std::string() : drawn.at(*slot(tex).image));
                const int target = *slot(csp->desc().output).image;
                const auto at    = place.find(csp->desc().node);
                if (at == place.end()) {
                    drawn[target] = "other";
                    continue;
                }
                drawn[target] = std::to_string(frame) + "/" + std::to_string(at->second);
                sampled.back()[at->second] = held;
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

    std::string at(int f, int pass, usize texture) const { return sampled.at((usize)f).at(pass).at(texture); }
};

std::string what(int frame, int pass) { return std::to_string(frame) + "/" + std::to_string(pass); }

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::vector<std::string> targetLines(const std::string& text, const std::string& name) {
    std::vector<std::string> out;
    bool                     in = false;
    for (const auto& l : linesOf(text)) {
        if (l.rfind("[", 0) == 0) {
            in = l == "[targets]";
            continue;
        }
        if (in && has(l, name)) out.push_back(l);
    }
    return out;
}

} // namespace

TEST_SUITE("EffectFluid") {
    TEST_CASE("on, the fluid's fields carry over: every pass of a frame reads what the solver's "
              "order says, and the first ones read the frame before") {
        auto   p = parsed("fluid=1", { kPlain, kFluid });
        Frames f(p, kFluid);
        for (int i = 0; i < 3; i++) f.run();

        // Without lighting the normal pass is not drawn: the combine pass
        // follows the dye's advection.
        const int combine = kNormal;
        REQUIRE(nodesOf(*p.scene, kFluid).size() == 17);

        // The first frame starts from empty fields.
        CHECK(f.at(0, kCurl, 0) == "empty");
        CHECK(f.at(0, kPressureClear, 0) == "empty");
        CHECK(f.at(0, kAdvectionDye, 1) == "empty");

        for (int n = 1; n < 3; n++) {
            CAPTURE(n);
            // The velocity the frame before advected.
            CHECK(f.at(n, kCurl, 0) == what(n - 1, kAdvection));
            CHECK(f.at(n, kVorticity, 0) == what(n - 1, kAdvection));
            CHECK(f.at(n, kVorticity, 1) == what(n, kCurl));
            CHECK(f.at(n, kDivergence, 0) == what(n, kVorticity));
            // The pressure the frame before ended on, then nine steps that
            // each read the step before and the divergence.
            CHECK(f.at(n, kPressureClear, 0) == what(n - 1, kPressureLast));
            CHECK(f.at(n, kPressureFirst, 0) == what(n, kPressureClear));
            for (int s = kPressureFirst + 1; s <= kPressureLast; s++) {
                CHECK(f.at(n, s, 0) == what(n, s - 1));
                CHECK(f.at(n, s, 1) == what(n, kDivergence));
            }
            CHECK(f.at(n, kGradient, 0) == what(n, kPressureLast));
            CHECK(f.at(n, kGradient, 1) == what(n, kVorticity));
            CHECK(f.at(n, kAdvection, 0) == what(n, kGradient));
            CHECK(f.at(n, kAdvection, 1) == what(n, kGradient));
            // The dye the frame before advected, moved by this frame's velocity.
            CHECK(f.at(n, kAdvectionDye, 0) == what(n, kAdvection));
            CHECK(f.at(n, kAdvectionDye, 1) == what(n - 1, kAdvectionDye));
            CHECK(f.at(n, combine, 1) == what(n, kAdvectionDye));
        }
    }

    TEST_CASE("on, a pass and a bind are there only when the pass's combos meet their conditions; "
              "with effect feedback alone every one of them is") {
        // The shader's own LIGHTING is 0 and RENDERING 0.
        auto on = parsed("fluid=1", { kFluid });
        CHECK(nodesOf(*on.scene, kFluid).size() == 17);
        for (const auto& l : linesOf(on.text))
            if (l.rfind("draw ", 0) == 0 || has(l, "node ")) CHECK_FALSE(has(l, "SmokeNormal"));
        {
            const auto& combine = nodesOf(*on.scene, kFluid).back()->Mesh()->Material()->textures;
            REQUIRE(combine.size() >= 2);
            CHECK(has(combine[1], "SmokeDye"));
            for (usize i = 2; i < combine.size(); i++) CHECK(combine[i].empty());
        }

        auto alone = parsed("feedback=1", { kFluid });
        CHECK(nodesOf(*alone.scene, kFluid).size() == 18);
        {
            const auto& combine = nodesOf(*alone.scene, kFluid).back()->Mesh()->Material()->textures;
            REQUIRE(combine.size() == 5);
            CHECK(has(combine[2], "SmokeNormal"));
            CHECK(has(combine[4], "SmokeVelocity"));
        }
    }

    TEST_CASE("on, a scene entry with one pass entry per line of the pass list sets the combos of "
              "the passes at those lines; before, such an entry set nothing") {
        // kLit: LIGHTING 1 on the normal and the combine pass, RENDERING 3 on
        // the combine pass, in a list of twenty entries for eighteen passes.
        auto on = parsed("fluid=1", { kLit });
        const auto nodes = nodesOf(*on.scene, kLit);
        REQUIRE(nodes.size() == 18);
        const auto& combine = nodes.back()->Mesh()->Material()->textures;
        REQUIRE(combine.size() == 5);
        CHECK(has(combine[2], "SmokeNormal"));
        CHECK(has(combine[4], "SmokeVelocity"));
        Frames f(on, kLit);
        f.run();
        f.run();
        CHECK(f.at(1, kNormal, 0) == what(1, kAdvectionDye));
        CHECK(f.at(1, kCombine, 2) == what(1, kNormal));
        CHECK(f.at(1, kCombine, 4) == what(1, kAdvection));

        // The same scene entry with the setting off sets nothing: the lit
        // layer's effect is drawn with the effect file's own combos, like
        // the unlit layer's (default.txt has both).
        auto alone = parsed("feedback=1", { kFluid, kLit });
        CHECK(nodesOf(*alone.scene, kLit).size() == nodesOf(*alone.scene, kFluid).size());
    }

    TEST_CASE("on, a target with \"fit\" has its larger side at that many pixels; with effect "
              "feedback alone it has the layer's size") {
        // The layer is 640x360; the grid targets ask for fit 256, the dye for
        // half the layer.
        auto on = parsed("fluid=1", { kFluid });
        for (const char* name : { "SmokeVelocity1", "SmokeVelocity2", "SmokePressure1", "SmokePressure2",
                                  "SmokeDivergence", "SmokeCurl" }) {
            const auto lines = targetLines(on.text, name);
            REQUIRE_FALSE(lines.empty());
            for (const auto& l : lines) CHECK_MESSAGE(has(l, " 256x144 "), l);
        }
        for (const auto& l : targetLines(on.text, "SmokeDye")) CHECK_MESSAGE(has(l, " 320x180 "), l);
        CHECK(on.scene->fluidFit.empty());

        auto alone = parsed("feedback=1", { kFluid });
        for (const auto& l : targetLines(alone.text, "SmokeVelocity")) CHECK_MESSAGE(has(l, " 640x360 "), l);
        for (const auto& l : targetLines(alone.text, "SmokeDye")) CHECK_MESSAGE(has(l, " 320x180 "), l);

        // A full-screen layer's targets follow the output: the renderer sizes
        // them when it knows it, from what the parser left it.
        auto full = parsed("fluid=1", { kFull });
        usize grid = 0;
        for (const auto& [name, fit] : full.scene->fluidFit) {
            CHECK(full.scene->renderTargets.count(name) == 1);
            CHECK(fit == 256);
            CHECK_FALSE(has(name, "SmokeDye"));
            grid++;
        }
        CHECK(grid == 6);
        // Sizes in, sizes out: the longer side is the fit, the other keeps
        // the layer's proportions to the nearest pixel.
        using Size = std::pair<std::int32_t, std::int32_t>;
        CHECK(fluid::FitSize(3840, 2160, 256) == Size(256, 144));
        CHECK(fluid::FitSize(1080, 1920, 256) == Size(144, 256));
        CHECK(fluid::FitSize(640, 360, 256) == Size(256, 144));
        CHECK(fluid::FitSize(100, 100, 256) == Size(256, 256)); // a small layer's grid is as large
        CHECK(fluid::FitSize(1000, 333, 64) == Size(64, 21));   // 21.3
        CHECK(fluid::FitSize(1000, 336, 64) == Size(64, 22));   // 21.5 and up goes up
        CHECK(fluid::FitSize(4000, 3, 64) == Size(64, 1));      // never less than a pixel
        CHECK(fluid::FitSize(0, 0, 256) == Size(256, 256));     // a layer without a size
        CHECK(fluid::FitSize(3840, 2160, 1 << 20) == Size(fluid::kLargestFit, fluid::kLargestFit * 9 / 16));
        CHECK(parsed("feedback=1", { kFull }).scene->fluidFit.empty());
    }

    TEST_CASE("on, kept targets start with their colour and a script's function names the kept "
              "targets it clears; with effect feedback alone there is neither") {
        auto        on    = parsed("fluid=1", { kFluid });
        const auto& scene = *on.scene;
        // Kept: the velocity pair, the dye pair and the pressure the frame
        // ends on.  The fixture starts the pressure at 0.5 and the dye opaque.
        usize pressure = 0, dye = 0;
        for (const auto& [name, colour] : scene.fluidStart) {
            CHECK(scene.feedbackTargets.count(name) == 1);
            if (has(name, "SmokePressure1")) {
                CHECK(colour == Scene::FluidColour { 0.5f, 0.0f, 0.0f, 0.0f });
                pressure++;
            } else {
                CHECK(has(name, "SmokeDye"));
                CHECK(colour == Scene::FluidColour { 0.0f, 0.0f, 0.0f, 1.0f });
                dye++;
            }
        }
        CHECK(pressure == 1);
        CHECK(dye == 2);

        const auto& functions = scene.nodeEffectLayerMap.at(kFluid)->GetEffect(0)->fluidFunctions;
        REQUIRE(functions.size() == 2);
        for (const auto& fn : functions) {
            const bool velocity = fn.name == "clearVelocity";
            CHECK((velocity || fn.name == "clearDye"));
            CHECK(fn.clears.size() == 2);
            std::set<std::string> names;
            for (const auto& [name, colour] : fn.clears) {
                names.insert(name);
                CHECK(scene.feedbackTargets.count(name) == 1);
                CHECK(has(name, velocity ? "SmokeVelocity" : "SmokeDye"));
                CHECK(colour[3] == (velocity ? 0.0f : 1.0f));
            }
            CHECK(names.size() == 2);
        }
        const auto states = nlohmann::json::parse(scene.SerializeLayerInitialStates());
        REQUIRE(states.contains("layer20"));
        CHECK(states.at("layer20").at("efn") ==
              nlohmann::json::array({ nlohmann::json::array({ "clearDye", "clearVelocity" }) }));

        auto alone = parsed("feedback=1", { kFluid });
        CHECK(alone.scene->fluidStart.empty());
        CHECK(alone.scene->layerEffectFunctions.empty());
        CHECK(alone.scene->nodeEffectLayerMap.at(kFluid)->GetEffect(0)->fluidFunctions.empty());
        CHECK_FALSE(nlohmann::json::parse(alone.scene->SerializeLayerInitialStates()).at("layer20").contains("efn"));
    }

    TEST_CASE("on, an effect file with functions of a shape that is not known still loads, with "
              "the functions that can be read") {
        auto on = parsed("fluid=1", { kOdd });
        REQUIRE(nodesOf(*on.scene, kOdd).size() == 17);
        const auto& functions = on.scene->nodeEffectLayerMap.at(kOdd)->GetEffect(0)->fluidFunctions;
        REQUIRE(functions.size() == 2);
        for (const auto& fn : functions) CHECK((fn.name == "clearDye" || fn.name == "clearVelocity"));
        // The rest of the effect is the plain stand-in's: its grid and its
        // start colours are there.
        CHECK_FALSE(targetLines(on.text, "SmokeVelocity1").empty());
        for (const auto& l : targetLines(on.text, "SmokeVelocity1")) CHECK_MESSAGE(has(l, " 256x144 "), l);
        CHECK(on.scene->fluidStart.size() == 3);
    }

    TEST_CASE("on, an effect without the fluid's parts, and one that carries nothing over, is built "
              "as effect feedback alone builds it") {
        namespace fb = wek::test::effect_feedback;
        const std::vector<int> layers { fb::kPlain, fb::kTrail, fb::kState, fb::kCopyBack, fb::kBlur };
        auto                   dumpWith = [&](const char* quality) {
            WPSceneParser parser;
            parser.SetQuality(SceneQuality::FromString(quality));
            auto scene  = fb::parse(parser, layers);
            auto rgraph = sceneToRenderGraph(*scene);
            CHECK(scene->fluidFit.empty());
            CHECK(scene->fluidStart.empty());
            CHECK(scene->layerEffectFunctions.empty());
            return fb::dump(*scene, *rgraph);
        };
        CHECK(firstDifference(dumpWith("fluid=1"), dumpWith("feedback=1")) == "");
    }
}

namespace
{

// What one pass's shader is given for the cursor in a frame.
struct Cursor {
    std::array<float, 2> pos { -1.0f, -1.0f }, last { -1.0f, -1.0f };
    std::array<float, 4> state { -1.0f, -1.0f, -1.0f, -1.0f };
    bool                 has_last { false }, has_state { false };
};

Cursor cursorOf(Scene& scene, SceneNode* node) {
    Cursor       c;
    sprite_map_t sprites;
    scene.shaderValueUpdater->UpdateUniforms(node, sprites, [&c](std::string_view name, const ShaderValue& v) {
        if (name == G_POINTERPOSITION) c.pos = { v[0], v[1] };
        if (name == G_POINTERPOSITIONLAST) {
            c.last     = { v[0], v[1] };
            c.has_last = true;
        }
        if (name == G_POINTERSTATE) {
            c.state     = { v[0], v[1], v[2], v[3] };
            c.has_state = true;
        }
    });
    return c;
}

std::vector<std::string> askedOf(Scene& scene, SceneNode* node) {
    std::vector<std::string> asked;
    scene.shaderValueUpdater->InitUniforms(node, [&asked](std::string_view name) {
        asked.emplace_back(name);
        return true;
    });
    return asked;
}

} // namespace

TEST_SUITE("EffectFluid.pointer") {
    TEST_CASE("on, the fluid's passes get the cursor where it is, where it was a frame ago and the "
              "left button; every other pass gets the pointer it always got") {
        auto  p       = parsed("fluid=1", { kPlain, kFluid });
        auto& scene   = *p.scene;
        auto* updater = static_cast<WPShaderValueUpdater*>(scene.shaderValueUpdater.get());
        REQUIRE(updater->FluidPointer());
        SceneNode* fluid = nodesOf(scene, kFluid).front();
        SceneNode* plain = nodesOf(scene, kPlain).front();
        const auto asked = askedOf(scene, fluid);
        CHECK(std::count(asked.begin(), asked.end(), std::string(G_POINTERSTATE)) == 1);
        const auto plainAsked = askedOf(scene, plain);
        CHECK(std::count(plainAsked.begin(), plainAsked.end(), std::string(G_POINTERSTATE)) == 0);

        // One frame as SceneWallpaper runs it: FrameBegin, the position the
        // item last reported to both, then the draws.  A leave told since
        // the frame before comes with the position the cursor had then.
        using Point = std::array<float, 2>;
        auto frame = [&](Point at, bool down = false, std::optional<Point> leftAt = std::nullopt) {
            updater->FrameBegin();
            updater->MouseInput(at[0], at[1]);
            updater->FluidPointerInput(at, down, leftAt.has_value(), leftAt.value_or(Point {}));
            return cursorOf(scene, fluid);
        };

        // The scene loads with the cursor somewhere off the wallpaper: the
        // position is the one the wallpaper was told last (here the middle,
        // where it starts).  Frames pass; then the cursor comes onto the
        // wallpaper.  That position starts a stroke, it is not a move from
        // the middle.
        const Point middle { 0.5f, 0.5f };
        auto        c = frame(middle);
        REQUIRE(c.has_last);
        REQUIRE(c.has_state);
        CHECK(c.last == c.pos);
        CHECK(c.state == std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f });
        for (int i = 0; i < 3; i++) CHECK(frame(middle).last == middle);
        c = frame({ 0.2f, 0.3f });
        CHECK(c.pos == Point { 0.2f, 0.3f });
        CHECK(c.last == c.pos);

        // A move with the button held: there at once, the last position the
        // frame before's.
        c = frame({ 0.6f, 0.1f }, true);
        CHECK(c.pos == Point { 0.6f, 0.1f });
        CHECK(c.last == Point { 0.2f, 0.3f });
        CHECK(c.state == std::array<float, 4> { 1.0f, 0.0f, 1.0f, 0.0f });

        // The plain layer's pass in the same frame: the pointer every shader
        // got before (which follows the cursor with the scene's delay), and
        // no button.
        const auto other = cursorOf(scene, plain);
        CHECK(other.pos == updater->GetMousePosition());
        CHECK(other.pos != c.pos);
        CHECK_FALSE(other.has_state);

        // Standing still: no difference.
        c = frame({ 0.6f, 0.1f });
        CHECK(c.pos == c.last);
        CHECK(c.state[2] == 0.0f);

        // Outside the picture the position is not clamped.
        c = frame({ 1.25f, -0.5f });
        CHECK(c.pos == Point { 1.25f, -0.5f });

        // The cursor leaves while it moves, the usual way: its last move and
        // the leave reach the renderer between the same two frames.  That
        // frame still has the last move as a move.  Frames pass with the
        // position standing where the cursor went out; then the cursor is
        // back somewhere else, and that frame starts a new stroke.
        frame({ 0.3f, 0.5f });
        frame({ 0.1f, 0.5f });
        c = frame({ 0.01f, 0.5f }, false, Point { 0.01f, 0.5f });
        CHECK(c.pos == Point { 0.01f, 0.5f });
        CHECK(c.last == Point { 0.1f, 0.5f });
        for (int i = 0; i < 5; i++) {
            c = frame({ 0.01f, 0.5f });
            CHECK(c.last == c.pos);
        }
        c = frame({ 0.9f, 0.2f });
        CHECK(c.pos == Point { 0.9f, 0.2f });
        CHECK(c.last == c.pos);
        // From there on a move is a move again.
        c = frame({ 0.8f, 0.2f });
        CHECK(c.last == Point { 0.9f, 0.2f });

        // The leave told a frame after the last move: the same.
        c = frame({ 0.8f, 0.2f }, false, Point { 0.8f, 0.2f });
        CHECK(c.last == c.pos);
        frame({ 0.8f, 0.2f });
        c = frame({ 0.1f, 0.9f });
        CHECK(c.last == c.pos);
        c = frame({ 0.2f, 0.9f });
        CHECK(c.last == Point { 0.1f, 0.9f });

        // Out and back in between two frames: the position is already the
        // one after the leave, and starts the stroke.
        c = frame({ 0.7f, 0.3f }, false, Point { 0.2f, 0.9f });
        CHECK(c.pos == Point { 0.7f, 0.3f });
        CHECK(c.last == c.pos);
        c = frame({ 0.75f, 0.3f });
        CHECK(c.last == Point { 0.7f, 0.3f });
    }

    TEST_CASE("a scene that loads with the cursor resting on the wallpaper: its first move starts "
              "the stroke, and the one after it is a move") {
        auto  p       = parsed("fluid=1", { kFluid });
        auto& scene   = *p.scene;
        auto* updater = static_cast<WPShaderValueUpdater*>(scene.shaderValueUpdater.get());
        SceneNode* fluid = nodesOf(scene, kFluid).front();
        askedOf(scene, fluid);
        using Point = std::array<float, 2>;
        auto frame = [&](Point at) {
            updater->FrameBegin();
            updater->MouseInput(at[0], at[1]);
            updater->FluidPointerInput(at, false, false, Point {});
            return cursorOf(scene, fluid);
        };
        // The position of before the load, twice; a reload keeps it.
        CHECK(frame({ 0.3f, 0.7f }).last == Point { 0.3f, 0.7f });
        CHECK(frame({ 0.3f, 0.7f }).last == Point { 0.3f, 0.7f });
        auto c = frame({ 0.8f, 0.2f });
        CHECK(c.last == c.pos);
        c = frame({ 0.7f, 0.2f });
        CHECK(c.last == Point { 0.8f, 0.2f });
    }

    TEST_CASE("with effect feedback alone the fluid's passes get the pointer of that patch, and "
              "the button is not looked for") {
        auto  p       = parsed("feedback=1", { kFluid });
        auto& scene   = *p.scene;
        auto* updater = static_cast<WPShaderValueUpdater*>(scene.shaderValueUpdater.get());
        CHECK_FALSE(updater->FluidPointer());
        SceneNode* fluid = nodesOf(scene, kFluid).front();
        const auto asked = askedOf(scene, fluid);
        CHECK(std::count(asked.begin(), asked.end(), std::string(G_POINTERSTATE)) == 0);
        CHECK(std::count(asked.begin(), asked.end(), std::string(G_POINTERPOSITIONLAST)) == 1);
        updater->FrameBegin();
        updater->MouseInput(0.2, 0.3);
        updater->FrameBegin();
        const auto c = cursorOf(scene, fluid);
        CHECK(c.pos == updater->GetMousePosition());
        CHECK(c.last == updater->GetMousePositionLast());
        CHECK_FALSE(c.has_state);
    }
}

TEST_SUITE("EffectFluid.file") {
    using fluid::Combos;
    using nlohmann::json;

    TEST_CASE("conditions: one alternative is enough, all of its combos must hold, and a combo "
              "the pass does not have holds for nothing") {
        const auto one = fluid::ReadConditions(json::parse(R"({ "conditions": [ { "LIGHTING": 1 } ] })"));
        CHECK(fluid::ConditionsMet(one, Combos { { "LIGHTING", 1 } }));
        CHECK_FALSE(fluid::ConditionsMet(one, Combos { { "LIGHTING", 0 } }));
        CHECK_FALSE(fluid::ConditionsMet(one, Combos {}));

        // A single alternative without the list, a comparison, a lower-case name.
        const auto ge = fluid::ReadConditions(
            json::parse(R"({ "conditions": { "rendering": { "op": "ge", "value": 2 } } })"));
        CHECK(fluid::ConditionsMet(ge, Combos { { "RENDERING", 2 } }));
        CHECK(fluid::ConditionsMet(ge, Combos { { "RENDERING", 3 } }));
        CHECK_FALSE(fluid::ConditionsMet(ge, Combos { { "RENDERING", 1 } }));

        const auto ops = [](const char* op, int value) {
            const auto c = fluid::ReadConditions(json::parse(
                std::string(R"({ "conditions": [ { "X": { "op": ")") + op + R"(", "value": 2 } } ] })"));
            return fluid::ConditionsMet(c, Combos { { "X", value } });
        };
        CHECK((ops("eq", 2) && ! ops("eq", 3)));
        CHECK((ops("gt", 3) && ! ops("gt", 2)));
        CHECK((ops("le", 2) && ! ops("le", 3)));
        CHECK((ops("lt", 1) && ! ops("lt", 2)));
        CHECK_FALSE(ops("between", 2)); // an operator nobody knows holds for nothing

        const auto either = fluid::ReadConditions(
            json::parse(R"({ "conditions": [ { "A": 1, "B": 2 }, { "C": 0 } ] })"));
        CHECK(fluid::ConditionsMet(either, Combos { { "A", 1 }, { "B", 2 } }));
        CHECK_FALSE(fluid::ConditionsMet(either, Combos { { "A", 1 }, { "B", 1 } }));
        CHECK(fluid::ConditionsMet(either, Combos { { "A", 0 }, { "C", 0 } }));

        // Nothing asked.
        CHECK(fluid::ConditionsMet(fluid::ReadConditions(json::parse("{}")), Combos {}));
        CHECK(fluid::ConditionsMet(fluid::ReadConditions(json::parse(R"({ "conditions": [] })")), Combos {}));
    }

    TEST_CASE("the stand-in's file reads as its lines say") {
        const auto x = fluid::Read(json::parse(kFluidEffect));
        REQUIRE(x.passes.size() == 20);
        CHECK(x.MaterialPasses().size() == 18);
        CHECK((x.passes[18].command && x.passes[19].command));
        CHECK(x.Conditional());
        CHECK_FALSE(x.passes[kNormal].conditions.empty());
        REQUIRE(x.passes[kCombine].binds.size() == 4);
        CHECK(x.passes[kCombine].binds[0].empty());
        CHECK_FALSE(x.passes[kCombine].binds[2].empty());
        CHECK_FALSE(x.passes[kCombine].binds[3].empty());
        REQUIRE(x.FindFbo("_rt_SmokeVelocity1") != nullptr);
        CHECK(x.FindFbo("_rt_SmokeVelocity1")->fit == 256);
        CHECK(x.FindFbo("_rt_SmokeDye1")->fit == 0);
        CHECK(x.FindFbo("_rt_SmokeDye1")->clear == std::array<float, 4> { 0.0f, 0.0f, 0.0f, 1.0f });
        CHECK_FALSE(x.FindFbo("_rt_SmokeNormal")->has_clear);
        CHECK(x.functions.size() == 2);

        CHECK(fluid::ReadColour("0.25 0.5 1") == std::array<float, 4> { 0.25f, 0.5f, 1.0f, 0.0f });
        CHECK(fluid::ReadColour("") == std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f });
        CHECK(fluid::ReadColour("1 nan x 1") == std::array<float, 4> { 1.0f, 0.0f, 0.0f, 0.0f });

        // Twenty scene entries for the twenty lines go by line.  Any other
        // number is left to the renderer's own rule: up to eighteen it takes
        // them pass by pass, more it drops.
        const auto byLine = fluid::SceneEntryOfPass(x, 20);
        REQUIRE(byLine.size() == 18);
        for (int i = 0; i < 18; i++) CHECK(byLine[(usize)i] == i);
        CHECK(fluid::SceneEntryOfPass(x, 18).empty());
        CHECK(fluid::SceneEntryOfPass(x, 19).empty());
        CHECK(fluid::SceneEntryOfPass(x, 21).empty());
        CHECK(fluid::SceneEntryOfPass(x, 0).empty());

        // A command between two passes: the entry at its line is nobody's.
        const auto mid = fluid::Read(json::parse(R"({ "passes": [
            { "material": "a" }, { "command": "swap", "source": "x", "target": "y" }, { "material": "b" } ] })"));
        CHECK(fluid::SceneEntryOfPass(mid, 3) == std::vector<std::int32_t> { 0, 2 });
        CHECK(fluid::SceneEntryOfPass(mid, 2).empty());
        // A pass list without commands has nothing to take by line.
        const auto plain = fluid::Read(json::parse(R"({ "passes": [ { "material": "a" }, { "material": "b" } ] })"));
        CHECK(fluid::SceneEntryOfPass(plain, 2).empty());
    }

    TEST_CASE("a file whose parts are not of the shape known costs those parts, never the load") {
        // Each of these is read without an exception, and gives what of it
        // can be taken.
        const auto functions = fluid::Read(json::parse(R"({ "functions": {
            "number": { "action": 1, "fbos": [ "a" ] },
            "object": { "action": { "kind": "clear" } },
            "none": { "fbos": [ "a" ] },
            "text": "clear",
            "other": { "action": "resize", "fbos": [ "a" ] },
            "good": { "action": "clear", "fbos": [ "a", 7, "b" ] } } })"));
        REQUIRE(functions.functions.size() == 1);
        CHECK(functions.functions[0].name == "good");
        CHECK(functions.functions[0].fbos == std::vector<std::string> { "a", "b" });
        CHECK(functions.functionsNotRead.size() == 5);
        CHECK(std::count(functions.functionsNotRead.begin(), functions.functionsNotRead.end(),
                         std::string("other (action 'resize')")) == 1);

        for (const char* text : { R"({ "functions": [ 1, 2 ] })", R"({ "functions": "clearDye" })",
                                  R"({ "fbos": { "name": "a" }, "passes": 3 })",
                                  R"({ "fbos": [ 1, { "name": 2 }, { "name": "a", "fit": "256", "clear": 0 } ] })",
                                  R"({ "passes": [ 1, { "material": "m", "bind": 4, "conditions": 5 },
                                                   { "material": "m", "bind": [ 1, { "conditions": "x" } ],
                                                     "conditions": [ 1, { "A": "one" }, { "B": { "op": 3, "value": 1 } },
                                                                     { "C": { "op": "eq" } } ] } ] })",
                                  "[]", "3" }) {
            CAPTURE(text);
            CHECK_NOTHROW(fluid::Read(json::parse(text)));
        }
        // A condition that cannot be read is one no pass meets.
        const auto odd = fluid::ReadConditions(json::parse(R"({ "conditions": [ { "A": "one" } ] })"));
        CHECK_FALSE(fluid::ConditionsMet(odd, Combos { { "A", 1 } }));
    }
}
