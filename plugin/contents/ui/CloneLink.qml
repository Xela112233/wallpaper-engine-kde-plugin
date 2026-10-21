// One renderer for screens that show the same scene (we-manager clone patch,
// main.xml ShareRenderer).  main.qml makes this with Qt.createComponent, and
// only while the setting is on: off, there is no CloneLink, no CloneGroup and
// no shared frame, and every screen renders its own scene as before.  A
// plugin library without CloneGroup (one from before the patch, kept by
// plasmashell across an RPM upgrade) fails this file only, which main.qml
// logs; the wallpaper loads as before.
//
// What it does, with the rules of js/clone.mjs on the table every screen of
// the process writes to (src/CloneGroup.hpp):
//  * claim(), from main.qml's loadBackend() for a scene: this screen joins
//    the group of its wallpaper (the same scene file with the same user
//    properties) and is told its part.  The source loads backend/Scene.qml as
//    always, and its SceneViewer publishes its frame (shareKey); a mirror
//    loads backend/SceneMirror.qml, which shows that frame and holds no
//    renderer.  No screen begins to render inside claim(): it loads a mirror
//    with no frame first and takes the rendering in follow(), followDelayMs
//    later, when the screens that load in the same moment are all in the
//    table.  So the screen the user chose, or the largest, renders from the
//    start, and no scene load is begun and dropped.
//  * follow(), when another screen's entry changed: if this screen's part
//    changed (the source stopped, went away, shows another wallpaper, or the
//    user chose another screen to render), its backend is loaded again, as a
//    reload: the old one stays on screen until the new one draws.
//  * keepsPlaying(): the source plays on while another screen of the group
//    wants the wallpaper playing, whatever pauses it on its own screen.
//  * A screen that leaves the scene (a stop, another type of wallpaper, a
//    failure pane) leaves the group; a source that leaves is replaced.
//  * A source whose window is not drawn (stallCheck) hands over, but only to
//    a screen whose window is drawn (drawCheck, Clone.mayTake).  With every
//    screen asleep nothing moves and nothing is unloaded.
// Each change of part is one line in the journal: "[WEK] clone: ...".
import QtQuick
import com.github.captsilver.wallpaperEngineKde
import "js/clone.mjs" as Clone

