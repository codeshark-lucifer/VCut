# VCut

**VCut** is a fast, lightweight desktop video editor inspired by CapCut, built in **C++20** with **Dear ImGui (Docking)**, **SDL3**, **OpenGL 3.3**, and **FFmpeg**.

---

## Features

- **Media Bin & Import**
  - Import video (`.mp4`, `.mov`, `.mkv`, `.webm`, `.avi`), audio (`.mp3`, `.wav`, `.aac`, `.flac`, `.ogg`), and images (`.png`, `.jpg`, `.bmp`) via native file dialog or OS drag-and-drop.
  - Automatic scene thumbnail extraction (skipping black fade-in frames), media type badges, and duration/resolution metadata.
- **Multi-Track Timeline**
  - Multiple video (`Video 1`, `Video 2`, ...) and audio (`Audio 1`, `Audio 2`, ...) tracks with per-track **Mute** and **Hide** toggles.
  - Drag-and-drop assets from the Media Bin onto any compatible track, move clips across time and tracks, trim left/right edges, split clips at the playhead (`S` / `Ctrl+B`), and delete clips (`Del`).
  - Magnetic snapping to clip edges, `00:00`, and the playhead.
  - Smooth horizontal and vertical panning (drag empty track space, Middle/Right-click drag, Hand tool `H`, Mouse Wheel, or bottom scrollbar) and cursor-anchored zooming (`Ctrl + Mouse Wheel`).
- **Audio Editing & Waveforms**
  - Real-time audio waveforms and fade envelopes rendered directly on timeline clips.
  - Per-clip **Volume (`0%–200%`)**, **Mute**, **Fade In**, and **Fade Out** controls in the Inspector.
  - **Extract Audio**: detach embedded audio from any video clip onto an independent Audio track with one click.
  - Live **Stereo Peak Level Meter (L/R)** and Master Volume / Mute controls in the Preview transport bar.
- **Real-Time Preview Player**
  - Aspect-ratio-preserving letterbox compositing and multi-track stereo audio mixing.
  - Transport controls: Play/Pause (`Space`), single-frame step backward/forward (`Left`/`Right`), jump to start/end (`Home`), loop toggle, and interactive scrubber bar.
- **MP4 Video Export**
  - Background-threaded export to `.mp4` (**H.264** video + **AAC** audio) at `1080p`, `720p`, or `480p` and `24`/`30`/`60 fps` with a live progress bar and cancel button.
- **Project Save / Load & Undo / Redo**
  - Save and open `.vcut` JSON project files (`Ctrl+S` / `Ctrl+O`).
  - Full Undo / Redo history (`Ctrl+Z` / `Ctrl+Y`).

---

## Prerequisites (Windows / MSYS2 UCRT64)

Make sure MSYS2 (`D:\msys64` or `C:\msys64`) has the UCRT64 toolchain, SDL3, FFmpeg, and `nlohmann-json` installed:

```powershell
pacman -S --needed `
  mingw-w64-ucrt-x86_64-gcc `
  mingw-w64-ucrt-x86_64-ninja `
  mingw-w64-ucrt-x86_64-pkgconf `
  mingw-w64-ucrt-x86_64-sdl3 `
  mingw-w64-ucrt-x86_64-ffmpeg `
  mingw-w64-ucrt-x86_64-nlohmann-json
```

Dear ImGui (docking branch) is vendored under `external/imgui/`.

---

## Building & Running

### Quick Build with `compile.bat`

```bat
:: Build Debug (build\debug\vcut.exe)
.\compile.bat

:: Build and launch VCut immediately
.\compile.bat run

:: Build Release (no console window)
.\compile.bat release

:: Build and run unit & media pipeline tests
.\compile.bat test

:: Clean and rebuild
.\compile.bat clean
```

### Manual CMake Build

```powershell
cmake --preset debug
cmake --build --preset debug
.\build\debug\vcut.exe
```

You can also pass media files or a `.vcut` project file directly on the command line:

```powershell
.\build\debug\vcut.exe my_video.mp4
.\build\debug\vcut.exe my_project.vcut
```

---

## Keyboard & Mouse Controls

| Shortcut / Action | Description |
|---|---|
| `Space` | Play / Pause preview |
| `S` or `Ctrl + B` | Split selected clip (or all clips) at the playhead |
| `Delete` / `Backspace` | Delete selected timeline clip |
| `Left` / `Right` | Step 1 frame backward / forward |
| `Home` | Jump playhead to `00:00:00.00` |
| `H` / `V` | Toggle Hand (Pan) tool / Select tool |
| `Ctrl + Z` / `Ctrl + Y` | Undo / Redo |
| `Ctrl + I` | Import media files |
| `Ctrl + O` / `Ctrl + S` | Open / Save `.vcut` project |
| `Ctrl + E` | Open Export MP4 dialog |
| `Mouse Wheel` (in Timeline) | Pan timeline horizontally |
| `Shift + Mouse Wheel` | Scroll timeline tracks vertically |
| `Ctrl + Mouse Wheel` | Zoom timeline in / out at mouse cursor |
| `Middle / Right Drag` or `Drag Empty Track` | Pan timeline horizontally & vertically |
| `Right-Click Clip` | Open clip context menu (Split, Extract Audio, Mute, Delete) |

---

## Project Structure

- `src/core/` — `Time`, `Project`, `TimelineOps`, and `Editor` (undo/redo state).
- `src/media/` — FFmpeg wrappers (`MediaProbe`, `VideoDecoder`, `AudioDecoder`, `FrameBlit`, `Waveform`).
- `src/engine/` — Real-time `Compositor`, `AudioMixer`, and `PlaybackEngine`.
- `src/export/` — MP4 (`H.264 + AAC`) `Exporter` and background `ExportJob`.
- `src/io/` — `.vcut` JSON `ProjectSerializer`.
- `src/ui/` — OpenGL `GlTexture`, `ThumbnailCache`, custom-styled `Widgets`, and dockable `Panels`.
- `src/app/` — `App` window/GL/ImGui initialization, docking layout, dialogs, and shortcuts.
- `tests/` — Automated unit tests (`test_timeline.cpp`) and end-to-end media/export pipeline tests (`test_pipeline.cpp`).
