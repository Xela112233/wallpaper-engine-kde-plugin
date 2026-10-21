import QtQuick 2.5
import com.github.captsilver.wallpaperEngineKde 1.2
import ".."
import "../js/clone.mjs" as Clone
import "../js/span.mjs" as Span

// The backend of a screen that shows another screen's scene (we-manager clone
// patch, ShareRenderer; main.qml loads it only when CloneLink.claim() says
// so).  It draws the frame the rendering screen's SceneViewer publishes under
// `frameKey` and holds no renderer: no Vulkan device, no textures, no scripts
// and no sound.  The frame rate, the quality settings, the sound and the
// volume are the rendering screen's; this screen's alignment and background
// colour are its own.
//
// The frame is the rendering screen's picture.  Cover (crop) fills this
// screen with the middle of it, Fit shows all of it between bars of the
// background colour, Stretch fills the screen with all of it
// (Clone.mirrorPlacement).
//
// A pause on this screen (its own window rules, the user's pause) cannot
// pause the renderer while another screen of the group plays, and the
// renderer writes its images again in turn, so the mirror cannot simply hold
// the last one.  Paused, it shows a still of its own: one picture of the
// frame it was showing, taken by a ShaderEffectSource, after which the
// SceneMirror item is hidden and follows no frame until play().  The same
// when the renderer itself is paused.
//
// SpanGroup (we-manager span patch): the frame is the picture of a canvas
// that covers every screen of the group, `spanCanvas`.  This screen then
// shows the part of the frame that lies on it, whatever its own alignment
// (Span.mirrorPlacement); the alignment of the picture in the canvas is the
// rendering screen's.
//
// The pointer: main.qml's MouseGrabber sends this screen's pointer to
// getMouseTarget(), the SceneMirror item, which passes each event on to the
// rendering SceneViewer at the point of its item where the same part of the
// picture is (SharedFrame.hpp, MirrorPointToSource).
Item {
    id: mirrorItem
    // Set once a frame is on screen; main.qml keeps the previous backend
    // visible until then.
    property bool frameShown: false
    anchors.fill: parent
    // main.qml sets the wallpaper's file; the mirror shows the frame, not the file.
    property string source: ""
    // main.qml's CloneLink, and the frame to show: fixed for this item's
    // life.  Another source, or the same one loaded again, is another item.
    property var link: null
    property string frameKey: ""
    // SpanGroup: the canvas that frame was rendered for, in Plasma's screen
    // coordinates; like the frame, fixed for this item's life.  Empty (the
    // default): the frame is another screen's picture, not a canvas.  And
    // this screen's own rectangle, which CloneLink sets again when the
    // screen moves inside the same canvas.
    property rect spanCanvas
    property rect spanScreen
    property int displayMode: background.displayMode
    property bool pixelArt: background.pixelArt
    property bool _paused: false
    // Paused with a frame to keep: the still is wanted, and then taken.  Once
    // taken it stays until play(), also when the renderer's frame goes (the
    // renderer moved to another screen): the paused screen keeps its picture.
    readonly property bool _wantStill: _paused && (mirror.hasFrame || _stillTaken)
    property bool _stillTaken: false
    on_WantStillChanged: {
        if (_wantStill) still.scheduleUpdate();
        else _stillTaken = false;
    }

    SceneMirror {
        id: mirror
        frameKey: mirrorItem.frameKey
        // Live until the still is there (a paused mirror with no frame yet
        // waits for one, to have something to keep).
        live: !mirrorItem._stillTaken
        smooth: !mirrorItem.pixelArt
        readonly property var _place: mirrorItem.spanCanvas.width > 0
            ? Span.mirrorPlacement(mirrorItem.spanCanvas, mirrorItem.spanScreen, frameAspect,
                                   mirrorItem.width, mirrorItem.height)
            : Clone.mirrorPlacement(
            mirrorItem.displayMode === Common.DisplayMode.Aspect ? "fit"
                : (mirrorItem.displayMode === Common.DisplayMode.Crop ? "crop" : "stretch"),
            frameAspect, mirrorItem.width, mirrorItem.height)
        x: _place.x
        y: _place.y
        width: _place.width
        height: _place.height
        sourceRect: Qt.rect(_place.sx, _place.sy, _place.sw, _place.sh)

        onHasFrameChanged: {
            if (!hasFrame || mirrorItem.frameShown) return;
            mirrorItem.frameShown = true;
            if (mirrorItem.link) mirrorItem.link.mirrorShown();
            background.sig_backendFirstFrame('scene');
        }
        onFailed: function(reason) {
            if (mirrorItem.link) mirrorItem.link.mirrorFailed(reason);
        }
    }

    // The paused picture.  hideSource takes the SceneMirror out of the
    // window's own scene while this shows, so the renderer's image is not
    // drawn again; live: false makes it one picture, taken at scheduleUpdate().
    ShaderEffectSource {
        id: still
        x: mirror.x
        y: mirror.y
        width: mirror.width
        height: mirror.height
        sourceItem: mirrorItem._wantStill ? mirror : null
        visible: mirrorItem._wantStill
        live: false
        hideSource: true
        smooth: mirror.smooth
        onScheduledUpdateCompleted: if (mirrorItem._wantStill) mirrorItem._stillTaken = true
    }

    Component.onCompleted: {
        background.nowBackend = 'scene';
        if (mirrorItem.link) mirrorItem.link.mirrorCreated(mirror);
    }

    function play() { _paused = false; }
    function pause(now) { _paused = true; }
    // main.qml calls this when the backend becomes the outgoing layer of a
    // handoff: background.* then describes the next wallpaper.
    function freezeOptions() {
        displayMode = displayMode;
        pixelArt = pixelArt;
    }
    function getMouseTarget() {
        return Qt.binding(function() { return mirror; })
    }
}
