#include "core/Time.hpp"

#include <cstdio>

namespace vcut {

std::string formatTime(Time t) {
    if (t < 0) t = 0;
    const long long cs = t / 10'000;  // centiseconds
    const long long s = cs / 100;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld.%02lld", s / 3600, (s / 60) % 60, s % 60,
                  cs % 100);
    return buf;
}

}  // namespace vcut
