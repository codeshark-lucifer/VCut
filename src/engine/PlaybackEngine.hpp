#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "core/Project.hpp"
#include "engine/AudioMixer.hpp"
#include "engine/Compositor.hpp"

struct SDL_AudioStream;

namespace vcut {

// Real-time preview engine: drives playhead clock, SDL3 audio output, and
// frame compositing for the preview panel.
class PlaybackEngine {
public:
    PlaybackEngine() = default;
    ~PlaybackEngine();
    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    bool initAudio(int sampleRate = 48000);
    void shutdownAudio();

    void syncProject(const Project& project, std::uint64_t version);

    void play();
    void pause();
    void togglePlay();
    void stop();
    void seek(Time t);
    void stepFrames(const Project& project, int deltaFrames);

    bool isPlaying() const { return playing_.load(); }
    Time playhead() const { return playhead_.load(); }

    void setLoop(bool loop) { loop_ = loop; }
    bool loop() const { return loop_; }

    void setMasterVolume(float v) { masterVolume_.store(v); }
    float masterVolume() const { return masterVolume_.load(); }

    void setMasterMuted(bool m) { masterMuted_.store(m); }
    bool masterMuted() const { return masterMuted_.load(); }

    float peakLeft() const { return peakL_.load(); }
    float peakRight() const { return peakR_.load(); }

    // Called once per UI frame: advances clock and renders the preview RGBA
    // buffer if the frame or project changed.
    bool updateFrame(const Project& project, std::uint64_t version, int previewW, int previewH,
                     double dtSeconds);

    const std::vector<std::uint8_t>& frameRgba() const { return rgba_; }
    int frameWidth() const { return frameW_; }
    int frameHeight() const { return frameH_; }

private:
    static void audioCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount,
                              int totalAmount);
    void fillAudio(SDL_AudioStream* stream, int bytesNeeded);

    SDL_AudioStream* audioStream_ = nullptr;
    int sampleRate_ = 48000;

    std::atomic<bool> playing_{false};
    std::atomic<Time> playhead_{0};
    std::atomic<Time> audioCursor_{0};
    std::atomic<float> masterVolume_{1.0f};
    std::atomic<bool> masterMuted_{false};
    std::atomic<float> peakL_{0.0f};
    std::atomic<float> peakR_{0.0f};
    bool loop_ = false;

    std::mutex audioMutex_;
    Project audioProject_;
    std::uint64_t audioProjectVersion_ = 0;
    AudioMixer mixer_;
    std::vector<float> mixBuf_;

    Compositor compositor_;
    std::vector<std::uint8_t> rgba_;
    int frameW_ = 0;
    int frameH_ = 0;
    Time lastRenderedTime_ = -1;
    std::uint64_t lastRenderedVersion_ = 0;
};

}  // namespace vcut
