import QtQuick
import QtTest
import Helpers 1.0
import "_stubs/com/github/captsilver/wallpaperEngineKde/LoadGuardData.js" as Guards

// Safe start after a crash (we-manager load-guard patch) through the REAL
// main.qml (ScreenRig).  Each load writes this screen's guard and it goes once
// the wallpaper has drawn, on a failure, a stop or when the screen goes away.
// A plasmashell that ends while loading leaves it (Guards.crash()), and the
// next start, within the window, shows the safe start pane instead of the
// wallpaper, until Load anyway or another wallpaper.  The guard files and the
// process checks are LoadGuard.hpp's (tst_loadguard.cpp).
TestCase {
    id: tc
    name: "Main_SafeStart"
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
    function _pane(rig) {
        const l = _loader(rig);
        return l && l.item && typeof l.item.loadAnywayRequested !== "undefined" ? l.item : null;
    }
    // A screen as plasmashell starts it: the configuration is there before
    // main.qml's Component.onCompleted.
    function _rig(id, config) {
        failOnWarning(/wallpaper is not defined/);
        failOnWarning(/Unable to assign \[undefined\]/);
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: id, WallpaperSource: _scenePath(id) };
        for (const k in (config || {})) c[k] = config[k];
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080), initialConfig: c });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        return rig;
    }
    // A plasmashell that crashed while loading `id`: its guard is left behind.
    function _crashWhileLoading(id) {
        const rig = _rig(id);
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        compare(rig.fileHelper()._ownGuards().length, 1);
        Guards.crash();
        rig.destroy();
        wait(0);
    }

    function init() { Guards.reset(); }

    function test_a_load_writes_the_guard_and_drawing_for_a_while_removes_it() {
        const rig = _rig("111");
        const bg = rig.background();
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        const own = rig.fileHelper()._ownGuards();
        compare(own.length, 1);
        compare(own[0].source, _scenePath("111"));
        compare(own[0].workshopid, "111");
        compare(bg._loadGuarded, true);
        bg.safeStartClearMs = 100;
        _player(rig).firstFrame();
        tryVerify(() => rig.fileHelper()._ownGuards().length === 0, 2000, "the guard stayed after the first frame");
        compare(bg._loadGuarded, false);
        // The next wallpaper writes it again, and a load failure removes it.
        rig.setConfig({ WallpaperWorkShopId: "222", WallpaperSource: _scenePath("222") });
        tryVerify(() => rig.fileHelper()._ownGuards().length === 1
                        && rig.fileHelper()._ownGuards()[0].workshopid === "222", 2000);
        _loader(rig).loadInfoShow("The scene produced no frame within 20 seconds.");
        compare(rig.fileHelper()._ownGuards().length, 0, "a load failure did not take Plasma down");
        rig.destroy();
    }

    function test_a_stop_and_a_screen_that_goes_away_remove_the_guard() {
        const rig = _rig("111", { PauseMode: 5, FullscreenAction: 3 });
        const bg = rig.background();
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        compare(rig.fileHelper()._ownGuards().length, 1);
        bg.stopBackend();
        compare(rig.fileHelper()._ownGuards().length, 0, "a stopped wallpaper is not loading");
        bg.startBackend();
        tryVerify(() => _player(rig) !== null, 2000);
        compare(rig.fileHelper()._ownGuards().length, 1);
        rig.destroy();
        wait(0);
        compare(Guards.guards.length, 0, "a screen that goes away (a clean exit) left its guard");
    }

    function test_a_crash_while_loading_holds_the_wallpaper_back_until_load_anyway() {
        _crashWhileLoading("111");
        const rig = _rig("111");
        const bg = rig.background();
        tryVerify(() => _pane(rig) !== null, 2000, "the safe start pane never showed");
        compare(_player(rig), null, "the wallpaper that stopped Plasma was loaded again");
        compare(bg._heldSource, _scenePath("111"));
        const pane = _pane(rig);
        compare(pane.loadAnyway, true);
        compare(pane.heading, "Plasma stopped while this wallpaper was loading");
        verify(pane.info.indexOf("It was not loaded again") === 0, pane.info);
        compare(bg.nowBackend, "InfoShow");
        compare(rig.fileHelper()._ownGuards().length, 0, "nothing loads, so nothing is guarded");
        const n = rig.notifier();
        compare(n.lastEvent, "wallpaperLoadFailed");
        compare(n.lastArgs[0], "111");
        // A stop and its end bring the pane back, not the wallpaper.
        bg.stopBackend();
        bg.startBackend();
        tryVerify(() => _pane(rig) !== null, 2000);
        compare(_player(rig), null);
        // Load anyway: it loads, guarded again.
        _pane(rig).loadAnywayRequested();
        tryVerify(() => _player(rig) !== null, 2000, "Load anyway did not load the wallpaper");
        compare(bg._heldSource, "");
        compare(rig.fileHelper()._ownGuards().length, 1);
        rig.destroy();
    }

    function test_another_wallpaper_ends_the_hold() {
        _crashWhileLoading("111");
        const rig = _rig("111");
        tryVerify(() => _pane(rig) !== null, 2000);
        rig.setConfig({ WallpaperWorkShopId: "222", WallpaperSource: _scenePath("222") });
        tryVerify(() => _player(rig) !== null, 2000, "the wallpaper picked instead did not load");
        verify(String(_player(rig).source).indexOf("/222/") > 0);
        // Back to the first one later in the session: it loads, the hold was for the start only.
        rig.setConfig({ WallpaperWorkShopId: "111", WallpaperSource: _scenePath("111") });
        tryVerify(() => _player(rig) !== null && String(_player(rig).source).indexOf("/111/") > 0, 2000);
        rig.destroy();
    }

    function test_an_old_guard_another_wallpaper_or_safe_start_off_load_as_always() {
        _crashWhileLoading("111");
        Guards.age(121000);                          // the restart came after the window: a login after a reboot
        let rig = _rig("111");
        tryVerify(() => _player(rig) !== null, 2000, "an old guard held the wallpaper back");
        rig.destroy();
        wait(0);
        Guards.reset();
        _crashWhileLoading("111");
        rig = _rig("222");                            // this screen shows another wallpaper
        tryVerify(() => _player(rig) !== null, 2000, "a guard for another wallpaper held this one back");
        rig.destroy();
        wait(0);
        rig = _rig("111", { SafeStartAfterCrash: false });
        tryVerify(() => _player(rig) !== null, 2000, "Safe start after a crash off still held it back");
        compare(rig.fileHelper()._ownGuards().length, 0, "off, nothing is written");
        rig.destroy();
    }

    function test_a_running_screen_loading_the_same_wallpaper_does_not_count() {
        const first = _rig("111");
        tryVerify(() => _player(first) !== null, 2000);
        const second = _rig("111");                   // another screen of the same plasmashell
        tryVerify(() => _player(second) !== null, 2000, "a live screen's guard held the wallpaper back");
        compare(Guards.guards.length, 2);
        first.destroy();
        second.destroy();
    }
}
