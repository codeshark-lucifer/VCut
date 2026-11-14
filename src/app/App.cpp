#include "app/App.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>

#include <cstdio>
#include <cstring>
#include <filesystem>

#include "core/PathUtil.hpp"
#include "core/TimelineOps.hpp"
#include "io/ProjectSerializer.hpp"
#include "media/MediaProbe.hpp"
#include "ui/Widgets.hpp"

namespace vcut {

namespace {

constexpr int kPreviewRenderW = 1280;
constexpr int kPreviewRenderH = 720;

}  // namespace

void App::applyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 7.0f;
    s.ChildRounding = 6.0f;
    s.FrameRounding = 6.0f;
    s.PopupRounding = 7.0f;
    s.ScrollbarRounding = 8.0f;
    s.GrabRounding = 6.0f;
    s.TabRounding = 6.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;
    s.FramePadding = ImVec2(9.0f, 5.0f);
    s.ItemSpacing = ImVec2(8.0f, 6.0f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.086f, 0.094f, 0.122f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.067f, 0.075f, 0.098f, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.110f, 0.122f, 0.157f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.196f, 0.220f, 0.282f, 1.00f);
    c[ImGuiCol_FrameBg] = ImVec4(0.133f, 0.149f, 0.196f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.180f, 0.204f, 0.267f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.212f, 0.243f, 0.318f, 1.00f);
    c[ImGuiCol_TitleBg] = ImVec4(0.059f, 0.067f, 0.086f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.094f, 0.106f, 0.137f, 1.00f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.067f, 0.075f, 0.098f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.145f, 0.165f, 0.216f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.200f, 0.231f, 0.302f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.000f, 0.670f, 0.725f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.000f, 0.680f, 0.740f, 0.35f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.000f, 0.750f, 0.810f, 0.55f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.000f, 0.800f, 0.855f, 0.80f);
    c[ImGuiCol_Tab] = ImVec4(0.094f, 0.106f, 0.137f, 1.00f);
    c[ImGuiCol_TabSelected] = ImVec4(0.153f, 0.176f, 0.231f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(0.200f, 0.235f, 0.310f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.000f, 0.800f, 0.855f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.180f, 0.890f, 0.940f, 1.00f);
    c[ImGuiCol_CheckMark] = ImVec4(0.000f, 0.820f, 0.875f, 1.00f);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.000f, 0.800f, 0.855f, 1.00f);
}

void App::buildDefaultDockLayout(unsigned int dockspaceId) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, vp->WorkSize);

    ImGuiID dockTop = 0;
    ImGuiID dockBottom = 0;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Down, 0.42f, &dockBottom, &dockTop);

    ImGuiID dockLeft = 0;
    ImGuiID dockCenterRight = 0;
    ImGui::DockBuilderSplitNode(dockTop, ImGuiDir_Left, 0.27f, &dockLeft, &dockCenterRight);

    ImGuiID dockRight = 0;
    ImGuiID dockCenter = 0;
    ImGui::DockBuilderSplitNode(dockCenterRight, ImGuiDir_Right, 0.26f, &dockRight, &dockCenter);

    ImGui::DockBuilderDockWindow("Media Bin", dockLeft);
    ImGui::DockBuilderDockWindow("Preview", dockCenter);
    ImGui::DockBuilderDockWindow("Inspector", dockRight);
    ImGui::DockBuilderDockWindow("Timeline", dockBottom);
    ImGui::DockBuilderFinish(dockspaceId);
}

bool App::importFile(const std::string& path) {
    std::string err;
    auto assetOpt = probeMedia(path, &err);
    if (!assetOpt) {
        statusMessage_ = "Import failed: " + err;
        return false;
    }
    MediaAsset asset = std::move(*assetOpt);
    int newAssetId = 0;
    int newClipId = 0;
    const bool wasEmpty = editor_.project().assets.empty() && editor_.project().duration() == 0;
    editor_.edit([&](Project& p) {
        asset.id = p.newId();
        newAssetId = asset.id;
        p.assets.push_back(asset);
        if (wasEmpty) newClipId = ops::appendClip(p, newAssetId);
        return true;
    });
    editor_.selectedAsset = newAssetId;
    if (newClipId != 0) editor_.selectedClip = newClipId;
    statusMessage_ = "Imported " + asset.name;
    return true;
}

