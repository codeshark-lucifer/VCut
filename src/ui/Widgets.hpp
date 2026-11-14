#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vcut::ui {

// Brand & surface palette (CapCut-inspired dark studio theme)
inline constexpr ImU32 kColAccent = IM_COL32(0, 204, 218, 255);
inline constexpr ImU32 kColAccentHover = IM_COL32(32, 224, 238, 255);
inline constexpr ImU32 kColAccentActive = IM_COL32(0, 170, 185, 255);
inline constexpr ImU32 kColAccentDim = IM_COL32(0, 204, 218, 55);

inline constexpr ImU32 kColSurface = IM_COL32(34, 38, 50, 255);
inline constexpr ImU32 kColSurfaceHover = IM_COL32(46, 52, 68, 255);
inline constexpr ImU32 kColSurfaceActive = IM_COL32(28, 32, 42, 255);
inline constexpr ImU32 kColBorder = IM_COL32(58, 65, 84, 255);
inline constexpr ImU32 kColBorderHover = IM_COL32(88, 100, 128, 255);

inline constexpr ImU32 kColDangerHover = IM_COL32(235, 70, 80, 255);
inline constexpr ImU32 kColText = IM_COL32(240, 244, 250, 255);
inline constexpr ImU32 kColTextDark = IM_COL32(12, 16, 22, 255);
inline constexpr ImU32 kColTextMuted = IM_COL32(148, 158, 178, 255);

// Primary accent button (Cyan-Teal pill/rounded rectangle)
inline bool PrimaryButton(const char* label, ImVec2 size = ImVec2(0, 30)) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    if (size.x <= 0.0f) size.x = labelSize.x + 24.0f;
    if (size.y <= 0.0f) size.y = labelSize.y + 12.0f;

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    ImDrawList* dl = window->DrawList;
    const float rounding = size.y * 0.28f;
    const ImU32 bg = held ? kColAccentActive : (hovered ? kColAccentHover : kColAccent);
    dl->AddRectFilled(bb.Min, bb.Max, bg, rounding);
    dl->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 255, hovered ? 70 : 35), rounding);

    const ImVec2 textPos(bb.Min.x + (size.x - labelSize.x) * 0.5f,
                         bb.Min.y + (size.y - labelSize.y) * 0.5f);
    dl->AddText(textPos, kColTextDark, label, ImGui::FindRenderedTextEnd(label));
    return pressed;
}

// Secondary surface button with subtle border and optional active highlight
inline bool Button(const char* label, ImVec2 size = ImVec2(0, 28), bool active = false) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    if (size.x <= 0.0f) size.x = labelSize.x + 20.0f;
    if (size.y <= 0.0f) size.y = labelSize.y + 10.0f;

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const bool disabled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;

    ImDrawList* dl = window->DrawList;
    const float rounding = 6.0f;
    ImU32 bg = active ? IM_COL32(0, 155, 175, 95)
                      : (held ? kColSurfaceActive : (hovered ? kColSurfaceHover : kColSurface));
    ImU32 border = active ? kColAccent : (hovered ? kColBorderHover : kColBorder);
    ImU32 txtCol = active ? kColAccentHover : kColText;
    if (disabled) {
        bg = IM_COL32(26, 29, 38, 160);
        border = IM_COL32(42, 46, 58, 140);
        txtCol = IM_COL32(105, 112, 128, 160);
    }

    dl->AddRectFilled(bb.Min, bb.Max, bg, rounding);
    dl->AddRect(bb.Min, bb.Max, border, rounding);

    const ImVec2 textPos(bb.Min.x + (size.x - labelSize.x) * 0.5f,
                         bb.Min.y + (size.y - labelSize.y) * 0.5f);
    dl->AddText(textPos, txtCol, label, ImGui::FindRenderedTextEnd(label));
    return pressed;
}