Item {
    id: link
    visible: false

    // main.qml's background, its backendLoader and its Pyext.
    property var host: null
    property var loader: null
    property var pyext: null

    // ShareRenderer 2: this screen renders when it can.
    readonly property bool prefer: host !== null && host.shareRenderer === 2
    readonly property string screenName: host !== null ? (host.Screen.name || "") : ""
    readonly property real area: host !== null
        ? host.width * host.height * Math.pow(host.Screen.devicePixelRatio || 1, 2) : 0
    // The screen's own pause rules let the wallpaper play.
    readonly property bool playing: host !== null && host.ok === true

    // The group this screen is in (Clone.groupKey); "" in none.
    property string key: ""
    // "source": this screen's Scene.qml renders for the group.  "mirror":
    // its SceneMirror.qml shows the source's frame.  "": in no group.
    property string role: ""
    // The source's frame: the one this screen publishes, or the one it shows.
    property string frameKey: ""
    // This screen rendered while its window was not being drawn (stallCheck).
    property bool   stalled: false
    // Why this screen cannot show another screen's frame; "" while it can.
    // Set once, it renders for itself until the link is made again.
    property string failure: ""
    // The SceneViewer that renders, while this screen is the source.
    property var    player: null
    // The SceneMirror item of this screen's backend, while it is a mirror.
    property var    mirror: null
    // follow() decided that this screen takes the rendering: claim() may
    // answer "source" for a screen that does not render yet.
    property bool   _take: false
    // The group's renderer is marked stalled (kept up to date at once, not
    // followDelayMs later: it starts the mirror's drawCheck).
    property bool   _sourceStalled: false
    // drawCheck: the sync count at the last check (-1 before one), and how
    // many checks in a row saw this mirror's window draw.
    property int    _probeCount: -1
    property int    _drawnRuns: 0
    // The last "[WEK] clone:" line, as written (the QML tests read it).
    property string lastLine: ""
    property string _said: ""
    property bool   _playedOn: false

    // A change on another screen is followed this much later, so that a
    // change of wallpaper that reaches the screens one after the other does
    // not make a mirror load the old scene for the moment in between.
    property int followDelayMs: 200
    // A source whose window has drawn none of its frames for stallMs, seen
    // at two checks in a row, is marked stalled; once marked, it looks every
    // stallClearMs whether its window draws again.
    property int stallMs: 5000
    property int stallCheckMs: 2000
    property int stallClearMs: 500
    // A mirror asks every probeMs whether its own window is drawn, while the
    // source or it is marked stalled.  Longer than stallClearMs, twice over,
    // so that at a wake the source is unmarked before a mirror may take over.
    property int probeMs: 1500

    CloneGroup { id: group }
    readonly property int memberId: group.id
    function members() { return group.members(); }

    function _say(text, warn) {
        link.lastLine = "[WEK] clone: " + text;
        if (warn) console.warn(link.lastLine);
        else console.info(link.lastLine);
    }
    function _where() { return link.screenName || "this screen"; }
    function _what() {
        return wallpaper.configuration.WallpaperWorkShopId || (link.host ? link.host.wallpaperPath : "");
    }

    // This screen's entry in the table.
    function publish() {
        if (link.key === "") {
            group.member = ({});
            return;
        }
        group.member = {
            key: link.key, screen: link.screenName, prefer: link.prefer, area: link.area, role: link.role,
            frameKey: link.role === "source" ? link.frameKey : "", playing: link.playing, stalled: link.stalled
        };
    }
    onPreferChanged: { publish(); if (key !== "") followSoon.restart(); }
    onAreaChanged: publish()
    onScreenNameChanged: publish()
    onPlayingChanged: publish()

    // What this screen is to be, by the table as it is now.
    function _decide() {
        const all = group.members();
        const want = Clone.elect(all, link.key);
        if (want === group.id) return { role: "source", frame: "", from: "" };
        const src = Clone.memberById(all, want);
        // A source that has not made its renderer yet has no frame to show.
        const frame = src && src.role === "source" ? (src.frameKey || "") : "";
        // The screen that renders goes on rendering until the one named
        // has its renderer: nobody else's frame is there to show before.
        if (frame === "" && link.role === "source") return { role: "source", frame: "", from: "" };
        return { role: "mirror", frame: frame, from: src ? (src.screen || "another screen") : "another screen" };
    }

    // main.qml's loadBackend(), about to load a scene: true when this screen
    // is to show another screen's frame (backend/SceneMirror.qml with
    // mirrorProperties()), false when it loads backend/Scene.qml.
    function claim() {
        followSoon.stop();
        if (link.failure !== "" || link.host === null) {
            link.leave();
            return false;
        }
        // The wallpaper's own options are read here, not taken from the
        // host: its curOpt still describes the wallpaper before this one.
        const wid = wallpaper.configuration.WallpaperWorkShopId;
        let props = "";
        if (wid && link.pyext)
            link.pyext.read_wallpaper_config(wid).then((res) => {
                props = res && res['user_props'] ? JSON.stringify(res['user_props']) : "";
            });
        const key = Clone.groupKey(link.host.wallpaperPath, props);
        if (key === "") {
            link.leave();
            return false;
        }
        // Another wallpaper: what this screen renders now is not it.
        if (key !== link.key) link.role = "";
        link.key = key;
        link.player = null;
        link.frameKey = "";
        link.publish();
        const d = link._decide();
        if (d.role === "source" && link.role !== "source" && !link._take) {
            // Not in a hurry: the screens that load in this moment are not
            // all in the table yet.  A mirror with no frame for now; follow()
            // takes the rendering when this screen is still the one.
            link.role = "mirror";
            link.frameKey = "";
            link.publish();
            link._sourceStalled = false;
            followSoon.restart();
            return true;
        }
        link.role = d.role;
        link.frameKey = d.frame;
        if (d.role === "source") link.mirror = null;
        link.publish();
        link._watchSource();
        const said = d.role + "\n" + key + "\n" + d.from;
        if (said !== link._said) {
            link._said = said;
            if (d.role === "source")
                link._say(link._where() + " renders " + link._what()
                          + " for the screens that show the same wallpaper");
            else
                link._say(link._where() + " shows " + d.from + "'s frame of " + link._what());
        }
        return d.role === "mirror";
    }
    function mirrorProperties() {
        return { link: link, frameKey: link.frameKey };
    }

    // backend/Scene.qml, for the SceneViewer it has just made: the source's
    // frame gets its name, and the viewer publishes under it.
    function sourceCreated(viewer) {
        if (link.role !== "source" || !viewer) return;
        const frame = group.newFrameKey();
        if (!Common.setIfSupported(viewer, "SceneViewer", "shareKey", frame)) {
            link.failure = "SceneViewer.shareKey is missing";
            link.leave();
            return;
        }
        link.player = viewer;
        link.frameKey = frame;
        link.publish();
    }

    // backend/SceneMirror.qml: its item cannot draw the frame (reason).  The
    // screen leaves the group and loads a renderer of its own.
    function mirrorFailed(reason) {
        if (link.failure !== "") return;
        link.failure = String(reason);
        link._say(link._where() + " cannot show another screen's frame (" + link.failure + "); it renders the "
                  + "wallpaper itself until Plasma restarts or ShareRenderer is switched off and on", true);
        link.leave();
        Qt.callLater(link._reload);
    }
    // backend/SceneMirror.qml, for the SceneMirror item it has just made.
    function mirrorCreated(item) {
        link.mirror = item;
        link._probeCount = -1;
        link._drawnRuns = 0;
    }
    // backend/SceneMirror.qml: its first frame is on screen, so this
    // screen's window is being drawn.
    function mirrorShown() {
        if (!link.stalled) return;
        link.stalled = false;
        link.publish();
    }

    function leave() {
        followSoon.stop();
        if (link.key === "" && link.role === "") return;
        link.key = "";
        link.role = "";
        link.frameKey = "";
        link.player = null;
        link.mirror = null;
        link._said = "";
        link._playedOn = false;
        link._sourceStalled = false;
        link.publish();
    }

    // The source plays on for the screens that show its frame.
    function playsOn() {
        return link.role === "source" && Clone.othersPlaying(group.members(), link.key, group.id);
    }
    // main.qml's autoPause(), when this screen's own rules pause it: true
    // when it is to play on all the same.
    function keepsPlaying() {
        link._playedOn = link.playsOn();
        return link._playedOn;
    }

    function _reload() {
        if (link.host === null || link.host.stopped || link.host.nowBackend !== "scene") return;
        // Not a change of wallpaper: the handoff cuts (transitions patch).
        // Unless a change of wallpaper is under way on this screen, its old
        // layer still waiting for the new one's first frame: this load is
        // then the second half of that change (claim() waited, or the
        // renderer's frame got its name), and its transition stays.
        const changing = link.loader && link.loader._outgoing && !link.loader._outgoingCuts;
        if (!changing) link.host._transitionCause = "reload";
        try {
            link.host.loadBackend();
        } finally {
            link.host._transitionCause = "";
        }
    }

    // Whether the group's renderer is marked stalled now.
    function _watchSource() {
        const cur = link.key !== "" ? Clone.rendering(group.members(), link.key) : null;
        link._sourceStalled = cur !== null && cur.id !== group.id && cur.stalled === true;
    }

    // The table changed: load again when this screen's part did.
    function follow() {
        if (link.key === "" || link.host === null) return;
        link._watchSource();
        const d = link._decide();
        if (d.role === "source" && link.role !== "source") {
            // From a stalled renderer only when this screen is being drawn
            // (drawCheck calls again when it knows).
            if (!Clone.mayTake(group.members(), link.key, group.id, link._drawnRuns)) return;
            link._take = true;
            try {
                link._reload();
            } finally {
                link._take = false;
            }
            return;
        }
        if (d.role !== link.role || (d.role === "mirror" && d.frame !== "" && d.frame !== link.frameKey)) {
            link._reload();
            return;
        }
        // The other screens' pauses came or went: the source's pause follows.
        const on = !link.host.ok && link.playsOn();
        if (on !== link._playedOn) {
            link._playedOn = on;
            link.host.autoPause();
        }
    }
    Timer {
        id: followSoon
        interval: Math.max(0, link.followDelayMs)
        repeat: false
        onTriggered: link.follow()
    }
    Connections {
        target: group
        function onChanged() {
            if (link.key === "") return;
            link._watchSource();
            followSoon.restart();
        }
    }

    Connections {
        target: link.host
        // Anything but a scene on this screen (a stop, a video, a failure
        // pane): it is in no group.
        function onNowBackendChanged() {
            if (link.host.nowBackend !== "scene") link.leave();
        }
        // The wallpaper's properties were edited: it may be another
        // group's picture now.
        function onUserPropsJsonChanged() { link._rekey(); }
    }
    function _rekey() {
        if (link.key === "" || link.host === null) return;
        const key = Clone.groupKey(link.host.wallpaperPath, link.host.userPropsJson);
        if (key === "" || key === link.key) return;
        link.key = key;
        link.publish();
        followSoon.restart();
    }

    // The source's window is not being drawn (its output is off, or the
    // compositor has stopped asking it for frames): the renderer's frames
    // reach no screen.  It is marked stalled, and a screen that is being
    // drawn takes the rendering (drawCheck below); with none, it stays here.
    // The mark goes when this screen draws again (frames taken again, or,
    // once it is a mirror, drawCheck and mirrorShown).
    Timer {
        id: stallCheck
        interval: Math.max(20, link.stalled ? Math.min(link.stallClearMs, link.stallCheckMs) : link.stallCheckMs)
        repeat: true
        running: link.role === "source" && link.key !== "" && link.player !== null
        property int over: 0
        onRunningChanged: over = 0
        onTriggered: {
            const viewer = link.player;
            const ms = viewer && typeof viewer.sharedFrameStalledMs === "function" ? viewer.sharedFrameStalledMs() : 0;
            if (ms >= link.stallMs) {
                over += 1;
                if (over >= 2 && !link.stalled && Clone.others(group.members(), link.key, group.id) > 0) {
                    link.stalled = true;
                    link.publish();
                    link._say(link._where() + " renders, but its window has drawn no frame for "
                              + Math.round(ms / 1000) + " s; a screen that is being drawn takes the rendering, "
                              + "if there is one", true);
                }
            } else {
                over = 0;
                if (link.stalled) {
                    link.stalled = false;
                    link.publish();
                }
            }
        }
    }

    // A mirror's own window: is it being drawn?  Asked only while the
    // group's renderer or this screen is marked stalled.  The SceneMirror is
    // asked for a sync (probeSync); a count that went up since the last check
    // means the window draws.  A window that does not is marked stalled, so
    // the election passes it over; one that does counts its checks in a row,
    // and with two may take a stalled renderer's place (Clone.mayTake).
    Timer {
        id: drawCheck
        interval: Math.max(20, link.probeMs)
        repeat: true
        running: link.role === "mirror" && link.key !== "" && link.mirror !== null
                 && (link.stalled || link._sourceStalled)
        onRunningChanged: {
            link._probeCount = -1;
            link._drawnRuns = 0;
        }
        onTriggered: {
            const item = link.mirror;
            if (!item || typeof item.probeSync !== "function") return;
            const count = item.probeSync();
            const first = link._probeCount < 0;
            const drawn = !first && count > link._probeCount;
            link._probeCount = count;
            if (first) return;
            link._drawnRuns = drawn ? link._drawnRuns + 1 : 0;
            if (link.stalled === drawn) {
                link.stalled = !drawn;
                link.publish();
            }
            link.follow();
        }
    }
}
