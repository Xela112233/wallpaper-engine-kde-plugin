// Test stub — see tests/qml/_stubs/README.md for contract.
// Real source: src/CloneGroup.hpp + src/CloneGroup.cpp (we-manager clone patch)
// Last contract review: 2026-11-12

// One screen's entry in the table every screen writes to.  Like the C++
// type it keeps no rule: member, members(), newFrameKey() and changed(),
// which reaches every stub of the engine on the next turn of the event loop,
// once for all the changes of a turn.  The table lives in CloneGroupData.js
// (.pragma library), as the C++ one is process-wide.
import QtQuick
import "CloneGroupData.js" as Data
QtObject {
    id: stub
    readonly property int id: Data.nextId()
    property var member: ({})
    signal changed()

    property bool _listed: false
    property var _notifier: Timer {
        interval: 0
        repeat: false
        onTriggered: stub.changed()
    }
    function _notifyAll() {
        for (const g of Data.all.slice()) g._notifier.restart();
    }
    Component.onCompleted: {
        Data.all.push(stub);
        _listed = true;
    }
    // As the C++ setter: an entry written again unchanged tells no one.
    property string _last: "{}"
    onMemberChanged: {
        const now = JSON.stringify(member || {});
        if (now === _last) return;
        _last = now;
        if (_listed) _notifyAll();
    }
    function members() {
        const out = [];
        for (const g of Data.all) {
            const m = g.member;
            if (!m || Object.keys(m).length === 0) continue;
            const entry = { id: g.id };
            for (const k in m) entry[k] = m[k];
            out.push(entry);
        }
        return out;
    }
    function newFrameKey() { return "wek-frame-" + stub.id + "-" + Data.nextFrame(); }
    Component.onDestruction: {
        const i = Data.all.indexOf(stub);
        if (i >= 0) Data.all.splice(i, 1);
        const had = member && Object.keys(member).length > 0;
        if (had) for (const g of Data.all.slice()) g._notifier.restart();
    }
}
