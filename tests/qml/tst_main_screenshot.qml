import QtQuick
import QtTest
import Helpers 1.0

// WekControl.Screenshot through the REAL main.qml (ScreenRig): the
// PlaylistController takes the call for its own screen (or every screen,
// each adding its name before the extension), and main.qml hands it to the
// backend's own writer, which for a scene is SceneViewer.saveFrame.
TestCase {
    id: tc
    name: "Main_Screenshot"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }

    function _scenePath(id) {
        return "file:///tmp/fakelib/steamapps/workshop/content/431960/" + id + "/scene.pkg+scene";
    }

    function _rig(id) {
        failOnWarning(/wallpaper is not defined/);
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080) });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        if (id) {
            rig.setConfig({ SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: id,
                            WallpaperSource: _scenePath(id) });
            tryVerify(() => rig.player() !== null, 2000, "scene never mounted");
        }
        return rig;
    }

    function test_screenshot_of_this_screen_saves_the_scene_frame() {
        const rig = _rig("111");
        const ctrl = rig.playlistController();
        verify(ctrl !== null);
        compare(typeof ctrl.screenshot, "function");
        compare(ctrl.screenName, rig.background().Screen.name);   // bound to the window's screen
        ctrl.screenName = "DP-1";              // offscreen screens have no name
        verify(ctrl.screenshot("/tmp/shots/one.png", "DP-1"));
        compare(rig.player().savedFrames, ["/tmp/shots/one.png"]);
        rig.destroy();
    }

    function test_every_screen_adds_its_name_and_other_screens_are_left_alone() {
        const rig = _rig("111");
        const ctrl = rig.playlistController();
        ctrl.screenName = "HDMI-A-1";
        verify(ctrl.screenshot("/tmp/shots/all.png", ""));
        compare(rig.player().savedFrames, ["/tmp/shots/all-HDMI-A-1.png"]);
        compare(ctrl.screenshot("/tmp/shots/x.png", "DP-1"), false);
        compare(rig.player().savedFrames.length, 1);
        rig.destroy();
    }

    function test_path_for_names_without_extension_and_odd_screens() {
        const rig = _rig("");
        const ctrl = rig.playlistController();
        compare(ctrl.screenshotPathFor("/a.b/shot", "DP-1"), "/a.b/shot-DP-1");
        compare(ctrl.screenshotPathFor("/x/shot.jpg", "HDMI A/1"), "/x/shot-HDMI_A_1.jpg");
        compare(ctrl.screenshotPathFor("/x/shot.png", ""), "/x/shot-screen.png");
        rig.destroy();
    }

    function test_nothing_loaded_writes_nothing_and_says_so() {
        const rig = _rig("");
        ignoreWarning(/screenshot: no wallpaper is shown/);
        compare(rig.background().saveWallpaperScreenshot("/tmp/shots/none.png"), false);
        // Told through the notifier too: a global shortcut has no other way to say it.
        compare(rig.notifier().lastEvent, "screenshotFailed");
        compare(rig.notifier().lastArgs[0], "/tmp/shots/none.png");
        verify(String(rig.notifier().lastArgs[1]).indexOf("no wallpaper is shown") >= 0);
        rig.destroy();
    }

    // The scene's own writer, with a stand-in for main.qml's grab.
    function _scene(rig) {
        return rig._find(rig.mainItem, o => typeof o.saveScreenshot === "function" && typeof o.freezeOptions === "function");
    }

    // Defaults (PauseMode Max, MaximizedAction Pause) pause the scene behind a
    // maximized window, and a paused scene draws no frame: the shortcut must
    // still give the frame on screen, which the player item keeps showing.
    function test_a_paused_scene_gives_the_frame_on_screen() {
        const rig = _rig("111");
        const scene = _scene(rig);
        verify(scene !== null);
        scene.pause();
        const grabs = [];
        verify(scene.saveScreenshot("/tmp/shots/paused.png", function(item, path) { grabs.push([item, path]); return true; }));
        compare(rig.player().savedFrames, [], "a paused renderer is not asked for a frame it will not draw");
        compare(grabs.length, 1);
        compare(grabs[0][0], rig.player());
        compare(grabs[0][1], "/tmp/shots/paused.png");
        scene.play();
        rig.destroy();
    }

    function test_a_frame_that_never_came_falls_back_to_the_grab() {
        const rig = _rig("111");
        const scene = _scene(rig);
        const grabs = [];
        verify(scene.saveScreenshot("/tmp/shots/late.png", function(item, path) { grabs.push(path); return true; }));
        compare(rig.player().savedFrames, ["/tmp/shots/late.png"]);
        compare(grabs.length, 0);
        // SceneObject::saveFrame's 5 s timeout (paused after the request).
        ignoreWarning(/screenshot: the scene drew no frame within 5 s.*the frame on screen instead/);
        rig.player().frameSaved("/tmp/shots/late.png", false, "the scene drew no frame within 5 s (is it paused?)");
        compare(grabs, ["/tmp/shots/late.png"]);
        rig.player().frameSaved("/tmp/shots/late.png", true, "");
        compare(grabs.length, 1);
        rig.destroy();
    }

    // main.qml's own grab: a file for a wallpaper on screen, a notification
    // when it cannot grab.
    function test_the_grab_writes_the_file_or_notifies() {
        const rig = rigComp.createObject(tc.parent, { screenGeometry: Qt.rect(0, 0, 200, 100) });
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        const bg = rig.background();
        // Beside this file, in the test's own copy of the tree.
        const url = String(Qt.resolvedUrl("screenshot-grab-" + Date.now() + ".png"));
        const path = url.replace(/^file:\/\//, "");
        verify(bg.grabScreenshot(rig.mainItem, path));
        tryVerify(function() {
            const x = new XMLHttpRequest();
            x.open("GET", url, false);
            x.send();
            return x.status === 200 && x.responseText.length > 0;
        }, 5000, "the grab wrote no file");
        // Not in any window: nothing to grab.
        const hidden = Qt.createQmlObject("import QtQuick; Item { width: 10; height: 10 }", tc);
        hidden.parent = null;
        ignoreWarning(/screenshot: Qt could not grab the wallpaper.*nothing written to \/tmp\/shots\/hidden\.png/);
        compare(bg.grabScreenshot(hidden, "/tmp/shots/hidden.png"), false);
        compare(rig.notifier().lastEvent, "screenshotFailed");
        hidden.destroy();
        rig.parent = null;
        rig.destroy();
    }
}
