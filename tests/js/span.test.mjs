// The maths of SpanGroup (we-manager span patch): the canvas of a group of
// screens, the part of the frame each screen shows, where the pointer of a
// screen lands in the canvas, and the size the canvas is rendered at.  The
// same file runs in the plugin (CloneLink.qml, SpanCanvas.qml,
// backend/SceneMirror.qml).
import { test } from 'node:test';
import assert from 'node:assert/strict';

import { groupKey, entry, placeholder, placeOf, union, sameRect, canvasOf, frameRect, sourcePlacement,
         mirrorPlacement, renderScale, renderSize, maxSide, shownPart, shownOf, pointerRect }
    from '../../plugin/contents/ui/js/span.mjs';
import { groupKey as cloneKey, elect, others, othersPlaying } from '../../plugin/contents/ui/js/clone.mjs';

const SCENE = cloneKey("/lib/431960/111/scene.pkg+scene", "");
const K = groupKey(1, SCENE);
const R = (x, y, width, height) => ({ x, y, width, height });
// One screen's entry, as CloneLink publishes it for a group.
const m = (id, rect, o = {}) => entry({ id, key: K, screen: "DP-" + id, prefer: false, area: rect.width * rect.height,
                                        role: "mirror", frameKey: "", playing: true, stalled: false, ...o },
                                      1, rect, o.canvas || null);
const close = (a, b, what) => assert.ok(Math.abs(a - b) < 1e-9, `${what || "value"}: ${a} is not ${b}`);
const closeRect = (a, b, what) => {
    for (const k of Object.keys(b)) close(a[k], b[k], (what || "rect") + "." + k);
};

// Where the point `q` of a screen (its own logical pixels) lands in the
// canvas, in Plasma's screen coordinates.  On a screen that shows the frame
// this is what the plugin does: the mirror item gets the point in its own
// coordinates (main.qml's MouseGrabber maps it), SceneMirror takes it to the
// fraction of the frame under it (MirrorPointToSource, SharedFrame.hpp), and
// the frame lies in the canvas at frameRect().
function pointOnMirror(canvas, screen, aspect, q) {
    const p = mirrorPlacement(canvas, screen, aspect, screen.width, screen.height);
    const f = frameRect(canvas, aspect);
    const u = p.sx + (q.x - p.x) / p.width * p.sw, v = p.sy + (q.y - p.y) / p.height * p.sh;
    return { x: f.x + u * f.width, y: f.y + v * f.height };
}
// The same through the rendering screen's own layout: the place of the point
// in the SpanCanvas item, which the SceneViewer fills (Cover and Stretch).
// (The pointer itself goes through SpanPointer.qml on every screen: below.)
function pointOnSource(canvas, screen, q) {
    const p = sourcePlacement(canvas, screen, screen.width, screen.height);
    return { x: canvas.x + (q.x - p.x) / p.width * canvas.width, y: canvas.y + (q.y - p.y) / p.height * canvas.height };
}

// ---- what the scene is told, against where the scene is drawn ------------------------------
// The renderer's camera, written out on its own (VulkanRender.cpp UpdateCameraFillMode): where
// the point `g` of the desk (Plasma's screen coordinates) lies in a scene of `sw` x `sh`, as a
// fraction of the scene each way.  `mode` is the rendering screen's alignment.
function sceneUnder(canvas, mode, sw, sh, g) {
    if (mode === "fit") {
        // Scene.qml makes the item the scene's shape, as large as fits, centred (layout.mjs
        // letterboxSize with anchors.centerIn); the renderer stretches the scene into it.
        const scale = Math.min(canvas.width / sw, canvas.height / sh);
        const w = sw * scale, h = sh * scale;
        const left = canvas.x + (canvas.width - w) / 2, top = canvas.y + (canvas.height - h) / 2;
        return { x: (g.x - left) / w, y: (g.y - top) / h };
    }
    if (mode === "stretch")
        return { x: (g.x - canvas.x) / canvas.width, y: (g.y - canvas.y) / canvas.height };
    // Cover: one scale for both ways, the larger; the scene's middle on the canvas's middle.
    const scale = Math.max(canvas.width / sw, canvas.height / sh);
    return { x: (sw / 2 + (g.x - canvas.x - canvas.width / 2) / scale) / sw,
             y: (sh / 2 + (g.y - canvas.y - canvas.height / 2) / scale) / sh };
}
// What the renderer makes of the pointer at the point `q` of `screen`: the plugin's pointer
// item (SpanPointer.qml, the size of the screen) maps it through pointerRect to a fraction of
// the rendering item (MirrorPointToSource), and the renderer takes that fraction of its item
// for the same fraction of the scene (SceneObject: position / item size).
function sceneTold(canvas, mode, sw, sh, screen, q) {
    const frame = mode === "fit" ? frameRect(canvas, sw / sh) : canvas;
    const k = shownPart(mode === "crop", sw / sh, frame.width, frame.height);
    const r = pointerRect(canvas, screen, frame.width / frame.height, k.kx, k.ky);
    return { x: r.x + q.x / screen.width * r.width, y: r.y + q.y / screen.height * r.height };
}

