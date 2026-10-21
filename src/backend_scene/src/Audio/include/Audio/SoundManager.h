#pragma once
#include <memory>
#include <cstddef>
#include <cstdint>
#include <functional>
#include "Utils/Logging.h"
#include "Core/NoCopyMove.hpp"

namespace wallpaper
{

namespace fs
{
class IBinaryStream;
}
namespace audio
{
class AudioAnalyzer;

class SoundStream : NoCopy, NoMove {
public:
    struct Desc {
        uint32_t channels;
        uint32_t sampleRate;
    };

public:
    SoundStream()          = default;
    virtual ~SoundStream() = default;

    virtual uint64_t NextPcmData(void* pData, uint32_t frameCount) = 0;
    virtual void     PassDesc(const Desc&)                         = 0;
};
std::unique_ptr<SoundStream> CreateSoundStream(std::shared_ptr<fs::IBinaryStream>,
                                               const SoundStream::Desc&);

// Whether a scene should hold an open output stream.  An open stream, even a
// silent or stopped one, keeps the audio codec powered (~1 W measured on an
// Intel laptop, codec runtime-PM status stuck at "active"), and most scenes
// mount no sound at all.  With nothing mounted there is also nothing for the
// playback tap to analyse: the spectrum is zero either way.
//
// `outputWanted` (we-manager mute patch) is false with the plugin's
// AudioOutput off, and while the scene is muted and its visualiser takes the
// system capture rather than this device's own playback tap: then nothing
// would come out of the device and nothing reads what goes in.
inline bool WantsOutputDevice(bool playbackStopped, std::size_t mountedChannels, bool outputWanted = true) {
    return outputWanted && ! playbackStopped && mountedChannels > 0;
}
// That `outputWanted`: the output is on, and the scene is not muted while its
// spectrum comes from the system capture (when the capture is off, or failed,
// the visualiser reads this device's playback tap, so a muted scene keeps it).
inline bool OutputWanted(bool audioOutput, bool muted, bool spectrumFromSystemCapture) {
    return audioOutput && ! (muted && spectrumFromSystemCapture);
}

class SoundManager : NoCopy, NoMove {
public:
    SoundManager();
    ~SoundManager();
    void MountStream(std::unique_ptr<SoundStream>&&);
    void UnMountAll();
    // Number of streams still mounted.  Callers hold non-owning aliases to the
    // streams they mounted, so this is how they (and the tests) can tell
    // whether an alias still names a live object.
    std::size_t MountedChannelCount() const;
    void        Test(std::shared_ptr<fs::IBinaryStream>);
    bool        Init();
    bool        IsInited() const;
    void        Play();
    void        Pause();
    // Drop the audio device/stream; mounted streams survive and Init() reopens.
    void Close();

    float Volume() const;
    bool  Muted() const;
    void  SetMuted(bool);
    void  SetVolume(float);

    void SetAudioAnalyzer(std::shared_ptr<AudioAnalyzer> analyzer);

private:
    class impl;
    std::unique_ptr<impl> pImpl;
};
} // namespace audio
} // namespace wallpaper
