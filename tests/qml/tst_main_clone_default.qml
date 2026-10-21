import QtQuick
import QtTest
import Helpers 1.0

// ShareRenderer off (the default): what main.qml builds for a scene, written
// out as one line, "WEK-TREE " and JSON.  we-manager's test_ph_clone.py runs
// this file in the tree with the clone patch and in the tree without it and
// compares the two: every object under main.qml (the loaded backend's
// included), with its type, its properties and their values, its methods and
// signals.  The patched tree may differ only by what the patch declares on
// main.qml's background, at its default.  Run alone it checks that the dump
// can be made at all.
TestCase {
    id: tc
    name: "Main_CloneDefault"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }

    function _scenePath(id) {
        return "file:///tmp/fakelib/steamapps/workshop/content/431960/" + id + "/scene.pkg+scene";
    }
    function _typeOf(o) {
        // "Scene_QMLTYPE_12(0x55d0)" and "QQuickTimer(0x55d0)": the type, without the
        // numbers that differ from run to run.
        return String(o).replace(/\(0x[0-9a-f]+.*$/, "").replace(/_QMLTYPE_\d+/, "_QMLTYPE").replace(/_QML_\d+/, "_QML");
    }
    function _value(v, seen) {
        if (v === null) return null;
        if (v === undefined) return "undefined";
        const t = typeof v;
        if (t === "function") return "function";
        if (t === "number") return isFinite(v) ? v : String(v);
        if (t === "boolean" || t === "string") return v;
        if (t === "object" && typeof v.destroy === "function" && typeof v.toString === "function"
            && String(v).indexOf("(0x") > 0)
            return "object:" + _typeOf(v);
        try {
            const s = JSON.stringify(v);
            return s === undefined ? String(v) : "json:" + s;
        } catch (e) {
            return "value:" + String(v).replace(/0x[0-9a-f]+/g, "0x");
        }
    }
    function _dump(o, seen) {
        const node = { type: _typeOf(o), props: {}, children: [] };
        const names = [];
        for (const k in o) names.push(k);
        names.sort();
        for (const k of names) {
            if (k === "parent" || k === "data" || k === "resources" || k === "children") continue;
            let v;
            try { v = o[k]; } catch (e) { v = "error:" + e; }
            node.props[k] = _value(v, seen);
        }
        const kids = [];
        for (const list of [o.data, o.resources, o.children]) {
            if (!list) continue;
            for (let i = 0; i < list.length; i++) {
                const c = list[i];
                if (!c || seen.indexOf(c) >= 0) continue;
                seen.push(c);
                kids.push(c);
            }
        }
        for (const c of kids) node.children.push(_dump(c, seen));
        return node;
    }

    function test_the_default_tree_can_be_written_out() {
        failOnWarning(/wallpaper is not defined/);
        failOnWarning(/Unable to assign \[undefined\]/);
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080), initialConfig: {
            SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: "111", WallpaperSource: _scenePath("111") } });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null && rig.player() !== null, 2000);
        rig.player().firstFrame();
        // Past main.qml's start-up timers (the 300 ms pause, the 200 ms source pause),
        // well before the next ones (2 s, 5 s): the same moment in both trees.
        wait(900);
        const tree = _dump(rig.mainItem, [rig.mainItem]);
        verify(tree.children.length > 0);
        const text = JSON.stringify(tree);
        verify(text.indexOf("get_opt_value") > 0, "main.qml's background is not in the dump");
        verify(text.indexOf("setAcceptMouse") > 0, "the scene backend's viewer is not in the dump");
        console.info("WEK-TREE " + text);
        rig.destroy();
    }
}
