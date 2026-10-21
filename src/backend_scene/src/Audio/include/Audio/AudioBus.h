#pragma once
#include <memory>
#include <string>

#include "Audio/AudioAnalyzer.h"

namespace wallpaper
{
namespace audio
{

class AudioAnalyzer;

// Process-singleton audio fan-out: one AudioCapture + AudioAnalyzer pair
// shared across every subscriber (Scene wallpaper, Web wallpaper, future
// MPV taps).  Each subscriber Acquires a shared_ptr<AudioAnalyzer>; the
// underlying capture stays alive while any returned shared_ptr is alive
// and is torn down (via a custom deleter on the analyzer's shared_ptr)
// when the last subscriber drops.
//
// Process() ownership stays single-consumer via a bus-internal 60Hz
// thread; subscribers only read the spectrum via the analyzer's getters
// (GetRawSpectrum / GetSpectrum16Left etc.), which snapshot the last
// published frame without blocking the bus thread or each other.  That
// keeps one FFT pipeline for the whole process behind an MPSC feed and
// lock-free reads.  Nobody else may call Process() — a second caller
// races the bus thread on readPos and the kissfft scratch.
//
// Acquire is thread-safe.  When wantSystemCapture is true and capture
// init fails (no PulseAudio / PipeWire monitor source available) the
// returned analyzer is still valid — it just won't see system audio.
// Subscribers detect that case via the analyzer's HasData()/spectrum
// being empty (matching the pre-bus qWarning fall-through path).
class AudioBus {
public:
    // The process-wide audio settings (the KDE plugin's audio.json, which
    // we-manager writes): the capture source ("" = the default output's
    // monitor) and the analyzer's tuning.
    struct Config {
        std::string           captureSource;
        AudioAnalyzer::Tuning tuning {};
    };
    // $WEK_AUDIO_CONFIG, else $XDG_CONFIG_HOME/wekde/audio.json, else
    // ~/.config/wekde/audio.json.
    static std::string ConfigPath();
    // {"capture_source": "", "sensitivity": 0-100 (50 = the built-in gain),
    //  "floor": 0-100 (% taken off the scaled magnitude), "attack": 1-100,
    //  "decay": 0-99}.  A missing file, a key left out or a wrong type gives
    //  the built-in value, so no file means the analyzer as before.
    static Config ReadConfig(const std::string& path);
    // Reads the file again: the new tuning applies at the next FFT.  While a
    // subscriber wants system audio, a changed capture source reopens the
    // capture on it, and a capture that is not open (its source was absent
    // when it was asked for) is opened now.  Any thread.  Returns
    // CaptureStatus() afterwards.
    static std::string Reconfigure();
    // "off": no subscriber wants system audio; "on": the capture is open on
    // the source audio.json names (or the default output's monitor);
    // "fallback": the named source is absent, so the default output's
    // monitor is captured until it appears; "failed": wanted but nothing
    // could be opened.
    static std::string CaptureStatus();

    // Returns the shared analyzer.  wantSystemCapture=true requests the
    // PulseAudio/PipeWire monitor stream (lazily opened on the first
    // true-Acquire).  Returns a usable analyzer even when capture init
    // fails — subscribers that need real audio detect via HasData().
    static std::shared_ptr<AudioAnalyzer> Acquire(bool wantSystemCapture);

    // RAII declaration that the holder reads the spectrum.  The bus thread
    // runs the FFT only while at least one of these is alive, so a wallpaper
    // with no spectrum uniform, no audio-reactive particle and no SceneScript
    // audio buffer costs nothing.  Reset the handle to withdraw the
    // declaration; re-acquire to reinstate it.  Callers must also hold an
    // Acquire() handle — a lease alone doesn't keep the bus up.
    using SpectrumConsumer = std::shared_ptr<void>;
    static SpectrumConsumer AcquireSpectrumConsumer();

    // True when an AudioCapture (or the WEK_TEST_AUDIO_NULL_CAPTURE
    // stand-in) is currently feeding the shared analyzer.  Subscribers
    // use this after Acquire(true) to decide whether to fall back to the
    // SoundManager playback tap.  Returns false when no subscriber has
    // yet requested system capture, or when capture-init failed.
    static bool HasSystemCapture();

    // Test-only inspectors.  Implementation detail — never call from
    // production code.
    static bool        TEST_isExpired();
    static int         TEST_getInitCount();
    static std::string TEST_captureSource();
    // WEK_TEST_AUDIO_NULL_CAPTURE's stand-in fails to open this source, as
    // PulseAudio does for an unplugged device; cleared by
    // TEST_resetForNextCase.
    static void        TEST_setNullCaptureAbsentSource(const std::string& source);
    // Joins the background Process thread (if any) and clears all bus
    // state.  Required between doctest cases — the static analyzer's
    // custom deleter joins the thread asynchronously, so without this
    // sync point a subsequent TEST_isExpired() can race against an
    // in-flight teardown.
    static void TEST_resetForNextCase();
};

} // namespace audio
} // namespace wallpaper
