import QtQuick
import QtTest
import Helpers 1.0
import "_stubs/com/github/captsilver/wallpaperEngineKde/LoadGuardData.js" as Guards

// Reload after sleep (we-manager resume-reload patch) through the REAL
// main.qml (ScreenRig): the stub TTYSwitchMonitor says a resume came, and
// resumeDelayMs later the screen's backend is made again, with one info line.
// A stopped screen and one held back by safe start are left as they are, and
// say so; the reload's guard is marked "resume", so a crash in it does not
// hold the wallpaper back; the new backend is paused while a pause (the lock
// screen's) holds; WekControl's ReloadWallpaper reaches it through the
// playlist controller.  When a resume is a resume (logind, the suspended
// time) is tst_ttyswitchmonitor.cpp's; the guard file is tst_loadguard.cpp's.
TestCase {
    id: tc
    name: "Main_Resume"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }
    // The line the screen wrote last (console.info, and background.lastReloadLine).
    function _line(rig) { return rig.background().lastReloadLine; }

    function _scenePath(id) {
        return "file:///tmp/fakelib/steamapps/workshop/content/431960/" + id + "/scene.pkg+scene";
    }
    function _loader(rig) { return rig._find(rig.mainItem, o => typeof o.dropOutgoing === "function"); }
    function _player(rig) {
        const l = _loader(rig);
        return l && l.item ? rig._find(l.item, o => typeof o.setAcceptMouse === "function") : null;
    }
    function _monitor(rig) {
        return rig._find(rig.mainItem, o => typeof o.resumedFromSleep === "function" && typeof o.ttySwitch === "function");
    }
    function _rig(id, config) {
        failOnWarning(/wallpaper is not defined/);
        failOnWarning(/resumedFromSleep is missing/);
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: id, WallpaperSource: _scenePath(id) };
        for (const k in (config || {})) c[k] = config[k];
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080), initialConfig: c });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        rig.background().resumeDelayMs = 50;
        return rig;
    }

    function init() { Guards.reset(); }

    function test_a_resume_reloads_the_wallpaper_once_with_a_line_naming_it() {
        const rig = _rig("111");
        const bg = rig.background();
        compare(bg.reloadAfterSleep, true, "on without the key: our build's default");
        const first = _loader(rig).item;
        const marks = Guards.marks;
        _monitor(rig).resumedFromSleep("suspend", "logind", (7 * 60 + 12) * 60000);
        _monitor(rig).resumedFromSleep("suspend", "logind", (7 * 60 + 12) * 60000);   // within the delay: one reload
        tryVerify(() => _loader(rig).item !== first && _player(rig) !== null, 2000, "never reloaded");
        wait(200);
        compare(Guards.marks, marks + 1, "one reload, one load");
        verify(/^\[WEK\] resume: reloading the wallpaper on .* \(scene 111\): woke from suspend after 7 h 12 min \(logind\)$/
               .test(_line(rig)), _line(rig));
        const own = rig.fileHelper()._ownGuards();
        compare(own.length, 1);
        compare(own[0].after, "resume", "the reload's guard is not marked");
        // A crash during it does not hold the wallpaper back at the next start.
        Guards.crash();
        rig.destroy();
        wait(0);
        const again = _rig("111");
        compare(again.background()._heldSource, "", "a crash in the reload after sleep tripped safe start");
        compare(again.fileHelper()._ownGuards()[0].after, "", "an ordinary load is marked as a resume");
        again.destroy();
    }

    function test_hibernation_an_unknown_kind_and_an_aborted_suspend_are_named() {
        const rig = _rig("111");
        const first = _loader(rig).item;
        _monitor(rig).resumedFromSleep("hibernation", "clock", 45 * 60000);
        tryVerify(() => _loader(rig).item !== first, 2000);
        verify(/woke from hibernation after 45 min \(clock\)$/.test(_line(rig)), _line(rig));
        const second = _loader(rig).item;
        _monitor(rig).resumedFromSleep("sleep", "logind", 30000);
        tryVerify(() => _loader(rig).item !== second, 2000);
        verify(/woke from suspend or hibernation after 30 s \(logind\)$/.test(_line(rig)), _line(rig));
        const third = _loader(rig).item;
        _monitor(rig).resumedFromSleep("aborted", "logind", 0);
        tryVerify(() => _loader(rig).item !== third, 2000, "an aborted suspend still reloads");
        verify(/: woke from an aborted suspend or hibernation \(0 s asleep\) \(logind\)$/.test(_line(rig)), _line(rig));
        rig.destroy();
    }

    function test_the_reloaded_wallpaper_stays_paused_while_the_screen_is_locked() {
        const rig = _rig("111");
        const bg = rig.background();
        const lock = rig._find(rig.mainItem, o => typeof o.active !== "undefined"
                                               && typeof o.screenSaverActiveChanged !== "undefined");
        verify(lock !== null);
        lock.active = true;                 // the lock screen is up at the wake
        tryVerify(() => bg.ok === false, 2000);
        const first = _loader(rig).item;
        _monitor(rig).resumedFromSleep("suspend", "logind", 60000);
        tryVerify(() => _loader(rig).item !== first && _player(rig) !== null, 2000, "never reloaded");
        tryVerify(() => _loader(rig).item._paused === true, 2000, "the reloaded wallpaper draws behind the lock");
        wait(300);
        compare(_loader(rig).item._paused, true);
        lock.active = false;
        tryVerify(() => _loader(rig).item._paused === false, 2000, "the reloaded wallpaper stays paused after unlock");
        rig.destroy();
    }

    function test_turned_off_nothing_reloads() {
        const rig = _rig("111", { ReloadAfterSleep: false });
        const first = _loader(rig).item;
        _monitor(rig).resumedFromSleep("suspend", "logind", 60000);
        wait(300);
        compare(_loader(rig).item, first);
        compare(_line(rig), "");
        rig.destroy();
    }

    function test_a_stopped_screen_stays_stopped_and_says_why() {
        const rig = _rig("111");
        const bg = rig.background();
        bg.setStopReason("vram", true, "120 MiB free on Stub GPU");
        tryVerify(() => bg.stopped === true, 2000);
        _monitor(rig).resumedFromSleep("suspend", "logind", 60000);
        tryVerify(() => _line(rig) !== "", 2000);
        verify(/^\[WEK\] resume: not reloading on .*: stopped \(vram\); it loads fresh when the stop ends$/.test(_line(rig)),
               _line(rig));
        wait(200);
        compare(bg.stopped, true);
        compare(_loader(rig).item, null, "a stopped wallpaper was loaded by the resume");
        rig.destroy();
    }

    function test_a_screen_held_by_safe_start_waits_for_load_anyway() {
        const crashed = _rig("111");
        Guards.crash();
        crashed.destroy();
        wait(0);
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: "111", WallpaperSource: _scenePath("111") };
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080), initialConfig: c });
        tryVerify(() => rig.background() !== null && rig.background()._heldSource !== "", 2000);
        rig.background().resumeDelayMs = 50;
        _monitor(rig).resumedFromSleep("suspend", "logind", 60000);
        tryVerify(() => _line(rig) !== "", 2000);
        verify(/^\[WEK\] resume: not reloading on .*: held back by safe start$/.test(_line(rig)), _line(rig));
        compare(_player(rig), null, "the held wallpaper was loaded by the resume");
        rig.destroy();
    }

    function test_reload_wallpaper_over_dbus_reloads_the_screen_it_names() {
        const rig = _rig("111");
        const pc = rig.playlistController();
        const first = _loader(rig).item;
        verify(!pc.reloadBackend("NOT-THIS-SCREEN", "D-Bus"));
        compare(_loader(rig).item, first);
        verify(pc.reloadBackend("", "D-Bus"));
        tryVerify(() => _loader(rig).item !== first, 2000);
        verify(/^\[WEK\] reload: reloading the wallpaper on .* \(scene 111\): asked over D-Bus$/.test(_line(rig)), _line(rig));
        compare(rig.fileHelper()._ownGuards()[0].after, "", "a reload by hand counts for safe start");
        const second = _loader(rig).item;
        verify(pc.reloadBackend("", "shortcut"));
        tryVerify(() => _loader(rig).item !== second, 2000);
        verify(/asked by the Reload current wallpaper shortcut$/.test(_line(rig)), _line(rig));
        rig.destroy();
    }
}
