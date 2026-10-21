import QtQuick
import QtTest
import Helpers 1.0

// main.qml reads the per-wallpaper scene options from the wallpaper's
// <id>.json (curOpt, via get_opt_value) and the loaded scene gets them:
// mouse_parallax false turns the scene's camera parallax off, pixel_art true
// samples nearest-neighbour.  Absent, they keep today's behaviour.
TestCase {
    id: tc
    name: "Main_SceneOptions"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }

    function _player(rig) {
        const l = rig._find(rig.mainItem, o => typeof o.dropOutgoing === "function");
        return l && l.item ? rig._find(l.item, o => typeof o.setAcceptMouse === "function") : null;
    }

    function test_options_reach_the_scene() {
        failOnWarning(/wallpaper is not defined/);
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080) });
        tryVerify(() => rig.background() !== null, 2000);
        rig.setConfig({ SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: "111",
                        WallpaperSource: "file:///tmp/fakelib/steamapps/workshop/content/431960/111/scene.pkg+scene" });
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        const bg = rig.background();
        const p = _player(rig);
        compare(bg.cameraParallax, true, "parallax stays on without mouse_parallax");
        compare(bg.pixelArt, false);
        compare(p.cameraParallax, true);
        bg.curOpt = { mouse_parallax: false, pixel_art: true };
        compare(bg.cameraParallax, false);
        compare(bg.pixelArt, true);
        compare(p.cameraParallax, false);
        compare(p.pixelArt, true);
        // Only real booleans count: a stray value keeps the default.
        bg.curOpt = { mouse_parallax: 0, pixel_art: "yes" };
        compare(bg.cameraParallax, true);
        compare(bg.pixelArt, false);
        rig.destroy();
    }
}