bool App::openProjectFile(const std::string& path) {
    std::string err;
    auto proj = loadProject(path, &err);
    if (!proj) {
        statusMessage_ = "Open failed: " + err;
        return false;
    }
    playback_.stop();
    thumbs_.clear();
    waveforms_.clear();
    editor_.reset(std::move(*proj), path);
    statusMessage_ = "Opened " + path;
    return true;
}

bool App::saveProjectFile(const std::string& path) {
    std::string err;
    if (!saveProject(editor_.project(), path, &err)) {
        statusMessage_ = "Save failed: " + err;
        return false;
    }
    editor_.setPath(path);
    editor_.markClean();
    statusMessage_ = "Saved " + path;
    return true;
}

void App::showImportDialog() {
    static const SDL_DialogFileFilter filters[] = {
        {"Media files", "mp4;mov;mkv;webm;avi;mp3;wav;aac;flac;ogg;png;jpg;jpeg;bmp"},
        {"All files", "*"},
    };
    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* filelist, int /*filter*/) {
            if (!filelist) return;
            auto* self = static_cast<App*>(userdata);
            std::lock_guard<std::mutex> lock(self->dialogMutex_);
            for (const char* const* p = filelist; *p; ++p) {
                self->pendingImports_.emplace_back(*p);
            }
        },
        this, window_, filters, 2, nullptr, true);
}

void App::showOpenProjectDialog() {
    static const SDL_DialogFileFilter filters[] = {
        {"VCut Project (*.vcut)", "vcut"},
        {"All files", "*"},
    };
    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* filelist, int /*filter*/) {
            if (!filelist || !filelist[0]) return;
            auto* self = static_cast<App*>(userdata);
            std::lock_guard<std::mutex> lock(self->dialogMutex_);
            self->pendingOpenProject_ = filelist[0];
        },
        this, window_, filters, 2, nullptr, false);
}

void App::showSaveProjectDialog() {
    static const SDL_DialogFileFilter filters[] = {
        {"VCut Project (*.vcut)", "vcut"},
    };
    SDL_ShowSaveFileDialog(
        [](void* userdata, const char* const* filelist, int /*filter*/) {
            if (!filelist || !filelist[0]) return;
            auto* self = static_cast<App*>(userdata);
            std::string p = filelist[0];
            if (pathFromUtf8(p).extension().empty()) p += ".vcut";
            std::lock_guard<std::mutex> lock(self->dialogMutex_);
            self->pendingSaveProject_ = std::move(p);
        },
        this, window_, filters, 1, "project.vcut");
}

void App::showBrowseExportDialog() {
    static const SDL_DialogFileFilter filters[] = {
        {"MP4 Video (*.mp4)", "mp4"},
    };
    SDL_ShowSaveFileDialog(
        [](void* userdata, const char* const* filelist, int /*filter*/) {
            if (!filelist || !filelist[0]) return;
            auto* self = static_cast<App*>(userdata);
            std::string p = filelist[0];
            if (pathFromUtf8(p).extension().empty()) p += ".mp4";
            std::lock_guard<std::mutex> lock(self->dialogMutex_);
            self->pendingExportPath_ = std::move(p);
        },
        this, window_, filters, 1, exportDialog_.outputPath);
}

