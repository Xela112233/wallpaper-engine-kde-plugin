// Test stub — see tests/qml/_stubs/README.md for contract.
// Real source: src/SafeWallpaperBridge.hpp + src/SafeWallpaperBridge.cpp
// Last contract review: 2026-11-06

import QtQuick
// Test stub for the C++ SafeWallpaperBridge. Mirrors the production surface:
//   * generalProperties / userProperties / loaded are exposed (writable here
//     for stub-internal mutation by the setters; the production C++ properties
//     are READ-only over QWebChannel — the setters are the only mutation path
//     that fires the NOTIFY chain consistently).
//   * pushGeneralProperties / pushUserProperties / setLoaded are functions
//     (matching the C++ public methods, NOT Q_INVOKABLE — QML reaches them
//     via the type system).
//   * sigGeneralProperties / sigUserProperties / sigAudio / sigInit are
//     signals that wallpaper JS subscribes to over QWebChannel.
//   * loadedChanged is auto-emitted by the QML engine for the `loaded`
//     property; setLoaded routes through it + fires sigInit once per
//     loaded document.
//   * Media integration (we-manager): READ-only mediaStatus /
//     mediaProperties / mediaThumbnail / mediaPlayback / mediaTimeline /
//     paused, whose C++ NOTIFY signals are the argument-less sigMedia* /
//     sigPaused. QML names a property's own notify <name>Changed, so the
//     sig* signals are declared here and fired by the setters, which emit
//     only on a change, as the C++ ones do.
QtObject {
    id: bridge

    // Backing fields — writable internally; expose READ-only views.
    property var  _generalProperties: ({})
    property var  _userProperties:    ({})
    property bool _loaded:            false
    // Per-document init guard — matches the C++ m_initFired.
    property bool _initFired:         false

    // READ-only views — match the production C++ Q_PROPERTY surface
    // (READ only, no WRITE = JS-side assignments through QWebChannel
    // silently no-op; QML direct writes raise "Cannot assign to non-writable
    // property" warnings).
    readonly property var  generalProperties: _generalProperties
    readonly property var  userProperties:    _userProperties
    readonly property bool loaded:            _loaded

    property bool _mediaStatus:     false
    property var  _mediaProperties: ({})
    property var  _mediaThumbnail:  ({})
    property int  _mediaPlayback:   0
    property var  _mediaTimeline:   ({})
    property bool _paused:          false
    readonly property bool mediaStatus:     _mediaStatus
    readonly property var  mediaProperties: _mediaProperties
    readonly property var  mediaThumbnail:  _mediaThumbnail
    readonly property int  mediaPlayback:   _mediaPlayback
    readonly property var  mediaTimeline:   _mediaTimeline
    readonly property bool paused:          _paused

    signal sigGeneralProperties(var properties)
    signal sigUserProperties(var properties)
    signal sigAudio(var samples)
    signal sigInit()
    signal sigMediaStatus()
    signal sigMediaProperties()
    signal sigMediaThumbnail()
    signal sigMediaPlayback()
    signal sigMediaTimeline()
    signal sigPaused()

    function _same(a, b) { return JSON.stringify(a) === JSON.stringify(b); }
    function setMediaStatus(v)     { if (_mediaStatus !== v) { _mediaStatus = v; sigMediaStatus(); } }
    function setMediaProperties(m) { if (!_same(_mediaProperties, m)) { _mediaProperties = m; sigMediaProperties(); } }
    function setMediaThumbnail(m)  { if (!_same(_mediaThumbnail, m)) { _mediaThumbnail = m; sigMediaThumbnail(); } }
    function setMediaPlayback(v)   { if (_mediaPlayback !== v) { _mediaPlayback = v; sigMediaPlayback(); } }
    function setMediaTimeline(m)   { if (!_same(_mediaTimeline, m)) { _mediaTimeline = m; sigMediaTimeline(); } }
    function setPaused(v)          { if (_paused !== v) { _paused = v; sigPaused(); } }
    function clearMedia() {
        setMediaStatus(false);
        setMediaPlayback(0);
        setMediaProperties({});
        setMediaThumbnail({});
        setMediaTimeline({});
    }

    function pushGeneralProperties(m) {
        _generalProperties = m;
        sigGeneralProperties(m);
    }
    function pushUserProperties(m) {
        _userProperties = m;
        sigUserProperties(m);
    }
    function setLoaded(v) {
        if (_loaded === v) return;
        _loaded = v;
        // false means the document went away — re-arm init for the next one.
        if (!v) _initFired = false;
        if (v && !_initFired) {
            _initFired = true;
            sigInit();
        }
    }
}
