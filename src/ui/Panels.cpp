#include "ui/Panels.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <utility>

#include "core/TimelineOps.hpp"
#include "media/FrameBlit.hpp"
#include "ui/Widgets.hpp"

namespace vcut {

namespace {

const char* mediaBadge(MediaType t) {
    switch (t) {
        case MediaType::Video:
            return "VIDEO";
        case MediaType::Audio:
            return "AUDIO";
        case MediaType::Image:
            return "IMAGE";
        case MediaType::Text:
            return "TEXT";
    }
    return "MEDIA";
}

void drawStereoMeter(ImDrawList* dl, ImVec2 pos, float w, float h, float peakL, float peakR) {
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(20, 22, 28, 255), 3.0f);
    const float barH = (h - 6.0f) * 0.5f;
    const float innerW = w - 6.0f;

    auto drawBar = [&](float y, float level) {
        level = std::clamp(level, 0.0f, 1.0f);
        const float fill = innerW * level;
        if (fill <= 0.5f) return;
        const ImU32 col = level > 0.88f   ? IM_COL32(245, 75, 75, 255)
                          : level > 0.65f ? IM_COL32(245, 200, 65, 255)
                                          : IM_COL32(65, 210, 120, 255);
        dl->AddRectFilled(ImVec2(pos.x + 3.0f, y), ImVec2(pos.x + 3.0f + fill, y + barH), col,
                          2.0f);
    };
    drawBar(pos.y + 2.0f, peakL);
    drawBar(pos.y + 4.0f + barH, peakR);
    dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(60, 66, 80, 255), 3.0f);
}

std::string ellipsizeText(const std::string& s, float maxWidth) {
    if (maxWidth <= 8.0f || s.empty()) return "";
    if (ImGui::CalcTextSize(s.c_str()).x <= maxWidth) return s;

    const char* dots = "...";
    const float dotsW = ImGui::CalcTextSize(dots).x;
    if (maxWidth <= dotsW) return dots;

    std::size_t end = s.size();
    while (end > 0) {
        --end;
        // Step back to the start of a UTF-8 codepoint
        while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) {
            --end;
        }
        if (ImGui::CalcTextSize(s.data(), s.data() + end).x + dotsW <= maxWidth) {
            return s.substr(0, end) + dots;
        }
    }
    return dots;
}

void drawClipWaveform(ImDrawList* dl, const Clip& c, const AudioWaveform& wf, float x0, float x1,
                      float wy0, float wy1, float viewMinX, float viewMaxX, ImU32 color) {
    const float startX = std::max(x0 + 2.0f, viewMinX);
    const float endX = std::min(x1 - 2.0f, viewMaxX);
    if (endX <= startX || c.duration() <= 0) return;

    const float midY = (wy0 + wy1) * 0.5f;
    const float halfH = (wy1 - wy0) * 0.46f;
    const float clipW = std::max(1.0f, x1 - x0);
    const Time clipDur = c.duration();

    for (float px = startX; px < endX; px += 3.0f) {
        const double frac = std::clamp(static_cast<double>(px - x0) / clipW, 0.0, 1.0);
        const Time rel = static_cast<Time>(frac * static_cast<double>(clipDur));
        const Time srcT = c.in + c.timelineToSourceOffset(rel);
        float env = c.muted ? 0.0f : c.volume;
        if (c.fadeIn > 0 && rel < c.fadeIn) {
            env *= static_cast<float>(rel) / static_cast<float>(c.fadeIn);
        }
        if (c.fadeOut > 0 && clipDur - rel < c.fadeOut) {
            env *= static_cast<float>(clipDur - rel) / static_cast<float>(c.fadeOut);
        }
        const float amp = std::clamp(wf.peakAt(srcT) * env, 0.0f, 1.0f);
        const float dy = std::max(1.0f, amp * halfH);
        dl->AddLine(ImVec2(px, midY - dy), ImVec2(px, midY + dy), color, 1.5f);
    }
}

template <class Fn>
void applyControlEdit(Editor& editor, bool& inspectorGesture, bool changed, Fn&& fn) {
    if (ImGui::IsItemActivated() && !editor.inGesture()) {
        editor.beginGesture();
        inspectorGesture = true;
    }
    if (changed) {
        if (inspectorGesture && editor.inGesture())
            editor.gestureApply(std::forward<Fn>(fn));
        else
            editor.edit(std::forward<Fn>(fn));
    }
    if (ImGui::IsItemDeactivated() && inspectorGesture) {
        if (editor.inGesture()) editor.endGesture();
        inspectorGesture = false;
    }
}

void sameLineWrap(float nextEstimatedWidth) {
    const float avail = ImGui::GetWindowContentRegionMax().x;
    const float lastRight = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    if (lastRight + spacing + nextEstimatedWidth <= avail) {
        ImGui::SameLine();
    }
}

}  // namespace

