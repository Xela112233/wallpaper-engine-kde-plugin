#ifndef MPVRENDERER_H_
#define MPVRENDERER_H_

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickFramebufferObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMutex>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <string>
#include <utility>
#include <vector>
#include <atomic>
#include <memory>

#include "qthelper.hpp"

Q_DECLARE_LOGGING_CATEGORY(wekdeMpv)

namespace mpv
{

class MpvObject;

struct MpvHandle {
    MpvHandle(mpv_handle* mpv): handle(mpv) {}
    ~MpvHandle() { mpv_terminate_destroy(handle); }
    mpv_handle* handle;

    // Start tearing the player down. Called on the GUI thread from ~MpvObject:
    // detaches the wakeup callback, then asks the core to quit *without*
    // waiting for it. The blocking half of the teardown, ~MpvHandle's
    // mpv_terminate_destroy, runs on the Qt Quick render thread — the renderer
    // holds the last shared_ptr ref and cleanupNodes deletes it there — while
    // the GUI thread sits in polishAndSync, so anything it waits for freezes
    // every window plasmashell owns, not just the wallpaper. Starting the quit
    // here lets the core wind down alongside the QML destroy() delay, and
    // client.h names this as *the* asynchronous-destruction path: run "quit",
    // then react to MPV_EVENT_SHUTDOWN.
    void beginShutdown();
    // First half of beginShutdown: stop delivering mpv events to the owner.
    void detachOwner();

