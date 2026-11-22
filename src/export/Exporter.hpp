#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "core/Project.hpp"

namespace vcut {

enum class EncoderBackend {
    Auto,     // Try GPU hardware H.264 (NVENC / AMF / QSV), fall back to CPU libx264
    CpuX264,  // CPU libx264 (highest compatibility)
    Nvenc,    // NVIDIA NVENC (h264_nvenc)
    Amf,      // AMD AMF (h264_amf)
    Qsv,      // Intel Quick Sync (h264_qsv)
};

struct ExportOptions {
    std::string outputPath;
    int width = 0;       // 0 = use project settings
    int height = 0;      // 0 = use project settings
    int fps = 0;         // 0 = use project settings
    int sampleRate = 0;  // 0 = use project settings
    int crf = 21;        // H.264 quality (lower = higher quality)
    std::string preset = "veryfast";
    EncoderBackend encoder = EncoderBackend::CpuX264;
};

// Synchronously exports `project` to an MP4 (H.264 + AAC) file.
// Optional progress/cancel/usedEncoder pointers allow monitoring from UI/tests.
bool exportProject(const Project& project, const ExportOptions& opts, std::string* error = nullptr,
                   std::atomic<float>* progress = nullptr,
                   const std::atomic<bool>* cancel = nullptr,
                   std::string* usedEncoder = nullptr);

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
    std::string usedEncoder() const;

private:
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> done_{false};
    std::atomic<bool> ok_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<float> progress_{0.0f};
    mutable std::mutex errMutex_;
    std::string error_;
    std::string usedEncoder_;
};

}  // namespace vcut
