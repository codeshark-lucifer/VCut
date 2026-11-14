#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Time.hpp"

struct AVFrame;
struct SwsContext;

namespace vcut {

struct FitRect {
    int x = 0, y = 0, w = 0, h = 0;
};

// Largest rectangle with the source aspect ratio that fits (centered) in dst.
FitRect fitRect(int srcW, int srcH, int dstW, int dstH);

// Scales a decoded frame into an RGBA canvas, letterboxed, alpha-blended if needed.
class FrameBlitter {
public:
    FrameBlitter() = default;
    ~FrameBlitter();
    FrameBlitter(const FrameBlitter&) = delete;
    FrameBlitter& operator=(const FrameBlitter&) = delete;

    void blit(const AVFrame* frame, int dstW, int dstH, std::uint8_t* rgba);

private:
    SwsContext* sws_ = nullptr;
    std::vector<std::uint8_t> tmp_;
};

// Decode one frame of a video/image file into a w*h RGBA thumbnail.
bool makeThumbnail(const std::string& path, Time t, int w, int h, std::vector<std::uint8_t>& rgba);

}  // namespace vcut
