// Test stub — see tests/qml/_stubs/README.md for contract.
// Real source: src/KeyGrabber.hpp + src/KeyGrabber.cpp (we-manager input patch)
// Last contract review: 2026-11-06

import QtQuick
Item {
    property var target: null
    property bool active: false
    signal exitRequested()
}
