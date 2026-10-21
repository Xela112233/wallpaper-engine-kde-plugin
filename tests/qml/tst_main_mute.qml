import QtQuick
import QtTest
import Helpers 1.0

// The we-manager mute patch through the REAL main.qml (ScreenRig): the
// unsaved global mute (WekControl.SetGlobalMute, ToggleGlobalMute) and the
// automatic one (SetAutoMuted) silence the wallpaper over its own sound
// setting and write nothing to the config; AudioOutput off mutes every type
// and tells the scene to open no sound device; MuteStatus names why.  The
// values, the toggle and the lease are WekControl's, process-wide
// (tst_wekcontrol.cpp); the fade itself is tst_common's; the backends' hard
// mute is theirs.
TestCase {
    id: tc
    name: "Main_Mute"
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
    function _status(rig) { return JSON.parse(rig.playlistController().muteStatus()); }
    function _rig(config) {
        failOnWarning(/wallpaper is not defined/);
        failOnWarning(/Unable to assign \[undefined\]/);
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: "111", WallpaperSource: _scenePath("111"),
                    MuteAudio: false };
        for (const k in (config || {})) c[k] = config[k];
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080), initialConfig: c });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        return rig;
    }

    function test_the_global_mute_is_not_saved_and_wins_over_the_wallpapers_own_sound() {
        const rig = _rig();
        const bg = rig.background();
        const pc = rig.playlistController();
        compare(bg.mute, false);
        bg.curOpt = ({ mute_audio: false });          // the wallpaper's own options unmute it
        pc.setGlobalMute(true);
        compare(bg.mute, true, "the global mute lost to the wallpaper's own sound setting");
        compare(rig.ctx.configuration.MuteAudio, false, "the global mute was written to the config");
        compare(_status(rig).reasons, ["global"]);
        tryVerify(() => _player(rig).muted === true, 2000, "the scene was never muted");
        pc.setGlobalMute(false);
        compare(bg.mute, false);
        compare(_player(rig).muted, false, "the unmute did not lift the scene's mute at once");
        rig.destroy();
    }

    function test_the_automatic_mute_holds_until_it_is_ended() {
        const rig = _rig();
        const bg = rig.background();
        const pc = rig.playlistController();
        pc.setAutoMuted(true);
        compare(bg.mute, true);
        compare(_status(rig).reasons, ["auto"]);
        pc.setGlobalMute(true);
        compare(_status(rig).reasons, ["global", "auto"]);
        pc.setAutoMuted(false);
        compare(bg.mute, true, "ending the automatic mute lifted the global one");
        pc.setGlobalMute(false);
        compare(bg.mute, false);
        compare(rig.ctx.configuration.MuteAudio, false);
        rig.destroy();
    }

    function test_audio_output_off_mutes_every_wallpaper_and_closes_the_scenes_device() {
        const rig = _rig({ AudioOutput: false });
        const bg = rig.background();
        compare(bg.audioOutput, false);
        compare(bg.mute, true, "AudioOutput off did not mute");
        compare(_player(rig).audioOutput, false, "the scene was not told to open no device");
        const st = _status(rig);
        compare(st.audioOutput, false);
        compare(st.reasons, ["output off"]);
        rig.setConfig({ AudioOutput: true });
        compare(bg.mute, false);
        compare(_player(rig).audioOutput, true);
        rig.destroy();
    }

    function test_mute_status_names_the_setting_and_the_window() {
        const rig = _rig({ MuteAudio: true });
        compare(_status(rig).reasons, ["setting"]);
        compare(_status(rig).muted, true);
        rig.background().curOpt = ({ mute_audio: true });
        compare(_status(rig).reasons, ["wallpaper"]);
        rig.background().curOpt = ({ mute_audio: false });
        compare(_status(rig).reasons, []);
        compare(_status(rig).muted, false);
        rig.destroy();
    }
}
