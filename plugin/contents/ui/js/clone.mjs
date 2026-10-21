// Pure, cross-runtime (Node + QJSEngine) rules for ShareRenderer (we-manager
// clone patch): one scene renderer for the screens of a plasmashell that show
// the same wallpaper.  CloneLink.qml is a thin layer over these, and every
// screen runs them on the same table (src/CloneGroup.hpp), so every screen
// comes to the same answer.
//
// A member is one screen's entry in that table:
//   { id, key, screen, prefer, area, role, frameKey, playing, stalled }
//   id        the screen's number in the table
//   key       groupKey() of what it shows; screens with the same key are a group
//   screen    its connector name ("DP-1"), for the order and the log
//   prefer    ShareRenderer 2 on that screen: it renders when it can
//   area      its size in pixels
//   role      "source" (it runs the renderer) or "mirror" (it shows the source's frame)
//   frameKey  the name of the frame a source publishes ("" for a mirror)
//   playing   the screen's own pause rules let its wallpaper play
//   stalled   its window is not being drawn (a source: none of its frames
//             was taken; a mirror: asked to draw, it did not)

// The group of a scene: the same file with the same user properties gives the
// same picture.  The frame rate, the quality settings, the sound and the
// volume are the rendering screen's and are not part of it.  "" for no scene.
export function groupKey(path, userPropsJson) {
    if (!path) return "";
    return String(path) + "\n" + (userPropsJson || "");
}

function better(a, b) {
    if (!!a.prefer !== !!b.prefer) return !!a.prefer;
    const ra = a.role === "source", rb = b.role === "source";
    if (ra !== rb) return ra;
    const aa = Number(a.area) || 0, ab = Number(b.area) || 0;
    if (aa !== ab) return aa > ab;
    const sa = String(a.screen || ""), sb = String(b.screen || "");
    if (sa !== sb) return sa < sb;
    return a.id < b.id;
}

// The id of the member that renders for the group `key`, -1 with no member.
//  * A screen the user chose (prefer) comes first.
//  * Then the one that renders now stays: moving the renderer loads the scene
//    again, so it moves only when it has to.
//  * Then the largest screen, then the first by name.
// A stalled member is passed over while another one can render; when every
// member is stalled the rule above holds as if none were (the renderer stays
// where it is, and nothing moves in a circle while the screens are off).
// Being named here is not yet leave to begin: see mayTake().
export function elect(members, key) {
    if (!key) return -1;
    const group = (members || []).filter(m => m && m.key === key);
    if (group.length === 0) return -1;
    const fit = group.filter(m => !m.stalled);
    const pool = fit.length > 0 ? fit : group;
    let best = pool[0];
    for (let i = 1; i < pool.length; i++)
        if (better(pool[i], best)) best = pool[i];
    return best.id;
}

// The member that renders for the group `key` now, or null: the one whose
// role is "source" (the better one, should two say so for a moment).
export function rendering(members, key) {
    if (!key) return null;
    let best = null;
    for (const m of (members || [])) {
        if (!m || m.key !== key || m.role !== "source") continue;
        if (best === null || better(m, best)) best = m;
    }
    return best;
}

// Whether the screen `selfId`, which elect() names, may begin rendering now.
//  * Nobody renders, or it is the screen the user chose taking over from a
//    renderer that is fine: yes.
//  * The renderer is stalled (its window is not drawn): only a screen whose
//    own window was seen drawing at two checks in a row since (`drawnRuns`,
//    CloneLink's drawCheck).  With the monitors asleep no screen is drawn,
//    no screen takes over, and the renderer stays loaded where it is: at the
//    wake the same scene goes on.
export function mayTake(members, key, selfId, drawnRuns) {
    const cur = rendering(members, key);
    if (cur === null || cur.id === selfId || !cur.stalled) return true;
    return (Number(drawnRuns) || 0) >= 2;
}

// The member with this id, or null.
export function memberById(members, id) {
    for (const m of (members || []))
        if (m && m.id === id) return m;
    return null;
}

// Whether a screen of the group other than `selfId` wants the wallpaper to
// play: the rendering screen then plays on, whatever pauses it itself (a
// fullscreen window on it), so the other screens do not freeze.
export function othersPlaying(members, key, selfId) {
    if (!key) return false;
    return (members || []).some(m => m && m.key === key && m.id !== selfId && m.playing === true);
}

// How many screens of the group other than `selfId` there are.
export function others(members, key, selfId) {
    if (!key) return 0;
    return (members || []).filter(m => m && m.key === key && m.id !== selfId).length;
}

// Where a mirror puts the frame.  `mode` is the screen's alignment: "crop"
// (cover the screen, cut what does not fit), "fit" (the whole frame, bars of
// the background colour) or "stretch".  `frameAspect` is the frame's width
// over height on its own screen (0 before a frame).  The result is the mirror
// item's place in its parent (x, y, width, height) and the part of the frame
// it shows, in fractions of the frame (sx, sy, sw, sh).
//
// The frame is what the rendering screen shows, not the whole scene: a mirror
// crops or fits that picture again.  On screens of one shape it is the same
// picture; on another shape, crop cuts the rendering screen's picture down to
// this screen's shape.
export function mirrorPlacement(mode, frameAspect, parentW, parentH) {
    const full = { x: 0, y: 0, width: parentW, height: parentH, sx: 0, sy: 0, sw: 1, sh: 1 };
    if (!(frameAspect > 0) || !(parentW > 0) || !(parentH > 0) || mode === "stretch") return full;
    const own = parentW / parentH;
    if (mode === "fit") {
        if (frameAspect > own) {
            const h = parentW / frameAspect;
            return { x: 0, y: (parentH - h) / 2, width: parentW, height: h, sx: 0, sy: 0, sw: 1, sh: 1 };
        }
        const w = parentH * frameAspect;
        return { x: (parentW - w) / 2, y: 0, width: w, height: parentH, sx: 0, sy: 0, sw: 1, sh: 1 };
    }
    // crop
    if (frameAspect > own) {
        const sw = own / frameAspect;
        return { x: 0, y: 0, width: parentW, height: parentH, sx: (1 - sw) / 2, sy: 0, sw: sw, sh: 1 };
    }
    const sh = frameAspect / own;
    return { x: 0, y: 0, width: parentW, height: parentH, sx: 0, sy: (1 - sh) / 2, sw: 1, sh: sh };
}
