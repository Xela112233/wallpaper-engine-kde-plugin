#pragma once

// Shows another window's scene frame (we-manager clone patch; the KDE
// plugin's ShareRenderer).  See SharedFrame.hpp for how the frame gets here.
//
// The item draws `sourceRect` of the frame over its whole area; QML places
// and sizes the item (crop, fit, stretch).  It has no renderer: no Vulkan
// device, no textures of its own, no scripts and no sound.
//
// Pointer events it is sent (the plugin's MouseGrabber sends its target the
// desktop's) go on to the item that renders, at the point of that item where
// the same part of the picture is (MirrorPointToSource, SharedFrame.hpp).
//
// What can go wrong is reported once through failed(), and nothing is drawn:
// Qt Quick not rendering through OpenGL, or this window's OpenGL context not
// sharing textures with the renderer's.  The plugin then loads a renderer of
// its own for this screen.

#include <QtCore/QPair>
#include <QtCore/QRectF>
#include <QtCore/QString>
#include <QtGui/QHoverEvent>
#include <QtGui/QMouseEvent>
#include <QtQuick/QQuickItem>

#include <memory>
#include <optional>

namespace scenebackend
{

class SharedFrame;

class SceneMirror : public QQuickItem {
    Q_OBJECT
    // The frame to show: the rendering SceneViewer's shareKey.  Empty: none.
    Q_PROPERTY(QString frameKey READ frameKey WRITE setFrameKey NOTIFY frameKeyChanged)
    // The part of the frame this item shows, in fractions of the frame
    // (x, y, width, height from 0 to 1); the whole frame by default.
    Q_PROPERTY(QRectF sourceRect READ sourceRect WRITE setSourceRect NOTIFY sourceRectChanged)
    // false (paused): the item draws nothing and asks for no redraws.  The
    // renderer may play on for other screens and writes its images again in
    // turn, so a mirror cannot hold one of them as a still picture; the
    // plugin takes its own still before it pauses the item (SceneMirror.qml).
    Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged)
    // The renderer has a frame for this item (drawn while live).
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)
    // Width over height of the frame on its own screen; 0 before a frame.
    Q_PROPERTY(qreal frameAspect READ frameAspect NOTIFY frameAspectChanged)
    // Why nothing can be drawn; empty while all is well.
    Q_PROPERTY(QString error READ error NOTIFY failed)

public:
    explicit SceneMirror(QQuickItem* parent = nullptr);
    ~SceneMirror() override;

    QString frameKey() const { return m_frameKey; }
    void    setFrameKey(const QString&);
    QRectF  sourceRect() const { return m_sourceRect; }
    void    setSourceRect(const QRectF&);
    bool    live() const { return m_live; }
    void    setLive(bool);
    bool    hasFrame() const { return m_hasFrame; }
    qreal   frameAspect() const;
    QString error() const { return m_error; }

    // Where a point of this item is in the rendering item (its coordinates),
    // or nothing before a frame.  What the forwarded pointer events carry.
    Q_INVOKABLE QPointF mapToSource(const QPointF& point) const;
    // Whether this item's window is being drawn: asks for one sync of the
    // item and returns how many it has had so far.  A caller that sees the
    // number go up between two calls knows the window draws; in a window
    // that is not drawn (its output off, or hidden) it stays.
    Q_INVOKABLE int probeSync();

    // For tests: how many texture wrappers of this process's mirrors were
    // replaced because their texture name came back at another size, and how
    // many of those were still the texture their node drew when they were
    // deleted.  The second is 0 unless the node is no longer taken off the
    // wrapper first, a use after free that only a sanitizer would show.
    static QPair<int, int> wrapperCounts();

signals:
    void frameKeyChanged();
    void sourceRectChanged();
    void liveChanged();
    void hasFrameChanged();
    void frameAspectChanged();
    void failed(const QString& reason);

protected:
    QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;
    void     mousePressEvent(QMouseEvent*) override;
    void     mouseReleaseEvent(QMouseEvent*) override;
    void     mouseMoveEvent(QMouseEvent*) override;
    void     hoverMoveEvent(QHoverEvent*) override;

private slots:
    // From the scene graph node (queued): what it draws now.
    void onShown(bool hasFrame, const QSizeF& sourceItemSize);
    void onFailed(const QString& reason);

private:
    std::optional<QPointF> sourcePoint(const QPointF& point) const;
    void                   forwardMouse(QMouseEvent*);

    QString                      m_frameKey;
    QRectF                       m_sourceRect { 0, 0, 1, 1 };
    bool                         m_live { true };
    bool                         m_hasFrame { false };
    int                          m_syncs { 0 };
    QSizeF                       m_sourceItemSize;
    QString                      m_error;
    std::shared_ptr<SharedFrame> m_frame;
};

} // namespace scenebackend
