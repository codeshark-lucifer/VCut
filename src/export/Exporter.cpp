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

bool openVideoEncoderByName(const char* name, AVCodecID fallbackId, const AVFormatContext* fmt,
                            int W, int H, int fps, bool isHdOr4k, const ExportOptions& opts,
                            AVCodecContext*& outCtx, const AVCodec*& outCodec,
                            std::string& outErr) {
    const AVCodec* codec = name ? avcodec_find_encoder_by_name(name)
                                : avcodec_find_encoder(fallbackId);
    if (!codec) {
        outErr = name ? (std::string("Encoder not found: ") + name) : "H.264 encoder not found";
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        outErr = "Cannot allocate video encoder";
        return false;
    }

    const std::string_view cname = codec->name ? codec->name : "";
    const bool isLibx264 = (cname == "libx264");
    const bool isHw = (cname == "h264_nvenc" || cname == "h264_amf" || cname == "h264_qsv");

    ctx->width = W;
    ctx->height = H;
    ctx->time_base = AVRational{1, fps};
    ctx->framerate = AVRational{fps, 1};
    ctx->pix_fmt = isHw ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
    ctx->gop_size = fps * 2;
    ctx->thread_count = 0;  // auto-detect threads for 4K/HD encoding
    ctx->color_range = AVCOL_RANGE_MPEG;
    ctx->colorspace = isHdOr4k ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
    ctx->color_primaries = isHdOr4k ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M;
    ctx->color_trc = isHdOr4k ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M;
    if (fmt->oformat->flags & AVFMT_GLOBALHEADER) ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    const int crf = std::clamp(opts.crf, 0, 51);
    const double qualScale = crf <= 18 ? 1.5 : (crf >= 25 ? 0.65 : 1.0);
    const double pxScale = (static_cast<double>(W) * H) / (1920.0 * 1080.0);
    const int64_t targetBitrate = static_cast<int64_t>(
        std::clamp(12'000'000.0 * pxScale * (fps / 30.0) * qualScale, 1'500'000.0, 90'000'000.0));

    if (isLibx264) {
        const std::string crfStr = std::to_string(crf);
        const char* presetStr = opts.preset.empty() ? "veryfast" : opts.preset.c_str();
        av_opt_set(ctx->priv_data, "preset", presetStr, 0);
        av_opt_set(ctx->priv_data, "crf", crfStr.c_str(), 0);
    } else if (cname == "h264_nvenc") {
        ctx->bit_rate = targetBitrate;
        av_opt_set(ctx->priv_data, "preset", "p4", 0);
        av_opt_set(ctx->priv_data, "cq", std::to_string(crf).c_str(), 0);
    } else if (cname == "h264_qsv") {
        ctx->bit_rate = targetBitrate;
        av_opt_set_int(ctx->priv_data, "global_quality", crf, 0);
    } else {
        ctx->bit_rate = targetBitrate;
    }

    const int r = avcodec_open2(ctx, codec, nullptr);
    if (r < 0) {
        outErr = avErrorString(r);
        avcodec_free_context(&ctx);
        return false;
    }

    outCtx = ctx;
    outCodec = codec;
    return true;
}

}  // namespace

