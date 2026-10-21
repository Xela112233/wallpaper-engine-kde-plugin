import QtQuick
import QtTest

import "../../plugin/contents/ui" as Plugin

// we-manager AccentFromSchemeColor (SchemeAccent.qml): Plasma takes the
// wallpaper's own scheme colour as accentColor, and samples the picture
// (transparent accentColor plus accentColorChanged) when there is none.
TestCase {
    id: tc
    name: "SchemeAccent"

    // Stands in for Plasma's WallpaperItem, like Helpers/WallpaperFake.qml.
    Component { id: itemComp; Item { property color accentColor: "transparent" } }

    // project.json text by directory; readfile answers at once, or holds
    // the answer back until release() when `held` is set.
    property var projects: ({})
    property bool held: false
    property var heldCalls: []
    property var reads: []
    function readfile(path) {
        reads.push(path);
        const dir = path.substring(0, path.lastIndexOf('/'));
        const text = projects[dir];
        return {
            then: function(ok, fail) {
                const answer = function() {
                    if (text === undefined) { if (fail) fail(new Error("readFile failed: " + path)); }
                    else ok(text);
                };
                if (tc.held) tc.heldCalls.push(answer); else answer();
                return this;
            },
        };
    }
    function release() {
        const calls = heldCalls;
        heldCalls = [];
        calls.forEach(f => f());
    }

    function project(colour) {
        return JSON.stringify({ general: { properties: colour === null ? {}
            : { schemecolor: { type: "color", value: colour } } } });
    }

    Component {
        id: accentComp
        Plugin.SchemeAccent { readfile: tc.readfile }
    }

    SignalSpy { id: changed; signalName: "accentColorChanged" }

    function make(path, userProps) {
        const item = itemComp.createObject(tc);
        const a = accentComp.createObject(tc, { wallpaperItem: item, path: path, userProps: userProps || {} });
        changed.target = item;
        changed.clear();
        return [item, a];
    }

    function init() {
        projects = {
            "/ws/1": project("0 0.5 1"),
            "/ws/2": project(null),
            "/ws/3": project("1 0 0"),
        };
        held = false;
        heldCalls = [];
        reads = [];
    }

    function test_schemeColourBecomesTheAccentColour() {
        const [item, a] = make("file:///ws/1/scene.json");
        compare(reads, ["/ws/1/project.json"]);
        a.announce();
        verify(Qt.colorEqual(item.accentColor, Qt.rgba(0, 0.5, 1, 1)), String(item.accentColor));
        compare(changed.count, 1);
    }

    function test_noSchemeColour_samplesThePicture_atOnce() {
        const [item, a] = make("file:///ws/2/index.html");
        a.announce();
        verify(Qt.colorEqual(item.accentColor, "transparent"));
        compare(changed.count, 1, "Plasma must be told to sample, synchronously");
    }

    function test_noProjectJson_samplesThePicture() {
        const [item, a] = make("file:///videos/clip.mp4");
        a.announce();
        compare(changed.count, 1);
        verify(Qt.colorEqual(item.accentColor, "transparent"));
    }

    function test_theUsersValueWins() {
        const [item, a] = make("file:///ws/1/scene.json", { schemecolor: "1 1 0" });
        a.announce();
        verify(Qt.colorEqual(item.accentColor, Qt.rgba(1, 1, 0, 1)));
    }

    function test_nextWallpaperWithoutOne_goesBackToSampling() {
        const [item, a] = make("file:///ws/1/scene.json");
        a.announce();
        a.path = "file:///ws/2/scene.json";
        changed.clear();
        a.announce();
        verify(Qt.colorEqual(item.accentColor, "transparent"));
        compare(changed.count, 1, "back to transparent announces once");
    }

    function test_settingOff_samples_andOnAgainTakesTheColour() {
        const [item, a] = make("file:///ws/1/scene.json");
        a.enabled = false;
        a.announce();
        verify(Qt.colorEqual(item.accentColor, "transparent"));
        a.enabled = true;                  // while the wallpaper shows
        verify(Qt.colorEqual(item.accentColor, Qt.rgba(0, 0.5, 1, 1)));
        a.enabled = false;
        verify(Qt.colorEqual(item.accentColor, "transparent"));
    }

    // main.qml announces after the first frame; a project.json read that is
    // still out then hands its colour over when it lands.
    function test_aReadThatLandsAfterTheAnnouncement_stillCounts() {
        held = true;
        const [item, a] = make("file:///ws/1/scene.json");
        a.announce();
        compare(changed.count, 1);         // sampled meanwhile
        release();
        verify(Qt.colorEqual(item.accentColor, Qt.rgba(0, 0.5, 1, 1)));
    }

    function test_aStaleRead_isDropped() {
        held = true;
        const [item, a] = make("file:///ws/1/scene.json");
        a.path = "file:///ws/2/scene.json";
        release();                         // both answers; only /ws/2's counts
        a.announce();
        verify(Qt.colorEqual(item.accentColor, "transparent"));
    }

    function test_userPicksAnotherSchemeColour_whileShown() {
        const [item, a] = make("file:///ws/1/scene.json");
        a.announce();
        a.userProps = { schemecolor: "0 1 0" };
        verify(Qt.colorEqual(item.accentColor, Qt.rgba(0, 1, 0, 1)));
        // Before an announcement for the new path (a handoff), nothing moves.
        a.path = "file:///ws/3/scene.json";
        a.userProps = { schemecolor: "0 0 1" };
        verify(Qt.colorEqual(item.accentColor, Qt.rgba(0, 1, 0, 1)));
    }

    // A host whose accentColor cannot be written: sampled as before, with a
    // warning naming the write, and no exception into main.qml.
    Component { id: readOnlyComp; Item { readonly property color accentColor: "transparent" } }
    function test_aReadOnlyAccentColor_fallsBackToSampling() {
        const item = readOnlyComp.createObject(tc);
        const a = accentComp.createObject(tc, { wallpaperItem: item, path: "file:///ws/1/scene.json" });
        changed.target = item;
        changed.clear();
        ignoreWarning(/SchemeAccent: WallpaperItem\.accentColor = .* failed .*Plasma samples the picture instead/);
        a.announce();
        compare(changed.count, 1);
    }

    function test_aWallpaperItemWithoutAccentColor_isLeftAlone() {
        const a = accentComp.createObject(tc, { wallpaperItem: tc, path: "file:///ws/1/scene.json" });
        a.announce();                      // no throw
        verify(true);
    }
}
