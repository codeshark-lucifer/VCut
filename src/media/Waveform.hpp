#pragma once

#include <unordered_map>
#include <vector>

#include "core/Project.hpp"

namespace vcut {

struct AudioWaveform {
    static constexpr int kBucketsPerSecond = 50;  // 20 ms per bucket
    std::vector<float> peaks;                     // normalized [0.0 .. 1.0]

    float peakAt(Time sourceTime) const {
        if (peaks.empty() || sourceTime < 0) return 0.0f;
        const std::size_t idx =
            static_cast<std::size_t>((sourceTime * kBucketsPerSecond) / kSecond);
        return idx < peaks.size() ? peaks[idx] : 0.0f;
    }
};

bool buildWaveform(const std::string& path, AudioWaveform& out);

class WaveformCache {
public:
    const AudioWaveform* get(const MediaAsset& asset);
    void clear() { cache_.clear(); }

private:
    std::unordered_map<int, AudioWaveform> cache_;
};

}  // namespace vcut
