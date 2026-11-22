#include "core/TimelineOps.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#include "core/PathUtil.hpp"

namespace vcut::ops {

namespace {

Time minClipDuration(const Project& p) { return std::max<Time>(1, p.settings.frameDuration()); }
bool isStill(const MediaAsset& a) { return a.type == MediaType::Image || a.type == MediaType::Text; }
bool isElasticClip(const Clip& c, const MediaAsset* a) {
    return c.isText || (a && isStill(*a));
}

void clampFades(Clip& c) {
    const Time dur = c.duration();
    c.fadeIn = std::clamp<Time>(c.fadeIn, 0, dur);
    c.fadeOut = std::clamp<Time>(c.fadeOut, 0, dur - c.fadeIn);
    c.videoFadeIn = std::clamp<Time>(c.videoFadeIn, 0, dur);
    c.videoFadeOut = std::clamp<Time>(c.videoFadeOut, 0, dur - c.videoFadeIn);
    c.transitionInDur = std::clamp<Time>(c.transitionInDur, 0, dur);
}

bool parseSrtTime(const std::string& s, Time& out) {
    int hh = 0, mm = 0, ss = 0, ms = 0;
    if (std::sscanf(s.c_str(), "%d:%d:%d,%d", &hh, &mm, &ss, &ms) != 4 &&
        std::sscanf(s.c_str(), "%d:%d:%d.%d", &hh, &mm, &ss, &ms) != 4) {
        return false;
    }
    if (hh < 0 || mm < 0 || ss < 0 || ms < 0) return false;
    out = (static_cast<Time>(hh) * 3600 + static_cast<Time>(mm) * 60 + ss) * kSecond +
          static_cast<Time>(ms) * 1000;
    return true;
}

std::string trimCrWs(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) ++i;
    if (i > 0) s.erase(0, i);
    return s;
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
    if (track.kind == TrackKind::Video) return asset.hasVideo || asset.type == MediaType::Text;
    return asset.hasAudio && asset.type != MediaType::Image && asset.type != MediaType::Text;
}

bool clipAcceptsTrack(const Track& track, const Clip& clip, const Project& p) {
    if (clip.isText) return track.kind == TrackKind::Video;
    const MediaAsset* asset = p.findAsset(clip.assetId);
    return asset != nullptr && trackAccepts(track, *asset);
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
    if (asset.type == MediaType::Text) return kDefaultTextDuration;
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
    if (asset->type == MediaType::Text) {
        c.isText = true;
        c.text.content = asset->name.empty() ? "Sample Text" : asset->name;
    }
    const int newId = c.id;
    track->clips.push_back(std::move(c));
    sortTrack(*track);
    return newId;
}

int appendClip(Project& p, int assetId) {
    const MediaAsset* asset = p.findAsset(assetId);
    if (!asset) return 0;
    const TrackKind kind =
        (asset->hasVideo || asset->type == MediaType::Text) ? TrackKind::Video : TrackKind::Audio;

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

int addTextClip(Project& p, int trackId, Time start, const std::string& text, Time duration) {
    if (duration <= 0) duration = kDefaultTextDuration;
    start = std::max<Time>(0, start);

    Track* target = trackId != 0 ? p.findTrack(trackId) : nullptr;
    if (target && target->kind != TrackKind::Video) target = nullptr;

    if (!target) {
        // Pick the topmost Video track where [start, start + duration) is free.
        for (auto& t : p.tracks) {
            if (t.kind == TrackKind::Video && isFree(t, start, start + duration)) {
                target = &t;
                break;
            }
        }
        if (!target) target = p.findTrack(addTrack(p, TrackKind::Video));
    }
    if (!target) return 0;

    Clip c;
    c.id = p.newId();
    c.assetId = 0;
    c.isText = true;
    c.text.content = text.empty() ? "Sample Text" : text;
    c.in = 0;
    c.out = duration;
    c.start = findFreeStart(*target, start, duration);
    const int id = c.id;
    target->clips.push_back(std::move(c));
    sortTrack(*target);
    return id;
}

int importSrtSubtitles(Project& p, const std::string& path, std::string* error) {
    std::ifstream in(pathFromUtf8(path), std::ios::binary);
    if (!in) {
        if (error) *error = "Cannot open subtitle file: " + path;
        return 0;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();
    // Strip UTF-8 BOM if present
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }

    struct Cue {
        Time start = 0;
        Time end = 0;
        std::string text;
    };
    std::vector<Cue> cues;

    std::istringstream lines(raw);
    std::string line;
    while (std::getline(lines, line)) {
        line = trimCrWs(line);
        if (line.empty()) continue;

        // Accept either cue index followed by timing line, or timing line directly.
        if (line.find("-->") == std::string::npos) {
            if (!std::getline(lines, line)) break;
            line = trimCrWs(line);
        }
        const std::size_t arrow = line.find("-->");
        if (arrow == std::string::npos) continue;

        Time st = 0, en = 0;
        if (!parseSrtTime(trimCrWs(line.substr(0, arrow)), st) ||
            !parseSrtTime(trimCrWs(line.substr(arrow + 3)), en) || en <= st) {
            continue;
        }

        std::string body;
        while (std::getline(lines, line)) {
            const std::string t = trimCrWs(line);
            if (t.empty()) break;
            if (!body.empty()) body += '\n';
            body += t;
        }
        if (!body.empty()) cues.push_back({st, en, std::move(body)});
    }

    if (cues.empty()) {
        if (error) *error = "No valid subtitle entries found in file";
        return 0;
    }

    const int trackId = addTrack(p, TrackKind::Video);
    Track* tr = p.findTrack(trackId);
    if (!tr) return 0;
    tr->name = "Subtitles";

    int created = 0;
    for (const Cue& cue : cues) {
        Clip c;
        c.id = p.newId();
        c.isText = true;
        c.text.content = cue.text;
        c.text.fontSize = 44;
        c.text.bgOpacity = 0.65f;
        c.posY = 0.36f;
        c.in = 0;
        c.out = cue.end - cue.start;
        c.start = findFreeStart(*tr, cue.start, c.out);
        tr->clips.push_back(std::move(c));
        ++created;
    }
    sortTrack(*tr);
    return created;
}

bool moveClip(Project& p, int clipId, int newTrackId, Time newStart) {
    ClipRef ref = findClip(p, clipId);
    if (!ref) return false;
    Track* dst = p.findTrack(newTrackId);
    if (!dst || !clipAcceptsTrack(*dst, *ref.clip, p)) return false;

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
    dst->clips.push_back(std::move(c));
    sortTrack(*dst);
    return true;
}

bool trimLeft(Project& p, int clipId, Time newStart) {
    ClipRef ref = findClip(p, clipId);
    if (!ref) return false;
    Clip& c = *ref.clip;
    const MediaAsset* asset = c.isText ? nullptr : p.findAsset(c.assetId);
    if (!c.isText && !asset) return false;

    const bool elastic = isElasticClip(c, asset);
    const Time oldEnd = c.end();
    const double sp = std::clamp(static_cast<double>(c.speed), 0.25, 4.0);

    Time lo = 0;
    for (const auto& o : ref.track->clips)
        if (o.id != c.id && o.end() <= c.start) lo = std::max(lo, o.end());
    if (!elastic) {
        const Time availLeft = static_cast<Time>(std::llround(static_cast<double>(c.in) / sp));
        lo = std::max(lo, c.start - availLeft);
    }
    const Time hi = oldEnd - minClipDuration(p);
    if (hi < lo) return false;

    newStart = std::clamp(newStart, lo, hi);
    const Time newDur = oldEnd - newStart;
    const Time newSrcLen =
        std::max<Time>(1, static_cast<Time>(std::llround(static_cast<double>(newDur) * sp)));
    if (elastic) {
        c.in = 0;
        c.out = newSrcLen;
    } else {
        c.in = std::max<Time>(0, c.out - newSrcLen);
    }
    c.start = oldEnd - c.duration();
    clampFades(c);
    return true;
}

bool trimRight(Project& p, int clipId, Time newEnd) {
    ClipRef ref = findClip(p, clipId);
    if (!ref) return false;
    Clip& c = *ref.clip;
    const MediaAsset* asset = c.isText ? nullptr : p.findAsset(c.assetId);
    if (!c.isText && !asset) return false;

    const bool elastic = isElasticClip(c, asset);
    const double sp = std::clamp(static_cast<double>(c.speed), 0.25, 4.0);

    Time hi = std::numeric_limits<Time>::max() / 4;
    for (const auto& o : ref.track->clips)
        if (o.id != c.id && o.start >= c.end()) hi = std::min(hi, o.start);
    if (!elastic) {
        const Time availRight =
            static_cast<Time>(std::llround(static_cast<double>(asset->duration - c.in) / sp));
        hi = std::min(hi, c.start + availRight);
    }
    const Time lo = c.start + minClipDuration(p);
    if (hi < lo) return false;

    newEnd = std::clamp(newEnd, lo, hi);
    c.out = c.in + std::max<Time>(
                       1, static_cast<Time>(std::llround(static_cast<double>(newEnd - c.start) * sp)));
    clampFades(c);
    return true;
}

bool setClipSpeed(Project& p, int clipId, float newSpeed) {
    ClipRef ref = findClip(p, clipId);
    if (!ref) return false;
    Clip& c = *ref.clip;
    newSpeed = std::clamp(newSpeed, 0.25f, 4.0f);
    if (std::fabs(newSpeed - c.speed) < 1e-4f) return false;

    Time nextStart = std::numeric_limits<Time>::max() / 4;
    for (const auto& o : ref.track->clips) {
        if (o.id != c.id && o.start >= c.end()) nextStart = std::min(nextStart, o.start);
    }

    const Time maxTimelineDur = nextStart - c.start;
    if (maxTimelineDur < minClipDuration(p)) return false;

    c.speed = newSpeed;
    if (c.duration() > maxTimelineDur) {
        const Time clampedSrc = std::max<Time>(
            1, static_cast<Time>(std::floor(static_cast<double>(maxTimelineDur) * newSpeed)));
        c.out = c.in + clampedSrc;
    }
    clampFades(c);
    return true;
}

int splitClip(Project& p, int clipId, Time t) {
    ClipRef ref = findClip(p, clipId);
    if (!ref) return 0;
    Clip& c = *ref.clip;
    const Time minDur = minClipDuration(p);
    if (t < c.start + minDur || t > c.end() - minDur) return 0;

    const MediaAsset* asset = c.isText ? nullptr : p.findAsset(c.assetId);
    const bool elastic = isElasticClip(c, asset);
    const Time leftTimeline = t - c.start;
    const Time rightTimeline = c.end() - t;
    const double sp = std::clamp(static_cast<double>(c.speed), 0.25, 4.0);

    Clip right = c;
    right.id = p.newId();
    right.start = t;
    right.transitionIn = TransitionKind::None;
    right.transitionInDur = 0;

    if (elastic) {
        right.in = 0;
        right.out =
            std::max<Time>(1, static_cast<Time>(std::llround(static_cast<double>(rightTimeline) * sp)));
        c.out =
            std::max<Time>(1, static_cast<Time>(std::llround(static_cast<double>(leftTimeline) * sp)));
    } else {
        const Time srcSplit = c.in + c.timelineToSourceOffset(leftTimeline);
        right.in = srcSplit;
        c.out = srcSplit;
    }

    c.fadeOut = 0;
    c.videoFadeOut = 0;
    clampFades(c);
    right.fadeIn = 0;
    right.videoFadeIn = 0;
    clampFades(right);

    const int rightId = right.id;
    Track* track = ref.track;  // `c` is invalid after push_back
    track->clips.push_back(std::move(right));
    sortTrack(*track);
    return rightId;
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
    audioClip.isText = false;
    audioClip.start = findFreeStart(*targetAudio, srcClip.start, srcClip.duration());
    const int newId = audioClip.id;
    targetAudio->clips.push_back(std::move(audioClip));
    sortTrack(*targetAudio);
    return newId;
}

int pasteClip(Project& p, const Clip& srcClip, int preferredTrackId, Time targetStart) {
    Track* target = preferredTrackId != 0 ? p.findTrack(preferredTrackId) : nullptr;
    if (!target || !clipAcceptsTrack(*target, srcClip, p)) {
        target = nullptr;
        for (auto& t : p.tracks) {
            if (clipAcceptsTrack(t, srcClip, p)) {
                target = &t;
                break;
            }
        }
    }
    if (!target) return 0;

    Clip copy = srcClip;
    copy.id = p.newId();
    copy.start = findFreeStart(*target, std::max<Time>(0, targetStart), copy.duration());
    const int newId = copy.id;
    target->clips.push_back(std::move(copy));
    sortTrack(*target);
    return newId;
}

int duplicateClip(Project& p, int clipId) {
    ClipRef ref = findClip(p, clipId);
    if (!ref) return 0;
    const Clip copy = *ref.clip;
    return pasteClip(p, copy, ref.track->id, copy.end());
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

bool rippleDeleteClip(Project& p, int clipId) {
    for (auto& t : p.tracks) {
        auto it = std::find_if(t.clips.begin(), t.clips.end(),
                               [&](const Clip& c) { return c.id == clipId; });
        if (it != t.clips.end()) {
            const Time removedEnd = it->end();
            const Time shift = it->duration();
            t.clips.erase(it);
            for (auto& c : t.clips) {
                if (c.start >= removedEnd) c.start = std::max<Time>(0, c.start - shift);
            }
            sortTrack(t);
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

bool moveTrack(Project& p, int trackId, int delta) {
    if (delta == 0) return false;
    int idx = -1;
    for (int i = 0; i < static_cast<int>(p.tracks.size()); ++i) {
        if (p.tracks[i].id == trackId) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return false;
    const int step = delta < 0 ? -1 : 1;
    const int next = idx + step;
    if (next < 0 || next >= static_cast<int>(p.tracks.size())) return false;
    if (p.tracks[next].kind != p.tracks[idx].kind) return false;
    std::swap(p.tracks[idx], p.tracks[next]);
    return true;
}

bool removeAsset(Project& p, int assetId) {
    auto it = std::find_if(p.assets.begin(), p.assets.end(),
                           [&](const MediaAsset& a) { return a.id == assetId; });
    if (it == p.assets.end()) return false;
    for (auto& t : p.tracks)
        t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(),
                                     [&](const Clip& c) { return !c.isText && c.assetId == assetId; }),
                      t.clips.end());
    p.assets.erase(it);
    return true;
}

}  // namespace vcut::ops
