#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <string>
#include <string_view>

namespace vcut {

inline constexpr AVRational kMicros = {1, 1000000};

inline std::string avErrorString(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return buf;
}

// Still images are opened by FFmpeg's image demuxers ("image2", "png_pipe", ...).
inline bool isImageFormat(const AVFormatContext* fmt) {
    if (!fmt || !fmt->iformat || !fmt->iformat->name) return false;
    const std::string_view n = fmt->iformat->name;
    return n == "image2" || n == "image2pipe" ||
           (n.size() > 5 && n.substr(n.size() - 5) == "_pipe");
}

}  // namespace vcut
