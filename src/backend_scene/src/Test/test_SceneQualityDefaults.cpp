#include <doctest.h>

// With no scene quality switch set, the parser and the render graph give
// what the tree without the scene-quality patch gave (we-manager patch,
// SceneQuality.hpp): the fixture scenes of SceneQualityFixtures.hpp, parsed
// with each MsaaMode value the settings page had, against the text files
// under tests/fixtures/scene_quality.
//
// Those files were written by this same file built in the tree without the
// patch (it uses nothing the patch adds):
//   WEK_SCENE_QUALITY_REGEN=1 ./backend_scene_tests -ts='SceneQuality.defaults'
// Never set that variable in the patched tree: it would make the patched
// output the reference.

#include "SceneQualityFixtures.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace wek::test::scene_quality;

namespace
{

std::filesystem::path referenceFile(const Fixture& f, int msaa_mode) {
    return std::filesystem::path(WEK_FIXTURES_DIR) / "scene_quality" /
           (std::string(f.name) + ".msaa" + std::to_string(msaa_mode) + ".txt");
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream      in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// "line N: got ..., the tree without the patch gave ...", or empty when equal.
std::string firstDifference(const std::string& got, const std::string& want) {
    const auto a = linesOf(got);
    const auto b = linesOf(want);
    for (size_t i = 0; i < std::max(a.size(), b.size()); i++) {
        const std::string x = i < a.size() ? a[i] : "(nothing)";
        const std::string y = i < b.size() ? b[i] : "(nothing)";
        if (x != y)
            return "line " + std::to_string(i + 1) + ": got '" + x + "', the tree without the patch gave '" + y + "'";
    }
    return {};
}

} // namespace

TEST_SUITE("SceneQuality.defaults") {
    TEST_CASE("a fixture scene dumps to the same text on every parse") {
        for (const auto& f : allFixtures()) {
            CAPTURE(std::string(f.name));
            const std::string first = dump(f, 1, wallpaper::SceneQuality {});
            CHECK(firstDifference(dump(f, 1, wallpaper::SceneQuality {}), first) == "");
            CHECK(first.find("@?") == std::string::npos);
        }
    }

    TEST_CASE("with no switch set, every fixture scene parses and builds its render graph as the "
              "tree without the patch does, for each anti-aliasing setting it had") {
        const bool regen = std::getenv("WEK_SCENE_QUALITY_REGEN") != nullptr;
        for (const auto& f : allFixtures()) {
            for (int mode : kOldMsaaModes) {
                CAPTURE(std::string(f.name));
                CAPTURE(mode);
                const std::string text = dump(f, mode, wallpaper::SceneQuality {});
                const auto        path = referenceFile(f, mode);
                if (regen) {
                    std::filesystem::create_directories(path.parent_path());
                    std::ofstream(path, std::ios::binary) << text;
                    continue;
                }
                REQUIRE_MESSAGE(std::filesystem::exists(path), "no reference file " << path.string());
                CHECK(firstDifference(text, readFile(path)) == "");
            }
        }
    }
}
