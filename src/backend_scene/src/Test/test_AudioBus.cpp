// Process-singleton AudioBus — Scene + Web wallpapers share one
// AudioCapture + AudioAnalyzer pair instead of each spinning their own
// PulseAudio monitor stream + miniaudio thread + FFT.
//
// Tests run under WEK_TEST_AUDIO_NULL_CAPTURE=1 so the bus pretends the
// capture is active without actually opening PulseAudio/PipeWire (which
// would require a working sound server in the test environment).

#include <doctest.h>
#include "Audio/AudioBus.h"
#include "Audio/AudioAnalyzer.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
struct NullCaptureGuard {
    NullCaptureGuard() { setenv("WEK_TEST_AUDIO_NULL_CAPTURE", "1", 1); }
    ~NullCaptureGuard() {
        unsetenv("WEK_TEST_AUDIO_NULL_CAPTURE");
        // Drop any stale bus state from a previous test so the next test
        // sees a clean weak_ptr.  TEST_resetForNextCase blocks until the
        // background Process thread has joined.
        wallpaper::audio::AudioBus::TEST_resetForNextCase();
    }
};
} // namespace

// audio.json, the process-wide config the KDE plugin's we-manager side writes.
namespace
{
std::string writeConfig(const std::string& name, const std::string& text) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << text;
    return path.string();
}
struct ConfigFileGuard {
    explicit ConfigFileGuard(const std::string& path) { setenv("WEK_AUDIO_CONFIG", path.c_str(), 1); }
    ~ConfigFileGuard() { unsetenv("WEK_AUDIO_CONFIG"); }
};
} // namespace

TEST_CASE("audio.json: no file gives the built-in values") {
    const auto cfg = wallpaper::audio::AudioBus::ReadConfig("/nonexistent/wekde/audio.json");
    CHECK(cfg.captureSource.empty());
    CHECK(cfg.tuning.gain == doctest::Approx(15.0f));
    CHECK(cfg.tuning.floor == 0.0f);
    CHECK(cfg.tuning.attack == doctest::Approx(0.8f));
    CHECK(cfg.tuning.decay == doctest::Approx(0.4f));
}

TEST_CASE("audio.json: sensitivity scales the gain, the rest maps from percent") {
    const auto path = writeConfig(
        "wek-audio-full.json",
        R"({"capture_source": "alsa_input.usb-mic", "sensitivity": 100, "floor": 25, "attack": 50, "decay": 90})");
    const auto cfg = wallpaper::audio::AudioBus::ReadConfig(path);
    CHECK(cfg.captureSource == "alsa_input.usb-mic");
    CHECK(cfg.tuning.gain == doctest::Approx(30.0f));
    CHECK(cfg.tuning.floor == doctest::Approx(0.25f));
    CHECK(cfg.tuning.attack == doctest::Approx(0.5f));
    CHECK(cfg.tuning.decay == doctest::Approx(0.9f));
}

TEST_CASE("audio.json: keys left out, wrong types and out-of-range values") {
    auto path = writeConfig("wek-audio-partial.json",
                            R"({"sensitivity": 500, "floor": "high", "capture_source": 7, "decay": -4})");
    auto cfg  = wallpaper::audio::AudioBus::ReadConfig(path);
    CHECK(cfg.captureSource.empty());
    CHECK(cfg.tuning.gain == doctest::Approx(30.0f)); // clamped to 100
    CHECK(cfg.tuning.floor == 0.0f);                  // not a number: the default
    CHECK(cfg.tuning.decay == 0.0f);                  // clamped to 0
    CHECK(cfg.tuning.attack == doctest::Approx(0.8f));
    path = writeConfig("wek-audio-garbage.json", "{not json");
    cfg  = wallpaper::audio::AudioBus::ReadConfig(path);
    CHECK(cfg.tuning.gain == doctest::Approx(15.0f));
}

TEST_CASE("audio.json is read when the analyzer is made") {
    NullCaptureGuard g;
    const auto       path = writeConfig("wek-audio-acquire.json", R"({"sensitivity": 25, "capture_source": "a.monitor"})");
    ConfigFileGuard  c(path);
    auto             a = wallpaper::audio::AudioBus::Acquire(true);
    REQUIRE(a);
    CHECK(a->GetTuning().gain == doctest::Approx(7.5f));
    CHECK(wallpaper::audio::AudioBus::TEST_captureSource() == "a.monitor");
}

