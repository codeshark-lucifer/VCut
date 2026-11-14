#include "media/VideoDecoder.hpp"

#include "media/FFmpeg.hpp"

namespace vcut {

namespace {
constexpr Time kForwardDecodeLimit = 2 * kSecond;  // further than this: seek instead
}

VideoDecoder::~VideoDecoder() {
    av_frame_free(&cur_);
    av_frame_free(&tmp_);
    av_packet_free(&pkt_);
    avcodec_free_context(&dec_);
    avformat_close_input(&fmt_);
}

bool VideoDecoder::open(const std::string& path, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    int r = avformat_open_input(&fmt_, path.c_str(), nullptr, nullptr);
    if (r < 0) return fail("open: " + avErrorString(r));
    if ((r = avformat_find_stream_info(fmt_, nullptr)) < 0) return fail("stream info: " + avErrorString(r));

    const AVCodec* codec = nullptr;
    stream_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (stream_ < 0 || !codec) return fail("no video stream");

    AVStream* st = fmt_->streams[stream_];
    tbNum_ = st->time_base.num;
    tbDen_ = st->time_base.den;

    dec_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec_, st->codecpar);
    dec_->pkt_timebase = st->time_base;
    dec_->thread_count = 0;  // auto
    if ((r = avcodec_open2(dec_, codec, nullptr)) < 0) return fail("decoder: " + avErrorString(r));

    startTime_ = st->start_time != AV_NOPTS_VALUE ? av_rescale_q(st->start_time, st->time_base, kMicros) : 0;
    const AVRational fr = av_guess_frame_rate(fmt_, st, nullptr);
    if (fr.num > 0 && fr.den > 0) frameDur_ = av_rescale(kSecond, fr.den, fr.num);
    still_ = isImageFormat(fmt_);

    pkt_ = av_packet_alloc();
    cur_ = av_frame_alloc();
    tmp_ = av_frame_alloc();
    return true;
}

Time VideoDecoder::frameTime(const AVFrame* f) const {
    int64_t pts = f->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) pts = f->pts;
    if (pts == AV_NOPTS_VALUE) return hasCur_ ? curPts_ + curDur_ : 0;
    return av_rescale_q(pts, AVRational{tbNum_, tbDen_}, kMicros) - startTime_;
}

bool VideoDecoder::decodeNext() {
    while (true) {
        int r = avcodec_receive_frame(dec_, tmp_);
        if (r == 0) return true;
        if (r != AVERROR(EAGAIN) || inputEof_) return false;

        r = av_read_frame(fmt_, pkt_);
        if (r < 0) {
            avcodec_send_packet(dec_, nullptr);  // flush: drain remaining frames
            inputEof_ = true;
            continue;
        }
        if (pkt_->stream_index == stream_) avcodec_send_packet(dec_, pkt_);
        av_packet_unref(pkt_);
    }
}

void VideoDecoder::seekTo(Time t) {
    const int64_t ts = av_rescale_q(t + startTime_, kMicros, AVRational{tbNum_, tbDen_});
    if (av_seek_frame(fmt_, stream_, ts, AVSEEK_FLAG_BACKWARD) < 0)
        av_seek_frame(fmt_, stream_, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(dec_);
    av_frame_unref(cur_);
    hasCur_ = false;
    eof_ = false;
    inputEof_ = false;
}

const AVFrame* VideoDecoder::frameAt(Time t) {
    if (!dec_) return nullptr;

    if (still_) {  // decode once, show forever
        if (!hasCur_ && decodeNext()) {
            av_frame_move_ref(cur_, tmp_);
            hasCur_ = true;
        }
        return hasCur_ ? cur_ : nullptr;
    }

    if (hasCur_ && eof_ && t >= curPts_) return cur_;  // past the last frame: hold it

    bool needSeek = false;
    if (!hasCur_)
        needSeek = t > kForwardDecodeLimit;
    else if (t < curPts_ - frameDur_)
        needSeek = true;  // going backwards
    else if (t > curPts_ + kForwardDecodeLimit)
        needSeek = true;  // jumping far ahead
    if (needSeek) seekTo(t);

    while (true) {
        if (hasCur_ && (t < curPts_ + curDur_ || eof_)) return cur_;
        if (!decodeNext()) {
            eof_ = true;
            return hasCur_ ? cur_ : nullptr;
        }
        const Time pts = frameTime(tmp_);
        Time dur = tmp_->duration > 0 ? av_rescale_q(tmp_->duration, AVRational{tbNum_, tbDen_}, kMicros) : 0;
        if (dur <= 0) dur = frameDur_;
        av_frame_unref(cur_);
        av_frame_move_ref(cur_, tmp_);
        hasCur_ = true;
        curPts_ = pts;
        curDur_ = dur;
    }
}

}  // namespace vcut
