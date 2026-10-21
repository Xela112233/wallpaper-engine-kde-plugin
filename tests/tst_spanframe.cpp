// One picture across three windows (we-manager span patch, SpanGroup): each
// window draws its own part of one texture, and the pointer of each lands
// where it is in the canvas.
//
// Real here: plugin/contents/ui/SpanCanvas.qml (the screen that renders),
// plugin/contents/ui/backend/SceneMirror.qml (the screens that show the
// frame), plugin/contents/ui/SpanPointer.qml (every screen's pointer),
// js/span.mjs, and the C++ SceneMirror and SharedFrame.  Not real:
// the renderer.  A FakeSource item stands in for the SceneViewer: SpanCanvas
// is handed it as CloneLink hands it the viewer, it fills its parent as
// backend/Scene.qml's viewer does, publishes one plain OpenGL texture as the
// frame and draws it over its own area, as the renderer's node draws its
// frame.  main.qml, CloneLink.qml and the table are not here either (the
// plugin's tst_main_span.qml runs those): the windows are given the canvas
// and their screens' rectangles directly.
//
// The desk is the one of tests/js/span.test.mjs at a tenth of its size: a
// 256x144 window between two 192x120 ones, tops aligned, so the canvas is
// 640x144.  The texture is ten columns of ten colours (64 canvas pixels
// each) with a white band over its lowest sixth (the 24 rows below the side
// windows): the left window must show columns 0 to 2, the middle one 3 to 6
// with the band, the right one 7 to 9, and the side windows no band.
//
// Needs Qt Quick on OpenGL (Mesa's software GL under Xvfb does) and the
// plugin's QML: WEK_UI_DIR in the environment, else the directory given at
// build time.  Skipped where either is missing, saying so.
#include <QtTest>

#include <QtGui/QGuiApplication>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>
#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QSGTexture>

#include "SceneMirror.hpp"
#include "SharedFrame.hpp"

using namespace scenebackend;

namespace
{

const QColor kColumns[10] = { QColor(255, 0, 0),   QColor(0, 255, 0),   QColor(0, 0, 255),   QColor(255, 255, 0),
                              QColor(255, 0, 255), QColor(0, 255, 255), QColor(128, 0, 0),   QColor(0, 128, 0),
                              QColor(0, 0, 128),   QColor(128, 128, 128) };

// The renderer's item, without a renderer: one 80x18 texture (a texel is 8x8
// canvas pixels), drawn over the item and published under `frame`.
class FakeSource : public QQuickItem {
    Q_OBJECT
public:
    explicit FakeSource(QQuickItem* parent = nullptr): QQuickItem(parent) {
        setFlag(ItemHasContents, true);
        setAcceptedMouseButtons(Qt::LeftButton);
        setAcceptHoverEvents(true);
    }
    std::shared_ptr<SharedFrame> frame;
    QStringList                  events;

signals:
    void published();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override {
        auto*           node = static_cast<QSGSimpleTextureNode*>(old);
        QOpenGLContext* ctx  = QOpenGLContext::currentContext();
        if (ctx == nullptr || ! frame) return node;
        const int w = 80, h = 18;
        if (! node) {
            QOpenGLFunctions* gl = ctx->functions();
            gl->glGenTextures(1, &m_tex);
            QVector<uchar> px(w * h * 4);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    const QColor c = y >= 15 ? QColor(Qt::white) : kColumns[x / 8];
                    uchar*       p = px.data() + (y * w + x) * 4;
                    p[0]           = uchar(c.red());
                    p[1]           = uchar(c.green());
                    p[2]           = uchar(c.blue());
                    p[3]           = 255;
                }
            gl->glBindTexture(GL_TEXTURE_2D, m_tex);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.constData());
            gl->glBindTexture(GL_TEXTURE_2D, 0);
            gl->glFinish(); // the other windows' contexts see the pixels from here on
            node = new QSGSimpleTextureNode;
            node->setTexture(QNativeInterface::QSGOpenGLTexture::fromNative(m_tex, window(), QSize(w, h)));
            node->setOwnsTexture(true);
            node->setFiltering(QSGTexture::Nearest);
        }
        node->setRect(boundingRect());
        frame->setItemSize(QSizeF(width(), height()));
        frame->publish(m_tex, QSize(w, h), ctx->shareGroup());
        Q_EMIT published();
        return node;
    }
    void mousePressEvent(QMouseEvent* e) override { note("press", e->position()); }
    void mouseReleaseEvent(QMouseEvent* e) override { note("release", e->position()); }
    void mouseMoveEvent(QMouseEvent* e) override { note("move", e->position()); }
    void hoverMoveEvent(QHoverEvent* e) override { note("hover", e->position()); }

