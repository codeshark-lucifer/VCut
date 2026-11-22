#include "media/FrameBlit.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

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

inline void rgbToCbCr(float r, float g, float b, float& cb, float& cr) {
    cb = -0.168736f * r - 0.331264f * g + 0.500000f * b;
    cr = 0.500000f * r - 0.418688f * g - 0.081312f * b;
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

FitRect transformedRect(int srcW, int srcH, int dstW, int dstH, float scale, float posX, float posY) {
    FitRect base = fitRect(srcW, srcH, dstW, dstH);
    if (base.w <= 0 || base.h <= 0) return base;
    const double s = std::clamp(static_cast<double>(scale), 0.1, 4.0);
    FitRect r;
    r.w = std::max(1, static_cast<int>(std::lround(base.w * s)));
    r.h = std::max(1, static_cast<int>(std::lround(base.h * s)));
    const double cx = dstW * (0.5 + static_cast<double>(posX));
    const double cy = dstH * (0.5 + static_cast<double>(posY));
    r.x = static_cast<int>(std::lround(cx - r.w * 0.5));
    r.y = static_cast<int>(std::lround(cy - r.h * 0.5));
    return r;
}

FrameBlitter::~FrameBlitter() { sws_freeContext(sws_); }

void FrameBlitter::blit(const AVFrame* f, int W, int H, std::uint8_t* rgba,
                        const FrameBlitParams& params) {
    if (!f || f->width <= 0 || f->height <= 0 || W <= 0 || H <= 0 || !rgba) return;
    if (params.opacity <= 0.001f) return;

    int displayW = f->width;
    if (f->sample_aspect_ratio.num > 0 && f->sample_aspect_ratio.den > 0)
        displayW = int(std::lround(double(f->width) * f->sample_aspect_ratio.num /
                                   f->sample_aspect_ratio.den));
    const FitRect r =
        transformedRect(displayW, f->height, W, H, params.scale, params.posX, params.posY);
    if (r.w <= 0 || r.h <= 0) return;

    const int wipeX0 = std::clamp(static_cast<int>(std::floor(params.wipeMinX * W)), 0, W);
    const int wipeX1 = std::clamp(static_cast<int>(std::ceil(params.wipeMaxX * W)), 0, W);
    const int dstX0 = std::max(wipeX0, r.x);
    const int dstY0 = std::max(0, r.y);
    const int dstX1 = std::min(wipeX1, r.x + r.w);
    const int dstY1 = std::min(H, r.y + r.h);
    if (dstX1 <= dstX0 || dstY1 <= dstY0) return;

    const auto fmt = static_cast<AVPixelFormat>(f->format);
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt);
    const bool hasAlpha = desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);
    const bool hasColorGrade = std::fabs(params.brightness) > 0.002f ||
                               std::fabs(params.contrast - 1.0f) > 0.002f ||
                               std::fabs(params.saturation - 1.0f) > 0.002f ||
                               params.vignette > 0.002f || params.grayscale;
    const bool fullyInsideWipe =
        (dstX0 == r.x && dstX1 == r.x + r.w && dstY0 == r.y && dstY1 == r.y + r.h);
    const bool canDirectWrite =
        !hasAlpha && !hasColorGrade && !params.chromaKey && params.opacity >= 0.999f &&
        fullyInsideWipe;

    if (canDirectWrite) {
        const int swsFlags =
            (r.w > f->width || r.h > f->height) ? SWS_BICUBIC : SWS_BILINEAR;
        sws_ = sws_getCachedContext(sws_, f->width, f->height, fmt, r.w, r.h, AV_PIX_FMT_RGBA,
                                    swsFlags, nullptr, nullptr, nullptr);
        if (!sws_) return;
        uint8_t* dst[4] = {rgba + (std::size_t(r.y) * W + r.x) * 4, nullptr, nullptr, nullptr};
        int dstStride[4] = {W * 4, 0, 0, 0};
        sws_scale(sws_, f->data, f->linesize, 0, f->height, dst, dstStride);
        return;
    }

    // For very large zoom-ins (e.g. >1.5x canvas size), convert at native source size and sample
    // only the visible viewport slice so memory stays bounded even on 4K canvases.
    const bool sampleOnFly = (r.w > W * 3 / 2 || r.h > H * 3 / 2);
    const int bufW = sampleOnFly ? std::min(r.w, f->width) : r.w;
    const int bufH = sampleOnFly ? std::min(r.h, f->height) : r.h;
    const int swsFlags =
        (bufW > f->width || bufH > f->height) ? SWS_BICUBIC : SWS_BILINEAR;
    sws_ = sws_getCachedContext(sws_, f->width, f->height, fmt, bufW, bufH, AV_PIX_FMT_RGBA,
                                swsFlags, nullptr, nullptr, nullptr);
    if (!sws_) return;

    tmp_.resize(std::size_t(bufW) * bufH * 4);
    uint8_t* tmpDst[4] = {tmp_.data(), nullptr, nullptr, nullptr};
    int tmpStride[4] = {bufW * 4, 0, 0, 0};
    sws_scale(sws_, f->data, f->linesize, 0, f->height, tmpDst, tmpStride);

    float keyCb = 0.0f, keyCr = 0.0f;
    if (params.chromaKey) {
        rgbToCbCr(params.chromaColor[0], params.chromaColor[1], params.chromaColor[2], keyCb,
                  keyCr);
    }
    const float keySim = std::max(0.01f, params.chromaSimilarity);
    const float keyBlend = std::max(0.001f, params.chromaBlend);
    const float brightOffset = params.brightness * 255.0f;
    const float sat = params.grayscale ? 0.0f : params.saturation;
    const float invHalfW = 2.0f / static_cast<float>(W);
    const float invHalfH = 2.0f / static_cast<float>(H);

    for (int y = dstY0; y < dstY1; ++y) {
        const int srcY = sampleOnFly
                             ? std::clamp(((y - r.y) * bufH) / std::max(1, r.h), 0, bufH - 1)
                             : (y - r.y);
        const uint8_t* srcRow = tmp_.data() + std::size_t(srcY) * bufW * 4;
        uint8_t* d = rgba + (std::size_t(y) * W + dstX0) * 4;
        const float ny = y * invHalfH - 1.0f;
        const float ny2 = ny * ny;

        for (int x = dstX0; x < dstX1; ++x, d += 4) {
            const int srcX = sampleOnFly
                                 ? std::clamp(((x - r.x) * bufW) / std::max(1, r.w), 0, bufW - 1)
                                 : (x - r.x);
            const uint8_t* s = srcRow + std::size_t(srcX) * 4;

            float sr = static_cast<float>(s[0]);
            float sg = static_cast<float>(s[1]);
            float sb = static_cast<float>(s[2]);
            float alpha = (hasAlpha ? (static_cast<float>(s[3]) / 255.0f) : 1.0f) * params.opacity;

            if (params.chromaKey && alpha > 0.0f) {
                float cb = 0.0f, cr = 0.0f;
                rgbToCbCr(sr / 255.0f, sg / 255.0f, sb / 255.0f, cb, cr);
                const float dist = std::sqrt((cb - keyCb) * (cb - keyCb) + (cr - keyCr) * (cr - keyCr));
                if (dist <= keySim) {
                    alpha = 0.0f;
                } else if (dist < keySim + keyBlend) {
                    const float mask = (dist - keySim) / keyBlend;
                    alpha *= mask;
                    // Light green spill reduction on semi-transparent edges
                    if (params.chromaColor[1] >= params.chromaColor[0] &&
                        params.chromaColor[1] >= params.chromaColor[2]) {
                        const float maxRB = std::max(sr, sb);
                        if (sg > maxRB) sg = sg * mask + maxRB * (1.0f - mask);
                    }
                }
            }
            if (alpha <= 0.001f) continue;

            if (hasColorGrade) {
                sr = (sr - 128.0f) * params.contrast + 128.0f + brightOffset;
                sg = (sg - 128.0f) * params.contrast + 128.0f + brightOffset;
                sb = (sb - 128.0f) * params.contrast + 128.0f + brightOffset;

                if (std::fabs(sat - 1.0f) > 0.002f) {
                    const float luma = 0.2126f * sr + 0.7152f * sg + 0.0722f * sb;
                    sr = luma + (sr - luma) * sat;
                    sg = luma + (sg - luma) * sat;
                    sb = luma + (sb - luma) * sat;
                }

                if (params.vignette > 0.002f) {
                    const float nx = x * invHalfW - 1.0f;
                    const float r2 = std::min(1.5f, (nx * nx + ny2) * 0.5f);
                    const float vMul = std::max(0.0f, 1.0f - params.vignette * r2 * 0.95f);
                    sr *= vMul;
                    sg *= vMul;
                    sb *= vMul;
                }

                sr = std::clamp(sr, 0.0f, 255.0f);
                sg = std::clamp(sg, 0.0f, 255.0f);
                sb = std::clamp(sb, 0.0f, 255.0f);
            }

            if (alpha >= 0.999f) {
                d[0] = static_cast<uint8_t>(sr);
                d[1] = static_cast<uint8_t>(sg);
                d[2] = static_cast<uint8_t>(sb);
                d[3] = 255;
            } else {
                const float inv = 1.0f - alpha;
                d[0] = static_cast<uint8_t>(std::clamp(sr * alpha + d[0] * inv, 0.0f, 255.0f));
                d[1] = static_cast<uint8_t>(std::clamp(sg * alpha + d[1] * inv, 0.0f, 255.0f));
                d[2] = static_cast<uint8_t>(std::clamp(sb * alpha + d[2] * inv, 0.0f, 255.0f));
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