void App::processPendingDialogResults() {
    std::vector<std::string> imports;
    std::string openPath;
    std::string savePath;
    std::string exportPath;
    {
        std::lock_guard<std::mutex> lock(dialogMutex_);
        imports.swap(pendingImports_);
        openPath.swap(pendingOpenProject_);
        savePath.swap(pendingSaveProject_);
        exportPath.swap(pendingExportPath_);
    }
    for (const auto& f : imports) importFile(f);
    if (!openPath.empty()) openProjectFile(openPath);
    if (!savePath.empty()) saveProjectFile(savePath);
    if (!exportPath.empty()) {
        std::snprintf(exportDialog_.outputPath, sizeof exportDialog_.outputPath, "%s",
                      exportPath.c_str());
    }
}

void App::drawMainMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Project", "Ctrl+N")) {
            playback_.stop();
            thumbs_.clear();
            waveforms_.clear();
            editor_.reset(Project::createDefault(), "");
            statusMessage_ = "Created new project";
        }
        if (ImGui::MenuItem("Open Project...", "Ctrl+O")) showOpenProjectDialog();
        if (ImGui::MenuItem("Save Project", "Ctrl+S")) {
            if (editor_.path().empty())
                showSaveProjectDialog();
            else
                saveProjectFile(editor_.path());
        }
        if (ImGui::MenuItem("Save Project As...")) showSaveProjectDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Import Media...", "Ctrl+I")) showImportDialog();
        if (ImGui::MenuItem("Export MP4...", "Ctrl+E")) {
            playback_.pause();
            exportDialog_.open = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) quitRequested_ = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, editor_.canUndo())) editor_.undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, editor_.canRedo())) editor_.redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Split at Playhead", "S")) {
            const Time t = playback_.playhead();
            const int pref = editor_.selectedClip;
            editor_.edit([&](Project& p) { return !ops::splitAt(p, t, pref).empty(); });
        }
        if (ImGui::MenuItem("Delete Selected Clip", "Del", false, editor_.selectedClip != 0)) {
            const int id = editor_.selectedClip;
            editor_.edit([&](Project& p) { return ops::deleteClip(p, id); });
            editor_.selectedClip = 0;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Playback")) {
        if (ImGui::MenuItem(playback_.isPlaying() ? "Pause" : "Play", "Space"))
            playback_.togglePlay();
        if (ImGui::MenuItem("Step Back 1 Frame", "Left"))
            playback_.stepFrames(editor_.project(), -1);
        if (ImGui::MenuItem("Step Forward 1 Frame", "Right"))
            playback_.stepFrames(editor_.project(), 1);
        if (ImGui::MenuItem("Jump to Start", "Home")) playback_.seek(0);
        ImGui::EndMenu();
    }

    ImGui::Separator();
    if (ui::PrimaryButton("Export MP4", ImVec2(102, 22))) {
        playback_.pause();
        exportDialog_.open = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|  %s%s  |  %s",
                        editor_.path().empty() ? "Untitled.vcut" : editor_.path().c_str(),
                        editor_.dirty() ? " *" : "", statusMessage_.c_str());

    ImGui::EndMainMenuBar();
}

void App::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;

    const bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) editor_.undo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) editor_.redo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O)) showOpenProjectDialog();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (editor_.path().empty())
            showSaveProjectDialog();
        else
            saveProjectFile(editor_.path());
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_I)) showImportDialog();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_E)) {
        playback_.pause();
        exportDialog_.open = true;
    }

    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Space)) playback_.togglePlay();
    if ((!ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) || (ctrl && ImGui::IsKeyPressed(ImGuiKey_B))) {
        const Time t = playback_.playhead();
        const int pref = editor_.selectedClip;
        editor_.edit([&](Project& p) { return !ops::splitAt(p, t, pref).empty(); });
    }
    if (!ctrl &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) &&
        editor_.selectedClip != 0) {
        const int id = editor_.selectedClip;
        editor_.edit([&](Project& p) { return ops::deleteClip(p, id); });
        editor_.selectedClip = 0;
    }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        playback_.stepFrames(editor_.project(), -1);
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        playback_.stepFrames(editor_.project(), 1);
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Home)) {
        playback_.seek(0);
        timelineView_.scrollX = 0.0f;
    }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_H)) {
        timelineView_.panMode = !timelineView_.panMode;
    }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
        timelineView_.panMode = false;
    }
}