TEST_CASE("Reconfigure retunes at once and reopens the capture only for a new source") {
    NullCaptureGuard g;
    const auto       path = writeConfig("wek-audio-reload.json", R"({"capture_source": ""})");
    ConfigFileGuard  c(path);
    auto             a = wallpaper::audio::AudioBus::Acquire(true);
    REQUIRE(a);
    const int opened = wallpaper::audio::AudioBus::TEST_getInitCount();
    writeConfig("wek-audio-reload.json", R"({"capture_source": "", "floor": 50})");
    wallpaper::audio::AudioBus::Reconfigure();
    CHECK(a->GetTuning().floor == doctest::Approx(0.5f));
    CHECK(wallpaper::audio::AudioBus::TEST_getInitCount() == opened); // same source: not reopened
    writeConfig("wek-audio-reload.json", R"({"capture_source": "alsa_input.usb-mic"})");
    wallpaper::audio::AudioBus::Reconfigure();
    CHECK(wallpaper::audio::AudioBus::TEST_captureSource() == "alsa_input.usb-mic");
    CHECK(wallpaper::audio::AudioBus::TEST_getInitCount() == opened + 1);
    CHECK(wallpaper::audio::AudioBus::HasSystemCapture());
}

// Settings > Audio picked a USB headset's monitor, and the headset is not
// plugged in when the wallpaper loads: the capture is not open.  Going back
// to the default output and pressing OK (ReloadAudioConfig) must open it
// then, not at the next wallpaper load.
TEST_CASE("Reconfigure opens a capture whose source was absent when it was asked for") {
    NullCaptureGuard g;
    const auto       headset = "alsa_output.usb-headset.analog-stereo.monitor";
    const auto       path    = writeConfig("wek-audio-absent.json", std::string(R"({"capture_source": ")") + headset + R"("})");
    ConfigFileGuard  c(path);
    wallpaper::audio::AudioBus::TEST_setNullCaptureAbsentSource(headset);
    auto a = wallpaper::audio::AudioBus::Acquire(true);
    REQUIRE(a);
    CHECK_FALSE(wallpaper::audio::AudioBus::HasSystemCapture());
    CHECK(wallpaper::audio::AudioBus::CaptureStatus() == "failed");
    const int opened = wallpaper::audio::AudioBus::TEST_getInitCount();
    writeConfig("wek-audio-absent.json", R"({"capture_source": ""})");
    CHECK(wallpaper::audio::AudioBus::Reconfigure() == "on");
    CHECK(wallpaper::audio::AudioBus::HasSystemCapture());
    CHECK(wallpaper::audio::AudioBus::TEST_getInitCount() == opened + 1);
}

TEST_CASE("Reconfigure with the same source tries again once it is there") {
    NullCaptureGuard g;
    const auto       path = writeConfig("wek-audio-replug.json", R"({"capture_source": "usb-mic"})");
    ConfigFileGuard  c(path);
    wallpaper::audio::AudioBus::TEST_setNullCaptureAbsentSource("usb-mic");
    auto a = wallpaper::audio::AudioBus::Acquire(true);
    REQUIRE(a);
    CHECK(wallpaper::audio::AudioBus::Reconfigure() == "failed"); // still unplugged
    CHECK_FALSE(wallpaper::audio::AudioBus::HasSystemCapture());
    wallpaper::audio::AudioBus::TEST_setNullCaptureAbsentSource(""); // plugged in
    CHECK(wallpaper::audio::AudioBus::Reconfigure() == "on");
    CHECK(wallpaper::audio::AudioBus::HasSystemCapture());
}

TEST_CASE("CaptureStatus is off while no subscriber wants system audio") {
    NullCaptureGuard g;
    const auto       path = writeConfig("wek-audio-off.json", R"({"capture_source": "x.monitor"})");
    ConfigFileGuard  c(path);
    auto             a      = wallpaper::audio::AudioBus::Acquire(false);
    const int        opened = wallpaper::audio::AudioBus::TEST_getInitCount();
    CHECK(wallpaper::audio::AudioBus::Reconfigure() == "off");
    CHECK(wallpaper::audio::AudioBus::TEST_getInitCount() == opened);
    CHECK_FALSE(wallpaper::audio::AudioBus::HasSystemCapture());
}

TEST_CASE("Reconfigure without any subscriber only remembers the source") {
    NullCaptureGuard g;
    const auto       path = writeConfig("wek-audio-idle.json", R"({"capture_source": "x.monitor"})");
    ConfigFileGuard  c(path);
    const int        opened = wallpaper::audio::AudioBus::TEST_getInitCount();
    wallpaper::audio::AudioBus::Reconfigure();
    CHECK(wallpaper::audio::AudioBus::TEST_getInitCount() == opened);
    CHECK(wallpaper::audio::AudioBus::TEST_captureSource() == "x.monitor");
}

TEST_CASE("AudioBus::Acquire returns same analyzer for concurrent subscribers") {
    NullCaptureGuard g;
    auto             a = wallpaper::audio::AudioBus::Acquire(true);
    auto             b = wallpaper::audio::AudioBus::Acquire(true);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a.get() == b.get());
}

