#include <cassert>
#include <cmath>
#include <cstdio>
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
    assert(assetOpt->hasVideo);
    assert(assetOpt->hasAudio);
    assert(assetOpt->duration > 3 * kSecond);

    std::vector<std::uint8_t> thumb;
    assert(makeThumbnail(inPath, kSecond / 2, 96, 54, thumb));
    assert(thumb.size() == 96 * 54 * 4);

    // Test waveform generation on real audio stream
    AudioWaveform wf;
    assert(buildWaveform(inPath, wf));
    assert(!wf.peaks.empty());
    assert(wf.peakAt(kSecond / 2) > 0.05f);

    Project proj = Project::createDefault();
    proj.settings.width = 640;
    proj.settings.height = 360;
    proj.settings.fps = 30;
    proj.settings.sampleRate = 48000;

    MediaAsset a = *assetOpt;
    a.id = proj.newId();
    proj.assets.push_back(a);

    const int c1 = ops::appendClip(proj, a.id);
    assert(c1 != 0);
    // Trim to 2 seconds and split at 1 second
    assert(ops::trimRight(proj, c1, 2 * kSecond));
    const int c2 = ops::splitClip(proj, c1, 1 * kSecond);
    assert(c2 != 0);
    assert(proj.duration() == 2 * kSecond);

    // Extract audio of c2 onto Audio 1 and apply fade-out
    const int audioClip = ops::extractAudio(proj, c2);
    assert(audioClip != 0);
    if (auto r = ops::findClip(proj, audioClip)) {
        r.clip->volume = 1.2f;
        r.clip->fadeOut = kSecond / 4;
    }

    // Test compositor & audio mixer (verifying non-zero audio peaks)
    Compositor comp;
    std::vector<std::uint8_t> frame;
    comp.render(proj, kSecond / 2, 640, 360, frame);
    assert(frame.size() == 640 * 360 * 4);

    AudioMixer mixer;
    std::vector<float> samples(1024 * 2);
    float peakL = 0.0f, peakR = 0.0f;
    mixer.mix(proj, kSecond / 2, 1024, 48000, samples.data(), 1.0f, &peakL, &peakR);
    assert(peakL > 0.05f && peakR > 0.05f);

    // Verify muting the clip silences output at 0.5s
    if (auto r = ops::findClip(proj, c1)) r.clip->muted = true;
    float mutedL = 1.0f, mutedR = 1.0f;
    mixer.mix(proj, kSecond / 2, 1024, 48000, samples.data(), 1.0f, &mutedL, &mutedR);
    assert(mutedL == 0.0f && mutedR == 0.0f);
    if (auto r = ops::findClip(proj, c1)) r.clip->muted = false;

    // Export to MP4
    ExportOptions opts;
    opts.outputPath = outPath;
    opts.width = 640;
    opts.height = 360;
    opts.fps = 30;
    opts.sampleRate = 48000;

    if (!exportProject(proj, opts, &err)) {
        std::fprintf(stderr, "exportProject failed: %s\n", err.c_str());
        return 1;
    }

    auto outAsset = probeMedia(outPath, &err);
    if (!outAsset) {
        std::fprintf(stderr, "probeMedia(output) failed: %s\n", err.c_str());
        return 1;
    }
    assert(outAsset->hasVideo);
    assert(outAsset->hasAudio);
    assert(outAsset->width == 640 && outAsset->height == 360);
    assert(std::abs(toSeconds(outAsset->duration) - 2.0) < 0.25);

    std::printf("Media & audio pipeline test passed! Peak=(%.2f, %.2f), Exported=%.2fs\n", peakL,
                peakR, toSeconds(outAsset->duration));
    return 0;
}
