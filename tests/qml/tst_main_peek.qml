import QtQuick
import QtTest
import org.kde.kwindowsystem
import Helpers 1.0

// A peek (we-manager unpause-overview patch) through the REAL main.qml
// (ScreenRig): Show Desktop, or Overview, lifts a fullscreen window's pause
// and nothing else.  The user's pause, the battery and the lock screen still
// pause; a VRAM stop and a window's stop hold; a long-pause unload is not
// reloaded by a peek, and its timer starts again after one.  How a peek is
// told (KWindowSystem, the EffectsWatcher) is tst_windowmodel.qml's and
// tst_effectswatcher.cpp's.
TestCase {
    id: tc
    name: "Main_Peek"
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
    function _reasons(rig) {
        return JSON.parse(rig.playlistController().stopReasons()).reasons.map(r => r.reason);
    }
    function _lock(rig) {
        return rig._find(rig.mainItem, o => typeof o.active !== "undefined"
                                           && typeof o.screenSaverActiveChanged !== "undefined");
    }
    function _rig(config) {
        failOnWarning(/wallpaper is not defined/);
        failOnWarning(/Unable to assign \[undefined\]/);
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: "111", WallpaperSource: _scenePath("111"),
                    PauseMode: 5 /* FullScreen */ };
        for (const k in (config || {})) c[k] = config[k];
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080), initialConfig: c });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        rig.background().stopGraceMs = 150;
        return rig;
    }
    function cleanup() { KWindowSystem.showingDesktop = false; }

    function test_a_peek_lifts_the_window_pause_but_not_the_users_the_batterys_or_the_locks() {
        const rig = _rig({ PauseOnBatPower: true });
        const bg = rig.background();
        _setWindows(rig, _game());
        compare(bg.ok, false, "the fullscreen game did not pause");
        KWindowSystem.showingDesktop = true;
        tryVerify(() => bg.ok === true, 2000, "Show Desktop did not lift the game's pause");
        compare(rig.windowModel().peeking, true);
        rig.playlistController().pause();
        compare(bg.ok, false, "a peek lifted the user's pause");
        rig.playlistController().resume();
        tryVerify(() => bg.ok === true, 2000);
        const lock = _lock(rig);
        verify(lock !== null);
        lock.active = true;
        tryVerify(() => bg.ok === false, 2000, "a peek lifted the lock screen's pause");
        lock.active = false;
        tryVerify(() => bg.ok === true, 2000);
        const ps = rig.powerSource();
        const ds = rig._find(ps, o => typeof o.connectSource === "function" && typeof o.data !== "undefined");
        verify(ds !== null);
        ds.data = { "Battery": { "Has Battery": true, "State": "Discharging", "Percent": 80 } };
        tryVerify(() => bg.ok === false, 2000, "a peek lifted the battery's pause");
        ds.data = { "Battery": { "Has Battery": true, "State": "Charging", "Percent": 80 } };
        tryVerify(() => bg.ok === true, 2000);
        KWindowSystem.showingDesktop = false;
        tryVerify(() => bg.ok === false, 2000, "the end of Show Desktop did not pause again");
        rig.destroy();
    }

    function test_a_vram_stop_and_a_window_stop_hold_through_a_peek() {
        const rig = _rig();
        const bg = rig.background();
        bg.setStopReason("vram", true, "120 MiB free on Stub GPU");
        tryVerify(() => bg.stopped === true, 2000);
        KWindowSystem.showingDesktop = true;
        wait(300);
        compare(bg.stopped, true, "a peek ended a VRAM stop");
        compare(_player(rig), null);
        KWindowSystem.showingDesktop = false;
        bg.setStopReason("vram", false);
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000);
        rig.setConfig({ FullscreenAction: 3 });     // the game stops the wallpaper now
        _setWindows(rig, _game());
        tryVerify(() => bg.stopped === true, 2000, "the game's stop never came");
        KWindowSystem.showingDesktop = true;
        wait(300);
        compare(bg.stopped, true, "Show Desktop reloaded a scene a window stopped");
        compare(_player(rig), null);
        rig.destroy();
    }

    function test_a_long_pause_unload_is_not_reloaded_by_a_peek_and_its_timer_starts_again() {
        const rig = _rig({ UnloadAfterPausedMin: 5 });
        const bg = rig.background();
        bg.pauseUnloadMs = 300;
        _setWindows(rig, _game());
        tryVerify(() => bg.stopped === true, 2000, "a long pause did not unload");
        compare(_reasons(rig), ["paused"]);
        KWindowSystem.showingDesktop = true;
        tryVerify(() => rig.windowModel().peeking === true, 2000);
        wait(300);
        compare(bg.stopped, true, "a peek reloaded a wallpaper a long pause had unloaded");
        compare(_reasons(rig), ["paused"]);
        KWindowSystem.showingDesktop = false;
        tryVerify(() => rig.windowModel().peeking === false, 2000);
        compare(bg.stopped, true);
        _setWindows(rig, []);                         // the game goes: the pause ends
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000, "the end of the pause did not reload");

        // The timer: 600 ms of pause, 400 of them before a peek; after the
        // peek it counts from zero, so 400 ms later the wallpaper is loaded.
        bg.pauseUnloadMs = 600;
        _setWindows(rig, _game());
        wait(400);
        KWindowSystem.showingDesktop = true;
        tryVerify(() => rig.windowModel().peeking === true, 2000);
        wait(100);
        KWindowSystem.showingDesktop = false;
        tryVerify(() => rig.windowModel().peeking === false && bg.ok === false, 2000);
        wait(400);
        compare(bg.stopped, false, "the pause before the peek was counted on");
        tryVerify(() => bg.stopped === true, 2000, "the timer did not start again after the peek");
        rig.destroy();
    }

    // The user's Resume ends a long-pause unload even during a peek: it is
    // the window pause a peek lifts that must not reload the scene.
    function test_the_users_resume_during_a_peek_reloads_a_long_pause_unload() {
        const rig = _rig({ UnloadAfterPausedMin: 5 });
        const bg = rig.background();
        bg.pauseUnloadMs = 200;
        _setWindows(rig, _game());                    // a window pauses too
        rig.playlistController().pause();
        tryVerify(() => bg.stopped === true, 2000, "a long pause did not unload");
        KWindowSystem.showingDesktop = true;
        tryVerify(() => rig.windowModel().peeking === true, 2000);
        compare(bg.stopped, true, "the peek alone reloaded the scene");
        rig.playlistController().resume();
        tryVerify(() => bg.stopped === false && _player(rig) !== null, 2000,
                  "the user's Resume during a peek did not reload the scene");
        compare(bg.ok, true);
        KWindowSystem.showingDesktop = false;
        tryVerify(() => bg.ok === false, 2000, "the game's pause did not come back after the peek");
        compare(bg.stopped, false);
        rig.destroy();
    }
}
