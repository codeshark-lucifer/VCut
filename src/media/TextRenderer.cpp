#include "media/TextRenderer.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace vcut {

namespace {

uint32_t nextUtf8(const std::string& s, std::size_t& pos) {
    const unsigned char c0 = static_cast<unsigned char>(s[pos]);
    if (c0 < 0x80) {
        ++pos;
        return c0;
    }
    uint32_t cp = 0xFFFD;
    std::size_t extra = 0;
    if ((c0 & 0xE0) == 0xC0) {
        cp = c0 & 0x1F;
        extra = 1;
    } else if ((c0 & 0xF0) == 0xE0) {
        cp = c0 & 0x0F;
        extra = 2;
    } else if ((c0 & 0xF8) == 0xF0) {
        cp = c0 & 0x07;
        extra = 3;
    } else {
        ++pos;
        return '?';
    }
    ++pos;
    for (std::size_t i = 0; i < extra && pos < s.size(); ++i) {
        const unsigned char cx = static_cast<unsigned char>(s[pos]);
        if ((cx & 0xC0) != 0x80) break;
        cp = (cp << 6) | (cx & 0x3F);
        ++pos;
    }
    return cp;
}

std::vector<std::vector<uint32_t>> splitLines(const std::string& text) {
    std::vector<std::vector<uint32_t>> lines(1);
    std::size_t i = 0;
    while (i < text.size()) {
        const uint32_t cp = nextUtf8(text, i);
        if (cp == '\r') continue;
        if (cp == '\n') {
            lines.emplace_back();
            continue;
        }
        lines.back().push_back(cp);
    }
    return lines;
}

void blendPixel(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, float a) {
    if (a <= 0.0f) return;
    if (a >= 0.999f) {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = 255;
        return;
    }
    const float inv = 1.0f - a;
    dst[0] = static_cast<uint8_t>(std::clamp(r * a + dst[0] * inv, 0.0f, 255.0f));
    dst[1] = static_cast<uint8_t>(std::clamp(g * a + dst[1] * inv, 0.0f, 255.0f));
    dst[2] = static_cast<uint8_t>(std::clamp(b * a + dst[2] * inv, 0.0f, 255.0f));
    dst[3] = 255;
}

struct GlyphBitmap {
    int width = 0;
    int height = 0;
    int left = 0;
    int top = 0;
    int advanceX = 0;
    std::vector<uint8_t> alpha;
};

struct GlyphKey {
    int pxSize = 0;
    uint32_t cp = 0;
    bool operator==(const GlyphKey& o) const { return pxSize == o.pxSize && cp == o.cp; }
};

struct GlyphKeyHash {
    std::size_t operator()(const GlyphKey& k) const {
        return (static_cast<std::size_t>(k.pxSize) << 24) ^ k.cp;
    }
};

}  // namespace

struct TextRenderer::Impl {
    FT_Library ft = nullptr;
    FT_Face face = nullptr;
    int curPx = 0;
    std::unordered_map<GlyphKey, GlyphBitmap, GlyphKeyHash> cache;

    Impl() {
        if (FT_Init_FreeType(&ft) != 0) return;
        const char* candidates[] = {
            "C:\\Windows\\Fonts\\segoeuib.ttf",
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\arialbd.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\calibrib.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        };
        for (const char* path : candidates) {
            if (std::filesystem::exists(path) && FT_New_Face(ft, path, 0, &face) == 0) {
                FT_Select_Charmap(face, FT_ENCODING_UNICODE);
                break;
            }
        }
    }

    ~Impl() {
        if (face) FT_Done_Face(face);
        if (ft) FT_Done_FreeType(ft);
    }

