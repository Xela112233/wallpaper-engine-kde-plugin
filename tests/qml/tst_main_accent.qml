import QtQuick
import QtTest
import Helpers 1.0

// Plasma's accent colour from the wallpaper's scheme colour through the REAL
// main.qml (ScreenRig): main.qml hands SchemeAccent the wallpaper's file
// (wallpaperPath), the reader (pyext.readfile), the user's own properties
// (curOpt.user_props) and AccentFromSchemeColor, and announces after the
// first frame.  tst_scheme_accent.qml covers SchemeAccent on its own.
TestCase {
    id: tc
    name: "Main_Accent"
    when: windowShown
    width: 400; height: 300

    Component { id: rigComp; ScreenRig {} }

    function _scenePath(id) {
        return "/tmp/fakelib/steamapps/workshop/content/431960/" + id + "/scene.pkg+scene";
    }
    function _loader(rig) { return rig._find(rig.mainItem, o => typeof o.dropOutgoing === "function"); }
    function _player(rig) {
        const l = _loader(rig);
        return l && l.item ? rig._find(l.item, o => typeof o.setAcceptMouse === "function") : null;
    }
    function _project(colour) {
        return JSON.stringify({ general: { properties: { schemecolor: { type: "color", value: colour } } } });
    }

    // A rig showing scene `id`, whose project.json (every file the stub
    // FileHelper reads) holds `projectText` and whose per-wallpaper file
    // holds `perWallpaper`; its first frame drawn.
    function _rig(id, projectText, perWallpaper, config) {
        failOnWarning(/wallpaper is not defined/);
        const rig = rigComp.createObject(tc, { screenGeometry: Qt.rect(0, 0, 1920, 1080) });
        verify(rig !== null);
        tryVerify(() => rig.mainItem !== null && rig.fileHelper() !== null, 2000);
        rig.fileHelper()._readReturns = projectText;
        rig.fileHelper()._wallpaperConfigReturns = perWallpaper || {};
        const c = { SteamLibraryPath: "/tmp/fakelib", WallpaperWorkShopId: id, WallpaperSource: _scenePath(id) };
        for (const k in (config || {})) c[k] = config[k];
        rig.setConfig(c);
        tryVerify(() => _player(rig) !== null, 2000, "scene never mounted");
        _player(rig).firstFrame();
        return rig;
    }

    function test_the_scheme_colour_of_project_json_becomes_the_accent_colour() {
        const rig = _rig("111", _project("0 0.5 1"));
        tryVerify(() => Qt.colorEqual(rig.ctx.accentColor, Qt.rgba(0, 0.5, 1, 1)), 2000,
                  "project.json's schemecolor never reached Plasma");
        verify(String(rig.fileHelper().lastRequestReadFilePath).indexOf("/431960/111/project.json") >= 0,
               String(rig.fileHelper().lastRequestReadFilePath));
        rig.destroy();
    }

    function test_the_users_scheme_colour_wins() {
        const rig = _rig("111", _project("0 0.5 1"), { user_props: { schemecolor: "1 1 0" } });
        tryVerify(() => Qt.colorEqual(rig.ctx.accentColor, Qt.rgba(1, 1, 0, 1)), 2000,
                  "the user's schemecolor was not used");
        rig.destroy();
    }

    function test_setting_off_lets_plasma_sample_the_picture() {
        const rig = _rig("111", _project("0 0.5 1"), {}, { AccentFromSchemeColor: false });
        wait(300);
        verify(Qt.colorEqual(rig.ctx.accentColor, "transparent"), String(rig.ctx.accentColor));
        rig.setConfig({ AccentFromSchemeColor: true });
        tryVerify(() => Qt.colorEqual(rig.ctx.accentColor, Qt.rgba(0, 0.5, 1, 1)), 2000);
        rig.destroy();
    }
}
