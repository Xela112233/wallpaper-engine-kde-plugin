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
//
// SpanGroup (we-manager span patch; main.xml SpanGroup, 0 off): the screens of
// a group that show the same scene show one picture, each its own part.  The
// link is then made for SpanGroup alone too, and everything above holds for
// the group (its key carries the group's number, so it never mixes with
// screens that only share a renderer).  What is added, all behind `span > 0`
// (js/span.mjs has the maths):
//  * every screen's entry carries its rectangle in Plasma's screen
//    coordinates; the canvas is the rectangle around the group's screens;
//  * the screen that renders puts its SceneViewer into a SpanCanvas.qml the
//    size of the canvas (_spread), so the renderer draws the whole canvas and
//    the window shows this screen's part; its entry names the canvas;
//  * a screen that shows the frame is handed that canvas (mirrorProperties)
//    and draws the part of the frame that lies on it;
//  * a canvas that is no longer the group's (a screen came, went or moved) is
//    rendered again: follow() loads the scene again, as a reload;
//  * a screen that is stopped keeps its place in the canvas (_place), so the
//    picture on the other screens does not move when one of them stops;
//  * the pointer of every screen of the group goes through a SpanPointer.qml
//    (_point, pointerPad), which hands the renderer the scene's point under
//    the pointer; the rendering screen's entry says how much of the scene
//    its frame shows (_shown);
//  * the table is followed spanFollowDelayMs after a change, not
//    followDelayMs: every change of the group's screens is a scene load, so
//    screens that come up one after the other (a login, a wake, we-manager
//    writing the group) are waited for.
import QtQuick
import com.github.captsilver.wallpaperEngineKde
import "js/clone.mjs" as Clone
import "js/span.mjs" as Span

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
    // SpanGroup: the group this screen stretches a scene across with the
    // others of the same number; 0, it does not.
    readonly property int span: host !== null ? (Number(host.spanGroup) || 0) : 0
    // This screen's rectangle in Plasma's screen coordinates (logical
    // pixels), as Plasma gives it to the wallpaper; read only for a group.
    readonly property rect screenRect: {
        if (link.span <= 0 || link.host === null) return Qt.rect(0, 0, 0, 0);
        const g = wallpaper.parent ? wallpaper.parent.screenGeometry : undefined;
        if (g && g.width > 0 && g.height > 0) return Qt.rect(g.x, g.y, g.width, g.height);
        return Qt.rect(link.host.Screen.virtualX, link.host.Screen.virtualY, link.host.width, link.host.height);
    }
    // The canvas this screen's renderer was made for, while it renders for a
    // group; set with the renderer (_spread) and fixed for its life.
    property rect canvas
    // A stopped screen's place: the key of the group whose canvas it stays
    // part of ("" for none).
    property string _place: ""
    // The SpanCanvas of the renderer this screen loaded last.
    property var    _holder: null
    // The SpanPointer of the backend this screen loaded last: main.qml's
    // MouseGrabber sends the pointer to it (null: to the backend's own item).
    property var    pointerPad: null
    // How much of the scene the frame this screen renders shows, each way
    // (Span.shownPart); null while it renders for no group.
    readonly property var _shown: link.span > 0 && link.player !== null
        ? Span.shownPart(link.player.fillMode === SceneViewer.ASPECTCROP, link.player.nativeAspectRatio,
                         link.player.width, link.player.height)
        : null
    on_ShownChanged: if (link.span > 0 && link.role === "source") publish()
    // No canvas could be made for want of a rectangle (_spread): tried again
    // when the table has one.
    property bool   _noCanvas: false

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
    // SpanGroup: the same wait for a screen of a group.  Longer, because
    // there every screen that comes or goes changes the canvas, which is a
    // scene load: screens that come up within this of each other (a login, a
    // wake, we-manager's two writes) cost one load, not one each.
    property int spanFollowDelayMs: 1000
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
            group.member = link._place !== "" ? Span.placeholder(link._place, link.screenName, link.screenRect) : ({});
            return;
        }
        const entry = {
            key: link.key, screen: link.screenName, prefer: link.prefer, area: link.area, role: link.role,
            frameKey: link.role === "source" ? link.frameKey : "", playing: link.playing, stalled: link.stalled
        };
        // SpanGroup: where this screen is, and the canvas its renderer draws.
        group.member = link.span <= 0 ? entry
            : Span.entry(entry, link.span, link.screenRect,
                         link.role === "source" && link.frameKey !== "" ? link.canvas : null, link._shown);
    }
    // The group of what this screen shows: the scene and its properties,
    // and, stretched across screens, the SpanGroup's number.
    function _groupKey(path, props) {
        const key = Clone.groupKey(path, props);
        return link.span > 0 ? Span.groupKey(link.span, key) : key;
    }
    // SpanGroup: this screen moved (Plasma arranged the screens anew).  The
    // table hears of it, and what this screen shows now is told its place:
    // if the canvas is still the same rectangle, nothing is loaded again.
    onScreenRectChanged: {
        if (link.span <= 0 || !(link.screenRect.width > 0)) return;
        publish();
        if (link.role === "source" && link._holder) link._holder.screen = link.screenRect;
        if (link.pointerPad) link.pointerPad.screen = link.screenRect;
        const shown = link.loader ? link.loader.item : null;
        if (link.role === "mirror" && shown && shown.spanCanvas !== undefined && shown.spanCanvas.width > 0)
            shown.spanScreen = link.screenRect;
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
        // SpanGroup: a stopped screen's place ends when it loads again.
        if (link._place !== "") link._dropPlace();
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
        const key = link._groupKey(link.host.wallpaperPath, props);
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
        const props = { link: link, frameKey: link.frameKey };
        // SpanGroup: the canvas that frame was rendered for; this screen
        // shows the part of it that lies on it (backend/SceneMirror.qml).
        if (link.span > 0) {
            const c = Span.canvasOf(group.members(), link.frameKey);
            if (c !== null) {
                props.spanCanvas = Qt.rect(c.x, c.y, c.width, c.height);
                props.spanScreen = link.screenRect;
            }
        }
        return props;
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
        if (link.span > 0) link._spread(viewer);
        link.publish();
    }
    // SpanGroup: the viewer just made renders the group's whole canvas.  It
    // goes into a SpanCanvas.qml of the canvas's size, placed so that this
    // screen's window shows its own part; backend/Scene.qml sizes the viewer
    // to its parent, and the renderer makes its target from the viewer's size
    // times the window's scale factor times RenderScale, as for one screen.
    // A canvas beyond Span.maxSide pixels a side gets a smaller render scale.
    // If the canvas cannot be made, the group goes on as screens that share a
    // renderer: each shows the rendering screen's picture.
    function _spread(viewer) {
        link.canvas = Qt.rect(0, 0, 0, 0);
        link._holder = null;
        link.pointerPad = null;
        link._noCanvas = false;
        const u = Span.union(group.members(), link.key);
        const inside = viewer.parent;
        if (u === null || !inside) {
            // Plasma gave no screen of the group a rectangle (yet).
            link._noCanvas = true;
            link._unspread("no screen of the group has a rectangle");
            return;
        }
        const comp = Qt.createComponent("SpanCanvas.qml");
        const holder = comp.status === Component.Ready
            ? comp.createObject(inside, { canvas: Qt.rect(u.x, u.y, u.width, u.height), screen: link.screenRect })
            : null;
        if (!holder) {
            link._unspread(comp.errorString().trim() || "SpanCanvas.qml made no item");
            return;
        }
        viewer.parent = holder;
        // The canvas is cut off at the backend's own item, which is the
        // screen's: main.qml's transition moves that item (slide) or scales
        // it (zoom), and what leaves the window must be this screen's part,
        // not a canvas that still reaches across the window.
        inside.clip = true;
        const dpr = link.host.Screen.devicePixelRatio || 1;
        const w = holder.width > 0 ? holder.width : u.width, h = holder.height > 0 ? holder.height : u.height;
        const scale = Span.renderScale(link.host.renderScale, w, h, dpr);
        if (scale !== link.host.renderScale) Common.setIfSupported(viewer, "SceneViewer", "renderScale", scale);
        link.canvas = holder.canvas;
        link._holder = holder;
        link._point(inside, viewer, null);
        const px = Span.renderSize(w, h, dpr, scale);
        link._say("span " + link.span + ": " + link._where() + " renders " + link._what() + " on a canvas of "
                  + Math.round(u.width) + "x" + Math.round(u.height) + " at " + Math.round(u.x) + ","
                  + Math.round(u.y) + ", about " + px.width + "x" + px.height + " pixels"
                  + (scale !== link.host.renderScale
                     ? " (scaled down for the limit of " + Span.maxSide + " pixels a side)" : ""));
    }
    // SpanGroup: the canvas could not be laid out.  One line says so, and
    // the group goes on as screens that share a renderer.
    function _unspread(why) {
        link.lastLine = "[WEK] span: " + link._where() + " could not lay out the canvas of group " + link.span
                        + " for " + link._what() + " (" + why + "): the screens of the group each show the "
                        + "rendering screen's whole picture, not their part of one";
        console.warn(link.lastLine);
    }
    // SpanGroup: this screen's pointer goes through a SpanPointer.qml made
    // inside the backend just loaded (`inside`): for the screen that renders,
    // with its `viewer`; for one that shows the frame, with its `mirror`
    // item.  Without one (it could not be made) the pointer goes to the
    // backend's own item, as without a group.
    function _point(inside, viewer, mirror) {
        link.pointerPad = null;
        const comp = Qt.createComponent("SpanPointer.qml");
        const canvas = viewer ? link.canvas : inside.spanCanvas;
        const pad = comp.status === Component.Ready
            ? comp.createObject(inside, { frameKey: link.frameKey, canvas: canvas, screen: link.screenRect }) : null;
        if (!pad) {
            console.warn("[WEK] span: " + link._where() + " could not make the pointer's item ("
                         + comp.errorString().trim() + "): the scene's cursor is not under the pointer "
                         + "where the canvas shows a part of the scene only");
            return;
        }
        if (viewer) {
            pad.shape = Qt.binding(function() { return viewer.height > 0 ? viewer.width / viewer.height : 0; });
            pad.partX = Qt.binding(function() { return link._shown ? link._shown.kx : 1; });
            pad.partY = Qt.binding(function() { return link._shown ? link._shown.ky : 1; });
        } else {
            pad.shape = Qt.binding(function() { return mirror.frameAspect; });
            const k = Span.shownOf(group.members(), link.frameKey);
            pad.partX = k.kx;
            pad.partY = k.ky;
        }
        link.pointerPad = pad;
    }
    // SpanGroup, when the table changed: a screen that shows the frame learns
    // how much of the scene the frame shows.
    function _spanFollowed() {
        if (link.role !== "mirror" || !link.pointerPad) return;
        const k = Span.shownOf(group.members(), link.frameKey);
        link.pointerPad.partX = k.kx;
        link.pointerPad.partY = k.ky;
    }
    // SpanGroup: the canvas this screen renders is not the group's any more:
    // a screen of it came, went or moved.
    function _canvasStale() {
        if (link.span <= 0 || link.role !== "source" || link.player === null) return false;
        const u = Span.union(group.members(), link.key);
        if (!(link.canvas.width > 0)) return link._noCanvas && u !== null;
        return u !== null && !Span.sameRect(u, link.canvas);
    }
    // SpanGroup: a stopped screen keeps its place in the canvas of `key`
    // until it loads again, is given another wallpaper while stopped, or is
    // taken out of the group.
    function _keepPlace(key) {
        link._place = key;
        if (link.host === null) return;
        link.host.sourceChanged.connect(link, link._dropPlace);
        link.host.userPropsJsonChanged.connect(link, link._dropPlace);
    }
    function _dropPlace() {
        if (link._place === "") return;
        link._place = "";
        if (link.host !== null) {
            link.host.sourceChanged.disconnect(link, link._dropPlace);
            link.host.userPropsJsonChanged.disconnect(link, link._dropPlace);
        }
        if (link.key === "") link.publish();
    }
    onSpanChanged: {
        _dropPlace();
        if (link.span <= 0) {
            link.canvas = Qt.rect(0, 0, 0, 0);
            link._holder = null;
            link.pointerPad = null;
            link._noCanvas = false;
        }
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
        // SpanGroup: the pointer of a screen that shows its part of a canvas.
        link.pointerPad = null;
        if (link.span > 0 && item && item.parent && item.parent.spanCanvas !== undefined
            && item.parent.spanCanvas.width > 0)
            link._point(item.parent, null, item);
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
        // SpanGroup: a screen that is stopped (its backend unloaded until the
        // reason ends) keeps its place in the group's canvas, so the other
        // screens' parts stay where they are.
        if (link.span > 0 && link.key !== "" && link.host !== null && link.host.stopped === true)
            link._keepPlace(link.key);
        if (link.pointerPad) link.pointerPad = null;
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
        // SpanGroup: the group's screens changed; the canvas is rendered anew.
        if (link.span > 0) {
            link._spanFollowed();
            if (link._canvasStale()) {
                link._reload();
                return;
            }
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
        interval: Math.max(0, link.span > 0 ? link.spanFollowDelayMs : link.followDelayMs)
        repeat: false
        onTriggered: link.follow()
    }
    Connections {
        target: group
        function onChanged() {
            if (link.key === "") return;
            link._watchSource();
            // SpanGroup: what the frame shows of its scene, for the pointer,
            // at once (the rest is followed after the wait).
            if (link.span > 0) link._spanFollowed();
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
        const key = link._groupKey(link.host.wallpaperPath, link.host.userPropsJson);
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
