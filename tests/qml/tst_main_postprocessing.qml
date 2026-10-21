import QtQuick
import QtTest
import Helpers 1.0
import "../../plugin/contents/ui"

// main.qml's post-processing level: the wallpaper's own `postprocessing` (a
// level, or the old on/off switch's true, "ultra"), else the screen's
// PostProcessing, which nothing read before.  The old switch's false is not
// set: the plugin always drew it like no value at all.  The scene gets
// it as SceneViewer.postprocessingOverride ("" = the scene's own setting).
TestCase {
    id: tc
    name: "Main_PostProcessing"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }

    function _player(rig) {
        const l = rig._find(rig.mainItem, o => typeof o.dropOutgoing === "function");
        return l && l.item ? rig._find(l.item, o => typeof o.setAcceptMouse === "function") : null;
    }

    function test_level_from_the_wallpaper_else_the_screen() {
        failOnWarning(/wallpaper is not defined/);
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080) });
        tryVerify(() => rig.background() !== null, 2000);
        rig.setConfig({ SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: "111",
                        WallpaperSource: "file:///tmp/fakelib/steamapps/workshop/content/431960/111/scene.pkg+scene" });
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        const bg = rig.background();
        compare(bg.postProcessing, "", "nothing set: the scene's own");
        rig.setConfig({ PostProcessing: "low" });
        compare(bg.postProcessing, "low", "the screen's level applies to a wallpaper without its own");
        compare(_player(rig).postprocessingOverride, "low");
        const cases = [
            [true, "ultra"], [false, "low"], ["off", "off"], ["medium", "medium"], ["", ""],
            ["bogus", "low"], [7, "low"],
        ];
        for (const [own, want] of cases) {
            bg.curOpt = { postprocessing: own };
            compare(bg.postProcessing, want, "postprocessing " + JSON.stringify(own));
        }
        bg.curOpt = {};
        compare(bg.postProcessing, "low");
        rig.setConfig({ PostProcessing: "nonsense" });
        compare(bg.postProcessing, "", "an unknown screen value is the scene's own");
        rig.destroy();
    }

    // The Wallpapers tab's Post-processing row starts on what a saved value
    // stands for: its combo's model (WallpaperPage.qml) finds the level, and
    // a value that is not set leaves the row's default, the screen's level.
    // (The page itself needs Plasma's components; tst_pages_deep.qml loads it.)
    function _source(rel) {
        const xhr = new XMLHttpRequest();
        xhr.open("GET", Qt.resolvedUrl("../../plugin/contents/ui/" + rel), false);
        xhr.send(null);
        compare(xhr.status, 200, "could not read " + rel);
        return xhr.responseText;
    }
    function test_the_wallpaper_page_row_shows_the_level_a_saved_value_stands_for() {
        const page = _source("page/WallpaperPage.qml");
        verify(/const changed_val = Common\.shownOptionValue\(modelData\.config_key,\s*right_opts\.get_config_val\(modelData\.config_key\)\);/
               .test(page), "the option rows no longer start from Common.shownOptionValue");
        const combo = { model: ["", "off", "low", "medium", "ultra"].map(v => ({ value: v })) };
        const screen = "low";
        const cases = [[true, "ultra"], ["medium", "medium"], ["", ""], [false, screen], ["Ultra", screen],
                       [5, screen], [null, screen]];
        for (const [saved, want] of cases) {
            const shown = Common.shownOptionValue("postprocessing", saved);
            const def_val = shown !== null ? shown : screen;
            compare(combo.model[Common.cbIndexOfValue(combo, def_val)].value, want, JSON.stringify(saved));
        }
        compare(Common.shownOptionValue("volume", 40), 40, "other options as saved");
        compare(Common.shownOptionValue("mute_audio", false), false);
        compare(Common.shownOptionValue("volume", null), null);
    }
}
