#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Project.hpp"
#include "media/AudioDecoder.hpp"

namespace vcut {

// Mixes all unmuted clips overlapping [t, t + frames/sampleRate) into
// interleaved stereo float samples, applying speed, clip volume, and fade-in/out.
class AudioMixer {
public:
    void mix(const Project& project, Time t, int frames, int sampleRate, float* outStereo,
             float masterGain = 1.0f, float* outPeakL = nullptr, float* outPeakR = nullptr);

    void clear();

private:
    struct Entry {
        std::string path;
        int sampleRate = 0;
        std::unique_ptr<AudioDecoder> decoder;
        bool failed = false;
    };

    AudioDecoder* getDecoder(int key, const std::string& path, int sampleRate);

    std::unordered_map<int, Entry> decoders_;
    std::vector<float> clipBuf_;
    std::vector<float> speedBuf_;
};

}  // namespace vcut
