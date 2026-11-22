#pragma once

#include <cstdint>
#include <memory>

#include "core/Project.hpp"

namespace vcut {

// Headless UTF-8 text overlay renderer backed by FreeType2 (with a compact bitmap fallback).
// Renders anti-aliased multi-line text with optional drop shadow and background pill into RGBA.
class TextRenderer {
public:
    TextRenderer();
    ~TextRenderer();
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // Bounding box on a dstW x dstH canvas (also used for Preview interactive selection handles).
    struct Box {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    Box measure(const TextOverlay& overlay, int dstW, int dstH, float scale, float posX,
                float posY);

    void render(const TextOverlay& overlay, int dstW, int dstH, float scale, float posX, float posY,
                float opacity, std::uint8_t* rgba, float wipeMinX = 0.0f, float wipeMaxX = 1.0f);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vcut
