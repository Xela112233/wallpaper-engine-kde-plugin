// Test stub — see tests/qml/_stubs/README.md for contract.
// Real source: src/EffectsWatcher.hpp + src/EffectsWatcher.cpp (we-manager unpause-overview patch)
// Last contract review: 2026-11-11

// Whether KWin shows Overview or Present Windows.  Tests set overviewActive
// (and available, reason) themselves; WindowModel binds watching, which the
// tests read to see when KWin would be asked.
import QtQuick
QtObject {
    property bool   watching: false
    property bool   overviewActive: false
    property bool   available: true
    property string reason: ""
}
