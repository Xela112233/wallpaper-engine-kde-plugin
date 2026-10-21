import { test } from 'node:test';
import assert from 'node:assert/strict';

import { PauseMode, Action, countWindows, shouldPlay, coversScreen, windowRequests, isOwnApp }
    from '../../plugin/contents/ui/js/windowplay.mjs';

// Window descriptor helper: defaults everything false, override per case.
// Matches the shape WindowModel.qml builds from tasksModel.data(idx, role):
//   { isMinimized, isMaximized, isFullScreen, isActive }
const w = (o = {}) => ({
    isMinimized: false, isMaximized: false, isFullScreen: false, isActive: false, ...o,
});

// ── enum values must match Common.qml's PauseMode declaration order ──────────
test('PauseMode integer values match the Common.qml enum order', () => {
    assert.equal(PauseMode.Never, 0);
    assert.equal(PauseMode.Any, 1);
    assert.equal(PauseMode.Max, 2);
    assert.equal(PauseMode.Focus, 3);
    assert.equal(PauseMode.FocusOrMax, 4);
    assert.equal(PauseMode.FullScreen, 5);
});

// ── empty model: every mode plays (no window satisfies any predicate) ────────
test('empty model plays in every mode (including Never and unknown)', () => {
    for (const m of [0, 1, 2, 3, 4, 5, 99]) assert.equal(shouldPlay([], m), true);
});

// ── Any: pause iff a non-minimized window exists ─────────────────────────────
test('Any: one non-minimized window pauses', () => {
    assert.equal(shouldPlay([w()], PauseMode.Any), false);
});
test('Any: a single minimized window plays (minimized excluded)', () => {
    assert.equal(shouldPlay([w({ isMinimized: true })], PauseMode.Any), true);
});

// ── Max: pause iff a non-minimized maximized-or-fullscreen window exists ──────
test('Max: a maximized window pauses', () => {
    assert.equal(shouldPlay([w({ isMaximized: true })], PauseMode.Max), false);
});
test('Max: a fullscreen window pauses (max counts fullscreen)', () => {
    assert.equal(shouldPlay([w({ isFullScreen: true })], PauseMode.Max), false);
});
test('Max: a plain non-minimized window plays', () => {
    assert.equal(shouldPlay([w()], PauseMode.Max), true);
});
test('Max: a maximized-but-minimized window plays (minimized excluded first)', () => {
    assert.equal(shouldPlay([w({ isMaximized: true, isMinimized: true })], PauseMode.Max), true);
});

// ── Focus: pause iff a non-minimized active window exists ─────────────────────
test('Focus: an active window pauses', () => {
    assert.equal(shouldPlay([w({ isActive: true })], PauseMode.Focus), false);
});
test('Focus: an active-but-minimized window plays', () => {
    assert.equal(shouldPlay([w({ isActive: true, isMinimized: true })], PauseMode.Focus), true);
});
test('Focus: non-active windows play', () => {
    assert.equal(shouldPlay([w(), w()], PauseMode.Focus), true);
});

// ── FocusOrMax: pause iff active OR maximized (non-minimized) ──────────────────
test('FocusOrMax: active-only pauses', () => {
    assert.equal(shouldPlay([w({ isActive: true })], PauseMode.FocusOrMax), false);
});
test('FocusOrMax: maximized-only pauses', () => {
    assert.equal(shouldPlay([w({ isMaximized: true })], PauseMode.FocusOrMax), false);
});
test('FocusOrMax: neither active nor maximized plays', () => {
    assert.equal(shouldPlay([w(), w()], PauseMode.FocusOrMax), true);
});

// ── FullScreen: pause iff a non-minimized fullscreen window exists ────────────
test('FullScreen: a fullscreen window pauses', () => {
    assert.equal(shouldPlay([w({ isFullScreen: true })], PauseMode.FullScreen), false);
});
test('FullScreen: a maximized-not-fullscreen window plays (distinguishes Max from FullScreen)', () => {
    assert.equal(shouldPlay([w({ isMaximized: true })], PauseMode.FullScreen), true);
});
test('FullScreen: a fullscreen-but-minimized window plays', () => {
    assert.equal(shouldPlay([w({ isFullScreen: true, isMinimized: true })], PauseMode.FullScreen), true);
});

// ── countWindows early-out invariants (pin the optimization, not just decision)
test('countWindows(Any) skips max/full/active work (all stay 0)', () => {
    const c = countWindows([w({ isActive: true, isMaximized: true, isFullScreen: true })], PauseMode.Any);
    assert.equal(c.notMin, 1);
    assert.equal(c.max, 0);
    assert.equal(c.full, 0);
    assert.equal(c.active, 0);
});
test('countWindows(Focus) counts active but skips max/full', () => {
    const c = countWindows([w({ isActive: true, isMaximized: true, isFullScreen: true })], PauseMode.Focus);
    assert.equal(c.notMin, 1);
    assert.equal(c.active, 1);
    assert.equal(c.max, 0);
    assert.equal(c.full, 0);
});

