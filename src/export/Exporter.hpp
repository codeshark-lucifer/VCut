#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "core/Project.hpp"

namespace vcut {

struct ExportOptions {
    std::string outputPath;
    int width = 0;       // 0 = use project settings
    int height = 0;      // 0 = use project settings
    int fps = 0;         // 0 = use project settings
    int sampleRate = 0;  // 0 = use project settings
};

// Synchronously exports `project` to an MP4 (H.264 + AAC) file.
// Optional progress/cancel atomics allow driving from a worker thread.
bool exportProject(const Project& project, const ExportOptions& opts, std::string* error = nullptr,
                   std::atomic<float>* progress = nullptr,
                   const std::atomic<bool>* cancel = nullptr);

// Background export runner for the UI.
class ExportJob {
public:
    ExportJob() = default;
    ~ExportJob();
    ExportJob(const ExportJob&) = delete;
    ExportJob& operator=(const ExportJob&) = delete;

    void start(Project project, ExportOptions opts);
    void cancel();
    void wait();

    bool isRunning() const { return running_.load(); }
    bool isDone() const { return done_.load(); }
    bool succeeded() const { return ok_.load(); }
    float progress() const { return progress_.load(); }
    std::string error() const;

private:
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> done_{false};
    std::atomic<bool> ok_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<float> progress_{0.0f};
    mutable std::mutex errMutex_;
    std::string error_;
};

}  // namespace vcut
