#include "core/Project.hpp"

#include <algorithm>
#include <utility>

namespace vcut {

const MediaAsset* Project::findAsset(int id) const {
    for (const auto& a : assets)
        if (a.id == id) return &a;
    return nullptr;
}

MediaAsset* Project::findAsset(int id) {
    return const_cast<MediaAsset*>(std::as_const(*this).findAsset(id));
}

const Track* Project::findTrack(int id) const {
    for (const auto& t : tracks)
        if (t.id == id) return &t;
    return nullptr;
}

Track* Project::findTrack(int id) { return const_cast<Track*>(std::as_const(*this).findTrack(id)); }

Time Project::duration() const {
    Time d = 0;
    for (const auto& t : tracks)
        for (const auto& c : t.clips) d = std::max(d, c.end());
    return d;
}

Project Project::createDefault() {
    Project p;
    Track video;
    video.id = p.newId();
    video.kind = TrackKind::Video;
    video.name = "Video 1";
    Track audio;
    audio.id = p.newId();
    audio.kind = TrackKind::Audio;
    audio.name = "Audio 1";
    p.tracks.push_back(std::move(video));
    p.tracks.push_back(std::move(audio));
    return p;
}

}  // namespace vcut
