#include "core/TimelineOps.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>

namespace vcut::ops {

namespace {

Time minClipDuration(const Project& p) { return p.settings.frameDuration(); }
bool isStill(const MediaAsset& a) { return a.type == MediaType::Image; }

void clampFades(Clip& c) {
    const Time dur = c.duration();
    c.fadeIn = std::clamp<Time>(c.fadeIn, 0, dur);
    c.fadeOut = std::clamp<Time>(c.fadeOut, 0, dur - c.fadeIn);
}

}  // namespace

void sortTrack(Track& track) {
    std::sort(track.clips.begin(), track.clips.end(),
              [](const Clip& a, const Clip& b) { return a.start < b.start; });
}

ClipRef findClip(Project& p, int clipId) {
    if (clipId == 0) return {};
    for (auto& t : p.tracks)
        for (auto& c : t.clips)
            if (c.id == clipId) return {&t, &c};
    return {};
}

const Clip* findClip(const Project& p, int clipId, const Track** outTrack) {
    if (clipId == 0) return nullptr;
    for (const auto& t : p.tracks)
        for (const auto& c : t.clips)
            if (c.id == clipId) {
                if (outTrack) *outTrack = &t;
                return &c;
            }
    return nullptr;
}

bool trackAccepts(const Track& track, const MediaAsset& asset) {
    if (track.kind == TrackKind::Video) return asset.hasVideo;
    return asset.hasAudio && asset.type != MediaType::Image;
}

bool isFree(const Track& track, Time start, Time end, int ignoreClipId) {
    for (const auto& c : track.clips) {
        if (c.id == ignoreClipId) continue;
        if (start < c.end() && c.start < end) return false;
    }
    return true;
}

Time findFreeStart(const Track& track, Time desired, Time length, int ignoreClipId) {
    desired = std::max<Time>(0, desired);
    if (isFree(track, desired, desired + length, ignoreClipId)) return desired;

    std::vector<Time> candidates{0};
    for (const auto& c : track.clips) {
        if (c.id == ignoreClipId) continue;
        candidates.push_back(c.end());
        if (c.start - length >= 0) candidates.push_back(c.start - length);
    }
    Time best = -1;
    Time bestDist = std::numeric_limits<Time>::max();
    for (Time s : candidates) {
        if (!isFree(track, s, s + length, ignoreClipId)) continue;
        const Time d = std::llabs(s - desired);
        if (d < bestDist) {
            best = s;
            bestDist = d;
        }
    }
    return best;  // the end of the last clip is always free, so best >= 0
}

Time defaultClipDuration(const MediaAsset& asset) {
    return isStill(asset) ? kDefaultImageDuration : asset.duration;
}

int addClip(Project& p, int trackId, int assetId, Time start) {
    Track* track = p.findTrack(trackId);
    const MediaAsset* asset = p.findAsset(assetId);
    if (!track || !asset || !trackAccepts(*track, *asset)) return 0;
    const Time len = defaultClipDuration(*asset);
    if (len <= 0) return 0;

    Clip c;
    c.id = p.newId();
    c.assetId = assetId;
    c.in = 0;
    c.out = len;
    c.start = findFreeStart(*track, start, len);
    track->clips.push_back(c);
    sortTrack(*track);
    return c.id;
}

int appendClip(Project& p, int assetId) {
    const MediaAsset* asset = p.findAsset(assetId);
    if (!asset) return 0;
    const TrackKind kind = asset->hasVideo ? TrackKind::Video : TrackKind::Audio;

    // Main video track = bottom-most video track; main audio track = first audio track.
    Track* target = nullptr;
    for (auto& t : p.tracks) {
        if (t.kind != kind) continue;
        target = &t;
        if (kind == TrackKind::Audio) break;
    }
    if (!target) target = p.findTrack(addTrack(p, kind));

    Time end = 0;
    for (const auto& c : target->clips) end = std::max(end, c.end());
    return addClip(p, target->id, assetId, end);
}

bool moveClip(Project& p, int clipId, int newTrackId, Time newStart) {
    ClipRef ref = findClip(p, clipId);
    if (!ref) return false;
    Track* dst = p.findTrack(newTrackId);
    const MediaAsset* asset = p.findAsset(ref.clip->assetId);
    if (!dst || !asset || !trackAccepts(*dst, *asset)) return false;

    Clip c = *ref.clip;
    const Time s = findFreeStart(*dst, newStart, c.duration(), clipId);
    if (dst == ref.track) {
        ref.clip->start = s;
        sortTrack(*dst);
        return true;
    }
    auto& src = ref.track->clips;
    src.erase(std::remove_if(src.begin(), src.end(), [&](const Clip& x) { return x.id == clipId; }),
              src.end());
    c.start = s;
    dst->clips.push_back(c);
    sortTrack(*dst);
    return true;
}

bool trimLeft(Project& p, int clipId, Time newStart) {
    ClipRef ref = findClip(p, clipId);
    if (!ref) return false;
    Clip& c = *ref.clip;
    const MediaAsset* asset = p.findAsset(c.assetId);
    if (!asset) return false;

    Time lo = 0;
    for (const auto& o : ref.track->clips)
        if (o.id != c.id && o.end() <= c.start) lo = std::max(lo, o.end());
    if (!isStill(*asset)) lo = std::max(lo, c.start - c.in);  // can't go before source start
    const Time hi = c.end() - minClipDuration(p);
    if (hi < lo) return false;

    newStart = std::clamp(newStart, lo, hi);
    const Time delta = newStart - c.start;
    if (isStill(*asset))
        c.out -= delta;  // stills have no real source range: just change the length
    else
        c.in += delta;
    c.start = newStart;
    clampFades(c);
    return true;
}

bool trimRight(Project& p, int clipId, Time newEnd) {
    ClipRef ref = findClip(p, clipId);
    if (!ref) return false;
    Clip& c = *ref.clip;
    const MediaAsset* asset = p.findAsset(c.assetId);
    if (!asset) return false;

    Time hi = std::numeric_limits<Time>::max() / 4;
    for (const auto& o : ref.track->clips)
        if (o.id != c.id && o.start >= c.end()) hi = std::min(hi, o.start);
    if (!isStill(*asset)) hi = std::min(hi, c.start + (asset->duration - c.in));
    const Time lo = c.start + minClipDuration(p);
    if (hi < lo) return false;

    newEnd = std::clamp(newEnd, lo, hi);
    c.out = c.in + (newEnd - c.start);
    clampFades(c);
    return true;
}

int splitClip(Project& p, int clipId, Time t) {
    ClipRef ref = findClip(p, clipId);
    if (!ref) return 0;
    Clip& c = *ref.clip;
    const Time minDur = minClipDuration(p);
    if (t < c.start + minDur || t > c.end() - minDur) return 0;

    const MediaAsset* asset = p.findAsset(c.assetId);
    const Time offset = t - c.start;
    Clip right = c;
    right.id = p.newId();
    right.start = t;
    if (asset && isStill(*asset)) {
        right.in = 0;
        right.out = c.end() - t;
        c.out = offset;
    } else {
        right.in = c.in + offset;
        c.out = c.in + offset;
    }
    c.fadeOut = 0;
    clampFades(c);
    right.fadeIn = 0;
    clampFades(right);

    Track* track = ref.track;  // `c` is invalid after push_back
    track->clips.push_back(right);
    sortTrack(*track);
    return right.id;
}

std::vector<int> splitAt(Project& p, Time t, int preferredClipId) {
    if (preferredClipId) {
        ClipRef ref = findClip(p, preferredClipId);
        if (ref && ref.clip->contains(t)) {
            const int id = splitClip(p, preferredClipId, t);
            return id ? std::vector<int>{id} : std::vector<int>{};
        }
    }
    std::vector<int> targets;
    for (const auto& tr : p.tracks)
        for (const auto& c : tr.clips)
            if (c.contains(t)) targets.push_back(c.id);
    std::vector<int> created;
    for (int id : targets)
        if (int n = splitClip(p, id, t)) created.push_back(n);
    return created;
}

int extractAudio(Project& p, int videoClipId) {
    ClipRef ref = findClip(p, videoClipId);
    if (!ref || ref.track->kind != TrackKind::Video) return 0;
    const MediaAsset* asset = p.findAsset(ref.clip->assetId);
    if (!asset || !asset->hasAudio) return 0;

    const Clip srcClip = *ref.clip;
    ref.clip->muted = true;  // mute video clip so extracted audio doesn't double-play

    Track* targetAudio = nullptr;
    for (auto& t : p.tracks) {
        if (t.kind == TrackKind::Audio && isFree(t, srcClip.start, srcClip.end())) {
            targetAudio = &t;
            break;
        }
    }
    if (!targetAudio) {
        const int newTrackId = addTrack(p, TrackKind::Audio);
        targetAudio = p.findTrack(newTrackId);
    }
    if (!targetAudio) return 0;

    Clip audioClip = srcClip;
    audioClip.id = p.newId();
    audioClip.muted = false;
    audioClip.start = findFreeStart(*targetAudio, srcClip.start, srcClip.duration());
    targetAudio->clips.push_back(audioClip);
    sortTrack(*targetAudio);
    return audioClip.id;
}

bool deleteClip(Project& p, int clipId) {
    for (auto& t : p.tracks) {
        auto it = std::find_if(t.clips.begin(), t.clips.end(),
                               [&](const Clip& c) { return c.id == clipId; });
        if (it != t.clips.end()) {
            t.clips.erase(it);
            return true;
        }
    }
    return false;
}

Time snapTime(const Project& p, Time t, Time threshold, Time playhead, int ignoreClipId,
              bool* snapped) {
    Time best = t;
    Time bestDist = threshold + 1;
    auto consider = [&](Time candidate) {
        const Time d = std::llabs(candidate - t);
        if (d <= threshold && d < bestDist) {
            best = candidate;
            bestDist = d;
        }
    };
    consider(0);
    consider(playhead);
    for (const auto& tr : p.tracks)
        for (const auto& c : tr.clips) {
            if (c.id == ignoreClipId) continue;
            consider(c.start);
            consider(c.end());
        }
    if (snapped) *snapped = bestDist <= threshold;
    return best;
}

Time snapClipMove(const Project& p, Time desiredStart, Time clipDuration, Time threshold,
                  Time playhead, int ignoreClipId, Time* outSnapLine) {
    if (threshold <= 0) {
        if (outSnapLine) *outSnapLine = -1;
        return std::max<Time>(0, desiredStart);
    }
    bool snappedLeft = false;
    bool snappedRight = false;
    const Time leftSnap = snapTime(p, desiredStart, threshold, playhead, ignoreClipId, &snappedLeft);
    const Time rightSnap =
        snapTime(p, desiredStart + clipDuration, threshold, playhead, ignoreClipId, &snappedRight);

    const Time dLeft = snappedLeft ? std::llabs(leftSnap - desiredStart) : threshold + 1;
    const Time dRight =
        snappedRight ? std::llabs(rightSnap - (desiredStart + clipDuration)) : threshold + 1;

    if (snappedLeft && dLeft <= dRight) {
        if (outSnapLine) *outSnapLine = leftSnap;
        return std::max<Time>(0, leftSnap);
    }
    if (snappedRight && rightSnap - clipDuration >= 0) {
        if (outSnapLine) *outSnapLine = rightSnap;
        return rightSnap - clipDuration;
    }
    if (outSnapLine) *outSnapLine = -1;
    return std::max<Time>(0, desiredStart);
}

int addTrack(Project& p, TrackKind kind) {
    int count = 1;
    for (const auto& t : p.tracks)
        if (t.kind == kind) ++count;
    Track t;
    t.id = p.newId();
    t.kind = kind;
    t.name = (kind == TrackKind::Video ? "Video " : "Audio ") + std::to_string(count);
    const int id = t.id;
    if (kind == TrackKind::Video)
        p.tracks.insert(p.tracks.begin(), std::move(t));  // new video tracks go on top
    else
        p.tracks.push_back(std::move(t));
    return id;
}

bool removeTrack(Project& p, int trackId) {
    const Track* track = p.findTrack(trackId);
    if (!track) return false;
    int sameKind = 0;
    for (const auto& t : p.tracks)
        if (t.kind == track->kind) ++sameKind;
    if (sameKind <= 1) return false;
    p.tracks.erase(std::remove_if(p.tracks.begin(), p.tracks.end(),
                                  [&](const Track& t) { return t.id == trackId; }),
                   p.tracks.end());
    return true;
}

bool removeAsset(Project& p, int assetId) {
    auto it = std::find_if(p.assets.begin(), p.assets.end(),
                           [&](const MediaAsset& a) { return a.id == assetId; });
    if (it == p.assets.end()) return false;
    for (auto& t : p.tracks)
        t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(),
                                     [&](const Clip& c) { return c.assetId == assetId; }),
                      t.clips.end());
    p.assets.erase(it);
    return true;
}

}  // namespace vcut::ops