// Modern pill toggle switch (replaces standard square checkbox)
inline bool ToggleSwitch(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float switchW = 34.0f;
    const float switchH = 18.0f;
    const float totalW =
        switchW + (labelSize.x > 0.0f ? 8.0f + labelSize.x : 0.0f);
    const float totalH = std::max(switchH, labelSize.y + 4.0f);

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + totalW, pos.y + totalH));
    ImGui::ItemSize(ImVec2(totalW, totalH));
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) *v = !*v;

    ImDrawList* dl = window->DrawList;
    const float swY = pos.y + (totalH - switchH) * 0.5f;
    const ImRect swRect(ImVec2(pos.x, swY), ImVec2(pos.x + switchW, swY + switchH));
    const float radius = switchH * 0.5f;

    const ImU32 trackCol = *v ? (hovered ? kColAccentHover : kColAccent)
                              : (hovered ? IM_COL32(58, 64, 82, 255) : IM_COL32(42, 47, 60, 255));
    dl->AddRectFilled(swRect.Min, swRect.Max, trackCol, radius);
    dl->AddRect(swRect.Min, swRect.Max,
                *v ? IM_COL32(255, 255, 255, 45) : IM_COL32(72, 80, 100, 200), radius);

    const float knobX = *v ? (swRect.Max.x - radius) : (swRect.Min.x + radius);
    const float knobY = swRect.Min.y + radius;
    dl->AddCircleFilled(ImVec2(knobX, knobY), radius - 2.5f, IM_COL32(250, 252, 255, 255));

    if (labelSize.x > 0.0f) {
        dl->AddText(ImVec2(swRect.Max.x + 8.0f, pos.y + (totalH - labelSize.y) * 0.5f), kColText,
                    label, ImGui::FindRenderedTextEnd(label));
    }
    return pressed;
}

// Sleek filled-track slider with circular thumb knob
inline bool SliderFloat(const char* label, float* v, float vMin, float vMax,
                        const char* format = "%.1f", float width = 0.0f) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const bool showLabel = (label[0] != '#' || label[1] != '#') && labelSize.x > 0.0f;

    char valBuf[64] = {};
    if (format && format[0] != '\0') {
        std::snprintf(valBuf, sizeof valBuf, format, *v);
    }
    const ImVec2 valSize = ImGui::CalcTextSize(valBuf);

    const float availW = ImGui::GetContentRegionAvail().x;
    const float trackW = width > 0.0f   ? width
                         : width < 0.0f ? std::max(60.0f, availW)
                                        : std::max(80.0f, availW - (showLabel ? labelSize.x + 12.0f : 0.0f));
    const float h = 24.0f;
    const float totalW = trackW + (showLabel ? labelSize.x + 10.0f : 0.0f);

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + totalW, pos.y + h));
    const ImRect trackBB(pos, ImVec2(pos.x + trackW, pos.y + h));

    ImGui::ItemSize(ImVec2(totalW, h));
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    ImGui::ButtonBehavior(trackBB, id, &hovered, &held);

    bool changed = false;
    const float pad = 8.0f;
    const float usableW = std::max(1.0f, trackW - pad * 2.0f);
    if (held) {
        const float mouseX = ImGui::GetIO().MousePos.x;
        const float t = std::clamp((mouseX - (trackBB.Min.x + pad)) / usableW, 0.0f, 1.0f);
        const float newVal = vMin + t * (vMax - vMin);
        if (std::fabs(newVal - *v) > 1e-6f) {
            *v = newVal;
            changed = true;
        }
    }

    const float t = (vMax > vMin) ? std::clamp((*v - vMin) / (vMax - vMin), 0.0f, 1.0f) : 0.0f;
    const float cy = pos.y + h * 0.5f;
    const float grooveH = (hovered || held) ? 6.0f : 5.0f;
    const float x0 = trackBB.Min.x + pad;
    const float x1 = trackBB.Max.x - pad;
    const float xFill = x0 + t * usableW;

    ImDrawList* dl = window->DrawList;
    // Groove background
    dl->AddRectFilled(ImVec2(x0, cy - grooveH * 0.5f), ImVec2(x1, cy + grooveH * 0.5f),
                      IM_COL32(36, 40, 52, 255), grooveH * 0.5f);
    // Filled progress
    if (xFill > x0 + 1.0f) {
        dl->AddRectFilled(ImVec2(x0, cy - grooveH * 0.5f), ImVec2(xFill, cy + grooveH * 0.5f),
                          held ? kColAccentHover : kColAccent, grooveH * 0.5f);
    }
    // Circular thumb
    const float knobR = (hovered || held) ? 7.5f : 6.0f;
    if (hovered || held) {
        dl->AddCircleFilled(ImVec2(xFill, cy), knobR + 3.0f, kColAccentDim);
    }
    dl->AddCircleFilled(ImVec2(xFill, cy), knobR, kColAccent);
    dl->AddCircleFilled(ImVec2(xFill, cy), knobR - 2.2f, IM_COL32(250, 252, 255, 255));

    // Value label right-aligned above/on track or tooltip when dragging
    if (valBuf[0] != '\0') {
        if (showLabel) {
            dl->AddText(ImVec2(trackBB.Max.x + 8.0f, pos.y + (h - labelSize.y) * 0.5f), kColText,
                        label, ImGui::FindRenderedTextEnd(label));
            if (hovered || held) {
                ImGui::SetTooltip("%s: %s", label, valBuf);
            }
        } else if (hovered || held) {
            ImGui::SetTooltip("%s", valBuf);
        }
    } else if (showLabel) {
        dl->AddText(ImVec2(trackBB.Max.x + 8.0f, pos.y + (h - labelSize.y) * 0.5f), kColText,
                    label, ImGui::FindRenderedTextEnd(label));
    }
    (void)valSize;
    return changed;
}

