// Pure, cross-runtime (Node + QJSEngine) maths for SpanGroup (we-manager span
// patch): one scene stretched across the screens of a group.  It sits on
// ShareRenderer's parts (clone patch): one screen of the group renders, the
// others show its frame (js/clone.mjs decides who).  What differs is in this
// file: the renderer draws a canvas that covers every screen of the group,
// and each screen shows its own part of it.
//
// Rectangles are { x, y, width, height } in Plasma's screen coordinates:
// logical pixels, the space the screens are arranged in and the pointer
// moves in.  A screen's scale factor does not enter the layout: a 3840x2160
// screen at 200 % is 1920x1080 here, and lines up with a 1920x1080 screen at
// 100 % as it does on the desk.  A rotated screen is the rectangle Plasma
// gives it (1200x1920 for a portrait 1920x1200).
//
// A screen's entry in the table (src/CloneGroup.hpp; clone.mjs lists the
// rest) gets, with SpanGroup on:
//   span        the group's number
//   x, y, w, h  the screen's rectangle
//   cx,cy,cw,ch the canvas its renderer was made for (a source only)
//   kx, ky      how much of the scene its frame shows, each way (a source
//               only; shownPart below)
// and a screen that is stopped keeps an entry that is nothing but its place:
//   { key: "", place: <the group's key>, screen, x, y, w, h }
// The clone rules go by `key`, so they do not see it; union() does.

// The largest side of the canvas in pixels.  Mesa's Vulkan and OpenGL drivers
// for AMD and Intel take 2D images up to 16384 a side; a larger canvas is
// rendered smaller and scaled up (renderScale below).
export const maxSide = 16384;

// The group of a spanned scene: its number and what it shows (clone.mjs
// groupKey).  Screens that only share a renderer (SpanGroup 0) and screens
// of another group are never in it.
export function groupKey(span, key) {
    const n = Number(span) || 0;
    if (!key || !(n > 0)) return key || "";
    return "span" + n + "\n" + key;
}

function ok(r) {
    return !!r && r.width > 0 && r.height > 0 && isFinite(r.x) && isFinite(r.y)
           && isFinite(r.width) && isFinite(r.height);
}

// What the table carries for a screen of a group, added to its clone entry.
// `canvas` and `shown` (shownPart) for the screen that renders (and has its
// renderer), else null.
export function entry(base, span, screen, canvas, shown) {
    const out = {};
    for (const k in base) out[k] = base[k];
    out.span = Number(span) || 0;
    if (ok(screen)) {
        out.x = screen.x; out.y = screen.y; out.w = screen.width; out.h = screen.height;
    }
    if (ok(canvas)) {
        out.cx = canvas.x; out.cy = canvas.y; out.cw = canvas.width; out.ch = canvas.height;
        out.kx = shown && shown.kx > 0 ? shown.kx : 1;
        out.ky = shown && shown.ky > 0 ? shown.ky : 1;
    }
    return out;
}

// A stopped screen's entry: its place in the canvas of `key`, and no part in
// who renders, who plays or who is counted (its key is empty).
export function placeholder(key, screenName, screen) {
    if (!key || !ok(screen)) return {};
    return { key: "", place: key, screen: screenName || "", x: screen.x, y: screen.y, w: screen.width,
             h: screen.height };
}

// The screen rectangle of an entry, or null.
export function placeOf(m) {
    if (!m) return null;
    const r = { x: Number(m.x), y: Number(m.y), width: Number(m.w), height: Number(m.h) };
    return ok(r) ? r : null;
}

// The canvas of the group `key`: the smallest rectangle that holds every
// screen of it, the stopped ones too.  null with no screen.
//  * Screens side by side or stacked: the rectangle around them.
//  * Screens of different heights, or shifted against each other: the
//    corners no screen covers are part of the canvas and are rendered, but
//    nobody sees them.
//  * A gap between two screens is part of the canvas too: the picture goes
//    on behind it, as behind a bezel.
export function union(members, key) {
    if (!key) return null;
    let l = Infinity, t = Infinity, r = -Infinity, b = -Infinity;
    for (const m of (members || [])) {
        if (!m || (m.key !== key && m.place !== key)) continue;
        const p = placeOf(m);
        if (p === null) continue;
        l = Math.min(l, p.x); t = Math.min(t, p.y);
        r = Math.max(r, p.x + p.width); b = Math.max(b, p.y + p.height);
    }
    if (!(r > l) || !(b > t)) return null;
    return { x: l, y: t, width: r - l, height: b - t };
}

