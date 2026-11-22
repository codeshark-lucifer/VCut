#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Project.hpp"
#include "media/FrameBlit.hpp"
#include "media/TextRenderer.hpp"
#include "media/VideoDecoder.hpp"

namespace vcut {

// Renders the visible video frame of a Project at timeline time `t` into an
// RGBA buffer (width * height * 4 bytes).
class Compositor {
public:
    void render(const Project& project, Time t, int width, int height,
                std::vector<std::uint8_t>& rgba);

    // Computes canvas bounding box for an interactive clip overlay (for Preview drag/scale UI).
    FitRect clipScreenBox(const Project& project, const Clip& clip, int width, int height);

    void clear();

private:
    struct Entry {
        std::string path;
        std::unique_ptr<VideoDecoder> decoder;
        bool failed = false;
    };

    VideoDecoder* getDecoder(int key, const std::string& path);
    void pruneUnused(const Project& project);
    void renderClipAt(const Project& project, const Clip& clip, Time relTimeline, int width,
                      int height, std::vector<std::uint8_t>& rgba, float opacityMul, float wipeMinX,
                      float wipeMaxX);

    std::unordered_map<int, Entry> decoders_;
    FrameBlitter blitter_;
    TextRenderer textRenderer_;
};

}  // namespace vcut
