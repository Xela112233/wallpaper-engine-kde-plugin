#pragma once

// One scene renderer's frame, for the other windows of this process that show
// the same wallpaper (we-manager clone patch; the KDE plugin's ShareRenderer).
//
// The renderer's TextureNode already turns each Vulkan frame into an OpenGL
// texture for its own window.  With a share key set (SceneViewer.shareKey) it
// also publishes that texture's name here; a SceneMirror item in another
// window, given the same key, wraps the same texture name for its own window
// and draws it.  That needs both windows' OpenGL contexts in one share group,
// which plasmashell's are (it sets Qt::AA_ShareOpenGLContexts); a reader
// checks it (`shareGroup`) and refuses otherwise.
//
// Nothing here is made unless a key is set: the registry is created by the
// first acquire(), and a renderer without a key never calls it.
//
// Threads: publish(), withdraw() and noteRendered() come from the renderer's
// side (its window's scene graph thread, the wallpaper's render thread);
// current() and the reader list from each reader's scene graph thread.  One
// mutex per frame covers them.  A reader's wake callback is called with no
// lock held and may still arrive once after removeReader() returned, so it
// must guard itself (SceneMirror's node does, with a CallbackGuard).
//
// Ownership: the GL textures stay the renderer's.  It calls withdraw() before
// it deletes them, and readers drop the name at their next frame.  The frame
// object itself lives while anyone holds it (the registry only looks on), so
// the last holder gone frees it.

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QPointer>
#include <QtCore/QRectF>
#include <QtCore/QSize>
#include <QtCore/QString>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace scenebackend
{

struct SharedFrameInfo {
    // GL texture name of the newest frame; 0 while there is none.
    unsigned texture { 0 };
    // Its size in pixels.
    QSize textureSize;
    // The rendering item's size in logical pixels.  The frame covers that
    // item exactly, so this is the frame's shape on screen (the texture's
    // pixel shape can differ: Fit mode letterboxes the item, not the target).
    QSizeF itemSize;
    // The share group (QOpenGLContextGroup) of the context the texture lives
    // in; compared, never dereferenced.
    const void* shareGroup { nullptr };
    // Goes up with every change, withdraw() included.
    quint64 serial { 0 };
};

class SharedFrame {
public:
    using Wake = std::function<void()>;

    // ---- the renderer's side -------------------------------------------
    // The newest frame; wakes the readers.
    void publish(unsigned texture, QSize textureSize, const void* shareGroup, qint64 nowMs = -1) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_info.texture     = texture;
            m_info.textureSize = textureSize;
            m_info.shareGroup  = shareGroup;
            m_info.serial++;
            m_lastPublishMs = nowMs < 0 ? clockMs() : nowMs;
            m_waitingSince  = 0;
        }
        wakeReaders();
    }
    void setItemSize(QSizeF size) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_info.itemSize == size) return;
        m_info.itemSize = size;
        m_info.serial++;
    }
    // The renderer goes (or stops sharing): the texture name must not be used
    // after this returns from a reader's next frame on.  Wakes the readers.
    void withdraw() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_info.texture     = 0;
            m_info.textureSize = QSize();
            m_info.shareGroup  = nullptr;
            m_info.serial++;
            m_waitingSince = 0;
        }
        wakeReaders();
    }
    // The wallpaper's render thread finished a frame, which the renderer's
    // window has yet to take (publish).  Only for stalledMs().
    void noteRendered(qint64 nowMs = -1) {
        const qint64 now = nowMs < 0 ? clockMs() : nowMs;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastRenderedMs = now;
        if (m_waitingSince == 0) m_waitingSince = now;
    }
    // How long rendered frames have been waiting for the renderer's window to
    // take one, in ms: 0 while it takes them, and 0 while the renderer draws
    // none (paused).  A window that is not drawn (its output is off, or the
    // compositor has stopped asking it for frames) lets this grow; the KDE
    // plugin then moves the rendering to another screen.
    int stalledMs(qint64 nowMs = -1) const {
        const qint64 now = nowMs < 0 ? clockMs() : nowMs;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_waitingSince == 0 || now - m_lastRenderedMs > 1000) return 0;
        return static_cast<int>(std::min<qint64>(now - m_waitingSince, 86400000));
    }

    // ---- a reader's side -------------------------------------------------
    SharedFrameInfo current() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_info;
    }
    // `wake` is called (from any thread, no lock held) when the frame changes.
    int addReader(Wake wake) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const int id = ++m_nextReader;
        m_readers.emplace_back(id, std::move(wake));
        return id;
    }
    void removeReader(int id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_readers.begin(); it != m_readers.end(); ++it) {
            if (it->first != id) continue;
            m_readers.erase(it);
            return;
        }
    }
    int readers() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<int>(m_readers.size());
    }

    // ---- the pointer's way back (GUI thread) ------------------------------
    // The item that renders: a mirror sends it the pointer events it gets,
    // mapped into that item's coordinates (MirrorPointToSource below).
    void setSourceItem(QObject* item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sourceItem = item;
    }
    QObject* sourceItem() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_sourceItem.data();
    }

