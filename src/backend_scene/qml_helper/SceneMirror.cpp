#include "SceneMirror.hpp"

#include "RenderCallbackGuard.hpp"
#include "SceneGraphApi.hpp"
#include "SharedFrame.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QLoggingCategory>
#include <QtGui/QImage>
#include <QtGui/QOpenGLContext>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QSGTexture>

#include <atomic>
#include <unordered_map>

Q_LOGGING_CATEGORY(wekdeMirror, "wekde.mirror")

namespace scenebackend
{

namespace
{
// See MirrorNode::wrapper() and SceneMirror::wrapperCounts().
std::atomic<int> g_wrappersReplaced { 0 };
std::atomic<int> g_wrappersDeletedInUse { 0 };
} // namespace

// The scene graph node of a SceneMirror: lives on its window's scene graph
// thread.  It holds one wrapper (QSGTexture) per GL texture name the renderer
// has published; the names stay the renderer's.
class MirrorNode : public QObject, public QSGSimpleTextureNode {
    Q_OBJECT
public:
    explicit MirrorNode(QQuickWindow* window): m_window(window) {
        // A texture node must have a texture: one transparent pixel stands in
        // while there is no frame, so nothing is drawn then.
        QImage clear(1, 1, QImage::Format_ARGB32_Premultiplied);
        clear.fill(Qt::transparent);
        m_empty = window->createTextureFromImage(clear, QQuickWindow::TextureHasAlphaChannel);
        setTexture(m_empty);
        setFiltering(QSGTexture::Linear);
        setOwnsTexture(false);
    }

    ~MirrorNode() override {
        // First: after this no wake from the renderer's side reaches the node.
        m_guard->invalidate();
        detach();
        dropWrappers();
        delete m_empty;
    }

    // Scene graph sync (the GUI thread is blocked): follow the item.
    void sync(const std::shared_ptr<SharedFrame>& frame, bool live, const QRectF& rect,
              const QRectF& sourceRect, bool smooth) {
        m_live.store(live);
        if (frame != m_frame) {
            detach();
            setTexture(m_empty);
            dropWrappers();
            m_frame = frame;
            m_shown = 0;
            if (m_frame) {
                auto               guard  = m_guard;
                const SharedFrame* source = m_frame.get(); // alive while it calls this
                m_reader                  = m_frame->addReader([guard, source]() {
                    guard->invoke([source](MirrorNode* self) {
                        // A paused mirror follows no frame; it is woken only
                        // to learn that there is one now, or that it is gone.
                        if (self->m_live.load()
                            || (source->current().texture != 0) != self->m_reportedFrame.load())
                            Q_EMIT self->redraw();
                    });
                });
            }
        }
        const SharedFrameInfo info = m_frame ? m_frame->current() : SharedFrameInfo {};
        QSGTexture*           tex  = m_empty;
        unsigned              name = 0;
        bool                  has  = false;
        if (info.texture != 0) {
            const QOpenGLContext* ctx = QOpenGLContext::currentContext();
            if (ctx == nullptr || ctx->shareGroup() != info.shareGroup) {
                fail(QStringLiteral("this screen's OpenGL context does not share textures with the "
                                    "rendering screen's"));
            } else if (! live) {
                // Paused: the renderer may play on for other screens and
                // writes its images again in turn, so an image held here
                // would change under the mirror.  Nothing of it is drawn
                // (the plugin shows a still it took before, SceneMirror.qml).
                has = true;
            } else if (QSGTexture* w = wrapper(info.texture, info.textureSize)) {
                tex  = w;
                name = info.texture;
                has  = true;
            } else {
                fail(QStringLiteral("Qt could not wrap the rendering screen's texture"));
            }
        }
        setFiltering(smooth ? QSGTexture::Linear : QSGTexture::Nearest);
        setRect(rect);
        if (name != 0) {
            const QSize ts = info.textureSize;
            setSourceRect(QRectF(sourceRect.x() * ts.width(),
                                 sourceRect.y() * ts.height(),
                                 sourceRect.width() * ts.width(),
                                 sourceRect.height() * ts.height()));
        } else {
            setSourceRect(QRectF(0, 0, 1, 1));
        }
        if (texture() != tex) setTexture(tex);
        if (name == 0 && ! m_wrappers.empty()) dropWrappers();
        markDirty(DirtyGeometry | DirtyMaterial);
        m_shown = name;
        if (has != m_reportedFrame.load() || info.itemSize != m_reportedSize) {
            m_reportedFrame.store(has);
            m_reportedSize = info.itemSize;
            Q_EMIT shown(has, m_reportedSize);
        }
    }

public slots:
    // QQuickWindow::beforeRendering, on the scene graph thread: a frame the
    // renderer has withdrawn since the last sync is not drawn again.
    void dropWithdrawn() {
        if (! m_frame || m_shown == 0) return;
        if (m_frame->current().texture != 0) return;
        setTexture(m_empty);
        markDirty(DirtyMaterial);
        m_shown = 0;
    }

signals:
    // The frame changed: the item is to be synced again.
    void redraw();
    // What is drawn now, and the rendering item's size with it.
    void shown(bool hasFrame, const QSizeF& sourceItemSize);
    void failed(const QString& reason);

private:
    void detach() {
        if (m_frame && m_reader >= 0) m_frame->removeReader(m_reader);
        m_reader = -1;
    }
    void dropWrappers() {
        for (auto& w : m_wrappers) delete w.second;
        m_wrappers.clear();
    }
    QSGTexture* wrapper(unsigned name, QSize size) {
        auto it = m_wrappers.find(name);
        if (it != m_wrappers.end()) {
            if (it->second->textureSize() == size) return it->second;
            // The name was reused for another size: a new renderer's texture.
            // The node may be drawing the old wrapper: off it first.
            if (texture() == it->second) setTexture(m_empty);
            // Counted for the tests (SceneMirror::wrapperCounts): a wrapper
            // deleted while the node still held it crashes only now and then
            // (the next wrapper usually gets the same address), so a plain
            // build does not show the mistake by itself.
            g_wrappersReplaced.fetch_add(1);
            if (texture() == it->second) g_wrappersDeletedInUse.fetch_add(1);
            delete it->second;
            m_wrappers.erase(it);
        }
        QSGTexture* w = QNativeInterface::QSGOpenGLTexture::fromNative(name, m_window, size);
        if (w != nullptr) m_wrappers.emplace(name, w);
        return w;
    }
    void fail(const QString& reason) {
        if (m_failed) return;
        m_failed = true;
        Q_EMIT failed(reason);
    }

