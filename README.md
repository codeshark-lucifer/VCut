# VCut

**VCut** is a fast, lightweight desktop video editor inspired by CapCut, built in **C++20** with **Dear ImGui (Docking)**, **SDL3**, **OpenGL 3.3**, **FFmpeg**, and **FreeType2**.

---

## Features

- **Media Bin, Text & Subtitles**
  - Import video (`.mp4`, `.mov`, `.mkv`, `.webm`, `.avi`), audio (`.mp3`, `.wav`, `.aac`, `.flac`, `.ogg`), images (`.png`, `.jpg`, `.bmp`), and `.srt` subtitles via native file dialog or OS drag-and-drop.
  - One-click **+ Text** overlay clips with customizable font size, text/box colors, box opacity, and drop shadow rendered identically in live preview and exports.
  - Automatic scene thumbnail extraction (skipping black fade-in frames), media type badges, and duration/resolution metadata.
- **Multi-Track Non-Linear Timeline**
  - Multiple video (`Video 1`, `Video 2`, ...) and audio (`Audio 1`, `Audio 2`, ...) tracks with per-track **Mute**, **Hide**, **Move Up/Down**, add, and delete controls.
  - Switch between **Select (`V`)**, **Razor (`C`)** (click anywhere on a clip to slice), and **Hand (`H`)** pan tools.
  - Drag-and-drop assets from Media Bin, move clips across tracks, trim edges, **Copy/Paste/Duplicate (`Ctrl+C` / `Ctrl+V` / `Ctrl+D`)**, **Split (`S` / `Ctrl+B`)**, **Delete (`Del`)**, and **Ripple Delete (`Shift+Del`)**.
  - Magnetic snapping, smooth horizontal/vertical panning (Middle/Right-drag, empty-track drag, mouse wheel, bottom scrollbar), and cursor-anchored zoom (`Ctrl + Mouse Wheel`).
- **Visual Transforms, Color Grading, Chroma Key & Transitions**
  - **Transform & Picture-in-Picture**: per-clip **Scale (`10%–400%`)**, **Pos X / Pos Y**, **Opacity (`0%–100%`)**, **Video Fade In / Out**, plus **interactive drag and corner resize handles** directly in the Preview canvas.
  - **Speed Control (`0.25x–4.0x`)**: adjust clip playback speed for both video and audio while preserving non-overlapping timeline layout.
  - **Color & Filters**: per-clip **Brightness**, **Contrast**, **Saturation**, **Vignette**, and **Black & White** toggle.
  - **Chroma Key (Green Screen)**: key color picker with **Similarity**, **Edge Blend**, and spill reduction for transparent overlays.
  - **Transitions**: **Cross Dissolve**, **Dip to Black**, **Wipe Left**, and **Wipe Right** at clip seams.
- **Audio Editing & Waveforms**
  - Real-time audio waveforms and fade envelopes rendered directly on timeline clips.
  - Per-clip **Volume (`0%–200%`)**, **Mute**, **Fade In**, and **Fade Out** controls in the Inspector.
  - **Extract Audio**: detach embedded audio from any video clip onto an independent Audio track with one click.
  - Live **Stereo Peak Level Meter (L/R)** and Master Volume / Mute controls in the Preview transport bar.
- **Real-Time Preview & Shuttle Playback**
  - Aspect-ratio-aware compositing (supporting landscape 16:9 and vertical 9:16 4K canvases) and multi-track stereo audio mixing.
  - Transport controls: Play/Pause (`Space`), **Shuttle speed (`J` / `K` / `L`)** from `-4x` to `+4x`, single-frame step (`Left`/`Right`), jump to start/end (`Home`), loop toggle, and scrubber bar.
- **4K UHD & GPU Hardware MP4 Export**
  - Background-threaded export to `.mp4` (**H.264 + AAC**, Rec.709 color-tagged with `+faststart`) supporting **4K UHD (`3840x2160`)**, **4K Vertical (`2160x3840`)**, `1440p`, `1080p`, `720p`, `480p`, **Match Project Canvas**, and **Custom** resolutions at `24`/`30`/`60 fps`.
  - Quality presets (**High CRF 18**, **Balanced CRF 21**, **Compact CRF 25**) and **Encoder** selection (**Auto GPU NVENC / AMF / QSV with CPU fallback**, or explicit **CPU libx264** / **NVENC** / **AMF** / **QSV**).