private:
    static qint64 clockMs() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
    void wakeReaders() {
        std::vector<Wake> wakes;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            wakes.reserve(m_readers.size());
            for (const auto& r : m_readers) wakes.push_back(r.second);
        }
        for (const auto& wake : wakes)
            if (wake) wake();
    }

    mutable std::mutex                 m_mutex;
    SharedFrameInfo                    m_info;
    std::vector<std::pair<int, Wake>>  m_readers;
    int                                m_nextReader { 0 };
    qint64                             m_lastPublishMs { 0 };
    qint64                             m_lastRenderedMs { 0 };
    qint64                             m_waitingSince { 0 };
    QPointer<QObject>                  m_sourceItem;
};

// The frames of this process by key.  Made at the first acquire().
class SharedFrameRegistry {
public:
    static SharedFrameRegistry& instance() {
        static SharedFrameRegistry registry;
        made().store(true);
        return registry;
    }
    // Whether anything in this process has asked for the registry: false in
    // a process where no share key was ever set.
    static bool exists() { return made().load(); }

    // The frame for `key`, made if nobody holds one.  Renderer and readers
    // call this with the same key.
    std::shared_ptr<SharedFrame> acquire(const QString& key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_frames.begin(); it != m_frames.end();) {
            if (it.value().expired())
                it = m_frames.erase(it);
            else
                ++it;
        }
        if (auto held = m_frames.value(key).lock()) return held;
        auto frame = std::make_shared<SharedFrame>();
        m_frames.insert(key, frame);
        return frame;
    }
    // How many frames someone still holds.
    int frames() {
        std::lock_guard<std::mutex> lock(m_mutex);
        int n = 0;
        for (const auto& f : std::as_const(m_frames))
            if (! f.expired()) n++;
        return n;
    }

private:
    SharedFrameRegistry() = default;
    static std::atomic<bool>& made() {
        static std::atomic<bool> flag { false };
        return flag;
    }
    std::mutex                                  m_mutex;
    QHash<QString, std::weak_ptr<SharedFrame>>  m_frames;
};

// Cursor mapping under clone.  A mirror item shows `sourceRect` of the frame
// (in fractions of the frame, 0..1 each way) stretched over its own area, and
// the frame covers the rendering item exactly.  So a point `p` of the mirror
// item (its own coordinates, logical pixels) is this point of the rendering
// item (that item's coordinates, logical pixels):
//
//     u = sourceRect.x + p.x / mirror.width  * sourceRect.width
//     v = sourceRect.y + p.y / mirror.height * sourceRect.height
//     q = (u * source.width, v * source.height)
//
// Nothing is clamped: a point outside the mirror item (a letterbox bar in Fit
// mode) maps outside the rendering item, as a point beside a letterboxed
// renderer does without sharing.  No value when a size is empty.
inline std::optional<QPointF> MirrorPointToSource(QPointF p, QSizeF mirrorSize, QRectF sourceRect,
                                                  QSizeF sourceSize) {
    if (mirrorSize.width() <= 0 || mirrorSize.height() <= 0) return std::nullopt;
    if (sourceSize.width() <= 0 || sourceSize.height() <= 0) return std::nullopt;
    if (sourceRect.width() <= 0 || sourceRect.height() <= 0) return std::nullopt;
    const qreal u = sourceRect.x() + p.x() / mirrorSize.width() * sourceRect.width();
    const qreal v = sourceRect.y() + p.y() / mirrorSize.height() * sourceRect.height();
    return QPointF(u * sourceSize.width(), v * sourceSize.height());
}

} // namespace scenebackend
