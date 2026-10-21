import QtQuick
import QtTest
import Helpers 1.0
import "_stubs/com/github/captsilver/wallpaperEngineKde/CloneGroupData.js" as CloneData

// One renderer for screens that show the same scene (we-manager clone patch,
// ShareRenderer) through the REAL main.qml, CloneLink.qml, backend/Scene.qml
// and backend/SceneMirror.qml, one ScreenRig per screen.  The stubs stand in
// for the C++ only: CloneGroup (the table every screen writes to),
// SceneViewer (shareKey, sharedFrameStalledMs) and SceneMirror (tests say
// when its frame arrives).  The rules themselves are js/clone.mjs, the real
// file; tst_clonegroup.cpp and tst_sharedframe.cpp cover the C++.
TestCase {
    id: tc
    name: "Main_Clone"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }
    property var rigs: []

    function _scenePath(id) {
        return "file:///tmp/fakelib/steamapps/workshop/content/431960/" + id + "/scene.pkg+scene";
    }
    function _loader(rig) { return rig._find(rig.mainItem, o => typeof o.dropOutgoing === "function"); }
    // The current backend's SceneViewer (it renders) or SceneMirror (it shows a frame).
    function _player(rig) {
        const l = _loader(rig);
        return l && l.item ? rig._find(l.item, o => typeof o.setAcceptMouse === "function") : null;
    }
    function _mirror(rig) {
        const l = _loader(rig);
        return l && l.item ? rig._find(l.item, o => typeof o.mapToSource === "function") : null;
    }
    function _link(rig) { return rig.background().cloneLink; }
    // main.qml's transition animation (transitions patch).
    function _fade(rig) { return rig._find(rig.mainItem, o => typeof o.incoming !== "undefined" && typeof o.kind === "string"); }
    // How many backends are alive under the loader (destroy() is deferred).
    function _backends(rig) {
        return _loader(rig).children.filter(o => typeof o.frameShown !== "undefined").length;
    }
    function _tune(rig) {
        const link = _link(rig);
        if (link) {
            link.followDelayMs = 20;
            link.stallCheckMs = 40;
            link.stallClearMs = 20;
            link.probeMs = 40;
        }
    }
    function _make(id, config, geometry) {
        failOnWarning(/wallpaper is not defined/);
        failOnWarning(/Unable to assign \[undefined\]/);
        failOnWarning(/TypeError/);
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: id, WallpaperSource: _scenePath(id) };
        for (const k in (config || {})) c[k] = config[k];
        const rig = rigComp.createObject(tc, { screenGeometry: geometry || Qt.rect(0, 0, 1920, 1080),
                                               initialConfig: c });
        verify(rig !== null);
        rigs.push(rig);
        return rig;
    }
    // One screen, loaded and (with the sharing on) settled in its part.
    function _rig(id, config, geometry) {
        const rig = _make(id, config, geometry);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        tryVerify(() => _loader(rig).item !== null, 2000, "no backend was loaded");
        _tune(rig);
        if (_link(rig))
            tryVerify(() => _player(rig) !== null || (_mirror(rig) !== null && _mirror(rig).frameKey !== ""), 2000,
                      "the screen neither renders nor shows a renderer's frame");
        return rig;
    }
    // The first frame of what the screen shows now: a renderer's, or, for a
    // mirror, the frame arriving from the rendering screen.
    function _draw(rig) {
        if (_player(rig)) _player(rig).firstFrame();
        else _mirror(rig).hasFrame = true;
    }
    // The screen is a mirror of `of`'s renderer.
    function _mirrors(rig, of) {
        return _player(rig) === null && _mirror(rig) !== null && _player(of) !== null
               && _mirror(rig).frameKey === _player(of).shareKey;
    }
    function cleanup() {
        for (const r of rigs) r.destroy();
        rigs = [];
        // The stubs leave the table as their rigs go.
        tryVerify(() => CloneData.all.length === 0, 2000, "a screen's entry outlived its rig");
    }

    function test_off_every_screen_renders_and_nothing_of_the_sharing_is_made() {
        const made = CloneData.made;
        const a = _rig("111"), b = _rig("111");
        wait(100);
        for (const rig of [a, b]) {
            compare(_link(rig), null, "ShareRenderer off: no CloneLink");
            verify(_player(rig) !== null, "each screen has its own renderer");
            compare(_mirror(rig), null);
            compare(_player(rig).shareKey, "", "no frame is published");
        }
        compare(CloneData.made, made, "no CloneGroup was made");
    }

    function test_two_screens_with_the_same_scene_share_one_renderer() {
        const a = _rig("111", { ShareRenderer: 1 }), b = _rig("111", { ShareRenderer: 1 });
        // The first screen renders and publishes its frame ...
        verify(_player(a) !== null);
        verify(_player(a).shareKey !== "", "the rendering viewer was given no frame name");
        compare(_link(a).role, "source");
        // ... the second has no renderer: it shows that frame.
        verify(_mirrors(b, a), "the second screen does not show the first one's frame");
        compare(_link(b).role, "mirror");
        verify(_link(a).lastLine.indexOf("renders 111") > 0, _link(a).lastLine);
        verify(_link(b).lastLine.indexOf("frame of 111") > 0, _link(b).lastLine);
        // The mirror counts as on screen when the frame arrives, not before.
        compare(_loader(b).item.frameShown, false);
        _player(a).firstFrame();
        _mirror(b).hasFrame = true;
        compare(_loader(b).item.frameShown, true);
        // It holds no video memory, whatever this screen's scene held before.
        _player(a).vramMiB = 700;
        compare(a.background().vramEstimateNow, 700);
        compare(b.background().vramEstimateNow, 0);
    }

    function test_screens_that_load_together_begin_no_renderer_until_it_is_clear_who_renders() {
        // As at a login: the three screens load in the same moment.  The middle one is the
        // largest; the last one is the one the user chose.
        const a = _make("111", { ShareRenderer: 1 }), b = _make("111", { ShareRenderer: 1 }, Qt.rect(0, 0, 3440, 1440)),
              c = _make("111", { ShareRenderer: 2 });
        // Nobody has begun to render: each waits for the others to be in the table.
        for (const rig of [a, b, c]) {
            verify(rig.background() !== null && _loader(rig).item !== null);
            compare(_player(rig), null, "a screen began to render before the others had loaded");
        }
        for (const rig of [a, b, c]) _tune(rig);
        // The chosen screen renders, although it loaded last and is not the largest;
        // the others never made a renderer.
        tryVerify(() => _player(c) !== null && _mirrors(a, c) && _mirrors(b, c), 3000, "the chosen screen does not render");
        verify(_link(a).lastLine.indexOf("renders") < 0 && _link(b).lastLine.indexOf("renders") < 0,
               "another screen began the scene first");
        compare(_backends(a), 1);
        compare(_backends(b), 1);
    }

    function test_with_no_screen_chosen_the_largest_of_those_that_load_together_renders() {
        const a = _make("111", { ShareRenderer: 1 }), b = _make("111", { ShareRenderer: 1 }, Qt.rect(0, 0, 3440, 1440));
        for (const rig of [a, b]) _tune(rig);
        tryVerify(() => _player(b) !== null && _mirrors(a, b), 3000, "the larger screen does not render");
    }

    function test_another_wallpaper_or_other_properties_get_their_own_renderer() {
        const a = _rig("111", { ShareRenderer: 1 }), b = _rig("222", { ShareRenderer: 1 });
        verify(_player(a) !== null && _player(b) !== null, "two wallpapers, two renderers");
        compare(_link(b).role, "source");
        // The second screen takes the first one's wallpaper: it becomes its mirror.
        b.setConfig({ WallpaperWorkShopId: "111", WallpaperSource: _scenePath("111") });
        tryVerify(() => _mirror(b) !== null && _mirror(b).frameKey === _player(a).shareKey, 2000,
                  "the screen did not become a mirror of the same wallpaper");
        _mirror(b).hasFrame = true;
        tryVerify(() => _backends(b) === 1, 2000, "the old renderer stayed");
        // Its properties are edited on that screen only: not the same picture any more.
        b.fileHelper()._wallpaperConfigReturns = { user_props: { color: "1 0 0" } };
        b.setConfig({ PerOptChanged: 1 });
        tryVerify(() => _player(b) !== null, 2000, "other properties, but still the other screen's frame");
        compare(_link(b).role, "source");
        verify(_player(b).shareKey !== _player(a).shareKey);
        compare(_link(a).role, "source");
    }

    function test_a_stop_on_the_rendering_screen_moves_the_renderer() {
        const a = _rig("111", { ShareRenderer: 1 }), b = _rig("111", { ShareRenderer: 1 });
        _draw(a); _draw(b);
        compare(_link(a).role, "source");
        a.background().setStopReason("ext:game", true, "a game");
        tryVerify(() => a.background().stopped === true, 2000);
        tryVerify(() => _player(b) !== null, 2000, "nobody renders after the source stopped");
        compare(_link(b).role, "source");
        verify(_player(b).shareKey !== "");
        _draw(b);
        // The stop ends: the screen comes back as a mirror; the renderer stays where it is.
        a.background().setStopReason("ext:game", false);
        tryVerify(() => a.background().stopped === false && _mirrors(a, b), 2000,
                  "the stopped screen did not come back as a mirror");
        compare(_link(b).role, "source");
    }

    function test_a_rendering_screen_that_goes_away_is_replaced() {
        const a = _rig("111", { ShareRenderer: 1 }), b = _rig("111", { ShareRenderer: 1 }),
              c = _rig("111", { ShareRenderer: 1 }, Qt.rect(0, 0, 2560, 1440));
        _draw(a); _draw(b); _draw(c);
        compare(_link(b).role, "mirror");
        compare(_link(c).role, "mirror");
        rigs = rigs.filter(r => r !== a);
        a.destroy();
        // Of the two left, the larger screen renders; the other follows its frame.
        tryVerify(() => _player(c) !== null, 2000, "the larger of the screens left did not take over");
        tryVerify(() => _mirrors(b, c), 2000, "the other mirror did not follow the new renderer");
    }

    function test_a_pause_on_the_rendering_screen_does_not_freeze_the_others() {
        const a = _rig("111", { ShareRenderer: 1 }), b = _rig("111", { ShareRenderer: 1 });
        _draw(a); _draw(b);
        wait(400);                              // main.qml's own pause 300 ms after start is over
        const scene = _loader(a).item;
        // Only the rendering screen is paused (a fullscreen window on it): it plays on.
        a.playlistController().pause();
        tryVerify(() => a.background().ok === false, 2000);
        tryVerify(() => scene._paused === false, 2000, "the renderer paused while another screen shows its frame");
        // The other screen pauses too: now nobody watches, and it pauses.
        b.playlistController().pause();
        tryVerify(() => scene._paused === true, 2000, "the renderer kept playing with every screen paused");
        // One of them plays again: so does the renderer.
        b.playlistController().resume();
        tryVerify(() => scene._paused === false, 2000, "the renderer did not play again for the other screen");
    }

    function test_a_paused_mirror_shows_a_still_of_its_own_and_follows_no_frame() {
        const a = _rig("111", { ShareRenderer: 1 }), b = _rig("111", { ShareRenderer: 1 });
        _draw(a);
        wait(400);                              // main.qml's own pause 300 ms after start is over
        const item = _loader(b).item, mirror = _mirror(b);
        const still = b._find(item, o => typeof o.scheduleUpdate === "function");
        verify(still !== null);
        tryVerify(() => mirror.live === true && still.sourceItem === null, 2000);
        // Paused before the renderer's first frame: nothing to keep yet, so it goes on waiting for one.
        b.playlistController().pause();
        tryVerify(() => item._paused === true, 2000);
        compare(mirror.live, true);
        compare(still.sourceItem, null);
        // The frame arrives: the still is taken of the mirror, and only then the mirror stops following.
        mirror.hasFrame = true;
        tryVerify(() => still.sourceItem === mirror, 2000, "no still of the paused mirror");
        compare(still.live, false);
        compare(still.hideSource, true, "the renderer's image would be drawn again beside the still");
        still.scheduledUpdateCompleted();
        tryVerify(() => mirror.live === false, 2000, "the paused mirror goes on following the renderer's images");
        // The renderer's frame goes (the renderer moved): the paused screen keeps its still.
        mirror.hasFrame = false;
        wait(50);
        verify(still.sourceItem === mirror && mirror.live === false, "the paused screen lost its picture");
        mirror.hasFrame = true;
        // Playing again: the mirror itself, following.
        b.playlistController().resume();
        tryVerify(() => mirror.live === true && still.sourceItem === null, 2000,
                  "the mirror did not follow again");
    }

    function test_the_screen_the_user_chose_takes_the_renderer_over() {
        const a = _rig("111", { ShareRenderer: 1 });
        _draw(a);
        const b = _rig("111", { ShareRenderer: 2 });
        // The chosen screen loads the scene although another one renders it already ...
        verify(_player(b) !== null, "the chosen screen did not load the scene");
        compare(_link(b).role, "source");
        // ... and the first one becomes its mirror, keeping its picture until the frame arrives.
        tryVerify(() => _mirrors(a, b), 2000);
        compare(_backends(a), 2);
        _draw(b); _draw(a);
        tryVerify(() => _backends(a) === 1, 2000, "the old renderer stayed after the mirror's frame");
        // No longer chosen: the renderer stays where it is.
        b.setConfig({ ShareRenderer: 1 });
        wait(150);
        compare(_link(b).role, "source");
        verify(_player(b) !== null);
    }

    function test_a_mirror_that_cannot_draw_the_frame_renders_itself() {
        const a = _rig("111", { ShareRenderer: 1 }), b = _rig("111", { ShareRenderer: 1 });
        _draw(a);
        ignoreWarning(/\[WEK\] clone: .* cannot show another screen's frame \(no shared OpenGL context\)/);
        _mirror(b).failed("no shared OpenGL context");
        tryVerify(() => _player(b) !== null, 2000, "the screen got no renderer of its own");
        compare(_player(b).shareKey, "", "it shares nothing");
        compare(_link(b).role, "");
        compare(_link(a).members().length, 1, "it is out of the table");
        compare(_link(a).role, "source");
        // It stays that way: a reload does not try the frame again.
        b.background().reloadWallpaper("reload", "D-Bus", "", 0);
        verify(_player(b) !== null);
    }

    function test_a_renderer_whose_window_is_not_drawn_hands_over_to_a_screen_that_is() {
        const a = _rig("111", { ShareRenderer: 1 });
        _draw(a);
        // Alone, there is nobody to hand over to.
        _player(a).stalledMs = 9000;
        wait(300);
        compare(_link(a).stalled, false);
        _player(a).stalledMs = 0;
        // Two more screens; the first of them is the larger, and its window is not drawn either.
        const b = _rig("111", { ShareRenderer: 1 }, Qt.rect(0, 0, 3440, 1440)), c = _rig("111", { ShareRenderer: 1 });
        _draw(b); _draw(c);
        _mirror(b).windowDrawn = false;
        ignoreWarning(/\[WEK\] clone: .* its window has drawn no frame for 9 s/);
        _player(a).stalledMs = 9000;
        // The screen that is drawn takes the rendering, not the larger one that is not.
        tryVerify(() => _player(c) !== null, 3000, "the screen that is drawn did not take the rendering");
        tryVerify(() => _mirrors(a, c) && _mirrors(b, c), 3000);
        compare(_link(a).stalled, true);
        _mirror(b).windowDrawn = false;         // its new mirror item's window is the same one
        tryVerify(() => _link(b).stalled === true, 2000, "a mirror whose window is not drawn was not marked");
        // The first one is drawn again (its mirror shows a frame): it can render again, but the renderer stays.
        _draw(c);
        _mirror(a).hasFrame = true;
        tryVerify(() => _link(a).stalled === false, 2000);
        wait(150);
        compare(_link(c).role, "source");
        // Now the one that renders stops being drawn, and the first one is the only one that is:
        // the renderer goes back to it.
        ignoreWarning(/\[WEK\] clone: .* its window has drawn no frame for 9 s/);
        _player(c).stalledMs = 9000;
        tryVerify(() => _player(a) !== null && _mirrors(c, a), 3000, "the renderer did not go to the screen drawn now");
        compare(_player(b), null);
    }

    function test_with_every_screen_asleep_the_renderer_stays_and_the_wake_goes_on_with_the_same_scene() {
        const a = _rig("111", { ShareRenderer: 1 }), b = _rig("111", { ShareRenderer: 2 }), c = _rig("111", { ShareRenderer: 1 });
        // b is the chosen screen and renders.
        tryVerify(() => _player(b) !== null && _mirrors(a, b) && _mirrors(c, b), 3000);
        _draw(b); _draw(a); _draw(c);
        tryVerify(() => _backends(a) === 1 && _backends(b) === 1 && _backends(c) === 1, 2000);
        const renderer = _player(b), frame = renderer.shareKey;
        const loaded = [_loader(a).item, _loader(b).item, _loader(c).item];
        // The monitors go to sleep: no window is drawn, the renderer's frames are taken by no one.
        ignoreWarning(/\[WEK\] clone: .* its window has drawn no frame for 9 s/);
        _mirror(a).windowDrawn = false;
        _mirror(c).windowDrawn = false;
        renderer.stalledMs = 9000;
        tryVerify(() => _link(b).stalled && _link(a).stalled && _link(c).stalled, 3000,
                  "the screens that are not drawn were not all marked");
        wait(400);                              // many checks later
        // Nothing moved and nothing was unloaded.
        verify(_player(b) === renderer && renderer.shareKey === frame, "the renderer was handed to a screen that is not drawn");
        compare(_link(b).role, "source");
        compare([_loader(a).item, _loader(b).item, _loader(c).item], loaded, "a backend was loaded again while asleep");
        verify(_mirror(a).frameKey === frame && _mirror(c).frameKey === frame);
        // The wake: every window is drawn again.  The same renderer goes on; the marks go.
        renderer.stalledMs = 0;
        _mirror(a).windowDrawn = true;
        _mirror(c).windowDrawn = true;
        tryVerify(() => !_link(b).stalled && !_link(a).stalled && !_link(c).stalled, 3000, "a mark stayed after the wake");
        wait(300);
        verify(_player(b) === renderer, "the scene was loaded again at the wake");
        compare([_loader(a).item, _loader(b).item, _loader(c).item], loaded);
    }

    function test_switched_off_each_screen_renders_again() {
        const a = _rig("111", { ShareRenderer: 1 }), b = _rig("111", { ShareRenderer: 1 });
        _draw(a); _draw(b);
        b.setConfig({ ShareRenderer: 0 });
        tryVerify(() => _player(b) !== null, 2000, "the mirror did not load its own renderer");
        compare(_link(b), null);
        compare(_player(b).shareKey, "");
        a.setConfig({ ShareRenderer: 0 });
        tryVerify(() => _link(a) === null && _player(a) !== null && _player(a).shareKey === "", 2000,
                  "the renderer went on publishing with the setting off");
        // And on again on both: one renderer.
        a.setConfig({ ShareRenderer: 1 });
        b.setConfig({ ShareRenderer: 1 });
        _tune(a); _tune(b);
        tryVerify(() => _mirrors(a, b) || _mirrors(b, a), 3000, "not one renderer for the two");
    }

    function test_a_new_wallpaper_plays_its_transition_on_the_sharing_screens_and_a_moved_renderer_cuts() {
        failOnWarning(/Cannot animate/);
        const cfg = { ShareRenderer: 1, TransitionDuration: 600 };
        const a = _rig("111", cfg), b = _rig("111", cfg);
        _draw(a); _draw(b);
        const old = [_loader(a).item, _loader(b).item];
        for (const rig of [a, b])
            rig.setConfig({ WallpaperWorkShopId: "222", WallpaperSource: _scenePath("222") });
        tryVerify(() => _player(a) !== null && String(_player(a).source).indexOf("/222/") > 0 && _mirrors(b, a), 3000);
        _draw(a); _draw(b);
        // As with the sharing off: the old wallpaper fades over the length set, on the screen
        // that renders and on the one that shows its frame.
        [a, b].forEach((rig, i) => {
            const f = _fade(rig);
            verify(f.running && f.target === old[i], "no transition runs on screen " + i);
            compare(f.ms, 600, "the change of wallpaper was cut on screen " + i);
            compare(f.kind, "fade");
        });
        tryVerify(() => _backends(a) === 1 && _backends(b) === 1, 3000, "an old layer outlived its transition");
        // The renderer moving to another screen is not a change of wallpaper: a cut.
        a.background().setStopReason("ext:game", true, "a game");
        tryVerify(() => _player(b) !== null, 3000);
        _draw(b);
        tryVerify(() => _backends(b) === 1, 2000);
        verify(_fade(b).ms === 0 && !_fade(b).running, "the renderer's move played a transition");
    }

    function test_a_reload_after_sleep_keeps_the_renderer_where_it_is_and_the_pictures_until_it_draws() {
        const list = [_rig("111", { ShareRenderer: 1 }), _rig("111", { ShareRenderer: 1 }), _rig("111", { ShareRenderer: 1 })];
        const [a, b, c] = list;
        list.forEach(r => _draw(r));
        const first = list.map(r => _loader(r).item), oldKey = _player(a).shareKey;
        for (const r of list) {
            r.background().resumeDelayMs = 30;
            r._find(r.mainItem, o => typeof o.resumedFromSleep === "function").resumedFromSleep("suspend", "logind", 3600000);
        }
        // Every screen loads again; the same screen renders, under a new frame name, and the
        // other two follow it.
        tryVerify(() => list.every((r, i) => _loader(r).item !== first[i]) && _player(a) !== null
                        && _mirrors(b, a) && _mirrors(c, a), 3000, "the screens did not settle after the reload");
        verify(_player(a).shareKey !== oldKey, "the reloaded renderer publishes under the old name");
        // The old renderer and the old mirrors stay until the new frame.
        list.forEach((r, i) => compare(_loader(r)._outgoing, first[i], "an old layer went before the new frame"));
        list.forEach(r => _draw(r));
        tryVerify(() => list.every(r => _backends(r) === 1), 3000, "an old layer stayed");
    }

    function test_the_rendering_screen_alone_gets_another_wallpaper_and_the_others_keep_theirs() {
        const a = _rig("111", { ShareRenderer: 1 }), b = _rig("111", { ShareRenderer: 1 }), c = _rig("111", { ShareRenderer: 1 });
        _draw(a); _draw(b); _draw(c);
        a.setConfig({ WallpaperWorkShopId: "222", WallpaperSource: _scenePath("222") });
        // It renders the new one; one of the other two takes the old scene and the third shows that.
        tryVerify(() => _player(a) !== null && String(_player(a).source).indexOf("/222/") > 0, 3000);
        tryVerify(() => _mirrors(c, b) || _mirrors(b, c), 3000, "the two screens left did not settle on one renderer");
        const taker = _player(b) !== null ? b : c;
        verify(String(_player(taker).source).indexOf("/111/") > 0, "the screens left do not show their own wallpaper");
        compare(_link(a).role, "source");
    }

    function test_a_new_wallpaper_on_both_keeps_one_renderer_and_the_old_picture_until_it_draws() {
        const a = _rig("111", { ShareRenderer: 1 }), b = _rig("111", { ShareRenderer: 1 });
        _draw(a); _draw(b);
        const old = _loader(a).item, oldMirror = _loader(b).item, oldKey = _player(a).shareKey;
        for (const rig of [a, b])
            rig.setConfig({ WallpaperWorkShopId: "222", WallpaperSource: _scenePath("222") });
        tryVerify(() => _player(a) !== null && String(_player(a).source).indexOf("/222/") > 0, 2000);
        tryVerify(() => _mirrors(b, a), 2000);
        verify(_player(a).shareKey !== oldKey, "the new scene publishes under the old scene's name");
        // Both old layers stay until the new frame: the old renderer, and the mirror of it.
        verify(_loader(a)._outgoing === old && _loader(b)._outgoing === oldMirror, "an old layer went before the new frame");
        _draw(a); _draw(b);
        tryVerify(() => _backends(a) === 1 && _backends(b) === 1, 3000, "an old layer stayed");
        compare(_player(b), null, "still one renderer");
    }
}
