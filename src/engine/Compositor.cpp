#include "engine/Compositor.hpp"

#include <unordered_set>

namespace vcut {

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
        for (const Clip& c : tr.clips) {
            if (!c.contains(t)) continue;
            const MediaAsset* asset = project.findAsset(c.assetId);
            if (!asset || !asset->hasVideo || asset->missing) continue;
            if (VideoDecoder* dec = getDecoder(c.id, asset->path)) {
                const Time srcT = c.in + (t - c.start);
                if (const AVFrame* f = dec->frameAt(srcT)) {
                    blitter_.blit(f, width, height, rgba.data());
                }
            }
        }
    }
}

}  // namespace vcut
