import QtQuick
import QtTest
import Helpers 1.0

// Stop on low video memory, unload after a long pause, and stops requested
// over D-Bus (we-manager vram patch), through the REAL main.qml (ScreenRig).
// The VramSampler stub stands in for the process's memory monitor: the test
// sets its verdict (stopWanted, tight); VramPolicy's rules and the Vulkan
// reads are tst_vrampolicy.cpp's and tst_vramsampler.cpp's.
TestCase {
    id: tc
    name: "Main_Vram"
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
    function _rig(id, config) {
        failOnWarning(/wallpaper is not defined/);
        failOnWarning(/Unable to assign \[undefined\]/);
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: id, WallpaperSource: _scenePath(id) };
        for (const k in (config || {})) c[k] = config[k];
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080), initialConfig: c });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        return rig;
    }
    function _reasons(rig) {
        return JSON.parse(rig.playlistController().stopReasons()).reasons.map(r => r.reason);
    }

    function test_low_video_memory_stops_and_room_again_reloads() {
        const rig = _rig("111", { VramStop: true });
        const bg = rig.background();
        const w = bg.vramWatch;
        verify(w !== null, "VramStop on, but no sampler was made");
        compare(w.enabled, true);
        const first = _loader(rig).item;
        w.freeMiB = 120;
        w.stopWanted = true;
        tryVerify(() => bg.stopped === true, 2000, "never stopped for video memory");
        compare(_loader(rig).item, null);
        compare(_reasons(rig), ["vram"]);
        verify(bg.stopCaption.indexOf("Stopped to free video memory (120 MiB free on Stub GPU)") === 0, bg.stopCaption);
        w.stopWanted = false;
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000, "never loaded again");
        verify(_loader(rig).item !== first);
        compare(_reasons(rig), []);
        rig.destroy();
    }

    function test_the_setting_off_makes_no_sampler_and_turning_it_off_reloads() {
        const rig = _rig("111");
        const bg = rig.background();
        compare(bg.vramWatch, null, "VramStop off: no monitor runs");
        rig.setConfig({ VramStop: true });
        const w = bg.vramWatch;
        verify(w !== null);
        w.stopWanted = true;
        tryVerify(() => bg.stopped === true, 2000);
        rig.setConfig({ VramStop: false });
        compare(w.enabled, false);
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000, "switching VramStop off did not reload");
        rig.destroy();
    }

    function test_a_pick_by_hand_loads_and_the_next_shortage_stops_it() {
        const rig = _rig("111", { VramStop: true });
        const bg = rig.background();
        const w = bg.vramWatch;
        w.stopWanted = true;
        tryVerify(() => bg.stopped === true, 2000);
        rig.setConfig({ WallpaperWorkShopId: "222", WallpaperSource: _scenePath("222") });
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000, "a pick by hand did not load");
        compare(bg.stopCaption, "");
        w.stopWanted = false;                       // room again: the shortage ends
        compare(bg.stopped, false);
        w.stopWanted = true;                        // the next one stops it
        tryVerify(() => bg.stopped === true, 2000, "the next shortage did not stop the picked wallpaper");
        rig.destroy();
    }

    function test_tight_memory_makes_the_handoff_a_cut() {
        const rig = _rig("111", { VramStop: true });
        const bg = rig.background();
        const l = _loader(rig);
        _player(rig).firstFrame();                  // on screen: a handoff would keep it
        rig.setConfig({ WallpaperWorkShopId: "222", WallpaperSource: _scenePath("222") });
        tryVerify(() => l._outgoing !== null, 2000, "with room, the old scene stays until the new one draws");
        compare(l.liveBackends, 2);
        _player(rig).firstFrame();
        tryVerify(() => l._outgoing === null, 2000);
        bg.vramWatch.tight = true;
        compare(bg.overlapAllowed, false);
        rig.setConfig({ WallpaperWorkShopId: "333", WallpaperSource: _scenePath("333") });
        tryVerify(() => _player(rig) !== null && String(_player(rig).source).indexOf("/333/") > 0, 2000);
        compare(l._outgoing, null, "no room for two: the old scene went at once");
        compare(l.liveBackends, 1);
        rig.destroy();
    }

    function test_a_long_pause_unloads_and_its_end_reloads() {
        const rig = _rig("111", { UnloadAfterPausedMin: 5 });
        const bg = rig.background();
        bg.pauseUnloadMs = 150;
        rig.playlistController().pause();
        tryVerify(() => bg.stopped === true, 2000, "a long pause did not unload");
        compare(_reasons(rig), ["paused"]);
        compare(bg.stopCaption, "", "a pause says nothing over its preview");
        rig.playlistController().resume();
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000, "the end of the pause did not reload");
        // Off (0): a pause stays a pause.
        rig.setConfig({ UnloadAfterPausedMin: 0 });
        rig.playlistController().pause();
        wait(400);
        compare(bg.stopped, false);
        rig.playlistController().resume();
        rig.destroy();
    }

    function test_a_request_holds_for_its_lease_and_0_ends_it() {
        const rig = _rig("111");
        const bg = rig.background();
        const pc = rig.playlistController();
        pc.requestStop("Factorio is running", "HDMI-A-9", 30);     // another screen: not this one
        wait(100);
        compare(bg.stopped, false);
        pc.requestStop("Factorio is running", "", 0.4);
        tryVerify(() => bg.stopped === true, 2000);
        compare(bg.stopCaption, "Stopped: Factorio is running");
        const r = JSON.parse(pc.stopReasons());
        compare(r.stopped, true);
        compare(r.reasons[0].reason, "ext:Factorio is running");
        verify(r.reasons[0].leftMs > 0 && r.reasons[0].leftMs <= 400, String(r.reasons[0].leftMs));
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 3000, "the lease ran out but the stop held");
        pc.requestStop("Factorio is running", "", 30);
        tryVerify(() => bg.stopped === true, 2000);
        pc.requestStop("Factorio is running", "", 30);              // renewed: still one reason
        compare(_reasons(rig), ["ext:Factorio is running"]);
        pc.requestStop("Factorio is running", "", 0);
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000, "0 seconds did not end it");
        rig.destroy();
    }
}
