import QtQuick
import QtTest
import Helpers 1.0
import com.github.captsilver.wallpaperEngineKde 1.2
import "_stubs/com/github/captsilver/wallpaperEngineKde/CloneGroupData.js" as CloneData

// One scene stretched across screens (we-manager span patch, SpanGroup)
// through the REAL main.qml, CloneLink.qml, SpanCanvas.qml, backend/Scene.qml
// and backend/SceneMirror.qml with the real js/span.mjs, one ScreenRig per
// screen, each at its own place (ScreenRig.screenGeometry is what Plasma
// hands the wallpaper as its screen's rectangle).  The stubs stand in for
// the C++ only, as in tst_main_clone.qml.  What is checked is what each
// screen is given to draw: the size and place of the rendering item on the
// screen that renders, and on the others the place of the mirror item and
// the part of the frame it shows.  tst_spanframe.cpp shows the same geometry
// drawn in three windows, and the pointer.
TestCase {
    id: tc
    name: "Main_Span"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }
    // The desktop's pointer hook, as main.qml's doHookMouse() makes it.
    Component { id: hookComp; MouseGrabber {} }
    property var rigs: []

    // A desk like the user's: a 2560x1440 screen between two 1920x1200 ones.
    readonly property rect leftRect: Qt.rect(0, 0, 1920, 1200)
    readonly property rect midRect: Qt.rect(1920, 0, 2560, 1440)
    readonly property rect rightRect: Qt.rect(4480, 0, 1920, 1200)

    function _scenePath(id) {
        return "file:///tmp/fakelib/steamapps/workshop/content/431960/" + id + "/scene.pkg+scene";
    }
    function _loader(rig) { return rig._find(rig.mainItem, o => typeof o.dropOutgoing === "function"); }
    function _player(rig) {
        const l = _loader(rig);
        return l && l.item ? rig._find(l.item, o => typeof o.setAcceptMouse === "function") : null;
    }
    function _mirror(rig) {
        const l = _loader(rig);
        return l && l.item ? rig._find(l.item, o => typeof o.mapToSource === "function") : null;
    }
    function _link(rig) { return rig.background().cloneLink; }
    function _backends(rig) {
        return _loader(rig).children.filter(o => typeof o.frameShown !== "undefined").length;
    }
    function _tune(rig) {
        const link = _link(rig);
        if (link) {
            link.followDelayMs = 20;
            link.spanFollowDelayMs = 20;
            link.stallCheckMs = 40;
            link.stallClearMs = 20;
            link.probeMs = 40;
        }
    }
    function _make(geometry, config, id) {
        failOnWarning(/wallpaper is not defined/);
        failOnWarning(/Unable to assign \[undefined\]/);
        failOnWarning(/TypeError/);
        failOnWarning(/anchor/);
        const wid = id || "111";
        // Cover (1), not the fake's Keep aspect ratio: the picture fills the canvas.
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: wid, WallpaperSource: _scenePath(wid),
                    DisplayMode: 1, SpanGroup: 1 };
        for (const k in (config || {})) c[k] = config[k];
        const rig = rigComp.createObject(tc, { screenGeometry: geometry, initialConfig: c });
        verify(rig !== null);
        rigs.push(rig);
        return rig;
    }
    // Screens that load in the same moment, as at a login or when we-manager
    // writes the group; settled when one renders and the others show its frame.
    function _desk(geometries, configs) {
        const out = [];
        for (let i = 0; i < geometries.length; i++) out.push(_make(geometries[i], (configs || [])[i]));
        for (const rig of out) {
            tryVerify(() => rig.mainItem !== null && rig.background() !== null && _loader(rig).item !== null, 2000);
            _tune(rig);
        }
        tryVerify(() => _settled(out), 3000, "the group did not settle on one renderer");
        return out;
    }
    function _settled(all) {
        const src = all.filter(r => _player(r) !== null);
        if (src.length !== 1 || _player(src[0]).shareKey === "") return false;
        return all.every(r => r === src[0] || (_mirror(r) !== null && _mirror(r).frameKey === _player(src[0]).shareKey));
    }
    function _source(all) { return all.filter(r => _player(r) !== null)[0]; }
    // The frame arrives on every screen: the renderer's first frame, and the
    // mirrors learn the frame's shape (the rendering item's) with it.
    function _draw(all) {
        const src = _source(all), v = _player(src);
        v.firstFrame();
        for (const r of all) {
            if (r === src) continue;
            _mirror(r).frameAspect = v.width / v.height;
            _mirror(r).hasFrame = true;
        }
    }
    function _near(a, b) { return Math.abs(a - b) < 1e-6; }
    // The rendering item's rectangle in its screen's own coordinates.
    function _viewerRect(rig) {
        const v = _player(rig), p = v.mapToItem(rig.mainItem, 0, 0);
        return [Math.round(p.x), Math.round(p.y), Math.round(v.width), Math.round(v.height)];
    }
    // What a mirror draws: its item's rectangle on its screen, and the part of the frame.
    function _part(rig) {
        const v = _mirror(rig), p = v.mapToItem(rig.mainItem, 0, 0), s = v.sourceRect;
        const r4 = x => Math.round(x * 10000) / 10000;
        return [Math.round(p.x), Math.round(p.y), Math.round(v.width), Math.round(v.height),
                r4(s.x), r4(s.y), r4(s.width), r4(s.height)];
    }
    function cleanup() {
        for (const r of rigs) r.destroy();
        rigs = [];
        tryVerify(() => CloneData.all.length === 0, 2000, "a screen's entry outlived its rig");
    }

    function test_three_screens_of_a_group_show_one_picture_each_its_own_part() {
        const [l, m, r] = _desk([leftRect, midRect, rightRect]);
        // No screen was chosen: the largest renders.  Its viewer is the whole canvas, 6400x1440,
        // and lies 1920 to the left of the window, so the window shows the canvas's middle.
        compare(_source([l, m, r]), m);
        compare(_viewerRect(m), [-1920, 0, 6400, 1440]);
        compare(_link(m).canvas, Qt.rect(0, 0, 6400, 1440));
        verify(_link(m).lastLine.indexOf("span 1: ") > 0 && _link(m).lastLine.indexOf("canvas of 6400x1440 at 0,0") > 0
               && _link(m).lastLine.indexOf("about 6400x1440 pixels") > 0, _link(m).lastLine);
        compare(_player(m).renderScale, 1.0, "the canvas is inside the limit: rendered at the screen's scale");
        // The viewer is still the backend's own one, and the pointer's target.
        compare(m.background().nowBackend, "scene");
        _draw([l, m, r]);
        // The side screens show the first and the last 30 % of the frame, 1200 of its 1440 rows.
        compare(_part(l), [0, 0, 1920, 1200, 0, 0, 0.3, 0.8333]);
        compare(_part(r), [0, 0, 1920, 1200, 0.7, 0, 0.3, 0.8333]);
        // One renderer for the three.
        compare(_player(l), null);
        compare(_player(r), null);
        compare(_backends(m), 1);
        compare(m.background().vramEstimateNow >= 0, true);
        compare(l.background().vramEstimateNow, 0, "a screen that shows the frame holds no video memory");
    }

    function test_the_chosen_screen_renders_and_every_part_stays_where_it_is() {
        const [l, m, r] = _desk([leftRect, midRect, rightRect], [{ ShareRenderer: 2 }, {}, {}]);
        compare(_source([l, m, r]), l, "the screen set to render does not render");
        // The canvas begins at this screen's left edge.
        compare(_viewerRect(l), [0, 0, 6400, 1440]);
        _draw([l, m, r]);
        compare(_part(m), [0, 0, 2560, 1440, 0.3, 0, 0.4, 1]);
        compare(_part(r), [0, 0, 1920, 1200, 0.7, 0, 0.3, 0.8333]);
    }

    function test_a_screen_that_is_unplugged_lays_the_picture_out_over_the_screens_left() {
        const [l, m, r] = _desk([leftRect, midRect, rightRect]);
        _draw([l, m, r]);
        const first = _player(m), frame = first.shareKey;
        // The right screen goes (its wallpaper with it).
        rigs = rigs.filter(x => x !== r);
        r.destroy();
        // The canvas is rendered anew for the two left: another renderer, 4480 wide.
        tryVerify(() => _player(m) !== null && _player(m) !== first && _player(m).shareKey !== frame, 3000,
                  "the canvas was not rendered again for the screens left");
        compare(_viewerRect(m), [-1920, 0, 4480, 1440]);
        compare(_link(m).canvas, Qt.rect(0, 0, 4480, 1440));
        // The old picture stays on both screens until the new one draws.
        compare(_backends(m), 2);
        tryVerify(() => _settled([l, m]), 3000);
        _draw([l, m]);
        tryVerify(() => _backends(m) === 1 && _backends(l) === 1, 3000, "an old layer stayed");
        compare(_part(l), [0, 0, 1920, 1200, 0, 0, 0.4286, 0.8333]);
        // The rendering screen goes too: the one left renders, and its canvas is itself.
        rigs = rigs.filter(x => x !== m);
        m.destroy();
        tryVerify(() => _player(l) !== null, 3000, "the last screen got no renderer");
        compare(_viewerRect(l), [0, 0, 1920, 1200]);
        compare(_link(l).canvas, leftRect);
    }

    function test_a_screen_that_joins_gets_its_part_when_the_canvas_is_rendered_again() {
        const [l, m] = _desk([leftRect, midRect]);
        _draw([l, m]);
        compare(_viewerRect(m), [-1920, 0, 4480, 1440]);
        const first = _player(m);
        // A third screen comes (plugged in, or given the wallpaper and the group).
        const r = _make(rightRect);
        tryVerify(() => r.background() !== null && _loader(r).item !== null, 2000);
        _tune(r);
        tryVerify(() => _player(m) !== null && _player(m) !== first, 3000, "the canvas did not grow for the new screen");
        compare(_viewerRect(m), [-1920, 0, 6400, 1440]);
        tryVerify(() => _settled([l, m, r]), 3000);
        _draw([l, m, r]);
        compare(_part(r), [0, 0, 1920, 1200, 0.7, 0, 0.3, 0.8333]);
        compare(_part(l), [0, 0, 1920, 1200, 0, 0, 0.3, 0.8333]);
        compare(_player(r), null, "the new screen loaded a renderer of its own");
    }

    function test_a_stopped_screen_keeps_its_place_and_the_others_their_parts() {
        const [l, m, r] = _desk([leftRect, midRect, rightRect]);
        _draw([l, m, r]);
        const renderer = _player(m), frame = renderer.shareKey, shown = _loader(l).item;
        // The right screen stops (a window rule set to Stop, the battery, video memory).
        r.background().setStopReason("ext:game", true, "a game");
        tryVerify(() => r.background().stopped === true, 2000);
        wait(150);                              // many follows later
        // The same renderer draws the same canvas, and the left screen shows what it showed.
        verify(_player(m) === renderer && renderer.shareKey === frame, "the scene was loaded again for a stop");
        compare(_viewerRect(m), [-1920, 0, 6400, 1440]);
        verify(_loader(l).item === shown);
        compare(_part(l), [0, 0, 1920, 1200, 0, 0, 0.3, 0.8333]);
        // The stopped screen is in nobody's count: with the left one paused too, the renderer
        // has no one to play for but its own screen.
        compare(_link(m).members().filter(e => e.key !== "").length, 2);
        // The rendering screen stops as well: the left one takes the rendering, on the same canvas.
        m.background().setStopReason("ext:game", true, "a game");
        tryVerify(() => m.background().stopped === true && _player(l) !== null, 3000,
                  "nobody renders after the rendering screen stopped");
        compare(_viewerRect(l), [0, 0, 6400, 1440]);
        _player(l).firstFrame();
        // Both stops end: the two come back as screens that show the frame, each with its part.
        m.background().setStopReason("ext:game", false);
        r.background().setStopReason("ext:game", false);
        tryVerify(() => _settled([l, m, r]) && _source([l, m, r]) === l, 3000, "the stopped screens did not come back");
        _draw([l, m, r]);
        compare(_part(m), [0, 0, 2560, 1440, 0.3, 0, 0.4, 1]);
        compare(_part(r), [0, 0, 1920, 1200, 0.7, 0, 0.3, 0.8333]);
        compare(_viewerRect(l), [0, 0, 6400, 1440]);
    }

    function test_a_stopped_screen_given_another_wallpaper_gives_its_place_up() {
        const [l, m, r] = _desk([leftRect, midRect, rightRect]);
        _draw([l, m, r]);
        const first = _player(m);
        r.background().setStopReason("ext:game", true, "a game");
        tryVerify(() => r.background().stopped === true, 2000);
        wait(100);
        verify(_player(m) === first);
        // Still stopped, it is given another wallpaper: it is not part of this picture any more.
        r.setConfig({ WallpaperWorkShopId: "222", WallpaperSource: _scenePath("222") });
        tryVerify(() => _player(m) !== null && _player(m) !== first, 3000, "the canvas kept a place for another wallpaper");
        compare(_viewerRect(m), [-1920, 0, 4480, 1440]);
    }

    function test_screens_of_another_group_or_of_none_are_not_part_of_the_picture() {
        // Four screens with the same scene: two in group 1, one in group 2, one that only
        // shares its renderer (ShareRenderer on, no group).
        const far = Qt.rect(6400, 0, 1920, 1080);
        const a = _make(leftRect), b = _make(midRect), c = _make(rightRect, { SpanGroup: 2 }),
              d = _make(far, { SpanGroup: 0, ShareRenderer: 1 });
        for (const rig of [a, b, c, d]) {
            tryVerify(() => rig.background() !== null && _loader(rig).item !== null, 2000);
            _tune(rig);
        }
        tryVerify(() => _settled([a, b]) && _player(c) !== null && _player(d) !== null, 3000);
        // Group 1 is the two left screens, 4480 wide.
        compare(_source([a, b]), b);
        compare(_viewerRect(b), [-1920, 0, 4480, 1440]);
        // Group 2 is one screen: its canvas is that screen, the whole picture on it.
        compare(_viewerRect(c), [0, 0, 1920, 1200]);
        compare(_link(c).canvas, rightRect);
        // The screen without a group renders the scene for itself, as with ShareRenderer alone:
        // no canvas, its viewer straight in its backend.
        compare(_viewerRect(d), [0, 0, 1920, 1080]);
        compare(_link(d).canvas.width, 0);
        compare(_player(d).parent, _loader(d).item);
        verify(_player(d).shareKey !== "" && _player(d).shareKey !== _player(b).shareKey);
        // It joins group 1: now part of the picture.
        d.setConfig({ SpanGroup: 1 });
        tryVerify(() => _settled([a, b, d]), 3000, "the screen did not join the group");
        tryVerify(() => _link(_source([a, b, d])).canvas.width === 8320, 3000);
    }

    function test_the_group_switched_off_every_screen_shows_the_whole_picture_again() {
        const [l, m] = _desk([leftRect, midRect]);
        _draw([l, m]);
        const oldViewer = _player(m), oldPart = _mirror(l);
        for (const rig of [l, m]) rig.setConfig({ SpanGroup: 0 });
        tryVerify(() => _link(l) === null && _link(m) === null && _player(l) !== null && _player(m) !== null, 3000,
                  "the screens did not load their own renderers");
        // Until the new renderers have drawn, the old layers show what they showed: the
        // canvas under the middle window, the left screen its part of the frame.
        compare(_backends(m), 2);
        const at = oldViewer.mapToItem(m.mainItem, 0, 0);
        compare([Math.round(at.x), Math.round(oldViewer.width)], [-1920, 4480]);
        verify(_near(oldPart.sourceRect.width, 1920 / 4480) && Math.round(oldPart.width) === 1920,
               "the old layer lost its part when the group was switched off");
        for (const rig of [l, m]) _player(rig).firstFrame();
        tryVerify(() => _backends(m) === 1 && _backends(l) === 1, 3000);
        // As without the patch: each viewer in its own backend, its screen's size, nothing published.
        for (const rig of [l, m]) {
            compare(_player(rig).parent, _loader(rig).item);
            compare(_player(rig).shareKey, "");
        }
        compare(_viewerRect(l), [0, 0, 1920, 1200]);
        compare(_viewerRect(m), [0, 0, 2560, 1440]);
        // With ShareRenderer left on, switching the group off leaves a clone: one renderer, and
        // the other screen shows the rendering screen's whole picture, by its own alignment.
        for (const rig of [l, m]) rig.setConfig({ SpanGroup: 1, ShareRenderer: 1 });
        for (const rig of [l, m]) _tune(rig);
        tryVerify(() => _settled([l, m]) && _link(_source([l, m])).canvas.width === 4480, 3000);
        for (const rig of [l, m]) rig.setConfig({ SpanGroup: 0 });
        tryVerify(() => _settled([l, m]) && _link(_source([l, m])).canvas.width === 0, 3000,
                  "the group did not become screens that share a renderer");
        const src = _source([l, m]), other = src === l ? m : l;
        compare(_viewerRect(src), [0, 0, src.width, src.height]);
        compare(_player(src).parent, _loader(src).item);
        _draw([l, m]);
        compare(_mirror(other).width, other.width);
        verify(_mirror(other).sourceRect.width < 1 || _mirror(other).sourceRect.height < 1,
               "a clone crops the other screen's picture to this screen's shape");
    }

    function test_a_group_set_changed_and_cleared_on_screens_that_are_running() {
        // As from we-manager on a running desktop: nothing is on when the screens load, so
        // each link is made later, by the setting.
        const l = _make(leftRect, { SpanGroup: 0 }), m = _make(midRect, { SpanGroup: 0 });
        for (const rig of [l, m])
            tryVerify(() => rig.background() !== null && _player(rig) !== null, 2000);
        compare(_link(l), null);
        for (const rig of [l, m]) rig.setConfig({ SpanGroup: 1 });
        for (const rig of [l, m]) _tune(rig);
        tryVerify(() => _settled([l, m]) && _link(_source([l, m])).canvas.width === 4480, 3000,
                  "the group set on running screens did not stretch the picture");
        _draw([l, m]);
        // One of them is given another group: two pictures, each screen its own canvas.
        l.setConfig({ SpanGroup: 2 });
        tryVerify(() => _player(l) !== null && _player(m) !== null && _link(l).canvas.width === 1920
                        && _link(m).canvas.width === 2560, 3000, "the screen did not leave the group");
        // The other follows it into group 2: one picture again.
        m.setConfig({ SpanGroup: 2 });
        tryVerify(() => _settled([l, m]) && _link(_source([l, m])).canvas.width === 4480, 3000,
                  "the screens of the new group did not join");
        verify(_link(l).key.indexOf("span2") === 0, _link(l).key);
        // ShareRenderer is on as well, as we-manager's span leaves it; the group is cleared:
        // the two go on as screens that share a renderer.
        for (const rig of [l, m]) rig.setConfig({ ShareRenderer: 1 });
        for (const rig of [l, m]) rig.setConfig({ SpanGroup: 0 });
        tryVerify(() => _settled([l, m]) && _link(_source([l, m])).canvas.width === 0
                        && _link(l).key.indexOf("span") < 0 && _link(m).key.indexOf("span") < 0, 3000,
                  "the group cleared on running screens is still a group");
        const src = _source([l, m]);
        compare(_player(src).parent, _loader(src).item);
    }

    function test_the_pointer_of_every_screen_goes_through_the_groups_item_with_the_scenes_point() {
        const [l, m, r] = _desk([leftRect, midRect, rightRect]);
        _draw([l, m, r]);
        // The scene's shape becomes known when it has loaded: a 16:9 scene with Cover, of
        // which the 6400x1440 canvas shows the whole width and the middle 40 % of the height.
        // The other screens are told at once, not after the group's wait (set long here).
        for (const rig of [l, r]) _link(rig).spanFollowDelayMs = 60000;
        _player(m).nativeAspectRatio = 16 / 9;
        const hooks = [];
        for (const rig of [l, m, r]) {
            const hook = hookComp.createObject(rig);
            rig.background().mouseHooker = hook;
            hooks.push(hook);
        }
        const rect = pad => {
            const s = pad.sourceRect, r4 = x => Math.round(x * 10000) / 10000;
            return [r4(s.x), r4(s.y), r4(s.width), r4(s.height)];
        };
        // Each screen's pointer goes to the group's own item, not to the viewer or the
        // mirror: an item the size of the screen, on the renderer's frame.
        for (let i = 0; i < 3; i++) {
            const rig = [l, m, r][i], pad = _link(rig).pointerPad;
            verify(pad !== null && hooks[i].target === pad, "the pointer does not go through the group's item");
            compare([pad.width, pad.height], [rig.width, rig.height]);
            compare(pad.frameKey, _player(m).shareKey);
            compare(pad.opacity, 0);
        }
        verify(hooks[1].target !== _player(m) && hooks[0].target !== _mirror(l));
        // The rectangle each maps its screen through: the screen's 30 or 40 % of the width,
        // and its rows drawn into the middle 40 % of the scene's height (0.3 to 0.7).  The
        // other screens learn the 40 % from the rendering screen's entry.
        compare(rect(_link(m).pointerPad), [0.3, 0.3, 0.4, 0.4]);
        tryVerify(() => Math.abs(_link(l).pointerPad.sourceRect.height - 0.4 * 1200 / 1440) < 1e-6, 500,
                  "the screen did not learn at once how much of the scene the frame shows");
        compare(_link(l).pointerPad.live, false, "the pointer's item follows the renderer's frames");
        compare(rect(_link(l).pointerPad), [0, 0.3, 0.3, 0.3333]);
        compare(rect(_link(r).pointerPad), [0.7, 0.3, 0.3, 0.3333]);
        // Stretch on the rendering screen: nothing is cut, the rectangle is the screen's place.
        m.setConfig({ DisplayMode: 2 });
        tryVerify(() => Math.abs(_link(l).pointerPad.sourceRect.height - 1200 / 1440) < 1e-6, 2000);
        compare(rect(_link(m).pointerPad), [0.3, 0, 0.4, 1]);
        compare(rect(_link(r).pointerPad), [0.7, 0, 0.3, 0.8333]);
        // A stop takes the hook's target away with the backend, as always.
        r.background().setStopReason("ext:game", true, "a game");
        tryVerify(() => r.background().stopped === true && hooks[2].target === null, 2000);
        compare(_link(r).pointerPad, null);
    }

    function test_without_a_group_the_pointer_goes_where_it_went() {
        // ShareRenderer only: the clone patch's targets, the viewer and the mirror item.
        const a = _make(leftRect, { SpanGroup: 0, ShareRenderer: 1 }), b = _make(midRect, { SpanGroup: 0, ShareRenderer: 1 });
        for (const rig of [a, b]) {
            tryVerify(() => rig.background() !== null && _loader(rig).item !== null, 2000);
            _tune(rig);
        }
        tryVerify(() => _settled([a, b]), 3000);
        _draw([a, b]);
        const src = _source([a, b]), other = src === a ? b : a;
        for (const rig of [a, b]) rig.background().mouseHooker = hookComp.createObject(rig);
        compare(src.background().mouseHooker.target, _player(src));
        compare(other.background().mouseHooker.target, _mirror(other));
        compare(_link(src).pointerPad, null);
        compare(_link(other).pointerPad, null);
    }

    function test_screens_that_come_up_one_after_the_other_cost_one_load() {
        // As at a login or a wake, and as we-manager writes a group (the screen that renders
        // first, the others in a second script): the second screen is there half a second
        // after the first.  The waits are the plugin's own here.
        const m = _make(midRect);
        tryVerify(() => m.background() !== null && _loader(m).item !== null, 2000);
        wait(500);
        compare(_player(m), null, "the first screen began the scene for itself before the group was there");
        const l = _make(leftRect);
        tryVerify(() => _settled([l, m]), 5000, "the group did not settle");
        // One renderer was made, and for the whole canvas at once.
        compare(_viewerRect(m), [-1920, 0, 4480, 1440]);
        compare(_backends(m), 1);
        verify(_link(m).lastLine.indexOf("canvas of 4480x1440") > 0, _link(m).lastLine);
        compare(_loader(m)._outgoing, null, "a renderer for the first screen alone was made and dropped");
    }

    function test_a_group_whose_screens_have_no_rectangle_says_so_and_stretches_when_they_have_one() {
        // Plasma has not told the wallpaper its screen yet.
        ignoreWarning(/\[WEK\] span: .* could not lay out the canvas of group 1 .*no screen of the group has a rectangle/);
        const a = _make(Qt.rect(0, 0, 0, 0));
        tryVerify(() => a.background() !== null && _loader(a).item !== null, 2000);
        _tune(a);
        tryVerify(() => _player(a) !== null, 3000);
        // It renders its picture as without a group, and says why in one line.
        compare(_player(a).parent, _loader(a).item);
        verify(_link(a).lastLine.indexOf("[WEK] span: ") === 0 && _link(a).lastLine.indexOf("whole picture") > 0,
               _link(a).lastLine);
        compare(_link(a).pointerPad, null);
        const first = _player(a);
        // The rectangle arrives: the canvas is laid out after all.
        a.screenGeometry = leftRect;
        tryVerify(() => _player(a) !== null && _player(a) !== first && _link(a).canvas.width === 1920, 3000,
                  "the canvas was not laid out when the screen got its rectangle");
        compare(_viewerRect(a), [0, 0, 1920, 1200]);
    }

    function test_two_screens_that_change_places_keep_the_renderer_and_swap_their_parts() {
        const a = Qt.rect(0, 0, 1920, 1080), b = Qt.rect(1920, 0, 1920, 1080);
        const [x, y] = _desk([a, b]);
        _draw([x, y]);
        const src = _source([x, y]), other = src === x ? y : x, renderer = _player(src);
        compare(_viewerRect(src)[0], src === x ? 0 : -1920);
        // Plasma swaps the two screens' places: the canvas is the same rectangle.
        x.screenGeometry = b;
        y.screenGeometry = a;
        wait(150);
        verify(_player(src) === renderer, "the scene was loaded again though the canvas did not change");
        compare(_viewerRect(src)[0], src === x ? -1920 : 0);
        compare(_part(other).slice(4), src === x ? [0, 0, 0.5, 1] : [0.5, 0, 0.5, 1]);
    }

    function test_keep_aspect_ratio_fits_the_scene_into_the_canvas() {
        const [l, m, r] = _desk([leftRect, midRect, rightRect], [{ DisplayMode: 0 }, { DisplayMode: 0 }, { DisplayMode: 0 }]);
        const v = _player(m);
        // The scene is 32:9: its picture is 5120x1440 in the middle of the 6400 wide canvas.
        v.nativeAspectRatio = 32 / 9;
        tryVerify(() => Math.round(v.width) === 5120, 2000);
        compare(_viewerRect(m), [-1280, 0, 5120, 1440]);
        _draw([l, m, r]);
        // The side screens show the strips of it that reach them; the rest is background colour.
        compare(_part(l), [640, 0, 1280, 1200, 0, 0, 0.25, 0.8333]);
        compare(_part(r), [0, 0, 1280, 1200, 0.75, 0, 0.25, 0.8333]);
        // A 16:9 scene does not reach the side screens at all: they draw nothing.
        v.nativeAspectRatio = 16 / 9;
        tryVerify(() => Math.round(v.width) === 2560, 2000);
        for (const rig of [l, r]) _mirror(rig).frameAspect = 16 / 9;
        compare(_mirror(l).width, 0);
        compare(_mirror(r).width, 0);
        compare(_viewerRect(m), [0, 0, 2560, 1440]);
    }

    function test_a_canvas_beyond_the_limit_is_rendered_smaller_and_the_users_scale_is_kept() {
        const big = [Qt.rect(0, 0, 7680, 4320), Qt.rect(7680, 0, 7680, 4320), Qt.rect(15360, 0, 7680, 4320)];
        const all = _desk(big);
        const v = _player(_source(all));
        // 23040 pixels wide: brought to 16384.
        verify(_near(v.renderScale, 16384 / 23040), "render scale " + v.renderScale);
        verify(_link(_source(all)).lastLine.indexOf("about 16384x3072 pixels (scaled down for the limit of 16384 pixels a side)") > 0,
               _link(_source(all)).lastLine);
        compare(Math.round(v.width), 23040, "the item is still the whole canvas");
        cleanup();
        // RenderScale 50 already brings it under the limit: it is the user's.
        const half = _desk(big, [{ RenderScale: 50 }, { RenderScale: 50 }, { RenderScale: 50 }]);
        compare(_player(_source(half)).renderScale, 0.5);
    }

    function test_a_pause_on_one_screen_does_not_freeze_the_picture_on_the_others() {
        const [l, m, r] = _desk([leftRect, midRect, rightRect]);
        _draw([l, m, r]);
        wait(400);                              // main.qml's own pause 300 ms after start is over
        const scene = _loader(m).item;
        // A fullscreen window on the rendering screen: the renderer plays on for the other two.
        m.playlistController().pause();
        tryVerify(() => m.background().ok === false, 2000);
        tryVerify(() => scene._paused === false, 2000, "the renderer paused while two screens show its picture");
        // Every screen paused: now it pauses.
        l.playlistController().pause();
        r.playlistController().pause();
        tryVerify(() => scene._paused === true, 2000, "the renderer kept playing with every screen paused");
        l.playlistController().resume();
        tryVerify(() => scene._paused === false, 2000);
    }

    // main.qml's transition animation (transitions patch).
    function _fade(rig) { return rig._find(rig.mainItem, o => typeof o.incoming !== "undefined" && typeof o.kind === "string"); }
    // How many of the window's columns a backend layer's picture covers: its rendering or
    // mirror item, cut off at the layer's own item where that clips.
    function _seen(rig, layer) {
        const v = rig._find(layer, o => typeof o.setAcceptMouse === "function")
                  || rig._find(layer, o => typeof o.mapToSource === "function" && o.opacity > 0);
        let a = v.mapToItem(rig.mainItem, 0, 0).x, b = a + v.width;
        if (layer.clip) {
            const at = layer.mapToItem(rig.mainItem, 0, 0).x;
            a = Math.max(a, at);
            b = Math.min(b, at + layer.width);
        }
        return Math.max(0, Math.min(b, rig.width) - Math.max(a, 0));
    }

    function test_a_slide_moves_each_screens_own_part_on_the_rendering_screen_too() {
        failOnWarning(/Cannot animate/);
        const cfg = { TransitionDuration: 600, TransitionType: "slide" };
        const [l, m] = _desk([leftRect, midRect], [cfg, cfg]);
        _draw([l, m]);
        compare(_source([l, m]), m);
        const olds = [_loader(l).item, _loader(m).item];
        // Standing still, the rendering screen's window is covered by its part of the canvas.
        compare(_seen(m, olds[1]), 2560);
        for (const rig of [l, m])
            rig.setConfig({ WallpaperWorkShopId: "222", WallpaperSource: _scenePath("222") });
        tryVerify(() => _settled([l, m]) && String(_player(m).source).indexOf("/222/") > 0, 3000);
        _draw([l, m]);
        tryVerify(() => _fade(m).running && _fade(l).running, 1000, "no transition runs");
        compare(_fade(m).kind, "slide");
        // Part of the way: on each screen the old picture has left as much of the window as
        // the new one has entered, the screen that renders the canvas like the other.  (An
        // old canvas that is not cut off at its screen would still cover the whole window.)
        wait(300);
        [l, m].forEach((rig, i) => {
            const w = rig.width, old = _seen(rig, olds[i]), now = _seen(rig, _loader(rig).item);
            verify(old > 0.1 * w && old < 0.9 * w, "screen " + i + ": the old picture covers " + old + " of " + w);
            verify(Math.abs(old + now - w) < 2, "screen " + i + ": old " + old + " and new " + now + " do not make " + w);
        });
        tryVerify(() => _backends(l) === 1 && _backends(m) === 1, 3000, "an old layer stayed");
        compare(_seen(m, _loader(m).item), 2560);
        compare(_seen(l, _loader(l).item), 1920);
        // Without a group nothing is cut off: the backend's item is as the clone patch has it.
        compare(_loader(m).item.clip, true);
        m.setConfig({ SpanGroup: 0 });
        tryVerify(() => _link(m) === null && _player(m) !== null, 3000);
        compare(_loader(m).item.clip, false);
    }

    function test_a_new_wallpaper_on_the_group_keeps_the_old_picture_until_the_new_canvas_draws() {
        failOnWarning(/Cannot animate/);
        const cfg = { TransitionDuration: 600 };
        const [l, m] = _desk([leftRect, midRect], [cfg, cfg]);
        _draw([l, m]);
        const old = _loader(m).item, oldMirror = _loader(l).item;
        for (const rig of [l, m])
            rig.setConfig({ WallpaperWorkShopId: "222", WallpaperSource: _scenePath("222") });
        tryVerify(() => _settled([l, m]) && String(_player(_source([l, m])).source).indexOf("/222/") > 0, 3000);
        compare(_source([l, m]), m);
        verify(_loader(m)._outgoing === old && _loader(l)._outgoing === oldMirror, "an old layer went before the new frame");
        compare(_viewerRect(m), [-1920, 0, 4480, 1440]);
        // The old layer still shows its own part of the old frame.
        const oldPart = oldMirror.children.filter(o => typeof o.mapToSource === "function")[0];
        verify(_near(oldPart.sourceRect.width, 1920 / 4480));
        _draw([l, m]);
        // As on screens of their own, the change of wallpaper plays its transition, on the
        // screen that renders the canvas and on the one that shows its part: the old part
        // fades over the length set.
        const olds = [oldMirror, old];
        [l, m].forEach((rig, i) => {
            const f = _fade(rig);
            verify(f.running && f.target === olds[i], "no transition runs on screen " + i);
            compare(f.ms, 600, "the change of wallpaper was cut on screen " + i);
            compare(f.kind, "fade");
        });
        tryVerify(() => _backends(l) === 1 && _backends(m) === 1, 3000, "an old layer stayed");
        compare(_part(l), [0, 0, 1920, 1200, 0, 0, 0.4286, 0.8333]);
        // A canvas rendered again because the group changed is not a change of wallpaper: a cut.
        const first = _player(m);
        const r = _make(rightRect, cfg, "222");
        tryVerify(() => r.background() !== null && _loader(r).item !== null, 2000);
        _tune(r);
        tryVerify(() => _settled([l, m, r]) && _player(m) !== first, 3000);
        _draw([l, m, r]);
        tryVerify(() => _backends(m) === 1 && _backends(l) === 1, 3000);
        verify(_fade(m).ms === 0 && !_fade(m).running, "the canvas laid out again played a transition");
        verify(_fade(l).ms === 0 && !_fade(l).running, "a part of the canvas laid out again played a transition");
    }
}
