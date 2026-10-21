// SPDX-License-Identifier: GPL-2.0-or-later
// Unit tests for the effect feedback plan (wallpaper::feedback::Analyse), the
// image moves between frames (wallpaper::vulkan::MoveSlotImages) and the
// setting's text form (SceneQuality), we-manager fluid-targets patch.
// Header-only code, no Vulkan and no scene: what a whole fixture scene becomes
// is the renderer's own test_EffectFeedback.cpp.

#include <QtTest/QtTest>

#include "EffectFeedback.hpp"
#include "SceneQuality.hpp"
#include "VulkanRender/include/VulkanRender/FeedbackMoves.hpp"

#include <optional>

using namespace wallpaper;
using feedback::Analyse;
using feedback::Fbo;
using feedback::Image;
using feedback::Plan;
using feedback::Step;

namespace
{

Step pass(std::vector<std::string> reads, std::string target = {}) {
    return { Step::Kind::Pass, std::move(reads), std::move(target) };
}
Step copy(std::string source, std::string target) { return { Step::Kind::Copy, { std::move(source) }, std::move(target) }; }
Step swap(std::string source, std::string target) { return { Step::Kind::Swap, { std::move(source) }, std::move(target) }; }

Image img(const char* fbo, bool twin = false) { return { fbo, twin }; }

using Moves = std::vector<std::pair<Image, Image>>;

struct Slot {
    std::optional<int> image;
    std::uint32_t      generation { 0 };
};

} // namespace

class TestEffectFeedback : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void anEffectThatDrawsBeforeItSamplesIsNotFeedback();
    void aSwappedPairKeepsBothAndExchangesThem();
    void aPassSamplingItsOwnTargetGetsATwin();
    void aCopyBackKeepsOneTargetAndMovesNothing();
    void anEvenNumberOfSwapsLeavesTheImagesInPlace();
    void aTargetNeverDrawnIsNotFeedback();
    void aSwapOfDifferentShapesStaysACopy();
    void imagesChangePlacesAndThePassesNotice();
    void theSettingIsNamedInTheTextOnlyWhenOn();
};

void TestEffectFeedback::anEffectThatDrawsBeforeItSamplesIsNotFeedback() {
    // A blur: A drawn, B drawn from A, the two swapped, A shown.
    const Plan p = Analyse({ { "A", 0 }, { "B", 0 } },
                           { pass({ "previous" }, "A"), pass({ "A" }, "B"), swap("A", "B"), pass({ "previous", "A" }) });
    QVERIFY(! p.feedback);
    QVERIFY(p.persistent.empty());
    QVERIFY(p.moves.empty());
}

void TestEffectFeedback::aSwappedPairKeepsBothAndExchangesThem() {
    const Plan p = Analyse({ { "A", 0 }, { "B", 0 } },
                           { pass({ "previous", "A" }, "B"), swap("A", "B"), pass({ "previous", "A" }) });
    QVERIFY(p.feedback);
    QCOMPARE(p.persistent, (std::set<Image> { img("A"), img("B") }));
    QVERIFY(p.twins.empty());
    // The first pass samples A's image and draws B's; the swap is no pass;
    // the last pass, which names A, samples what was just drawn.
    QVERIFY(p.steps[0].reads[1].img == img("A"));
    QVERIFY(p.steps[0].target.img == img("B"));
    QVERIFY(p.steps[1].dropped);
    QVERIFY(p.steps[2].reads[1].img == img("B"));
    QVERIFY(! p.steps[0].reads[0].image); // "previous" is not a target of the effect
    QCOMPARE(p.moves, (Moves { { img("A"), img("B") }, { img("B"), img("A") } }));
}

void TestEffectFeedback::aPassSamplingItsOwnTargetGetsATwin() {
    const Plan p = Analyse({ { "X", 0 } }, { pass({ "previous", "X" }, "X"), pass({ "X" }) });
    QVERIFY(p.feedback);
    QCOMPARE(p.twins, (std::set<Image> { img("X", true) }));
    QCOMPARE(p.persistent, (std::set<Image> { img("X"), img("X", true) }));
    QVERIFY(p.steps[0].reads[1].img == img("X"));
    QVERIFY(p.steps[0].target.img == img("X", true));
    QVERIFY(p.steps[1].reads[0].img == img("X", true));
    QCOMPARE(p.moves, (Moves { { img("X"), img("X", true) }, { img("X", true), img("X") } }));
}

void TestEffectFeedback::aCopyBackKeepsOneTargetAndMovesNothing() {
    const Plan p = Analyse({ { "Keep", 0 }, { "Work", 0 } },
                           { pass({ "previous", "Keep" }, "Work"), pass({ "Work" }), copy("Work", "Keep") });
    QVERIFY(p.feedback);
    QCOMPARE(p.persistent, (std::set<Image> { img("Keep") }));
    QVERIFY(p.moves.empty());
    QVERIFY(! p.steps[2].dropped);
    QVERIFY(p.steps[2].reads[0].img == img("Work"));
    QVERIFY(p.steps[2].target.img == img("Keep"));
}

