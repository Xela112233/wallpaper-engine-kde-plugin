// The shared frame (we-manager clone patch, ShareRenderer): one window's
// texture drawn in another window, and the pointer's way back.
//
// SharedFrame.hpp and SceneMirror are the real ones.  The renderer is not: a
// FakeSource item in the first window stands in for SceneBackend.cpp's
// TextureNode.  Like it, it has three textures whose names it keeps, writes
// them again in turn, one per frame, and publishes the one just written
// (publish, setItemSize, withdraw before the textures are deleted).  Unlike
// it, the textures are plain OpenGL ones written from the same context and
// flushed before they are published: the renderer's are Vulkan memory,
// written by another thread with nothing to wait for.  So what runs here is
// everything from the published texture name on: the registry and its
// lifetime, the mirror's wrapper for the name in its own window, its
// redraws, its source rectangle, a paused mirror, a name that comes back at
// another size, the still a paused screen shows (Qt's ShaderEffectSource of
// the mirror, as backend/SceneMirror.qml makes it), a frame that goes away, a
// context that does not share, and the pointer mapped into the rendering item.  A mirror sampling an image
// while the renderer writes it (tearing) cannot be shown here.
//
// The window cases need Qt Quick on OpenGL (Mesa's software GL under Xvfb
// does); elsewhere they are skipped and say so.  WEK_TEST_NO_SHARE=1 runs the
// process without Qt::AA_ShareOpenGLContexts, for the case of contexts that
// do not share.
#include <QtTest>

#include <QtGui/QGuiApplication>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>
#include <QtQuick/QSGSimpleRectNode>

#include "SceneMirror.hpp"
#include "SharedFrame.hpp"

using namespace scenebackend;

namespace
{

// What the renderer's item does for a mirror, without a renderer: three
// textures written in turn with two colours (left half, right half), the one
// just written published under `frame`.
class FakeSource : public QQuickItem {
    Q_OBJECT
public:
    explicit FakeSource(QQuickItem* parent = nullptr): QQuickItem(parent) {
        setFlag(ItemHasContents, true);
        setAcceptedMouseButtons(Qt::LeftButton);
        setAcceptHoverEvents(true);
    }
    std::shared_ptr<SharedFrame> frame;
    QColor                       left { Qt::red }, right { Qt::red };
    bool                         withdraw { false };
    // The next frame is the texture published last, made again at 16x8: a
    // name that comes back at another size.
    bool otherSize { false };
    // The pointer events that arrived, as "type x,y".
    QStringList events;
    void        redraw() { update(); }

signals:
    void published();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override {
        auto* node = static_cast<QSGSimpleRectNode*>(old);
        if (! node) node = new QSGSimpleRectNode(boundingRect(), Qt::black);
        node->setRect(boundingRect());
        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        if (ctx == nullptr || ! frame) return node;
        QOpenGLFunctions* gl = ctx->functions();
        if (withdraw) {
            // As ~TextureNode: the readers drop the name, then it is deleted.
            frame->withdraw();
            if (! m_texs.isEmpty()) gl->glDeleteTextures(GLsizei(m_texs.size()), m_texs.constData());
            m_texs.clear();
            Q_EMIT published();
            return node;
        }
        int w = 8, h = 4;
        if (m_texs.isEmpty()) {
            // As the renderer's three images: made once, their names kept.
            m_texs.resize(3);
            gl->glGenTextures(3, m_texs.data());
            for (GLuint t : std::as_const(m_texs)) {
                gl->glBindTexture(GL_TEXTURE_2D, t);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            }
        }
        GLuint tex = 0;
        if (otherSize && m_next > 0) {
            tex = m_texs[(m_next - 1) % 3];
            w   = 16;
            h   = 8;
        } else {
            tex = m_texs[m_next++ % 3];
        }
        QVector<uchar> px(w * h * 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const QColor c   = x < w / 2 ? left : right;
                uchar*       p   = px.data() + (y * w + x) * 4;
                p[0]             = uchar(c.red());
                p[1]             = uchar(c.green());
                p[2]             = uchar(c.blue());
                p[3]             = 255;
            }
        gl->glBindTexture(GL_TEXTURE_2D, tex);
        if (otherSize)
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.constData());
        else
            gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.constData());
        gl->glBindTexture(GL_TEXTURE_2D, 0);
        gl->glFinish(); // the other window's context sees the pixels from here on
        frame->setItemSize(QSizeF(width(), height()));
        frame->publish(tex, QSize(w, h), ctx->shareGroup());
        Q_EMIT published();
        return node;
    }
    void mousePressEvent(QMouseEvent* e) override { note("press", e->position()); }
    void mouseReleaseEvent(QMouseEvent* e) override { note("release", e->position()); }
    void mouseMoveEvent(QMouseEvent* e) override { note("move", e->position()); }
    void hoverMoveEvent(QHoverEvent* e) override { note("hover", e->position()); }

