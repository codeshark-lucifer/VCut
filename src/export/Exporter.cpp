#include "export/Exporter.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

#include "engine/AudioMixer.hpp"
#include "engine/Compositor.hpp"
#include "media/FFmpeg.hpp"

namespace vcut {

namespace {

bool drainPackets(AVCodecContext* ctx, AVStream* st, AVFormatContext* fmt, AVPacket* pkt) {
    while (true) {
        const int r = avcodec_receive_packet(ctx, pkt);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
        if (r < 0) return false;
        av_packet_rescale_ts(pkt, ctx->time_base, st->time_base);
        pkt->stream_index = st->index;
        const int w = av_interleaved_write_frame(fmt, pkt);
        av_packet_unref(pkt);
        if (w < 0) return false;
    }
}

}  // namespace

bool exportProject(const Project& project, const ExportOptions& opts, std::string* error,
                   std::atomic<float>* progress, const std::atomic<bool>* cancel) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    const Time totalDur = project.duration();
    if (totalDur <= 0) return fail("Timeline is empty");

    const int W = ((opts.width > 0 ? opts.width : project.settings.width) + 1) & ~1;
    const int H = ((opts.height > 0 ? opts.height : project.settings.height) + 1) & ~1;
    const int fps = opts.fps > 0 ? opts.fps : project.settings.fps;
    const int rate = opts.sampleRate > 0 ? opts.sampleRate : project.settings.sampleRate;

    AVFormatContext* fmt = nullptr;
    int r = avformat_alloc_output_context2(&fmt, nullptr, "mp4", opts.outputPath.c_str());
    if (r < 0 || !fmt) return fail("Cannot create MP4 output context");

    AVCodecContext* vctx = nullptr;
    AVCodecContext* actx = nullptr;
    AVFrame* vframe = nullptr;
    AVFrame* aframe = nullptr;
    AVPacket* pkt = av_packet_alloc();
    SwsContext* sws = nullptr;

    struct Cleanup {
        AVFormatContext*& fmt;
        AVCodecContext*& vctx;
        AVCodecContext*& actx;
        AVFrame*& vframe;
        AVFrame*& aframe;
        AVPacket*& pkt;
        SwsContext*& sws;
        ~Cleanup() {
            sws_freeContext(sws);
            av_frame_free(&vframe);
            av_frame_free(&aframe);
            av_packet_free(&pkt);
            avcodec_free_context(&vctx);
            avcodec_free_context(&actx);
            if (fmt) {
                if (!(fmt->oformat->flags & AVFMT_NOFILE) && fmt->pb) avio_closep(&fmt->pb);
                avformat_free_context(fmt);
            }
        }
    } cleanup{fmt, vctx, actx, vframe, aframe, pkt, sws};

