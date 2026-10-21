// SPDX-License-Identifier: GPL-2.0-only
// Unit tests for wallpaper::SceneQuality and LeadingMipsToDrop (we-manager
// texture-quality patch): which leading mip levels a texture leaves out per
// texture resolution and render target, the textures that never get
// smaller, and the property's text form.

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

QTEST_GUILESS_MAIN(TestSceneQuality)
#include "tst_scenequality.moc"
