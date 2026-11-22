#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/Time.hpp"

namespace vcut {

enum class MediaType { Video, Audio, Image, Text };

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

enum class TransitionKind { None, Dissolve, DipToBlack, WipeLeft, WipeRight };

struct TextOverlay {
    std::string content = "Sample Text";
    int fontSize = 64;  // height in px relative to a 1080p reference canvas
    float color[3] = {1.0f, 1.0f, 1.0f};
    float bgColor[3] = {0.0f, 0.0f, 0.0f};
    float bgOpacity = 0.45f;
    bool shadow = true;
};

// A piece of media or a text overlay placed on the timeline.
//   timeline range: [start, start + duration())
//   source range:   [in, out)
struct Clip {
    int id = 0;
    int assetId = 0;
    Time start = 0;
    Time in = 0;
    Time out = 0;

    // Timing / playback speed
    float speed = 1.0f;  // 0.25 .. 4.0

    // Audio properties
    float volume = 1.0f;  // 0.0 .. 2.0 (0% .. 200%)
    bool muted = false;
    Time fadeIn = 0;
    Time fadeOut = 0;

    // Video transform & compositing
    float opacity = 1.0f;  // 0.0 .. 1.0
    float scale = 1.0f;    // 0.1 .. 4.0
    float posX = 0.0f;     // normalized canvas offset (-1.5 .. 1.5; 0 = centered)
    float posY = 0.0f;     // normalized canvas offset (-1.5 .. 1.5; 0 = centered)
    Time videoFadeIn = 0;
    Time videoFadeOut = 0;

    // Color adjustments & vignette
    float brightness = 0.0f;  // -1.0 .. 1.0
    float contrast = 1.0f;    //  0.0 .. 2.0
    float saturation = 1.0f;  //  0.0 .. 2.0
    float vignette = 0.0f;    //  0.0 .. 1.0
    bool grayscale = false;

    // Chroma key (green/blue screen)
    bool chromaKey = false;
    float chromaColor[3] = {0.0f, 0.85f, 0.0f};
    float chromaSimilarity = 0.28f;
    float chromaBlend = 0.08f;

    // Incoming transition at clip start
    TransitionKind transitionIn = TransitionKind::None;
    Time transitionInDur = 0;

    // Text overlay properties (used when isText == true)
    bool isText = false;
    TextOverlay text;

    Time sourceDuration() const { return out - in; }
    Time duration() const {
        if (speed == 1.0f) return out - in;
        const double sp = std::clamp(static_cast<double>(speed), 0.25, 4.0);
        return std::max<Time>(
            1, static_cast<Time>(std::llround(static_cast<double>(out - in) / sp)));
    }
    Time end() const { return start + duration(); }
    bool contains(Time t) const { return t >= start && t < end(); }
    Time timelineToSourceOffset(Time relTimeline) const {
        if (speed == 1.0f) return relTimeline;
        const double sp = std::clamp(static_cast<double>(speed), 0.25, 4.0);
        return static_cast<Time>(std::llround(static_cast<double>(relTimeline) * sp));
    }
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
inline constexpr Time kDefaultTextDuration = 4 * kSecond;

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
