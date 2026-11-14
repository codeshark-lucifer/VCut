#pragma once

#include <string>

#include "core/Time.hpp"

struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;

namespace vcut {

// Decodes the video stream of one file and returns the frame shown at a given
// source time. Sequential playback decodes forward; jumps trigger a seek.
class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool open(const std::string& path, std::string* error = nullptr);

    // Frame visible at source time t (relative to the start of the file).
    // The returned frame stays valid until the next call. nullptr if nothing decodable.
    const AVFrame* frameAt(Time t);

    bool isStill() const { return still_; }

private:
    void seekTo(Time t);
    bool decodeNext();  // into tmp_
    Time frameTime(const AVFrame* f) const;

    AVFormatContext* fmt_ = nullptr;
    AVCodecContext* dec_ = nullptr;
    AVPacket* pkt_ = nullptr;
    AVFrame* cur_ = nullptr;
    AVFrame* tmp_ = nullptr;
    int stream_ = -1;
    int tbNum_ = 1, tbDen_ = 1;
    Time startTime_ = 0;
    Time frameDur_ = 40'000;
    Time curPts_ = 0;
    Time curDur_ = 0;
    bool hasCur_ = false;
    bool eof_ = false;
    bool inputEof_ = false;
    bool still_ = false;
};

}  // namespace vcut
