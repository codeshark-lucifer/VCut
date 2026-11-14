#include "engine/AudioMixer.hpp"

#include <algorithm>
#include <cmath>

#include "media/FFmpeg.hpp"

namespace vcut {

void AudioMixer::clear() { decoders_.clear(); }

AudioDecoder* AudioMixer::getDecoder(int key, const std::string& path, int sampleRate) {
    auto& e = decoders_[key];
    if (e.path != path || e.sampleRate != sampleRate) {
        e.path = path;
        e.sampleRate = sampleRate;
        e.decoder = std::make_unique<AudioDecoder>();
        e.failed = !e.decoder->open(path, sampleRate);
        if (e.failed) e.decoder.reset();
    }
    return e.decoder.get();
}

void AudioMixer::mix(const Project& project, Time t, int frames, int sampleRate, float* outStereo,
                     float masterGain, float* outPeakL, float* outPeakR) {
    if (frames <= 0 || sampleRate <= 0 || !outStereo) return;
    std::fill(outStereo, outStereo + static_cast<std::size_t>(frames) * 2, 0.0f);

    const Time blockDur = av_rescale(frames, kSecond, sampleRate);
    const Time blockEnd = t + blockDur;

    for (const Track& tr : project.tracks) {
        if (tr.muted) continue;
        for (const Clip& c : tr.clips) {
            if (c.muted || c.volume <= 0.0f) continue;
            if (c.end() <= t || c.start >= blockEnd) continue;
            const MediaAsset* asset = project.findAsset(c.assetId);
            if (!asset || !asset->hasAudio || asset->missing) continue;

            const Time ovStart = std::max(t, c.start);
            const Time ovEnd = std::min(blockEnd, c.end());
            const int s0 =
                std::clamp(static_cast<int>(av_rescale(ovStart - t, sampleRate, kSecond)), 0, frames);
            const int s1 =
                std::clamp(static_cast<int>(av_rescale(ovEnd - t, sampleRate, kSecond)), s0, frames);
            const int count = s1 - s0;
            if (count <= 0) continue;

            AudioDecoder* dec = getDecoder(c.id, asset->path, sampleRate);
            if (!dec) continue;

            const Time srcT = c.in + (ovStart - c.start);
            const float baseGain = c.volume * masterGain;

            if (c.fadeIn <= 0 && c.fadeOut <= 0) {
                dec->readAdd(srcT, count, outStereo + static_cast<std::size_t>(s0) * 2, baseGain);
            } else {
                clipBuf_.assign(static_cast<std::size_t>(count) * 2, 0.0f);
                dec->readAdd(srcT, count, clipBuf_.data(), baseGain);
                const Time clipDur = c.duration();
                for (int i = 0; i < count; ++i) {
                    const Time rel = (ovStart - c.start) + av_rescale(i, kSecond, sampleRate);
                    float env = 1.0f;
                    if (c.fadeIn > 0 && rel < c.fadeIn) {
                        env = std::min(env, static_cast<float>(rel) / static_cast<float>(c.fadeIn));
                    }
                    if (c.fadeOut > 0 && clipDur - rel < c.fadeOut) {
                        env = std::min(env, static_cast<float>(clipDur - rel) /
                                                static_cast<float>(c.fadeOut));
                    }
                    env = std::clamp(env, 0.0f, 1.0f);
                    float* dst = outStereo + static_cast<std::size_t>(s0 + i) * 2;
                    dst[0] += clipBuf_[static_cast<std::size_t>(i) * 2] * env;
                    dst[1] += clipBuf_[static_cast<std::size_t>(i) * 2 + 1] * env;
                }
            }
        }
    }

    float peakL = 0.0f;
    float peakR = 0.0f;
    const std::size_t total = static_cast<std::size_t>(frames) * 2;
    for (std::size_t i = 0; i < total; i += 2) {
        outStereo[i] = std::clamp(outStereo[i], -1.0f, 1.0f);
        outStereo[i + 1] = std::clamp(outStereo[i + 1], -1.0f, 1.0f);
        peakL = std::max(peakL, std::fabs(outStereo[i]));
        peakR = std::max(peakR, std::fabs(outStereo[i + 1]));
    }
    if (outPeakL) *outPeakL = peakL;
    if (outPeakR) *outPeakR = peakR;
}

}  // namespace vcut
