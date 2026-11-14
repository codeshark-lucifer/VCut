#pragma once

#include <cstdint>
#include <string>

namespace vcut {

// All timeline times are integer microseconds (no floating-point drift).
using Time = std::int64_t;

inline constexpr Time kSecond = 1'000'000;

inline double toSeconds(Time t) { return static_cast<double>(t) / kSecond; }
inline Time fromSeconds(double s) {
    return static_cast<Time>(s * kSecond + (s >= 0 ? 0.5 : -0.5));
}

// "HH:MM:SS.cc"
std::string formatTime(Time t);

}  // namespace vcut