private:
    void note(const char* what, QPointF at) {
        events << QStringLiteral("%1 %2,%3").arg(QLatin1String(what)).arg(qRound(at.x())).arg(qRound(at.y()));
    }
    GLuint m_tex { 0 };
};

bool near(const QColor& a, const QColor& b) {
    return qAbs(a.red() - b.red()) < 24 && qAbs(a.green() - b.green()) < 24 && qAbs(a.blue() - b.blue()) < 24;
}

QString uiDir() {
    QString dir = qEnvironmentVariable("WEK_UI_DIR");
#ifdef WEK_UI_DIR
    if (dir.isEmpty()) dir = QStringLiteral(WEK_UI_DIR);
#endif
    return dir;
}

// What a window's root stands in for: main.qml's background (the names
// backend/SceneMirror.qml reads), its CloneLink (`link`: the calls the
// backend makes), and the loaded backend.
const char* kHost = R"(
import QtQuick
Item {
    id: background
    property int displayMode: 1
    property bool pixelArt: true
    property string nowBackend: ""
    property int firstFrames: 0
    signal sig_backendFirstFrame(string name)
    onSig_backendFirstFrame: firstFrames += 1
    property QtObject link: QtObject {
        function mirrorCreated(item) {}
        function mirrorShown() {}
        function mirrorFailed(reason) { console.warn("mirror failed: " + reason); }
    }
    property var item: null
    function load(url, properties) {
        const comp = Qt.createComponent(url);
        if (comp.status !== Component.Ready) {
            console.warn(comp.errorString());
            return false;
        }
        const props = {};
        if (String(url).indexOf("SceneMirror.qml") >= 0) props.link = background.link;
        for (const k in properties) props[k] = properties[k];
        background.item = comp.createObject(background, props);
        return background.item !== null;
    }
}
)";

} // namespace

class TstSpanFrame : public QObject {
    Q_OBJECT

    struct Screen {
        QRectF                        rect; // in the desk's coordinates
        std::unique_ptr<QQuickWindow> window;
        QQuickItem*                   host { nullptr };
    };
    struct Desk {
        QQmlEngine  engine;
        Screen      left, mid, right;
        FakeSource* src { nullptr };
        QQuickItem* holder { nullptr };
    };

