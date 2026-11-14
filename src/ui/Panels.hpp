#pragma once

#include <functional>
#include <string>

#include "core/Editor.hpp"
#include "engine/PlaybackEngine.hpp"
#include "export/Exporter.hpp"
#include "media/Waveform.hpp"
#include "ui/Texture.hpp"

namespace vcut {

struct UiCallbacks {
    std::function<void()> onImportMedia;
    std::function<void()> onOpenExportDialog;
    std::function<void()> onBrowseExportPath;
};

struct TimelineViewState {
    float pixelsPerSecond = 80.0f;
    float scrollX = 0.0f;  // horizontal pan offset in pixels
    float scrollY = 0.0f;  // vertical pan offset in pixels
    bool snapEnabled = true;
    bool panMode = false;      // Hand/Pan tool active
    bool autoFollow = true;    // auto-pan to keep playhead in view while playing

    enum class DragKind { None, MoveClip, TrimLeft, TrimRight, Scrub, Pan, Scrollbar };
    DragKind dragKind = DragKind::None;
    int dragClipId = 0;
    int dragOriginTrackId = 0;
    Time dragOriginStart = 0;
    Time dragOriginEnd = 0;
    Time dragGrabOffset = 0;   // pointer time minus clip start when grab began
    float dragStartMouseX = 0.0f;
    float dragStartMouseY = 0.0f;
    float dragStartScrollX = 0.0f;
    float dragStartScrollY = 0.0f;
    bool panMoved = false;
    Time snapLineTime = -1;
};

struct ExportDialogState {
    bool open = false;
    int resolutionPreset = 0;  // 0 = 1080p, 1 = 720p, 2 = 480p
    int fpsPreset = 1;         // 0 = 24, 1 = 30, 2 = 60
    char outputPath[512] = "output.mp4";
};

void drawMediaBinPanel(Editor& editor, ThumbnailCache& thumbs, const UiCallbacks& cb);
void drawPreviewPanel(Editor& editor, PlaybackEngine& playback, GlTexture& previewTex);
void drawInspectorPanel(Editor& editor, PlaybackEngine& playback);
void drawTimelinePanel(Editor& editor, PlaybackEngine& playback, TimelineViewState& view,
                       WaveformCache& waveforms);
void drawExportDialog(const Project& project, ExportDialogState& state, ExportJob& job,
                      const UiCallbacks& cb);

}  // namespace vcut
