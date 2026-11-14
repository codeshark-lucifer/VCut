#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>

#include "core/Project.hpp"

namespace vcut {

// Owns the Project plus undo/redo history and selection.
// Undo is snapshot-based: projects are small (no media data), so copying is cheap.
class Editor {
public:
    Editor() : project_(Project::createDefault()) {}

    const Project& project() const { return project_; }

    // Bumped on every change; used to know when to refresh preview/audio snapshots.
    std::uint64_t version() const { return version_; }

    // Apply a one-shot edit. `f(Project&)` returns true if it changed something.
    template <class F>
    bool edit(F&& f) {
        Project before = project_;
        if (!f(project_)) {
            project_ = std::move(before);
            return false;
        }
        pushUndo(std::move(before));
        touch();
        return true;
    }

    // Continuous edits (dragging a clip): each gestureApply() starts again from the
    // state at beginGesture(), so the result only depends on the current mouse delta.
    void beginGesture() {
        gestureBase_ = project_;
        gestureChanged_ = false;
    }
    template <class F>
    void gestureApply(F&& f) {
        if (!gestureBase_) return;
        Project p = *gestureBase_;
        if (f(p)) {
            project_ = std::move(p);
            gestureChanged_ = true;
            touch();
        }
    }
    void endGesture() {
        if (gestureBase_ && gestureChanged_) pushUndo(std::move(*gestureBase_));
        gestureBase_.reset();
    }
    bool inGesture() const { return gestureBase_.has_value(); }

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void undo() {
        if (undo_.empty()) return;
        redo_.push_back(std::move(project_));
        project_ = std::move(undo_.back());
        undo_.pop_back();
        touch();
    }
    void redo() {
        if (redo_.empty()) return;
        undo_.push_back(std::move(project_));
        project_ = std::move(redo_.back());
        redo_.pop_back();
        touch();
    }

    // Replace the whole document (new / open). Clears history.
    void reset(Project p, std::string path) {
        project_ = std::move(p);
        path_ = std::move(path);
        undo_.clear();
        redo_.clear();
        gestureBase_.reset();
        dirty_ = false;
        selectedClip = 0;
        selectedAsset = 0;
        ++version_;
    }

    bool dirty() const { return dirty_; }
    void markClean() { dirty_ = false; }
    const std::string& path() const { return path_; }
    void setPath(std::string p) { path_ = std::move(p); }

    int selectedClip = 0;
    int selectedAsset = 0;

private:
    static constexpr std::size_t kMaxUndo = 200;

    void pushUndo(Project p) {
        undo_.push_back(std::move(p));
        if (undo_.size() > kMaxUndo) undo_.pop_front();
        redo_.clear();
    }
    void touch() {
        ++version_;
        dirty_ = true;
    }

    Project project_;
    std::string path_;
    std::deque<Project> undo_;
    std::deque<Project> redo_;
    std::optional<Project> gestureBase_;
    bool gestureChanged_ = false;
    bool dirty_ = false;
    std::uint64_t version_ = 1;
};

}  // namespace vcut
