// The canvas of a scene stretched across screens (we-manager span patch,
// main.xml SpanGroup), on the screen that renders it.  CloneLink.qml makes
// this, with Qt.createComponent and only for a screen of a group that
// renders: inside the backend/Scene.qml it has just been told of, and puts
// that backend's SceneViewer into it.
//
// The item is the size of the group's whole canvas, in this screen's logical
// pixels, and lies where the canvas lies as seen from this screen (left of
// and above the window for a screen in the middle).  Scene.qml sizes its
// SceneViewer to its parent, so the viewer now fills the canvas; the
// renderer makes its target from the viewer's size, as for one screen, so it
// renders the canvas; and this screen's window shows the part of the item
// that is inside it.  The other screens of the group show their parts of the
// same frame (backend/SceneMirror.qml, js/span.mjs mirrorPlacement).
//
// The item is cut off at the backend's own item (CloneLink sets that item's
// clip), so that a transition, which moves or scales the backend, moves this
// screen's part and not the canvas beside it.
//
// The pointer does not come through here: every screen of a group, this one
// too, has a SpanPointer.qml for it.
//
// `canvas` is fixed for the life of the renderer: a canvas that changes (a
// screen of the group came, went or moved) is another renderer, which
// CloneLink loads.  `screen` is this screen's own rectangle; CloneLink sets
// it again when the screen moves inside the same canvas, for the renderer
// it loaded last (one on its way out keeps what it had).
import QtQuick
import "js/span.mjs" as Span

Item {
    id: holder
    // Both in Plasma's screen coordinates.
    property rect canvas
    property rect screen
    readonly property var _place: Span.sourcePlacement(
        holder.canvas, holder.screen,
        holder.parent ? holder.parent.width : 0, holder.parent ? holder.parent.height : 0)
    x: _place.x
    y: _place.y
    width: _place.width
    height: _place.height
    // Under the backend's own overlays (Scene.qml's video-decode message).
    z: -1
}
