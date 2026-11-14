#pragma once

#include <filesystem>
#include <string>

namespace vcut {

// All paths inside VCut are UTF-8 std::string (what SDL and FFmpeg use).
// These helpers convert safely to/from std::filesystem::path on Windows.
inline std::filesystem::path pathFromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

inline std::string pathToUtf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

}  // namespace vcut
