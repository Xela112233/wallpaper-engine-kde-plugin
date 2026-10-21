#pragma once
#include <QQuickItem>
#include <QEvent>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QPointer>
#include <QTouchEvent>
#include <QWheelEvent>

namespace wekde
{

// Sits under the desktop's icons and widgets (main.qml doHookMouse) and hands
// the pointer to the wallpaper's mouse target.  The left button is taken (its
// press accepted, so the release comes back here) and forwarded.  The wheel
// and the right and middle buttons are forwarded as copies and the originals
// ignored, so Plasma's own desktop actions (the context menu, wheel actions)
// still run; a right or middle press reaches only a target that accepts that
// button itself (a web page does, a scene takes the left button only, as
// Wallpaper Engine's cursor events do).  Touch follows its first point as the
// left button.
class MouseGrabber : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool forceCapture READ forceCapture WRITE setForceCapture NOTIFY forceCaptureChanged)
    Q_PROPERTY(QQuickItem* target READ target WRITE setTarget NOTIFY targetChanged)

public:
    MouseGrabber(QQuickItem* parent = nullptr);
    virtual ~MouseGrabber() override {};

    bool        forceCapture() const;
    QQuickItem* target() const;

    void setForceCapture(bool);
    void setTarget(QQuickItem*);

    Q_INVOKABLE void sendEvent(QObject*, QEvent*);

protected:
    void mouseUngrabEvent() override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void hoverMoveEvent(QHoverEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void touchEvent(QTouchEvent*) override;
    void touchUngrabEvent() override;

signals:
    void forceCaptureChanged();
    void targetChanged();

private:
    void                 sendMouseEvent(QMouseEvent*);
    void                 sendHoverEvent(QHoverEvent*);
    // A mouse event made here (the end of a lost press, touch as the left
    // button), at `pos` in this item's coordinates.
    void                 sendSynthetic(QEvent::Type type, const QPointF& pos, const QPointF& globalPos,
                                       Qt::MouseButton button, Qt::MouseButtons buttons);
    // Whether `button` is the right or middle one and the target wants it
    // (its own mask, from before setTarget).  Back, Forward and the other
    // extra buttons are never forwarded.
    bool                 targetTakes(Qt::MouseButton button) const;
    // A left press went to the target and its release has not: a lost grab
    // ends it with a release, so the target is not left with the button down.
    void                 endPress();
    bool                 m_forceCapture { false };
    QPointer<QQuickItem> m_target { nullptr };
    // Snapshot of m_target->acceptedMouseButtons() captured BEFORE the grabber
    // overwrites it with Qt::LeftButton. Restored on detach (target change or
    // null clear). NoButton matches the QQuickItem default, so a never-attached
    // grabber's cache is harmless.
    Qt::MouseButtons m_prevTargetButtons { Qt::NoButton };
    bool             m_pressForwarded { false };
    QPointF          m_lastPos;
    QPointF          m_lastGlobalPos;
    // The touch point followed as the left button; -1 when none is down.
    int              m_touchId { -1 };
};
} // namespace wekde
