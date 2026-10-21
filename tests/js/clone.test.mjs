// The rules of ShareRenderer (we-manager clone patch): which screen renders
// for a group, when it moves, when the renderer plays on for the others, and
// where a mirror puts the frame.  The same file runs in the plugin
// (CloneLink.qml, backend/SceneMirror.qml).
import { test } from 'node:test';
import assert from 'node:assert/strict';

import { groupKey, elect, memberById, othersPlaying, others, mirrorPlacement, rendering, mayTake }
    from '../../plugin/contents/ui/js/clone.mjs';

const K = groupKey("/lib/431960/111/scene.pkg+scene", "");
const m = (id, o = {}) => ({
    id, key: K, screen: "DP-" + id, prefer: false, area: 1920 * 1080, role: "mirror", frameKey: "",
    playing: true, stalled: false, ...o,
});

test('the group is the scene file with its user properties', () => {
    assert.equal(groupKey("", '{"a":1}'), "", "no scene, no group");
    assert.equal(groupKey("/a/scene.pkg", ""), groupKey("/a/scene.pkg", undefined));
    assert.notEqual(groupKey("/a/scene.pkg", ""), groupKey("/b/scene.pkg", ""));
    assert.notEqual(groupKey("/a/scene.pkg", '{"color":"1 0 0"}'), groupKey("/a/scene.pkg", '{"color":"0 1 0"}'));
});

test('nobody renders for a group without members', () => {
    assert.equal(elect([], K), -1);
    assert.equal(elect([m(1, { key: "other" })], K), -1);
    assert.equal(elect([m(1)], ""), -1);
});

test('with nobody rendering yet, the largest screen renders, then the first by name', () => {
    assert.equal(elect([m(1), m(2, { area: 3440 * 1440 }), m(3)], K), 2);
    assert.equal(elect([m(3, { screen: "HDMI-A-1" }), m(2, { screen: "DP-2" }), m(1, { screen: "DP-10" })], K), 1,
                 "by name, as text");
    // the same on every screen: the order of the table does not matter
    assert.equal(elect([m(3), m(2, { area: 3440 * 1440 }), m(1)], K), 2);
});

test('the screen that renders stays: a larger one joining does not move the renderer', () => {
    const table = [m(1, { role: "source", frameKey: "f1" }), m(2, { area: 3840 * 2160 })];
    assert.equal(elect(table, K), 1);
});

test('the screen the user chose renders when it can, and takes the renderer over', () => {
    const table = [m(1, { role: "source", frameKey: "f1" }), m(2, { prefer: true })];
    assert.equal(elect(table, K), 2);
    // no longer chosen, it keeps rendering (it is the one that renders now)
    assert.equal(elect([m(1), m(2, { role: "source", frameKey: "f2" })], K), 2);
});

test('a source that left the group is replaced by one of the screens left', () => {
    // it stopped, went away or shows another wallpaper: its entry is gone or has another key
    assert.equal(elect([m(2), m(3, { area: 2560 * 1440 })], K), 3);
    assert.equal(elect([m(1, { role: "source", key: "other" }), m(2)], K), 2);
});

test('a stalled source hands over, and gets nothing back while it is stalled', () => {
    const table = [m(1, { role: "source", frameKey: "f1", stalled: true, prefer: true }), m(2), m(3)];
    assert.equal(elect(table, K), 2, "even the chosen screen is passed over while its window is not drawn");
    // drawn again: the chosen screen takes the renderer back
    table[0].stalled = false;
    assert.equal(elect(table, K), 1);
});

test('with every screen stalled the renderer stays where it is', () => {
    const table = [m(1, { stalled: true }), m(2, { role: "source", stalled: true }), m(3, { stalled: true, area: 1e9 })];
    assert.equal(elect(table, K), 2);
});

// What CloneLink does with these two: the screen elect() names begins to render only when
// mayTake() says so; a mirror asked whether its window draws marks itself stalled when not.
// `drawn` is each screen's count of checks in a row that saw its window draw.
function whoRenders(table, drawn) {
    const want = elect(table, K);
    const cur = rendering(table, K);
    if (cur && want === cur.id) return cur.id;
    return mayTake(table, K, want, drawn[want] || 0) ? want : (cur ? cur.id : -1);
}

test('the renderer is the member that says so', () => {
    assert.equal(rendering([m(1), m(2)], K), null);
    assert.equal(rendering([m(1), m(2, { role: "source" })], K).id, 2);
    assert.equal(rendering([m(1, { role: "source", key: "other" }), m(2)], K), null);
    assert.equal(rendering([m(1), m(2)], ""), null);
});

test('a screen named with nobody rendering, or chosen over a renderer that is fine, may begin', () => {
    assert.equal(mayTake([m(1), m(2)], K, 1, 0), true);
    assert.equal(mayTake([m(1, { role: "source" }), m(2, { prefer: true })], K, 2, 0), true);
    assert.equal(mayTake([m(1, { role: "source" })], K, 1, 0), true, "it renders already");
});

test('every screen asleep: the renderer stays where it is', () => {
    // 1 renders and has marked itself; 2 and 3 have not been asked yet
    const table = [m(1, { role: "source", stalled: true }), m(2, { area: 1e9 }), m(3)];
    assert.equal(elect(table, K), 2, "the election alone would name a screen that is not drawn");
    assert.equal(mayTake(table, K, 2, 0), false, "not before its window was seen drawing");
    assert.equal(mayTake(table, K, 2, 1), false, "and not at the first sign of it");
    assert.equal(whoRenders(table, {}), 1);
    // asked, 2 does not draw and marks itself; then 3
    table[1].stalled = true;
    assert.equal(whoRenders(table, {}), 1);
    table[2].stalled = true;
    assert.equal(elect(table, K), 1, "with all of them marked the election keeps the renderer");
    assert.equal(whoRenders(table, {}), 1);
    // the wake: the renderer's window draws first, the marks go one by one
    table[0].stalled = false;
    assert.equal(whoRenders(table, { 2: 1, 3: 1 }), 1);
    table[1].stalled = false; table[2].stalled = false;
    assert.equal(whoRenders(table, { 2: 5, 3: 5 }), 1, "awake again, nobody takes the renderer away");
});