    QQuickWindow*                             m_window;
    QSGTexture*                               m_empty { nullptr };
    std::shared_ptr<SharedFrame>              m_frame;
    int                                       m_reader { -1 };
    unsigned                                  m_shown { 0 };
    std::unordered_map<unsigned, QSGTexture*> m_wrappers;
    std::atomic<bool>                         m_live { true };
    bool                                      m_failed { false };
    std::atomic<bool>                         m_reportedFrame { false };
    QSizeF                                    m_reportedSize;
    // The renderer's side reaches the node only through this (see
    // RenderCallbackGuard.hpp): its wake can come from any thread, and once
    // more after the node is gone.
    std::shared_ptr<wek::qml_helper::CallbackGuard<MirrorNode>> m_guard {
        std::make_shared<wek::qml_helper::CallbackGuard<MirrorNode>>(this)
    };
};

SceneMirror::SceneMirror(QQuickItem* parent): QQuickItem(parent) { setFlag(ItemHasContents, true); }

SceneMirror::~SceneMirror() = default;

void SceneMirror::setFrameKey(const QString& key) {
    if (key == m_frameKey) return;
    m_frameKey = key;
    m_frame    = key.isEmpty() ? nullptr : SharedFrameRegistry::instance().acquire(key);
    update();
    Q_EMIT frameKeyChanged();
}

void SceneMirror::setSourceRect(const QRectF& rect) {
    if (rect == m_sourceRect) return;
    m_sourceRect = rect;
    update();
    Q_EMIT sourceRectChanged();
}

void SceneMirror::setLive(bool live) {
    if (live == m_live) return;
    m_live = live;
    update();
    Q_EMIT liveChanged();
}

qreal SceneMirror::frameAspect() const {
    if (m_sourceItemSize.width() <= 0 || m_sourceItemSize.height() <= 0) return 0;
    return m_sourceItemSize.width() / m_sourceItemSize.height();
}

void SceneMirror::onShown(bool hasFrame, const QSizeF& sourceItemSize) {
    if (sourceItemSize != m_sourceItemSize) {
        m_sourceItemSize = sourceItemSize;
        Q_EMIT frameAspectChanged();
    }
    if (hasFrame != m_hasFrame) {
        m_hasFrame = hasFrame;
        Q_EMIT hasFrameChanged();
    }
}

void SceneMirror::onFailed(const QString& reason) {
    if (! m_error.isEmpty()) return;
    m_error = reason;
    qCWarning(wekdeMirror).noquote() << "SceneMirror:" << reason;
    Q_EMIT failed(reason);
}

QPair<int, int> SceneMirror::wrapperCounts() {
    return { g_wrappersReplaced.load(), g_wrappersDeletedInUse.load() };
}

int SceneMirror::probeSync() {
    const int before = m_syncs;
    update();
    return before;
}

QSGNode* SceneMirror::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    // The GUI thread is blocked while this runs: the count is its own.
    m_syncs++;
    auto*       node = static_cast<MirrorNode*>(oldNode);
    const auto* rif  = window() ? window()->rendererInterface() : nullptr;
    if (rif == nullptr || ! CanShareFramesWith(rif->graphicsApi())) {
        // Queued: this runs on the scene graph thread.
        QMetaObject::invokeMethod(
            this,
            [this]() {
                onFailed(QStringLiteral("Qt Quick is not rendering through OpenGL"));
            },
            Qt::QueuedConnection);
        delete node;
        return nullptr;
    }
    if (! node) {
        node = new MirrorNode(window());
        connect(node, &MirrorNode::redraw, this, &QQuickItem::update, Qt::QueuedConnection);
        connect(node, &MirrorNode::shown, this, &SceneMirror::onShown, Qt::QueuedConnection);
        connect(node, &MirrorNode::failed, this, &SceneMirror::onFailed, Qt::QueuedConnection);
        connect(window(),
                &QQuickWindow::beforeRendering,
                node,
                &MirrorNode::dropWithdrawn,
                Qt::DirectConnection);
    }
    node->sync(m_frame, m_live, boundingRect(), m_sourceRect, smooth());
    return node;
}

