#include "engine/Compositor.hpp"

#include <algorithm>
#include <unordered_set>

namespace vcut {

namespace {

float clipVideoFade(const Clip& c, Time relTimeline) {
    float env = 1.0f;
    const Time dur = c.duration();
    if (c.videoFadeIn > 0 && relTimeline < c.videoFadeIn) {
        env = std::min(env, static_cast<float>(relTimeline) / static_cast<float>(c.videoFadeIn));
    }
    if (c.videoFadeOut > 0 && dur - relTimeline < c.videoFadeOut) {
        env = std::min(env,
                       static_cast<float>(dur - relTimeline) / static_cast<float>(c.videoFadeOut));
    }
    return std::clamp(env, 0.0f, 1.0f);
}

}  // namespace

void Compositor::clear() { decoders_.clear(); }

VideoDecoder* Compositor::getDecoder(int key, const std::string& path) {
    auto& e = decoders_[key];
    if (e.path != path) {
        e.path = path;
        e.decoder = std::make_unique<VideoDecoder>();
        e.failed = !e.decoder->open(path);
        if (e.failed) e.decoder.reset();
    }
    return e.decoder.get();
}

void Compositor::pruneUnused(const Project& project) {
    if (decoders_.size() <= 16) return;
    std::unordered_set<int> active;
    for (const auto& tr : project.tracks)
        for (const auto& c : tr.clips) active.insert(c.id);
    for (auto it = decoders_.begin(); it != decoders_.end();) {
        if (active.find(it->first) == active.end())
            it = decoders_.erase(it);
        else
            ++it;
    }
}

FitRect Compositor::clipScreenBox(const Project& project, const Clip& c, int width, int height) {
    if (width <= 0 || height <= 0) return {};
    if (c.isText) {
        const auto box =
            textRenderer_.measure(c.text, width, height, c.scale, c.posX, c.posY);
        return {box.x, box.y, box.w, box.h};
    }
    const MediaAsset* asset = project.findAsset(c.assetId);
    if (!asset || asset->width <= 0 || asset->height <= 0) return {};
    return transformedRect(asset->width, asset->height, width, height, c.scale, c.posX, c.posY);
}

void Compositor::renderClipAt(const Project& project, const Clip& c, Time relTimeline, int width,
                              int height, std::vector<std::uint8_t>& rgba, float opacityMul,
                              float wipeMinX, float wipeMaxX) {
    relTimeline = std::clamp<Time>(relTimeline, 0, std::max<Time>(0, c.duration() - 1));
    const float effectiveOpacity =
        std::clamp(c.opacity * clipVideoFade(c, relTimeline) * opacityMul, 0.0f, 1.0f);
    if (effectiveOpacity <= 0.001f) return;

    if (c.isText) {
        textRenderer_.render(c.text, width, height, c.scale, c.posX, c.posY, effectiveOpacity,
                             rgba.data(), wipeMinX, wipeMaxX);
        return;
    }

    const MediaAsset* asset = project.findAsset(c.assetId);
    if (!asset || !asset->hasVideo || asset->missing) return;

    if (VideoDecoder* dec = getDecoder(c.id, asset->path)) {
        const Time srcT = c.in + c.timelineToSourceOffset(relTimeline);
        if (const AVFrame* f = dec->frameAt(srcT)) {
            FrameBlitParams bp;
            bp.opacity = effectiveOpacity;
            bp.scale = c.scale;
            bp.posX = c.posX;
            bp.posY = c.posY;
            bp.brightness = c.brightness;
            bp.contrast = c.contrast;
            bp.saturation = c.saturation;
            bp.vignette = c.vignette;
            bp.grayscale = c.grayscale;
            bp.chromaKey = c.chromaKey;
            bp.chromaColor[0] = c.chromaColor[0];
            bp.chromaColor[1] = c.chromaColor[1];
            bp.chromaColor[2] = c.chromaColor[2];
            bp.chromaSimilarity = c.chromaSimilarity;
            bp.chromaBlend = c.chromaBlend;
            bp.wipeMinX = wipeMinX;
            bp.wipeMaxX = wipeMaxX;
            blitter_.blit(f, width, height, rgba.data(), bp);
        }
    }
}

void Compositor::render(const Project& project, Time t, int width, int height,
                        std::vector<std::uint8_t>& rgba) {
    if (width <= 0 || height <= 0) return;
    pruneUnused(project);

    const std::size_t total = static_cast<std::size_t>(width) * height * 4;
    rgba.resize(total);
    for (std::size_t i = 0; i < total; i += 4) {
        rgba[i] = 0;
        rgba[i + 1] = 0;
        rgba[i + 2] = 0;
        rgba[i + 3] = 255;
    }

    // Tracks are listed top-to-bottom in the UI; composite bottom-to-top so
    // higher tracks draw over lower tracks.
    for (auto it = project.tracks.rbegin(); it != project.tracks.rend(); ++it) {
        const Track& tr = *it;
        if (tr.kind != TrackKind::Video || tr.hidden) continue;

        for (std::size_t ci = 0; ci < tr.clips.size(); ++ci) {
            const Clip& c = tr.clips[ci];
            if (!c.contains(t)) continue;

            const Time rel = t - c.start;
            const Time transDur = std::min(c.transitionInDur, c.duration());
            if (c.transitionIn != TransitionKind::None && transDur > 0 && rel < transDur) {
                const float prog =
                    std::clamp(static_cast<float>(rel) / static_cast<float>(transDur), 0.0f, 1.0f);
                const Clip* prev =
                    (ci > 0 && c.start - tr.clips[ci - 1].end() <= 100'000) ? &tr.clips[ci - 1]
                                                                            : nullptr;

                if (c.transitionIn == TransitionKind::DipToBlack) {
                    if (prog < 0.5f) {
                        if (prev) {
                            renderClipAt(project, *prev, prev->duration() - 1, width, height, rgba,
                                         1.0f - prog * 2.0f, 0.0f, 1.0f);
                        }
                    } else {
                        renderClipAt(project, c, rel, width, height, rgba, (prog - 0.5f) * 2.0f,
                                     0.0f, 1.0f);
                    }
                } else if (c.transitionIn == TransitionKind::WipeLeft) {
                    if (prev) {
                        renderClipAt(project, *prev, prev->duration() - 1, width, height, rgba,
                                     1.0f, 0.0f, 1.0f);
                    }
                    renderClipAt(project, c, rel, width, height, rgba, 1.0f, 0.0f, prog);
                } else if (c.transitionIn == TransitionKind::WipeRight) {
                    if (prev) {
                        renderClipAt(project, *prev, prev->duration() - 1, width, height, rgba,
                                     1.0f, 0.0f, 1.0f);
                    }
                    renderClipAt(project, c, rel, width, height, rgba, 1.0f, 1.0f - prog, 1.0f);
                } else {
                    // Dissolve
                    if (prev) {
                        renderClipAt(project, *prev, prev->duration() - 1, width, height, rgba,
                                     1.0f, 0.0f, 1.0f);
                    }
                    renderClipAt(project, c, rel, width, height, rgba, prog, 0.0f, 1.0f);
                }
            } else {
                renderClipAt(project, c, rel, width, height, rgba, 1.0f, 0.0f, 1.0f);
            }
        }
    }
}

}  // namespace vcut
