#include "media/MediaProbe.hpp"

#include "core/PathUtil.hpp"
#include "media/FFmpeg.hpp"

namespace vcut {

std::optional<MediaAsset> probeMedia(const std::string& path, std::string* error) {
    auto fail = [&](const std::string& msg) -> std::optional<MediaAsset> {
        if (error) *error = msg;
        return std::nullopt;
    };

    AVFormatContext* fmt = nullptr;
    int r = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    if (r < 0) return fail("Cannot open file: " + avErrorString(r));
    struct Closer {
        AVFormatContext** f;
        ~Closer() { avformat_close_input(f); }
    } closer{&fmt};

    if ((r = avformat_find_stream_info(fmt, nullptr)) < 0)
        return fail("Cannot read stream info: " + avErrorString(r));

    int v = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (v >= 0 && (fmt->streams[v]->disposition & AV_DISPOSITION_ATTACHED_PIC))
        v = -1;  // album art inside an mp3 is not "video"
    const int a = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (v < 0 && a < 0) return fail("No audio or video stream found");

    MediaAsset m;
    m.path = path;
    m.name = pathToUtf8(pathFromUtf8(path).filename());
    m.hasVideo = v >= 0;
    m.hasAudio = a >= 0;

    if (v >= 0) {
        AVStream* st = fmt->streams[v];
        m.width = st->codecpar->width;
        m.height = st->codecpar->height;
        const AVRational fr = av_guess_frame_rate(fmt, st, nullptr);
        m.fps = fr.num > 0 && fr.den > 0 ? av_q2d(fr) : 0.0;
    }

    const bool image = v >= 0 && a < 0 && isImageFormat(fmt);
    m.type = image ? MediaType::Image : (v >= 0 ? MediaType::Video : MediaType::Audio);

    if (image) {
        m.duration = 0;
        m.fps = 0;
        return m;
    }

    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) {
        m.duration = av_rescale_q(fmt->duration, AVRational{1, AV_TIME_BASE}, kMicros);
    } else {
        for (int idx : {v, a}) {
            if (idx < 0) continue;
            AVStream* st = fmt->streams[idx];
            if (st->duration != AV_NOPTS_VALUE && st->duration > 0)
                m.duration = std::max(m.duration, av_rescale_q(st->duration, st->time_base, kMicros));
        }
    }
    if (m.duration <= 0) return fail("Unknown media duration");
    return m;
}

}  // namespace vcut
