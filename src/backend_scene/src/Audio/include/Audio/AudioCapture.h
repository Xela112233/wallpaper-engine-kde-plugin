#pragma once
#include <memory>
#include <string>

namespace wallpaper
{
namespace audio
{

class AudioAnalyzer;

class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    // Try to open a system audio monitor source (PipeWire/PulseAudio .monitor)
    // Returns true if capture is active.  `source` (the KDE plugin's
    // audio.json capture_source): a PulseAudio source name opened as given,
    // a sink's monitor or an input such as a microphone; changes of the
    // default output are then ignored, and the source is opened again when
    // it comes back after it went away.  A named source that is absent now
    // is opened when it appears; until then the default output's monitor is
    // captured (OnFallback).  Empty: the default output's monitor, followed
    // when the default output changes.
    bool Init(std::shared_ptr<AudioAnalyzer> analyzer, const std::string& source = {});

    // The PulseAudio device name a capture opens: the fixed source when one
    // is set, else the monitor of `sink`.
    static std::string DeviceNameFor(const std::string& sink, const std::string& fixedSource);

    // Query whether system capture is working
    bool IsActive() const;

    // A named source that was absent at Init: the default output's monitor
    // is captured instead until the named one appears (it is then opened).
    bool OnFallback() const;

    // Stop capture and release device
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace audio
} // namespace wallpaper
