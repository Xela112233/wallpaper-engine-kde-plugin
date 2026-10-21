import QtQuick
import QtTest
import Helpers 1.0

// Texture resolution and the scene's own count of its video memory
// (we-manager texture-quality patch), through the REAL main.qml (ScreenRig):
// TextureQuality reaches the loaded scene as SceneViewer.sceneQuality, and
// the scene's vramMiB after its first frame reaches the VramSampler as its
// estimate, kept through a stop and dropped once a video loads.  Which mip
// levels a texture loses is tst_scenequality.cpp's; the estimate's use is
// tst_vrampolicy.cpp's.
TestCase {
    id: tc
    name: "Main_TextureQuality"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }

    function _scenePath(id) {
        return "file:///tmp/fakelib/steamapps/workshop/content/431960/" + id + "/scene.pkg+scene";
    }
    function _loader(rig) { return rig._find(rig.mainItem, o => typeof o.dropOutgoing === "function"); }
    function _player(rig) {
        const l = _loader(rig);
        return l && l.item ? rig._find(l.item, o => typeof o.setAcceptMouse === "function") : null;
    }
    function _rig(config) {
        failOnWarning(/wallpaper is not defined/);
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: "111",
                    WallpaperSource: _scenePath("111") };
        for (const k in (config || {})) c[k] = config[k];
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080), initialConfig: c });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        return rig;
    }

    function test_the_texture_resolution_reaches_the_scene() {
        const rig = _rig();
        compare(rig.background().textureQuality, 0, "Full without the key");
        compare(_player(rig).sceneQuality.textures, 0);
        rig.setConfig({ TextureQuality: 1 });
        tryCompare(rig.background(), "textureQuality", 1);
        compare(_player(rig).sceneQuality.textures, 1);
        rig.setConfig({ TextureQuality: 2 });
        compare(_player(rig).sceneQuality.textures, 2);
        rig.destroy();
    }

    function test_the_scenes_count_is_the_samplers_estimate_through_a_stop() {
        const rig = _rig({ VramStop: true });
        const bg = rig.background();
        const w = bg.vramWatch;
        verify(w !== null);
        compare(w.estimateMiB, 0, "nothing drawn yet: no estimate");
        _player(rig).vramMiB = 900;
        compare(bg.vramEstimateMiB, 900);
        compare(w.estimateMiB, 900);
        w.stopWanted = true;
        tryVerify(() => bg.stopped === true, 2000);
        compare(w.estimateMiB, 900, "the stop unloads the scene, the estimate stays for the reload");
        w.stopWanted = false;
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000);
        compare(w.estimateMiB, 900);
        // A video says nothing about itself: no scene count stands for it.
        rig.setConfig({ WallpaperWorkShopId: "222",
                        WallpaperSource: "file:///tmp/fakelib/steamapps/workshop/content/431960/222/video.mp4+video" });
        tryCompare(w, "estimateMiB", 0);
        rig.destroy();
    }
}