    // Video stream (H.264)
    const AVCodec* vcodec = avcodec_find_encoder_by_name("libx264");
    if (!vcodec) vcodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!vcodec) return fail("H.264 encoder not found");

    AVStream* vst = avformat_new_stream(fmt, nullptr);
    vctx = avcodec_alloc_context3(vcodec);
    vctx->width = W;
    vctx->height = H;
    vctx->time_base = AVRational{1, fps};
    vctx->framerate = AVRational{fps, 1};
    vctx->pix_fmt = AV_PIX_FMT_YUV420P;
    vctx->gop_size = fps * 2;
    if (fmt->oformat->flags & AVFMT_GLOBALHEADER) vctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (vcodec->name && std::string_view(vcodec->name) == "libx264") {
        av_opt_set(vctx->priv_data, "preset", "veryfast", 0);
        av_opt_set(vctx->priv_data, "crf", "21", 0);
    }
    if ((r = avcodec_open2(vctx, vcodec, nullptr)) < 0)
        return fail("Cannot open video encoder: " + avErrorString(r));
    avcodec_parameters_from_context(vst->codecpar, vctx);
    vst->time_base = vctx->time_base;

    // Audio stream (AAC stereo)
    const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!acodec) return fail("AAC encoder not found");

    AVStream* ast = avformat_new_stream(fmt, nullptr);
    actx = avcodec_alloc_context3(acodec);
    actx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    actx->sample_rate = rate;
    av_channel_layout_default(&actx->ch_layout, 2);
    actx->bit_rate = 192000;
    actx->time_base = AVRational{1, rate};
    if (fmt->oformat->flags & AVFMT_GLOBALHEADER) actx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if ((r = avcodec_open2(actx, acodec, nullptr)) < 0)
        return fail("Cannot open audio encoder: " + avErrorString(r));
    avcodec_parameters_from_context(ast->codecpar, actx);
    ast->time_base = actx->time_base;

    if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
        if ((r = avio_open(&fmt->pb, opts.outputPath.c_str(), AVIO_FLAG_WRITE)) < 0)
            return fail("Cannot open output file: " + avErrorString(r));
    }
    if ((r = avformat_write_header(fmt, nullptr)) < 0)
        return fail("Cannot write MP4 header: " + avErrorString(r));

    vframe = av_frame_alloc();
    vframe->format = vctx->pix_fmt;
    vframe->width = W;
    vframe->height = H;
    av_frame_get_buffer(vframe, 32);

    const int audioFrameSize = actx->frame_size > 0 ? actx->frame_size : 1024;
    aframe = av_frame_alloc();
    aframe->format = actx->sample_fmt;
    av_channel_layout_copy(&aframe->ch_layout, &actx->ch_layout);
    aframe->sample_rate = rate;
    aframe->nb_samples = audioFrameSize;
    av_frame_get_buffer(aframe, 0);

    sws = sws_getContext(W, H, AV_PIX_FMT_RGBA, W, H, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr,
                         nullptr, nullptr);
    if (!sws) return fail("Cannot create RGBA->YUV converter");

    Compositor compositor;
    AudioMixer mixer;
    std::vector<std::uint8_t> rgba;
    std::vector<float> audioBuf(static_cast<std::size_t>(audioFrameSize) * 2);

    const int64_t totalVideoFrames = std::max<int64_t>(1, av_rescale(totalDur, fps, kSecond));
    const int64_t totalAudioSamples = std::max<int64_t>(1, av_rescale(totalDur, rate, kSecond));
    int64_t vPts = 0;
    int64_t aPts = 0;

    while (vPts < totalVideoFrames || aPts < totalAudioSamples) {
        if (cancel && cancel->load()) return fail("Export cancelled");

        const Time vTime = av_rescale(vPts, kSecond, fps);
        const Time aTime = av_rescale(aPts, kSecond, rate);

        if (vPts < totalVideoFrames && (aPts >= totalAudioSamples || vTime <= aTime)) {
            compositor.render(project, vTime, W, H, rgba);
            av_frame_make_writable(vframe);
            const uint8_t* srcSlice[4] = {rgba.data(), nullptr, nullptr, nullptr};
            const int srcStride[4] = {W * 4, 0, 0, 0};
            sws_scale(sws, srcSlice, srcStride, 0, H, vframe->data, vframe->linesize);
            vframe->pts = vPts++;
            if (avcodec_send_frame(vctx, vframe) < 0 || !drainPackets(vctx, vst, fmt, pkt))
                return fail("Video encoding failed");
        } else {
            const int n = static_cast<int>(
                std::min<int64_t>(audioFrameSize, totalAudioSamples - aPts));
            mixer.mix(project, aTime, n, rate, audioBuf.data());
            av_frame_make_writable(aframe);
            aframe->nb_samples = n;
            auto* left = reinterpret_cast<float*>(aframe->data[0]);
            auto* right = reinterpret_cast<float*>(aframe->data[1]);
            for (int i = 0; i < n; ++i) {
                left[i] = audioBuf[static_cast<std::size_t>(i) * 2];
                right[i] = audioBuf[static_cast<std::size_t>(i) * 2 + 1];
            }
            aframe->pts = aPts;
            aPts += n;
            if (avcodec_send_frame(actx, aframe) < 0 || !drainPackets(actx, ast, fmt, pkt))
                return fail("Audio encoding failed");
        }

        if (progress) {
            progress->store(static_cast<float>(vPts) / static_cast<float>(totalVideoFrames));
        }
    }

    avcodec_send_frame(vctx, nullptr);
    drainPackets(vctx, vst, fmt, pkt);
    avcodec_send_frame(actx, nullptr);
    drainPackets(actx, ast, fmt, pkt);
    av_write_trailer(fmt);

    if (progress) progress->store(1.0f);
    return true;
}

ExportJob::~ExportJob() {
    cancel();
    wait();
}

void ExportJob::start(Project project, ExportOptions opts) {
    cancel();
    wait();
    cancel_.store(false);
    done_.store(false);
    ok_.store(false);
    progress_.store(0.0f);
    {
        std::lock_guard<std::mutex> lock(errMutex_);
        error_.clear();
    }
    running_.store(true);
    worker_ = std::thread([this, p = std::move(project), o = std::move(opts)]() {
        std::string err;
        const bool ok = exportProject(p, o, &err, &progress_, &cancel_);
        {
            std::lock_guard<std::mutex> lock(errMutex_);
            error_ = std::move(err);
        }
        ok_.store(ok);
        done_.store(true);
        running_.store(false);
    });
}

void ExportJob::cancel() { cancel_.store(true); }

void ExportJob::wait() {
    if (worker_.joinable()) worker_.join();
}

std::string ExportJob::error() const {
    std::lock_guard<std::mutex> lock(errMutex_);
    return error_;
}

}  // namespace vcut
