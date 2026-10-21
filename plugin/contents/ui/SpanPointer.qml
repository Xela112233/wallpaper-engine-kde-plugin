// The pointer of one screen of a scene stretched across screens (we-manager
// span patch, main.xml SpanGroup).  CloneLink.qml makes this, with
// Qt.createComponent and only for a screen of a group: once inside the
// backend it loads, on the rendering screen and on the others alike, and
// main.qml then gives it to its MouseGrabber as the pointer's target in the
// place of the backend's own.
//
// It is a SceneMirror the size of the screen that draws nothing (opacity 0)
// and is here for one thing a SceneMirror does: it passes the pointer events
// it is sent on to the rendering SceneViewer, at the point its source
// rectangle maps them to (SharedFrame.hpp MirrorPointToSource).  The source
// rectangle is js/span.mjs pointerRect: this screen's place in the frame,
// drawn towards the frame's middle by how much of the scene the frame shows.
// So the renderer, which reads a position as a fraction of its item and takes
// that for the same fraction of the scene, gets the scene's point that is
// under the pointer, with Cover too, where the canvas shows a band of the
// scene.  With Stretch and Keep aspect ratio the frame shows the whole scene
// and the point is the pointer's place in the frame.
//
// It follows no frame (live: false): it is synced when the frame comes or
// goes and when its rectangle changes, which is when it learns the rendering
// item's size, and it asks for one more sync a tenth of a second after the
// frame's shape changed and every two seconds, so that a size it read a
// moment too early does not stay.  It asks its window for no frame otherwise.
import QtQuick
import com.github.captsilver.wallpaperEngineKde
import "js/span.mjs" as Span

SceneMirror {
    id: pad
    anchors.fill: parent
    opacity: 0
    live: false
    // The canvas the frame was rendered for and this screen's rectangle, in
    // Plasma's screen coordinates; CloneLink sets `screen` again when the
    // screen moves.
    property rect canvas
    property rect screen
    // The frame's width over height (0 before it is known), and how much of
    // the scene the frame shows each way (span.mjs shownPart); CloneLink
    // binds or sets them.
    property real shape: 0
    property real partX: 1
    property real partY: 1
    readonly property var _rect: Span.pointerRect(pad.canvas, pad.screen, pad.shape, pad.partX, pad.partY)
    sourceRect: Qt.rect(_rect.x, _rect.y, _rect.width, _rect.height)
    onShapeChanged: soon.restart()
    Timer { id: soon; interval: 100; repeat: false; onTriggered: pad.probeSync() }
    Timer { interval: 2000; repeat: true; running: true; onTriggered: pad.probeSync() }
}