private:
    void note(const char* what, QPointF at) {
        events << QStringLiteral("%1 %2,%3").arg(QLatin1String(what)).arg(at.x()).arg(at.y());
    }
    QVector<GLuint> m_texs;
    int             m_next { 0 };
};

QColor pixel(QQuickWindow& w, double fx, double fy) {
    const QImage img = w.grabWindow();
    return img.pixelColor(int(img.width() * fx), int(img.height() * fy));
}
bool near(const QColor& a, const QColor& b) {
    return qAbs(a.red() - b.red()) < 24 && qAbs(a.green() - b.green()) < 24 && qAbs(a.blue() - b.blue()) < 24;
}

} // namespace

class TstSharedFrame : public QObject {
    Q_OBJECT

    static bool sharing() { return ! qEnvironmentVariableIsSet("WEK_TEST_NO_SHARE"); }

    // Two windows on OpenGL, or the reason there are none.
    struct Rig {
        QQuickWindow source, mirrorWindow;
        FakeSource*  src { nullptr };
        SceneMirror* mirror { nullptr };
    };
    QString setUp(Rig& r, const QString& key) {
        for (QQuickWindow* w : { &r.source, &r.mirrorWindow }) {
            w->setColor(Qt::white);
            w->resize(160, 80);
            w->show();
        }
        if (! QTest::qWaitForWindowExposed(&r.source) || ! QTest::qWaitForWindowExposed(&r.mirrorWindow))
            return QStringLiteral("the windows were not shown");
        if (r.source.rendererInterface()->graphicsApi() != QSGRendererInterface::OpenGL)
            return QStringLiteral("Qt Quick does not render through OpenGL here");
        r.src = new FakeSource(r.source.contentItem());
        r.src->setSize(QSizeF(160, 80));
        r.src->frame = SharedFrameRegistry::instance().acquire(key);
        r.src->frame->setSourceItem(r.src);
        r.mirror = new SceneMirror(r.mirrorWindow.contentItem());
        r.mirror->setSize(QSizeF(160, 80));
        r.mirror->setSmooth(false);
        r.mirror->setFrameKey(key);
        return QString();
    }
    static bool publish(Rig& r) {
        QSignalSpy spy(r.src, &FakeSource::published);
        r.src->redraw();
        return spy.wait(3000);
    }

private slots:
    // ---- no window needed ---------------------------------------------------
    void nothing_exists_until_a_key_is_asked_for() {
        // First in this file: the registry is made by the first acquire().
        QVERIFY(! SharedFrameRegistry::exists());
        SceneMirror idle; // an item without a key asks for nothing
        QVERIFY(! SharedFrameRegistry::exists());
        QCOMPARE(idle.hasFrame(), false);
    }

    void the_last_holder_gone_frees_the_frame() {
        auto& reg = SharedFrameRegistry::instance();
        QVERIFY(SharedFrameRegistry::exists());
        QCOMPARE(reg.frames(), 0);
        auto renderer = reg.acquire("k1");
        auto reader   = reg.acquire("k1");
        QCOMPARE(renderer.get(), reader.get());
        QVERIFY(reg.acquire("k2").get() != renderer.get()); // held by no one: gone again
        QCOMPARE(reg.frames(), 1);
        renderer->publish(7, QSize(4, 4), this);
        renderer.reset();
        QCOMPARE(reader->current().texture, 7u); // a reader keeps it alive
        std::weak_ptr<SharedFrame> watch = reader;
        reader.reset();
        QVERIFY(watch.expired());
        QCOMPARE(reg.frames(), 0);
        QCOMPARE(reg.acquire("k1")->current().texture, 0u); // a new frame, not the old one's texture
    }

