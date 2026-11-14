#include "media/AudioDecoder.hpp"

#include <algorithm>
#include <cstdlib>

#include "media/FFmpeg.hpp"

namespace vcut {

namespace {
constexpr Time kSeekTolerance = 45'000;  // 45 ms
}

AudioDecoder::~AudioDecoder() {
    av_frame_free(&frame_);
    av_packet_free(&pkt_);
    swr_free(&swr_);
    avcodec_free_context(&dec_);
    avformat_close_input(&fmt_);
}

bool AudioDecoder::open(const std::string& path, int sampleRate, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    rate_ = sampleRate;
    int r = avformat_open_input(&fmt_, path.c_str(), nullptr, nullptr);
    if (r < 0) return fail("open: " + avErrorString(r));
    if ((r = avformat_find_stream_info(fmt_, nullptr)) < 0)
        return fail("stream info: " + avErrorString(r));

    const AVCodec* codec = nullptr;
    stream_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (stream_ < 0 || !codec) return fail("no audio stream");

    AVStream* st = fmt_->streams[stream_];
    tbNum_ = st->time_base.num;
    tbDen_ = st->time_base.den;
    startTime_ =
        st->start_time != AV_NOPTS_VALUE ? av_rescale_q(st->start_time, st->time_base, kMicros) : 0;

    dec_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec_, st->codecpar);
    dec_->pkt_timebase = st->time_base;
    if ((r = avcodec_open2(dec_, codec, nullptr)) < 0) return fail("decoder: " + avErrorString(r));

    if (dec_->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
        const int n = dec_->ch_layout.nb_channels;
        av_channel_layout_uninit(&dec_->ch_layout);
        av_channel_layout_default(&dec_->ch_layout, n > 0 ? n : 2);
    }
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, kChannels);
    r = swr_alloc_set_opts2(&swr_, &outLayout, AV_SAMPLE_FMT_FLT, rate_, &dec_->ch_layout,
                            dec_->sample_fmt, dec_->sample_rate, 0, nullptr);
    if (r < 0 || swr_init(swr_) < 0) return fail("resampler init failed");

    pkt_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    return true;
}

void AudioDecoder::seekTo(Time t) {
    t = std::max<Time>(0, t);
    const int64_t ts = av_rescale_q(t + startTime_, kMicros, AVRational{tbNum_, tbDen_});
    if (av_seek_frame(fmt_, stream_, ts, AVSEEK_FLAG_BACKWARD) < 0)
        av_seek_frame(fmt_, stream_, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(dec_);
    swr_close(swr_);
    swr_init(swr_);
    fifo_.clear();
    fifoPos_ = 0;
    eof_ = false;
    inputEof_ = false;
    dropUntil_ = t;
    nextFrameTime_ = 0;
    pos_ = t;
    positioned_ = true;
}

bool AudioDecoder::decodeMore() {
    while (true) {
        int r = avcodec_receive_frame(dec_, frame_);
        if (r == 0) {
            appendConverted(frame_);
            av_frame_unref(frame_);
            return true;
        }
        if (r != AVERROR(EAGAIN) || inputEof_) {
            eof_ = true;
            return false;
        }
        r = av_read_frame(fmt_, pkt_);
        if (r < 0) {
            avcodec_send_packet(dec_, nullptr);
            inputEof_ = true;
            continue;
        }
        if (pkt_->stream_index == stream_) avcodec_send_packet(dec_, pkt_);
        av_packet_unref(pkt_);
    }
}

void AudioDecoder::appendConverted(const AVFrame* f) {
    const int srcRate = f->sample_rate > 0 ? f->sample_rate : dec_->sample_rate;
    const Time frameDur = srcRate > 0 ? av_rescale(f->nb_samples, kSecond, srcRate) : 0;

    int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    Time ft = nextFrameTime_;
    if (pts != AV_NOPTS_VALUE) {
        ft = av_rescale_q(pts, AVRational{tbNum_, tbDen_}, kMicros) - startTime_;
    }
    nextFrameTime_ = ft + frameDur;

    // If this entire frame ends before dropUntil_, skip it before touching swr_convert.
    if (dropUntil_ >= 0 && ft + frameDur <= dropUntil_) {
        return;
    }

    const int outCount = swr_get_out_samples(swr_, f->nb_samples);
    if (outCount <= 0) return;
    conv_.resize(static_cast<std::size_t>(outCount) * kChannels);
    uint8_t* outPtr = reinterpret_cast<uint8_t*>(conv_.data());
    const int n = swr_convert(swr_, &outPtr, outCount,
                              const_cast<const uint8_t**>(f->extended_data), f->nb_samples);
    if (n <= 0) return;

    int64_t begin = 0;
    if (dropUntil_ >= 0) {
        if (ft > dropUntil_) {
            const int64_t pad =
                std::min<int64_t>(av_rescale(ft - dropUntil_, rate_, kSecond), rate_ * 5);
            if (pad > 0) fifo_.insert(fifo_.end(), static_cast<std::size_t>(pad) * kChannels, 0.0f);
        } else {
            begin = std::clamp<int64_t>(av_rescale(dropUntil_ - ft, rate_, kSecond), 0, n);
        }
        dropUntil_ = -1;
    }
    if (begin < n) {
        fifo_.insert(fifo_.end(), conv_.begin() + begin * kChannels,
                     conv_.begin() + static_cast<int64_t>(n) * kChannels);
    }
}

void AudioDecoder::readAdd(Time t, int frames, float* out, float gain) {
    if (!dec_ || frames <= 0) return;
    if (!positioned_ || std::llabs(t - pos_) > kSeekTolerance) seekTo(t);

    while (available() < static_cast<std::size_t>(frames) && !eof_) decodeMore();

    const std::size_t n = std::min(available(), static_cast<std::size_t>(frames));
    const float* src = fifo_.data() + fifoPos_ * kChannels;
    for (std::size_t i = 0; i < n * kChannels; ++i) out[i] += src[i] * gain;
    fifoPos_ += n;

    if (fifoPos_ > static_cast<std::size_t>(rate_) * 4) {
        fifo_.erase(fifo_.begin(),
                    fifo_.begin() + static_cast<std::ptrdiff_t>(fifoPos_ * kChannels));
        fifoPos_ = 0;
    }
    pos_ = t + av_rescale(frames, kSecond, rate_);
}

}  // namespace vcut
