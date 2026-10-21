// Pure, cross-runtime (Node + QJSEngine) pause/play decision for WindowModel.
// Mirrors the layout.mjs extraction: keep the decision pure so it is
// unit-testable without a Plasma WindowModel / TasksModel / QQuickWindow, and
// so its equivalence to the previous multi-pass filter pipeline can be pinned.
// WindowModel.qml is a thin binding layer over this.

// Integer values MUST match Common.qml's PauseMode enum (declaration order).
export const PauseMode = Object.freeze({
    Never: 0, Any: 1, Max: 2, Focus: 3, FocusOrMax: 4, FullScreen: 5,
});

// A window whose own reported rect contains the wallpaper's screen rect is
// occluding the desktop even when the window manager never sets
// IsMaximized/IsFullScreen -- the common case for a game run in
// borderless/windowed-fullscreen mode.
export function coversScreen(windowGeometry, screenGeometry) {
    if (!windowGeometry || !screenGeometry) return false;
    return windowGeometry.x <= screenGeometry.x
        && windowGeometry.y <= screenGeometry.y
        && windowGeometry.x + windowGeometry.width  >= screenGeometry.x + screenGeometry.width
        && windowGeometry.y + windowGeometry.height >= screenGeometry.y + screenGeometry.height;
}

// One pass over `windows`. Each descriptor is a window that already passed the
// IsWindow + activity filters: { isMinimized, isMaximized, isFullScreen,
// isActive, geometry } (booleans, plus an optional geometry rect). `mode`
// selects an early-out so we only inspect what a mode needs (Focus ->
// isActive only; Any -> notMin only). Returns the small count set the modes
// consume. max/full/active are conditioned on "not minimized", matching the
// previous code where maxWModel/fullSModel/activeModel were all derived from
// notMinWModel. `screenGeometry`, when supplied, also counts a window as
// full/maximized when its own geometry covers the screen (see coversScreen).
export function countWindows(windows, mode, screenGeometry) {
    let notMin = 0, max = 0, full = 0, active = 0;
    for (let i = 0; i < windows.length; i++) {
        const w = windows[i];
        if (w.isMinimized === true) continue;   // matches IsMinimized:false filter
        notMin++;
        if (mode === PauseMode.Any) continue;    // Any needs only notMin
        if (w.isActive === true) active++;
        if (mode === PauseMode.Focus) continue;  // Focus needs only active
        const isFull = w.isFullScreen === true || coversScreen(w.geometry, screenGeometry);
        if (isFull) full++;
        if (w.isMaximized === true || isFull) max++;
    }
    return { notMin, max, full, active };
}

// What a window condition does to the wallpaper (FocusAction, MaximizedAction,
// FullscreenAction in main.xml).  Pause is every action's default, which is
// what the plugin did before there were per-condition actions.
export const Action = Object.freeze({ Keep: 0, Mute: 1, Pause: 2, Stop: 3 });

// The conditions each PauseMode watches.  `any` is Any mode's catch-all: a
// window that is none of the other three still pauses there.
function watched(mode) {
    switch (mode) {
    case PauseMode.Any:        return { focus: true,  max: true,  full: true, any: true };
    case PauseMode.Max:        return { focus: false, max: true,  full: true, any: false };
    case PauseMode.Focus:      return { focus: true,  max: false, full: false, any: false };
    case PauseMode.FocusOrMax: return { focus: true,  max: true,  full: true, any: false };
    case PauseMode.FullScreen: return { focus: false, max: false, full: true, any: false };
    default:                   return { focus: false, max: false, full: false, any: false };
    }
}

// we-manager's own windows (TasksModel's AppId: the desktop file id on
// Wayland, and on X11 too when KWin matched its class to the desktop file).
// You tune the wallpaper there while you watch it, mini mode above all, and
// Wallpaper Engine's rules only count other applications: open or focused,
// such a window never pauses or stops the wallpaper.  Maximized or
// fullscreen it hides the desktop, and counts as any window does.
export const OWN_APP_IDS = Object.freeze(['io.github.wemanager', 'we-manager']);
export function isOwnApp(appId) {
    const id = String(appId || '').replace(/\.desktop$/, '');
    return OWN_APP_IDS.indexOf(id) >= 0;
}

function actionOf(value) {
    return (value === Action.Keep || value === Action.Mute || value === Action.Stop) ? value : Action.Pause;
}

// The per-condition decision: { pause, mute, stop } requested by the windows.
// PauseMode still picks which conditions are watched; `actions`
// ({ focus, maximized, fullscreen }, each an Action, missing = Pause) says
// what each one does.  A window counts once, for the most specific watched
// condition it meets (fullscreen, then maximized, then focused), so a
// focused fullscreen game follows FullscreenAction where fullscreen is
// watched.  With every action Pause, `pause` is exactly !shouldPlay(), so
// configs from before the actions existed behave as they did.
export function windowRequests(windows, mode, screenGeometry, actions) {
    const out = { pause: false, mute: false, stop: false };
    const w = watched(mode);
    const a = actions || {};
    for (let i = 0; i < windows.length; i++) {
        const win = windows[i];
        if (win.isMinimized === true) continue;
        const isFull = win.isFullScreen === true || coversScreen(win.geometry, screenGeometry);
        const isMax = win.isMaximized === true || isFull;
        const own = win.own === true;                // we-manager's window (isOwnApp)
        let act = null;
        if (w.full && isFull)                      act = actionOf(a.fullscreen);
        else if (w.max && isMax)                   act = actionOf(a.maximized);
        else if (w.focus && win.isActive === true && !own) act = actionOf(a.focus);
        else if (w.any && (!own || isMax))         act = Action.Pause;
        if (act === Action.Pause) out.pause = true;
        else if (act === Action.Mute) out.mute = true;
        else if (act === Action.Stop) out.stop = true;
    }
    return out;
}

// The play decision: returns true when the wallpaper should PLAY, exactly
// reproducing the previous switch (play iff no window satisfies the mode's
// predicate). Unknown modes play (the previous `default:` branch). The
// optional `screenGeometry` extends the max/full family (Max, FullScreen,
// FocusOrMax) with the geometry-containment trigger; omitting it reproduces
// the pre-geometry behavior exactly.
export function shouldPlay(windows, mode, screenGeometry) {
    if (mode === PauseMode.Never) return true;
    const c = countWindows(windows, mode, screenGeometry);
    switch (mode) {
    case PauseMode.Any:        return c.notMin === 0;
    case PauseMode.Max:        return c.max === 0;
    case PauseMode.Focus:      return c.active === 0;
    case PauseMode.FocusOrMax: return c.max === 0 && c.active === 0;
    case PauseMode.FullScreen: return c.full === 0;
    default:                   return true;
    }
}