// ── mixed realistic set across all six modes (integration-shaped equivalence) ─
test('mixed set (active+maximized, fullscreen, minimized) matches every mode', () => {
    const set = [
        w({ isActive: true, isMaximized: true }),  // counts: notMin, active, max
        w({ isFullScreen: true }),                 // counts: notMin, full, max
        w({ isMinimized: true, isMaximized: true }), // excluded (minimized)
    ];
    assert.equal(shouldPlay(set, PauseMode.Any), false);        // 2 non-min
    assert.equal(shouldPlay(set, PauseMode.Max), false);        // 2 max-or-fs
    assert.equal(shouldPlay(set, PauseMode.Focus), false);      // 1 active
    assert.equal(shouldPlay(set, PauseMode.FocusOrMax), false); // active or max
    assert.equal(shouldPlay(set, PauseMode.FullScreen), false); // 1 fullscreen
    assert.equal(shouldPlay(set, PauseMode.Never), true);       // never pauses
});

// ── coversScreen: does a window's own rect contain the screen's rect? ────────
const screen = { x: 0, y: 0, width: 1920, height: 1080 };

test('coversScreen: an exact-match window rect covers the screen', () => {
    assert.equal(coversScreen({ x: 0, y: 0, width: 1920, height: 1080 }, screen), true);
});
test('coversScreen: a window rect larger than the screen still covers it (decoration/rounding slop)', () => {
    assert.equal(coversScreen({ x: -4, y: -4, width: 1930, height: 1090 }, screen), true);
});
test('coversScreen: a window smaller than the screen does not cover it', () => {
    assert.equal(coversScreen({ x: 0, y: 0, width: 800, height: 600 }, screen), false);
});
test('coversScreen: a task with no reported geometry does not cover the screen', () => {
    assert.equal(coversScreen(undefined, screen), false);
});
test('coversScreen: no screen geometry to compare against does not cover the screen', () => {
    assert.equal(coversScreen({ x: 0, y: 0, width: 1920, height: 1080 }, undefined), false);
});

// ── a screen-covering window with neither WM state flag set (borderless / ────
// windowed-fullscreen games are ordinary, undecorated windows as far as
// IsMaximized/IsFullScreen go) must still be treated as full/maximized.
const borderless = w({ geometry: { x: 0, y: 0, width: 1920, height: 1080 } });

test('Max: a screen-covering window with no isMaximized/isFullScreen flags still pauses', () => {
    assert.equal(shouldPlay([borderless], PauseMode.Max, screen), false);
});
test('FullScreen: same screen-covering window still pauses', () => {
    assert.equal(shouldPlay([borderless], PauseMode.FullScreen, screen), false);
});
test('FocusOrMax: same screen-covering window still pauses', () => {
    assert.equal(shouldPlay([borderless], PauseMode.FocusOrMax, screen), false);
});
test('Focus: a screen-covering window without isActive does not pause (geometry trigger never reached)', () => {
    assert.equal(shouldPlay([borderless], PauseMode.Focus, screen), true);
});
test('Max: omitting the screenGeometry argument matches the pre-geometry behavior (no false pause)', () => {
    assert.equal(shouldPlay([borderless], PauseMode.Max), true);
});