    const GlyphBitmap* getGlyph(int pxSize, uint32_t cp) {
        if (!face) return nullptr;
        const GlyphKey key{pxSize, cp};
        auto it = cache.find(key);
        if (it != cache.end()) return &it->second;

        if (cache.size() > 2048) cache.clear();

        if (curPx != pxSize) {
            FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pxSize));
            curPx = pxSize;
        }
        GlyphBitmap gb;
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER) == 0 && face->glyph) {
            const FT_GlyphSlot g = face->glyph;
            gb.width = static_cast<int>(g->bitmap.width);
            gb.height = static_cast<int>(g->bitmap.rows);
            gb.left = g->bitmap_left;
            gb.top = g->bitmap_top;
            gb.advanceX = static_cast<int>((g->advance.x + 32) >> 6);
            if (gb.width > 0 && gb.height > 0 && g->bitmap.buffer) {
                gb.alpha.resize(static_cast<std::size_t>(gb.width) * gb.height);
                for (int r = 0; r < gb.height; ++r) {
                    const uint8_t* src = g->bitmap.buffer + r * g->bitmap.pitch;
                    std::copy(src, src + gb.width,
                              gb.alpha.data() + static_cast<std::size_t>(r) * gb.width);
                }
            }
        } else {
            gb.advanceX = std::max(4, pxSize / 2);
        }
        auto [ins, _] = cache.emplace(key, std::move(gb));
        return &ins->second;
    }

    void layout(const TextOverlay& overlay, int dstW, int dstH, float scale, float posX, float posY,
                int& outPx, int& outLineH, int& outAscent, int& outPadX, int& outPadY,
                std::vector<std::vector<uint32_t>>& lines, std::vector<int>& lineWidths, Box& box) {
        const float pxF = static_cast<float>(overlay.fontSize) * (static_cast<float>(dstH) / 1080.0f) *
                          std::clamp(scale, 0.1f, 4.0f);
        outPx = std::clamp(static_cast<int>(std::lround(pxF)), 8, 512);
        lines = splitLines(overlay.content);
        if (lines.empty()) lines.emplace_back();

        if (face) {
            if (curPx != outPx) {
                FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(outPx));
                curPx = outPx;
            }
            outAscent = static_cast<int>(face->size->metrics.ascender >> 6);
            outLineH = std::max(outPx + 4, static_cast<int>(face->size->metrics.height >> 6));
        } else {
            outAscent = outPx;
            outLineH = outPx + std::max(4, outPx / 4);
        }

        int maxW = std::max(outPx, 20);
        lineWidths.resize(lines.size(), 0);
        for (std::size_t li = 0; li < lines.size(); ++li) {
            int w = 0;
            for (uint32_t cp : lines[li]) {
                if (const GlyphBitmap* g = getGlyph(outPx, cp)) {
                    w += g->advanceX;
                } else {
                    w += std::max(6, outPx * 3 / 5);
                }
            }
            lineWidths[li] = w;
            maxW = std::max(maxW, w);
        }

        outPadX = std::max(8, outPx / 3);
        outPadY = std::max(6, outPx / 4);
        const int totalTextH = static_cast<int>(lines.size()) * outLineH;
        box.w = maxW + outPadX * 2;
        box.h = totalTextH + outPadY * 2;
        const int cx = static_cast<int>(std::lround(dstW * (0.5f + posX)));
        const int cy = static_cast<int>(std::lround(dstH * (0.5f + posY)));
        box.x = cx - box.w / 2;
        box.y = cy - box.h / 2;
    }
};

TextRenderer::TextRenderer() : impl_(std::make_unique<Impl>()) {}
TextRenderer::~TextRenderer() = default;

TextRenderer::Box TextRenderer::measure(const TextOverlay& overlay, int dstW, int dstH, float scale,
                                        float posX, float posY) {
    int px = 0, lh = 0, asc = 0, pxPad = 0, pyPad = 0;
    std::vector<std::vector<uint32_t>> lines;
    std::vector<int> widths;
    Box b;
    impl_->layout(overlay, dstW, dstH, scale, posX, posY, px, lh, asc, pxPad, pyPad, lines, widths, b);
    return b;
}

