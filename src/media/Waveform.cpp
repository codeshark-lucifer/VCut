#include "media/Waveform.hpp"

#include <algorithm>
#include <cmath>

#include "media/AudioDecoder.hpp"

namespace vcut {

bool buildWaveform(const std::string& path, AudioWaveform& out) {
    out.peaks.clear();
    constexpr int kDecodeRate = 8000;  // low sample rate is fast and plenty for visual peaks
    constexpr int kSamplesPerBucket = kDecodeRate / AudioWaveform::kBucketsPerSecond;  // 160

    AudioDecoder dec;
    if (!dec.open(path, kDecodeRate)) return false;

    std::vector<float> buf(static_cast<std::size_t>(kSamplesPerBucket) * 2);
    Time t = 0;
    constexpr Time kStep = kSecond / AudioWaveform::kBucketsPerSecond;
    // Cap waveform scan at 30 minutes to keep import snappy
    constexpr int kMaxBuckets = 30 * 60 * AudioWaveform::kBucketsPerSecond;

    int silentRun = 0;
    for (int b = 0; b < kMaxBuckets; ++b) {
        std::fill(buf.begin(), buf.end(), 0.0f);
        dec.readAdd(t, kSamplesPerBucket, buf.data(), 1.0f);
        float peak = 0.0f;
        for (float s : buf) peak = std::max(peak, std::fabs(s));
        out.peaks.push_back(std::min(1.0f, peak));
        t += kStep;

        if (peak == 0.0f) {
            if (++silentRun > AudioWaveform::kBucketsPerSecond * 120) break;
        } else {
            silentRun = 0;
        }
    }
    return !out.peaks.empty();
}

const AudioWaveform* WaveformCache::get(const MediaAsset& asset) {
    if (!asset.hasAudio || asset.missing) return nullptr;
    auto it = cache_.find(asset.id);
    if (it != cache_.end()) return &it->second;

    AudioWaveform wf;
    if (asset.duration > 0) {
        buildWaveform(asset.path, wf);
        const std::size_t expected = static_cast<std::size_t>(
            (asset.duration * AudioWaveform::kBucketsPerSecond) / kSecond + 1);
        if (wf.peaks.size() > expected) wf.peaks.resize(expected);
    }
    auto [ins, _] = cache_.emplace(asset.id, std::move(wf));
    return &ins->second;
}

}  // namespace vcut
