// SPDX-License-Identifier: GPL-2.0-only
// Unit tests for wallpaper::SceneQuality and LeadingMipsToDrop (we-manager
// texture-quality patch): which leading mip levels a texture leaves out per
// texture resolution and render target, the textures that never get
// smaller, and the property's text form, with the scene-quality patch's
// switches in it.

#include <QtTest>

#include "SceneQuality.hpp"

using namespace wallpaper;

namespace
{
SceneQuality quality(SceneQuality::Textures t, int w = 0, int h = 0) {
    SceneQuality q;
    q.textures     = t;
    q.targetWidth  = w;
    q.targetHeight = h;
    return q;
}
constexpr auto Full        = SceneQuality::Textures::Full;
constexpr auto Performance = SceneQuality::Textures::Performance;
constexpr auto Automatic   = SceneQuality::Textures::Automatic;
} // namespace

class TestSceneQuality : public QObject {
    Q_OBJECT

private slots:
    void fullLeavesEveryLevel();
    void performanceLeavesOutOneLevel();
    void automaticStopsAtTheScreensSize();
    void automaticComparesTheImageNotThePadding();
    void aTextureKeepsAtLeastOneLevel();
    void spritesAndVideosAreExempt();
    void theTextFormRoundTripsAndToleratesTheUnknown();
    void theSwitchesAreNamedOnlyWhenSet();
};

void TestSceneQuality::fullLeavesEveryLevel() {
    QCOMPARE(LeadingMipsToDrop(quality(Full), 13, 4096, 4096, 3840, 2160, false), 0);
}

void TestSceneQuality::performanceLeavesOutOneLevel() {
    // 4K goes to about 1080p, a small texture halves too.
    QCOMPARE(LeadingMipsToDrop(quality(Performance), 13, 4096, 4096, 3840, 2160, false), 1);
    QCOMPARE(LeadingMipsToDrop(quality(Performance), 9, 256, 256, 256, 256, false), 1);
}

void TestSceneQuality::automaticStopsAtTheScreensSize() {
    // A 3840x2160 image: on a 1080p screen one level goes (1920x1080 still
    // covers it); on a 4K screen none; a 720p screen two would leave 960x540,
    // smaller than 1280x720, so one.
    QCOMPARE(LeadingMipsToDrop(quality(Automatic, 1920, 1080), 12, 3840, 2160, 3840, 2160, false), 1);
    QCOMPARE(LeadingMipsToDrop(quality(Automatic, 3840, 2160), 12, 3840, 2160, 3840, 2160, false), 0);
    QCOMPARE(LeadingMipsToDrop(quality(Automatic, 1280, 720), 12, 3840, 2160, 3840, 2160, false), 1);
    // An 8K image on a 1080p screen: two levels.
    QCOMPARE(LeadingMipsToDrop(quality(Automatic, 1920, 1080), 14, 7680, 4320, 7680, 4320, false), 2);
    // Smaller than the screen (a sprite, a cloud, a mask): never.
    QCOMPARE(LeadingMipsToDrop(quality(Automatic, 1920, 1080), 11, 1024, 1024, 1024, 1024, false), 0);
    // No target known yet: nothing.
    QCOMPARE(LeadingMipsToDrop(quality(Automatic), 12, 3840, 2160, 3840, 2160, false), 0);
}

void TestSceneQuality::automaticComparesTheImageNotThePadding() {
    // 1920x1080 in a 2048x2048 power-of-two texture, on a 1080p screen: the
    // image is the screen's size, so nothing goes, whatever the padding.
    QCOMPARE(LeadingMipsToDrop(quality(Automatic, 1920, 1080), 12, 2048, 2048, 1920, 1080, false), 0);
    // No map size (0) or one larger than the level: the level itself.
    QCOMPARE(LeadingMipsToDrop(quality(Automatic, 1920, 1080), 12, 3840, 2160, 0, 0, false), 1);
}