std::optional<QPointF> SceneMirror::sourcePoint(const QPointF& point) const {
    return MirrorPointToSource(point, QSizeF(width(), height()), m_sourceRect, m_sourceItemSize);
}

QPointF SceneMirror::mapToSource(const QPointF& point) const {
    return sourcePoint(point).value_or(QPointF());
}

// The rendering item gets the event as if the pointer were over the same part
// of the picture on its own screen.  It must be alive and in a window; it is
// the GUI thread's, like this item.
void SceneMirror::forwardMouse(QMouseEvent* event) {
    auto* source = m_frame ? qobject_cast<QQuickItem*>(m_frame->sourceItem()) : nullptr;
    const auto at = sourcePoint(event->position());
    if (source == nullptr || source == this || ! at) return;
    QMouseEvent copy(event->type(),
                     *at,
                     event->globalPosition(),
                     event->button(),
                     event->buttons(),
                     event->modifiers());
    QCoreApplication::sendEvent(source, &copy);
}

void SceneMirror::mousePressEvent(QMouseEvent* event) { forwardMouse(event); }
void SceneMirror::mouseReleaseEvent(QMouseEvent* event) { forwardMouse(event); }
void SceneMirror::mouseMoveEvent(QMouseEvent* event) { forwardMouse(event); }

void SceneMirror::hoverMoveEvent(QHoverEvent* event) {
    auto* source = m_frame ? qobject_cast<QQuickItem*>(m_frame->sourceItem()) : nullptr;
    const auto at  = sourcePoint(event->position());
    const auto old = sourcePoint(event->oldPosF());
    if (source == nullptr || source == this || ! at) return;
    QHoverEvent copy(event->type(), *at, event->globalPosition(), old.value_or(*at), event->modifiers());
    QCoreApplication::sendEvent(source, &copy);
}

} // namespace scenebackend

#include "SceneMirror.moc"
