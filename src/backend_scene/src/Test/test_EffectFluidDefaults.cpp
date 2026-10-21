#include <doctest.h>

// With the fluid simulation setting off (we-manager fluid-effect patch), the
// parser and the render graph make of a scene what the trees before the
// patch made of it: the fixture scene of EffectFluidFixtures.hpp, a stand-in
// for Wallpaper Engine's fluid effect with everything of its file the patch
// reads (conditions, "fit", "clear", functions, a scene entry with one pass
// entry per line), against two reference files.
//
// * tests/fixtures/effect_fluid/default.txt: no setting at all.  Written by
//   this same file built in the tree of the first 26 patches ("3.26"):
//     WEK_EFFECT_FLUID_REGEN=default ./backend_scene_tests -ts='EffectFluid.defaults'
// * tests/fixtures/effect_fluid/feedback.txt: effect feedback on and fluid
//   simulation off.  Written the same way, with WEK_EFFECT_FLUID_REGEN=feedback,
//   in the tree of the first 31 patches (the fluid-targets patch on top).
// Never set that variable in a tree with the fluid-effect patch: it would
// make the patched output the reference.

#include "EffectFluidFixtures.hpp"

#if __has_include("EffectFeedback.hpp")
#    include "SceneQuality.hpp"
#    define WEK_HAS_EFFECT_FEEDBACK 1
#endif

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wek::test::effect_fluid;

namespace
{

std::filesystem::path referenceFile(const char* name) {
    return std::filesystem::path(WEK_FIXTURES_DIR) / "effect_fluid" / (std::string(name) + ".txt");
}

std::string dumpWith(wallpaper::WPSceneParser& parser) {
    auto scene  = parse(parser, { kPlain, kFluid, kFull, kLit });
    auto rgraph = wallpaper::sceneToRenderGraph(*scene);
    return dump(*scene, *rgraph);
}

void compare(const char* name, const std::string& text) {
    const auto  path  = referenceFile(name);
    const char* regen = std::getenv("WEK_EFFECT_FLUID_REGEN");
    if (regen != nullptr && std::string(regen) == name) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
        return;
    }
    REQUIRE_MESSAGE(std::filesystem::exists(path), "no reference file " << path.string());
    std::ifstream      in(path, std::ios::binary);
    std::ostringstream want;
    want << in.rdbuf();
    CHECK(firstDifference(text, want.str()) == "");
}

} // namespace

TEST_SUITE("EffectFluid.defaults") {
    TEST_CASE("the fixture scene dumps to the same text on every parse") {
        wallpaper::WPSceneParser a, b;
        CHECK(firstDifference(dumpWith(a), dumpWith(b)) == "");
    }

    TEST_CASE("with no setting, a scene with the fluid effect is parsed and put in the render graph "
              "as the tree of the first 26 patches does") {
        wallpaper::WPSceneParser parser; // as SceneWallpaper holds it before any setting arrives
        compare("default", dumpWith(parser));
    }

#ifdef WEK_HAS_EFFECT_FEEDBACK
    TEST_CASE("with effect feedback on and fluid simulation off, it is parsed and put in the render "
              "graph as the tree with the fluid-targets patch does") {
        wallpaper::WPSceneParser parser;
        parser.SetQuality(wallpaper::SceneQuality::FromString("textures=0;feedback=1"));
        compare("feedback", dumpWith(parser));
    }
#endif
}
