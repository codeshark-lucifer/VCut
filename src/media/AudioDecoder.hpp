#pragma once

#include <string>
#include <vector>

#include "core/Time.hpp"

struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwrContext;

namespace vcut {

// Decodes the audio stream of one file to interleaved stereo float at a fixed
// sample rate. Sequential reads continue decoding; jumps trigger a seek.
class AudioDecoder {
public:
    static constexpr int kChannels = 2;

    AudioDecoder() = default;
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    bool open(const std::string& path, int sampleRate, std::string* error = nullptr);

    // Adds `frames` stereo samples starting at source time t into `out` (interleaved).
    void readAdd(Time t, int frames, float* out, float gain = 1.0f);

private:
    void seekTo(Time t);
    bool decodeMore();
    void appendConverted(const AVFrame* f);
    std::size_t available() const { return fifo_.size() / kChannels - fifoPos_; }

    AVFormatContext* fmt_ = nullptr;
    AVCodecContext* dec_ = nullptr;
    SwrContext* swr_ = nullptr;
    AVPacket* pkt_ = nullptr;
    AVFrame* frame_ = nullptr;
    int stream_ = -1;
    int tbNum_ = 1, tbDen_ = 1;
    Time startTime_ = 0;
    int rate_ = 48000;

    std::vector<float> fifo_;  // decoded, converted samples
    std::size_t fifoPos_ = 0;  // read position in frames
    std::vector<float> conv_;
    Time pos_ = 0;             // source time of the next sample to read
    Time nextFrameTime_ = 0;   // fallback timestamp when AV_NOPTS_VALUE
    Time dropUntil_ = -1;      // after a seek: discard samples before this time
    bool positioned_ = false;
    bool eof_ = false;
    bool inputEof_ = false;
};

}  // namespace vcut
