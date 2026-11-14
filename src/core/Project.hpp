#pragma once

#include <string>
#include <vector>

#include "core/Time.hpp"

namespace vcut {

enum class MediaType { Video, Audio, Image };

// A file imported into the project (shown in the Media bin).
struct MediaAsset {
    int id = 0;
    std::string path;  // UTF-8, absolute
    std::string name;  // file name for display
    MediaType type = MediaType::Video;
    Time duration = 0;  // 0 for still images (they can be stretched to any length)
    int width = 0;
    int height = 0;
    double fps = 0.0;
    bool hasVideo = false;
    bool hasAudio = false;
    bool missing = false;  // file not found when the project was loaded
};

enum class TrackKind { Video, Audio };

// A piece of a media asset placed on the timeline.
//   timeline range: [start, start + (out - in))
//   source range:   [in, out)
struct Clip {
    int id = 0;
    int assetId = 0;
    Time start = 0;
    Time in = 0;
    Time out = 0;

    // Audio properties
    float volume = 1.0f;  // 0.0 .. 2.0 (0% .. 200%)
    bool muted = false;
    Time fadeIn = 0;
    Time fadeOut = 0;

    Time duration() const { return out - in; }
    Time end() const { return start + duration(); }
    bool contains(Time t) const { return t >= start && t < end(); }
};

struct Track {
    int id = 0;
    TrackKind kind = TrackKind::Video;
    std::string name;
    std::vector<Clip> clips;  // sorted by start, never overlapping
    bool muted = false;
    bool hidden = false;
};

struct ProjectSettings {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int sampleRate = 48000;

    Time frameDuration() const { return kSecond / fps; }
    Time frameTime(std::int64_t frame) const { return frame * kSecond / fps; }
};

inline constexpr Time kDefaultImageDuration = 5 * kSecond;

// The whole editable document. Tracks are listed top-to-bottom as shown in the
// timeline; for video, a track higher in the list is drawn on top.
struct Project {
    ProjectSettings settings;
    std::vector<MediaAsset> assets;
    std::vector<Track> tracks;
    int nextId = 1;

    int newId() { return nextId++; }

    const MediaAsset* findAsset(int id) const;
    MediaAsset* findAsset(int id);
    const Track* findTrack(int id) const;
    Track* findTrack(int id);

    Time duration() const;  // end of the last clip

    static Project createDefault();
};

}  // namespace vcut
