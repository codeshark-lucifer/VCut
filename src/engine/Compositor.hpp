#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Project.hpp"
#include "media/FrameBlit.hpp"
#include "media/VideoDecoder.hpp"

namespace vcut {

// Renders the visible video frame of a Project at timeline time `t` into an
// RGBA buffer (width * height * 4 bytes).
class Compositor {
public:
    void render(const Project& project, Time t, int width, int height,
                std::vector<std::uint8_t>& rgba);

    void clear();

private:
    struct Entry {
        std::string path;
        std::unique_ptr<VideoDecoder> decoder;
        bool failed = false;
    };

    VideoDecoder* getDecoder(int key, const std::string& path);
    void pruneUnused(const Project& project);

    std::unordered_map<int, Entry> decoders_;
    FrameBlitter blitter_;
};

}  // namespace vcut
