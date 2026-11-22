#pragma once

#include <functional>
#include <optional>
#include <string>

#include "core/Editor.hpp"
#include "engine/PlaybackEngine.hpp"
#include "export/Exporter.hpp"
#include "media/Waveform.hpp"
#include "ui/Texture.hpp"

namespace vcut {

struct UiCallbacks {
    std::function<void()> onImportMedia;
    std::function<void()> onImportSubtitles;
    std::function<void()> onOpenExportDialog;
    std::function<void()> onBrowseExportPath;
};

enum class TimelineTool { Select, Razor, Hand };

struct TimelineViewState {
    float pixelsPerSecond = 80.0f;
    float scrollX = 0.0f;  // horizontal pan offset in pixels
    float scrollY = 0.0f;  // vertical pan offset in pixels
    bool snapEnabled = true;
    TimelineTool tool = TimelineTool::Select;
    bool autoFollow = true;  // auto-pan to keep playhead in view while playing

    enum class DragKind { None, MoveClip, TrimLeft, TrimRight, Scrub, Pan, Scrollbar };
    DragKind dragKind = DragKind::None;
    int dragClipId = 0;
    int dragOriginTrackId = 0;
    Time dragOriginStart = 0;
    Time dragOriginEnd = 0;
    Time dragGrabOffset = 0;  // pointer time minus clip start when grab began
    float dragStartMouseX = 0.0f;
    float dragStartMouseY = 0.0f;
    float dragStartScrollX = 0.0f;
    float dragStartScrollY = 0.0f;
    bool panMoved = false;
    Time snapLineTime = -1;

    // Interactive Preview canvas transform drag state
    enum class PreviewDragKind { None, Move, Scale };
    PreviewDragKind previewDragKind = PreviewDragKind::None;
    int previewDragClipId = 0;
    float previewStartMouseX = 0.0f;
    float previewStartMouseY = 0.0f;
    float previewOrigPosX = 0.0f;
    float previewOrigPosY = 0.0f;
    float previewOrigScale = 1.0f;

    std::optional<Clip> clipboardClip;
};

struct ExportDialogState {
    bool open = false;
    // 0=4K UHD (3840x2160), 1=1440p QHD, 2=1080p FHD, 3=720p HD, 4=480p SD,
    // 5=4K Vertical (2160x3840), 6=Match Project, 7=Custom
    int resolutionPreset = 2;
    int customWidth = 3840;
    int customHeight = 2160;
    int fpsPreset = 1;      // 0 = 24, 1 = 30, 2 = 60
    int qualityPreset = 1;  // 0 = High (CRF 18), 1 = Balanced (CRF 21), 2 = Compact (CRF 25)
    int encoderPreset = 0;  // 0 = Auto GPU (-> CPU fallback), 1 = CPU libx264, 2 = NVENC, 3 = AMF, 4 = QSV
    char outputPath[512] = "output.mp4";
};

void drawMediaBinPanel(Editor& editor, PlaybackEngine& playback, ThumbnailCache& thumbs,
                       const UiCallbacks& cb);
void drawPreviewPanel(Editor& editor, PlaybackEngine& playback, TimelineViewState& view,
                      GlTexture& previewTex);
void drawInspectorPanel(Editor& editor, PlaybackEngine& playback, TimelineViewState& view);
void drawTimelinePanel(Editor& editor, PlaybackEngine& playback, TimelineViewState& view,
                       WaveformCache& waveforms);
void drawExportDialog(const Project& project, ExportDialogState& state, ExportJob& job,
                      const UiCallbacks& cb);

}  // namespace vcut
