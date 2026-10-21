import QtQuick
import QtTest
import Helpers 1.0

// Effect feedback (we-manager fluid-targets patch, main.xml EffectFeedback),
// through the REAL main.qml (ScreenRig): off, the scene gets the quality map
// it got before the patch, {textures} and nothing else; on, the map says
// feedback, on the running scene (SceneViewer reloads the scene on a change
// of the map).  What the renderer does with it is the renderer's
// test_EffectFeedback.cpp.
TestCase {
    id: tc
    name: "Main_EffectFeedback"
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

    function test_off_the_scene_gets_the_map_it_got_before() {
        const rig = _rig({ TextureQuality: 1 });
        compare(rig.background().effectFeedback, false);
        compare(JSON.stringify(_player(rig).sceneQuality), JSON.stringify({ textures: 1 }));
        rig.destroy();
    }

    function test_on_reaches_the_running_scene_and_off_takes_it_away_again() {
        const rig = _rig();
        const player = _player(rig);
        compare(JSON.stringify(player.sceneQuality), JSON.stringify({ textures: 0 }));
        rig.setConfig({ EffectFeedback: true });
        tryCompare(rig.background(), "effectFeedback", true);
        compare(_player(rig), player, "the same scene item: the renderer reloads the scene itself");
        compare(JSON.stringify(player.sceneQuality), JSON.stringify({ textures: 0, feedback: 1 }));
        rig.setConfig({ EffectFeedback: false });
        tryCompare(rig.background(), "effectFeedback", false);
        compare(JSON.stringify(player.sceneQuality), JSON.stringify({ textures: 0 }));
        rig.destroy();
    }
}
