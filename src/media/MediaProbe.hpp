#pragma once

#include <optional>
#include <string>

#include "core/Project.hpp"

namespace vcut {

// Opens a file with FFmpeg and fills a MediaAsset (id left as 0).
std::optional<MediaAsset> probeMedia(const std::string& path, std::string* error = nullptr);

}  // namespace vcut