// Video player progress scrubber bar
inline bool ScrubberBar(const char* strId, float* sec, float maxSec) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id = window->GetID(strId);
    const float w = std::max(60.0f, ImGui::GetContentRegionAvail().x);
    const float h = 18.0f;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));

    ImGui::ItemSize(ImVec2(w, h));
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    ImGui::ButtonBehavior(bb, id, &hovered, &held);

    const float pad = 7.0f;
    const float usableW = std::max(1.0f, w - pad * 2.0f);
    bool changed = false;
    if (held) {
        const float t =
            std::clamp((ImGui::GetIO().MousePos.x - (bb.Min.x + pad)) / usableW, 0.0f, 1.0f);
        *sec = t * maxSec;
        changed = true;
    }

    const float t = maxSec > 0.0f ? std::clamp(*sec / maxSec, 0.0f, 1.0f) : 0.0f;
    const float cy = pos.y + h * 0.5f;
    const float barH = (hovered || held) ? 6.0f : 4.0f;
    const float x0 = bb.Min.x + pad;
    const float x1 = bb.Max.x - pad;
    const float xFill = x0 + t * usableW;

    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(ImVec2(x0, cy - barH * 0.5f), ImVec2(x1, cy + barH * 0.5f),
                      IM_COL32(38, 42, 54, 255), barH * 0.5f);
    if (xFill > x0 + 1.0f) {
        dl->AddRectFilled(ImVec2(x0, cy - barH * 0.5f), ImVec2(xFill, cy + barH * 0.5f),
                          IM_COL32(255, 76, 88, 255), barH * 0.5f);
    }
    const float knobR = (hovered || held) ? 7.0f : 5.0f;
    dl->AddCircleFilled(ImVec2(xFill, cy), knobR, IM_COL32(255, 86, 96, 255));
    dl->AddCircleFilled(ImVec2(xFill, cy), knobR - 2.0f, IM_COL32(255, 255, 255, 255));
    return changed;
}

enum class TransportIcon { SkipStart, StepBack, Play, Pause, StepFwd, SkipEnd };

