// Pure helpers for SchemeAccent.qml (we-manager AccentFromSchemeColor): the
// wallpaper's own scheme colour, which Plasma's "accent colour from
// wallpaper" takes instead of sampling the picture. Cross-runtime (QJSEngine
// + Node) so it is unit-testable without Plasma.

// A Wallpaper Engine colour value, "r g b" with each channel 0-1, as
// [r, g, b]; null for anything else. A value written with 0-255 channels
// (any channel above 1) is scaled down rather than clamped to white.
export function parseSchemeColor(value) {
    if (typeof value !== 'string') return null;
    const parts = value.trim().split(/\s+/).map(Number);
    if (parts.length !== 3 || parts.some((v) => !Number.isFinite(v) || v < 0)) return null;
    const scale = parts.some((v) => v > 1) ? 255 : 1;
    return parts.map((v) => Math.min(1, v / scale));
}

// The user's own value for the schemecolor property: the wallpaper's
// per-wallpaper config user_props, which holds "r g b" (or {value: "r g b"}).
export function userSchemeColor(userProps) {
    const v = userProps && userProps.schemecolor;
    return parseSchemeColor(v && typeof v === 'object' ? v.value : v);
}

// project.json's general.properties.schemecolor, from the file's text.
export function projectSchemeColor(projectJsonText) {
    let project = null;
    try {
        project = JSON.parse(projectJsonText);
    } catch (e) {
        return null;
    }
    const p = project && project.general && project.general.properties
        && project.general.properties.schemecolor;
    return parseSchemeColor(p && typeof p === 'object' ? p.value : null);
}

// project.json next to the wallpaper's file (scene.json, index.html, the
// video), as a native path; '' for none.
export function projectJsonPath(wallpaperPath) {
    let p = String(wallpaperPath || '');
    if (p.startsWith('file://')) p = p.slice(7);
    else if (p.startsWith('file:')) p = p.slice(5);
    const slash = p.lastIndexOf('/');
    return slash > 0 ? p.substring(0, slash) + '/project.json' : '';
}