test('one screen awake: it takes the rendering, not a larger one that is asleep', () => {
    const table = [m(1, { role: "source", stalled: true }), m(2, { area: 1e9, stalled: true }), m(3)];
    assert.equal(elect(table, K), 3);
    assert.equal(whoRenders(table, { 3: 1 }), 1, "seen drawing once: not yet");
    assert.equal(whoRenders(table, { 3: 2 }), 3);
});

test('the awake screen changes: the renderer follows the screen that is drawn', () => {
    // 3 renders now; 1 woke up and its mark is gone; 2 sleeps on; then 3 stops being drawn
    const table = [m(1), m(2, { area: 1e9, stalled: true }), m(3, { role: "source" })];
    assert.equal(whoRenders(table, { 1: 9 }), 3, "nothing moves while the renderer is drawn");
    table[2].stalled = true;
    assert.equal(whoRenders(table, { 1: 2 }), 1);
    // and when 1 sleeps too before it took over: nobody is drawn, 3 keeps the renderer
    table[0].stalled = true;
    assert.equal(whoRenders(table, {}), 3);
});

test('other groups do not count', () => {
    const table = [m(1, { key: "other", prefer: true, role: "source" }), m(2), m(3, { key: "other" })];
    assert.equal(elect(table, K), 2);
    assert.equal(elect(table, "other"), 1);
});

test('the renderer plays on while another screen of its group plays', () => {
    const table = [m(1, { role: "source", playing: false }), m(2, { playing: true }), m(3, { key: "other" })];
    assert.equal(othersPlaying(table, K, 1), true);
    table[1].playing = false;
    assert.equal(othersPlaying(table, K, 1), false, "every screen of the group is paused");
    table[0].playing = true;
    assert.equal(othersPlaying(table, K, 1), false, "its own playing is not another screen's");
    assert.equal(othersPlaying([m(1, { playing: false }), m(3, { key: "other", playing: true })], K, 1), false,
                 "another group's screen does not keep it playing");
    assert.equal(others(table, K, 1), 1);
    assert.equal(others(table, "", 1), 0);
});

test('memberById finds a screen, or nothing', () => {
    const table = [m(1), m(2, { frameKey: "f2" })];
    assert.equal(memberById(table, 2).frameKey, "f2");
    assert.equal(memberById(table, 9), null);
    assert.equal(memberById(null, 1), null);
});

const close = (a, b) => Math.abs(a - b) < 1e-9;

test('a mirror of the same shape shows the whole frame, whatever the mode', () => {
    for (const mode of ["crop", "fit", "stretch"]) {
        const p = mirrorPlacement(mode, 16 / 9, 1920, 1080);
        assert.deepEqual([p.x, p.y, p.width, p.height], [0, 0, 1920, 1080], mode);
        assert.ok(close(p.sx, 0) && close(p.sy, 0) && close(p.sw, 1) && close(p.sh, 1), mode);
    }
});

test('crop fills the screen with the middle of the frame', () => {
    // a 16:10 screen showing a 21.5:9 frame: the sides of the frame are cut
    let p = mirrorPlacement("crop", 3440 / 1440, 1920, 1200);
    assert.deepEqual([p.x, p.y, p.width, p.height], [0, 0, 1920, 1200]);
    assert.ok(close(p.sw, (1920 / 1200) / (3440 / 1440)) && close(p.sh, 1));
    assert.ok(close(p.sx, (1 - p.sw) / 2) && close(p.sy, 0));
    // an ultrawide screen showing a 16:10 frame: its top and bottom are cut
    p = mirrorPlacement("crop", 1920 / 1200, 3440, 1440);
    assert.ok(close(p.sw, 1) && close(p.sh, (1920 / 1200) / (3440 / 1440)));
    assert.ok(close(p.sx, 0) && close(p.sy, (1 - p.sh) / 2));
    // what is shown has the screen's shape
    assert.ok(close((p.sw * 1920) / (p.sh * 1200), 3440 / 1440));
});

test('fit shows all of the frame between bars', () => {
    let p = mirrorPlacement("fit", 3440 / 1440, 1920, 1200);
    assert.ok(close(p.width, 1920) && close(p.height, 1920 / (3440 / 1440)));
    assert.ok(close(p.x, 0) && close(p.y, (1200 - p.height) / 2));
    assert.deepEqual([p.sx, p.sy, p.sw, p.sh], [0, 0, 1, 1]);
    p = mirrorPlacement("fit", 1920 / 1200, 3440, 1440);
    assert.ok(close(p.height, 1440) && close(p.width, 1440 * 1.6) && close(p.x, (3440 - p.width) / 2));
});

test('stretch, and a frame not there yet, fill the screen with the whole frame', () => {
    const full = { x: 0, y: 0, width: 1920, height: 1200, sx: 0, sy: 0, sw: 1, sh: 1 };
    assert.deepEqual(mirrorPlacement("stretch", 3440 / 1440, 1920, 1200), full);
    assert.deepEqual(mirrorPlacement("crop", 0, 1920, 1200), full);
    assert.deepEqual(mirrorPlacement("fit", NaN, 1920, 1200), full);
    const empty = mirrorPlacement("crop", 2, 0, 0);
    assert.deepEqual([empty.sx, empty.sy, empty.sw, empty.sh], [0, 0, 1, 1]);
});
