// Stand-in for plasma-desktop's containments/desktop FolderViewLayer.qml, the
// layer that draws the desktop's icons (a FocusScope there).  main.qml's icon
// opacity finds it by the type name at the start of toString().
import QtQuick

FocusScope {
    anchors.fill: parent
}
