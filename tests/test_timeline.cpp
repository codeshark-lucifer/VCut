#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "core/Editor.hpp"
#include "core/TimelineOps.hpp"
#include "io/ProjectSerializer.hpp"

using namespace vcut;

int main() {
    Editor ed;
    MediaAsset videoAsset;
    videoAsset.path = "sample.mp4";
    videoAsset.name = "sample.mp4";
    videoAsset.type = MediaType::Video;
    videoAsset.duration = 10 * kSecond;
    videoAsset.width = 1920;
    videoAsset.height = 1080;
    videoAsset.fps = 30.0;
    videoAsset.hasVideo = true;
    videoAsset.hasAudio = true;

    int assetId = 0;
    int clip1 = 0;
    ed.edit([&](Project& p) {
        videoAsset.id = p.newId();
        assetId = videoAsset.id;
        p.assets.push_back(videoAsset);
        clip1 = ops::appendClip(p, assetId);
        return clip1 != 0;
    });
    assert(clip1 != 0);
    assert(ed.project().duration() == 10 * kSecond);

    // Split at 4s -> two clips: [0, 4s) and [4s, 10s)
    int clip2 = 0;
    ed.edit([&](Project& p) {
        clip2 = ops::splitClip(p, clip1, 4 * kSecond);
        return clip2 != 0;
    });
    assert(clip2 != 0);
    const Clip* c1 = ops::findClip(ed.project(), clip1);
    const Clip* c2 = ops::findClip(ed.project(), clip2);
    assert(c1 && c2);
    assert(c1->start == 0 && c1->duration() == 4 * kSecond);
    assert(c2->start == 4 * kSecond && c2->in == 4 * kSecond && c2->out == 10 * kSecond);

    // Trim right of first clip to 3s
    ed.edit([&](Project& p) { return ops::trimRight(p, clip1, 3 * kSecond); });
    c1 = ops::findClip(ed.project(), clip1);
    assert(c1 && c1->duration() == 3 * kSecond);

    // Test snapClipMove (snapping left edge to c1->end() == 3s)
    Time snapLine = -1;
    const Time snappedStart = ops::snapClipMove(ed.project(), 3 * kSecond + 50'000, c2->duration(),
                                                200'000, 0, clip2, &snapLine);
    assert(snappedStart == 3 * kSecond);
    assert(snapLine == 3 * kSecond);

    // Move second clip to 3s (snapping gap shut)
    const Track* tr = nullptr;
    ops::findClip(ed.project(), clip2, &tr);
    assert(tr);
    const int trackId = tr->id;
    ed.edit([&](Project& p) { return ops::moveClip(p, clip2, trackId, 3 * kSecond); });
    c2 = ops::findClip(ed.project(), clip2);
    assert(c2 && c2->start == 3 * kSecond);
    assert(ed.project().duration() == 9 * kSecond);

    // Test undo / redo
    ed.undo();
    assert(ed.project().duration() == 10 * kSecond);
    ed.redo();
    assert(ed.project().duration() == 9 * kSecond);

    // Test extractAudio and audio properties (volume, fadeIn, fadeOut)
    int audioClipId = 0;
    ed.edit([&](Project& p) {
        audioClipId = ops::extractAudio(p, clip1);
        if (auto r = ops::findClip(p, audioClipId)) {
            r.clip->volume = 1.5f;
            r.clip->fadeIn = kSecond / 2;
            r.clip->fadeOut = kSecond / 2;
        }
        return audioClipId != 0;
    });
    assert(audioClipId != 0);
    c1 = ops::findClip(ed.project(), clip1);
    const Track* audioTr = nullptr;
    const Clip* ac = ops::findClip(ed.project(), audioClipId, &audioTr);
    assert(c1 && c1->muted);  // original video clip muted after extracting audio
    assert(ac && audioTr && audioTr->kind == TrackKind::Audio);
    assert(!ac->muted && std::fabs(ac->volume - 1.5f) < 1e-4f);
    assert(ac->start == c1->start && ac->duration() == c1->duration());

    // Test JSON save & load round-trip including audio properties
    const std::string tmpFile = "test_roundtrip.vcut";
    std::string err;
    assert(saveProject(ed.project(), tmpFile, &err));
    auto loaded = loadProject(tmpFile, &err);
    assert(loaded.has_value());
    assert(loaded->assets.size() == 1);
    assert(loaded->duration() == 9 * kSecond);
    const Clip* loadedAc = ops::findClip(*loaded, audioClipId).clip;
    assert(loadedAc && std::fabs(loadedAc->volume - 1.5f) < 1e-4f);
    assert(loadedAc->fadeIn == kSecond / 2 && loadedAc->fadeOut == kSecond / 2);
    std::filesystem::remove(tmpFile);

    std::printf("All timeline, audio extraction & serialization tests passed!\n");
    return 0;
}
