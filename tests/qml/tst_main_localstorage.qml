import QtQuick
import QtTest
import Helpers 1.0

// SceneScript localStorage per wallpaper and per screen (we-manager
// localstorage patch), through the REAL main.qml (ScreenRig): with
// LocalStoragePerWallpaper off, the loaded scene is told nothing (both
// SceneViewer properties keep the values the library gives them, so the
// renderer keeps its cache files); on, it gets the switch and the screen
// whose LOCATION_SCREEN values it holds.  What the renderer then reads and
// writes is test_SceneLocalStoragePerWallpaper.cpp's.
TestCase {
    id: tc
    name: "Main_LocalStorage"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }

    function _loader(rig) { return rig._find(rig.mainItem, o => typeof o.dropOutgoing === "function"); }
    function _player(rig) {
        const l = _loader(rig);
        return l && l.item ? rig._find(l.item, o => typeof o.setAcceptMouse === "function") : null;
    }
    function _rig(config) {
        failOnWarning(/wallpaper is not defined/);
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: "111",
                    WallpaperSource: "file:///tmp/fakelib/steamapps/workshop/content/431960/111/scene.pkg+scene" };
        for (const k in (config || {})) c[k] = config[k];
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080), initialConfig: c });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        return rig;
    }

    function test_off_the_scene_is_told_nothing() {
        const rig = _rig();
        const bg = rig.background();
        bg.storageScreen = "DP-1";
        compare(bg.localStoragePerWallpaper, false, "off without the key");
        compare(_player(rig).storagePerWallpaper, false);
        compare(_player(rig).storageScreen, "", "off, not even the screen's name reaches the renderer");
        rig.destroy();
    }

    function test_on_the_scene_gets_the_switch_and_its_screen() {
        const rig = _rig({ LocalStoragePerWallpaper: true });
        const bg = rig.background();
        compare(bg.storageScreen, bg.Screen.name || "", "this screen's connector unless something else names one");
        bg.storageScreen = "DP-2";
        compare(_player(rig).storagePerWallpaper, true);
        compare(_player(rig).storageScreen, "DP-2");
        // A renderer that stands for another screen is given that screen.
        bg.storageScreen = "HDMI-A-1";
        compare(_player(rig).storageScreen, "HDMI-A-1");
        // Live, both ways.
        rig.setConfig({ LocalStoragePerWallpaper: false });
        tryCompare(_player(rig), "storagePerWallpaper", false);
        compare(_player(rig).storageScreen, "");
        rig.setConfig({ LocalStoragePerWallpaper: true });
        tryCompare(_player(rig), "storagePerWallpaper", true);
        compare(_player(rig).storageScreen, "HDMI-A-1");
        rig.destroy();
    }
}