// A desk like the user's: a 2560x1440 screen between two 1920x1200 ones.
const LEFT = R(0, 0, 1920, 1200), MID = R(1920, 0, 2560, 1440), RIGHT = R(4480, 0, 1920, 1200);

test('the group is the SpanGroup number with the scene; without a number it is the clone group', () => {
    assert.equal(groupKey(0, SCENE), SCENE, "SpanGroup 0: nothing of the span");
    assert.equal(groupKey(1, ""), "", "no scene, no group");
    assert.notEqual(groupKey(1, SCENE), SCENE, "a stretched screen does not share with one that only clones");
    assert.notEqual(groupKey(1, SCENE), groupKey(2, SCENE), "two groups with the same scene are two canvases");
    assert.equal(groupKey(2, SCENE), groupKey(2, SCENE));
});

test('three screens side by side: the canvas is the rectangle around them', () => {
    const table = [m(1, LEFT), m(2, MID), m(3, RIGHT)];
    assert.deepEqual(union(table, K), R(0, 0, 6400, 1440));
    // tops aligned: the 240 rows under the two smaller screens are canvas nobody sees
    const seen = LEFT.width * LEFT.height + MID.width * MID.height + RIGHT.width * RIGHT.height;
    assert.equal(6400 * 1440 - seen, 2 * 1920 * 240);
    // the order of the table does not matter, and other groups' screens are not in it
    assert.deepEqual(union([m(3, RIGHT), m(1, LEFT), m(2, MID), m(4, R(9000, 0, 1920, 1080), { key: "other" })], K),
                     R(0, 0, 6400, 1440));
    assert.equal(union(table, "other"), null);
    assert.equal(union(table, ""), null);
    assert.equal(union([], K), null);
});

test('each screen shows its own part of the frame, and the parts join without a seam', () => {
    const c = R(0, 0, 6400, 1440);
    const l = mirrorPlacement(c, LEFT, 6400 / 1440, 1920, 1200);
    closeRect(l, { x: 0, y: 0, width: 1920, height: 1200, sx: 0, sy: 0, sw: 0.3, sh: 1200 / 1440 }, "left");
    const mid = mirrorPlacement(c, MID, 6400 / 1440, 2560, 1440);
    closeRect(mid, { x: 0, y: 0, width: 2560, height: 1440, sx: 0.3, sy: 0, sw: 0.4, sh: 1 }, "middle");
    const r = mirrorPlacement(c, RIGHT, 6400 / 1440, 1920, 1200);
    closeRect(r, { x: 0, y: 0, width: 1920, height: 1200, sx: 0.7, sy: 0, sw: 0.3, sh: 1200 / 1440 }, "right");
    close(l.sx + l.sw, mid.sx, "left ends where the middle begins");
    close(mid.sx + mid.sw, r.sx, "the middle ends where the right begins");
    // one frame pixel is the same size on every screen: no screen stretches its part
    for (const [p, s] of [[l, LEFT], [mid, MID], [r, RIGHT]]) {
        close(p.sw * c.width / p.width, 1, "horizontal scale");
        close(p.sh * c.height / p.height, 1, "vertical scale");
        assert.equal(p.width, s.width);
    }
    // before the first frame the frame's shape is not known: the canvas stands in
    closeRect(mirrorPlacement(c, MID, 0, 2560, 1440), mid, "no frame yet");
});