    // The three windows, each with its host; or the reason there are none.
    QString setUp(Desk& d) {
        const QString ui = uiDir();
        if (ui.isEmpty() || ! QFile::exists(ui + "/SpanCanvas.qml"))
            return QStringLiteral("the plugin's QML was not found (set WEK_UI_DIR to plugin/contents/ui)");
        d.left.rect  = QRectF(0, 0, 192, 120);
        d.mid.rect   = QRectF(192, 0, 256, 144);
        d.right.rect = QRectF(448, 0, 192, 120);
        for (Screen* s : { &d.left, &d.mid, &d.right }) {
            s->window = std::make_unique<QQuickWindow>();
            s->window->setColor(Qt::black);
            s->window->resize(s->rect.size().toSize());
            s->window->show();
        }
        for (Screen* s : { &d.left, &d.mid, &d.right })
            if (! QTest::qWaitForWindowExposed(s->window.get())) return QStringLiteral("the windows were not shown");
        if (d.mid.window->rendererInterface()->graphicsApi() != QSGRendererInterface::OpenGL)
            return QStringLiteral("Qt Quick does not render through OpenGL here");
        for (Screen* s : { &d.left, &d.mid, &d.right }) {
            QQmlComponent comp(&d.engine);
            comp.setData(kHost, QUrl::fromLocalFile(ui + "/tst_spanframe_host.qml"));
            s->host = qobject_cast<QQuickItem*>(comp.create());
            if (s->host == nullptr) return comp.errorString();
            s->host->setParentItem(s->window->contentItem());
            s->host->setSize(s->rect.size());
        }
        return QString();
    }
    // The middle window renders: SpanCanvas.qml in its host, the source in it.
    QString render(Desk& d, const QString& key, const QRectF& canvas) {
        QVariantMap props { { "canvas", canvas }, { "screen", d.mid.rect } };
        QVariant    ok;
        QMetaObject::invokeMethod(d.mid.host, "load", Q_RETURN_ARG(QVariant, ok),
                                  Q_ARG(QVariant, QUrl::fromLocalFile(uiDir() + "/SpanCanvas.qml")),
                                  Q_ARG(QVariant, props));
        if (! ok.toBool()) return QStringLiteral("SpanCanvas.qml did not load");
        d.holder = d.mid.host->property("item").value<QQuickItem*>();
        d.src    = new FakeSource(d.holder); // as CloneLink: viewer.parent = holder
        d.src->setSize(d.holder->size());    // as Scene.qml: the viewer fills its parent
        d.src->frame = SharedFrameRegistry::instance().acquire(key);
        d.src->frame->setSourceItem(d.src);
        return QString();
    }
    // A side window shows the frame: the real backend/SceneMirror.qml.
    static SceneMirror* mirror(Screen& s, const QString& key, const QRectF& canvas) {
        QVariantMap props { { "frameKey", key }, { "spanCanvas", canvas }, { "spanScreen", s.rect } };
        QVariant    ok;
        QMetaObject::invokeMethod(s.host, "load", Q_RETURN_ARG(QVariant, ok),
                                  Q_ARG(QVariant, QUrl::fromLocalFile(uiDir() + "/backend/SceneMirror.qml")),
                                  Q_ARG(QVariant, props));
        return ok.toBool() ? s.host->findChild<SceneMirror*>() : nullptr;
    }
    // A window's pointer item: the real SpanPointer.qml, made in the window's
    // host as CloneLink makes it in the backend, with what CloneLink gives it.
    static SceneMirror* pointer(Screen& s, const QString& key, const QRectF& canvas, double shape, double partX,
                                double partY) {
        QVariantMap props { { "frameKey", key }, { "canvas", canvas }, { "screen", s.rect },
                            { "shape", shape },  { "partX", partX },   { "partY", partY } };
        QVariant    ok;
        QMetaObject::invokeMethod(s.host, "load", Q_RETURN_ARG(QVariant, ok),
                                  Q_ARG(QVariant, QUrl::fromLocalFile(uiDir() + "/SpanPointer.qml")),
                                  Q_ARG(QVariant, props));
        return ok.toBool() ? qobject_cast<SceneMirror*>(s.host->property("item").value<QQuickItem*>()) : nullptr;
    }
    static bool publish(Desk& d) {
        QSignalSpy spy(d.src, &FakeSource::published);
        d.src->update();
        return spy.wait(3000);
    }
    // The colour at a point of a window (its own pixels).
    static QColor at(Screen& s, int x, int y) { return s.window->grabWindow().pixelColor(x, y); }
    // The window shows the canvas's columns from `first` on, one every 64
    // pixels, at the height `y`.
    static bool columns(Screen& s, int first, int count, int y) {
        const QImage img = s.window->grabWindow();
        for (int i = 0; i < count; i++)
            for (int dx : { 8, 32, 56 })
                if (! near(img.pixelColor(i * 64 + dx, y), kColumns[first + i])) return false;
        return true;
    }

private slots:
    void three_windows_show_one_picture_each_its_own_part() {
        Desk          d;
        QString why = setUp(d);
        if (! why.isEmpty()) QSKIP(qPrintable(why));
        const QRectF canvas(0, 0, 640, 144);
        why = render(d, "span-three", canvas);
        QVERIFY2(why.isEmpty(), qPrintable(why));
        // The rendering item is the whole canvas, 192 to the left of the middle window.
        QCOMPARE(d.holder->position(), QPointF(-192, 0));
        QCOMPARE(d.holder->size(), QSizeF(640, 144));
        SceneMirror* l = mirror(d.left, "span-three", canvas);
        SceneMirror* r = mirror(d.right, "span-three", canvas);
        QVERIFY(l != nullptr && r != nullptr);
        QSignalSpy failedL(l, &SceneMirror::failed), failedR(r, &SceneMirror::failed);
        QVERIFY(publish(d));
        QTRY_VERIFY(l->hasFrame() && r->hasFrame());

        // The middle window: columns 3 to 6, and the white band over its lowest sixth.
        QTRY_VERIFY(columns(d.mid, 3, 4, 60));
        QVERIFY(columns(d.mid, 3, 4, 4));
        QVERIFY(columns(d.mid, 3, 4, 116));
        QVERIFY(near(at(d.mid, 128, 124), Qt::white));
        QVERIFY(near(at(d.mid, 128, 140), Qt::white));
        // The left window: columns 0 to 2 over its whole height, no band.
        QTRY_VERIFY(columns(d.left, 0, 3, 60));
        QVERIFY(columns(d.left, 0, 3, 2));
        QVERIFY(columns(d.left, 0, 3, 117));
        // The right window: columns 7 to 9.
        QTRY_VERIFY(columns(d.right, 7, 3, 60));
        QVERIFY(columns(d.right, 7, 3, 117));
        // The parts: three and four tenths of the frame's width, five sixths of its height.
        QCOMPARE(l->size(), QSizeF(192, 120));
        QVERIFY(qAbs(l->sourceRect().width() - 0.3) < 1e-9 && qAbs(l->sourceRect().height() - 120.0 / 144) < 1e-9);
        QVERIFY(qAbs(r->sourceRect().x() - 0.7) < 1e-9);
        // Each side window reported its first frame once, as a backend does.
        QCOMPARE(d.left.host->property("firstFrames").toInt(), 1);
        QCOMPARE(failedL.count() + failedR.count(), 0);
    }

