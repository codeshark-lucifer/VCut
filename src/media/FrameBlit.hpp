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

struct FrameBlitParams {
    float opacity = 1.0f;
    float scale = 1.0f;
    float posX = 0.0f;  // normalized canvas offset (-1.5 .. 1.5)
    float posY = 0.0f;  // normalized canvas offset (-1.5 .. 1.5)

    // Color adjustments
    float brightness = 0.0f;  // -1.0 .. 1.0
    float contrast = 1.0f;    //  0.0 .. 2.0
    float saturation = 1.0f;  //  0.0 .. 2.0
    float vignette = 0.0f;    //  0.0 .. 1.0
    bool grayscale = false;

    // Chroma key (green/blue screen)
    bool chromaKey = false;
    float chromaColor[3] = {0.0f, 0.85f, 0.0f};
    float chromaSimilarity = 0.28f;
    float chromaBlend = 0.08f;

    // Horizontal wipe crop in normalized [0..1] canvas coordinates
    float wipeMinX = 0.0f;
    float wipeMaxX = 1.0f;
};

// Largest rectangle with the source aspect ratio that fits (centered) in dst.
FitRect fitRect(int srcW, int srcH, int dstW, int dstH);

// Computes transformed (possibly partially off-screen) placement rect matching FrameBlitter::blit.
FitRect transformedRect(int srcW, int srcH, int dstW, int dstH, float scale, float posX, float posY);

// Scales and composites a decoded frame into an RGBA canvas with optional transform, color
// adjustments, chroma key, vignette, wipe crop, and opacity blending.
class FrameBlitter {
public:
    FrameBlitter() = default;
    ~FrameBlitter();
    FrameBlitter(const FrameBlitter&) = delete;
    FrameBlitter& operator=(const FrameBlitter&) = delete;

    void blit(const AVFrame* frame, int dstW, int dstH, std::uint8_t* rgba,
              const FrameBlitParams& params = {});

private:
    SwsContext* sws_ = nullptr;
    std::vector<std::uint8_t> tmp_;
};

// Decode one frame of a video/image file into a w*h RGBA thumbnail.
bool makeThumbnail(const std::string& path, Time t, int w, int h, std::vector<std::uint8_t>& rgba);

}  // namespace vcut