test('the screen that renders holds the whole canvas, shifted so its window shows its part', () => {
    const c = R(0, 0, 6400, 1440);
    assert.deepEqual(sourcePlacement(c, MID, 2560, 1440), R(-1920, 0, 6400, 1440));
    assert.deepEqual(sourcePlacement(c, LEFT, 1920, 1200), R(0, 0, 6400, 1440));
    assert.deepEqual(sourcePlacement(c, RIGHT, 1920, 1200), R(-4480, 0, 6400, 1440));
    // a canvas that does not start at 0,0 (the group is the two right screens of four)
    assert.deepEqual(sourcePlacement(R(3840, 200, 3840, 1080), R(5760, 200, 1920, 1080), 1920, 1080),
                     R(-1920, 0, 3840, 1080));
    // nothing to lay out: the item fills its screen, as without a canvas
    assert.deepEqual(sourcePlacement(R(0, 0, 0, 0), MID, 2560, 1440), R(0, 0, 2560, 1440));
});

test('screens shifted against each other, and a gap between them', () => {
    // the smaller screens centred on the large one's height
    const l = R(0, 120, 1920, 1200), r = R(4480, 120, 1920, 1200);
    const c = union([m(1, l), m(2, MID), m(3, r)], K);
    assert.deepEqual(c, R(0, 0, 6400, 1440));
    closeRect(mirrorPlacement(c, l, c.width / c.height, 1920, 1200),
              { x: 0, y: 0, width: 1920, height: 1200, sx: 0, sy: 120 / 1440, sw: 0.3, sh: 1200 / 1440 });
    // a gap of 200 between two screens is part of the canvas: the picture goes on behind it
    const a = R(0, 0, 1920, 1080), b = R(2120, 0, 1920, 1080);
    const g = union([m(1, a), m(2, b)], K);
    assert.deepEqual(g, R(0, 0, 4040, 1080));
    const pa = mirrorPlacement(g, a, g.width / g.height, 1920, 1080);
    const pb = mirrorPlacement(g, b, g.width / g.height, 1920, 1080);
    close(pb.sx - (pa.sx + pa.sw), 200 / 4040, "the gap's share of the frame is shown by nobody");
    close(pa.sw, 1920 / 4040); close(pb.sw, 1920 / 4040);
});

test('a vertical stack and a portrait screen', () => {
    const top = R(0, 0, 1920, 1080), bottom = R(0, 1080, 1920, 1080);
    const c = union([m(1, top), m(2, bottom)], K);
    assert.deepEqual(c, R(0, 0, 1920, 2160));
    closeRect(mirrorPlacement(c, bottom, 1920 / 2160, 1920, 1080),
              { x: 0, y: 0, width: 1920, height: 1080, sx: 0, sy: 0.5, sw: 1, sh: 0.5 });
    assert.deepEqual(sourcePlacement(c, bottom, 1920, 1080), R(0, -1080, 1920, 2160));
    // a 1920x1200 screen turned upright is 1200x1920 to Plasma, left of a 2560x1440 one
    const portrait = R(0, 0, 1200, 1920), wide = R(1200, 240, 2560, 1440);
    const p = union([m(1, portrait), m(2, wide)], K);
    assert.deepEqual(p, R(0, 0, 3760, 1920));
    closeRect(mirrorPlacement(p, portrait, 3760 / 1920, 1200, 1920),
              { x: 0, y: 0, width: 1200, height: 1920, sx: 0, sy: 0, sw: 1200 / 3760, sh: 1 });
    closeRect(mirrorPlacement(p, wide, 3760 / 1920, 2560, 1440),
              { x: 0, y: 0, width: 2560, height: 1440, sx: 1200 / 3760, sy: 240 / 1920, sw: 2560 / 3760, sh: 0.75 });
});

