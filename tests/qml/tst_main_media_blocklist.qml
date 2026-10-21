import QtQuick
import QtTest
import Helpers 1.0

// MediaBlocklist (we-manager media-blocklist patch) through the REAL main.qml
// (ScreenRig): the screen's key reaches the scene's MprisMonitor as
// ignoredPlayers, and a later write (we-manager's, through plasmashell's
// scripting API) reaches the same monitor live, with no reload of the
// wallpaper.  What the monitor does with the list is tst_mpriscolors.cpp's;
// the two backends' bindings are tst_backend_scene's and
// tst_backend_qtwebview's.
TestCase {
    id: tc
    name: "Main_MediaBlocklist"
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
    // The stub monitor is a QtObject in Scene.qml's Loader: reach it through
    // the Loader's item.
    function _monitor(rig) {
        const l = _loader(rig);
        const m = l && l.item ? rig._find(l.item, o => o.item && typeof o.item.engageCount !== "undefined"
                                                         && typeof o.item.ignoredPlayers !== "undefined") : null;
        return m ? m.item : null;
    }
    function _rig(config) {
        failOnWarning(/wallpaper is not defined/);
        failOnWarning(/Unable to assign \[undefined\]/);
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: "111", WallpaperSource: _scenePath("111") };
        for (const k in (config || {})) c[k] = config[k];
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080), initialConfig: c });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        tryVerify(() => _monitor(rig) !== null, 2000, "the scene has no media monitor");
        return rig;
    }

    function test_the_key_reaches_the_scenes_monitor_and_follows_a_write_live() {
        const rig = _rig({ MediaBlocklist: "spotify" });
        const bg = rig.background();
        const mpris = _monitor(rig);
        compare(bg.mediaBlocklist, "spotify");
        compare(mpris.ignoredPlayers, "spotify", "the list did not reach the monitor before it engaged");
        compare(mpris.engageCount, 1);
        const scene = _loader(rig).item;
        rig.setConfig({ MediaBlocklist: "spotify,chromium" });
        tryCompare(mpris, "ignoredPlayers", "spotify,chromium");
        compare(_monitor(rig), mpris, "a new monitor was made for the change");
        compare(mpris.engageCount, 1);
        compare(_loader(rig).item, scene, "the change reloaded the wallpaper");
        rig.setConfig({ MediaBlocklist: "" });
        tryCompare(mpris, "ignoredPlayers", "");
        rig.destroy();
    }

    function test_by_default_nothing_is_ignored() {
        const rig = _rig();
        compare(rig.background().mediaBlocklist, "");
        compare(_monitor(rig).ignoredPlayers, "");
        rig.destroy();
    }
}
