import QtQuick
import QtTest
import Helpers 1.0

// Fluid simulation (we-manager fluid-effect patch, main.xml FluidSimulation),
// through the REAL main.qml (ScreenRig): off, the scene gets the quality map
// it got before the patch ({textures}, or with feedback when EffectFeedback
// alone is on, as the fluid-targets patch left it); on, the map says fluid
// and feedback, whatever EffectFeedback says, and the pointer hook is told to
// pass a hover leave on.  What the renderer does with it is the renderer's
// test_EffectFluid.cpp, what the hook does tests/tst_mousegrabber_leave.cpp.
TestCase {
    id: tc
    name: "Main_FluidSimulation"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }
    Component { id: appletsComp; AppletsLayout {} }

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
        compare(rig.background().fluidSimulation, false);
        compare(JSON.stringify(_player(rig).sceneQuality), JSON.stringify({ textures: 1 }));
        rig.setConfig({ EffectFeedback: true });
        tryCompare(rig.background(), "effectFeedback", true);
        compare(JSON.stringify(_player(rig).sceneQuality), JSON.stringify({ textures: 1, feedback: 1 }),
                "EffectFeedback alone: the map of the fluid-targets patch");
        rig.destroy();
    }

    function test_on_says_fluid_and_feedback_and_off_takes_both_away_again() {
        const rig = _rig();
        const player = _player(rig);
        compare(JSON.stringify(player.sceneQuality), JSON.stringify({ textures: 0 }));
        rig.setConfig({ FluidSimulation: true });
        tryCompare(rig.background(), "fluidSimulation", true);
        compare(rig.background().effectFeedback, false, "the other switch is not flipped");
        compare(_player(rig), player, "the same scene item: the renderer reloads the scene itself");
        compare(JSON.stringify(player.sceneQuality), JSON.stringify({ textures: 0, feedback: 1, fluid: 1 }));
        rig.setConfig({ EffectFeedback: true });
        tryCompare(rig.background(), "effectFeedback", true);
        compare(JSON.stringify(player.sceneQuality), JSON.stringify({ textures: 0, feedback: 1, fluid: 1 }));
        rig.setConfig({ FluidSimulation: false, EffectFeedback: false });
        tryCompare(rig.background(), "fluidSimulation", false);
        tryCompare(rig.background(), "effectFeedback", false);
        compare(JSON.stringify(player.sceneQuality), JSON.stringify({ textures: 0 }));
        rig.destroy();
    }
    // The pointer hook (the MouseGrabber stub, whose hoverLeaveToTarget has no
    // value until something writes one) is told to pass a hover leave on
    // only with the setting on: when it is made, and when the setting
    // changes.  With the setting off nothing writes to it.
    function test_the_pointer_hook_is_told_of_the_leave_only_with_the_setting_on() {
        const applets = appletsComp.createObject(tc);
        const rig = _rig();
        const bg = rig.background();
        verify(bg.doHookMouse(), "the hook is made on the AppletsLayout");
        const first = bg.mouseHooker;
        verify(first !== null);
        compare(first.hoverLeaveToTarget, undefined, "off: never written");
        rig.setConfig({ EffectFeedback: true, TextureQuality: 1 });
        tryCompare(bg, "effectFeedback", true);
        compare(first.hoverLeaveToTarget, undefined, "another setting changes nothing here");

        rig.setConfig({ FluidSimulation: true });
        tryCompare(bg, "fluidSimulation", true);
        compare(bg.mouseHooker, first);
        compare(first.hoverLeaveToTarget, true);
        rig.setConfig({ FluidSimulation: false });
        tryCompare(bg, "fluidSimulation", false);
        compare(first.hoverLeaveToTarget, false);

        // A hook made while the setting is on is told as it is made, and one
        // made after it went off again is not.
        rig.setConfig({ FluidSimulation: true });
        tryCompare(bg, "fluidSimulation", true);
        verify(bg.doHookMouse());
        verify(bg.mouseHooker !== first);
        compare(bg.mouseHooker.hoverLeaveToTarget, true);
        rig.setConfig({ FluidSimulation: false });
        tryCompare(bg, "fluidSimulation", false);
        verify(bg.doHookMouse());
        compare(bg.mouseHooker.hoverLeaveToTarget, undefined);
        applets.parent = null;
        applets.destroy();
        rig.destroy();
    }
}