    // Wakeup callback indirection. The callback runs on an arbitrary mpv
    // player thread; without this hop it would deref a destroyed MpvObject
    // (the .so is dlopen'd into plasmashell — a dangling postEvent would
    // crash the desktop). MpvHandle outlives any single MpvObject via the
    // MpvRender shared_ptr ref, so the mutex + owner pointer stay valid
    // until ~MpvHandle joins the player thread via mpv_terminate_destroy.
    QMutex     wakeup_mutex;
    MpvObject* owner { nullptr };
};

// What synchronize() should do with one mpv_render_context_create attempt.
// libmpv's GL bring-up needs a current QOpenGLContext on the scene graph's
// render thread, so the call itself only happens in a live plasmashell; the
// decision made from its return code lives out here where it can be exercised.
struct RenderInitOutcome {
    bool    usable { false }; // context is live: install the update callback, signal inited
    bool    report { false }; // first failure: warn and hand the reason to QML
    QString reason;           // libmpv's explanation; empty unless report
};

// `already_reported` is the caller's one-shot latch: synchronize() runs again
// on every resize and item update, and a bring-up that failed once fails the
// same way every time.
RenderInitOutcome classifyRenderInit(int create_result, bool already_reported);

class MpvRender;

class MpvObject : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool mute READ mute WRITE setMute NOTIFY muteChanged)
    Q_PROPERTY(QString logfile READ logfile WRITE setLogfile NOTIFY logfileChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    // Opt-in (MpvPowerSaving): cheap scalers, frames at the video's own rate.
    Q_PROPERTY(bool powerSaving READ powerSaving WRITE setPowerSaving NOTIFY powerSavingChanged)
    // Opt-in (MpvInterpolation): frame blending at the display's refresh
    // rate, interpolation=yes with tscale=oversample.
    Q_PROPERTY(bool interpolation READ interpolation WRITE setInterpolation NOTIFY interpolationChanged)
    // MpvHwdec (default on): hwdec=auto, else hwdec=no (CPU decoding, for
    // broken VA-API drivers).  Written live; mpv reopens the decoder.
    Q_PROPERTY(bool hwdec READ hwdec WRITE setHwdec NOTIFY hwdecChanged)
    // MpvLoadInMemory: a local file is held whole in mpv's demuxer cache
    // (up to kMemoryCacheCap), so the loop wraps inside the cache.  Taken at
    // the next load.
    Q_PROPERTY(bool loadInMemory READ loadInMemory WRITE setLoadInMemory NOTIFY loadInMemoryChanged)
    // MpvVideoSync: 0 the video's own clock (nothing written, as before),
    // 1 display-resample, 2 display-vdrop, both with the screen's refresh
    // rate as display-fps-override.  Power saving and interpolation win.
    Q_PROPERTY(int videoSync READ videoSync WRITE setVideoSync NOTIFY videoSyncChanged)
    // Upper bound on how often a new video frame is drawn; 0 = every frame
    // mpv produces.  mpv drops the frames in between, like on a slow display.
    Q_PROPERTY(int maxFps READ maxFps WRITE setMaxFps NOTIFY maxFpsChanged)

    friend class MpvRender;

public:
    // mpv's wakeup callback fires on an arbitrary player thread; it must only
    // marshal back to the GUI thread (queued), never touch QObject state direct.
    static void wakeup(void* ctx);

    explicit MpvObject(QQuickItem* parent = nullptr);
    virtual ~MpvObject();
    Renderer* createRenderer() const override;

    enum Status
    {
        Stopped,
        Playing,
        Paused,
    };
    Q_ENUM(Status)
    // Returns the cached status maintained by refreshStatus on observed mpv
    // property-change events. Inline + trivial so QML bindings that read
    // `status` reactively pay no sync mpv_get_property cost per evaluation.
    Status status() const { return m_lastStatus; }
    // Sync read from mpv (the historical status() body). Used at ctor time
    // to bootstrap m_lastStatus before the event-driven path takes over.
    Status liveStatus() const;
    // Pure mapping of mpv's (idle-active, pause) flags to a playback Status.
    static Status deriveStatus(bool idleActive, bool paused);
    // Re-derive from the given flags; emit statusChanged() iff the status
    // crossed a boundary vs the last seen value. Returns whether it changed.
    bool refreshStatus(bool idleActive, bool paused);
    // True only after mpv_create() AND mpv_initialize() both succeeded: libmpv
    // is usable at all. Distinct from the private `inited`, which tracks the GL
    // render context — that one can still fail with this true.
    bool    initialized() const { return m_mpv != nullptr && m_inited_ok; }
    QUrl    source() const;
    bool    mute() const;
    QString logfile() const;
    int     volume() const;
    bool    powerSaving() const { return m_powerSaving; }
    bool    interpolation() const { return m_interpolation; }
    bool    hwdec() const { return m_hwdec; }
    bool    loadInMemory() const { return m_loadInMemory; }
    int     videoSync() const { return m_videoSync; }
    int     maxFps() const { return m_maxFps; }

    // The mpv options powerSaving sets: the built-in `fast` profile (bilinear
    // scaling, no dithering / linear or sigmoid scaling / HDR peak detection)
    // plus video-sync=audio, i.e. present frames at the video's own rate
    // instead of retiming to every display refresh.
    static const std::vector<std::pair<const char*, const char*>>& powerSavingOptions();
    // Frame-rate cap: how long to hold a redraw requested `sinceLastMs` after
    // the previous one; 0 = draw now.  maxFps <= 0 disables the cap.
    static int frameCapDelayMs(int maxFps, qint64 sinceLastMs);

    // The demuxer cache a load gets.  Network streams always get mpv's cache
    // and readahead (the constructor's cache=no would make them stutter);
    // a local file with loadInMemory gets a cache big enough for all of it
    // (forward and back, so the loop's seek to the start stays in the
    // cache), at most kMemoryCacheCap; anything else keeps cache=no.  A size
    // of 0 means mpv's own default.  `fileSize` < 0: unknown.
    struct CachePlan {
        bool   cache { false };
        qint64 maxBytes { 0 };
        qint64 maxBackBytes { 0 };
    };
    static constexpr qint64 kMemoryCacheCap = qint64(1) << 30; // 1 GiB
    static CachePlan        cachePlan(bool network, bool loadInMemory, qint64 fileSize);
    // http(s), rtmp(s), rtsp, rtp, udp, tcp, srt, mms and ytdl URLs; Mpv.qml's
    // load watchdog waits longer for the same schemes.
    static bool isNetworkSource(const QUrl& source);
    // The video-sync value for MpvVideoSync's mode, or nullptr for 0 (the
    // video's clock: leave mpv's option alone).
    static const char* videoSyncMode(int mode);

    void setSource(const QUrl& source);
    void setMute(const bool& mute);
    void setLogfile(const QString& logfile);
    void setVolume(const int& volume);
    void setPowerSaving(bool on);
    void setInterpolation(bool on);
    void setHwdec(bool on);
    void setLoadInMemory(bool on);
    void setVideoSync(int mode);
    void setMaxFps(int fps);

public slots:
    void play();
    void pause();
    void stop();
    // Fire-and-forget `stop` for a backend about to be destroyed; see .cpp.
    void stopAsync();

    bool     command(const QVariant& params);
    bool     setProperty(const QString& name, const QVariant& value);
    QVariant getProperty(const QString& name, bool* ok = nullptr) const;
    void     initCallback();
    // Queued from the render thread when the mpv render context could not be
    // brought up.
    void onRenderInitFailed(const QString& reason);
    void checkAndEmitFirstFrame();
    // GUI-thread drain of mpv's event queue (posted by wakeup()); emits
    // statusChanged() when an observed property moves the derived status.
    void onMpvEvents();

signals:
    void initFinished();
    void statusChanged();
    void initializedChanged();
    void sourceChanged();
    void firstFrame();
    // Surface mpv failures so QML can react immediately instead of waiting for
    // the 15s watchdog. Three emit sites:
    //   - setSource(): when this->command(loadfile, ...) returns false
    //     (sync rejection by mpv's command parser).
    //   - onMpvEvents(): when mpv posts MPV_EVENT_END_FILE with
    //     reason == MPV_END_FILE_REASON_ERROR (async demux/decode failure).
    //   - onRenderInitFailed(): when the GL render context never came up, so
    //     nothing will play regardless of the file.
    // The reason string is short English (mpv_error_string for the async and
    // render paths; a literal diagnostic for the sync path), suitable for
    // direct display in the loadInfoShow pane.
    void sourceLoadFailed(const QString& reason);
    void muteChanged();
    void volumeChanged();
    void powerSavingChanged();
    void interpolationChanged();
    void hwdecChanged();
    void loadInMemoryChanged();
    void videoSyncChanged();
    void maxFpsChanged();
    void logfileChanged();

private:
    // Render context ready: set by the queued initCallback() after MpvRender emits
    // inited(); gates setSource()'s loadfile. Distinct from m_inited_ok (mpv_initialize).
    bool inited = false;
    QUrl m_source;
    // Last status surfaced via statusChanged(); the diff anchor so the signal
    // fires once per real transition rather than per observed-property event.
    Status m_lastStatus { Stopped };
    // Last observed "idle-active" / "pause" values, taken from the property
    // events themselves (a synchronous mpv_get_property on the GUI thread
    // blocks while the core is busy, e.g. tearing its video output down).
    bool m_propIdle { true };
    bool m_propPaused { false };
    bool m_inited_ok { false }; // set once mpv_initialize() succeeds

    // powerSaving: the values its options had before it was switched on, so
    // switching it off restores exactly those (mpv's defaults vary by version).
    bool                                             m_powerSaving { false };
    std::vector<std::pair<std::string, std::string>> m_savedOptions;
    // interpolation: mpv blends frames only with its display sync running, and
    // vo_libmpv cannot report the display's refresh rate, so it is passed as
    // display-fps-override from the screen this item is on, and followed when
    // the window moves to another screen or the mode changes.  m_applied* is
    // what mpv has now (skips repeated writes); m_syncOwned is set once
    // interpolation was on, from then on applyDisplaySync() owns video-sync.
    bool                    m_interpolation { false };
    bool                    m_appliedOn { false };
    double                  m_appliedHz { 0.0 };
    bool                    m_syncOwned { false };
    bool                    m_stopping { false }; // stopAsync(): never touch the player again
    QMetaObject::Connection m_screenConn;
    QMetaObject::Connection m_refreshConn;
    void                    followScreen();
    // Owns video-sync, display-fps-override and interpolation once
    // interpolation or a display videoSync mode has been on.
    void applyDisplaySync(bool force = false);
    // mpv-options patch.  m_appliedSync is the display mode mpv has now
    // ("" none), next to m_appliedOn (interpolation) and m_appliedHz.
    bool        m_hwdec { true };
    bool        m_loadInMemory { false };
    int         m_videoSync { 0 };
    std::string m_appliedSync;
    // Cache options: written only once a load needed a cache, so a player
    // that never does keeps the constructor's options exactly.  The defaults
    // are mpv's own, read after mpv_initialize().
    bool   m_cacheTouched { false };
    qint64 m_defaultMaxBytes { 0 };
    qint64 m_defaultMaxBackBytes { 0 };
    void   applyCachePlan(const QUrl& source);
    // Frame-rate cap (GUI thread): when the last redraw went out, and a
    // single-shot timer for a deferred one.
    int           m_maxFps { 0 };
    QElapsedTimer m_frameClock;
    QTimer        m_frameTimer;
    void          scheduleUpdate();
    void          redrawNow();

private:
    mpv_handle*                m_mpv { nullptr };
    std::shared_ptr<MpvHandle> m_shared_mpv { nullptr };
    // Written on the GUI thread (setSource) and read+written on the Qt Quick
    // render thread (checkAndEmitFirstFrame, via MpvRender::synchronize), so it
    // must be atomic.  Mirrors the sibling MpvRender::m_dirty atomic style.
    std::atomic<bool> m_first_frame { true };
};
} // namespace mpv

#endif
