#include "media/FrameBlit.hpp"

#include <algorithm>
#include <cmath>

#include "media/FFmpeg.hpp"
#include "media/VideoDecoder.hpp"

namespace vcut {

namespace {

double averageLuma(const std::vector<std::uint8_t>& rgba) {
    if (rgba.empty()) return 0.0;
    std::uint64_t sum = 0;
    const std::size_t pixels = rgba.size() / 4;
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        sum += static_cast<std::uint64_t>(rgba[i]) + rgba[i + 1] + rgba[i + 2];
    }
    return pixels > 0 ? static_cast<double>(sum) / static_cast<double>(pixels * 3) : 0.0;
}

}  // namespace

FitRect fitRect(int srcW, int srcH, int dstW, int dstH) {
    FitRect r;
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return r;
    const double scale = std::min(double(dstW) / srcW, double(dstH) / srcH);
    r.w = std::clamp(int(std::lround(srcW * scale)), 1, dstW);
    r.h = std::clamp(int(std::lround(srcH * scale)), 1, dstH);
    r.x = (dstW - r.w) / 2;
    r.y = (dstH - r.h) / 2;
    return r;
}

FrameBlitter::~FrameBlitter() { sws_freeContext(sws_); }

void FrameBlitter::blit(const AVFrame* f, int W, int H, std::uint8_t* rgba) {
    if (!f || f->width <= 0 || f->height <= 0) return;

    int displayW = f->width;
    if (f->sample_aspect_ratio.num > 0 && f->sample_aspect_ratio.den > 0)
        displayW = int(std::lround(double(f->width) * f->sample_aspect_ratio.num /
                                   f->sample_aspect_ratio.den));
    const FitRect r = fitRect(displayW, f->height, W, H);
    if (r.w <= 0 || r.h <= 0) return;

    const auto fmt = static_cast<AVPixelFormat>(f->format);
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt);
    const bool hasAlpha = desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);

    sws_ = sws_getCachedContext(sws_, f->width, f->height, fmt, r.w, r.h, AV_PIX_FMT_RGBA,
                                SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) return;

    if (!hasAlpha) {
        // Opaque: scale straight into the canvas.
        uint8_t* dst[4] = {rgba + (std::size_t(r.y) * W + r.x) * 4, nullptr, nullptr, nullptr};
        int dstStride[4] = {W * 4, 0, 0, 0};
        sws_scale(sws_, f->data, f->linesize, 0, f->height, dst, dstStride);
        return;
    }

    // Transparent (e.g. PNG): scale to a temp buffer, then alpha-blend.
    tmp_.resize(std::size_t(r.w) * r.h * 4);
    uint8_t* dst[4] = {tmp_.data(), nullptr, nullptr, nullptr};
    int dstStride[4] = {r.w * 4, 0, 0, 0};
    sws_scale(sws_, f->data, f->linesize, 0, f->height, dst, dstStride);
    for (int y = 0; y < r.h; ++y) {
        const uint8_t* s = tmp_.data() + std::size_t(y) * r.w * 4;
        uint8_t* d = rgba + (std::size_t(r.y + y) * W + r.x) * 4;
        for (int x = 0; x < r.w; ++x, s += 4, d += 4) {
            const unsigned a = s[3];
            if (a == 255) {
                d[0] = s[0], d[1] = s[1], d[2] = s[2], d[3] = 255;
            } else if (a > 0) {
                for (int c = 0; c < 3; ++c) d[c] = uint8_t((s[c] * a + d[c] * (255 - a)) / 255);
                d[3] = 255;
            }
        }
    }
}

bool makeThumbnail(const std::string& path, Time t, int w, int h, std::vector<std::uint8_t>& rgba) {
    VideoDecoder dec;
    if (!dec.open(path)) return false;

    FrameBlitter blitter;
    auto renderAt = [&](Time sampleT) -> bool {
        const AVFrame* f = dec.frameAt(sampleT);
        if (!f) return false;
        rgba.assign(std::size_t(w) * h * 4, 0);
        for (std::size_t i = 0; i < rgba.size(); i += 4) {
            rgba[i] = rgba[i + 1] = rgba[i + 2] = 20;
            rgba[i + 3] = 255;
        }
        blitter.blit(f, w, h, rgba.data());
        return true;
    };

    if (!renderAt(t)) return false;

    // If the initial frame is nearly black (e.g. fade-in from black), probe a bit further in.
    if (!dec.isStill() && averageLuma(rgba) < 24.0) {
        std::vector<std::uint8_t> best = rgba;
        double bestLuma = averageLuma(rgba);
        for (Time cand : {t + kSecond, t + 2 * kSecond, t + 3 * kSecond}) {
            if (renderAt(cand)) {
                const double l = averageLuma(rgba);
                if (l > bestLuma) {
                    bestLuma = l;
                    best = rgba;
                }
                if (bestLuma >= 32.0) break;
            }
        }
        rgba = std::move(best);
    }
    return true;
}

}  // namespace vcut
