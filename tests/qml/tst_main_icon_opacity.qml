import QtQuick
import QtTest
import Helpers 1.0

// Desktop icon opacity (we-manager icon-opacity patch): main.qml finds Folder
// View's icon layer (FolderViewLayer) in the window's item tree and binds its
// opacity to IconOpacity / 100, leaving the widgets beside it alone, and puts
// it back when the setting returns to 100 or the wallpaper goes.
TestCase {
    id: tc
    name: "Main_IconOpacity"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }
    // The desktop containment's shape: AppletsLayout holding the icon layer's
    // loader (the icon layer inside it) and a widget.
    Component {
        id: desktopComp
        AppletsLayout {
            property alias iconLoader: iconLoader
            property alias icons: icons
            property alias widget: widget
            FolderViewLayerLoader { id: iconLoader; FolderViewLayer { id: icons } }
            Item { id: widget }
        }
    }
    Component { id: layerComp; FolderViewLayer {} }

    // destroy() is deferred: out of the item tree first, so the next test's
    // search does not find this one.
    function _drop(item) {
        item.parent = null;
        item.destroy();
    }
    function _rig(config) {
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 400, 300),
                                               initialConfig: config || {} });
        tryVerify(() => rig.background() !== null, 2000);
        return rig;
    }

    function test_the_icons_follow_the_setting_and_the_widgets_do_not() {
        const desk = desktopComp.createObject(tc);
        const rig = _rig();
        const bg = rig.background();
        wait(50);
        compare(bg.iconLayer, null, "at 100 nothing is looked for");
        compare(desk.icons.opacity, 1);

        rig.setConfig({ IconOpacity: 40 });
        tryCompare(bg, "iconLayer", desk.icons, 3000);
        tryVerify(() => Math.abs(desk.icons.opacity - 0.4) < 1e-6, 2000);
        compare(desk.iconLoader.opacity, 1, "the icon layer, not its loader");
        compare(desk.widget.opacity, 1, "widgets keep their opacity");
        compare(desk.opacity, 1);

        rig.setConfig({ IconOpacity: 75 });
        verify(Math.abs(desk.icons.opacity - 0.75) < 1e-6);
        rig.setConfig({ IconOpacity: 100 });
        compare(desk.icons.opacity, 1, "back to 100 puts the old value back");
        _drop(rig);
        _drop(desk);
    }

    function test_the_wallpaper_going_puts_the_icons_back() {
        const desk = desktopComp.createObject(tc);
        const rig = _rig({ IconOpacity: 20 });
        tryVerify(() => Math.abs(desk.icons.opacity - 0.2) < 1e-6, 3000);
        rig.destroy();
        tryCompare(desk.icons, "opacity", 1, 2000);
        _drop(desk);
    }

    function test_a_layer_that_loads_later_is_found() {
        const rig = _rig({ IconOpacity: 50 });
        const bg = rig.background();
        wait(100);
        compare(bg.iconLayer, null);
        const layer = layerComp.createObject(tc);   // Folder View came up after the wallpaper
        tryCompare(bg, "iconLayer", layer, 5000);
        tryVerify(() => Math.abs(layer.opacity - 0.5) < 1e-6, 2000);
        _drop(rig);
        _drop(layer);
    }

    function test_out_of_range_values_are_clamped() {
        const desk = desktopComp.createObject(tc);
        const rig = _rig({ IconOpacity: 150 });
        const bg = rig.background();
        compare(bg.iconOpacity, 100);
        wait(50);
        compare(desk.icons.opacity, 1);
        rig.setConfig({ IconOpacity: -5 });
        compare(bg.iconOpacity, 0);
        tryCompare(desk.icons, "opacity", 0, 3000);
        _drop(rig);
        _drop(desk);
    }
}
