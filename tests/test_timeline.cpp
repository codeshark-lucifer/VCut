#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "core/Editor.hpp"
#include "core/TimelineOps.hpp"
#include "io/ProjectSerializer.hpp"

using namespace vcut;

#define VCUT_CHECK(cond)                                                                         \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            std::fprintf(stderr, "Check failed (%s:%d): %s\n", __FILE__, __LINE__, #cond);       \
            std::abort();                                                                        \
        }                                                                                        \
    } while (0)

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
    VCUT_CHECK(clip1 != 0);
    VCUT_CHECK(ed.project().duration() == 10 * kSecond);

    // Split at 4s -> two clips: [0, 4s) and [4s, 10s)
    int clip2 = 0;
    ed.edit([&](Project& p) {
        clip2 = ops::splitClip(p, clip1, 4 * kSecond);
        return clip2 != 0;
    });
    VCUT_CHECK(clip2 != 0);
    const Clip* c1 = ops::findClip(ed.project(), clip1);
    const Clip* c2 = ops::findClip(ed.project(), clip2);
    VCUT_CHECK(c1 && c2);
    VCUT_CHECK(c1->start == 0 && c1->duration() == 4 * kSecond);
    VCUT_CHECK(c2->start == 4 * kSecond && c2->in == 4 * kSecond && c2->out == 10 * kSecond);

    // Trim right of first clip to 3s
    VCUT_CHECK(ed.edit([&](Project& p) { return ops::trimRight(p, clip1, 3 * kSecond); }));
    c1 = ops::findClip(ed.project(), clip1);
    VCUT_CHECK(c1 && c1->duration() == 3 * kSecond);

    // Test snapClipMove (snapping left edge to c1->end() == 3s)
    Time snapLine = -1;
    const Time snappedStart = ops::snapClipMove(ed.project(), 3 * kSecond + 50'000, c2->duration(),
                                                200'000, 0, clip2, &snapLine);
    VCUT_CHECK(snappedStart == 3 * kSecond);
    VCUT_CHECK(snapLine == 3 * kSecond);

    // Move second clip to 3s (snapping gap shut)
    const Track* tr = nullptr;
    ops::findClip(ed.project(), clip2, &tr);
    VCUT_CHECK(tr);
    const int trackId = tr->id;
    VCUT_CHECK(ed.edit([&](Project& p) { return ops::moveClip(p, clip2, trackId, 3 * kSecond); }));
    c2 = ops::findClip(ed.project(), clip2);
    VCUT_CHECK(c2 && c2->start == 3 * kSecond);
    VCUT_CHECK(ed.project().duration() == 9 * kSecond);

    // Test undo / redo
    ed.undo();
    VCUT_CHECK(ed.project().duration() == 10 * kSecond);
    ed.redo();
    VCUT_CHECK(ed.project().duration() == 9 * kSecond);

    // Test extractAudio and audio properties
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
    VCUT_CHECK(audioClipId != 0);
    c1 = ops::findClip(ed.project(), clip1);
    const Track* audioTr = nullptr;
    const Clip* ac = ops::findClip(ed.project(), audioClipId, &audioTr);
    VCUT_CHECK(c1 && c1->muted);
    VCUT_CHECK(ac && audioTr && audioTr->kind == TrackKind::Audio);
    VCUT_CHECK(!ac->muted && std::fabs(ac->volume - 1.5f) < 1e-4f);
    VCUT_CHECK(ac->start == c1->start && ac->duration() == c1->duration());

    // Test playback speed (2.0x halves c2 duration from 6s -> 3s)
    VCUT_CHECK(ed.edit([&](Project& p) { return ops::setClipSpeed(p, clip2, 2.0f); }));
    c2 = ops::findClip(ed.project(), clip2);
    VCUT_CHECK(c2 && std::fabs(c2->speed - 2.0f) < 1e-4f && c2->duration() == 3 * kSecond);
    VCUT_CHECK(ed.project().duration() == 6 * kSecond);

    // Split c2 (3s at 2.0x speed) at midpoint 4.5s -> 1.5s + 1.5s timeline, 3s + 3s source!
    int clip2b = 0;
    VCUT_CHECK(ed.edit([&](Project& p) {
        clip2b = ops::splitClip(p, clip2, 4 * kSecond + kSecond / 2);
        return clip2b != 0;
    }));
    const Clip* c2a = ops::findClip(ed.project(), clip2);
    const Clip* c2bPtr = ops::findClip(ed.project(), clip2b);
    VCUT_CHECK(c2a && c2bPtr);
    VCUT_CHECK(c2a->duration() == 1500'000 && c2bPtr->duration() == 1500'000);
    VCUT_CHECK(c2bPtr->in == 7 * kSecond && c2a->out == 7 * kSecond);

    // Duplicate & Ripple Delete
    int dupId = 0;
    VCUT_CHECK(ed.edit([&](Project& p) {
        dupId = ops::duplicateClip(p, clip2b);
        return dupId != 0;
    }));
    VCUT_CHECK(dupId != 0);
    const Clip* dupPtr = ops::findClip(ed.project(), dupId);
    VCUT_CHECK(dupPtr && dupPtr->start == 6 * kSecond);
    VCUT_CHECK(ed.edit([&](Project& p) { return ops::rippleDeleteClip(p, clip2b); }));
    dupPtr = ops::findClip(ed.project(), dupId);
    VCUT_CHECK(dupPtr && dupPtr->start == 4 * kSecond + kSecond / 2);

    // Add text clip with visual transforms, color grading, chroma key, and transition
    int textClipId = 0;
    VCUT_CHECK(ed.edit([&](Project& p) {
        textClipId = ops::addTextClip(p, 0, kSecond, "Hello VCut 4K", 2 * kSecond);
        if (auto r = ops::findClip(p, textClipId)) {
            r.clip->scale = 0.75f;
            r.clip->posX = 0.25f;
            r.clip->posY = -0.25f;
            r.clip->opacity = 0.9f;
            r.clip->transitionIn = TransitionKind::Dissolve;
            r.clip->transitionInDur = kSecond / 2;
        }
        return textClipId != 0;
    }));
    VCUT_CHECK(textClipId != 0);
    VCUT_CHECK(ed.edit([&](Project& p) { return ops::trimRight(p, textClipId, 4 * kSecond); }));
    const Clip* tc = ops::findClip(ed.project(), textClipId);
    VCUT_CHECK(tc && tc->isText && tc->duration() == 3 * kSecond);

    // Import .srt subtitle file
    const std::string srtFile = "test_subtitles.srt";
    {
        std::ofstream sf(srtFile);
        sf << "1\n00:00:00,500 --> 00:00:02,000\nFirst line\nSecond line\n\n"
              "2\n00:00:02,500 --> 00:00:04,000\nAnother subtitle\n";
    }
    int subCount = 0;
    std::string err;
    VCUT_CHECK(ed.edit([&](Project& p) {
        subCount = ops::importSrtSubtitles(p, srtFile, &err);
        return subCount == 2;
    }));
    std::filesystem::remove(srtFile);

    // Set visual properties on clip1 for v2 round-trip check
    VCUT_CHECK(ed.edit([&](Project& p) {
        if (auto r = ops::findClip(p, clip1)) {
            r.clip->brightness = 0.15f;
            r.clip->contrast = 1.25f;
            r.clip->saturation = 0.8f;
            r.clip->vignette = 0.4f;
            r.clip->grayscale = true;
            r.clip->chromaKey = true;
            r.clip->chromaColor[1] = 0.9f;
            r.clip->chromaSimilarity = 0.32f;
            return true;
        }
        return false;
    }));

    // Test JSON v2 save & load round-trip
    const std::string tmpFile = "test_roundtrip.vcut";
    VCUT_CHECK(saveProject(ed.project(), tmpFile, &err));
    auto loaded = loadProject(tmpFile, &err);
    VCUT_CHECK(loaded.has_value());
    VCUT_CHECK(loaded->assets.size() == 1);
    const Clip* loadedAc = ops::findClip(*loaded, audioClipId).clip;
    VCUT_CHECK(loadedAc && std::fabs(loadedAc->volume - 1.5f) < 1e-4f);
    VCUT_CHECK(loadedAc->fadeIn == kSecond / 2 && loadedAc->fadeOut == kSecond / 2);

    const Clip* loadedC1 = ops::findClip(*loaded, clip1).clip;
    VCUT_CHECK(loadedC1 && loadedC1->grayscale && loadedC1->chromaKey);
    VCUT_CHECK(std::fabs(loadedC1->brightness - 0.15f) < 1e-4f);
    VCUT_CHECK(std::fabs(loadedC1->vignette - 0.4f) < 1e-4f);

    const Clip* loadedText = ops::findClip(*loaded, textClipId).clip;
    VCUT_CHECK(loadedText && loadedText->isText && loadedText->text.content == "Hello VCut 4K");
    VCUT_CHECK(loadedText->transitionIn == TransitionKind::Dissolve);
    VCUT_CHECK(std::fabs(loadedText->posX - 0.25f) < 1e-4f);
    std::filesystem::remove(tmpFile);

    std::printf("All timeline, transform, speed, subtitle & v2 serialization tests passed!\n");
    return 0;
}