test('screens of different scale factors lie where Plasma puts them, in logical pixels', () => {
    // a 3840x2160 screen at 200 % is 1920x1080 to Plasma, beside a 1920x1080 screen at 100 %
    const dense = R(0, 0, 1920, 1080), plain = R(1920, 0, 1920, 1080);
    const c = union([m(1, dense), m(2, plain)], K);
    assert.deepEqual(c, R(0, 0, 3840, 1080));
    const pd = mirrorPlacement(c, dense, 3840 / 1080, 1920, 1080), pp = mirrorPlacement(c, plain, 3840 / 1080, 1920, 1080);
    close(pd.sw, 0.5); close(pp.sw, 0.5); close(pp.sx, 0.5);
    // the canvas is rendered at the rendering screen's scale factor
    assert.deepEqual(renderSize(c.width, c.height, 2, 1), { width: 7680, height: 2160 });   // the dense one renders
    assert.deepEqual(renderSize(c.width, c.height, 1, 1), { width: 3840, height: 1080 });   // the plain one renders
    assert.deepEqual(renderSize(c.width, c.height, 1.5, 0.5), { width: 2880, height: 810 });
});

test('with Keep aspect ratio the frame is the scene\'s shape centred in the canvas', () => {
    const c = R(0, 0, 6400, 1440);
    // a 16:9 scene: its picture is 2560 wide in the middle of the canvas
    closeRect(frameRect(c, 16 / 9), R(1920, 0, 2560, 1440));
    closeRect(mirrorPlacement(c, MID, 16 / 9, 2560, 1440),
              { x: 0, y: 0, width: 2560, height: 1440, sx: 0, sy: 0, sw: 1, sh: 1 }, "the middle screen has all of it");
    // the side screens are in the bars: they draw nothing (the background colour shows)
    assert.deepEqual(mirrorPlacement(c, LEFT, 16 / 9, 1920, 1200), { x: 0, y: 0, width: 0, height: 0, sx: 0, sy: 0, sw: 1, sh: 1 });
    // a 32:9 scene reaches 1280 into each side screen: those draw only that strip
    closeRect(frameRect(c, 32 / 9), R(640, 0, 5120, 1440));
    closeRect(mirrorPlacement(c, LEFT, 32 / 9, 1920, 1200),
              { x: 640, y: 0, width: 1280, height: 1200, sx: 0, sy: 0, sw: 0.25, sh: 1200 / 1440 }, "left strip");
    closeRect(mirrorPlacement(c, RIGHT, 32 / 9, 1920, 1200),
              { x: 0, y: 0, width: 1280, height: 1200, sx: 0.75, sy: 0, sw: 0.25, sh: 1200 / 1440 }, "right strip");
    // a tall scene: bars left and right of a frame as high as the canvas
    closeRect(frameRect(R(0, 0, 3840, 1080), 1), R(1380, 0, 1080, 1080));
});

test('the pointer of every screen lands where it is in the canvas', () => {
    const c = R(0, 0, 6400, 1440), aspect = 6400 / 1440;
    for (const [s, q] of [[LEFT, { x: 100, y: 50 }], [MID, { x: 1280, y: 720 }], [RIGHT, { x: 1919, y: 1199 }],
                          [R(0, 120, 1920, 1200), { x: 0, y: 0 }]]) {
        const want = { x: s.x + q.x, y: s.y + q.y };
        closeRect(pointOnMirror(c, s, aspect, q), want, "a screen that shows the frame");
        closeRect(pointOnSource(c, s, q), want, "the screen that renders");
    }
    // so the same spot of the picture is under the pointer whichever screen renders, and a
    // pointer that crosses from one screen to the next moves on without a jump
    const a = pointOnMirror(c, LEFT, aspect, { x: 1920, y: 600 }), b = pointOnMirror(c, MID, aspect, { x: 0, y: 600 });
    closeRect(a, b, "the shared edge");
    // Keep aspect ratio: a pointer in the bars maps outside the frame, not onto its edge
    const p = mirrorPlacement(c, LEFT, 32 / 9, 1920, 1200), f = frameRect(c, 32 / 9);
    const u = p.sx + (100 - p.x) / p.width * p.sw;
    close(f.x + u * f.width, 100, "left of the frame");
    assert.ok(u < 0);
    // a screen the frame does not reach has no item to send the pointer through
    assert.equal(mirrorPlacement(c, LEFT, 16 / 9, 1920, 1200).width, 0);
});