export function sameRect(a, b) {
    if (!a || !b) return false;
    return Math.abs(a.x - b.x) < 0.5 && Math.abs(a.y - b.y) < 0.5
           && Math.abs(a.width - b.width) < 0.5 && Math.abs(a.height - b.height) < 0.5;
}

// The canvas the frame `frameKey` was rendered for: its source's entry says.
// null when no source publishes that frame, or it renders no canvas.
export function canvasOf(members, frameKey) {
    if (!frameKey) return null;
    for (const m of (members || [])) {
        if (!m || m.role !== "source" || m.frameKey !== frameKey) continue;
        const c = { x: Number(m.cx), y: Number(m.cy), width: Number(m.cw), height: Number(m.ch) };
        return ok(c) ? c : null;
    }
    return null;
}

// Where the frame lies in the canvas.  The frame is the rendering item's
// picture, and backend/Scene.qml lays that item out in the canvas as it does
// on one screen: it fills the canvas (Cover, Stretch), or, with Keep aspect
// ratio, it is the largest rectangle of the scene's shape centred in it.
// Both are "the largest centred rectangle of the frame's shape", so the
// frame's shape (`frameAspect`, width over height; 0 before a frame) is all
// a screen needs to know.
export function frameRect(canvas, frameAspect) {
    if (!ok(canvas)) return null;
    if (!(frameAspect > 0)) return { x: canvas.x, y: canvas.y, width: canvas.width, height: canvas.height };
    const own = canvas.width / canvas.height;
    if (frameAspect > own) {
        const h = canvas.width / frameAspect;
        return { x: canvas.x, y: canvas.y + (canvas.height - h) / 2, width: canvas.width, height: h };
    }
    const w = canvas.height * frameAspect;
    return { x: canvas.x + (canvas.width - w) / 2, y: canvas.y, width: w, height: canvas.height };
}

// Where the screen that renders puts the canvas: the place and size, in its
// own item (`parentW` x `parentH`, which covers `screen`), of the item that
// holds the renderer.  The canvas is larger than the screen; the window
// shows the part of it that is this screen.
export function sourcePlacement(canvas, screen, parentW, parentH) {
    if (!ok(canvas) || !ok(screen) || !(parentW > 0) || !(parentH > 0))
        return { x: 0, y: 0, width: Math.max(0, parentW || 0), height: Math.max(0, parentH || 0) };
    const kx = parentW / screen.width, ky = parentH / screen.height;
    return { x: (canvas.x - screen.x) * kx, y: (canvas.y - screen.y) * ky,
             width: canvas.width * kx, height: canvas.height * ky };
}

// What a screen that shows the frame draws: the place of its mirror item in
// its own item (x, y, width, height) and the part of the frame it shows, in
// fractions of the frame (sx, sy, sw, sh).  That is the part of the frame
// that lies on this screen; where the frame does not reach (the bars of Keep
// aspect ratio, a screen outside the canvas) nothing is drawn and the
// background colour shows: a screen wholly outside gets an empty item.
export function mirrorPlacement(canvas, screen, frameAspect, parentW, parentH) {
    const none = { x: 0, y: 0, width: 0, height: 0, sx: 0, sy: 0, sw: 1, sh: 1 };
    const f = frameRect(canvas, frameAspect);
    if (f === null || !ok(screen) || !(parentW > 0) || !(parentH > 0)) return none;
    const l = Math.max(f.x, screen.x), t = Math.max(f.y, screen.y);
    const r = Math.min(f.x + f.width, screen.x + screen.width);
    const b = Math.min(f.y + f.height, screen.y + screen.height);
    if (!(r > l) || !(b > t)) return none;
    const kx = parentW / screen.width, ky = parentH / screen.height;
    return { x: (l - screen.x) * kx, y: (t - screen.y) * ky, width: (r - l) * kx, height: (b - t) * ky,
             sx: (l - f.x) / f.width, sy: (t - f.y) / f.height, sw: (r - l) / f.width, sh: (b - t) / f.height };
}