// Vector-drawn circular/rounded transport icon button
inline bool TransportButton(const char* strId, TransportIcon icon, bool primary = false) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id = window->GetID(strId);
    const float sz = primary ? 34.0f : 28.0f;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + sz, pos.y + sz));

    ImGui::ItemSize(ImVec2(sz, sz));
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    ImDrawList* dl = window->DrawList;
    const ImVec2 c(pos.x + sz * 0.5f, pos.y + sz * 0.5f);

    if (primary) {
        const ImU32 bg = held ? kColAccentActive : (hovered ? kColAccentHover : kColAccent);
        dl->AddCircleFilled(c, sz * 0.5f, bg);
    } else {
        const ImU32 bg = held ? kColSurfaceActive : (hovered ? kColSurfaceHover : kColSurface);
        dl->AddRectFilled(bb.Min, bb.Max, bg, 7.0f);
        dl->AddRect(bb.Min, bb.Max, hovered ? kColBorderHover : kColBorder, 7.0f);
    }

    const ImU32 fg = primary ? kColTextDark : kColText;
    switch (icon) {
        case TransportIcon::Play:
            dl->AddTriangleFilled(ImVec2(c.x - 4.0f, c.y - 6.0f), ImVec2(c.x - 4.0f, c.y + 6.0f),
                                  ImVec2(c.x + 6.5f, c.y), fg);
            break;
        case TransportIcon::Pause:
            dl->AddRectFilled(ImVec2(c.x - 5.0f, c.y - 6.0f), ImVec2(c.x - 1.5f, c.y + 6.0f), fg,
                              1.0f);
            dl->AddRectFilled(ImVec2(c.x + 1.5f, c.y - 6.0f), ImVec2(c.x + 5.0f, c.y + 6.0f), fg,
                              1.0f);
            break;
        case TransportIcon::StepBack:
            dl->AddTriangleFilled(ImVec2(c.x + 4.0f, c.y - 5.0f), ImVec2(c.x + 4.0f, c.y + 5.0f),
                                  ImVec2(c.x - 4.5f, c.y), fg);
            break;
        case TransportIcon::StepFwd:
            dl->AddTriangleFilled(ImVec2(c.x - 4.0f, c.y - 5.0f), ImVec2(c.x - 4.0f, c.y + 5.0f),
                                  ImVec2(c.x + 4.5f, c.y), fg);
            break;
        case TransportIcon::SkipStart:
            dl->AddRectFilled(ImVec2(c.x - 5.5f, c.y - 5.0f), ImVec2(c.x - 3.5f, c.y + 5.0f), fg);
            dl->AddTriangleFilled(ImVec2(c.x + 5.0f, c.y - 5.0f), ImVec2(c.x + 5.0f, c.y + 5.0f),
                                  ImVec2(c.x - 2.5f, c.y), fg);
            break;
        case TransportIcon::SkipEnd:
            dl->AddTriangleFilled(ImVec2(c.x - 5.0f, c.y - 5.0f), ImVec2(c.x - 5.0f, c.y + 5.0f),
                                  ImVec2(c.x + 2.5f, c.y), fg);
            dl->AddRectFilled(ImVec2(c.x + 3.5f, c.y - 5.0f), ImVec2(c.x + 5.5f, c.y + 5.0f), fg);
            break;
    }
    return pressed;
}

// Vector-drawn "+" or "x" icon button for Media Bin / Track headers
inline bool CircleActionButton(const char* strId, bool isDelete, float size = 26.0f) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id = window->GetID(strId);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + size, pos.y + size));

    ImGui::ItemSize(ImVec2(size, size));
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    ImDrawList* dl = window->DrawList;
    const ImVec2 c(pos.x + size * 0.5f, pos.y + size * 0.5f);
    const ImU32 bg = hovered ? (isDelete ? kColDangerHover : kColAccent)
                             : IM_COL32(36, 40, 52, 255);
    const ImU32 fg = hovered && !isDelete ? kColTextDark : kColText;

    dl->AddRectFilled(bb.Min, bb.Max, bg, 6.0f);
    dl->AddRect(bb.Min, bb.Max, hovered ? bg : kColBorder, 6.0f);

    if (isDelete) {
        const float r = 4.2f;
        dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), fg, 1.8f);
        dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x - r, c.y + r), fg, 1.8f);
    } else {
        const float r = 4.8f;
        dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), fg, 1.8f);
        dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), fg, 1.8f);
    }
    return pressed;
}

}  // namespace vcut::ui