    void readers_are_woken_by_each_frame_and_by_its_end() {
        SharedFrame frame;
        int         a = 0, b = 0;
        const int   ra = frame.addReader([&a] { a++; });
        frame.addReader([&b] { b++; });
        QCOMPARE(frame.readers(), 2);
        frame.publish(3, QSize(8, 4), this);
        QCOMPARE(a, 1);
        QCOMPARE(b, 1);
        const SharedFrameInfo first = frame.current();
        QCOMPARE(first.texture, 3u);
        QCOMPARE(first.textureSize, QSize(8, 4));
        QCOMPARE(first.shareGroup, static_cast<const void*>(this));
        frame.removeReader(ra);
        frame.publish(4, QSize(8, 4), this);
        QCOMPARE(a, 1); // gone: not woken again
        QCOMPARE(b, 2);
        QVERIFY(frame.current().serial > first.serial);
        frame.withdraw();
        QCOMPARE(b, 3);
        QCOMPARE(frame.current().texture, 0u);
        QCOMPARE(frame.current().shareGroup, static_cast<const void*>(nullptr));
    }

    void frames_nobody_takes_count_as_a_stall() {
        SharedFrame frame;
        QCOMPARE(frame.stalledMs(1000), 0);
        // Rendered and taken, frame after frame: no stall.
        for (qint64 t = 1000; t < 3000; t += 33) {
            frame.noteRendered(t);
            frame.publish(1, QSize(4, 4), this, t + 5);
            QCOMPARE(frame.stalledMs(t + 6), 0);
        }
        // The renderer goes on, the window takes nothing.
        for (qint64 t = 3000; t <= 9000; t += 33) frame.noteRendered(t);
        QVERIFY(frame.stalledMs(9010) >= 5900);
        // A frame taken ends it.
        frame.publish(1, QSize(4, 4), this, 9020);
        QCOMPARE(frame.stalledMs(9030), 0);
        // A renderer that draws nothing (paused) is not stalled.
        frame.noteRendered(9100);
        QVERIFY(frame.stalledMs(9500) > 0);
        QCOMPARE(frame.stalledMs(12000), 0);
    }

    void a_point_of_the_mirror_maps_into_the_rendering_item() {
        const QSizeF src(1920, 1080);
        // The same shape, the whole frame: the same place, scaled.
        auto p = MirrorPointToSource({ 480, 270 }, { 960, 540 }, { 0, 0, 1, 1 }, src);
        QVERIFY(p.has_value());
        QCOMPARE(*p, QPointF(960, 540));
        // A portrait screen showing the middle third of the frame's width
        // (crop): its left edge is a third into the rendering item.
        p = MirrorPointToSource({ 0, 0 }, { 360, 1080 }, { 1.0 / 3, 0, 1.0 / 3, 1 }, src);
        QVERIFY(qAbs(p->x() - 640) < 1e-6 && qAbs(p->y()) < 1e-6);
        p = MirrorPointToSource({ 360, 1080 }, { 360, 1080 }, { 1.0 / 3, 0, 1.0 / 3, 1 }, src);
        QVERIFY(qAbs(p->x() - 1280) < 1e-6 && qAbs(p->y() - 1080) < 1e-6);
        // Fit: the item is the letterboxed frame; a point above it (in the
        // bar) lies above the rendering item, not clamped onto it.
        p = MirrorPointToSource({ 100, -50 }, { 800, 450 }, { 0, 0, 1, 1 }, src);
        QCOMPARE(*p, QPointF(240, -120));
        // Nothing to map into yet.
        QVERIFY(! MirrorPointToSource({ 1, 1 }, { 800, 450 }, { 0, 0, 1, 1 }, QSizeF()).has_value());
        QVERIFY(! MirrorPointToSource({ 1, 1 }, QSizeF(), { 0, 0, 1, 1 }, src).has_value());
    }

