#include "MouseGrabber.hpp"
#include <iostream>
#include <QLoggingCategory>
#include <QCoreApplication>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QtGlobal>

using namespace wekde;

MouseGrabber::MouseGrabber(QQuickItem* parent): QQuickItem(parent) {
    // Every button reaches mousePressEvent; only the left one is accepted
    // there, so the others still go on to Plasma (the context menu on the
    // right button, paste on the middle one).
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setAcceptTouchEvents(true);
}

bool MouseGrabber::forceCapture() const { return m_forceCapture; }

QQuickItem* MouseGrabber::target() const { return m_target; }

void MouseGrabber::setForceCapture(bool value) {
    if (value == m_forceCapture) return;
    m_forceCapture = value;
    if (value) {
        grabMouse();
    } else {
        ungrabMouse();
    }
    Q_EMIT forceCaptureChanged();
}

void MouseGrabber::setTarget(QQuickItem* item) {
    if (item == m_target) return;
    // Restore the OLD target's accepted-button mask to whatever the QML
    // author set it to before the grabber attached. QPointer auto-nulls if
    // the underlying QQuickItem was destroyed, so the if-check is also a
    // dead-pointer guard.
    if (m_target) m_target->setAcceptedMouseButtons(m_prevTargetButtons);
    m_target = item;
    if (m_target) {
        // Snapshot BEFORE overwrite so a chain setTarget(A)->setTarget(B)->
        // setTarget(A) returns A and B to their original masks.
        m_prevTargetButtons = m_target->acceptedMouseButtons();
        m_target->setAcceptedMouseButtons(Qt::LeftButton);
    } else {
        m_prevTargetButtons = Qt::NoButton;
    }
    // A press the old target saw is not ended on the new one.
    m_pressForwarded = false;
    m_touchId        = -1;
    Q_EMIT targetChanged();
}

bool MouseGrabber::targetTakes(Qt::MouseButton button) const {
    // Only the right and middle buttons, besides the left: Back, Forward and
    // the other extra buttons stay Plasma's.  A WebEngineView takes every
    // button, and Chromium goes back or forward in the page's history on
    // those.
    if (! (button & (Qt::RightButton | Qt::MiddleButton))) return false;
    return m_target && (m_prevTargetButtons & button);
}

void MouseGrabber::sendSynthetic(QEvent::Type type, const QPointF& pos, const QPointF& globalPos,
                                 Qt::MouseButton button, Qt::MouseButtons buttons) {
    if (! m_target) return;
    QMouseEvent temp(type, mapToItem(m_target, pos), globalPos, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(m_target, &temp);
}

void MouseGrabber::endPress() {
    if (! m_pressForwarded) return;
    m_pressForwarded = false;
    sendSynthetic(
        QEvent::MouseButtonRelease, m_lastPos, m_lastGlobalPos, Qt::LeftButton, Qt::NoButton);
}

void MouseGrabber::mouseUngrabEvent() {
    // After a release this does nothing.  Before one (another item took the
    // mouse), the target gets the release it would otherwise never see: a
    // scene's input.cursorLeftDown, a page's mouseup.
    endPress();
    if (m_forceCapture) grabMouse();
}

void MouseGrabber::sendEvent(QObject* target, QEvent* event) {
    QCoreApplication::sendEvent(target, event);
}

void MouseGrabber::sendMouseEvent(QMouseEvent* event) {
    if (m_target) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        QMouseEvent temp(event->type(),
                         mapToItem(m_target, event->position()),
                         event->globalPosition(),
                         event->button(),
                         event->buttons(),
                         event->modifiers());
#else
        QMouseEvent temp(event->type(),
                         mapToItem(m_target, event->localPos()),
                         event->screenPos(),
                         event->button(),
                         event->buttons(),
                         event->modifiers());
#endif
        QCoreApplication::sendEvent(m_target, &temp);
    }
}

void MouseGrabber::sendHoverEvent(QHoverEvent* event) {
    if (m_target) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        auto        newPos = mapToItem(m_target, event->position());
        QHoverEvent temp(event->type(),
                         newPos,
                         event->globalPosition(),
                         mapToItem(m_target, event->oldPosF()),
                         event->modifiers());
#else
        QHoverEvent temp(event->type(),
                         mapToItem(m_target, event->posF()),
                         mapToItem(m_target, event->oldPosF()),
                         event->modifiers());
#endif
        QCoreApplication::sendEvent(m_target, &temp);
    }
}