TEST_CASE("AudioBus survives first subscriber drop") {
    NullCaptureGuard g;
    auto             a = wallpaper::audio::AudioBus::Acquire(true);
    auto             b = wallpaper::audio::AudioBus::Acquire(true);
    REQUIRE(a);
    REQUIRE(b);
    a.reset();
    // b still functional; HasData callable without crash.
    CHECK_NOTHROW((void)b->HasData());
}

TEST_CASE("AudioBus expires when last subscriber drops") {
    NullCaptureGuard g;
    {
        auto a = wallpaper::audio::AudioBus::Acquire(true);
        REQUIRE(a);
    }
    // The custom deleter has fired by now.  The bus's weak_ptr observes
    // expiry; TEST_isExpired reports it under the bus mutex.
    CHECK(wallpaper::audio::AudioBus::TEST_isExpired());
}

TEST_CASE("AudioBus upgrade: false-then-true acquirer enables capture") {
    NullCaptureGuard g;
    auto             a = wallpaper::audio::AudioBus::Acquire(false);
    REQUIRE(a);
    CHECK(! wallpaper::audio::AudioBus::HasSystemCapture());
    auto b = wallpaper::audio::AudioBus::Acquire(true);
    REQUIRE(b);
    CHECK(wallpaper::audio::AudioBus::HasSystemCapture());
    CHECK(a.get() == b.get()); // upgrade returns the same shared analyzer
}

TEST_CASE("AudioBus reinit after full teardown mints fresh capture") {
    NullCaptureGuard g;
    int              before = wallpaper::audio::AudioBus::TEST_getInitCount();
    {
        auto a = wallpaper::audio::AudioBus::Acquire(true);
        REQUIRE(a);
    }
    // Bus torn down — next Acquire opens a fresh capture.
    auto b = wallpaper::audio::AudioBus::Acquire(true);
    REQUIRE(b);
    int after = wallpaper::audio::AudioBus::TEST_getInitCount();
    CHECK(after == before + 2); // one open at first Acquire, one at re-Acquire
}

TEST_CASE("the bus thread runs no FFT while nothing declares a spectrum consumer") {
    // The FFT costs a 512-sample stereo transform 60 times a second.  A
    // wallpaper with no spectrum uniform, no audio-reactive particle and no
    // SceneScript audio buffer reads nothing, so the bus must stay idle
    // rather than burn that work for every scene on the desktop.
    NullCaptureGuard g;
    auto             a = wallpaper::audio::AudioBus::Acquire(true);
    REQUIRE(a);

    // Two full FFT windows are queued and ready to consume.
    std::vector<float> pcm(2048, 0.25f);
    a->FeedPcm(pcm.data(), 1024, 2);

    // Ten bus ticks' worth of wall clock (the loop wakes every 16 ms).
    std::this_thread::sleep_for(std::chrono::milliseconds(160));
    CHECK(a->WindowsProcessedForTest() == 0);
}

TEST_CASE("a spectrum lease starts the bus FFT, dropping it stops it again") {
    NullCaptureGuard g;
    auto             a = wallpaper::audio::AudioBus::Acquire(true);
    REQUIRE(a);

    auto lease = wallpaper::audio::AudioBus::AcquireSpectrumConsumer();
    REQUIRE(lease);

    std::vector<float> pcm(4096, 0.25f);
    a->FeedPcm(pcm.data(), 2048, 2);

    // Poll rather than sleep a fixed span: the bus wakes every 16 ms but the
    // test host's scheduling is not ours to assume.
    uint64_t processed = 0;
    for (int i = 0; i < 200 && processed == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        processed = a->WindowsProcessedForTest();
    }
    REQUIRE(processed > 0);

    lease.reset();
    // Give the loop a moment to observe the withdrawal, then confirm fresh
    // PCM sits unconsumed.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const uint64_t after_withdraw = a->WindowsProcessedForTest();
    a->FeedPcm(pcm.data(), 2048, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(160));
    CHECK(a->WindowsProcessedForTest() == after_withdraw);
}
