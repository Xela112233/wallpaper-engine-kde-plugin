import QtQuick
import QtTest
import Helpers 1.0

// The scene quality switches and the web wallpaper resolution (we-manager
// scene-quality patch), through the REAL main.qml (ScreenRig).
//
// With the settings at their defaults the loaded scene is handed the map it
// was handed before the patch, {textures}, and the web view is laid out as
// before: filling its parent, scale 1, the zoom factor never written.
// Volumetrics, Reflections and MsaaOffForFlatScenes each add their key to
// SceneViewer.sceneQuality of the scene that is running (the renderer reloads
// the scene when the map changes; no new backend is made), and take it out
// again.  WebRenderScale 50 lays a web view whose page has loaded out at half
// the screen, scales it up and halves its zoom factor; a page that is loading
// (the first load, a reload, after a discard, another file of the wallpaper)
// always has the full-size view at zoom 1.  What the renderer does with a key
// is the renderer's own tests' (test_SceneQualitySwitches.cpp).
TestCase {
    id: tc
    name: "Main_SceneQuality"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }

    function _scenePath(id) {
        return "file:///tmp/fakelib/steamapps/workshop/content/431960/" + id + "/scene.pkg+scene";
    }
    function _loader(rig) { return rig._find(rig.mainItem, o => typeof o.dropOutgoing === "function"); }
    function _player(rig) {
        const l = _loader(rig);
        return l && l.item ? rig._find(l.item, o => typeof o.setAcceptMouse === "function") : null;
    }
    // The WebEngineView inside the mounted QtWebView.qml.
    function _web(rig) {
        const item = rig.webView();
        return item ? rig._find(item, o => typeof o.runJavaScript === "function") : null;
    }
    function _rig(config) {
        failOnWarning(/wallpaper is not defined/);
        const c = { SteamLibraryPath: "file:///tmp/fakelib", WallpaperWorkShopId: "111",
                    WallpaperSource: _scenePath("111") };
        for (const k in (config || {})) c[k] = config[k];
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080), initialConfig: c });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.background() !== null, 2000);
        return rig;
    }
    function _sceneRig(config) {
        const rig = _rig(config);
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        return rig;
    }
    function _webRig(config) {
        const c = { WallpaperSource: "/tmp/fakelib/steamapps/workshop/content/431960/111/index.html+web" };
        for (const k in (config || {})) c[k] = config[k];
        const rig = _rig(c);
        tryVerify(() => _web(rig) !== null, 2000, "web backend never mounted");
        return rig;
    }
    function _keys(map) { return Object.keys(map).sort().join(","); }

    function test_with_the_defaults_the_scene_gets_the_map_it_always_got() {
        const rig = _sceneRig();
        compare(_keys(_player(rig).sceneQuality), "textures");
        compare(_player(rig).sceneQuality.textures, 0);
        // Written out as their defaults, the same.
        rig.setConfig({ Volumetrics: true, Reflections: true, MsaaOffForFlatScenes: false, TextureQuality: 1 });
        compare(_keys(_player(rig).sceneQuality), "textures");
        compare(_player(rig).sceneQuality.textures, 1);
        rig.destroy();
    }

    function test_each_switch_names_its_key_on_the_running_scene_and_takes_it_back() {
        const rig = _sceneRig();
        const player = _player(rig);
        rig.setConfig({ Volumetrics: false });
        compare(_keys(player.sceneQuality), "textures,volumetrics");
        compare(player.sceneQuality.volumetrics, 0);
        rig.setConfig({ Reflections: false });
        compare(_keys(player.sceneQuality), "reflections,textures,volumetrics");
        compare(player.sceneQuality.reflections, 0);
        rig.setConfig({ Volumetrics: true, MsaaOffForFlatScenes: true });
        compare(_keys(player.sceneQuality), "flatMsaa,reflections,textures");
        compare(player.sceneQuality.flatMsaa, 1);
        rig.setConfig({ Reflections: true, MsaaOffForFlatScenes: false });
        compare(_keys(player.sceneQuality), "textures");
        verify(_player(rig) === player, "the same scene item throughout: the renderer reloads, not main.qml");
        rig.destroy();
    }

    function test_a_scene_loaded_with_a_switch_off_has_it_from_the_start() {
        const rig = _sceneRig({ Reflections: false, MsaaMode: 8 });
        compare(_keys(_player(rig).sceneQuality), "reflections,textures");
        compare(_player(rig).msaaMode, 8, "8x goes the way 2x and 4x do");
        rig.destroy();
    }

    // The view's own loadingChanged, as the real engine sends it.
    function _loadStarted(web)   { web.loadingChanged({ status: 0, url: "" }); }
    function _loadSucceeded(web) { web.loadProgress = 100; web.loadingChanged({ status: 2, url: "" }); }
    function _isFull(web, item) {
        return web.width === item.width && web.height === item.height && web.scale === 1
            && web.zoomFactor === 1 && web.anchors.rightMargin === 0 && web.anchors.bottomMargin === 0;
    }

    function test_with_the_default_the_web_view_is_laid_out_as_before() {
        const rig = _webRig();
        const web = _web(rig);
        const item = rig.webView();
        verify(item.width > 0 && item.height > 0);
        verify(_isFull(web, item));
        compare(web.transformOrigin, Item.Center);
        compare(item.state, "");
        // A page that loads, reloads and is discarded: nothing follows it.
        _loadSucceeded(web);
        _loadStarted(web);
        _loadSucceeded(web);
        verify(_isFull(web, item));
        compare(item.state, "");
        compare(item.documentLoaded, false, "at 100 the view's loads are not even listened to");
        compare(web.lifecycleState, 0, "the view is never discarded for a load");
        rig.destroy();
    }

    function test_half_resolution_reduces_a_loaded_page_and_never_a_starting_one() {
        const rig = _webRig({ WebRenderScale: 50 });
        const web = _web(rig);
        const item = rig.webView();
        const w = item.width, h = item.height;
        // Loading: the page starts in the full-size view at zoom 1.
        verify(_isFull(web, item), "a page that has not loaded sees the whole view");
        _loadStarted(web);
        verify(_isFull(web, item));
        // Loaded: half the view, scaled by two, half the zoom factor.
        _loadSucceeded(web);
        tryCompare(web, "scale", 2);
        compare(web.width, Math.ceil(w / 2));
        compare(web.height, Math.ceil(h / 2));
        compare(web.zoomFactor, 0.5);
        // The view still covers the screen, and a point of the screen is the
        // same point of the page: half the item's pixels, twice the CSS ones.
        const far = item.mapFromItem(web, web.width, web.height);
        verify(far.x >= w && far.x < w + 2, "covers the width: " + far.x);
        verify(far.y >= h && far.y < h + 2, "covers the height: " + far.y);
        const p = web.mapFromItem(item, 300, 200);
        compare(p.x / web.zoomFactor, 300);
        compare(p.y / web.zoomFactor, 200);
        // The page reloads itself: full size while the new document starts.
        _loadStarted(web);
        verify(_isFull(web, item));
        _loadSucceeded(web);
        compare(web.scale, 2);
        // A long pause discards the document: full size until the next one has loaded.
        web.lifecycleState = 2;
        verify(_isFull(web, item));
        web.lifecycleState = 0;
        verify(_isFull(web, item), "a discarded view that wakes has no loaded document yet");
        _loadSucceeded(web);
        compare(web.scale, 2);
        rig.destroy();
    }

    function test_another_file_of_the_same_wallpaper_starts_in_the_full_size_view() {
        const rig = _webRig({ WebRenderScale: 50 });
        const web = _web(rig);
        const item = rig.webView();
        _loadSucceeded(web);
        compare(web.scale, 2);
        // main.qml keeps the backend for a change of file only; loadWallpaper()
        // drops the reduced view's document first, so the new one starts at full size.
        let discards = 0;
        const count = function() { if (web.lifecycleState === 2) discards += 1; };
        web.lifecycleStateChanged.connect(count);
        const loads = item._testReloadCallCount;
        rig.setConfig({ WallpaperSource: "/tmp/fakelib/steamapps/workshop/content/431960/111/other.html+web" });
        tryVerify(() => item._testReloadCallCount === loads + 1, 2000, "the new file was not loaded");
        verify(rig.webView() === item, "the same backend");
        compare(discards, 1, "the old document is dropped before the load");
        verify(_isFull(web, item));
        _loadSucceeded(web);
        compare(web.scale, 2);
        // While paused the view is hidden and frozen: it is left alone.
        item.pause();
        rig.setConfig({ WallpaperSource: "/tmp/fakelib/steamapps/workshop/content/431960/111/third.html+web" });
        tryVerify(() => item._testReloadCallCount === loads + 2, 2000);
        compare(discards, 1);
        rig.destroy();
    }

    function test_the_setting_applies_to_a_running_page_at_once_and_goes_back() {
        const rig = _webRig();
        const web = _web(rig);
        const item = rig.webView();
        const w = item.width, h = item.height;
        _loadSucceeded(web);                    // at 100: not listened to
        rig.setConfig({ WebRenderScale: 50 });
        tryCompare(web, "scale", 2);            // the page is loaded: reduced now
        compare(web.width, Math.ceil(w / 2));
        verify(rig.webView() === item && _web(rig) === web, "the page is not reloaded");
        compare(web.lifecycleState, 0);
        rig.setConfig({ WebRenderScale: 100 });
        tryCompare(web, "scale", 1);
        verify(_isFull(web, item));
        // A value outside 25 to 99 is full resolution.
        rig.setConfig({ WebRenderScale: 0 });
        verify(_isFull(web, item));
        rig.setConfig({ WebRenderScale: 150 });
        verify(_isFull(web, item));
        rig.destroy();
    }
}