void TestEffectFeedback::anEvenNumberOfSwapsLeavesTheImagesInPlace() {
    // Two rounds of "sample P, draw Q, swap": each round samples what the one
    // before drew, and after two the names are where they started.
    const Plan p = Analyse({ { "P", 0 }, { "Q", 0 } },
                           { pass({ "P" }, "Q"), swap("P", "Q"), pass({ "P" }, "Q"), swap("P", "Q"), pass({ "P" }) });
    QVERIFY(p.feedback);
    QVERIFY(p.steps[0].reads[0].img == img("P"));
    QVERIFY(p.steps[0].target.img == img("Q"));
    QVERIFY(p.steps[2].reads[0].img == img("Q"));
    QVERIFY(p.steps[2].target.img == img("P"));
    QVERIFY(p.steps[4].reads[0].img == img("P"));
    QCOMPARE(p.persistent, (std::set<Image> { img("P"), img("Q") }));
    QVERIFY(p.moves.empty());
}

void TestEffectFeedback::aTargetNeverDrawnIsNotFeedback() {
    const Plan p = Analyse({ { "Empty", 0 }, { "A", 0 } }, { pass({ "previous" }, "A"), pass({ "A", "Empty" }) });
    QVERIFY(! p.feedback);
}

void TestEffectFeedback::aSwapOfDifferentShapesStaysACopy() {
    // Half-size Small and full-size Big cannot change places; the swap stays
    // the copy from source to target it was, and Small, sampled first and
    // drawn by nothing, carries nothing over.
    Plan p = Analyse({ { "Small", 1 }, { "Big", 0 } }, { pass({ "Small" }, "Big"), swap("Big", "Small"), pass({ "Small" }) });
    QVERIFY(p.steps[1].copies);
    QVERIFY(! p.steps[1].dropped);
    QVERIFY(p.feedback); // the copy draws into Small after it was sampled
    QCOMPARE(p.persistent, (std::set<Image> { img("Small") }));
    QVERIFY(p.moves.empty());
    // The same with an undeclared name.
    p = Analyse({ { "A", 0 } }, { pass({ "previous" }, "A"), swap("A", "previous"), pass({ "previous" }) });
    QVERIFY(p.steps[1].copies);
    QVERIFY(! p.feedback);
}

void TestEffectFeedback::imagesChangePlacesAndThePassesNotice() {
    using vulkan::MoveSlotImages;
    using vulkan::MovesArePermutation;
    std::vector<std::optional<int>> scratch;
    Slot a { 1 }, b { 2 }, c { 3 };

    std::vector<std::pair<Slot*, Slot*>> pair { { &a, &b }, { &b, &a } };
    QVERIFY(MovesArePermutation<Slot>(pair));
    MoveSlotImages<Slot, std::optional<int>>(pair, scratch);
    QCOMPARE(*a.image, 2);
    QCOMPARE(*b.image, 1);
    QCOMPARE(a.generation, 1u);
    QCOMPARE(b.generation, 1u);
    MoveSlotImages<Slot, std::optional<int>>(pair, scratch);
    QCOMPARE(*a.image, 1);
    QCOMPARE(*b.image, 2);

    // Three in a ring: a takes b's, b takes c's, c takes a's.
    std::vector<std::pair<Slot*, Slot*>> ring { { &a, &b }, { &b, &c }, { &c, &a } };
    QVERIFY(MovesArePermutation<Slot>(ring));
    MoveSlotImages<Slot, std::optional<int>>(ring, scratch);
    QCOMPARE(*a.image, 2);
    QCOMPARE(*b.image, 3);
    QCOMPARE(*c.image, 1);
    QCOMPARE(c.generation, 1u);

    // A set of moves that would lose an image is refused.
    std::vector<std::pair<Slot*, Slot*>> lossy { { &a, &b }, { &c, &b } };
    QVERIFY(! MovesArePermutation<Slot>(lossy));
    std::vector<std::pair<Slot*, Slot*>> open { { &a, &b } };
    QVERIFY(! MovesArePermutation<Slot>(open));
}

void TestEffectFeedback::theSettingIsNamedInTheTextOnlyWhenOn() {
    SceneQuality q;
    QVERIFY(! q.feedback);
    QCOMPARE(QString::fromStdString(q.ToString()), QStringLiteral("textures=0;targetWidth=0;targetHeight=0"));
    QVERIFY(SceneQuality::FromString("textures=1").feedback == false);
    QVERIFY(SceneQuality::FromString("textures=1;feedback=0").feedback == false);
    QVERIFY(SceneQuality::FromString("textures=1;feedback=2").feedback == false);
    q = SceneQuality::FromString("textures=1;feedback=1");
    QVERIFY(q.feedback);
    QCOMPARE(q.textures, SceneQuality::Textures::Performance);
    QCOMPARE(QString::fromStdString(q.ToString()), QStringLiteral("textures=1;targetWidth=0;targetHeight=0;feedback=1"));
    QCOMPARE(SceneQuality::FromString(q.ToString()), q);
    QVERIFY(! (q == SceneQuality::FromString("textures=1")));
    QVERIFY(feedback::IsFloatFormat("rg1616f"));
    QVERIFY(feedback::IsFloatFormat("rgba16161616f"));
    QVERIFY(! feedback::IsFloatFormat("rgba8888"));
    QVERIFY(! feedback::IsFloatFormat("rgba_backbuffer"));
    QVERIFY(! feedback::IsFloatFormat(""));
}

QTEST_GUILESS_MAIN(TestEffectFeedback)
#include "tst_effectfeedback.moc"