    // ---- two windows ----------------------------------------------------------
    void a_second_window_draws_the_first_ones_frame() {
        if (! sharing()) QSKIP("this run is the one without shared contexts");
        Rig r;
        const QString why = setUp(r, "two-windows");
        if (! why.isEmpty()) QSKIP(qPrintable(why));
        QSignalSpy failed(r.mirror, &SceneMirror::failed);
        QCOMPARE(r.mirror->hasFrame(), false);
        QVERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::white)); // nothing yet: the window's own colour

        r.src->left = r.src->right = QColor(Qt::red);
        QVERIFY(publish(r));
        QTRY_VERIFY(r.mirror->hasFrame());
        QTRY_VERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::red));
        QCOMPARE(r.mirror->frameAspect(), 2.0);

        // The next frame, with no call on the mirror: it follows.
        r.src->left = r.src->right = QColor(Qt::green);
        QVERIFY(publish(r));
        QTRY_VERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::green));

        // Half the frame only (what a crop or a span hands a screen).
        r.src->left  = QColor(Qt::red);
        r.src->right = QColor(Qt::blue);
        QVERIFY(publish(r));
        QTRY_VERIFY(near(pixel(r.mirrorWindow, 0.25, 0.5), Qt::red));
        QVERIFY(near(pixel(r.mirrorWindow, 0.75, 0.5), Qt::blue));
        r.mirror->setSourceRect(QRectF(0.5, 0, 0.5, 1));
        QTRY_VERIFY(near(pixel(r.mirrorWindow, 0.25, 0.5), Qt::blue));
        QVERIFY(near(pixel(r.mirrorWindow, 0.75, 0.5), Qt::blue));

        // Paused, it draws nothing of the renderer's: the renderer plays on
        // for other screens and writes its three images again in turn, so an
        // image held here would change under the mirror.  (The plugin shows
        // a still it took before the pause, SceneMirror.qml.)
        r.mirror->setLive(false);
        QTRY_VERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::white));
        QCOMPARE(r.mirror->hasFrame(), true); // the frame is there; it is not drawn
        // New frames do not make its window draw, and when the window draws
        // for another reason (the grab here, a desktop icon there), nothing
        // of the images written since shows, the one it showed last included.
        QSignalSpy drawn(&r.mirrorWindow, &QQuickWindow::frameSwapped);
        while (drawn.wait(300)) {} // the sync that takes the pause, and what was still being drawn
        drawn.clear();
        for (const QColor& c : { QColor(Qt::green), QColor(Qt::yellow), QColor(Qt::magenta), QColor(Qt::cyan) }) {
            r.src->left = r.src->right = c;
            QVERIFY(publish(r));
        }
        QTest::qWait(300);
        QCOMPARE(drawn.count(), 0);
        QVERIFY(near(pixel(r.mirrorWindow, 0.25, 0.5), Qt::white));
        QVERIFY(near(pixel(r.mirrorWindow, 0.75, 0.5), Qt::white));
        // Playing again, the newest frame.
        r.mirror->setLive(true);
        QTRY_VERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::cyan));
        r.src->left = r.src->right = QColor(Qt::green);
        QVERIFY(publish(r));
        QTRY_VERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::green));

        // The renderer goes: the mirror draws nothing, and says so.
        r.src->withdraw = true;
        QVERIFY(publish(r));
        QTRY_VERIFY(! r.mirror->hasFrame());
        QTRY_VERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::white));
        QCOMPARE(failed.count(), 0);
        QCOMPARE(r.src->frame->readers(), 1);
        delete r.mirror;
        r.mirror = nullptr;
        // Its node goes with the next sync of its window.
        r.mirrorWindow.update();
        QTRY_COMPARE(r.src->frame->readers(), 0);
    }

    void a_still_taken_before_the_pause_keeps_the_picture_while_the_renderer_writes_on() {
        if (! sharing()) QSKIP("this run is the one without shared contexts");
        Rig r;
        const QString why = setUp(r, "still");
        if (! why.isEmpty()) QSKIP(qPrintable(why));
        // What backend/SceneMirror.qml does for a paused screen, with the
        // same Qt item: a ShaderEffectSource takes one picture of the mirror
        // and hides it; then the mirror is stopped.
        QQmlEngine    engine;
        QQmlComponent comp(&engine);
        comp.setData("import QtQuick\nShaderEffectSource { live: false; hideSource: true }", QUrl());
        std::unique_ptr<QQuickItem> still(qobject_cast<QQuickItem*>(comp.create()));
        QVERIFY2(still != nullptr, qPrintable(comp.errorString()));
        still->setParentItem(r.mirrorWindow.contentItem());
        still->setSize(r.mirror->size());

        r.src->left = r.src->right = QColor(Qt::red);
        QVERIFY(publish(r));
        QTRY_VERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::red));
        QSignalSpy taken(still.get(), SIGNAL(scheduledUpdateCompleted()));
        still->setProperty("sourceItem", QVariant::fromValue<QQuickItem*>(r.mirror));
        QVERIFY(QMetaObject::invokeMethod(still.get(), "scheduleUpdate"));
        QTRY_VERIFY(taken.count() > 0);
        r.mirror->setLive(false);
        // The renderer plays on for other screens: every one of its three
        // images is written again, the one this screen showed too.
        for (const QColor& c : { QColor(Qt::green), QColor(Qt::yellow), QColor(Qt::magenta), QColor(Qt::cyan) }) {
            r.src->left = r.src->right = c;
            QVERIFY(publish(r));
        }
        QTest::qWait(200);
        // The paused screen still shows the frame it was paused on, whenever its window draws.
        QVERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::red));
        QVERIFY(near(pixel(r.mirrorWindow, 0.1, 0.9), Qt::red));
        // And when the renderer goes altogether.
        r.src->withdraw = true;
        QVERIFY(publish(r));
        QTest::qWait(100);
        QVERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::red));
        // Playing again: the mirror itself, with the newest frame.
        r.src->withdraw = false;
        r.src->left = r.src->right = QColor(Qt::blue);
        QVERIFY(publish(r));
        still->setProperty("sourceItem", QVariant::fromValue<QQuickItem*>(nullptr));
        still->setVisible(false);
        r.mirror->setLive(true);
        QTRY_VERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::blue));
    }

    void a_paused_mirror_learns_that_the_frame_went_and_that_one_came() {
        if (! sharing()) QSKIP("this run is the one without shared contexts");
        Rig r;
        const QString why = setUp(r, "paused-told");
        if (! why.isEmpty()) QSKIP(qPrintable(why));
        // A paused item follows no frame, but it is told when the frame goes
        // and when there is one again (the plugin waits for hasFrame to take
        // its still), and draws none of it.
        r.mirror->setLive(false);
        QVERIFY(publish(r));
        QTRY_VERIFY(r.mirror->hasFrame());
        r.src->withdraw = true;
        QVERIFY(publish(r));
        QTRY_VERIFY(! r.mirror->hasFrame());
        r.src->withdraw = false;
        QVERIFY(publish(r));
        QTRY_VERIFY(r.mirror->hasFrame());
        QVERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::white));
    }

    void a_texture_name_that_comes_back_at_another_size_is_wrapped_again() {
        if (! sharing()) QSKIP("this run is the one without shared contexts");
        Rig r;
        const QString why = setUp(r, "other-size");
        if (! why.isEmpty()) QSKIP(qPrintable(why));
        r.src->left = r.src->right = QColor(Qt::red);
        QVERIFY(publish(r));
        QTRY_VERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::red));
        // The mirror is drawing the wrapper it made for this name at 8x4.
        // The name is published again at 16x8 (a renderer made again got it
        // back): the old wrapper goes, and must not while the node holds it.
        const QPair<int, int> before = SceneMirror::wrapperCounts();
        r.src->otherSize = true;
        r.src->left      = QColor(Qt::blue);
        r.src->right     = QColor(Qt::yellow);
        QVERIFY(publish(r));
        QTRY_VERIFY(near(pixel(r.mirrorWindow, 0.25, 0.5), Qt::blue));
        QVERIFY(near(pixel(r.mirrorWindow, 0.75, 0.5), Qt::yellow));
        // The old wrapper was replaced, and the node was off it by then.  A
        // wrapper deleted under the node is a use after free that this build
        // does not crash on (the new wrapper usually takes its address), so
        // the node counts it.
        const QPair<int, int> after = SceneMirror::wrapperCounts();
        QCOMPARE(after.first - before.first, 1);
        QCOMPARE(after.second - before.second, 0);
    }

    void a_window_that_is_drawn_counts_syncs_and_one_that_is_not_does_not() {
        if (! sharing()) QSKIP("this run is the one without shared contexts");
        Rig r;
        const QString why = setUp(r, "probe");
        if (! why.isEmpty()) QSKIP(qPrintable(why));
        // What the plugin asks a mirror while the renderer is stalled.
        int before = r.mirror->probeSync();
        QTRY_VERIFY(r.mirror->probeSync() > before);
        // Paused, the same: the question is about the window, not the frame.
        r.mirror->setLive(false);
        before = r.mirror->probeSync();
        QTRY_VERIFY(r.mirror->probeSync() > before);
        // A window that is not shown is not drawn: asked again and again, the count stays.
        r.mirrorWindow.hide();
        QTest::qWait(200);
        before = r.mirror->probeSync();
        for (int i = 0; i < 5; i++) {
            QTest::qWait(60);
            QCOMPARE(r.mirror->probeSync(), before);
        }
        r.mirrorWindow.show();
        QTRY_VERIFY(r.mirror->probeSync() > before);
    }

    void the_pointer_on_a_mirror_reaches_the_rendering_item() {
        if (! sharing()) QSKIP("this run is the one without shared contexts");
        Rig r;
        const QString why = setUp(r, "pointer");
        if (! why.isEmpty()) QSKIP(qPrintable(why));
        // The mirror: a quarter the size, showing the right half of the frame.
        r.mirror->setSize(QSizeF(40, 40));
        r.mirror->setSourceRect(QRectF(0.5, 0, 0.5, 1));
        QVERIFY(publish(r));
        QTRY_VERIFY(r.mirror->hasFrame());
        QCOMPARE(r.mirror->mapToSource(QPointF(20, 10)), QPointF(120, 20));

        const QPointF at(20, 10), global(500, 500);
        QMouseEvent   press(QEvent::MouseButtonPress, at, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent   move(QEvent::MouseMove, QPointF(40, 40), global, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent   release(QEvent::MouseButtonRelease, QPointF(0, 0), global, Qt::LeftButton, Qt::NoButton,
                              Qt::NoModifier);
        QHoverEvent   hover(QEvent::HoverMove, QPointF(10, 30), global, QPointF(10, 30));
        for (QEvent* e : std::initializer_list<QEvent*> { &press, &move, &release, &hover })
            QCoreApplication::sendEvent(r.mirror, e);
        QCOMPARE(r.src->events,
                 QStringList({ "press 120,20", "move 160,80", "release 80,0", "hover 100,60" }));

        // With the rendering item gone, the events go nowhere.
        r.src->frame->setSourceItem(nullptr);
        QCoreApplication::sendEvent(r.mirror, &press);
        QCOMPARE(r.src->events.size(), 4);
    }

    void a_window_whose_context_does_not_share_says_so_and_draws_nothing() {
        if (sharing()) QSKIP("this run has shared contexts; the test runs again with WEK_TEST_NO_SHARE=1");
        Rig r;
        const QString why = setUp(r, "no-share");
        if (! why.isEmpty()) QSKIP(qPrintable(why));
        QSignalSpy failed(r.mirror, &SceneMirror::failed);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("SceneMirror: .*does not share textures"));
        QVERIFY(publish(r));
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed[0][0].toString().contains("does not share textures"));
        QCOMPARE(r.mirror->hasFrame(), false);
        QVERIFY(! r.mirror->error().isEmpty());
        QVERIFY(near(pixel(r.mirrorWindow, 0.5, 0.5), Qt::white));
        // Said once, however many frames follow.
        QVERIFY(publish(r));
        QTest::qWait(100);
        QCOMPARE(failed.count(), 1);
    }
};

int main(int argc, char** argv) {
    if (! qEnvironmentVariableIsSet("WEK_TEST_NO_SHARE"))
        QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts); // as plasmashell does
    QGuiApplication app(argc, argv);
    TstSharedFrame  test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_sharedframe.moc"
