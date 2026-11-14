#pragma once

#include <SDL3/SDL_opengl.h>
#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/Project.hpp"
#include "media/FrameBlit.hpp"

namespace vcut {

class GlTexture {
public:
    GlTexture() = default;
    ~GlTexture() { reset(); }
    GlTexture(const GlTexture&) = delete;
    GlTexture& operator=(const GlTexture&) = delete;

    GlTexture(GlTexture&& o) noexcept : id_(o.id_), w_(o.w_), h_(o.h_) {
        o.id_ = 0;
        o.w_ = o.h_ = 0;
    }
    GlTexture& operator=(GlTexture&& o) noexcept {
        if (this != &o) {
            reset();
            id_ = o.id_;
            w_ = o.w_;
            h_ = o.h_;
            o.id_ = 0;
            o.w_ = o.h_ = 0;
        }
        return *this;
    }

    void reset() {
        if (id_ != 0) {
            glDeleteTextures(1, &id_);
            id_ = 0;
            w_ = h_ = 0;
        }
    }

    void upload(int w, int h, const std::uint8_t* rgba) {
        if (w <= 0 || h <= 0 || !rgba) return;
        if (id_ == 0) {
            glGenTextures(1, &id_);
            glBindTexture(GL_TEXTURE_2D, id_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            glBindTexture(GL_TEXTURE_2D, id_);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (w != w_ || h != h_) {
            w_ = w;
            h_ = h;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    bool valid() const { return id_ != 0; }
    ImTextureID imguiId() const { return static_cast<ImTextureID>(id_); }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    GLuint id_ = 0;
    int w_ = 0;
    int h_ = 0;
};

class ThumbnailCache {
public:
    static constexpr int kThumbW = 88;
    static constexpr int kThumbH = 50;

    ImTextureID get(const MediaAsset& asset) {
        if (!asset.hasVideo || asset.missing) return 0;
        auto it = cache_.find(asset.id);
        if (it != cache_.end()) return it->second.tex.imguiId();

        Entry e;
        std::vector<std::uint8_t> rgba;
        const Time sampleTime =
            asset.duration > 0 ? std::min<Time>(kSecond, asset.duration / 5) : 0;
        if (makeThumbnail(asset.path, sampleTime, kThumbW, kThumbH, rgba)) {
            e.tex.upload(kThumbW, kThumbH, rgba.data());
        }
        auto [ins, _] = cache_.emplace(asset.id, std::move(e));
        return ins->second.tex.imguiId();
    }

    void clear() { cache_.clear(); }

private:
    struct Entry {
        GlTexture tex;
    };
    std::unordered_map<int, Entry> cache_;
};

}  // namespace vcut
