#include "engine/PlaybackEngine.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "media/FFmpeg.hpp"

namespace vcut {

PlaybackEngine::~PlaybackEngine() { shutdownAudio(); }

bool PlaybackEngine::initAudio(int sampleRate) {
    shutdownAudio();
    sampleRate_ = sampleRate > 0 ? sampleRate : 48000;

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = sampleRate_;

    audioStream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                             &PlaybackEngine::audioCallback, this);
    if (!audioStream_) return false;
    SDL_ResumeAudioStreamDevice(audioStream_);
    return true;
}

void PlaybackEngine::shutdownAudio() {
    playing_.store(false);
    if (audioStream_) {
        SDL_DestroyAudioStream(audioStream_);
        audioStream_ = nullptr;
    }
}

void PlaybackEngine::syncProject(const Project& project, std::uint64_t version) {
    if (audioProjectVersion_ == version) return;
    std::lock_guard<std::mutex> lock(audioMutex_);
    audioProject_ = project;
    audioProjectVersion_ = version;
}

void PlaybackEngine::play() {
    playbackSpeed_.store(1.0f);
    const Time dur = audioProject_.duration();
    if (dur > 0 && playhead_.load() >= dur) {
        seek(0);
    } else {
        audioCursor_.store(playhead_.load());
        if (audioStream_) SDL_ClearAudioStream(audioStream_);
    }
    playing_.store(true);
}

void PlaybackEngine::pause() {
    playing_.store(false);
    playbackSpeed_.store(1.0f);
    peakL_.store(0.0f);
    peakR_.store(0.0f);
}

void PlaybackEngine::togglePlay() {
    if (playing_.load())
        pause();
    else
        play();
}

void PlaybackEngine::shuttleBackward() {
    const float cur = playbackSpeed_.load();
    float next = -1.0f;
    if (playing_.load() && cur < 0.0f) {
        if (cur > -1.5f)
            next = -2.0f;
        else
            next = -4.0f;
    }
    playbackSpeed_.store(next);
    playing_.store(true);
}

void PlaybackEngine::shuttleForward() {
    const float cur = playbackSpeed_.load();
    float next = 1.0f;
    if (playing_.load() && cur > 0.0f) {
        if (cur < 1.5f)
            next = 2.0f;
        else
            next = 4.0f;
    }
    if (next == 1.0f) {
        play();
    } else {
        playbackSpeed_.store(next);
        playing_.store(true);
    }
}

void PlaybackEngine::stop() {
    pause();
    seek(0);
}

void PlaybackEngine::seek(Time t) {
    const Time clamped = std::max<Time>(0, t);
    playhead_.store(clamped);
    audioCursor_.store(clamped);
    if (audioStream_) SDL_ClearAudioStream(audioStream_);
}

void PlaybackEngine::stepFrames(const Project& project, int deltaFrames) {
    pause();
    const Time step = project.settings.frameDuration();
    seek(playhead_.load() + step * deltaFrames);
}

void SDLCALL PlaybackEngine::audioCallback(void* userdata, SDL_AudioStream* stream,
                                           int additionalAmount, int /*totalAmount*/) {
    if (additionalAmount <= 0) return;
    static_cast<PlaybackEngine*>(userdata)->fillAudio(stream, additionalAmount);
}

void PlaybackEngine::fillAudio(SDL_AudioStream* stream, int bytesNeeded) {
    const int frames = std::max(1, bytesNeeded / static_cast<int>(sizeof(float) * 2));
    mixBuf_.assign(static_cast<std::size_t>(frames) * 2, 0.0f);

    const float speed = playbackSpeed_.load();
    if (playing_.load() && std::fabs(speed - 1.0f) < 0.01f) {
        std::lock_guard<std::mutex> lock(audioMutex_);
        const Time cur = audioCursor_.load();
        const Time dur = audioProject_.duration();
        if (dur > 0 && cur < dur) {
            const float gain = masterMuted_.load() ? 0.0f : masterVolume_.load();
            float pL = 0.0f, pR = 0.0f;
            mixer_.mix(audioProject_, cur, frames, sampleRate_, mixBuf_.data(), gain, &pL, &pR);
            peakL_.store(pL);
            peakR_.store(pR);

            const Time next = cur + av_rescale(frames, kSecond, sampleRate_);
            if (next >= dur && loop_) {
                audioCursor_.store(0);
            } else {
                audioCursor_.store(std::min(next, dur));
            }
        } else {
            peakL_.store(0.0f);
            peakR_.store(0.0f);
        }
    } else {
        peakL_.store(0.0f);
        peakR_.store(0.0f);
    }

    SDL_PutAudioStreamData(stream, mixBuf_.data(),
                           static_cast<int>(mixBuf_.size() * sizeof(float)));
}

bool PlaybackEngine::updateFrame(const Project& project, std::uint64_t version, int previewW,
                                 int previewH, double dtSeconds) {
    syncProject(project, version);

    if (playing_.load()) {
        const Time dur = project.duration();
        const float speed = playbackSpeed_.load();
        if (dur <= 0) {
            pause();
        } else {
            const double clampedDt = std::clamp(dtSeconds, 0.0, 0.1);
            Time next = playhead_.load() + fromSeconds(clampedDt * speed);

            // Synchronize with audio clock only during normal 1.0x forward playback
            if (audioStream_ && std::fabs(speed - 1.0f) < 0.01f) {
                const int queuedBytes = SDL_GetAudioStreamAvailable(audioStream_);
                const int queuedFrames =
                    std::max(0, queuedBytes / static_cast<int>(sizeof(float) * 2));
                const Time audioHeard =
                    audioCursor_.load() - av_rescale(queuedFrames, kSecond, sampleRate_);
                if (audioHeard >= 0 && std::llabs(next - audioHeard) > 90'000) {
                    next = audioHeard;
                }
            }

            if (speed < 0.0f && next <= 0) {
                playhead_.store(0);
                pause();
            } else if (speed > 0.0f && next >= dur) {
                if (loop_) {
                    seek(0);
                } else {
                    playhead_.store(dur);
                    pause();
                }
            } else {
                playhead_.store(std::max<Time>(0, next));
            }
        }
    }

    const Time cur = playhead_.load();
    const Time frameStep = project.settings.frameDuration();
    const Time quantized = frameStep > 0 ? (cur / frameStep) * frameStep : cur;

    if (previewW == frameW_ && previewH == frameH_ && version == lastRenderedVersion_ &&
        quantized == lastRenderedTime_) {
        return false;
    }

    frameW_ = previewW;
    frameH_ = previewH;
    lastRenderedVersion_ = version;
    lastRenderedTime_ = quantized;
    compositor_.render(project, cur, previewW, previewH, rgba_);
    return true;
}

}  // namespace vcut