test('a stopped screen keeps its place in the canvas and takes no part in the rules', () => {
    const table = [m(1, LEFT, { role: "source", frameKey: "f1" }), m(2, MID), placeholder(K, "DP-3", RIGHT)];
    table[2].id = 3;
    assert.deepEqual(union(table, K), R(0, 0, 6400, 1440), "the canvas does not shrink when a screen stops");
    assert.equal(others(table, K, 1), 1, "it is not counted as a screen of the group");
    assert.equal(othersPlaying(table, K, 1), true);
    table[1].playing = false;
    assert.equal(othersPlaying(table, K, 1), false, "it does not keep the renderer playing");
    assert.equal(elect([placeholder(K, "DP-3", RIGHT)], K), -1, "it never renders");
    assert.deepEqual(placeOf(table[2]), RIGHT);
    // a screen that went away (unplugged) has no entry: the canvas is what is left
    assert.deepEqual(union(table.slice(0, 2), K), R(0, 0, 4480, 1440));
    // one screen left: the canvas is that screen, which then shows the whole picture
    const alone = union([m(2, MID)], K);
    assert.deepEqual(alone, MID);
    assert.deepEqual(sourcePlacement(alone, MID, 2560, 1440), R(0, 0, 2560, 1440));
    assert.deepEqual(placeholder("", "DP-3", RIGHT), {});
    assert.deepEqual(placeholder(K, "DP-3", R(0, 0, 0, 0)), {});
});

test('a frame names the canvas it was rendered for', () => {
    const c = R(0, 0, 6400, 1440);
    const table = [m(1, LEFT, { role: "source", frameKey: "f1", canvas: c }), m(2, MID), m(3, RIGHT)];
    assert.deepEqual(canvasOf(table, "f1"), c);
    assert.equal(canvasOf(table, "f2"), null, "another frame");
    assert.equal(canvasOf(table, ""), null);
    assert.equal(canvasOf([m(1, LEFT, { role: "source", frameKey: "f1" })], "f1"), null, "a source without a canvas");
    // a screen that joined after the canvas was made lies outside it: nothing to show until
    // the canvas is rendered again
    const late = R(6400, 0, 1920, 1080);
    assert.equal(mirrorPlacement(c, late, c.width / c.height, 1920, 1080).width, 0);
    assert.equal(sameRect(union([...table, m(4, late)], K), c), false, "which the rendering screen sees");
    assert.equal(sameRect(union(table, K), c), true);
    assert.equal(sameRect(null, c), false);
});

test('the canvas is rendered at the screen\'s own scale until a side passes the limit', () => {
    assert.equal(maxSide, 16384);
    // the desk above: 6400x1440 pixels, well inside
    assert.equal(renderScale(1, 6400, 1440, 1), 1);
    assert.deepEqual(renderSize(6400, 1440, 1, 1), { width: 6400, height: 1440 });
    // the user's own RenderScale goes through untouched
    assert.equal(renderScale(0.5, 6400, 1440, 1), 0.5);
    assert.deepEqual(renderSize(6400, 1440, 1, 0.5), { width: 3200, height: 720 });
    // three 3840x2160 screens: 11520 wide, inside
    assert.equal(renderScale(1, 11520, 2160, 1), 1);
    // three 7680x4320 screens: 23040 wide, scaled to 16384
    const s = renderScale(1, 23040, 4320, 1);
    close(s, 16384 / 23040);
    assert.deepEqual(renderSize(23040, 4320, 1, s), { width: 16384, height: 3072 });
    // the scale factor counts: 3 x 2560x1440 logical at 250 % is 19200 pixels wide
    close(renderScale(1, 7680, 1440, 2.5), 16384 / 19200);
    // a user scale that already brings it under the limit stays
    assert.equal(renderScale(0.5, 23040, 4320, 1), 0.5);
    // a tall canvas: the larger side is the height
    close(renderScale(1, 2160, 20000, 1), 16384 / 20000);
    assert.equal(renderScale(1, 0, 0, 1), 1);
});

