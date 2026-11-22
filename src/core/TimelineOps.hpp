#pragma once

#include <string>
#include <vector>

#include "core/Project.hpp"

// Pure editing operations on a Project. No UI, no media decoding.
namespace vcut::ops {

struct ClipRef {
    Track* track = nullptr;
    Clip* clip = nullptr;
    explicit operator bool() const { return clip != nullptr; }
};

ClipRef findClip(Project& p, int clipId);
const Clip* findClip(const Project& p, int clipId, const Track** outTrack = nullptr);

// Video tracks take videos/images/text; audio tracks take anything with sound.
bool trackAccepts(const Track& track, const MediaAsset& asset);
bool clipAcceptsTrack(const Track& track, const Clip& clip, const Project& p);

// True if [start, end) does not overlap any clip on the track (except ignoreClipId).
bool isFree(const Track& track, Time start, Time end, int ignoreClipId = 0);

// Closest start >= 0 to `desired` where a clip of `length` fits without overlap.
Time findFreeStart(const Track& track, Time desired, Time length, int ignoreClipId = 0);

Time defaultClipDuration(const MediaAsset& asset);

// Returns the new clip id, or 0 on failure. Overlaps are resolved by moving to the nearest gap.
int addClip(Project& p, int trackId, int assetId, Time start);
// Adds the asset at the end of the main track of the matching kind.
int appendClip(Project& p, int assetId);

// Adds a text overlay clip onto `trackId` (or uppermost free Video track when trackId == 0).
int addTextClip(Project& p, int trackId, Time start,
                const std::string& text = "Sample Text",
                Time duration = kDefaultTextDuration);

// Imports a SubRip (.srt) file onto a new top Video overlay track; returns count created.
int importSrtSubtitles(Project& p, const std::string& path, std::string* error = nullptr);

bool moveClip(Project& p, int clipId, int newTrackId, Time newStart);
// Move the left edge (end stays fixed). Clamped to source bounds and neighbours.
bool trimLeft(Project& p, int clipId, Time newStart);
// Move the right edge (start stays fixed).
bool trimRight(Project& p, int clipId, Time newEnd);
// Change clip playback speed (0.25 .. 4.0) while respecting track neighbors.
bool setClipSpeed(Project& p, int clipId, float newSpeed);

// Split a clip at timeline time t; returns id of the new right-hand clip, or 0.
int splitClip(Project& p, int clipId, Time t);
// Split the preferred clip if it is under t, otherwise every clip under t.
std::vector<int> splitAt(Project& p, Time t, int preferredClipId = 0);

// Extracts the audio of a video clip into a separate clip on an Audio track and mutes the video
// clip's embedded audio. Returns the new audio clip id, or 0 on failure.
int extractAudio(Project& p, int videoClipId);

// Duplicate / Paste clip on the same or matching track, returning new clip id or 0.
int duplicateClip(Project& p, int clipId);
int pasteClip(Project& p, const Clip& srcClip, int preferredTrackId, Time targetStart);

bool deleteClip(Project& p, int clipId);
// Delete clip and shift subsequent clips on that track left to close the gap.
bool rippleDeleteClip(Project& p, int clipId);

// Snap t to clip edges / playhead / 0 if within threshold.
Time snapTime(const Project& p, Time t, Time threshold, Time playhead, int ignoreClipId,
              bool* snapped = nullptr);

// Snap a moving clip of length `clipDuration` by either its start or end edge.
Time snapClipMove(const Project& p, Time desiredStart, Time clipDuration, Time threshold,
                  Time playhead, int ignoreClipId, Time* outSnapLine = nullptr);

int addTrack(Project& p, TrackKind kind);
bool removeTrack(Project& p, int trackId);  // keeps at least one track of each kind
bool moveTrack(Project& p, int trackId, int delta);  // delta = -1 (up) or +1 (down) within kind
bool removeAsset(Project& p, int assetId);  // also removes its clips

void sortTrack(Track& track);

}  // namespace vcut::ops
