#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "core/Project.hpp"
#include "core/TimelineOps.hpp"
#include "engine/AudioMixer.hpp"
#include "engine/Compositor.hpp"
#include "export/Exporter.hpp"
#include "media/FrameBlit.hpp"
#include "media/MediaProbe.hpp"
#include "media/Waveform.hpp"

using namespace vcut;

#define VCUT_CHECK(cond)                                                                         \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            std::fprintf(stderr, "Check failed (%s:%d): %s\n", __FILE__, __LINE__, #cond);       \
            std::abort();                                                                        \
        }                                                                                        \
    } while (0)

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: vcut_pipeline_test <input.mp4> <output.mp4>\n");
        return 1;
    }
    const std::string inPath = argv[1];
    const std::string outPath = argv[2];

    std::string err;
    auto assetOpt = probeMedia(inPath, &err);
    if (!assetOpt) {
        std::fprintf(stderr, "probeMedia failed: %s\n", err.c_str());
        return 1;
    }
    VCUT_CHECK(assetOpt->hasVideo);
    VCUT_CHECK(assetOpt->hasAudio);
    VCUT_CHECK(assetOpt->duration > 3 * kSecond);

    std::vector<std::uint8_t> thumb;
    VCUT_CHECK(makeThumbnail(inPath, kSecond / 2, 96, 54, thumb));
    VCUT_CHECK(thumb.size() == 96 * 54 * 4);

    // Test waveform generation on real audio stream
    AudioWaveform wf;
    VCUT_CHECK(buildWaveform(inPath, wf));
    VCUT_CHECK(!wf.peaks.empty());
    VCUT_CHECK(wf.peakAt(kSecond / 2) > 0.05f);

    Project proj = Project::createDefault();
    proj.settings.width = 640;
    proj.settings.height = 360;
    proj.settings.fps = 30;
    proj.settings.sampleRate = 48000;

    MediaAsset a = *assetOpt;
    a.id = proj.newId();
    proj.assets.push_back(a);

    const int c1 = ops::appendClip(proj, a.id);
    VCUT_CHECK(c1 != 0);
    // Trim to 2 seconds and split at 1 second
    VCUT_CHECK(ops::trimRight(proj, c1, 2 * kSecond));
    const int c2 = ops::splitClip(proj, c1, 1 * kSecond);
    VCUT_CHECK(c2 != 0);
    VCUT_CHECK(proj.duration() == 2 * kSecond);

    // Apply Dissolve transition on c2 and color grade + PiP text overlay on top track
    if (auto r = ops::findClip(proj, c2)) {
        r.clip->transitionIn = TransitionKind::Dissolve;
        r.clip->transitionInDur = kSecond / 3;
        r.clip->contrast = 1.15f;
        r.clip->vignette = 0.25f;
    }
    const int textClip = ops::addTextClip(proj, 0, kSecond / 4, "VCut Overlay", kSecond);
    VCUT_CHECK(textClip != 0);
    if (auto r = ops::findClip(proj, textClip)) {
        r.clip->text.fontSize = 72;
        r.clip->videoFadeIn = kSecond / 5;
    }

    // Extract audio of c2 onto Audio 1 and apply fade-out + 1.5x speed check
    const int audioClip = ops::extractAudio(proj, c2);
    VCUT_CHECK(audioClip != 0);
    if (auto r = ops::findClip(proj, audioClip)) {
        r.clip->volume = 1.2f;
        r.clip->fadeOut = kSecond / 4;
    }

    // Test compositor with text + transition + color filter
    Compositor comp;
    std::vector<std::uint8_t> frame;
    comp.render(proj, kSecond / 2, 640, 360, frame);
    VCUT_CHECK(frame.size() == 640 * 360 * 4);
    comp.render(proj, kSecond + 100'000, 640, 360, frame);  // mid-transition
    VCUT_CHECK(frame.size() == 640 * 360 * 4);

    // Verify AudioMixer at normal and 2x speed
    AudioMixer mixer;
    std::vector<float> samples(1024 * 2);
    float peakL = 0.0f, peakR = 0.0f;
    mixer.mix(proj, kSecond / 2, 1024, 48000, samples.data(), 1.0f, &peakL, &peakR);
    VCUT_CHECK(peakL > 0.05f && peakR > 0.05f);

    if (auto r = ops::findClip(proj, c1)) r.clip->speed = 2.0f;
    float fastL = 0.0f, fastR = 0.0f;
    mixer.mix(proj, kSecond / 8, 1024, 48000, samples.data(), 1.0f, &fastL, &fastR);
    VCUT_CHECK(fastL > 0.05f && fastR > 0.05f);
    if (auto r = ops::findClip(proj, c1)) r.clip->speed = 1.0f;

    if (auto r = ops::findClip(proj, c1)) r.clip->muted = true;
    float mutedL = 1.0f, mutedR = 1.0f;
    mixer.mix(proj, kSecond / 2, 1024, 48000, samples.data(), 1.0f, &mutedL, &mutedR);
    VCUT_CHECK(mutedL == 0.0f && mutedR == 0.0f);
    if (auto r = ops::findClip(proj, c1)) r.clip->muted = false;

    // Export MP4 using Auto backend (probes GPU, safely falls back to libx264 if unavailable)
    ExportOptions opts;
    opts.outputPath = outPath;
    opts.width = 640;
    opts.height = 360;
    opts.fps = 30;
    opts.sampleRate = 48000;
    opts.encoder = EncoderBackend::Auto;

    std::string usedEnc;
    if (!exportProject(proj, opts, &err, nullptr, nullptr, &usedEnc)) {
        std::fprintf(stderr, "exportProject failed: %s\n", err.c_str());
        return 1;
    }
    VCUT_CHECK(!usedEnc.empty());

    auto outAsset = probeMedia(outPath, &err);
    if (!outAsset) {
        std::fprintf(stderr, "probeMedia(output) failed: %s\n", err.c_str());
        return 1;
    }
    VCUT_CHECK(outAsset->hasVideo);
    VCUT_CHECK(outAsset->hasAudio);
    VCUT_CHECK(outAsset->width == 640 && outAsset->height == 360);
    VCUT_CHECK(std::abs(toSeconds(outAsset->duration) - 2.0) < 0.25);

    std::printf("Media & audio pipeline test passed! Encoder=%s, Peak=(%.2f, %.2f), Exported=%.2fs\n",
                usedEnc.c_str(), peakL, peakR, toSeconds(outAsset->duration));

    // Verify 4K UHD (3840x2160) export with Text overlay & PiP scale on 0.25s clip
    VCUT_CHECK(ops::deleteClip(proj, c2));
    VCUT_CHECK(ops::deleteClip(proj, audioClip));
    VCUT_CHECK(ops::trimRight(proj, c1, kSecond / 4));
    VCUT_CHECK(ops::trimRight(proj, textClip, kSecond / 4));
    VCUT_CHECK(ops::moveClip(proj, textClip, proj.tracks.front().id, 0));
    if (auto r = ops::findClip(proj, c1)) {
        r.clip->scale = 0.85f;
        r.clip->chromaKey = true;
    }
    VCUT_CHECK(proj.duration() == kSecond / 4);

    const std::string out4kPath =
        (std::filesystem::path(outPath).parent_path() / "export_4k.mp4").string();
    ExportOptions opts4k;
    opts4k.outputPath = out4kPath;
    opts4k.width = 3840;
    opts4k.height = 2160;
    opts4k.fps = 30;
    opts4k.sampleRate = 48000;
    opts4k.crf = 21;
    opts4k.encoder = EncoderBackend::Auto;

    std::string usedEnc4k;
    if (!exportProject(proj, opts4k, &err, nullptr, nullptr, &usedEnc4k)) {
        std::fprintf(stderr, "4K exportProject failed: %s\n", err.c_str());
        return 1;
    }
    auto out4kAsset = probeMedia(out4kPath, &err);
    if (!out4kAsset) {
        std::fprintf(stderr, "probeMedia(4K output) failed: %s\n", err.c_str());
        return 1;
    }
    VCUT_CHECK(out4kAsset->hasVideo);
    VCUT_CHECK(out4kAsset->hasAudio);
    VCUT_CHECK(out4kAsset->width == 3840 && out4kAsset->height == 2160);
    std::filesystem::remove(out4kPath);

    std::printf("4K UHD (3840x2160) export test passed! Encoder=%s\n", usedEnc4k.c_str());
    return 0;
}