bool exportProject(const Project& project, const ExportOptions& opts, std::string* error,
                   std::atomic<float>* progress, const std::atomic<bool>* cancel,
                   std::string* usedEncoder) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    const Time totalDur = project.duration();
    if (totalDur <= 0) return fail("Timeline is empty");

    const int rawW = opts.width > 0 ? opts.width : project.settings.width;
    const int rawH = opts.height > 0 ? opts.height : project.settings.height;
    const int W = (std::max(2, rawW) + 1) & ~1;
    const int H = (std::max(2, rawH) + 1) & ~1;
    const int fps = std::clamp(opts.fps > 0 ? opts.fps : project.settings.fps, 1, 240);
    const int rate =
        std::max(8000, opts.sampleRate > 0 ? opts.sampleRate : project.settings.sampleRate);
    const bool isHdOr4k = (W >= 1280 || H >= 720);

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

    // Video stream (H.264 via requested backend or safe CPU libx264 fallback)
    std::vector<const char*> candidates;
    switch (opts.encoder) {
        case EncoderBackend::Nvenc:
            candidates = {"h264_nvenc"};
            break;
        case EncoderBackend::Amf:
            candidates = {"h264_amf"};
            break;
        case EncoderBackend::Qsv:
            candidates = {"h264_qsv"};
            break;
        case EncoderBackend::Auto:
            candidates = {"h264_nvenc", "h264_amf", "h264_qsv", "libx264", nullptr};
            break;
        case EncoderBackend::CpuX264:
        default:
            candidates = {"libx264", nullptr};
            break;
    }

    const AVCodec* vcodec = nullptr;
    std::string lastEncErr;
    for (const char* cand : candidates) {
        if (openVideoEncoderByName(cand, AV_CODEC_ID_H264, fmt, W, H, fps, isHdOr4k, opts, vctx,
                                   vcodec, lastEncErr)) {
            break;
        }
    }
    if (!vctx || !vcodec) {
        return fail("Cannot open video encoder: " + lastEncErr);
    }
    if (usedEncoder && vcodec->name) *usedEncoder = vcodec->name;

    AVStream* vst = avformat_new_stream(fmt, nullptr);
    if (!vst) return fail("Cannot create video stream");
    avcodec_parameters_from_context(vst->codecpar, vctx);
    vst->time_base = vctx->time_base;

    // Audio stream (AAC stereo)
    const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!acodec) return fail("AAC encoder not found");

    AVStream* ast = avformat_new_stream(fmt, nullptr);
    actx = avcodec_alloc_context3(acodec);
    if (!ast || !actx) return fail("Cannot allocate audio encoder");
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
    {
        AVDictionary* muxOpts = nullptr;
        av_dict_set(&muxOpts, "movflags", "+faststart", 0);
        r = avformat_write_header(fmt, &muxOpts);
        av_dict_free(&muxOpts);
        if (r < 0) return fail("Cannot write MP4 header: " + avErrorString(r));
    }

    vframe = av_frame_alloc();
    if (!vframe) return fail("Cannot allocate video frame");
    vframe->format = vctx->pix_fmt;
    vframe->width = W;
    vframe->height = H;
    vframe->color_range = vctx->color_range;
    vframe->colorspace = vctx->colorspace;
    vframe->color_primaries = vctx->color_primaries;
    vframe->color_trc = vctx->color_trc;
    if ((r = av_frame_get_buffer(vframe, 32)) < 0)
        return fail("Cannot allocate video frame buffer: " + avErrorString(r));

    const int audioFrameSize = actx->frame_size > 0 ? actx->frame_size : 1024;
    aframe = av_frame_alloc();
    if (!aframe) return fail("Cannot allocate audio frame");
    aframe->format = actx->sample_fmt;
    av_channel_layout_copy(&aframe->ch_layout, &actx->ch_layout);
    aframe->sample_rate = rate;
    aframe->nb_samples = audioFrameSize;
    if ((r = av_frame_get_buffer(aframe, 0)) < 0)
        return fail("Cannot allocate audio frame buffer: " + avErrorString(r));

    sws = sws_getContext(W, H, AV_PIX_FMT_RGBA, W, H, vctx->pix_fmt, SWS_BILINEAR, nullptr,
                         nullptr, nullptr);
    if (!sws) return fail("Cannot create RGBA->YUV converter");
    if (isHdOr4k) {
        const int* invTable = sws_getCoefficients(SWS_CS_DEFAULT);
        const int* table = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(sws, invTable, 1, table, 0, 0, 1 << 16, 1 << 16);
    }

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
            progress->store(
                0.98f * (static_cast<float>(vPts) / static_cast<float>(totalVideoFrames)));
        }
    }

    avcodec_send_frame(vctx, nullptr);
    if (!drainPackets(vctx, vst, fmt, pkt)) return fail("Failed flushing video encoder");
    avcodec_send_frame(actx, nullptr);
    if (!drainPackets(actx, ast, fmt, pkt)) return fail("Failed flushing audio encoder");
    if ((r = av_write_trailer(fmt)) < 0)
        return fail("Cannot finalize MP4: " + avErrorString(r));

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
        usedEncoder_.clear();
    }
    running_.store(true);
    worker_ = std::thread([this, p = std::move(project), o = std::move(opts)]() {
        std::string err;
        std::string enc;
        const bool ok = exportProject(p, o, &err, &progress_, &cancel_, &enc);
        {
            std::lock_guard<std::mutex> lock(errMutex_);
            error_ = std::move(err);
            usedEncoder_ = std::move(enc);
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

std::string ExportJob::usedEncoder() const {
    std::lock_guard<std::mutex> lock(errMutex_);
    return usedEncoder_;
}

}  // namespace vcut