// How much of the scene the frame shows, each way: { kx, ky }, 1 for all of
// it.  The renderer fits the scene into its item by the fill mode:
//  * Stretch, and Keep aspect ratio (whose item is the scene's shape): the
//    whole scene, { 1, 1 };
//  * Cover (`crops`): the scene fills the item and what does not fit is cut
//    away evenly on both sides.  In an item wider than the scene (width over
//    height `sceneAspect`) the frame shows the scene's whole width and
//    sceneAspect / itemAspect of its height, about its middle; in a narrower
//    one, its whole height and itemAspect / sceneAspect of its width.
// Before the scene's shape is known (0) nothing is assumed: { 1, 1 }.
export function shownPart(crops, sceneAspect, itemW, itemH) {
    if (!crops || !(sceneAspect > 0) || !(itemW > 0) || !(itemH > 0)) return { kx: 1, ky: 1 };
    const item = itemW / itemH;
    return item > sceneAspect ? { kx: 1, ky: sceneAspect / item } : { kx: item / sceneAspect, ky: 1 };
}

// What the frame `frameKey` shows of its scene: its source's entry says.
export function shownOf(members, frameKey) {
    for (const m of (members || [])) {
        if (!m || !frameKey || m.role !== "source" || m.frameKey !== frameKey) continue;
        return { kx: m.kx > 0 ? Number(m.kx) : 1, ky: m.ky > 0 ? Number(m.ky) : 1 };
    }
    return { kx: 1, ky: 1 };
}

// The pointer under span.  The renderer takes a pointer event's position as
// a fraction of its item and reads that as the same fraction of the scene
// (SceneObject's handlers: scene point = position / item size x scene size),
// which is the scene's point under the pointer only while the item shows
// the whole scene.  A canvas with Cover shows a band of it (40 % of a 16:9
// scene's height on a 6400x1440 canvas), so the plugin hands the renderer
// another position: the one whose fraction of the item is the fraction of
// the SCENE under the pointer.  For a point at the fraction f of the frame
// (each way) that is
//     f' = 0.5 + (f - 0.5) * k        k = shownPart's kx or ky
// and the event's position is f' x the item's size.
//
// This gives the source rectangle (fractions of the frame: x, y, width,
// height) of the item that passes a screen's pointer on (SpanPointer.qml, a
// SceneMirror the size of the screen, which maps a point of itself through
// its source rectangle, SharedFrame.hpp MirrorPointToSource).  A point at
// the fraction t of the screen `screen` lies at the fraction
//     f = (screen.x - frame.x) / frame.width + t * screen.width / frame.width
// of the frame (frameRect), so
//     x     = 0.5 + ((screen.x - frame.x) / frame.width - 0.5) * kx
//     width = kx * screen.width / frame.width
// and the same for y and height with ky.  With kx = ky = 1 it is the
// screen's place in the frame.  Nothing is clamped: a pointer beside the
// frame (the bars of Keep aspect ratio) maps outside the item.
export function pointerRect(canvas, screen, frameAspect, kx, ky) {
    const whole = { x: 0, y: 0, width: 1, height: 1 };
    const f = frameRect(canvas, frameAspect);
    if (f === null || !ok(screen)) return whole;
    const a = kx > 0 ? kx : 1, b = ky > 0 ? ky : 1;
    return { x: 0.5 + ((screen.x - f.x) / f.width - 0.5) * a, y: 0.5 + ((screen.y - f.y) / f.height - 0.5) * b,
             width: a * screen.width / f.width, height: b * screen.height / f.height };
}

// The renderer clamps its scale to this range (RenderExtent.h).
function clampScale(scale) {
    const s = Number(scale);
    return Math.min(1, Math.max(0.25, isFinite(s) && s > 0 ? s : 1));
}

// The render scale for a canvas of `canvasW` x `canvasH` (the rendering
// screen's logical pixels) on a screen of scale factor `dpr`.  The renderer
// draws at item size x scale factor x render scale, as for one screen.  The
// user's own scale is returned as it is while the larger side stays within
// `cap` pixels; a larger canvas gets the scale that brings it to `cap`.
export function renderScale(userScale, canvasW, canvasH, dpr, cap) {
    const limit = cap > 0 ? cap : maxSide;
    const side = Math.max(Number(canvasW) || 0, Number(canvasH) || 0) * (dpr > 0 ? dpr : 1);
    if (!(side > 0) || side * clampScale(userScale) <= limit) return userScale;
    return limit / side;
}

// The size the renderer draws at for that canvas, in pixels (for the log;
// the renderer works it out itself from its window's scale factor).
export function renderSize(canvasW, canvasH, dpr, scale) {
    const k = (dpr > 0 ? dpr : 1) * clampScale(scale);
    return { width: Math.max(64, Math.round(canvasW * k)), height: Math.max(64, Math.round(canvasH * k)) };
}
