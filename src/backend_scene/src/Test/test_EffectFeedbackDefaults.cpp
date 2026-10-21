#include <doctest.h>

// With the effect feedback setting off (we-manager fluid-targets patch), the
// parser and the render graph make of a scene what the tree without the
// patch made of it: the fixture scene of EffectFeedbackFixtures.hpp, whose
// effects sample their own targets across frames in every way the patch
// treats, against tests/fixtures/effect_feedback/default.txt.
//
// That file was written by this same file built in the tree without the
// patch (it uses nothing the patch adds):
//   WEK_EFFECT_FEEDBACK_REGEN=1 ./backend_scene_tests -ts='EffectFeedback.defaults'
// Never set that variable in the patched tree: it would make the patched
// output the reference.

#include "EffectFeedbackFixtures.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wek::test::effect_feedback;

namespace
{

std::filesystem::path referenceFile() {
    return std::filesystem::path(WEK_FIXTURES_DIR) / "effect_feedback" / "default.txt";
}

std::string defaultDump() {
    wallpaper::WPSceneParser parser; // as SceneWallpaper holds it before any setting arrives
    auto scene  = parse(parser, { kPlain, kTrail, kState, kCopyBack, kBlur });
    auto                     rgraph = wallpaper::sceneToRenderGraph(*scene);
    return dump(*scene, *rgraph);
}

} // namespace

TEST_SUITE("EffectFeedback.defaults") {
    TEST_CASE("the fixture scene dumps to the same text on every parse") {
        CHECK(firstDifference(defaultDump(), defaultDump()) == "");
    }

    TEST_CASE("with the setting off, effects that sample their own targets across frames are "
              "parsed and put in the render graph as the tree without the patch does") {
        const std::string text = defaultDump();
        const auto        path = referenceFile();
        if (std::getenv("WEK_EFFECT_FEEDBACK_REGEN") != nullptr) {
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
}
