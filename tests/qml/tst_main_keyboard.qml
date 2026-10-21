import QtQuick
import QtTest
import Helpers 1.0

// Keyboard mode for web wallpapers (we-manager input patch): the shortcut's
// D-Bus ToggleKeyboard (the WekControl stub's toggleKeyboardAt) starts it on
// one screen, the best keyboardScore; main.qml then makes a KeyGrabber on
// the desktop's AppletsLayout aimed at the web page's input item, while the
// screen's KeyboardInput is on and its wallpaper is a web one.  The next
// press, or the mode ending on that screen, frees it again.
TestCase {
    id: tc
    name: "Main_Keyboard"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }
    Component { id: appletsComp; AppletsLayout {} }
    // What main.qml asks of a backend: only web pages have getKeyboardTarget.
    Component {
        id: fakeWebComp
        Item {
            property Item input: Item {}
            function getKeyboardTarget() { return input; }
        }
    }
    Component { id: fakeSceneComp; Item {} }

    function _loader(rig) {
        return rig._find(rig.mainItem, o => typeof o.dropOutgoing === "function");
    }
    // destroy() is deferred: out of the item tree first, so the next test's
    // Common.findItem does not find this one.
    function _drop(item) {
        item.parent = null;
        item.destroy();
    }
    // Under the window's root item: TestCase itself is not visible, so a
    // rig inside it never shows anything.
    function _rig(config, geometry) {
        const rig = rigComp.createObject(tc.parent, { screenGeometry: geometry || Qt.rect(0, 0, 400, 300),
                                                      initialConfig: config || {} });
        tryVerify(() => rig.background() !== null, 2000);
        return rig;
    }
    // The shortcut: WekControl.ToggleKeyboard with the pointer at `cursor`.
    function _toggle(rig, cursor) {
        const control = rig._find(rig.mainItem, o => typeof o.toggleKeyboardAt === "function");
        verify(control !== null);
        control.toggleKeyboardAt(cursor || null);
    }

    function test_the_shortcut_needs_keyboard_input_and_a_web_wallpaper() {
        const applets = appletsComp.createObject(tc);
        const rig = _rig();
        const bg = rig.background();
        const loader = _loader(rig);
        loader.item = fakeWebComp.createObject(loader);
        _toggle(rig);
        compare(bg.keyboardMode, false, "KeyboardInput is off by default");
        compare(bg.keyHooker, null, "no grabber is made while the mode cannot start");

        rig.setConfig({ KeyboardInput: true });
        loader.item = fakeSceneComp.createObject(loader);
        _toggle(rig);
        compare(bg.keyboardMode, false, "a scene does not take the keyboard");

        const web = fakeWebComp.createObject(loader);
        loader.item = web;
        _toggle(rig);
        compare(bg.keyboardMode, true);
        verify(bg.keyHooker !== null);
        compare(bg.keyHooker.parent, applets, "the grabber sits on the AppletsLayout");
        compare(bg.keyHooker.target, web.input);
        compare(bg.keyHooker.active, true);

        _toggle(rig);
        compare(bg.keyboardMode, false, "the shortcut again gives the keys back");
        compare(bg.keyHooker.active, false);
        _drop(rig);
        _drop(applets);
    }

    function test_escape_another_wallpaper_or_the_setting_end_the_mode() {
        const applets = appletsComp.createObject(tc);
        const rig = _rig({ KeyboardInput: true });
        const bg = rig.background();
        const loader = _loader(rig);
        loader.item = fakeWebComp.createObject(loader);

        _toggle(rig);
        compare(bg.keyboardMode, true);
        bg.keyHooker.exitRequested();           // Escape, or a click on an icon
        compare(bg.keyboardMode, false);

        _toggle(rig);
        compare(bg.keyboardMode, true);
        loader.loaded();                         // another wallpaper
        compare(bg.keyboardMode, false);

        _toggle(rig);
        compare(bg.keyboardMode, true);
        rig.setConfig({ KeyboardInput: false });
        compare(bg.keyboardMode, false);

        _drop(rig);
        _drop(applets);
    }

    function test_the_note_shows_while_the_keys_go_to_the_page() {
        const applets = appletsComp.createObject(tc);
        const rig = _rig({ KeyboardInput: true });
        const bg = rig.background();
        const loader = _loader(rig);
        loader.item = fakeWebComp.createObject(loader);
        const note = rig._find(rig.mainItem, o => o.text !== undefined && String(o.text).indexOf("Esc") >= 0);
        verify(note !== null);
        compare(note.parent.visible, false);
        _toggle(rig);
        compare(note.parent.visible, true);
        _drop(rig);
        _drop(applets);
    }

    function test_without_the_desktop_area_the_keys_stay_with_the_desktop() {
        const rig = _rig({ KeyboardInput: true });
        const bg = rig.background();
        const loader = _loader(rig);
        loader.item = fakeWebComp.createObject(loader);
        ignoreWarning(/keyboard mode: Common\.findItem\(Window\.contentItem, "AppletsLayout"\)/);
        _toggle(rig);
        compare(bg.keyboardMode, false);
        compare(bg.keyHooker, null);
        // Not left as the keyboard's screen: the next press tries again.
        ignoreWarning(/keyboard mode: Common\.findItem\(Window\.contentItem, "AppletsLayout"\)/);
        _toggle(rig);
        compare(bg.keyboardMode, false);
        _drop(rig);
    }

    // Two monitors, both with web wallpapers and KeyboardInput on: one
    // screen at a time, the one under the pointer; Escape on it frees the
    // keyboard, so the next press starts it again instead of ending it.
    function test_two_screens_take_the_keyboard_one_at_a_time() {
        const applets = appletsComp.createObject(tc);
        const left = _rig({ KeyboardInput: true }, Qt.rect(0, 0, 400, 300));
        const right = _rig({ KeyboardInput: true }, Qt.rect(400, 0, 400, 300));
        for (const r of [left, right]) {
            const loader = _loader(r);
            loader.item = fakeWebComp.createObject(loader);
        }
        const a = left.background(), b = right.background();

        _toggle(left, Qt.point(500, 20));        // the pointer over the right screen
        compare(b.keyboardMode, true);
        compare(a.keyboardMode, false, "only one screen takes the keyboard");
        _toggle(left, Qt.point(500, 20));        // the shortcut again
        compare(b.keyboardMode, false);
        compare(a.keyboardMode, false);

        _toggle(right, Qt.point(30, 20));        // either screen's control: the left one now
        compare(a.keyboardMode, true);
        compare(b.keyboardMode, false);
        a.keyHooker.exitRequested();             // Escape on the left screen
        compare(a.keyboardMode, false);
        _toggle(left, Qt.point(500, 20));        // starts again, on the right
        compare(b.keyboardMode, true);
        compare(a.keyboardMode, false);
        _toggle(left, Qt.point(500, 20));
        compare(b.keyboardMode, false);

        // A screen that cannot take it is passed over, wherever the pointer is.
        right.setConfig({ KeyboardInput: false });
        _toggle(left, Qt.point(500, 20));
        compare(a.keyboardMode, true);
        compare(b.keyboardMode, false);
        _toggle(left);
        compare(a.keyboardMode, false);
        _drop(left);
        _drop(right);
        _drop(applets);
    }
}