void drawMediaBinPanel(Editor& editor, PlaybackEngine& playback, ThumbnailCache& thumbs,
                       const UiCallbacks& cb) {
    ImGui::Begin("Media Bin");

    if (ui::PrimaryButton("+ Import Media", ImVec2(126, 30))) {
        if (cb.onImportMedia) cb.onImportMedia();
    }
    ImGui::SameLine();
    if (ui::Button("+ Text", ImVec2(64, 30))) {
        const Time t = playback.playhead();
        int textClipId = 0;
        editor.edit([&](Project& p) {
            textClipId = ops::addTextClip(p, 0, t);
            return textClipId != 0;
        });
        if (textClipId != 0) {
            editor.selectedClip = textClipId;
            editor.selectedAsset = 0;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a Text overlay clip at the playhead");
    ImGui::SameLine();
    if (ui::Button(".SRT", ImVec2(48, 30))) {
        if (cb.onImportSubtitles) cb.onImportSubtitles();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Import .srt subtitle file onto a track");

    ImGui::TextDisabled("%zu media item%s", editor.project().assets.size(),
                        editor.project().assets.size() == 1 ? "" : "s");
    ImGui::Separator();

    if (editor.project().assets.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Drop video, audio, image, or .srt files here,\n"
            "or click '+ Import Media' / '+ Text'.");
        ImGui::End();
        return;
    }

    int appendAssetId = 0;
    int deleteAssetId = 0;

    ImGui::BeginChild("MediaList", ImVec2(0, 0), ImGuiChildFlags_None);
    for (const MediaAsset& a : editor.project().assets) {
        ImGui::PushID(a.id);
        const bool selected = (editor.selectedAsset == a.id);

        const ImVec2 cardMin = ImGui::GetCursorScreenPos();
        const float cardW = std::max(200.0f, ImGui::GetContentRegionAvail().x - 2.0f);
        const float rowH = 62.0f;
        const ImVec2 cardMax(cardMin.x + cardW, cardMin.y + rowH);

        const float btnSize = 26.0f;
        const float btnGap = 6.0f;
        const float rightPad = 8.0f;
        const float buttonsBlockW = btnSize * 2.0f + btnGap + rightPad;
        const float plusX = cardMax.x - buttonsBlockW;
        const float delX = plusX + btnSize + btnGap;
        const float btnY = cardMin.y + (rowH - btnSize) * 0.5f;

        if (ImGui::InvisibleButton("##asset", ImVec2(std::max(60.0f, cardW - buttonsBlockW - 6.0f), rowH))) {
            editor.selectedAsset = a.id;
            editor.selectedClip = 0;
        }
        const bool cardHovered = ImGui::IsItemHovered();
        if (cardHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            appendAssetId = a.id;
        }
        if (cardHovered) {
            ImGui::SetTooltip("%s\nDouble-click or press '+' to add to timeline", a.name.c_str());
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("VCUT_ASSET", &a.id, sizeof(int));
            ImGui::Text("Add %s", a.name.c_str());
            ImGui::EndDragDropSource();
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 cardBg = selected      ? IM_COL32(38, 58, 88, 255)
                             : cardHovered ? IM_COL32(32, 36, 46, 255)
                                           : IM_COL32(23, 26, 33, 255);
        const ImU32 cardBorder =
            selected ? IM_COL32(82, 138, 215, 255) : IM_COL32(45, 50, 62, 255);
        dl->AddRectFilled(cardMin, cardMax, cardBg, 6.0f);
        dl->AddRect(cardMin, cardMax, cardBorder, 6.0f);

        const float thumbY = cardMin.y + (rowH - ThumbnailCache::kThumbH) * 0.5f;
        const ImVec2 thumbMin(cardMin.x + 6.0f, thumbY);
        const ImVec2 thumbMax(thumbMin.x + ThumbnailCache::kThumbW,
                              thumbMin.y + ThumbnailCache::kThumbH);
        dl->AddRectFilled(thumbMin, thumbMax, IM_COL32(16, 18, 24, 255), 4.0f);
        if (ImTextureID tid = thumbs.get(a)) {
            dl->AddImage(tid, thumbMin, thumbMax);
        } else {
            const char* badge = mediaBadge(a.type);
            const ImVec2 bsz = ImGui::CalcTextSize(badge);
            dl->AddText(ImVec2(thumbMin.x + (ThumbnailCache::kThumbW - bsz.x) * 0.5f,
                               thumbMin.y + (ThumbnailCache::kThumbH - bsz.y) * 0.5f),
                        IM_COL32(150, 165, 190, 255), badge);
        }
        dl->AddRect(thumbMin, thumbMax, IM_COL32(60, 66, 80, 255), 4.0f);

        const float textX = thumbMax.x + 10.0f;
        const float textMaxX = std::max(textX + 16.0f, plusX - 8.0f);
        const float textMaxW = textMaxX - textX;

        const std::string displayTitle = ellipsizeText(a.name, textMaxW);
        char meta[128];
        if (a.type == MediaType::Image) {
            std::snprintf(meta, sizeof meta, "IMAGE  %dx%d", a.width, a.height);
        } else if (a.type == MediaType::Video) {
            std::snprintf(meta, sizeof meta, "%s  %dx%d%s", formatTime(a.duration).c_str(), a.width,
                          a.height, a.hasAudio ? "  [Audio]" : "");
        } else {
            std::snprintf(meta, sizeof meta, "AUDIO  %s", formatTime(a.duration).c_str());
        }
        const std::string displayMeta = ellipsizeText(meta, textMaxW);

        dl->PushClipRect(ImVec2(textX, cardMin.y + 4.0f), ImVec2(textMaxX, cardMax.y - 4.0f), true);
        dl->AddText(ImVec2(textX, cardMin.y + 10.0f),
                    a.missing ? IM_COL32(255, 100, 100, 255) : IM_COL32(238, 241, 248, 255),
                    displayTitle.c_str());
        dl->AddText(ImVec2(textX, cardMin.y + 33.0f), IM_COL32(148, 156, 174, 255),
                    displayMeta.c_str());
        dl->PopClipRect();

        ImGui::SetCursorScreenPos(ImVec2(plusX, btnY));
        if (ui::CircleActionButton("##add", false, btnSize)) appendAssetId = a.id;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add to timeline");

        ImGui::SetCursorScreenPos(ImVec2(delX, btnY));
        if (ui::CircleActionButton("##del", true, btnSize)) deleteAssetId = a.id;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove from project");

        ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMax.y + 6.0f));
        ImGui::Dummy(ImVec2(1.0f, 0.0f));
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (appendAssetId != 0) {
        int newClip = 0;
        editor.edit([&](Project& p) {
            newClip = ops::appendClip(p, appendAssetId);
            return newClip != 0;
        });
        if (newClip != 0) editor.selectedClip = newClip;
    }
    if (deleteAssetId != 0) {
        editor.edit([&](Project& p) { return ops::removeAsset(p, deleteAssetId); });
        if (editor.selectedAsset == deleteAssetId) editor.selectedAsset = 0;
    }

    ImGui::End();
}

void drawPreviewPanel(Editor& editor, PlaybackEngine& playback, TimelineViewState& view,
                      GlTexture& previewTex) {
    ImGui::Begin("Preview");

    const Project& proj = editor.project();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float controlsH = 70.0f;
    const float canvasW = std::max(64.0f, avail.x);
    const float canvasH = std::max(64.0f, avail.y - controlsH);

    const FitRect fit = fitRect(proj.settings.width, proj.settings.height,
                                static_cast<int>(canvasW), static_cast<int>(canvasH));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 pMin(origin.x + fit.x, origin.y + fit.y);
    const ImVec2 pMax(pMin.x + fit.w, pMin.y + fit.h);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + canvasW, origin.y + canvasH),
                      IM_COL32(14, 15, 19, 255), 4.0f);
    if (previewTex.valid()) {
        dl->AddImage(previewTex.imguiId(), pMin, pMax);
    }
    dl->AddRect(pMin, pMax, IM_COL32(55, 60, 72, 255));

    // Interactive on-canvas selection overlay & drag/scale handles for the selected clip
    const Time curPlayhead = playback.playhead();
    const Track* selTrack = nullptr;
    const Clip* selClip = ops::findClip(proj, editor.selectedClip, &selTrack);
    const bool showOverlay = selClip && selTrack && selTrack->kind == TrackKind::Video &&
                             !selTrack->hidden && selClip->contains(curPlayhead) && fit.w > 8 &&
                             fit.h > 8;

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool previewHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (showOverlay) {
        const FitRect cBox =
            playback.clipScreenBox(proj, *selClip, proj.settings.width, proj.settings.height);
        if (cBox.w > 0 && cBox.h > 0) {
            const float sx = static_cast<float>(fit.w) / static_cast<float>(proj.settings.width);
            const float sy = static_cast<float>(fit.h) / static_cast<float>(proj.settings.height);
            const ImVec2 bMin(pMin.x + cBox.x * sx, pMin.y + cBox.y * sy);
            const ImVec2 bMax(pMin.x + (cBox.x + cBox.w) * sx, pMin.y + (cBox.y + cBox.h) * sy);

            dl->PushClipRect(pMin, pMax, true);
            dl->AddRect(bMin, bMax, IM_COL32(0, 215, 230, 235), 0.0f, 0, 1.5f);

            const float hr = 5.5f;
            const ImVec2 corners[4] = {
                bMin,
                ImVec2(bMax.x, bMin.y),
                ImVec2(bMin.x, bMax.y),
                bMax,
            };
            bool onCorner = false;
            for (const ImVec2& c : corners) {
                dl->AddRectFilled(ImVec2(c.x - hr, c.y - hr), ImVec2(c.x + hr, c.y + hr),
                                  IM_COL32(255, 255, 255, 255), 2.0f);
                dl->AddRect(ImVec2(c.x - hr, c.y - hr), ImVec2(c.x + hr, c.y + hr),
                            IM_COL32(0, 185, 200, 255), 2.0f);
                if (std::fabs(mouse.x - c.x) <= hr + 4.0f &&
                    std::fabs(mouse.y - c.y) <= hr + 4.0f) {
                    onCorner = true;
                }
            }
            dl->PopClipRect();

            const bool insideBox = (mouse.x >= bMin.x && mouse.x <= bMax.x && mouse.y >= bMin.y &&
                                    mouse.y <= bMax.y);
            if (previewHovered && view.previewDragKind == TimelineViewState::PreviewDragKind::None) {
                if (onCorner)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                else if (insideBox)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }

            if (previewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                (onCorner || insideBox) &&
                mouse.x >= pMin.x && mouse.x <= pMax.x && mouse.y >= pMin.y && mouse.y <= pMax.y) {
                view.previewDragKind = onCorner ? TimelineViewState::PreviewDragKind::Scale
                                                : TimelineViewState::PreviewDragKind::Move;
                view.previewDragClipId = selClip->id;
                view.previewStartMouseX = mouse.x;
                view.previewStartMouseY = mouse.y;
                view.previewOrigPosX = selClip->posX;
                view.previewOrigPosY = selClip->posY;
                view.previewOrigScale = selClip->scale;
                editor.beginGesture();
            }
        }
    }

    if (view.previewDragKind != TimelineViewState::PreviewDragKind::None) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const int cid = view.previewDragClipId;
            if (view.previewDragKind == TimelineViewState::PreviewDragKind::Move) {
                float nx = view.previewOrigPosX + (mouse.x - view.previewStartMouseX) / fit.w;
                float ny = view.previewOrigPosY + (mouse.y - view.previewStartMouseY) / fit.h;
                if (std::fabs(nx) < 0.015f) nx = 0.0f;
                if (std::fabs(ny) < 0.015f) ny = 0.0f;
                nx = std::clamp(nx, -1.5f, 1.5f);
                ny = std::clamp(ny, -1.5f, 1.5f);
                editor.gestureApply([cid, nx, ny](Project& p) {
                    if (auto r = ops::findClip(p, cid)) {
                        r.clip->posX = nx;
                        r.clip->posY = ny;
                        return true;
                    }
                    return false;
                });
            } else {
                const float cx = pMin.x + (0.5f + view.previewOrigPosX) * fit.w;
                const float cy = pMin.y + (0.5f + view.previewOrigPosY) * fit.h;
                const float d0 = std::hypot(view.previewStartMouseX - cx, view.previewStartMouseY - cy);
                const float d1 = std::hypot(mouse.x - cx, mouse.y - cy);
                if (d0 > 8.0f) {
                    const float newScale =
                        std::clamp(view.previewOrigScale * (d1 / d0), 0.1f, 4.0f);
                    editor.gestureApply([cid, newScale](Project& p) {
                        if (auto r = ops::findClip(p, cid)) {
                            r.clip->scale = newScale;
                            return true;
                        }
                        return false;
                    });
                }
            }
        } else {
            if (editor.inGesture()) editor.endGesture();
            view.previewDragKind = TimelineViewState::PreviewDragKind::None;
            view.previewDragClipId = 0;
        }
    }

    ImGui::Dummy(ImVec2(canvasW, canvasH));

    // Scrubber bar
    const Time dur = std::max(proj.duration(), Time{1});
    float sec = static_cast<float>(toSeconds(curPlayhead));
    const float maxSec = static_cast<float>(toSeconds(dur));
    if (ui::ScrubberBar("##scrub", &sec, maxSec)) {
        playback.pause();
        playback.seek(fromSeconds(sec));
    }

    // Transport buttons
    const float transportY = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(transportY + 3.0f);
    if (ui::TransportButton("##start", ui::TransportIcon::SkipStart)) playback.seek(0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jump to Start (Home)");
    ImGui::SameLine();
    ImGui::SetCursorPosY(transportY + 3.0f);
    if (ui::TransportButton("##prev", ui::TransportIcon::StepBack)) playback.stepFrames(proj, -1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous Frame (Left)");
    ImGui::SameLine();
    ImGui::SetCursorPosY(transportY);
    if (ui::TransportButton(
            "##play", playback.isPlaying() ? ui::TransportIcon::Pause : ui::TransportIcon::Play,
            true)) {
        playback.togglePlay();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play / Pause (Space)  |  Shuttle: J / K / L");
    ImGui::SameLine();
    ImGui::SetCursorPosY(transportY + 3.0f);
    if (ui::TransportButton("##next", ui::TransportIcon::StepFwd)) playback.stepFrames(proj, 1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next Frame (Right)");
    ImGui::SameLine();
    ImGui::SetCursorPosY(transportY + 3.0f);
    if (ui::TransportButton("##end", ui::TransportIcon::SkipEnd)) {
        playback.pause();
        playback.seek(proj.duration());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jump to End");

    ImGui::SameLine();
    ImGui::SetCursorPosY(transportY + 7.0f);
    bool loop = playback.loop();
    if (ui::ToggleSwitch("Loop", &loop)) playback.setLoop(loop);

    ImGui::SameLine();
    ImGui::SetCursorPosY(transportY + 7.0f);
    const float pSpeed = playback.playbackSpeed();
    if (playback.isPlaying() && std::fabs(pSpeed - 1.0f) > 0.05f) {
        ImGui::Text(" %s / %s (%.0fx)", formatTime(curPlayhead).c_str(),
                    formatTime(proj.duration()).c_str(), pSpeed);
    } else {
        ImGui::Text(" %s / %s", formatTime(curPlayhead).c_str(),
                    formatTime(proj.duration()).c_str());
    }

    ImGui::SameLine();
    ImGui::SetCursorPosY(transportY + 3.0f);
    const bool muted = playback.masterMuted();
    if (ui::Button(muted ? "Muted" : "Audio", ImVec2(58, 28), !muted)) {
        playback.setMasterMuted(!muted);
    }
    ImGui::SameLine();
    ImGui::SetCursorPosY(transportY + 5.0f);
    float vol = playback.masterVolume() * 100.0f;
    if (ui::SliderFloat("##mastervol", &vol, 0.0f, 150.0f, "%.0f%%", 82.0f)) {
        playback.setMasterVolume(vol / 100.0f);
    }

    // Live stereo peak level meter
    ImGui::SameLine();
    ImGui::SetCursorPosY(transportY + 7.0f);
    const ImVec2 meterPos = ImGui::GetCursorScreenPos();
    drawStereoMeter(dl, meterPos, 64.0f, 20.0f, playback.peakLeft(), playback.peakRight());
    ImGui::Dummy(ImVec2(64.0f, 20.0f));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stereo Output Level (L / R)");

    ImGui::End();
}

void drawInspectorPanel(Editor& editor, PlaybackEngine& playback, TimelineViewState& view) {
    ImGui::Begin("Inspector");

    static bool inspectorGesture = false;
    static int gestureClipId = 0;

    const Project& proj = editor.project();
    const Track* clipTrack = nullptr;
    const Clip* clip = ops::findClip(proj, editor.selectedClip, &clipTrack);

    if (inspectorGesture && (!clip || clip->id != gestureClipId)) {
        if (editor.inGesture()) editor.endGesture();
        inspectorGesture = false;
        gestureClipId = 0;
    }

    if (clip) {
        const MediaAsset* asset = clip->isText ? nullptr : proj.findAsset(clip->assetId);
        const int clipId = clip->id;
        gestureClipId = clipId;
        const bool isVisual =
            clip->isText || (clipTrack && clipTrack->kind == TrackKind::Video);

        ImGui::SeparatorText("Selected Clip");
        ImGui::Text("Source: %s",
                    clip->isText ? "Text Overlay" : (asset ? asset->name.c_str() : "(unknown)"));
        ImGui::Text("Track:  %s  |  Length: %s", clipTrack ? clipTrack->name.c_str() : "",
                    formatTime(clip->duration()).c_str());
        ImGui::Text("Range:  %s - %s", formatTime(clip->start).c_str(),
                    formatTime(clip->end()).c_str());

        if (clip->isText && ImGui::CollapsingHeader("Text Overlay", ImGuiTreeNodeFlags_DefaultOpen)) {
            char buf[512];
            std::snprintf(buf, sizeof buf, "%s", clip->text.content.c_str());
            const bool edited = ImGui::InputTextMultiline("##txtcontent", buf, sizeof buf,
                                                          ImVec2(-1.0f, 58.0f));
            applyControlEdit(editor, inspectorGesture, edited,
                             [clipId, s = std::string(buf)](Project& p) {
                                 if (auto r = ops::findClip(p, clipId)) {
                                     if (r.clip->text.content == s) return false;
                                     r.clip->text.content = s;
                                     return true;
                                 }
                                 return false;
                             });

            float fontSizeF = static_cast<float>(clip->text.fontSize);
            const bool fChanged =
                ui::SliderFloat("Font Size", &fontSizeF, 16.0f, 180.0f, "%.0f px");
            applyControlEdit(editor, inspectorGesture, fChanged, [clipId, fontSizeF](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    const int ns = std::clamp(static_cast<int>(std::lround(fontSizeF)), 12, 240);
                    if (r.clip->text.fontSize == ns) return false;
                    r.clip->text.fontSize = ns;
                    return true;
                }
                return false;
            });

            float col[3] = {clip->text.color[0], clip->text.color[1], clip->text.color[2]};
            if (ImGui::ColorEdit3("Text Color", col)) {
                editor.edit([clipId, col](Project& p) {
                    if (auto r = ops::findClip(p, clipId)) {
                        for (int i = 0; i < 3; ++i) r.clip->text.color[i] = col[i];
                        return true;
                    }
                    return false;
                });
            }

            float bgCol[3] = {clip->text.bgColor[0], clip->text.bgColor[1],
                              clip->text.bgColor[2]};
            if (ImGui::ColorEdit3("Box Color", bgCol)) {
                editor.edit([clipId, bgCol](Project& p) {
                    if (auto r = ops::findClip(p, clipId)) {
                        for (int i = 0; i < 3; ++i) r.clip->text.bgColor[i] = bgCol[i];
                        return true;
                    }
                    return false;
                });
            }

            float bgOp = clip->text.bgOpacity * 100.0f;
            const bool bgChanged = ui::SliderFloat("Box Alpha", &bgOp, 0.0f, 100.0f, "%.0f%%");
            applyControlEdit(editor, inspectorGesture, bgChanged, [clipId, bgOp](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->text.bgOpacity = std::clamp(bgOp / 100.0f, 0.0f, 1.0f);
                    return true;
                }
                return false;
            });

            bool sh = clip->text.shadow;
            if (ui::ToggleSwitch("Drop Shadow", &sh)) {
                editor.edit([clipId, sh](Project& p) {
                    if (auto r = ops::findClip(p, clipId)) {
                        r.clip->text.shadow = sh;
                        return true;
                    }
                    return false;
                });
            }
        }

        if (isVisual && ImGui::CollapsingHeader("Transform & Opacity", ImGuiTreeNodeFlags_DefaultOpen)) {
            float scPct = clip->scale * 100.0f;
            const bool scChanged = ui::SliderFloat("Scale", &scPct, 10.0f, 400.0f, "%.0f%%");
            applyControlEdit(editor, inspectorGesture, scChanged, [clipId, scPct](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->scale = std::clamp(scPct / 100.0f, 0.1f, 4.0f);
                    return true;
                }
                return false;
            });

            float pxPct = clip->posX * 100.0f;
            const bool pxChanged = ui::SliderFloat("Pos X", &pxPct, -100.0f, 100.0f, "%.0f%%");
            applyControlEdit(editor, inspectorGesture, pxChanged, [clipId, pxPct](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->posX = std::clamp(pxPct / 100.0f, -1.5f, 1.5f);
                    return true;
                }
                return false;
            });

            float pyPct = clip->posY * 100.0f;
            const bool pyChanged = ui::SliderFloat("Pos Y", &pyPct, -100.0f, 100.0f, "%.0f%%");
            applyControlEdit(editor, inspectorGesture, pyChanged, [clipId, pyPct](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->posY = std::clamp(pyPct / 100.0f, -1.5f, 1.5f);
                    return true;
                }
                return false;
            });

            float opPct = clip->opacity * 100.0f;
            const bool opChanged = ui::SliderFloat("Opacity", &opPct, 0.0f, 100.0f, "%.0f%%");
            applyControlEdit(editor, inspectorGesture, opChanged, [clipId, opPct](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->opacity = std::clamp(opPct / 100.0f, 0.0f, 1.0f);
                    return true;
                }
                return false;
            });

            const float maxFadeSec =
                std::max(0.1f, static_cast<float>(toSeconds(clip->duration()) * 0.5));
            float vFadeIn = static_cast<float>(toSeconds(clip->videoFadeIn));
            const bool vfiChanged = ui::SliderFloat("Vid Fade In", &vFadeIn, 0.0f, maxFadeSec, "%.2f s");
            applyControlEdit(editor, inspectorGesture, vfiChanged, [clipId, vFadeIn](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->videoFadeIn =
                        std::clamp<Time>(fromSeconds(vFadeIn), 0, r.clip->duration());
                    return true;
                }
                return false;
            });

            float vFadeOut = static_cast<float>(toSeconds(clip->videoFadeOut));
            const bool vfoChanged =
                ui::SliderFloat("Vid Fade Out", &vFadeOut, 0.0f, maxFadeSec, "%.2f s");
            applyControlEdit(editor, inspectorGesture, vfoChanged, [clipId, vFadeOut](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->videoFadeOut =
                        std::clamp<Time>(fromSeconds(vFadeOut), 0, r.clip->duration());
                    return true;
                }
                return false;
            });

            if (ui::Button("Reset Transform", ImVec2(ImGui::GetContentRegionAvail().x, 26))) {
                editor.edit([clipId](Project& p) {
                    if (auto r = ops::findClip(p, clipId)) {
                        r.clip->scale = 1.0f;
                        r.clip->posX = 0.0f;
                        r.clip->posY = 0.0f;
                        r.clip->opacity = 1.0f;
                        return true;
                    }
                    return false;
                });
            }
        }

        // Playback speed (for media clips: video & audio)
        if (!clip->isText && (!asset || asset->type != MediaType::Image) &&
            ImGui::CollapsingHeader("Speed", ImGuiTreeNodeFlags_DefaultOpen)) {
            float spd = clip->speed;
            const bool spdChanged = ui::SliderFloat("Speed", &spd, 0.25f, 4.0f, "%.2fx");
            applyControlEdit(editor, inspectorGesture, spdChanged, [clipId, spd](Project& p) {
                return ops::setClipSpeed(p, clipId, spd);
            });

            static constexpr float kSpeedPresets[] = {0.5f, 1.0f, 1.5f, 2.0f};
            for (int si = 0; si < 4; ++si) {
                if (si > 0) ImGui::SameLine(0.0f, 4.0f);
                char sl[16];
                std::snprintf(sl, sizeof sl, "%.1fx##sp", kSpeedPresets[si]);
                const bool active = std::fabs(clip->speed - kSpeedPresets[si]) < 0.02f;
                if (ui::Button(sl, ImVec2(48, 24), active)) {
                    const float target = kSpeedPresets[si];
                    editor.edit([clipId, target](Project& p) {
                        return ops::setClipSpeed(p, clipId, target);
                    });
                }
            }
        }

        if (isVisual && !clip->isText && ImGui::CollapsingHeader("Color & Filters")) {
            float br = clip->brightness;
            const bool brChanged = ui::SliderFloat("Brightness", &br, -1.0f, 1.0f, "%.2f");
            applyControlEdit(editor, inspectorGesture, brChanged, [clipId, br](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->brightness = std::clamp(br, -1.0f, 1.0f);
                    return true;
                }
                return false;
            });

            float ct = clip->contrast;
            const bool ctChanged = ui::SliderFloat("Contrast", &ct, 0.0f, 2.0f, "%.2f");
            applyControlEdit(editor, inspectorGesture, ctChanged, [clipId, ct](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->contrast = std::clamp(ct, 0.0f, 2.0f);
                    return true;
                }
                return false;
            });

            float sat = clip->saturation;
            const bool satChanged = ui::SliderFloat("Saturation", &sat, 0.0f, 2.0f, "%.2f");
            applyControlEdit(editor, inspectorGesture, satChanged, [clipId, sat](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->saturation = std::clamp(sat, 0.0f, 2.0f);
                    return true;
                }
                return false;
            });

            float vig = clip->vignette * 100.0f;
            const bool vigChanged = ui::SliderFloat("Vignette", &vig, 0.0f, 100.0f, "%.0f%%");
            applyControlEdit(editor, inspectorGesture, vigChanged, [clipId, vig](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->vignette = std::clamp(vig / 100.0f, 0.0f, 1.0f);
                    return true;
                }
                return false;
            });

            bool gs = clip->grayscale;
            if (ui::ToggleSwitch("Black & White", &gs)) {
                editor.edit([clipId, gs](Project& p) {
                    if (auto r = ops::findClip(p, clipId)) {
                        r.clip->grayscale = gs;
                        return true;
                    }
                    return false;
                });
            }

            if (ui::Button("Reset Color", ImVec2(ImGui::GetContentRegionAvail().x, 26))) {
                editor.edit([clipId](Project& p) {
                    if (auto r = ops::findClip(p, clipId)) {
                        r.clip->brightness = 0.0f;
                        r.clip->contrast = 1.0f;
                        r.clip->saturation = 1.0f;
                        r.clip->vignette = 0.0f;
                        r.clip->grayscale = false;
                        return true;
                    }
                    return false;
                });
            }
        }

        if (isVisual && !clip->isText && ImGui::CollapsingHeader("Chroma Key (Green Screen)")) {
            bool ck = clip->chromaKey;
            if (ui::ToggleSwitch("Enable Chroma Key", &ck)) {
                editor.edit([clipId, ck](Project& p) {
                    if (auto r = ops::findClip(p, clipId)) {
                        r.clip->chromaKey = ck;
                        return true;
                    }
                    return false;
                });
            }
            if (ck) {
                float ccol[3] = {clip->chromaColor[0], clip->chromaColor[1], clip->chromaColor[2]};
                if (ImGui::ColorEdit3("Key Color", ccol)) {
                    editor.edit([clipId, ccol](Project& p) {
                        if (auto r = ops::findClip(p, clipId)) {
                            for (int i = 0; i < 3; ++i) r.clip->chromaColor[i] = ccol[i];
                            return true;
                        }
                        return false;
                    });
                }

                float sim = clip->chromaSimilarity;
                const bool simChanged = ui::SliderFloat("Similarity", &sim, 0.02f, 0.65f, "%.2f");
                applyControlEdit(editor, inspectorGesture, simChanged, [clipId, sim](Project& p) {
                    if (auto r = ops::findClip(p, clipId)) {
                        r.clip->chromaSimilarity = std::clamp(sim, 0.02f, 0.65f);
                        return true;
                    }
                    return false;
                });

                float bld = clip->chromaBlend;
                const bool bldChanged = ui::SliderFloat("Edge Blend", &bld, 0.0f, 0.35f, "%.2f");
                applyControlEdit(editor, inspectorGesture, bldChanged, [clipId, bld](Project& p) {
                    if (auto r = ops::findClip(p, clipId)) {
                        r.clip->chromaBlend = std::clamp(bld, 0.0f, 0.35f);
                        return true;
                    }
                    return false;
                });
            }
        }

        if (isVisual && ImGui::CollapsingHeader("Transition In")) {
            static constexpr const char* kTransLabels[] = {
                "None", "Cross Dissolve", "Dip to Black", "Wipe Left", "Wipe Right",
            };
            int trIdx = static_cast<int>(clip->transitionIn);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##transin", &trIdx, kTransLabels, 5)) {
                editor.edit([clipId, trIdx](Project& p) {
                    if (auto r = ops::findClip(p, clipId)) {
                        r.clip->transitionIn = static_cast<TransitionKind>(std::clamp(trIdx, 0, 4));
                        if (r.clip->transitionIn != TransitionKind::None &&
                            r.clip->transitionInDur <= 0) {
                            r.clip->transitionInDur = std::min<Time>(kSecond / 2, r.clip->duration());
                        }
                        return true;
                    }
                    return false;
                });
            }
            if (clip->transitionIn != TransitionKind::None) {
                const float maxTr =
                    std::max(0.1f, static_cast<float>(toSeconds(clip->duration())));
                float trSec = static_cast<float>(toSeconds(clip->transitionInDur));
                const bool trChanged = ui::SliderFloat("Duration##tr", &trSec, 0.05f, maxTr, "%.2f s");
                applyControlEdit(editor, inspectorGesture, trChanged, [clipId, trSec](Project& p) {
                    if (auto r = ops::findClip(p, clipId)) {
                        r.clip->transitionInDur =
                            std::clamp<Time>(fromSeconds(trSec), 0, r.clip->duration());
                        return true;
                    }
                    return false;
                });
            }
        }

        if (asset && asset->hasAudio &&
            ImGui::CollapsingHeader("Clip Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool clipMuted = clip->muted;
            if (ui::ToggleSwitch("Mute Clip Audio", &clipMuted)) {
                editor.edit([clipId, clipMuted](Project& p) {
                    if (auto r = ops::findClip(p, clipId)) {
                        r.clip->muted = clipMuted;
                        return true;
                    }
                    return false;
                });
            }

            float volPct = clip->volume * 100.0f;
            const bool vChanged = ui::SliderFloat("Volume", &volPct, 0.0f, 200.0f, "%.0f%%");
            applyControlEdit(editor, inspectorGesture, vChanged, [clipId, volPct](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->volume = std::clamp(volPct / 100.0f, 0.0f, 2.0f);
                    return true;
                }
                return false;
            });

            const float maxFadeSec =
                std::max(0.1f, static_cast<float>(toSeconds(clip->duration()) * 0.5));
            float fadeInSec = static_cast<float>(toSeconds(clip->fadeIn));
            const bool fiChanged =
                ui::SliderFloat("Fade In", &fadeInSec, 0.0f, maxFadeSec, "%.2f s");
            applyControlEdit(editor, inspectorGesture, fiChanged, [clipId, fadeInSec](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->fadeIn = std::clamp<Time>(fromSeconds(fadeInSec), 0, r.clip->duration());
                    return true;
                }
                return false;
            });

            float fadeOutSec = static_cast<float>(toSeconds(clip->fadeOut));
            const bool foChanged =
                ui::SliderFloat("Fade Out", &fadeOutSec, 0.0f, maxFadeSec, "%.2f s");
            applyControlEdit(editor, inspectorGesture, foChanged, [clipId, fadeOutSec](Project& p) {
                if (auto r = ops::findClip(p, clipId)) {
                    r.clip->fadeOut = std::clamp<Time>(fromSeconds(fadeOutSec), 0, r.clip->duration());
                    return true;
                }
                return false;
            });

            if (clipTrack && clipTrack->kind == TrackKind::Video) {
                ImGui::Spacing();
                if (ui::PrimaryButton("Extract Audio to Track",
                                      ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
                    int audioClipId = 0;
                    editor.edit([&](Project& p) {
                        audioClipId = ops::extractAudio(p, clipId);
                        return audioClipId != 0;
                    });
                    if (audioClipId != 0) editor.selectedClip = audioClipId;
                }
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Clip Actions");
        const float halfW = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;
        if (ui::Button("Duplicate (Ctrl+D)", ImVec2(halfW, 28))) {
            int dupId = 0;
            editor.edit([&](Project& p) {
                dupId = ops::duplicateClip(p, clipId);
                return dupId != 0;
            });
            if (dupId != 0) editor.selectedClip = dupId;
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (ui::Button("Split (S)", ImVec2(halfW, 28))) {
            editor.edit(
                [&](Project& p) { return ops::splitClip(p, clipId, playback.playhead()) != 0; });
        }
        if (ui::Button("Delete (Del)", ImVec2(halfW, 28))) {
            editor.edit([&](Project& p) { return ops::deleteClip(p, clipId); });
            editor.selectedClip = 0;
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (ui::Button("Ripple Del", ImVec2(halfW, 28))) {
            editor.edit([&](Project& p) { return ops::rippleDeleteClip(p, clipId); });
            editor.selectedClip = 0;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Delete clip and shift later clips on track left (Shift+Del)");
        }
        (void)view;
    } else if (const MediaAsset* asset = proj.findAsset(editor.selectedAsset)) {
        ImGui::SeparatorText("Media Asset");
        ImGui::Text("Name: %s", asset->name.c_str());
        ImGui::Text("Type: %s", mediaBadge(asset->type));
        if (asset->hasVideo) {
            ImGui::Text("Size: %d x %d", asset->width, asset->height);
            if (asset->fps > 0.0) ImGui::Text("FPS:  %.2f", asset->fps);
        }
        ImGui::Text("Audio: %s", asset->hasAudio ? "Yes" : "No");
        if (asset->duration > 0) ImGui::Text("Duration: %s", formatTime(asset->duration).c_str());
        ImGui::TextWrapped("Path: %s", asset->path.c_str());

        ImGui::Spacing();
        const int assetId = asset->id;
        if (ui::PrimaryButton("Add to Timeline", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
            int newClip = 0;
            editor.edit([&](Project& p) {
                newClip = ops::appendClip(p, assetId);
                return newClip != 0;
            });
            if (newClip != 0) editor.selectedClip = newClip;
        }
    } else {
        ImGui::TextDisabled("Select a clip on the timeline or a file in the Media Bin.");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Project Settings");
    {
        struct CanvasPreset {
            const char* label;
            int w;
            int h;
        };
        static constexpr CanvasPreset kCanvasPresets[] = {
            {"4K UHD (3840x2160)", 3840, 2160},
            {"1440p QHD (2560x1440)", 2560, 1440},
            {"1080p FHD (1920x1080)", 1920, 1080},
            {"720p HD (1280x720)", 1280, 720},
            {"4K Vertical (2160x3840)", 2160, 3840},
            {"1080p Vertical (1080x1920)", 1080, 1920},
        };
        int curCanvasIdx = -1;
        for (int i = 0; i < static_cast<int>(std::size(kCanvasPresets)); ++i) {
            if (proj.settings.width == kCanvasPresets[i].w &&
                proj.settings.height == kCanvasPresets[i].h) {
                curCanvasIdx = i;
                break;
            }
        }
        char customLabel[48];
        std::snprintf(customLabel, sizeof customLabel, "Custom (%dx%d)", proj.settings.width,
                      proj.settings.height);
        const char* previewLabel =
            curCanvasIdx >= 0 ? kCanvasPresets[curCanvasIdx].label : customLabel;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##canvas_preset", previewLabel)) {
            for (int i = 0; i < static_cast<int>(std::size(kCanvasPresets)); ++i) {
                const bool selected = (i == curCanvasIdx);
                if (ImGui::Selectable(kCanvasPresets[i].label, selected)) {
                    const int newW = kCanvasPresets[i].w;
                    const int newH = kCanvasPresets[i].h;
                    editor.edit([newW, newH](Project& p) {
                        if (p.settings.width == newW && p.settings.height == newH) return false;
                        p.settings.width = newW;
                        p.settings.height = newH;
                        return true;
                    });
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        static constexpr int kProjectFps[] = {24, 30, 60};
        static constexpr const char* kProjectFpsLabels[] = {"24 fps", "30 fps", "60 fps"};
        int fpsIdx = 1;
        for (int i = 0; i < static_cast<int>(std::size(kProjectFps)); ++i) {
            if (proj.settings.fps == kProjectFps[i]) {
                fpsIdx = i;
                break;
            }
        }
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::Combo("Frame rate##proj", &fpsIdx, kProjectFpsLabels,
                         static_cast<int>(std::size(kProjectFpsLabels)))) {
            const int newFps = kProjectFps[fpsIdx];
            editor.edit([newFps](Project& p) {
                if (p.settings.fps == newFps) return false;
                p.settings.fps = newFps;
                return true;
            });
        }
    }
    ImGui::Text("Audio: %d Hz stereo", proj.settings.sampleRate);
    ImGui::Text("Timeline duration: %s", formatTime(proj.duration()).c_str());

    ImGui::End();
}

void drawTimelinePanel(Editor& editor, PlaybackEngine& playback, TimelineViewState& view,
                       WaveformCache& waveforms) {
    ImGui::Begin("Timeline");

    const Project& proj = editor.project();

    // Tools: Select (V), Razor (C), Hand (H)
    if (ui::Button("Select (V)", ImVec2(0, 28), view.tool == TimelineTool::Select)) {
        view.tool = TimelineTool::Select;
    }
    sameLineWrap(82.0f);
    if (ui::Button("Razor (C)", ImVec2(0, 28), view.tool == TimelineTool::Razor)) {
        view.tool = TimelineTool::Razor;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click directly on any clip to slice it (C)");
    sameLineWrap(76.0f);
    if (ui::Button("Hand (H)", ImVec2(0, 28), view.tool == TimelineTool::Hand)) {
        view.tool = TimelineTool::Hand;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Pan timeline (H).\n"
            "Tip: You can always pan with Middle/Right-drag or empty track space drag.");
    }

    sameLineWrap(74.0f);
    if (ui::Button("Split (S)")) {
        const Time t = playback.playhead();
        const int pref = editor.selectedClip;
        editor.edit([&](Project& p) { return !ops::splitAt(p, t, pref).empty(); });
    }
    sameLineWrap(56.0f);
    ImGui::BeginDisabled(editor.selectedClip == 0);
    if (ui::Button("Dup")) {
        const int id = editor.selectedClip;
        int dupId = 0;
        editor.edit([&](Project& p) {
            dupId = ops::duplicateClip(p, id);
            return dupId != 0;
        });
        if (dupId != 0) editor.selectedClip = dupId;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Duplicate selected clip (Ctrl+D)");
    sameLineWrap(62.0f);
    if (ui::Button("Delete")) {
        const int id = editor.selectedClip;
        editor.edit([&](Project& p) { return ops::deleteClip(p, id); });
        editor.selectedClip = 0;
    }
    sameLineWrap(66.0f);
    if (ui::Button("Ripple")) {
        const int id = editor.selectedClip;
        editor.edit([&](Project& p) { return ops::rippleDeleteClip(p, id); });
        editor.selectedClip = 0;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ripple Delete (Shift+Del)");
    ImGui::EndDisabled();

    const Track* selTrack = nullptr;
    const Clip* selClip = ops::findClip(proj, editor.selectedClip, &selTrack);
    const MediaAsset* selAsset = (selClip && !selClip->isText) ? proj.findAsset(selClip->assetId) : nullptr;
    const bool canExtract =
        selClip && selTrack && selTrack->kind == TrackKind::Video && selAsset && selAsset->hasAudio;
    sameLineWrap(105.0f);
    ImGui::BeginDisabled(!canExtract);
    if (ui::Button("Extract Audio", ImVec2(0, 28), canExtract)) {
        const int cid = editor.selectedClip;
        int newAudioId = 0;
        editor.edit([&](Project& p) {
            newAudioId = ops::extractAudio(p, cid);
            return newAudioId != 0;
        });
        if (newAudioId != 0) editor.selectedClip = newAudioId;
    }
    ImGui::EndDisabled();

    sameLineWrap(64.0f);
    if (ui::Button("+ Text", ImVec2(0, 28))) {
        const Time t = playback.playhead();
        int newText = 0;
        editor.edit([&](Project& p) {
            newText = ops::addTextClip(p, 0, t);
            return newText != 0;
        });
        if (newText != 0) editor.selectedClip = newText;
    }

    sameLineWrap(56.0f);
    ImGui::BeginDisabled(!editor.canUndo());
    if (ui::Button("Undo")) editor.undo();
    ImGui::EndDisabled();
    sameLineWrap(56.0f);
    ImGui::BeginDisabled(!editor.canRedo());
    if (ui::Button("Redo")) editor.redo();
    ImGui::EndDisabled();

    sameLineWrap(80.0f);
    if (ui::Button("+V Track")) {
        editor.edit([](Project& p) { return ops::addTrack(p, TrackKind::Video) != 0; });
    }
    sameLineWrap(80.0f);
    if (ui::Button("+A Track")) {
        editor.edit([](Project& p) { return ops::addTrack(p, TrackKind::Audio) != 0; });
    }

    sameLineWrap(82.0f);
    ui::ToggleSwitch("Snap", &view.snapEnabled);

    sameLineWrap(36.0f);
    if (ui::Button("<<", ImVec2(32, 28))) view.scrollX = std::max(0.0f, view.scrollX - 240.0f);
    sameLineWrap(36.0f);
    if (ui::Button(">>", ImVec2(32, 28))) view.scrollX += 240.0f;
    sameLineWrap(44.0f);
    bool fitRequested = ui::Button("Fit", ImVec2(40, 28));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fit entire timeline in view");

    sameLineWrap(180.0f);
    if (ui::Button("-", ImVec2(26, 28))) {
        view.pixelsPerSecond = std::clamp(view.pixelsPerSecond * 0.8f, 10.0f, 400.0f);
    }
    ImGui::SameLine();
    ui::SliderFloat("##zoom", &view.pixelsPerSecond, 10.0f, 400.0f, "%.0f px/s", 110.0f);
    ImGui::SameLine();
    if (ui::Button("+", ImVec2(26, 28))) {
        view.pixelsPerSecond = std::clamp(view.pixelsPerSecond * 1.25f, 10.0f, 400.0f);
    }

    ImGui::Separator();

    constexpr float kHeaderW = 186.0f;
    constexpr float kRulerH = 26.0f;
    constexpr float kTrackH = 58.0f;
    constexpr float kTrackGap = 4.0f;
    constexpr float kScrollbarH = 16.0f;

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float viewW = std::max(100.0f, avail.x - kHeaderW - 6.0f);
    const float viewH = std::max(80.0f, avail.y);
    const float tracksAreaH = std::max(40.0f, viewH - kRulerH - kScrollbarH);

    if (fitRequested) {
        const double durSec = std::max(5.0, toSeconds(proj.duration()) * 1.1);
        view.pixelsPerSecond =
            std::clamp(static_cast<float>(viewW / durSec), 10.0f, 400.0f);
        view.scrollX = 0.0f;
    }

    const float pps = view.pixelsPerSecond;
    const double totalSec =
        std::max({30.0, toSeconds(proj.duration()) + 20.0, toSeconds(playback.playhead()) + 15.0});
    const float totalContentW = std::max(viewW, static_cast<float>(totalSec * pps));
    const float totalTracksH =
        static_cast<float>(proj.tracks.size()) * (kTrackH + kTrackGap) + 8.0f;
    const float maxScrollX = std::max(0.0f, totalContentW - viewW);
    const float maxScrollY = std::max(0.0f, totalTracksH - tracksAreaH);

    // Auto-follow playhead while playing
    if (playback.isPlaying() && view.autoFollow &&
        view.dragKind == TimelineViewState::DragKind::None) {
        const float playheadPx = static_cast<float>(toSeconds(playback.playhead()) * pps);
        if (playheadPx > view.scrollX + viewW * 0.9f) {
            view.scrollX = playheadPx - viewW * 0.2f;
        } else if (playheadPx < view.scrollX) {
            view.scrollX = std::max(0.0f, playheadPx - 40.0f);
        }
    }

    view.scrollX = std::clamp(view.scrollX, 0.0f, maxScrollX);
    view.scrollY = std::clamp(view.scrollY, 0.0f, maxScrollY);

    // Left column: Track headers (synced with view.scrollY)
    ImGui::BeginChild("TrackHeaders", ImVec2(kHeaderW, viewH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
    const ImVec2 hdrOrigin = ImGui::GetCursorScreenPos();
    ImDrawList* hdl = ImGui::GetWindowDrawList();

    hdl->AddRectFilled(hdrOrigin, ImVec2(hdrOrigin.x + kHeaderW - 4.0f, hdrOrigin.y + kRulerH),
                       IM_COL32(22, 24, 30, 255));
    hdl->AddText(ImVec2(hdrOrigin.x + 8.0f, hdrOrigin.y + 5.0f), IM_COL32(150, 158, 175, 255),
                 "Tracks");

    hdl->PushClipRect(ImVec2(hdrOrigin.x, hdrOrigin.y + kRulerH),
                      ImVec2(hdrOrigin.x + kHeaderW, hdrOrigin.y + kRulerH + tracksAreaH), true);

    int removeTrackId = 0;
    int moveTrackId = 0;
    int moveTrackDelta = 0;
    for (std::size_t i = 0; i < proj.tracks.size(); ++i) {
        const Track& tr = proj.tracks[i];
        ImGui::PushID(tr.id);
        const float y0 =
            hdrOrigin.y + kRulerH + static_cast<float>(i) * (kTrackH + kTrackGap) - view.scrollY;
        const float y1 = y0 + kTrackH;

        hdl->AddRectFilled(ImVec2(hdrOrigin.x, y0), ImVec2(hdrOrigin.x + kHeaderW - 4.0f, y1),
                           tr.kind == TrackKind::Video ? IM_COL32(34, 40, 52, 255)
                                                       : IM_COL32(30, 48, 42, 255),
                           4.0f);
        hdl->AddText(ImVec2(hdrOrigin.x + 8.0f, y0 + 6.0f), IM_COL32(235, 240, 248, 255),
                     tr.name.c_str());

        ImGui::SetCursorScreenPos(ImVec2(hdrOrigin.x + 6.0f, y0 + 27.0f));
        const int tid = tr.id;
        if (ui::Button(tr.muted ? "M*" : "Mute", ImVec2(42, 22), tr.muted)) {
            editor.edit([tid](Project& p) {
                if (Track* t = p.findTrack(tid)) {
                    t->muted = !t->muted;
                    return true;
                }
                return false;
            });
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr.muted ? "Unmute track" : "Mute track");

        if (tr.kind == TrackKind::Video) {
            ImGui::SameLine(0.0f, 3.0f);
            if (ui::Button(tr.hidden ? "H*" : "Hide", ImVec2(42, 22), tr.hidden)) {
                editor.edit([tid](Project& p) {
                    if (Track* t = p.findTrack(tid)) {
                        t->hidden = !t->hidden;
                        return true;
                    }
                    return false;
                });
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr.hidden ? "Show track" : "Hide track");
        }

        ImGui::SameLine(0.0f, 3.0f);
        if (ui::Button("^", ImVec2(22, 22))) {
            moveTrackId = tid;
            moveTrackDelta = -1;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move track up");
        ImGui::SameLine(0.0f, 2.0f);
        if (ui::Button("v", ImVec2(22, 22))) {
            moveTrackId = tid;
            moveTrackDelta = +1;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move track down");

        ImGui::SameLine(0.0f, 3.0f);
        if (ui::CircleActionButton("##deltrack", true, 22.0f)) removeTrackId = tid;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete track");

        ImGui::PopID();
    }
    hdl->PopClipRect();
    ImGui::EndChild();

    if (moveTrackId != 0) {
        editor.edit(
            [moveTrackId, moveTrackDelta](Project& p) {
                return ops::moveTrack(p, moveTrackId, moveTrackDelta);
            });
    }
    if (removeTrackId != 0) {
        editor.edit([removeTrackId](Project& p) { return ops::removeTrack(p, removeTrackId); });
    }

    ImGui::SameLine(0.0f, 6.0f);

    // Right column: Custom panned/zoomed timeline viewport
    ImGui::BeginChild("TimelineViewport", ImVec2(viewW, viewH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 vpOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 vpMax(vpOrigin.x + viewW, vpOrigin.y + viewH);
    const ImVec2 tracksMin(vpOrigin.x, vpOrigin.y + kRulerH);
    const ImVec2 tracksMax(vpOrigin.x + viewW, vpOrigin.y + kRulerH + tracksAreaH);
    const ImVec2 sbMin(vpOrigin.x, tracksMax.y);
    const ImVec2 sbMax(vpOrigin.x + viewW, vpMax.y);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool vpHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    const auto xToTime = [&](float screenX) -> Time {
        return std::max<Time>(0, fromSeconds((screenX - vpOrigin.x + view.scrollX) / pps));
    };
    const auto timeToX = [&](Time t) -> float {
        return vpOrigin.x + static_cast<float>(toSeconds(t) * pps) - view.scrollX;
    };

    if (vpHovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)) {
        if (io.KeyCtrl && io.MouseWheel != 0.0f) {
            const Time anchorTime = xToTime(mouse.x);
            const float factor = io.MouseWheel > 0.0f ? 1.18f : (1.0f / 1.18f);
            view.pixelsPerSecond = std::clamp(view.pixelsPerSecond * factor, 10.0f, 400.0f);
            const float newX = static_cast<float>(toSeconds(anchorTime) * view.pixelsPerSecond);
            view.scrollX = std::clamp(
                newX - (mouse.x - vpOrigin.x), 0.0f,
                std::max(0.0f, static_cast<float>(totalSec * view.pixelsPerSecond) - viewW));
        } else if (io.KeyShift) {
            view.scrollY = std::clamp(view.scrollY - io.MouseWheel * 36.0f, 0.0f, maxScrollY);
        } else {
            const float delta = (io.MouseWheel != 0.0f ? -io.MouseWheel : -io.MouseWheelH) * 90.0f;
            view.scrollX = std::clamp(view.scrollX + delta, 0.0f, maxScrollX);
        }
    }

    // 1. Draw Ruler
    dl->AddRectFilled(vpOrigin, ImVec2(vpOrigin.x + viewW, vpOrigin.y + kRulerH),
                      IM_COL32(24, 26, 32, 255));
    double majorStep = 1.0;
    if (pps < 25.0f)
        majorStep = 10.0;
    else if (pps < 60.0f)
        majorStep = 5.0;
    else if (pps < 140.0f)
        majorStep = 2.0;

    const double firstTick = std::floor((view.scrollX / pps) / majorStep) * majorStep;
    const double lastTick = (view.scrollX + viewW) / pps + majorStep;
    dl->PushClipRect(vpOrigin, ImVec2(vpOrigin.x + viewW, vpOrigin.y + kRulerH), true);
    for (double s = std::max(0.0, firstTick); s <= lastTick; s += majorStep) {
        const float x = vpOrigin.x + static_cast<float>(s * pps) - view.scrollX;
        dl->AddLine(ImVec2(x, vpOrigin.y + kRulerH - 10.0f), ImVec2(x, vpOrigin.y + kRulerH),
                    IM_COL32(125, 134, 152, 255));
        char label[32];
        const int ti = static_cast<int>(std::lround(s));
        std::snprintf(label, sizeof label, "%02d:%02d", ti / 60, ti % 60);
        dl->AddText(ImVec2(x + 3.0f, vpOrigin.y + 4.0f), IM_COL32(180, 188, 205, 255), label);
    }
    dl->PopClipRect();

    // 2. Draw Track Lanes & Clips
    int hoveredClipId = 0;
    int hoveredTrackId = 0;
    float hoveredClipY0 = 0.0f;
    float hoveredClipY1 = 0.0f;
    TimelineViewState::DragKind hoverEdge = TimelineViewState::DragKind::MoveClip;

    dl->PushClipRect(tracksMin, tracksMax, true);
    for (std::size_t i = 0; i < proj.tracks.size(); ++i) {
        const Track& tr = proj.tracks[i];
        const float y0 =
            tracksMin.y + static_cast<float>(i) * (kTrackH + kTrackGap) - view.scrollY;
        const float y1 = y0 + kTrackH;

        if (y1 >= tracksMin.y && y0 <= tracksMax.y) {
            dl->AddRectFilled(ImVec2(tracksMin.x, y0), ImVec2(tracksMax.x, y1),
                              IM_COL32(20, 22, 28, 255));
            dl->AddLine(ImVec2(tracksMin.x, y1), ImVec2(tracksMax.x, y1),
                        IM_COL32(36, 40, 50, 255));
        }

        if (mouse.x >= tracksMin.x && mouse.x <= tracksMax.x &&
            mouse.y >= std::max(y0, tracksMin.y) && mouse.y < std::min(y1, tracksMax.y)) {
            hoveredTrackId = tr.id;
        }

        const ImRect laneRect(ImVec2(tracksMin.x, std::max(y0, tracksMin.y)),
                              ImVec2(tracksMax.x, std::min(y1, tracksMax.y)));
        if (laneRect.Min.y < laneRect.Max.y &&
            ImGui::BeginDragDropTargetCustom(laneRect, ImGui::GetID(&tr))) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VCUT_ASSET")) {
                const int assetId = *static_cast<const int*>(payload->Data);
                const Time dropTime = xToTime(mouse.x);
                const int tid = tr.id;
                int created = 0;
                editor.edit([&](Project& p) {
                    created = ops::addClip(p, tid, assetId, dropTime);
                    return created != 0;
                });
                if (created != 0) editor.selectedClip = created;
            }
            ImGui::EndDragDropTarget();
        }

        for (const Clip& c : tr.clips) {
            const float x0 = timeToX(c.start);
            const float x1 = std::max(x0 + 6.0f, timeToX(c.end()));
            if (x1 < tracksMin.x || x0 > tracksMax.x) continue;

            const bool selected = (editor.selectedClip == c.id);
            ImU32 fillCol = 0;
            if (c.isText) {
                fillCol = selected ? IM_COL32(146, 98, 225, 255) : IM_COL32(106, 70, 175, 240);
            } else if (tr.kind == TrackKind::Video) {
                fillCol = selected ? IM_COL32(58, 134, 220, 255) : IM_COL32(42, 98, 168, 240);
            } else {
                fillCol = selected ? IM_COL32(46, 162, 114, 255) : IM_COL32(32, 118, 84, 240);
            }
            const ImU32 borderCol =
                selected ? IM_COL32(255, 214, 92, 255) : IM_COL32(90, 140, 200, 180);

            dl->AddRectFilled(ImVec2(x0, y0 + 2.0f), ImVec2(x1, y1 - 2.0f), fillCol, 5.0f);

            const MediaAsset* asset = c.isText ? nullptr : proj.findAsset(c.assetId);
            if (asset && asset->hasAudio && !tr.muted && !c.muted) {
                if (const AudioWaveform* wf = waveforms.get(*asset)) {
                    const float wy0 = tr.kind == TrackKind::Audio ? (y0 + 18.0f) : (y1 - 22.0f);
                    const float wy1 = y1 - 3.0f;
                    const ImU32 wfCol = tr.kind == TrackKind::Audio
                                            ? IM_COL32(160, 245, 200, 195)
                                            : IM_COL32(145, 210, 255, 165);
                    drawClipWaveform(dl, c, *wf, x0, x1, wy0, wy1, tracksMin.x, tracksMax.x, wfCol);
                }
            }

            // Transition in wedge
            if (c.transitionIn != TransitionKind::None && c.transitionInDur > 0) {
                const float tw = std::min(
                    x1 - x0, static_cast<float>(toSeconds(c.transitionInDur) * pps));
                if (tw > 2.0f) {
                    dl->AddTriangleFilled(ImVec2(x0, y0 + 2.0f), ImVec2(x0 + tw, y0 + 2.0f),
                                          ImVec2(x0, y1 - 2.0f), IM_COL32(0, 230, 245, 130));
                }
            }

            // Fade-in / Fade-out visual envelopes
            const Time visFadeIn = std::max(c.fadeIn, c.videoFadeIn);
            const Time visFadeOut = std::max(c.fadeOut, c.videoFadeOut);
            if (visFadeIn > 0) {
                const float fx = std::min(x1, x0 + static_cast<float>(toSeconds(visFadeIn) * pps));
                dl->AddLine(ImVec2(x0, y1 - 3.0f), ImVec2(fx, y0 + 3.0f),
                            IM_COL32(255, 235, 120, 210), 1.8f);
            }
            if (visFadeOut > 0) {
                const float fx = std::max(x0, x1 - static_cast<float>(toSeconds(visFadeOut) * pps));
                dl->AddLine(ImVec2(fx, y0 + 3.0f), ImVec2(x1, y1 - 3.0f),
                            IM_COL32(255, 235, 120, 210), 1.8f);
            }

            dl->AddRect(ImVec2(x0, y0 + 2.0f), ImVec2(x1, y1 - 2.0f), borderCol, 5.0f, 0,
                        selected ? 2.0f : 1.0f);

            dl->AddRectFilled(ImVec2(x0, y0 + 2.0f), ImVec2(x0 + 5.0f, y1 - 2.0f),
                              IM_COL32(255, 255, 255, 75), 4.0f);
            dl->AddRectFilled(ImVec2(x1 - 5.0f, y0 + 2.0f), ImVec2(x1, y1 - 2.0f),
                              IM_COL32(255, 255, 255, 75), 4.0f);

            std::string titleStr = c.isText ? ("T: " + c.text.content)
                                            : (asset ? asset->name : std::string("Clip"));
            for (char& ch : titleStr) {
                if (ch == '\n' || ch == '\r') ch = ' ';
            }
            char sub[96];
            const std::string durStr = formatTime(c.duration());
            if (c.muted && !c.isText && asset && asset->hasAudio) {
                std::snprintf(sub, sizeof sub, "%s [Muted]", durStr.c_str());
            } else if (std::fabs(c.speed - 1.0f) > 0.02f) {
                std::snprintf(sub, sizeof sub, "%s (%.2fx)", durStr.c_str(), c.speed);
            } else if (!c.isText && std::fabs(c.volume - 1.0f) > 0.01f) {
                std::snprintf(sub, sizeof sub, "%s (%.0f%%)", durStr.c_str(), c.volume * 100.0f);
            } else {
                std::snprintf(sub, sizeof sub, "%s", durStr.c_str());
            }

            dl->PushClipRect(ImVec2(std::max(x0 + 6.0f, tracksMin.x), y0 + 3.0f),
                             ImVec2(std::min(x1 - 6.0f, tracksMax.x), y1 - 3.0f), true);
            dl->AddText(ImVec2(x0 + 8.0f, y0 + 5.0f), IM_COL32(248, 250, 255, 255),
                        titleStr.c_str());
            dl->AddText(ImVec2(x0 + 8.0f, y0 + 23.0f), IM_COL32(210, 226, 245, 215), sub);
            dl->PopClipRect();

            if (mouse.x >= x0 && mouse.x <= x1 && mouse.y >= std::max(y0 + 2.0f, tracksMin.y) &&
                mouse.y <= std::min(y1 - 2.0f, tracksMax.y)) {
                hoveredClipId = c.id;
                hoveredClipY0 = y0 + 2.0f;
                hoveredClipY1 = y1 - 2.0f;
                constexpr float kEdgePx = 8.0f;
                if (mouse.x - x0 <= kEdgePx)
                    hoverEdge = TimelineViewState::DragKind::TrimLeft;
                else if (x1 - mouse.x <= kEdgePx)
                    hoverEdge = TimelineViewState::DragKind::TrimRight;
                else
                    hoverEdge = TimelineViewState::DragKind::MoveClip;
            }
        }
    }

    // Razor blade guide line when hovering a clip
    if (view.tool == TimelineTool::Razor && vpHovered && hoveredClipId != 0 &&
        mouse.y >= tracksMin.y && mouse.y <= tracksMax.y) {
        dl->AddLine(ImVec2(mouse.x, hoveredClipY0), ImVec2(mouse.x, hoveredClipY1),
                    IM_COL32(0, 235, 245, 235), 2.0f);
    }
    dl->PopClipRect();

    // 3. Draw Interactive Horizontal Scrollbar at the bottom
    dl->AddRectFilled(sbMin, sbMax, IM_COL32(16, 18, 23, 255));
    const float thumbRatio = std::clamp(viewW / totalContentW, 0.05f, 1.0f);
    const float thumbW = std::max(30.0f, viewW * thumbRatio);
    const float scrollRatio = maxScrollX > 0.0f ? (view.scrollX / maxScrollX) : 0.0f;
    const float thumbX = sbMin.x + scrollRatio * (viewW - thumbW);
    const bool inScrollbar =
        (mouse.x >= sbMin.x && mouse.x <= sbMax.x && mouse.y >= sbMin.y && mouse.y <= sbMax.y);
    dl->AddRectFilled(ImVec2(thumbX, sbMin.y + 3.0f), ImVec2(thumbX + thumbW, sbMax.y - 3.0f),
                      (inScrollbar || view.dragKind == TimelineViewState::DragKind::Scrollbar)
                          ? IM_COL32(110, 145, 195, 255)
                          : IM_COL32(75, 85, 105, 255),
                      5.0f);

    // 4. Cursor feedback
    if (vpHovered) {
        if (view.dragKind == TimelineViewState::DragKind::Pan ||
            view.tool == TimelineTool::Hand) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else if (view.tool == TimelineTool::Razor && hoveredClipId != 0) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        } else if (hoveredClipId != 0 && view.dragKind == TimelineViewState::DragKind::None) {
            ImGui::SetMouseCursor(hoverEdge != TimelineViewState::DragKind::MoveClip
                                      ? ImGuiMouseCursor_ResizeEW
                                      : ImGuiMouseCursor_Hand);
        }
    }

    // 5. Right-click context menu on a clip
    if (vpHovered && hoveredClipId != 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        editor.selectedClip = hoveredClipId;
        ImGui::OpenPopup("ClipContextMenu");
    }
    if (ImGui::BeginPopup("ClipContextMenu")) {
        const Track* ctr = nullptr;
        const Clip* cc = ops::findClip(proj, editor.selectedClip, &ctr);
        const MediaAsset* ca = (cc && !cc->isText) ? proj.findAsset(cc->assetId) : nullptr;
        if (cc) {
            const int cid = cc->id;
            if (ImGui::MenuItem("Split at Playhead", "S")) {
                editor.edit(
                    [&](Project& p) { return ops::splitClip(p, cid, playback.playhead()) != 0; });
            }
            if (ImGui::MenuItem("Duplicate Clip", "Ctrl+D")) {
                int dupId = 0;
                editor.edit([&](Project& p) {
                    dupId = ops::duplicateClip(p, cid);
                    return dupId != 0;
                });
                if (dupId != 0) editor.selectedClip = dupId;
            }
            if (ImGui::MenuItem("Copy Clip", "Ctrl+C")) {
                view.clipboardClip = *cc;
            }
            if (ctr && ctr->kind == TrackKind::Video && ca && ca->hasAudio) {
                if (ImGui::MenuItem("Extract Audio to Track")) {
                    int aid = 0;
                    editor.edit([&](Project& p) {
                        aid = ops::extractAudio(p, cid);
                        return aid != 0;
                    });
                    if (aid != 0) editor.selectedClip = aid;
                }
            }
            if (ca && ca->hasAudio) {
                const bool m = cc->muted;
                if (ImGui::MenuItem(m ? "Unmute Clip Audio" : "Mute Clip Audio")) {
                    editor.edit([cid, m](Project& p) {
                        if (auto r = ops::findClip(p, cid)) {
                            r.clip->muted = !m;
                            return true;
                        }
                        return false;
                    });
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Clip", "Del")) {
                editor.edit([&](Project& p) { return ops::deleteClip(p, cid); });
                editor.selectedClip = 0;
            }
            if (ImGui::MenuItem("Ripple Delete", "Shift+Del")) {
                editor.edit([&](Project& p) { return ops::rippleDeleteClip(p, cid); });
                editor.selectedClip = 0;
            }
        }
        ImGui::EndPopup();
    }

    // 6. Start mouse interactions
    const float playheadScreenX = timeToX(playback.playhead());
    const bool onPlayheadHandle =
        (std::fabs(mouse.x - playheadScreenX) <= 8.0f && mouse.y >= vpOrigin.y &&
         mouse.y <= vpOrigin.y + kRulerH + 10.0f);
    const bool inRuler = (mouse.y >= vpOrigin.y && mouse.y <= vpOrigin.y + kRulerH);

    if (vpHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
                      (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hoveredClipId == 0))) {
        view.dragKind = TimelineViewState::DragKind::Pan;
        view.dragStartMouseX = mouse.x;
        view.dragStartMouseY = mouse.y;
        view.dragStartScrollX = view.scrollX;
        view.dragStartScrollY = view.scrollY;
        view.panMoved = true;
    }

    if (vpHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (inScrollbar) {
            view.dragKind = TimelineViewState::DragKind::Scrollbar;
            if (mouse.x < thumbX || mouse.x > thumbX + thumbW) {
                const float ratio =
                    std::clamp((mouse.x - sbMin.x - thumbW * 0.5f) / std::max(1.0f, viewW - thumbW),
                               0.0f, 1.0f);
                view.scrollX = ratio * maxScrollX;
            }
            view.dragStartMouseX = mouse.x;
            view.dragStartScrollX = view.scrollX;
        } else if (inRuler || onPlayheadHandle) {
            view.dragKind = TimelineViewState::DragKind::Scrub;
            playback.pause();
            playback.seek(xToTime(mouse.x));
        } else if (view.tool == TimelineTool::Hand || io.KeyAlt) {
            view.dragKind = TimelineViewState::DragKind::Pan;
            view.dragStartMouseX = mouse.x;
            view.dragStartMouseY = mouse.y;
            view.dragStartScrollX = view.scrollX;
            view.dragStartScrollY = view.scrollY;
            view.panMoved = false;
        } else if (view.tool == TimelineTool::Razor && hoveredClipId != 0) {
            Time cutT = xToTime(mouse.x);
            if (view.snapEnabled) {
                bool snapped = false;
                const Time snapThresh = fromSeconds(8.0 / pps);
                const Time s = ops::snapTime(proj, cutT, snapThresh, playback.playhead(),
                                             hoveredClipId, &snapped);
                if (snapped) cutT = s;
            }
            const int cid = hoveredClipId;
            int newRight = 0;
            editor.edit([&](Project& p) {
                newRight = ops::splitClip(p, cid, cutT);
                return newRight != 0;
            });
            editor.selectedClip = newRight != 0 ? newRight : cid;
        } else if (hoveredClipId != 0) {
            editor.selectedClip = hoveredClipId;
            editor.selectedAsset = 0;
            const Track* tr = nullptr;
            if (const Clip* c = ops::findClip(proj, hoveredClipId, &tr)) {
                view.dragKind = hoverEdge;
                view.dragClipId = c->id;
                view.dragOriginTrackId = tr->id;
                view.dragOriginStart = c->start;
                view.dragOriginEnd = c->end();
                view.dragGrabOffset = xToTime(mouse.x) - c->start;
                view.dragStartMouseX = mouse.x;
                editor.beginGesture();
            }
        } else {
            view.dragKind = TimelineViewState::DragKind::Pan;
            view.dragStartMouseX = mouse.x;
            view.dragStartMouseY = mouse.y;
            view.dragStartScrollX = view.scrollX;
            view.dragStartScrollY = view.scrollY;
            view.panMoved = false;
        }
    }

    // 7. Update active drag
    view.snapLineTime = -1;
    const bool anyDragButtonDown = ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                                   ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                                   ImGui::IsMouseDown(ImGuiMouseButton_Right);

    if (view.dragKind != TimelineViewState::DragKind::None && anyDragButtonDown) {
        const Time snapThresh = view.snapEnabled ? fromSeconds(9.0 / pps) : 0;

        if (view.dragKind == TimelineViewState::DragKind::Scrub) {
            playback.seek(xToTime(mouse.x));
        } else if (view.dragKind == TimelineViewState::DragKind::Pan) {
            const float dx = mouse.x - view.dragStartMouseX;
            const float dy = mouse.y - view.dragStartMouseY;
            if (std::fabs(dx) > 3.0f || std::fabs(dy) > 3.0f) view.panMoved = true;
            if (view.panMoved) {
                view.scrollX = std::clamp(view.dragStartScrollX - dx, 0.0f, maxScrollX);
                view.scrollY = std::clamp(view.dragStartScrollY - dy, 0.0f, maxScrollY);
            }
        } else if (view.dragKind == TimelineViewState::DragKind::Scrollbar) {
            const float dx = mouse.x - view.dragStartMouseX;
            const float trackSpan = std::max(1.0f, viewW - thumbW);
            view.scrollX =
                std::clamp(view.dragStartScrollX + (dx / trackSpan) * maxScrollX, 0.0f, maxScrollX);
        } else if (view.dragKind == TimelineViewState::DragKind::MoveClip) {
            const Time clipLen = view.dragOriginEnd - view.dragOriginStart;
            Time target = std::max<Time>(0, xToTime(mouse.x) - view.dragGrabOffset);
            if (view.snapEnabled) {
                target = ops::snapClipMove(proj, target, clipLen, snapThresh, playback.playhead(),
                                           view.dragClipId, &view.snapLineTime);
            }
            const int dstTrack = hoveredTrackId != 0 ? hoveredTrackId : view.dragOriginTrackId;
            const int cid = view.dragClipId;
            editor.gestureApply(
                [&](Project& p) { return ops::moveClip(p, cid, dstTrack, target); });
        } else if (view.dragKind == TimelineViewState::DragKind::TrimLeft) {
            Time target = xToTime(mouse.x);
            bool snapped = false;
            if (view.snapEnabled) {
                target = ops::snapTime(proj, target, snapThresh, playback.playhead(),
                                       view.dragClipId, &snapped);
                if (snapped) view.snapLineTime = target;
            }
            const int cid = view.dragClipId;
            editor.gestureApply([&](Project& p) { return ops::trimLeft(p, cid, target); });
        } else if (view.dragKind == TimelineViewState::DragKind::TrimRight) {
            Time target = xToTime(mouse.x);
            bool snapped = false;
            if (view.snapEnabled) {
                target = ops::snapTime(proj, target, snapThresh, playback.playhead(),
                                       view.dragClipId, &snapped);
                if (snapped) view.snapLineTime = target;
            }
            const int cid = view.dragClipId;
            editor.gestureApply([&](Project& p) { return ops::trimRight(p, cid, target); });
        }
    }

    if (view.dragKind != TimelineViewState::DragKind::None && !anyDragButtonDown) {
        if (view.dragKind == TimelineViewState::DragKind::Pan && !view.panMoved &&
            view.tool != TimelineTool::Hand) {
            editor.selectedClip = 0;
            playback.pause();
            playback.seek(xToTime(mouse.x));
        }
        if (editor.inGesture()) editor.endGesture();
        view.dragKind = TimelineViewState::DragKind::None;
        view.dragClipId = 0;
    }

    // 8. Draw Snap line & Playhead
    dl->PushClipRect(vpOrigin, tracksMax, true);
    if (view.snapLineTime >= 0) {
        const float sx = timeToX(view.snapLineTime);
        dl->AddLine(ImVec2(sx, vpOrigin.y), ImVec2(sx, tracksMax.y),
                    IM_COL32(255, 215, 64, 230), 1.5f);
    }

    const float px = timeToX(playback.playhead());
    dl->AddTriangleFilled(ImVec2(px - 7.0f, vpOrigin.y), ImVec2(px + 7.0f, vpOrigin.y),
                          ImVec2(px, vpOrigin.y + 12.0f), IM_COL32(255, 72, 72, 255));
    dl->AddLine(ImVec2(px, vpOrigin.y), ImVec2(px, tracksMax.y), IM_COL32(255, 72, 72, 255),
                2.0f);
    dl->PopClipRect();

    ImGui::Dummy(ImVec2(viewW, viewH));
    ImGui::EndChild();
    ImGui::End();
}

void drawExportDialog(const Project& project, ExportDialogState& state, ExportJob& job,
                      const UiCallbacks& cb) {
    if (state.open) {
        ImGui::OpenPopup("Export Video");
        state.open = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(490, 0), ImVec2(660, 580));

    if (ImGui::BeginPopupModal("Export Video", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        struct ResPreset {
            const char* label;
            int w;
            int h;
        };
        static constexpr ResPreset kResPresets[] = {
            {"4K UHD (3840x2160)", 3840, 2160},
            {"1440p QHD (2560x1440)", 2560, 1440},
            {"1080p FHD (1920x1080)", 1920, 1080},
            {"720p HD (1280x720)", 1280, 720},
            {"480p SD (854x480)", 854, 480},
            {"4K Vertical (2160x3840)", 2160, 3840},
            {"Match Project Canvas", 0, 0},
            {"Custom Resolution...", -1, -1},
        };
        static constexpr int kPresetCount = static_cast<int>(std::size(kResPresets));
        static constexpr int kCustomIdx = kPresetCount - 1;
        static constexpr int kMatchProjectIdx = kPresetCount - 2;
        static constexpr const char* kFpsItems[] = {"24 fps", "30 fps", "60 fps"};
        static constexpr int kFpsValues[] = {24, 30, 60};
        static constexpr const char* kQualityItems[] = {
            "High Quality (CRF 18)",
            "Balanced (CRF 21)",
            "Compact File (CRF 25)",
        };
        static constexpr int kQualityCrfs[] = {18, 21, 25};
        static constexpr const char* kEncoderItems[] = {
            "Auto (GPU NVENC / AMF / QSV -> CPU)",
            "CPU libx264 (Maximum Compatibility)",
            "NVIDIA NVENC (h264_nvenc)",
            "AMD AMF (h264_amf)",
            "Intel Quick Sync (h264_qsv)",
        };
        static constexpr EncoderBackend kEncoderValues[] = {
            EncoderBackend::Auto,
            EncoderBackend::CpuX264,
            EncoderBackend::Nvenc,
            EncoderBackend::Amf,
            EncoderBackend::Qsv,
        };

        state.resolutionPreset = std::clamp(state.resolutionPreset, 0, kPresetCount - 1);
        state.fpsPreset = std::clamp(state.fpsPreset, 0, 2);
        state.qualityPreset = std::clamp(state.qualityPreset, 0, 2);
        state.encoderPreset = std::clamp(state.encoderPreset, 0, 4);

        ImGui::BeginDisabled(job.isRunning());
        if (ImGui::BeginCombo("Resolution", kResPresets[state.resolutionPreset].label)) {
            for (int i = 0; i < kPresetCount; ++i) {
                const bool selected = (i == state.resolutionPreset);
                if (ImGui::Selectable(kResPresets[i].label, selected)) {
                    state.resolutionPreset = i;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (state.resolutionPreset == kCustomIdx) {
            ImGui::SetNextItemWidth(130.0f);
            ImGui::InputInt("Width", &state.customWidth, 64, 256);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(130.0f);
            ImGui::InputInt("Height", &state.customHeight, 64, 256);
            state.customWidth = std::clamp(state.customWidth, 64, 7680);
            state.customHeight = std::clamp(state.customHeight, 64, 7680);
        }

        ImGui::Combo("Frame Rate", &state.fpsPreset, kFpsItems, 3);
        ImGui::Combo("Quality", &state.qualityPreset, kQualityItems, 3);
        ImGui::Combo("Encoder", &state.encoderPreset, kEncoderItems, 5);

        ImGui::InputText("Output MP4", state.outputPath, sizeof state.outputPath);
        ImGui::SameLine();
        if (ui::Button("Browse...", ImVec2(0, 26))) {
            if (cb.onBrowseExportPath) cb.onBrowseExportPath();
        }
        ImGui::EndDisabled();

        int outW = kResPresets[state.resolutionPreset].w;
        int outH = kResPresets[state.resolutionPreset].h;
        if (state.resolutionPreset == kMatchProjectIdx) {
            outW = project.settings.width;
            outH = project.settings.height;
        } else if (state.resolutionPreset == kCustomIdx) {
            outW = state.customWidth;
            outH = state.customHeight;
        }
        outW = (std::max(2, outW) + 1) & ~1;
        outH = (std::max(2, outH) + 1) & ~1;
        const int outFps = kFpsValues[state.fpsPreset];

        ImGui::Spacing();
        ImGui::TextDisabled("MP4 (H.264 + AAC)  |  %dx%d @ %d fps  |  Duration: %s", outW, outH,
                            outFps, formatTime(project.duration()).c_str());
        if (static_cast<std::int64_t>(outW) * outH >= 3840LL * 2160LL &&
            state.encoderPreset == 1) {
            ImGui::TextDisabled("Tip: Switch Encoder to 'Auto' to use GPU acceleration if available.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (job.isRunning()) {
            ImGui::ProgressBar(job.progress(), ImVec2(-1, 22));
            if (ui::Button("Cancel Export", ImVec2(130, 28))) job.cancel();
        } else {
            if (job.isDone()) {
                if (job.succeeded()) {
                    const std::string enc = job.usedEncoder();
                    ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.50f, 1.0f),
                                       "Export finished (%s): %s",
                                       enc.empty() ? "h264" : enc.c_str(), state.outputPath);
                } else {
                    ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.40f, 1.0f), "Error: %s",
                                       job.error().c_str());
                }
            }

            if (ui::PrimaryButton("Start Export", ImVec2(145, 32))) {
                ExportOptions opts;
                opts.outputPath = state.outputPath;
                opts.width = outW;
                opts.height = outH;
                opts.fps = outFps;
                opts.sampleRate = project.settings.sampleRate;
                opts.crf = kQualityCrfs[state.qualityPreset];
                opts.encoder = kEncoderValues[state.encoderPreset];
                job.start(project, std::move(opts));
            }
            ImGui::SameLine();
            if (ui::Button("Close", ImVec2(100, 32))) {
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

}  // namespace vcut