// ── windowRequests: an action per condition (FocusAction, MaximizedAction, ──
// FullscreenAction).  With every action at its default (Pause) the decision
// must be exactly shouldPlay's, in every mode, so configs from before the
// actions existed keep their behaviour.
const shapes = [
    [], [w()], [w({ isMinimized: true })], [w({ isActive: true })], [w({ isMaximized: true })],
    [w({ isFullScreen: true })], [w({ isActive: true, isFullScreen: true })], [borderless],
    [w({ isMaximized: true, isMinimized: true })], [w(), w({ isActive: true }), w({ isMaximized: true })],
];
test('windowRequests with default actions pauses exactly when shouldPlay says not to play', () => {
    for (const mode of [0, 1, 2, 3, 4, 5, 99])
        for (const ws of shapes)
            for (const g of [undefined, screen]) {
                const r = windowRequests(ws, mode, g, {});
                assert.equal(r.pause, !shouldPlay(ws, mode, g), `mode ${mode} ${JSON.stringify(ws)}`);
                assert.equal(r.mute || r.stop, false);
            }
});
test('Action values are the main.xml ones: 0 keep, 1 mute, 2 pause, 3 stop', () => {
    assert.deepEqual([Action.Keep, Action.Mute, Action.Pause, Action.Stop], [0, 1, 2, 3]);
});
test('windowRequests: a fullscreen window follows FullscreenAction, not MaximizedAction', () => {
    const acts = { maximized: Action.Pause, fullscreen: Action.Stop };
    assert.deepEqual(windowRequests([w({ isFullScreen: true })], PauseMode.Max, screen, acts),
                     { pause: false, mute: false, stop: true });
    assert.deepEqual(windowRequests([w({ isMaximized: true })], PauseMode.Max, screen, acts),
                     { pause: true, mute: false, stop: false });
    // borderless: covers the screen without the flag, still fullscreen
    assert.equal(windowRequests([borderless], PauseMode.Max, screen, acts).stop, true);
});
test('windowRequests: a focused fullscreen window is fullscreen where fullscreen is watched', () => {
    const acts = { focus: Action.Mute, fullscreen: Action.Keep };
    const game = [w({ isActive: true, isFullScreen: true })];
    assert.deepEqual(windowRequests(game, PauseMode.FocusOrMax, screen, acts),
                     { pause: false, mute: false, stop: false });
    // Focus mode watches focus only: the same window is a focused window there
    assert.deepEqual(windowRequests(game, PauseMode.Focus, screen, acts),
                     { pause: false, mute: true, stop: false });
});
test('windowRequests: Keep ignores a condition, and each window adds its own request', () => {
    const acts = { focus: Action.Keep, maximized: Action.Mute, fullscreen: Action.Stop };
    const ws = [w({ isActive: true }), w({ isMaximized: true })];
    assert.deepEqual(windowRequests(ws, PauseMode.FocusOrMax, screen, acts),
                     { pause: false, mute: true, stop: false });
    assert.deepEqual(windowRequests(ws.concat([w({ isFullScreen: true })]), PauseMode.FocusOrMax, screen, acts),
                     { pause: false, mute: true, stop: true });
});
test('windowRequests: Any mode still pauses for a plain window whatever the actions', () => {
    const acts = { focus: Action.Keep, maximized: Action.Keep, fullscreen: Action.Keep };
    assert.equal(windowRequests([w()], PauseMode.Any, screen, acts).pause, true);
    assert.equal(windowRequests([w({ isMaximized: true })], PauseMode.Any, screen, acts).pause, false);
});
test('windowRequests: Never and minimized windows request nothing; unknown values mean Pause', () => {
    const none = { pause: false, mute: false, stop: false };
    assert.deepEqual(windowRequests([w({ isFullScreen: true })], PauseMode.Never, screen, { fullscreen: 3 }), none);
    assert.deepEqual(windowRequests([w({ isFullScreen: true, isMinimized: true })], PauseMode.Max, screen,
                                    { fullscreen: 3 }), none);
    assert.equal(windowRequests([w({ isFullScreen: true })], PauseMode.Max, screen, { fullscreen: 7 }).pause, true);
});
test('isOwnApp: we-manager by its desktop file id or its X11 class, nothing else', () => {
    assert.equal(isOwnApp('io.github.wemanager'), true);
    assert.equal(isOwnApp('io.github.wemanager.desktop'), true);
    assert.equal(isOwnApp('we-manager'), true);
    for (const other of ['org.kde.dolphin', 'firefox', 'io.github.wemanager2', '', undefined, null])
        assert.equal(isOwnApp(other), false, String(other));
});
test('windowRequests: we-manager\'s own window, open or focused, pauses and stops nothing', () => {
    // mini mode: the small window you tune the wallpaper in is the focused one
    const acts = { focus: Action.Stop, maximized: Action.Pause, fullscreen: Action.Pause };
    const none = { pause: false, mute: false, stop: false };
    const mini = w({ isActive: true, own: true });
    for (const mode of [PauseMode.Any, PauseMode.Focus, PauseMode.FocusOrMax, PauseMode.Max])
        assert.deepEqual(windowRequests([mini], mode, screen, acts), none, String(mode));
    // another application focused still counts
    assert.deepEqual(windowRequests([mini, w({ isActive: true })], PauseMode.Focus, screen, acts),
                     { pause: false, mute: false, stop: true });
    assert.equal(windowRequests([mini, w()], PauseMode.Any, screen, acts).pause, true);
    // maximized or fullscreen it hides the desktop like any window
    assert.equal(windowRequests([w({ own: true, isMaximized: true })], PauseMode.Max, screen, acts).pause, true);
    assert.equal(windowRequests([w({ own: true, isMaximized: true })], PauseMode.Any, screen, acts).pause, true);
    assert.equal(windowRequests([w({ own: true, isFullScreen: true })], PauseMode.FullScreen, screen, acts).pause,
                 true);
});