test('negative coordinates: a screen left of and above the primary one', () => {
    const a = R(-1920, -200, 1920, 1080), b = R(0, 0, 2560, 1440);
    const c = union([m(1, a), m(2, b)], K);
    assert.deepEqual(c, R(-1920, -200, 4480, 1640));
    closeRect(mirrorPlacement(c, a, c.width / c.height, 1920, 1080),
              { x: 0, y: 0, width: 1920, height: 1080, sx: 0, sy: 0, sw: 1920 / 4480, sh: 1080 / 1640 });
    closeRect(mirrorPlacement(c, b, c.width / c.height, 2560, 1440),
              { x: 0, y: 0, width: 2560, height: 1440, sx: 1920 / 4480, sy: 200 / 1640, sw: 2560 / 4480, sh: 1440 / 1640 });
    assert.deepEqual(sourcePlacement(c, b, 2560, 1440), R(-1920, -200, 4480, 1640));
});

test('how much of the scene a frame shows', () => {
    // Cover on the desk's canvas: a 16:9 scene's whole width, 40 % of its height
    closeRect(shownPart(true, 16 / 9, 6400, 1440), { kx: 1, ky: 0.4 });
    // Cover on two screens stacked: its whole height, half its width
    closeRect(shownPart(true, 16 / 9, 1920, 2160), { kx: 0.5, ky: 1 });
    // the same shape: all of it
    closeRect(shownPart(true, 16 / 9, 2560, 1440), { kx: 1, ky: 1 });
    // Stretch and Keep aspect ratio never cut; nor is anything assumed before the scene is known
    assert.deepEqual(shownPart(false, 16 / 9, 6400, 1440), { kx: 1, ky: 1 });
    assert.deepEqual(shownPart(true, 0, 6400, 1440), { kx: 1, ky: 1 });
    // a screen learns it from the rendering screen's entry
    const src = m(1, MID, { role: "source", frameKey: "f1", canvas: R(0, 0, 6400, 1440) });
    assert.deepEqual(shownOf([src], "f1"), { kx: 1, ky: 1 });
    const told = entry({ id: 1, key: K, role: "source", frameKey: "f1" }, 1, MID, R(0, 0, 6400, 1440), { kx: 1, ky: 0.4 });
    assert.deepEqual(shownOf([told, m(2, LEFT)], "f1"), { kx: 1, ky: 0.4 });
    assert.deepEqual(shownOf([told], "f2"), { kx: 1, ky: 1 });
    assert.equal(m(2, LEFT).kx, undefined, "a screen that shows the frame says nothing of it");
});

// Every alignment on two desks: where the pointer is in the scene as drawn, and what the
// renderer is told, must be the same point.
const DESKS = {
    "a 2560x1440 screen between two 1920x1200 ones": [LEFT, MID, RIGHT],
    "two 1920x1080 screens stacked": [R(0, 0, 1920, 1080), R(0, 1080, 1920, 1080)],
};
for (const [name, screens] of Object.entries(DESKS)) {
    for (const mode of ["crop", "fit", "stretch"]) {
        test(`the scene's point under the pointer is the one the renderer is told: ${name}, ${mode}`, () => {
            const c = union(screens.map((s, i) => m(i + 1, s)), K);
            for (const [sw, sh] of [[1920, 1080], [3840, 1080], [1080, 1920]]) {
                for (const s of screens) {
                    for (const q of [{ x: 0, y: 0 }, { x: s.width, y: s.height }, { x: s.width / 2, y: s.height / 2 },
                                     { x: 137, y: s.height - 61 }]) {
                        const g = { x: s.x + q.x, y: s.y + q.y };
                        closeRect(sceneTold(c, mode, sw, sh, s, q), sceneUnder(c, mode, sw, sh, g),
                                  `${sw}x${sh} scene, screen at ${s.x},${s.y}, point ${q.x},${q.y}`);
                    }
                }
            }
        });
    }
}

