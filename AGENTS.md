# AGENTS.md — VCut Developer & Agent Guide

This file documents the architecture, build commands, conventions, and design invariants of **VCut** for AI coding agents and contributors.

---

## 1. Project Overview

**VCut** is a lightweight, CapCut-inspired desktop video editor written in **C++20** using:
- **Dear ImGui (docking branch)** + **SDL3** + **OpenGL 3.3 Core** for the UI, windowing, native file dialogs, and audio output stream.
- **FFmpeg** (`libavformat`, `libavcodec`, `libswscale`, `libswresample`, `libavutil`) for probing, decoding, scaling, resampling, and MP4 (`H.264 + AAC`) encoding.
- **nlohmann/json** for `.vcut` project file serialization.

---

## 2. Build & Test Commands

### Using `compile.bat` (Windows)
```bat
.\compile.bat           :: Configure (if needed) and build Debug (build\debug\vcut.exe)
.\compile.bat release   :: Configure and build Release (build\release\vcut.exe)
.\compile.bat test      :: Build Debug and run CTest suite
.\compile.bat run       :: Build Debug and launch vcut.exe
.\compile.bat clean     :: Clean build directory and rebuild
```

### Using CMake & CTest Directly
```powershell
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure -V
.\build\debug\vcut.exe --smoke-test
```

---

## 3. Repository Architecture

```text
VCut/
├── CMakeLists.txt              # Targets: imgui, vcut_core, vcut, vcut_tests, vcut_pipeline_test
├── CMakePresets.json           # Ninja debug & release presets (MSYS2 UCRT64 g++)
├── compile.bat                 # Convenience build/test/run script for Windows
├── external/
│   └── imgui/                  # Vendored Dear ImGui (docking branch, 1.93+)
├── src/
│   ├── main.cpp                # CLI entry point (--smoke-test, .vcut project, or media args)
│   ├── app/
│   │   ├── App.hpp             # Main application lifecycle, SDL3/GL/ImGui init, dialogs
│   │   └── App.cpp             # DockBuilder default layout, menu bar, shortcuts, main loop
│   ├── core/
│   │   ├── Time.hpp/.cpp       # Microsecond timestamp type (vcut::Time) & HH:MM:SS.cc formatting
│   │   ├── PathUtil.hpp        # UTF-8 std::string <-> std::filesystem::path conversions
│   │   ├── Project.hpp/.cpp    # Pure data model: Project, MediaAsset, Track, Clip
│   │   ├── TimelineOps.hpp/.cpp# Non-overlapping edit ops: add/append/move/trim/split/extractAudio/snap
│   │   └── Editor.hpp          # Project owner with snapshot Undo/Redo & continuous gesture coalescing
│   ├── media/
│   │   ├── FFmpeg.hpp          # C headers wrapper & FFmpeg helpers
│   │   ├── MediaProbe.hpp/.cpp # Inspects media files into MediaAsset structs
│   │   ├── VideoDecoder.hpp/.cpp # Frame-accurate video & still-image decoder with smart seeking
│   │   ├── AudioDecoder.hpp/.cpp # Stereo float resampler/decoder via libswresample
│   │   ├── FrameBlit.hpp/.cpp  # Aspect-ratio letterbox scaling, alpha blend, thumbnail extraction
│   │   └── Waveform.hpp/.cpp   # Audio peak extraction & caching for timeline clip waveforms
│   ├── engine/
│   │   ├── Compositor.hpp/.cpp # Evaluates visible video tracks at Time t into an RGBA buffer
│   │   ├── AudioMixer.hpp/.cpp # Mixes unmuted clips at Time t with volume, fade-in/out & peak metering
│   │   └── PlaybackEngine.hpp/.cpp # SDL_AudioStream callback + smooth 60fps playhead sync
│   ├── export/
│   │   └── Exporter.hpp/.cpp   # Offline MP4 (H.264 + AAC) encoder & background ExportJob thread
│   ├── io/
│   │   └── ProjectSerializer.hpp/.cpp # .vcut JSON project save/load
│   └── ui/
│       ├── Texture.hpp         # OpenGL texture wrapper (GlTexture) & ThumbnailCache
│       ├── Widgets.hpp         # Custom-drawn buttons, sliders, scrubber bar, toggle switches
│       └── Panels.hpp/.cpp     # Media Bin, Preview, Inspector, Timeline, and Export Dialog panels
└── tests/
    ├── test_timeline.cpp       # Unit tests for timeline ops, audio extraction, undo/redo, JSON I/O
    └── test_pipeline.cpp       # End-to-end FFmpeg decode, waveform, mix, composite, and MP4 export test
```

---

## 4. Key Architectural Invariants

1. **`vcut_core` is Headless and UI-Free**
   - `src/core/`, `src/media/`, `src/engine/`, `src/export/`, and `src/io/` must never depend on Dear ImGui or OpenGL so they can be unit-tested in `vcut_tests` and `vcut_pipeline_test` and run on background threads (`ExportJob`).
2. **Integer Microsecond Timestamps (`vcut::Time`)**
   - All timeline and media timestamps are `std::int64_t` microseconds (`kSecond = 1'000'000`) to prevent floating-point drift across edits and frame calculations.
3. **Tracks and Clips Invariant**
   - Clips on a `Track` are always sorted by `Clip::start` and never overlap (`ops::isFree`, `ops::findFreeStart`, `ops::sortTrack`).
   - `Project::tracks` is ordered top-to-bottom as displayed in the UI. For video compositing, tracks are rendered bottom-to-top (`rbegin()` to `rend()`) so higher video tracks overlay lower tracks.
4. **Shared Evaluation Between Preview and Export**
   - Both real-time playback (`PlaybackEngine`) and offline rendering (`exportProject`) use the exact same `Compositor` and `AudioMixer` classes. Any new visual or audio effect added to `Compositor` / `AudioMixer` automatically works in both live preview and exported MP4 files.
5. **Undo / Redo Gesture Discipline**
   - One-shot edits use `editor.edit([&](Project& p) { ... })`.
   - Continuous mouse drags (moving or trimming a clip on the timeline) call `editor.beginGesture()` on mouse-down, `editor.gestureApply(...)` on mouse-move, and `editor.endGesture()` on mouse-up so an entire drag produces a single undo step.
6. **Dear ImGui Custom Widgets (`src/ui/Widgets.hpp`)**
   - Do not call `ImGui::ItemHoverable` before `ImGui::ButtonBehavior` with the same `ImGuiID` in the same frame; `ButtonBehavior` already calls `ItemHoverable` internally and duplicate calls trigger ImGui's ID conflict checker.
   - On timeline track lanes, use `ImGui::BeginDragDropTargetCustom` rather than `ImGui::InvisibleButton` so drag-drop targets do not steal mouse clicks from clips.
