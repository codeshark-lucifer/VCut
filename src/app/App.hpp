#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "core/Editor.hpp"
#include "engine/PlaybackEngine.hpp"
#include "export/Exporter.hpp"
#include "media/Waveform.hpp"
#include "ui/Panels.hpp"
#include "ui/Texture.hpp"

struct SDL_Window;

namespace vcut {

class App {
public:
    App() = default;
    ~App() = default;

    // If maxFrames > 0, runs that many frames and exits (used for smoke tests).
    int run(int maxFrames = 0);

    bool importFile(const std::string& path);
    bool openProjectFile(const std::string& path);
    bool saveProjectFile(const std::string& path);

private:
    void applyTheme();
    void buildDefaultDockLayout(unsigned int dockspaceId);
    void drawMainMenuBar();
    void handleShortcuts();
    void processPendingDialogResults();

    void showImportDialog();
    void showOpenProjectDialog();
    void showSaveProjectDialog();
    void showBrowseExportDialog();

    SDL_Window* window_ = nullptr;
    Editor editor_;
    PlaybackEngine playback_;
    GlTexture previewTex_;
    ThumbnailCache thumbs_;
    WaveformCache waveforms_;
    TimelineViewState timelineView_;
    ExportDialogState exportDialog_;
    ExportJob exportJob_;

    bool layoutInitialized_ = false;
    bool quitRequested_ = false;
    std::string statusMessage_ = "Ready";

    std::mutex dialogMutex_;
    std::vector<std::string> pendingImports_;
    std::string pendingOpenProject_;
    std::string pendingSaveProject_;
    std::string pendingExportPath_;
};

}  // namespace vcut
