import QtQuick
import "js/accent.mjs" as Accent

// Plasma's "accent colour from wallpaper" from the wallpaper's own scheme
// colour (we-manager, the AccentFromSchemeColor setting).
//
// Wallpaper Engine sets the system accent colour from the wallpaper's scheme
// colour. Plasma instead samples the picture whenever the wallpaper item
// announces accentColorChanged with a transparent accentColor, and takes a
// non-transparent accentColor as it is. So announce() hands Plasma the
// scheme colour (the user's value for it, else project.json's) as
// wallpaperItem.accentColor, and a wallpaper without one (or the setting off)
// gets accentColor back to transparent, which makes Plasma sample again.
// Plasma only reads the wallpaper on screen 0 (DesktopView::usedInAccentColor).
//
// project.json is read when `path` changes, long before main.qml announces
// after the first frame, so announce() itself stays synchronous; a read that
// lands after the announcement still hands its colour over.
QtObject {
    id: root

    // The Plasma WallpaperItem (main.qml's `wallpaper`).
    property var wallpaperItem: null
    // Pyext.readfile: path -> Promise of the file's text.
    property var readfile: null
    property bool enabled: true
    // The wallpaper's file (main.qml's background.wallpaperPath).
    property string path: ""
    // Its per-wallpaper user_props (main.qml's curOpt.user_props).
    property var userProps: ({})

    // project.json's colour for _projectFor ([r, g, b] or null).
    property var _projectColour: null
    property string _projectFor: ""
    property string _requestedFor: ""
    // The path the last announcement was for.
    property string _announcedFor: ""

    onPathChanged: _load()
    Component.onCompleted: _load()
    onEnabledChanged: if (_announcedFor !== "" && _announcedFor === path) announce()
    // The user picked another scheme colour while the wallpaper shows: follow
    // it. Only a colour is handed over here, never a resample, so a change
    // that arrives during a wallpaper handoff cannot make Plasma sample the
    // outgoing picture.
    onUserPropsChanged: {
        if (!enabled || _announcedFor === "" || _announcedFor !== path) return;
        const c = colour();
        if (c) _set(c);
    }

    function _usable() {
        return wallpaperItem && wallpaperItem.hasOwnProperty('accentColor');
    }

    function _load() {
        // A path given at creation reaches both handlers: read it once.
        const p = path;
        if (p === _requestedFor) return;
        _requestedFor = p;
        _projectColour = null;
        _projectFor = "";
        const file = Accent.projectJsonPath(p);
        if (!file || typeof readfile !== "function") return;
        readfile(file).then(function(text) {
            if (p !== root.path) return;          // another wallpaper since
            root._projectColour = Accent.projectSchemeColor(text);
            root._projectFor = p;
            // Announced before the file was read: hand the colour over now.
            if (root._projectColour && root._announcedFor === p && root.enabled
                && !Accent.userSchemeColor(root.userProps))
                root._set(root._projectColour);
        }, function() {
            // No project.json (a video from the Videos page): sampling it is.
            if (p === root.path) root._projectFor = p;
        });
    }

    // The scheme colour to use for `path` now, or null.
    function colour() {
        return Accent.userSchemeColor(userProps)
            || (_projectFor === path ? _projectColour : null);
    }

    // Called by main.qml once the wallpaper Plasma should take the colour
    // from is the one on screen (after its first frame and the handoff fade).
    function announce() {
        if (!_usable()) return;
        _announcedFor = path;
        const c = enabled ? colour() : null;
        if (c) _set(c);
        else sample();
    }

    // Plasma's WallpaperItem.accentColor is writable; a host whose isn't
    // (an older libplasma, a test harness) gets the old behaviour, sampling,
    // and a warning naming the write, instead of an exception that would
    // end main.qml's first-frame handler early.
    function _assign(value) {
        try {
            wallpaperItem.accentColor = value;
            return true;
        } catch (e) {
            console.warn("[WEK] SchemeAccent: WallpaperItem.accentColor = " + value + " failed (" + e
                         + "); Plasma samples the picture instead");
            return false;
        }
    }

    function _set(c) {
        if (_usable() && !_assign(Qt.rgba(c[0], c[1], c[2], 1)))
            wallpaperItem.accentColorChanged();
    }

    // Plasma samples the picture on accentColorChanged with a transparent
    // accentColor: back to transparent (which announces it), or announce.
    function sample() {
        if (!_usable()) return;
        if (Qt.colorEqual(wallpaperItem.accentColor, "transparent") || !_assign("transparent"))
            wallpaperItem.accentColorChanged();
    }
}
