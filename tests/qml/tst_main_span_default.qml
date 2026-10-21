import QtQuick
import QtTest
import Helpers 1.0
import "_stubs/com/github/captsilver/wallpaperEngineKde/CloneGroupData.js" as CloneData

// SpanGroup off (the default): what main.qml builds, written out as lines of
// "WEK-SPAN-TREE <name> " and JSON, for we-manager's test_ph8b_span.py to
// compare between the tree with the span patch and the tree without it:
//   off     one screen, ShareRenderer off too: 3.26's tree;
//   render  the screen that renders, of two with ShareRenderer on;
//   mirror  the screen that shows its frame.
// Every object under main.qml is in each (the loaded backend's and the
// CloneLink's included), with its type, its properties and their values, its
// methods and signals.  The patched tree may differ only by the names the
// patch declares, at their defaults.  Run alone this checks that the trees
// can be written at all.
TestCase {
    id: tc
    name: "Main_SpanDefault"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }

    function _scenePath(id) {
        return "file:///tmp/fakelib/steamapps/workshop/content/431960/" + id + "/scene.pkg+scene";
    }
    function _typeOf(o) {
        return String(o).replace(/\(0x[0-9a-f]+.*$/, "").replace(/_QMLTYPE_\d+/, "_QMLTYPE").replace(/_QML_\d+/, "_QML");
    }
    function _value(v) {
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
            node.props[k] = _value(v);
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
    function _write(name, rig, mustHave) {
        const tree = _dump(rig.mainItem, [rig.mainItem]);
        const text = JSON.stringify(tree);
        verify(text.indexOf("get_opt_value") > 0, "main.qml's background is not in the tree");
        for (const word of mustHave) verify(text.indexOf(word) > 0, word + " is not in the tree '" + name + "'");
        console.info("WEK-SPAN-TREE " + name + " " + text);
    }
    function _rig(config) {
        failOnWarning(/wallpaper is not defined/);
        failOnWarning(/Unable to assign \[undefined\]/);
        failOnWarning(/TypeError/);
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: "111", WallpaperSource: _scenePath("111") };
        for (const k in config) c[k] = config[k];
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080), initialConfig: c });
        verify(rig !== null);
        return rig;
    }
    function _loader(rig) { return rig._find(rig.mainItem, o => typeof o.dropOutgoing === "function"); }
    function _mirror(rig) {
        const l = _loader(rig);
        return l && l.item ? rig._find(l.item, o => typeof o.mapToSource === "function") : null;
    }

    function test_1_the_default_tree_can_be_written_out() {
        const rig = _rig({});
        tryVerify(() => rig.mainItem !== null && rig.background() !== null && rig.player() !== null, 2000);
        rig.player().firstFrame();
        // Past main.qml's start-up timers (the 300 ms pause, the 200 ms source pause),
        // well before the next ones (2 s, 5 s): the same moment in both trees.
        wait(900);
        _write("off", rig, ["setAcceptMouse"]);
        rig.destroy();
        tryVerify(() => CloneData.all.length === 0, 2000);
    }

    function test_2_two_screens_that_share_a_renderer_can_be_written_out() {
        // ShareRenderer on, SpanGroup off: the clone patch's own arrangement.  The first
        // screen is made alone and renders; the second shows its frame.
        const a = _rig({ ShareRenderer: 1 });
        tryVerify(() => a.mainItem !== null && a.background() !== null && a.player() !== null, 3000);
        a.player().firstFrame();
        const b = _rig({ ShareRenderer: 1 });
        tryVerify(() => b.mainItem !== null && b.background() !== null && _mirror(b) !== null
                        && _mirror(b).frameKey === a.player().shareKey, 3000, "the second screen is not a mirror");
        _mirror(b).frameAspect = 16 / 9;
        _mirror(b).hasFrame = true;
        // Past the second screen's start-up timers too, and well before the first one's
        // next ones (2 s): the same moment in both trees.
        wait(600);
        _write("render", a, ["setAcceptMouse", "sourceCreated", "wek-frame-"]);
        _write("mirror", b, ["mapToSource", "mirrorProperties", "wek-frame-"]);
        a.destroy();
        b.destroy();
    }
}