void TestSceneQuality::aTextureKeepsAtLeastOneLevel() {
    // Mipmaps turned off (one level, as Wallpaper Engine exempts them), and
    // fewer levels than the skip: the last one stays.
    QCOMPARE(LeadingMipsToDrop(quality(Performance), 1, 4096, 4096, 4096, 4096, false), 0);
    QCOMPARE(LeadingMipsToDrop(quality(Automatic, 640, 360), 2, 7680, 4320, 7680, 4320, false), 1);
    QCOMPARE(LeadingMipsToDrop(quality(Performance), 0, 4096, 4096, 4096, 4096, false), 0);
}

void TestSceneQuality::spritesAndVideosAreExempt() {
    QCOMPARE(LeadingMipsToDrop(quality(Performance), 8, 4096, 4096, 512, 512, true), 0);
    QCOMPARE(LeadingMipsToDrop(quality(Automatic, 640, 360), 8, 4096, 4096, 4096, 4096, true), 0);
}

void TestSceneQuality::theTextFormRoundTripsAndToleratesTheUnknown() {
    const SceneQuality q = quality(Automatic, 2560, 1440);
    QCOMPARE(SceneQuality::FromString(q.ToString()), q);
    QCOMPARE(SceneQuality::FromString("textures=1"), quality(Performance));
    // A key a newer page sends, a value out of range, junk: the rest stands.
    QCOMPARE(SceneQuality::FromString("volumetrics=2;textures=1;targetWidth=-5;;x"), quality(Performance));
    QCOMPARE(SceneQuality::FromString("textures=9"), quality(Full));
    QCOMPARE(SceneQuality::FromString(""), quality(Full));
    QCOMPARE(QString::fromLatin1(SceneQuality::TexturesName(Automatic)), QStringLiteral("automatic"));
}

// The scene-quality patch's switches (volumetric lighting, reflections, no
// anti-aliasing on 2D scenes): with none set the property's text is the one
// from before the patch, so a renderer that gets it decides as before; each
// one set is a different SceneQuality, which is what makes SceneWallpaper
// reload the scene.
void TestSceneQuality::theSwitchesAreNamedOnlyWhenSet() {
    const SceneQuality none;
    QCOMPARE(QString::fromStdString(none.ToString()), QStringLiteral("textures=0;targetWidth=0;targetHeight=0"));
    QCOMPARE(SceneQuality::FromString("textures=0;targetWidth=0;targetHeight=0"), none);
    QVERIFY(none.volumetrics == SceneQuality::Volumetrics::Scene);
    QVERIFY(none.reflections == SceneQuality::Reflections::Scene);
    QVERIFY(none.flatMsaa == SceneQuality::FlatMsaa::AsSet);

    SceneQuality q = quality(Performance);
    q.volumetrics  = SceneQuality::Volumetrics::Off;
    QCOMPARE(QString::fromStdString(q.ToString()),
             QStringLiteral("textures=1;targetWidth=0;targetHeight=0;volumetrics=0"));
    QVERIFY(! (q == quality(Performance)));
    QCOMPARE(SceneQuality::FromString(q.ToString()), q);

    q.reflections = SceneQuality::Reflections::Off;
    q.flatMsaa      = SceneQuality::FlatMsaa::Off;
    QCOMPARE(QString::fromStdString(q.ToString()),
             QStringLiteral("textures=1;targetWidth=0;targetHeight=0;volumetrics=0;reflections=0;flatMsaa=1"));
    QCOMPARE(SceneQuality::FromString(q.ToString()), q);

    // What the settings page sends for one switch, and the defaults written out.
    SceneQuality mirrorless;
    mirrorless.reflections = SceneQuality::Reflections::Off;
    QCOMPARE(SceneQuality::FromString("textures=0;reflections=0"), mirrorless);
    QCOMPARE(SceneQuality::FromString("textures=0;volumetrics=1;reflections=1;flatMsaa=0"), none);
    // A value no build defines leaves the switch as the scene asks.
    QCOMPARE(SceneQuality::FromString("reflections=7;flatMsaa=2;volumetrics=-1"), none);
}

QTEST_GUILESS_MAIN(TestSceneQuality)
#include "tst_scenequality.moc"