int App::run(int maxFrames) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const SDL_DisplayID displayId = SDL_GetPrimaryDisplay();
    float scale = SDL_GetDisplayContentScale(displayId);
    if (scale <= 0.0f) scale = 1.0f;

    int winW = 1280;
    int winH = 720;
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(displayId, &usable) && usable.w > 0 && usable.h > 0) {
        winW = std::min(winW, static_cast<int>(usable.w * 0.88f));
        winH = std::min(winH, static_cast<int>(usable.h * 0.82f));
    }

    const SDL_WindowFlags winFlags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window_ = SDL_CreateWindow("VCut - Video Editor", winW, winH, winFlags);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    // Set runtime window icon from icon.ico if available
    {
        std::vector<std::uint8_t> iconRgba;
        if (makeThumbnail("icon.ico", 0, 64, 64, iconRgba)) {
            if (SDL_Surface* surf = SDL_CreateSurfaceFrom(
                    64, 64, SDL_PIXELFORMAT_RGBA32, iconRgba.data(), 64 * 4)) {
                SDL_SetWindowIcon(window_, surf);
                SDL_DestroySurface(surf);
            }
        }
    }

    SDL_GLContext glCtx = SDL_GL_CreateContext(window_);
    if (!glCtx) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window_, glCtx);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    applyTheme();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;
    if (std::filesystem::exists("C:\\Windows\\Fonts\\segoeui.ttf")) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);
    }

    ImGui_ImplSDL3_InitForOpenGL(window_, glCtx);
    ImGui_ImplOpenGL3_Init("#version 150");

    playback_.initAudio(editor_.project().settings.sampleRate);

    UiCallbacks callbacks;
    callbacks.onImportMedia = [this]() { showImportDialog(); };
    callbacks.onOpenExportDialog = [this]() {
        playback_.pause();
        exportDialog_.open = true;
    };
    callbacks.onBrowseExportPath = [this]() { showBrowseExportDialog(); };

    int frameCount = 0;
    while (!quitRequested_) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) quitRequested_ = true;
            if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                ev.window.windowID == SDL_GetWindowID(window_)) {
                quitRequested_ = true;
            }
            if (ev.type == SDL_EVENT_DROP_FILE && ev.drop.data) {
                const std::string dropped = ev.drop.data;
                if (pathFromUtf8(dropped).extension() == ".vcut")
                    openProjectFile(dropped);
                else
                    importFile(dropped);
            }
        }

        processPendingDialogResults();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        handleShortcuts();
        drawMainMenuBar();

        const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        if (!layoutInitialized_) {
            layoutInitialized_ = true;
            buildDefaultDockLayout(dockspaceId);
        }

        if (playback_.updateFrame(editor_.project(), editor_.version(), kPreviewRenderW,
                                  kPreviewRenderH, io.DeltaTime)) {
            previewTex_.upload(playback_.frameWidth(), playback_.frameHeight(),
                               playback_.frameRgba().data());
        }

        drawMediaBinPanel(editor_, thumbs_, callbacks);
        drawPreviewPanel(editor_, playback_, previewTex_);
        drawInspectorPanel(editor_, playback_);
        drawTimelinePanel(editor_, playback_, timelineView_, waveforms_);
        drawExportDialog(editor_.project(), exportDialog_, exportJob_, callbacks);

        ImGui::Render();
        glViewport(0, 0, static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window_);

        if (maxFrames > 0 && ++frameCount >= maxFrames) {
            quitRequested_ = true;
        }
    }

    exportJob_.cancel();
    exportJob_.wait();
    playback_.shutdownAudio();
    thumbs_.clear();
    previewTex_.reset();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(glCtx);
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return 0;
}

}  // namespace vcut
