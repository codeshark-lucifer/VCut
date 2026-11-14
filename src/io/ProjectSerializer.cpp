#include "io/ProjectSerializer.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "core/PathUtil.hpp"
#include "core/TimelineOps.hpp"

namespace vcut {

namespace {

using json = nlohmann::json;

const char* mediaTypeToStr(MediaType t) {
    switch (t) {
        case MediaType::Video:
            return "video";
        case MediaType::Audio:
            return "audio";
        case MediaType::Image:
            return "image";
    }
    return "video";
}

MediaType strToMediaType(const std::string& s) {
    if (s == "audio") return MediaType::Audio;
    if (s == "image") return MediaType::Image;
    return MediaType::Video;
}

const char* trackKindToStr(TrackKind k) {
    return k == TrackKind::Audio ? "audio" : "video";
}

TrackKind strToTrackKind(const std::string& s) {
    return s == "audio" ? TrackKind::Audio : TrackKind::Video;
}

}  // namespace

bool saveProject(const Project& project, const std::string& path, std::string* error) {
    json j;
    j["format"] = "vcut";
    j["version"] = 1;
    j["nextId"] = project.nextId;
    j["settings"] = {
        {"width", project.settings.width},
        {"height", project.settings.height},
        {"fps", project.settings.fps},
        {"sampleRate", project.settings.sampleRate},
    };

    json assets = json::array();
    for (const auto& a : project.assets) {
        assets.push_back({
            {"id", a.id},
            {"path", a.path},
            {"name", a.name},
            {"type", mediaTypeToStr(a.type)},
            {"duration", a.duration},
            {"width", a.width},
            {"height", a.height},
            {"fps", a.fps},
            {"hasVideo", a.hasVideo},
            {"hasAudio", a.hasAudio},
        });
    }
    j["assets"] = std::move(assets);

    json tracks = json::array();
    for (const auto& t : project.tracks) {
        json clips = json::array();
        for (const auto& c : t.clips) {
            clips.push_back({
                {"id", c.id},
                {"assetId", c.assetId},
                {"start", c.start},
                {"in", c.in},
                {"out", c.out},
                {"volume", c.volume},
                {"muted", c.muted},
                {"fadeIn", c.fadeIn},
                {"fadeOut", c.fadeOut},
            });
        }
        tracks.push_back({
            {"id", t.id},
            {"kind", trackKindToStr(t.kind)},
            {"name", t.name},
            {"muted", t.muted},
            {"hidden", t.hidden},
            {"clips", std::move(clips)},
        });
    }
    j["tracks"] = std::move(tracks);

    std::ofstream out(pathFromUtf8(path), std::ios::binary);
    if (!out) {
        if (error) *error = "Cannot open file for writing: " + path;
        return false;
    }
    out << j.dump(2) << '\n';
    if (!out.good()) {
        if (error) *error = "Failed writing project file: " + path;
        return false;
    }
    return true;
}

std::optional<Project> loadProject(const std::string& path, std::string* error) {
    auto fail = [&](const std::string& msg) -> std::optional<Project> {
        if (error) *error = msg;
        return std::nullopt;
    };

    std::ifstream in(pathFromUtf8(path), std::ios::binary);
    if (!in) return fail("Cannot open project file: " + path);

    json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        return fail(std::string("Invalid JSON: ") + e.what());
    }

    if (j.value("format", "") != "vcut") return fail("Not a .vcut project file");

    Project p;
    p.nextId = j.value("nextId", 1);
    if (j.contains("settings")) {
        const auto& s = j["settings"];
        p.settings.width = s.value("width", 1920);
        p.settings.height = s.value("height", 1080);
        p.settings.fps = s.value("fps", 30);
        p.settings.sampleRate = s.value("sampleRate", 48000);
    }

    for (const auto& ja : j.value("assets", json::array())) {
        MediaAsset a;
        a.id = ja.value("id", 0);
        a.path = ja.value("path", "");
        a.name = ja.value("name", "");
        a.type = strToMediaType(ja.value("type", "video"));
        a.duration = ja.value("duration", Time{0});
        a.width = ja.value("width", 0);
        a.height = ja.value("height", 0);
        a.fps = ja.value("fps", 0.0);
        a.hasVideo = ja.value("hasVideo", false);
        a.hasAudio = ja.value("hasAudio", false);
        a.missing = !std::filesystem::exists(pathFromUtf8(a.path));
        p.nextId = std::max(p.nextId, a.id + 1);
        p.assets.push_back(std::move(a));
    }

    for (const auto& jt : j.value("tracks", json::array())) {
        Track t;
        t.id = jt.value("id", 0);
        t.kind = strToTrackKind(jt.value("kind", "video"));
        t.name = jt.value("name", "Track");
        t.muted = jt.value("muted", false);
        t.hidden = jt.value("hidden", false);
        p.nextId = std::max(p.nextId, t.id + 1);
        for (const auto& jc : jt.value("clips", json::array())) {
            Clip c;
            c.id = jc.value("id", 0);
            c.assetId = jc.value("assetId", 0);
            c.start = jc.value("start", Time{0});
            c.in = jc.value("in", Time{0});
            c.out = jc.value("out", Time{0});
            c.volume = jc.value("volume", 1.0f);
            c.muted = jc.value("muted", false);
            c.fadeIn = jc.value("fadeIn", Time{0});
            c.fadeOut = jc.value("fadeOut", Time{0});
            p.nextId = std::max(p.nextId, c.id + 1);
            t.clips.push_back(c);
        }
        ops::sortTrack(t);
        p.tracks.push_back(std::move(t));
    }

    if (p.tracks.empty()) p = Project::createDefault();
    return p;
}

}  // namespace vcut