    void the_pointer_of_each_window_reaches_the_rendering_item_through_the_groups_item() {
        Desk          d;
        QString why = setUp(d);
        if (! why.isEmpty()) QSKIP(qPrintable(why));
        const QRectF canvas(0, 0, 640, 144);
        why = render(d, "span-pointer", canvas);
        QVERIFY2(why.isEmpty(), qPrintable(why));
        // The pointer's item of each window, the rendering one's too.  First with a frame
        // that shows the whole scene (Stretch): the item maps to the pointer's place.
        const double shape = 640.0 / 144;
        SceneMirror* pl = pointer(d.left, "span-pointer", canvas, shape, 1, 1);
        SceneMirror* pm = pointer(d.mid, "span-pointer", canvas, shape, 1, 1);
        SceneMirror* pr = pointer(d.right, "span-pointer", canvas, shape, 1, 1);
        QVERIFY(pl != nullptr && pm != nullptr && pr != nullptr);
        QCOMPARE(pm->size(), QSizeF(256, 144)); // the size of its window, whatever the frame's part
        QCOMPARE(pm->live(), false);
        QVERIFY(publish(d));
        // It knows the rendering item's size although it is not drawn (opacity 0) and follows
        // no frame ...
        QTRY_COMPARE(pr->mapToSource(QPointF(10, 20)), QPointF(458, 20));
        QTRY_COMPARE(pm->mapToSource(QPointF(10, 20)), QPointF(202, 20));
        QTRY_COMPARE(pl->mapToSource(QPointF(10, 20)), QPointF(10, 20));
        // ... and the renderer's frames do not make its window draw: the left window holds
        // nothing but its pointer's item so far.
        QSignalSpy drawn(d.left.window.get(), &QQuickWindow::frameSwapped);
        while (drawn.wait(300)) {} // what was still being drawn
        drawn.clear();
        for (int i = 0; i < 6; i++) QVERIFY(publish(d));
        QTest::qWait(300);
        // (None for the six frames; one at most for the item's own look every two seconds.)
        QVERIFY2(drawn.count() <= 1, qPrintable(QString::number(drawn.count())));
        // With the parts of the picture in the side windows: the pointer's items draw nothing
        // over them, every window still shows its part.
        SceneMirror* l = mirror(d.left, "span-pointer", canvas);
        SceneMirror* r = mirror(d.right, "span-pointer", canvas);
        QVERIFY(l != nullptr && r != nullptr);
        QVERIFY(publish(d));
        QTRY_VERIFY(l->hasFrame() && r->hasFrame());
        QTRY_VERIFY(columns(d.mid, 3, 4, 60));
        QVERIFY(columns(d.left, 0, 3, 60));
        QVERIFY(columns(d.right, 7, 3, 60));

        // The pointer at a point of each window, as main.qml's MouseGrabber sends it: an
        // event for the window's pointer item, in its coordinates, which are the window's.
        const QPointF p(10, 20), global(700, 700);
        QHoverEvent   hoverR(QEvent::HoverMove, p, global, p);
        QCoreApplication::sendEvent(pr, &hoverR);
        QMouseEvent pressL(QEvent::MouseButtonPress, p, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(pl, &pressL);
        QMouseEvent moveR(QEvent::MouseMove, QPointF(191, 119), global, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(pr, &moveR);
        QMouseEvent moveM(QEvent::MouseMove, p, global, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(pm, &moveM);
        // All four in the rendering item's coordinates, which are the canvas's: the right
        // window begins at 448, the left at 0, the middle at 192.
        QCOMPARE(d.src->events, QStringList({ "hover 458,20", "press 10,20", "move 639,119", "move 202,20" }));

        // Cover with a scene of which the canvas shows the middle 40 % of the height (a 16:9
        // scene on this desk): the renderer reads a position as a fraction of its item and
        // takes it for that fraction of the scene, so each row is handed over drawn towards
        // the middle: row y of the canvas as 144 x (0.5 + (y / 144 - 0.5) x 0.4).
        d.src->events.clear();
        for (SceneMirror* pad : { pl, pm, pr }) pad->setProperty("partY", 0.4);
        QTRY_COMPARE(pr->mapToSource(QPointF(10, 20)), QPointF(458, 72 + (20 - 72) * 0.4));
        QCoreApplication::sendEvent(pr, &hoverR); // (10, 20) of the right window
        QMouseEvent pressL2(QEvent::MouseButtonPress, QPointF(10, 100), global, Qt::LeftButton, Qt::LeftButton,
                            Qt::NoModifier);
        QCoreApplication::sendEvent(pl, &pressL2);
        QMouseEvent middle(QEvent::MouseMove, QPointF(128, 72), global, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(pm, &middle); // the middle of the desk
        QMouseEvent top(QEvent::MouseMove, QPointF(128, 0), global, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(pm, &top); // the top edge: 30 % down the scene
        QCOMPARE(d.src->events, QStringList({ "hover 458,51", "press 10,83", "move 320,72", "move 320,43" }));
        // A scene cut at the sides instead (a narrow canvas would do it): columns move in.
        d.src->events.clear();
        for (SceneMirror* pad : { pl, pm, pr }) {
            pad->setProperty("partY", 1.0);
            pad->setProperty("partX", 0.5);
        }
        QTRY_COMPARE(pl->mapToSource(QPointF(0, 60)), QPointF(160, 60));
        QMouseEvent side(QEvent::MouseMove, QPointF(64, 119), global, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(pr, &side); // canvas 512, 0.8 of its width: 0.65 of the item's
        QCOMPARE(d.src->events, QStringList({ "move 416,119" }));
        // The rendering item changes its size while frames flow (the alignment is set to Keep
        // aspect ratio: Scene.qml makes it the scene's shape, 512x144, 64 in).  The pointer's
        // item follows no frame, but it learns the new size: its own later sync brings it.
        for (SceneMirror* pad : { pl, pm, pr }) pad->setProperty("partX", 1.0);
        d.src->setSize(QSizeF(512, 144));
        d.src->setPosition(QPointF(64, 0));
        for (SceneMirror* pad : { pl, pm, pr }) pad->setProperty("shape", 512.0 / 144);
        QVERIFY(publish(d));
        QTRY_COMPARE(pl->mapToSource(QPointF(64, 60)), QPointF(0, 60));
        QTRY_COMPARE(pm->mapToSource(QPointF(128, 60)), QPointF(256, 60)); // canvas 320: the frame's middle
        QTRY_COMPARE(pr->mapToSource(QPointF(128, 60)), QPointF(512, 60)); // canvas 576: the frame's right edge
    }

    void a_frame_fitted_into_the_canvas_leaves_the_rest_of_each_window_alone() {
        Desk          d;
        QString why = setUp(d);
        if (! why.isEmpty()) QSKIP(qPrintable(why));
        const QRectF canvas(0, 0, 640, 144);
        why = render(d, "span-fit", canvas);
        QVERIFY2(why.isEmpty(), qPrintable(why));
        // Keep aspect ratio with a 32:9 scene: backend/Scene.qml makes its viewer 512x144 and
        // centres it in the canvas, 64 in from either end.
        d.src->setSize(QSizeF(512, 144));
        d.src->setPosition(QPointF(64, 0));
        SceneMirror* l = mirror(d.left, "span-fit", canvas);
        SceneMirror* r = mirror(d.right, "span-fit", canvas);
        QVERIFY(l != nullptr && r != nullptr);
        QVERIFY(publish(d));
        QTRY_VERIFY(l->hasFrame() && qAbs(l->frameAspect() - 512.0 / 144) < 1e-6);
        // The frame's ten columns are 51.2 wide now.  The left window's first 64 pixels are
        // not the frame's: the window's own colour; then the frame from its first column on.
        QTRY_COMPARE(l->position(), QPointF(64, 0));
        QCOMPARE(l->size(), QSizeF(128, 120));
        QTRY_VERIFY(near(at(d.left, 30, 60), Qt::black));
        QVERIFY(near(at(d.left, 64 + 25, 60), kColumns[0]));
        QVERIFY(near(at(d.left, 64 + 76, 60), kColumns[1]));
        // The right window: the frame's last columns, then its own colour from 128 on.
        QTRY_COMPARE(r->size(), QSizeF(128, 120));
        QTRY_VERIFY(near(at(d.right, 128 - 25, 60), kColumns[9]));
        QVERIFY(near(at(d.right, 160, 60), Qt::black));
        // The middle window shows the frame's middle: canvas 192 to 448 is frame 128 to 384.
        QTRY_VERIFY(near(at(d.mid, 100, 60), kColumns[int((192 + 100 - 64) / 51.2)]));
        // The pointer in the left window's bar, at (30, 20) of the window, is left of the
        // frame, not on its edge; the window's pointer item is given the frame's shape.
        SceneMirror* pl = pointer(d.left, "span-fit", canvas, 512.0 / 144, 1, 1);
        QVERIFY(pl != nullptr);
        QTRY_COMPARE(pl->mapToSource(QPointF(64, 120)), QPointF(0, 120));
        QMouseEvent bar(QEvent::MouseMove, QPointF(30, 20), QPointF(700, 700), Qt::NoButton, Qt::LeftButton,
                        Qt::NoModifier);
        QCoreApplication::sendEvent(pl, &bar);
        QCOMPARE(d.src->events, QStringList({ "move -34,20" }));
    }
};

int main(int argc, char** argv) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts); // as plasmashell does
    QGuiApplication app(argc, argv);
    qmlRegisterType<SceneMirror>("com.github.captsilver.wallpaperEngineKde", 1, 2, "SceneMirror");
    TstSpanFrame test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_spanframe.moc"
