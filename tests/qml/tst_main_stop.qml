import QtQuick
import QtTest
import Helpers 1.0

// Per-condition actions and Stop through the REAL main.qml (ScreenRig): a
// window condition (or the battery, with PauseAction 1) set to Stop pauses at
// once, unloads the backend after the grace period, and loads it again when
// the reason ends; Mute silences the wallpaper; StopScope 1 carries a stop to
// every screen of the process.  A user pause never stops.
TestCase {
    id: tc
    name: "Main_Stop"
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
    function _placeholder(rig) {
        return rig._find(rig.mainItem, o => o.names !== undefined && o.tried !== undefined);
    }
    // The TasksModel stub's windows, and the window check run now.
    function _setWindows(rig, windows) {
        const wm = rig.windowModel();
        const tm = rig._find(wm, o => typeof o._windows !== "undefined");
        verify(tm !== null);
        tm._windows = windows;
        wm.updateWindowsinfo();
        const tt = rig._find(wm, o => typeof o.start === "function" && o.interval == 100);
        if (tt) tt.triggered();
    }
    function _game() {
        return [{ isWindow: true, isActive: true, isMaximized: false, isFullScreen: true,
                  isMinimized: false, activities: [], appName: "game" }];
    }
    function _maximized() {
        return [{ isWindow: true, isActive: false, isMaximized: true, isFullScreen: false,
                  isMinimized: false, activities: [], appName: "editor" }];
    }
    // A fullscreen window with FullscreenAction Stop starts the stop; none ends it.
    function _stopBy(rig) { _setWindows(rig, _game()); }
    function _endStop(rig) { _setWindows(rig, []); }
    function _settleWindows(rig) {
        // The resume delay (ResumeTime, 1 ms in WallpaperFake) before a request
        // that ended is dropped.
        const wm = rig.windowModel();
        tryVerify(() => !wm.reqPause && !wm.reqMute && !wm.reqStop, 2000, "window requests never cleared");
    }

    function _rig(id, config) {
        failOnWarning(/wallpaper is not defined/);
        failOnWarning(/Unable to assign \[undefined\]/);
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080) });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        rig.background().stopGraceMs = 150;
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: id, WallpaperSource: _scenePath(id),
                    PauseMode: 5, FullscreenAction: 3 };
        for (const k in (config || {})) c[k] = config[k];
        rig.setConfig(c);
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        return rig;
    }

    function test_stop_waits_for_the_grace_then_unloads_and_reloads() {
        const rig = _rig("111");
        const bg = rig.background();
        const wm = rig.windowModel();
        const first = _loader(rig).item;
        _stopBy(rig);
        compare(bg.ok, false, "a stop request must pause at once");
        compare(_loader(rig).item, first, "unloaded before the grace period");
        tryVerify(() => bg.stopped === true, 2000, "never stopped");
        compare(_loader(rig).item, null);
        compare(bg.nowBackend, "");
        // What is left on screen: the wallpaper's preview, tried by name; the
        // fake library has none, so all three are tried and the background
        // colour shows.
        const ph = _placeholder(rig);
        verify(ph !== null);
        compare(ph.dir, "file:///tmp/fakelib/steamapps/workshop/content/431960/111");
        tryVerify(() => ph.tried === ph.names.length, 2000, "not every preview name was tried");
        compare(ph.visible, false);
        _endStop(rig);
        tryVerify(() => bg.stopped === false, 2000, "never reloaded");
        tryVerify(() => _player(rig) !== null, 2000, "no backend after the stop ended");
        verify(_loader(rig).item !== first);
        compare(String(ph.source), "");
        compare(ph.tried, 0, "the next stop must start from the first name again");
        _settleWindows(rig);
        tryVerify(() => bg.ok === true, 2000);
        rig.destroy();
    }

    function test_a_short_request_never_unloads() {
        const rig = _rig("111");
        const bg = rig.background();
        const wm = rig.windowModel();
        const first = _loader(rig).item;
        _stopBy(rig);
        _endStop(rig);
        _settleWindows(rig);
        wait(300);
        compare(bg.stopped, false);
        compare(_loader(rig).item, first);
        rig.destroy();
    }

    function test_mute_action_silences_without_pausing() {
        const rig = _rig("111", { MuteAudio: false, PauseMode: 2, MaximizedAction: 1 });
        const bg = rig.background();
        const wm = rig.windowModel();
        compare(bg.mute, false);
        _setWindows(rig, _maximized());
        compare(bg.mute, true);
        compare(_player(rig).muted, true);
        compare(bg.ok, true, "Mute must not pause");
        _endStop(rig);
        _settleWindows(rig);
        compare(bg.mute, false);
        rig.destroy();
    }

    function test_window_actions_come_from_the_config() {
        const rig = _rig("111", { FocusAction: 0, MaximizedAction: 1, FullscreenAction: 3 });
        const wm = rig.windowModel();
        compare(wm.focusAction, 0);
        compare(wm.maximizedAction, 1);
        compare(wm.fullscreenAction, 3);
        rig.destroy();
    }

    function test_battery_stops_only_with_PauseAction_1() {
        const rig = _rig("111", { PauseOnBatPower: true, PauseAction: 0 });
        const bg = rig.background();
        const ps = rig.powerSource();
        const ds = rig._find(ps, o => typeof o.connectSource === "function" && typeof o.data !== "undefined");
        verify(ds !== null);
        ds.data = { "Battery": { "Has Battery": true, "State": "Discharging", "Percent": 80 } };
        tryVerify(() => bg.ok === false, 2000);
        wait(300);
        compare(bg.stopped, false, "PauseAction 0 pauses only");
        rig.setConfig({ PauseAction: 1 });
        tryVerify(() => bg.stopped === true, 2000, "PauseAction 1 never stopped");
        ds.data = { "Battery": { "Has Battery": true, "State": "Charging", "Percent": 80 } };
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000, "not reloaded on AC");
        rig.destroy();
    }

    function test_user_pause_never_stops() {
        const rig = _rig("111");
        const bg = rig.background();
        rig.playlistController().pause();
        tryVerify(() => bg.ok === false, 2000);
        wait(300);
        compare(bg.stopped, false);
        verify(_player(rig) !== null);
        rig.playlistController().resume();
        rig.destroy();
    }

    function test_playlist_pick_waits_and_a_pick_by_hand_loads() {
        const rig = _rig("111");
        const bg = rig.background();
        const wm = rig.windowModel();
        _stopBy(rig);
        tryVerify(() => bg.stopped === true, 2000);
        // The playlist rotates while stopped: nothing loads.  (main.qml's
        // setWallpaperFromItem raises _pickedByPlaylist around its two writes;
        // WallpaperFake does not notify single-key writes, so the test raises
        // it around a whole-config write instead.)
        bg._pickedByPlaylist = true;
        rig.setConfig({ WallpaperWorkShopId: "222", WallpaperSource: _scenePath("222") });
        bg._pickedByPlaylist = false;
        compare(bg.stopped, true);
        compare(_loader(rig).item, null);
        verify(bg.wallpaperPath.indexOf("/222/") > 0, bg.wallpaperPath);
        // A wallpaper applied by hand is shown, and stops again after the grace.
        rig.setConfig({ WallpaperWorkShopId: "333", WallpaperSource: _scenePath("333") });
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000, "a pick by hand did not load");
        verify(String(_player(rig).source).indexOf("/333/") > 0);
        tryVerify(() => bg.stopped === true, 2000, "the stop did not come back while its reason stayed");
        _endStop(rig);
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000);
        rig.destroy();
    }

    // we-manager's playlists and virtual desktops write AutoPickSource just
    // before WallpaperSource, with the same value: such a step waits for the
    // reload like the plugin's own playlist, whichever order the two keys
    // arrive in, and a mark left from an earlier step does not hold back a
    // pick by hand.  stopSpy counts every unload and reload in between.
    SignalSpy { id: stopSpy; signalName: "stoppedChanged" }
    SignalSpy { id: stopSpyB; signalName: "stoppedChanged" }
    function _autoStep(rig, id) {
        rig.setConfig({ AutoPickSource: _scenePath(id), WallpaperWorkShopId: id, WallpaperSource: _scenePath(id) });
    }

    function test_an_automatic_step_from_outside_waits_for_the_reload() {
        const rig = _rig("111");
        const bg = rig.background();
        _stopBy(rig);
        tryVerify(() => bg.stopped === true, 2000);
        stopSpy.target = bg;
        stopSpy.clear();
        _autoStep(rig, "222");
        wait(400);
        compare(stopSpy.count, 0, "an automatic step loaded the stopped wallpaper");
        compare(_loader(rig).item, null);
        verify(bg.wallpaperPath.indexOf("/222/") > 0, bg.wallpaperPath);
        compare(_placeholder(rig).dir, "file:///tmp/fakelib/steamapps/workshop/content/431960/222");
        // The mark after the source, in the same turn.
        rig.setConfig({ WallpaperWorkShopId: "333", WallpaperSource: _scenePath("333") });
        rig.setConfig({ AutoPickSource: _scenePath("333") });
        wait(400);
        compare(stopSpy.count, 0, "a step whose mark came second loaded the stopped wallpaper");
        // A pick by hand, with the last step's mark still there, loads.
        rig.setConfig({ WallpaperWorkShopId: "444", WallpaperSource: _scenePath("444") });
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000, "a pick by hand did not load");
        verify(String(_player(rig).source).indexOf("/444/") > 0);
        tryVerify(() => bg.stopped === true, 2000, "the stop did not come back while its reason stayed");
        // When the reason ends, the wallpaper the last step chose loads.
        _autoStep(rig, "555");
        _endStop(rig);
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000);
        verify(String(_player(rig).source).indexOf("/555/") > 0, String(_player(rig).source));
        rig.destroy();
    }

    function test_an_automatic_step_keeps_every_screen_stopped_with_StopScope_1() {
        const a = _rig("111", { StopScope: 1 });
        const b = _rig("222");
        _stopBy(a);
        tryVerify(() => a.background().stopped === true && b.background().stopped === true, 2000);
        stopSpy.target = a.background();
        stopSpyB.target = b.background();
        stopSpy.clear();
        stopSpyB.clear();
        _autoStep(b, "333");      // the screen another one stops
        _autoStep(a, "444");      // the screen whose window stops them all
        wait(400);
        compare(stopSpy.count, 0, "the source screen loaded an automatic step");
        compare(stopSpyB.count, 0, "the other screen loaded an automatic step");
        compare(b.background()._ignoreRemoteStop, false);
        _endStop(a);
        tryVerify(() => a.background().stopped === false && _player(a) !== null, 2000);
        tryVerify(() => b.background().stopped === false && _player(b) !== null, 2000);
        verify(String(_player(a).source).indexOf("/444/") > 0);
        verify(String(_player(b).source).indexOf("/333/") > 0);
        a.destroy();
        b.destroy();
    }

    // A library with a preview (fixtures/stoplib): it is what a stopped
    // wallpaper leaves on screen, and it goes with the stop.  A TestCase is
    // not visible itself, so nothing in it is: this test shows it, and
    // cleanup() hides it again.
    function cleanup() { tc.visible = false; }
    function test_the_preview_shows_while_stopped() {
        tc.visible = true;
        const lib = String(Qt.resolvedUrl("fixtures/stoplib"));
        const rig = _rig("222", { SteamLibraryPath: lib,
                                  WallpaperSource: lib + "/steamapps/workshop/content/431960/222/scene.pkg+scene" });
        const bg = rig.background();
        const ph = _placeholder(rig);
        verify(ph !== null);
        compare(ph.visible, false);
        _stopBy(rig);
        tryVerify(() => bg.stopped === true, 2000);
        tryVerify(() => ph.status === Image.Ready, 2000, "the preview never loaded");
        verify(String(ph.source).endsWith("/431960/222/preview.png"), String(ph.source));
        compare(ph.visible, true);
        _endStop(rig);
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000);
        compare(ph.visible, false);
        compare(String(ph.source), "");
        rig.destroy();
    }

    // A wallpaper that cannot be drawn shows InfoShow and notifies; a stop
    // unloads InfoShow and its end brings it back, without a new
    // notification.  Another broken wallpaper, or the same one after one
    // that drew, notifies again.
    function _appPath(id) {
        return "file:///tmp/fakelib/steamapps/workshop/content/431960/" + id + "/app.exe+application";
    }
    function test_a_broken_wallpaper_notifies_once_across_stops() {
        const rig = _rig("111");
        const bg = rig.background();
        const n = rig.notifier();
        verify(n !== null);
        rig.setConfig({ WallpaperWorkShopId: "444", WallpaperSource: _appPath("444") });
        tryVerify(() => bg.nowBackend === "InfoShow", 2000);
        compare(n.lastEvent, "wallpaperLoadFailed");
        for (let i = 0; i < 2; i++) {
            n.lastEvent = "";
            _stopBy(rig);
            tryVerify(() => bg.stopped === true, 2000);
            _endStop(rig);
            tryVerify(() => bg.stopped === false && bg.nowBackend === "InfoShow", 2000);
            compare(n.lastEvent, "", "the same failure notified again after stop " + (i + 1));
            _settleWindows(rig);
        }
        rig.setConfig({ WallpaperWorkShopId: "555", WallpaperSource: _appPath("555") });
        compare(n.lastEvent, "wallpaperLoadFailed", "another broken wallpaper must notify");
        rig.setConfig({ WallpaperWorkShopId: "111", WallpaperSource: _scenePath("111") });
        tryVerify(() => _player(rig) !== null, 2000);
        _player(rig).firstFrame();
        n.lastEvent = "";
        rig.setConfig({ WallpaperWorkShopId: "555", WallpaperSource: _appPath("555") });
        compare(n.lastEvent, "wallpaperLoadFailed", "the same failure after one that drew must notify");
        rig.destroy();
    }

    function test_stop_scope_all_screens_stops_the_other_screen_too() {
        const a = _rig("111", { StopScope: 1 });
        const b = _rig("222");
        _stopBy(a);
        tryVerify(() => a.background().stopped === true, 2000);
        tryVerify(() => b.background().stopped === true, 2000, "the other screen did not stop with StopScope 1");
        compare(b.background().ok, false);
        _endStop(a);
        tryVerify(() => a.background().stopped === false, 2000);
        tryVerify(() => b.background().stopped === false && _player(b) !== null, 2000,
                  "the other screen did not reload");
        a.destroy();
        b.destroy();
    }

    function test_stop_scope_this_screen_leaves_the_other_screen_alone() {
        const a = _rig("111", { StopScope: 0 });
        const b = _rig("222");
        _stopBy(a);
        tryVerify(() => a.background().stopped === true, 2000);
        wait(100);
        compare(b.background().stopped, false);
        compare(b.background().ok, true);
        _endStop(a);
        a.destroy();
        b.destroy();
    }

    function test_a_removed_screen_withdraws_its_stop() {
        const a = _rig("111", { StopScope: 1 });
        const b = _rig("222");
        _stopBy(a);
        tryVerify(() => b.background().stopped === true, 2000);
        a.destroy();
        tryVerify(() => b.background().stopped === false && _player(b) !== null, 2000,
                  "the stop of a removed screen outlived it");
        b.destroy();
    }
}
