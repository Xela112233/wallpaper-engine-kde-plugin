import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import Helpers 1.0

// Transition length and type (we-manager transitions patch) through the REAL
// main.qml (ScreenRig): the handoff keeps the old wallpaper until the new
// one's first frame (the stub's firstFrame()), then fades, zooms or slides it
// out over TransitionDuration; 0, Reduce animations, a reload and an InfoShow
// screen cut.  The VRAM guard's estimate counts both scenes while both live,
// a tight guard or a pause ends a transition at once, and the old layer never
// outlives it (A to B to C quickly, the handoff timeout).
TestCase {
    id: tc
    name: "Main_Transition"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }

    function _scenePath(id) {
        return "file:///tmp/fakelib/steamapps/workshop/content/431960/" + id + "/scene.pkg+scene";
    }
    function _loader(rig) { return rig._find(rig.mainItem, o => typeof o.dropOutgoing === "function"); }
    function _fade(rig) { return rig._find(rig.mainItem, o => typeof o.incoming !== "undefined" && typeof o.kind === "string"); }
    function _playerOf(backend) {
        return backend ? findChildren(backend).find(o => typeof o.setAcceptMouse === "function") || null : null;
    }
    function findChildren(item) {
        const out = [];
        const walk = o => { for (const c of (o.data || [])) { out.push(c); walk(c); } };
        walk(item);
        return out;
    }
    // The backends alive under the loader (destroy() is deferred: wait for it).
    function _backends(rig) {
        return _loader(rig).children.filter(o => typeof o.frameShown !== "undefined");
    }
    function _rig(config) {
        failOnWarning(/wallpaper is not defined/);
        failOnWarning(/Unable to assign \[undefined\]/);
        failOnWarning(/Cannot animate/);
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: "111", WallpaperSource: _scenePath("111") };
        for (const k in (config || {})) c[k] = config[k];
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 400, 300), initialConfig: c });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        tryVerify(() => _playerOf(_loader(rig).item) !== null, 2000, "scene never mounted");
        _playerOf(_loader(rig).item).firstFrame();
        return rig;
    }
    // Change to `id`; returns [old backend, new backend] once the new one is made.
    function _change(rig, id) {
        const l = _loader(rig);
        const old = l.item;
        rig.setConfig({ WallpaperWorkShopId: id, WallpaperSource: _scenePath(id) });
        tryVerify(() => l.item !== null && l.item !== old && _playerOf(l.item) !== null, 2000, "the new scene never mounted");
        return [old, l.item];
    }
    function _draw(backend) { _playerOf(backend).firstFrame(); }
    function cleanup() { Kirigami.Settings.hasReducedAnimations = false; }

    function test_the_old_wallpaper_fades_over_the_length_set() {
        const rig = _rig({ TransitionDuration: 600 });
        const l = _loader(rig), f = _fade(rig);
        compare(rig.background().transitionMs, 600);
        compare(rig.background().transitionType, "fade");
        const [a, b] = _change(rig, "222");
        compare(l._outgoing, a, "the old one did not wait for the new one's first frame");
        compare(f.running, false);
        _draw(b);
        verify(f.running && f.target === a && f.incoming === b);
        compare(f.kind, "fade");
        compare(f.ms, 600);
        wait(300);
        verify(a.opacity > 0 && a.opacity < 1, "not half way: " + a.opacity);
        tryVerify(() => f.target === null, 1500, "the fade never ended");
        compare(l.liveBackends, 1);
        tryVerify(() => _backends(rig).length === 1, 1500, "the old backend outlived its fade");
        compare(b.mapToItem(l, 0, 0).x, 0);
        compare(l.clip, false);
        rig.destroy();
    }

    function test_zero_is_a_cut_at_the_first_frame() {
        const rig = _rig({ TransitionDuration: 0, TransitionType: "zoom" });
        const l = _loader(rig), f = _fade(rig);
        const [a, b] = _change(rig, "222");
        compare(l._outgoing, a, "a cut still keeps the old one until the new one draws");
        _draw(b);
        tryVerify(() => f.target === null && !f.running, 500, "length 0 did not cut");
        tryVerify(() => _backends(rig).length === 1, 1000);
        rig.destroy();
    }

    function test_zoom_grows_and_fades_the_old_one() {
        const rig = _rig({ TransitionDuration: 600, TransitionType: "zoom" });
        const l = _loader(rig), f = _fade(rig);
        const [a, b] = _change(rig, "222");
        _draw(b);
        compare(f.kind, "zoom");
        compare(l.clip, true, "a zoom draws past the screen's edge without clip");
        wait(350);
        verify(a.scale > 1.05 && a.scale < 1.3 && a.opacity < 1, "scale " + a.scale + ", opacity " + a.opacity);
        compare(b.scale, 1);
        compare(b.mapToItem(l, 0, 0).x, 0);
        tryVerify(() => f.target === null, 1500);
        compare(l.clip, false);
        rig.destroy();
    }

    function test_slide_pushes_both_layers() {
        const rig = _rig({ TransitionDuration: 600, TransitionType: "slide" });
        const l = _loader(rig), f = _fade(rig);
        const [a, b] = _change(rig, "222");
        _draw(b);
        compare(f.kind, "slide");
        wait(300);
        const ax = a.mapToItem(l, 0, 0).x, bx = b.mapToItem(l, 0, 0).x;
        verify(ax < 0 && bx > 0 && bx < l.width, "old at " + ax + ", new at " + bx);
        compare(a.opacity, 1, "a slide does not fade");
        tryVerify(() => f.target === null, 1500);
        compare(b.mapToItem(l, 0, 0).x, 0, "the new one did not end in place");
        compare(b.transform.length, 0);
        rig.destroy();
    }

    function test_an_unknown_type_fades_and_reduce_animations_cuts() {
        const rig = _rig({ TransitionDuration: 600, TransitionType: "spiral" });
        const bg = rig.background(), f = _fade(rig);
        compare(bg.transitionType, "fade");
        Kirigami.Settings.hasReducedAnimations = true;
        compare(bg.transitionMs, 0, "Reduce animations did not make it a cut");
        const [a, b] = _change(rig, "222");
        _draw(b);
        tryVerify(() => f.target === null, 500);
        rig.setConfig({ TransitionDuration: 99999 });
        Kirigami.Settings.hasReducedAnimations = false;
        compare(bg.transitionMs, 5000, "the length is not capped at 5 s");
        rig.destroy();
    }

    function test_a_reload_cuts_and_clears_its_cause_even_when_it_fails() {
        const rig = _rig({ TransitionDuration: 600, TransitionType: "zoom" });
        const bg = rig.background(), l = _loader(rig), f = _fade(rig);
        const a = l.item;
        verify(bg.reloadWallpaper("reload", "D-Bus", "", 0));
        compare(bg._transitionCause, "");
        tryVerify(() => l.item !== a && _playerOf(l.item) !== null, 2000);
        compare(l._outgoing, a);
        compare(l._outgoingCuts, true, "a reload of the same wallpaper would get the zoom");
        _draw(l.item);
        tryVerify(() => f.target === null && !f.running, 500, "the reload did not cut");
        // A load that throws still clears the reload's cause (and the guard's why).
        bg._reloadLoadForTest = function() { throw new Error("the load failed"); };
        let threw = false;
        try { bg.reloadWallpaper("resume", "sleep", "suspend", 60000); } catch (e) { threw = true; }
        verify(threw);
        compare(bg._transitionCause, "");
        compare(bg._guardWhy, "");
        bg._reloadLoadForTest = null;
        // The next change of wallpaper transitions again.
        const [x, y] = _change(rig, "222");
        compare(l._outgoingCuts, false);
        _draw(y);
        compare(f.kind, "zoom");
        rig.destroy();
    }

    function test_an_infoshow_screen_cuts_both_ways() {
        const rig = _rig({ TransitionDuration: 600, TransitionType: "slide" });
        const l = _loader(rig), f = _fade(rig);
        const a = l.item;
        rig.setConfig({ WallpaperWorkShopId: "999", WallpaperSource: "file:///tmp/fakelib/steamapps/workshop/content/431960/999/x.exe+application" });
        tryVerify(() => l.item !== a && l.item !== null && l.item.frameShown === true, 2000, "no InfoShow");
        tryVerify(() => f.target === null && l._outgoing === null, 500, "the change to the message screen did not cut");
        const info = l.item;
        rig.setConfig({ WallpaperWorkShopId: "222", WallpaperSource: _scenePath("222") });
        tryVerify(() => l.item !== info && _playerOf(l.item) !== null, 2000);
        compare(l._outgoingCuts, true, "the change from the message screen would slide");
        _draw(l.item);
        tryVerify(() => f.target === null, 500);
        rig.destroy();
    }

    function test_the_estimate_counts_both_scenes_while_both_live() {
        const rig = _rig({ TransitionDuration: 800, VramStop: true });
        const bg = rig.background(), l = _loader(rig), f = _fade(rig);
        _playerOf(l.item).vramMiB = 600;
        compare(bg.vramEstimateNow, 600);
        compare(bg.vramWatch.estimateMiB, 600);
        const [a, b] = _change(rig, "222");
        compare(bg.vramEstimateNow, 600, "the new scene, not drawn yet, counted");
        _draw(b);
        _playerOf(b).vramMiB = 400;
        compare(bg.vramEstimateNow, 1000, "during the transition the old scene's memory was not counted");
        compare(bg.vramWatch.estimateMiB, 1000);
        tryVerify(() => f.target === null, 2000);
        compare(bg.vramEstimateNow, 400);
        rig.destroy();
    }

    function test_tight_memory_mid_transition_drops_the_old_layer_at_once() {
        const rig = _rig({ TransitionDuration: 3000, VramStop: true });
        const bg = rig.background(), l = _loader(rig), f = _fade(rig);
        const [a, b] = _change(rig, "222");
        _draw(b);
        verify(f.running);
        bg.vramWatch.tight = true;
        compare(f.target, null, "tight memory left the transition running");
        compare(l.liveBackends, 1);
        tryVerify(() => _backends(rig).length === 1, 1000);
        // Waiting for the new one's first frame when it turns tight: the old one goes too.
        bg.vramWatch.tight = false;
        const [c, d] = _change(rig, "333");
        compare(l._outgoing, c);
        bg.vramWatch.tight = true;
        compare(l._outgoing, null);
        rig.destroy();
    }

    function test_a_pause_or_a_vt_switch_mid_transition_ends_it() {
        const rig = _rig({ TransitionDuration: 3000 });
        const l = _loader(rig), f = _fade(rig);
        const [a, b] = _change(rig, "222");
        _draw(b);
        verify(f.running);
        rig.playlistController().pause();
        tryVerify(() => f.target === null, 1000, "a pause left the old wallpaper playing behind the transition");
        rig.playlistController().resume();
        const [c, d] = _change(rig, "333");
        _draw(d);
        verify(f.running);
        const tty = rig._find(rig.mainItem, o => typeof o.resumedFromSleep === "function" && typeof o.ttySwitch === "function");
        verify(tty !== null);
        tty.ttySwitch(true);
        compare(f.target, null, "a VT switch left the transition running");
        tty.ttySwitch(false);
        // Paused while the new one has not drawn: the old one is the only
        // picture, so it stays, paused.
        const [e, g] = _change(rig, "444");
        compare(l._outgoing, e);
        const paused = _playerOf(e).pauseCount;
        rig.playlistController().pause();
        compare(l._outgoing, e, "the only picture went with the pause");
        compare(e._paused, true, "the waiting old wallpaper played on through the pause");
        tryVerify(() => _playerOf(e).pauseCount > paused, 2000, "its renderer was never paused");
        rig.playlistController().resume();
        rig.destroy();
    }

    // The paused old layer (the only picture while the new one has not
    // drawn) still goes with the handoff timeout, paused or not.
    function test_a_paused_old_layer_still_goes_with_the_handoff_timeout() {
        const rig = _rig({ TransitionDuration: 400 });
        const l = _loader(rig), f = _fade(rig);
        const timeout = rig._find(l, o => o.interval === 25000);
        verify(timeout !== null);
        timeout.interval = 300;
        const [a, b] = _change(rig, "222");          // b never draws
        compare(l._outgoing, a);
        rig.playlistController().pause();
        compare(a._paused, true);
        compare(l._outgoing, a, "paused, the waiting old layer went before the timeout");
        tryVerify(() => l._outgoing === null, 2000, "the handoff timeout did not take the paused old layer");
        tryVerify(() => f.target === null && _backends(rig).length === 1, 2000,
                  "the paused old layer outlived the timeout: " + _backends(rig).length + " backends");
        compare(l.item, b);
        rig.playlistController().resume();
        rig.destroy();
    }

    function test_the_old_layer_never_outlives_its_transition() {
        const rig = _rig({ TransitionDuration: 1500, TransitionType: "slide" });
        const l = _loader(rig), f = _fade(rig);
        // A to B, then C while A is still going, then D before C has drawn.
        const [a, b] = _change(rig, "222");
        _draw(b);
        verify(f.target === a);
        wait(200);
        const [b2, c] = _change(rig, "333");
        compare(b2, b);
        compare(l._outgoing, b);
        compare(f.target, null, "A still going as C came: three wallpapers alive");
        compare(l.liveBackends, 2);
        _draw(c);
        compare(f.target, b, "C's transition did not take over");
        compare(b.transform.length, 1, "B did not slide out by itself");
        wait(100);
        verify(b.mapToItem(l, 0, 0).x <= 0, "B did not start from its place: " + b.mapToItem(l, 0, 0).x);
        tryVerify(() => _backends(rig).length <= 2, 1000, "A outlived C's coming");
        const [c2, d] = _change(rig, "444");
        compare(l._outgoing, c);
        tryVerify(() => f.target === null, 3000);
        _draw(d);
        tryVerify(() => f.target === null && _backends(rig).length === 1, 3000,
                  _backends(rig).length + " backends left");
        // The handoff timeout: a new one that never draws.
        const timeout = rig._find(l, o => o.interval === 25000);
        verify(timeout !== null);
        timeout.interval = 200;
        const [d2, e] = _change(rig, "555");
        compare(l._outgoing, d);
        tryVerify(() => l._outgoing === null, 1000, "the handoff timeout did not drop the old one");
        tryVerify(() => f.target === null && _backends(rig).length === 1, 3000);
        compare(l.item, e);
        rig.destroy();
    }

    // C fading out, then D and E in quick succession: never more than two
    // wallpapers alive (an old layer and the new one), nor counted.
    function test_rapid_changes_keep_two_wallpapers_at_most() {
        const rig = _rig({ TransitionDuration: 2000, VramStop: true });
        const bg = rig.background(), l = _loader(rig), f = _fade(rig);
        const mib = { "111": 100, "222": 200, "333": 400, "444": 800 };
        const step = (what, expect) => {
            verify(l.liveBackends <= 2, what + ": " + l.liveBackends + " wallpapers alive");
            compare(bg.vramEstimateNow, expect, what + ": the estimate");
            compare(bg.vramWatch.estimateMiB, expect, what + ": the guard's estimate");
        };
        const c = l.item;
        _playerOf(c).vramMiB = mib["111"];
        let [c2, d] = _change(rig, "222");
        _playerOf(d).vramMiB = mib["222"];
        step("D waits for its first frame", 300);
        _draw(d);
        verify(f.target === c && f.running);
        step("C fades out over D", 300);
        let [d2, e] = _change(rig, "333");
        _playerOf(e).vramMiB = mib["333"];
        compare(f.target, null, "C was still going");
        compare(l._outgoing, d);
        step("E comes while C was fading", 600);
        _draw(e);
        verify(f.target === d && f.running);
        step("D fades out over E", 600);
        let [e2, g] = _change(rig, "444");
        _playerOf(g).vramMiB = mib["444"];
        step("G comes before D has gone", 1200);
        tryVerify(() => _backends(rig).length <= 2, 1000, _backends(rig).length + " backends alive");
        _draw(g);
        step("E fades out over G", 1200);
        tryVerify(() => f.target === null, 3000);
        step("G alone", 800);
        tryVerify(() => _backends(rig).length === 1, 1000);
        rig.destroy();
    }

    SignalSpy { id: accentSpy; signalName: "accentColorChanged" }

    // Plasma's accent colour waits for the old layer to go; a transition cut
    // short (here by a pause) still sends it.
    function test_a_transition_cut_short_still_sends_the_accent_colour() {
        const rig = _rig({ TransitionDuration: 3000 });
        const bg = rig.background(), l = _loader(rig), f = _fade(rig);
        tryVerify(() => !bg._accentPending, 2000);
        accentSpy.target = rig.ctx;
        accentSpy.clear();
        const [a, b] = _change(rig, "222");
        _draw(b);
        verify(f.running);
        compare(accentSpy.count, 0, "announced with the old wallpaper still on top");
        compare(bg._accentPending, true);
        rig.playlistController().pause();
        tryVerify(() => f.target === null, 1000);
        compare(bg._accentPending, false, "the accent colour waits on after the pause ended the transition");
        compare(accentSpy.count, 1);
        rig.playlistController().resume();
        rig.destroy();
    }

    // A mute that comes on during a handoff reaches the old layer, whose
    // options are frozen: its sound goes at once, while it waits for the new
    // one's first frame and while it fades.
    function _setWindows(rig, windows) {
        const wm = rig.windowModel();
        const tm = rig._find(wm, o => typeof o._windows !== "undefined");
        verify(tm !== null);
        tm._windows = windows;
        wm.updateWindowsinfo();
        const tt = rig._find(wm, o => typeof o.start === "function" && o.interval == 100);
        if (tt) tt.triggered();
    }
    function test_a_mute_during_a_transition_cuts_the_old_sound_at_once() {
        const rig = _rig({ TransitionDuration: 3000, MuteAudio: false, PauseMode: 2, MaximizedAction: 1 });
        const bg = rig.background(), l = _loader(rig), f = _fade(rig);
        const pc = rig.playlistController();
        const ways = [
            ["the global mute", () => pc.setGlobalMute(true), () => pc.setGlobalMute(false)],
            ["the automatic mute", () => pc.setAutoMuted(true), () => pc.setAutoMuted(false)],
            ["AudioOutput off", () => rig.setConfig({ AudioOutput: false }), () => rig.setConfig({ AudioOutput: true })],
            ["a window's mute", () => _setWindows(rig, [{ isWindow: true, isActive: false, isMaximized: true,
                                                         isFullScreen: false, isMinimized: false, activities: [],
                                                         appName: "editor" }]),
             () => { _setWindows(rig, []); tryVerify(() => !rig.windowModel().reqMute, 2000); }],
        ];
        let id = 200;
        for (const [what, on, off] of ways) {
            compare(bg.screenMute, false, what + ": muted before");
            // Fading out.
            let [a, b] = _change(rig, String(++id));
            a.volumeFade.state.volumeFade = 80;
            _draw(b);
            verify(f.target === a && f.running, what);
            wait(100);
            verify(_playerOf(a).volume > 0, what + ": silent before the mute");
            on();
            compare(bg.screenMute, true, what);
            compare(_playerOf(a).volume, 0, what + ": the fading old sound played on");
            compare(_playerOf(a).muted, true, what + ": the fading old layer is not muted");
            off();
            tryVerify(() => f.target === null, 4000);
            // Waiting for the new one's first frame.
            [a, b] = _change(rig, String(++id));
            a.volumeFade.state.volumeFade = 80;
            compare(l._outgoing, a, what);
            on();
            compare(_playerOf(a).volume, 0, what + ": the waiting old sound played on");
            compare(_playerOf(a).muted, true, what + ": the waiting old layer is not muted");
            off();
            _draw(b);
            tryVerify(() => f.target === null && _backends(rig).length === 1, 4000);
        }
        rig.destroy();
    }

    function test_the_old_wallpapers_sound_fades_out_over_the_transition() {
        const rig = _rig({ TransitionDuration: 1000 });
        const l = _loader(rig), f = _fade(rig);
        const [a, b] = _change(rig, "222");
        const asked = [];
        const fadeOut = a.volumeFade.fadeOut;
        a.volumeFade.fadeOut = function(done, ms) { asked.push(ms); fadeOut(done, ms); };
        a.volumeFade.state.volumeFade = 80;
        _draw(b);
        compare(asked, [1000]);
        wait(500);
        const half = _playerOf(a).volume;
        verify(half > 0 && half < 0.8, "the old sound is not fading: " + half);
        tryVerify(() => f.target === null, 2000);
        tryVerify(() => _backends(rig).length === 1, 1000, "the faded backend lingers");
        rig.destroy();
    }
}