test('the numbers of the desk with Cover, and what went wrong without the correction', () => {
    const c = R(0, 0, 6400, 1440);
    // the pointer in the middle of the desk is the middle of the scene
    closeRect(sceneTold(c, "crop", 1920, 1080, MID, { x: 1280, y: 720 }), { x: 0.5, y: 0.5 });
    // at the canvas's top edge the scene's row under it is 30 % down the scene, not its top
    closeRect(sceneTold(c, "crop", 1920, 1080, MID, { x: 1280, y: 0 }), { x: 0.5, y: 0.3 });
    closeRect(sceneTold(c, "crop", 1920, 1080, LEFT, { x: 0, y: 1200 }), { x: 0, y: 0.3 + 0.4 * 1200 / 1440 });
    // the rectangle the left screen's pointer item is given: its 30 % of the width, and
    // 1200 of the 1440 rows drawn into the middle 40 % of the scene's height
    closeRect(pointerRect(c, LEFT, 6400 / 1440, 1, 0.4), { x: 0, y: 0.3, width: 0.3, height: 0.4 * 1200 / 1440 });
    // with nothing cut (Stretch) it is the screen's place in the frame
    closeRect(pointerRect(c, RIGHT, 6400 / 1440, 1, 1), { x: 0.7, y: 0, width: 0.3, height: 1200 / 1440 });
    // uncorrected, the renderer would take the canvas fraction for the scene's: 0 instead of 0.3
    const plain = pointerRect(c, MID, 6400 / 1440, 1, 1);
    close(plain.y + 0 * plain.height, 0);
    // Keep aspect ratio: a pointer on a side screen, in the bar, is left of the scene (not clamped)
    assert.ok(sceneTold(c, "fit", 1920, 1080, LEFT, { x: 100, y: 100 }).x < 0);
    assert.deepEqual(pointerRect(R(0, 0, 0, 0), LEFT, 1, 1, 1), { x: 0, y: 0, width: 1, height: 1 });
});

test('Keep aspect ratio with a scene wider than the canvas: bars above and below', () => {
    // two 1920x1080 screens stacked, a 32:9 scene: its picture is 1920x540 across the join
    const top = R(0, 0, 1920, 1080), bottom = R(0, 1080, 1920, 1080), c = R(0, 0, 1920, 2160);
    closeRect(frameRect(c, 32 / 9), R(0, 810, 1920, 540));
    // each screen draws the half of it that reaches it, at the edge towards the other screen
    closeRect(mirrorPlacement(c, top, 32 / 9, 1920, 1080),
              { x: 0, y: 810, width: 1920, height: 270, sx: 0, sy: 0, sw: 1, sh: 0.5 });
    closeRect(mirrorPlacement(c, bottom, 32 / 9, 1920, 1080),
              { x: 0, y: 0, width: 1920, height: 270, sx: 0, sy: 0.5, sw: 1, sh: 0.5 });
    // the desk of three with a scene wider still (48:9): 6400x1200, 120 down
    closeRect(frameRect(R(0, 0, 6400, 1440), 48 / 9), R(0, 120, 6400, 1200));
    closeRect(mirrorPlacement(R(0, 0, 6400, 1440), LEFT, 48 / 9, 1920, 1200),
              { x: 0, y: 120, width: 1920, height: 1080, sx: 0, sy: 0, sw: 0.3, sh: 0.9 });
    // the pointer at the top of the upper screen is above the scene, at its join in the middle
    const r = pointerRect(c, top, 32 / 9, 1, 1);
    closeRect(r, { x: 0, y: -1.5, width: 1, height: 2 });
    close(r.y + 1080 / 1080 * r.height, 0.5, "the join is the middle of the scene");
});