void TextRenderer::render(const TextOverlay& overlay, int dstW, int dstH, float scale, float posX,
                          float posY, float opacity, uint8_t* rgba, float wipeMinX, float wipeMaxX) {
    if (!rgba || dstW <= 0 || dstH <= 0 || opacity <= 0.001f || overlay.content.empty()) return;

    int px = 0, lineH = 0, ascent = 0, padX = 0, padY = 0;
    std::vector<std::vector<uint32_t>> lines;
    std::vector<int> lineWidths;
    Box box;
    impl_->layout(overlay, dstW, dstH, scale, posX, posY, px, lineH, ascent, padX, padY, lines,
                  lineWidths, box);

    const int clipX0 = std::clamp(static_cast<int>(std::floor(wipeMinX * dstW)), 0, dstW);
    const int clipX1 = std::clamp(static_cast<int>(std::ceil(wipeMaxX * dstW)), 0, dstW);
    if (clipX1 <= clipX0) return;

    // 1. Background rounded pill
    if (overlay.bgOpacity > 0.005f) {
        const uint8_t br = static_cast<uint8_t>(std::clamp(overlay.bgColor[0], 0.0f, 1.0f) * 255.0f);
        const uint8_t bg = static_cast<uint8_t>(std::clamp(overlay.bgColor[1], 0.0f, 1.0f) * 255.0f);
        const uint8_t bb = static_cast<uint8_t>(std::clamp(overlay.bgColor[2], 0.0f, 1.0f) * 255.0f);
        const float bgA = std::clamp(overlay.bgOpacity * opacity, 0.0f, 1.0f);
        const float radius = std::min<float>(std::max(4.0f, px * 0.25f),
                                             std::min(box.w, box.h) * 0.5f);

        const int yStart = std::max(0, box.y);
        const int yEnd = std::min(dstH, box.y + box.h);
        const int xStart = std::max(clipX0, box.x);
        const int xEnd = std::min(clipX1, box.x + box.w);

        for (int y = yStart; y < yEnd; ++y) {
            const float dyTop = (box.y + radius) - (y + 0.5f);
            const float dyBot = (y + 0.5f) - (box.y + box.h - radius);
            const float dy = std::max(0.0f, std::max(dyTop, dyBot));
            for (int x = xStart; x < xEnd; ++x) {
                const float dxLeft = (box.x + radius) - (x + 0.5f);
                const float dxRight = (x + 0.5f) - (box.x + box.w - radius);
                const float dx = std::max(0.0f, std::max(dxLeft, dxRight));
                if (dx > 0.0f && dy > 0.0f) {
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    if (dist > radius + 0.5f) continue;
                    const float edge = std::clamp(radius + 0.5f - dist, 0.0f, 1.0f);
                    blendPixel(rgba + (static_cast<std::size_t>(y) * dstW + x) * 4, br, bg, bb,
                               bgA * edge);
                } else {
                    blendPixel(rgba + (static_cast<std::size_t>(y) * dstW + x) * 4, br, bg, bb, bgA);
                }
            }
        }
    }

    // 2. Rasterize glyph bitmaps
    const uint8_t tr = static_cast<uint8_t>(std::clamp(overlay.color[0], 0.0f, 1.0f) * 255.0f);
    const uint8_t tg = static_cast<uint8_t>(std::clamp(overlay.color[1], 0.0f, 1.0f) * 255.0f);
    const uint8_t tb = static_cast<uint8_t>(std::clamp(overlay.color[2], 0.0f, 1.0f) * 255.0f);
    const int shadowOff = std::max(1, px / 18);

    auto drawGlyph = [&](const GlyphBitmap& g, int gx, int gy, uint8_t r, uint8_t gr, uint8_t b,
                         float mulA) {
        if (g.alpha.empty()) return;
        for (int row = 0; row < g.height; ++row) {
            const int py = gy + row;
            if (py < 0 || py >= dstH) continue;
            for (int col = 0; col < g.width; ++col) {
                const int pxX = gx + col;
                if (pxX < clipX0 || pxX >= clipX1) continue;
                const uint8_t cov = g.alpha[static_cast<std::size_t>(row) * g.width + col];
                if (cov == 0) continue;
                const float a = (static_cast<float>(cov) / 255.0f) * mulA;
                blendPixel(rgba + (static_cast<std::size_t>(py) * dstW + pxX) * 4, r, gr, b, a);
            }
        }
    };

    for (std::size_t li = 0; li < lines.size(); ++li) {
        int penX = box.x + (box.w - lineWidths[li]) / 2;
        const int baselineY = box.y + padY + static_cast<int>(li) * lineH + ascent;
        for (uint32_t cp : lines[li]) {
            if (const GlyphBitmap* g = impl_->getGlyph(px, cp)) {
                const int gx = penX + g->left;
                const int gy = baselineY - g->top;
                if (overlay.shadow) {
                    drawGlyph(*g, gx + shadowOff, gy + shadowOff, 0, 0, 0, opacity * 0.72f);
                }
                drawGlyph(*g, gx, gy, tr, tg, tb, opacity);
                penX += g->advanceX;
            } else {
                // Minimal rectangular fallback if no TrueType font was loaded
                const int w = std::max(4, px / 2);
                const int h = std::max(6, (px * 3) / 4);
                const int gy = baselineY - h;
                if (cp != ' ') {
                    for (int ry = std::max(0, gy); ry < std::min(dstH, gy + h); ++ry) {
                        for (int rx = std::max(clipX0, penX); rx < std::min(clipX1, penX + w); ++rx) {
                            blendPixel(rgba + (static_cast<std::size_t>(ry) * dstW + rx) * 4, tr, tg,
                                       tb, opacity * 0.85f);
                        }
                    }
                }
                penX += w + std::max(2, px / 8);
            }
        }
    }
}

}  // namespace vcut