void MouseGrabber::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        // A right or middle click: a target that takes the button gets it as
        // a press and its release (the real release goes to whoever takes
        // the press next, Plasma), and the press goes on to Plasma.  Other
        // buttons only go on to Plasma (targetTakes).
        if (targetTakes(event->button())) {
            sendMouseEvent(event);
            sendSynthetic(QEvent::MouseButtonRelease,
                          event->position(),
                          event->globalPosition(),
                          event->button(),
                          event->buttons() & ~event->button());
        }
        event->ignore();
        return;
    }
    sendMouseEvent(event);
    m_pressForwarded = m_target != nullptr;
    m_lastPos        = event->position();
    m_lastGlobalPos  = event->globalPosition();
    // need accept press to receive release
    // this break long press on desktop
    event->accept();
}

void MouseGrabber::mouseMoveEvent(QMouseEvent* event) {
    sendMouseEvent(event);
    if (m_pressForwarded) {
        m_lastPos       = event->position();
        m_lastGlobalPos = event->globalPosition();
    }
    event->ignore();
}

void MouseGrabber::mouseReleaseEvent(QMouseEvent* event) {
    // While the left button holds the grab, other buttons' releases come here
    // too; their click was already sent whole at the press.
    if (event->button() == Qt::LeftButton) {
        sendMouseEvent(event);
        m_pressForwarded = false;
    }
    event->ignore();
}

void MouseGrabber::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton || targetTakes(event->button())) sendMouseEvent(event);
    event->ignore();
}

void MouseGrabber::wheelEvent(QWheelEvent* event) {
    if (m_target) {
        QWheelEvent temp(mapToItem(m_target, event->position()),
                         event->globalPosition(),
                         event->pixelDelta(),
                         event->angleDelta(),
                         event->buttons(),
                         event->modifiers(),
                         event->phase(),
                         event->inverted(),
                         event->source());
        QCoreApplication::sendEvent(m_target, &temp);
    }
    // Ignored, so Plasma's own wheel action on the desktop still runs.
    event->ignore();
}

void MouseGrabber::touchEvent(QTouchEvent* event) {
    if (event->type() == QEvent::TouchCancel) {
        touchUngrabEvent();
        event->accept();
        return;
    }
    // The first finger down is the left button until it lifts; other fingers
    // are not forwarded.
    const QEventPoint* point = nullptr;
    for (const QEventPoint& p : event->points()) {
        if (m_touchId < 0 ? p.state() == QEventPoint::Pressed : p.id() == m_touchId) {
            point = &p;
            break;
        }
    }
    if (point) {
        switch (point->state()) {
        case QEventPoint::Pressed:
            m_touchId = point->id();
            sendSynthetic(QEvent::MouseButtonPress,
                          point->position(),
                          point->globalPosition(),
                          Qt::LeftButton,
                          Qt::LeftButton);
            m_pressForwarded = m_target != nullptr;
            m_lastPos        = point->position();
            m_lastGlobalPos  = point->globalPosition();
            break;
        case QEventPoint::Updated:
            sendSynthetic(QEvent::MouseMove,
                          point->position(),
                          point->globalPosition(),
                          Qt::NoButton,
                          Qt::LeftButton);
            m_lastPos       = point->position();
            m_lastGlobalPos = point->globalPosition();
            break;
        case QEventPoint::Released:
            sendSynthetic(QEvent::MouseButtonRelease,
                          point->position(),
                          point->globalPosition(),
                          Qt::LeftButton,
                          Qt::NoButton);
            m_touchId        = -1;
            m_pressForwarded = false;
            break;
        default: break;
        }
    }
    // Accepted, like the left press, so the rest of the touch comes here.
    event->accept();
}

void MouseGrabber::touchUngrabEvent() {
    m_touchId = -1;
    endPress();
}

void MouseGrabber::hoverMoveEvent(QHoverEvent* event) {
    // Sample log once per ~120 hover events so we can tell from journalctl
    // whether hover is actually reaching the wallpaper.  Demoted from
    // qInfo to qDebug so the steady drip doesn't flood plasmashell logs
    // in normal operation; users debugging mouse-hook issues can enable
    // QT_LOGGING_RULES="*.debug=true" to re-enable.
    static int s_hover_log = 0;
    if (++s_hover_log % 120 == 1) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        auto pos = event->position();
#else
        auto pos = event->posF();
#endif
        qDebug("[WEK] MouseGrabber::hoverMoveEvent pos=(%.1f,%.1f) target=%p",
               pos.x(),
               pos.y(),
               (void*)m_target);
    }
    sendHoverEvent(event);
    event->ignore();
}