- **Project Save / Load & Undo / Redo**
  - Save and open `.vcut` JSON project files (`Ctrl+S` / `Ctrl+O`, backwards-compatible with v1 projects).
  - Full Undo / Redo history (`Ctrl+Z` / `Ctrl+Y`) with coalesced slider and drag gestures.

---

## Prerequisites (Windows / MSYS2 UCRT64)

Make sure MSYS2 (`D:\msys64` or `C:\msys64`) has the UCRT64 toolchain, SDL3, FFmpeg, FreeType2, and `nlohmann-json` installed:

```powershell
pacman -S --needed `
  mingw-w64-ucrt-x86_64-gcc `
  mingw-w64-ucrt-x86_64-ninja `
  mingw-w64-ucrt-x86_64-pkgconf `
  mingw-w64-ucrt-x86_64-sdl3 `
  mingw-w64-ucrt-x86_64-ffmpeg `
  mingw-w64-ucrt-x86_64-freetype `
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
| `J` / `K` / `L` | Shuttle reverse (`-1x/-2x/-4x`), Pause, Shuttle forward (`1x/2x/4x`) |
| `V` / `C` / `H` | Select tool / Razor blade tool / Hand pan tool |
| `S` or `Ctrl + B` | Split selected clip (or all clips) at the playhead |
| `Ctrl + C` / `Ctrl + V` / `Ctrl + D` | Copy, Paste at playhead, or Duplicate selected clip |
| `Delete` / `Backspace` | Delete selected timeline clip |
| `Shift + Delete` | Ripple Delete selected clip (closes gap) |
| `Left` / `Right` | Step 1 frame backward / forward |
| `Home` | Jump playhead to `00:00:00.00` |
| `Ctrl + Z` / `Ctrl + Y` | Undo / Redo |
| `Ctrl + I` | Import media files |
| `Ctrl + O` / `Ctrl + S` | Open / Save `.vcut` project |
| `Ctrl + E` | Open Export MP4 dialog |
| `Preview Drag / Corner Handles` | Move and resize Picture-in-Picture or Text overlays directly in Preview |
| `Mouse Wheel` (in Timeline) | Pan timeline horizontally |
| `Shift + Mouse Wheel` | Scroll timeline tracks vertically |
| `Ctrl + Mouse Wheel` | Zoom timeline in / out at mouse cursor |
| `Middle / Right Drag` or `Drag Empty Track` | Pan timeline horizontally & vertically |
| `Right-Click Clip` | Open clip context menu (Split, Duplicate, Copy, Extract Audio, Mute, Delete, Ripple Delete) |

---

## Project Structure

- `src/core/` — `Time`, `Project`, `TimelineOps`, and `Editor` (undo/redo state).
- `src/media/` — FFmpeg & FreeType2 wrappers (`MediaProbe`, `VideoDecoder`, `AudioDecoder`, `FrameBlit`, `TextRenderer`, `Waveform`).
- `src/engine/` — Real-time `Compositor`, `AudioMixer`, and `PlaybackEngine`.
- `src/export/` — MP4 (`H.264 + AAC`) `Exporter` (CPU `libx264` & GPU `NVENC`/`AMF`/`QSV`) and background `ExportJob`.
- `src/io/` — `.vcut` JSON `ProjectSerializer` (v2 with v1 compatibility).
- `src/ui/` — OpenGL `GlTexture`, `ThumbnailCache`, custom-styled `Widgets`, and dockable `Panels`.
- `src/app/` — `App` window/GL/ImGui initialization, docking layout, dialogs, and shortcuts.
- `tests/` — Automated unit tests (`test_timeline.cpp`) and end-to-end media/export/4K pipeline tests (`test_pipeline.cpp`).
