#include "io/ProjectSerializer.hpp"

#include <algorithm>
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
        case MediaType::Text:
            return "text";
    }
    return "video";
}

MediaType strToMediaType(const std::string& s) {
    if (s == "audio") return MediaType::Audio;
    if (s == "image") return MediaType::Image;
    if (s == "text") return MediaType::Text;
    return MediaType::Video;
}

const char* trackKindToStr(TrackKind k) {
    return k == TrackKind::Audio ? "audio" : "video";
}

TrackKind strToTrackKind(const std::string& s) {
    return s == "audio" ? TrackKind::Audio : TrackKind::Video;
}

const char* transitionToStr(TransitionKind k) {
    switch (k) {
        case TransitionKind::None:
            return "none";
        case TransitionKind::Dissolve:
            return "dissolve";
        case TransitionKind::DipToBlack:
            return "dip_to_black";
        case TransitionKind::WipeLeft:
            return "wipe_left";
        case TransitionKind::WipeRight:
            return "wipe_right";
    }
    return "none";
}

TransitionKind strToTransition(const std::string& s) {
    if (s == "dissolve") return TransitionKind::Dissolve;
    if (s == "dip_to_black") return TransitionKind::DipToBlack;
    if (s == "wipe_left") return TransitionKind::WipeLeft;
    if (s == "wipe_right") return TransitionKind::WipeRight;
    return TransitionKind::None;
}

void readColor3(const json& j, const char* key, float dst[3]) {
    if (j.contains(key) && j[key].is_array() && j[key].size() == 3) {
        for (int i = 0; i < 3; ++i) {
            dst[i] = std::clamp(j[key][i].get<float>(), 0.0f, 1.0f);
        }
    }
}

}  // namespace

bool saveProject(const Project& project, const std::string& path, std::string* error) {
    json j;
    j["format"] = "vcut";
    j["version"] = 2;
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
            json jc = {
                {"id", c.id},
                {"assetId", c.assetId},
                {"start", c.start},
                {"in", c.in},
                {"out", c.out},
                {"speed", c.speed},
                {"volume", c.volume},
                {"muted", c.muted},
                {"fadeIn", c.fadeIn},
                {"fadeOut", c.fadeOut},
                {"opacity", c.opacity},
                {"scale", c.scale},
                {"posX", c.posX},
                {"posY", c.posY},
                {"videoFadeIn", c.videoFadeIn},
                {"videoFadeOut", c.videoFadeOut},
                {"brightness", c.brightness},
                {"contrast", c.contrast},
                {"saturation", c.saturation},
                {"vignette", c.vignette},
                {"grayscale", c.grayscale},
                {"chromaKey", c.chromaKey},
                {"chromaColor", {c.chromaColor[0], c.chromaColor[1], c.chromaColor[2]}},
                {"chromaSimilarity", c.chromaSimilarity},
                {"chromaBlend", c.chromaBlend},
                {"transitionIn", transitionToStr(c.transitionIn)},
                {"transitionInDur", c.transitionInDur},
                {"isText", c.isText},
            };
            if (c.isText) {
                jc["text"] = {
                    {"content", c.text.content},
                    {"fontSize", c.text.fontSize},
                    {"color", {c.text.color[0], c.text.color[1], c.text.color[2]}},
                    {"bgColor", {c.text.bgColor[0], c.text.bgColor[1], c.text.bgColor[2]}},
                    {"bgOpacity", c.text.bgOpacity},
                    {"shadow", c.text.shadow},
                };
            }
            clips.push_back(std::move(jc));
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
        a.missing = a.type != MediaType::Text && !std::filesystem::exists(pathFromUtf8(a.path));
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
            c.speed = std::clamp(jc.value("speed", 1.0f), 0.25f, 4.0f);
            c.volume = jc.value("volume", 1.0f);
            c.muted = jc.value("muted", false);
            c.fadeIn = jc.value("fadeIn", Time{0});
            c.fadeOut = jc.value("fadeOut", Time{0});
            c.opacity = std::clamp(jc.value("opacity", 1.0f), 0.0f, 1.0f);
            c.scale = std::clamp(jc.value("scale", 1.0f), 0.1f, 4.0f);
            c.posX = std::clamp(jc.value("posX", 0.0f), -1.5f, 1.5f);
            c.posY = std::clamp(jc.value("posY", 0.0f), -1.5f, 1.5f);
            c.videoFadeIn = std::max<Time>(0, jc.value("videoFadeIn", Time{0}));
            c.videoFadeOut = std::max<Time>(0, jc.value("videoFadeOut", Time{0}));
            c.brightness = std::clamp(jc.value("brightness", 0.0f), -1.0f, 1.0f);
            c.contrast = std::clamp(jc.value("contrast", 1.0f), 0.0f, 2.0f);
            c.saturation = std::clamp(jc.value("saturation", 1.0f), 0.0f, 2.0f);
            c.vignette = std::clamp(jc.value("vignette", 0.0f), 0.0f, 1.0f);
            c.grayscale = jc.value("grayscale", false);
            c.chromaKey = jc.value("chromaKey", false);
            readColor3(jc, "chromaColor", c.chromaColor);
            c.chromaSimilarity = std::clamp(jc.value("chromaSimilarity", 0.28f), 0.01f, 0.8f);
            c.chromaBlend = std::clamp(jc.value("chromaBlend", 0.08f), 0.0f, 0.5f);
            c.transitionIn = strToTransition(jc.value("transitionIn", "none"));
            c.transitionInDur = std::max<Time>(0, jc.value("transitionInDur", Time{0}));
            c.isText = jc.value("isText", false);
            if (jc.contains("text") && jc["text"].is_object()) {
                const auto& jtxt = jc["text"];
                c.text.content = jtxt.value("content", std::string("Sample Text"));
                c.text.fontSize = std::clamp(jtxt.value("fontSize", 64), 12, 240);
                readColor3(jtxt, "color", c.text.color);
                readColor3(jtxt, "bgColor", c.text.bgColor);
                c.text.bgOpacity = std::clamp(jtxt.value("bgOpacity", 0.45f), 0.0f, 1.0f);
                c.text.shadow = jtxt.value("shadow", true);
            }
            p.nextId = std::max(p.nextId, c.id + 1);
            t.clips.push_back(std::move(c));
        }
        ops::sortTrack(t);
        p.tracks.push_back(std::move(t));
    }

    if (p.tracks.empty()) p = Project::createDefault();
    return p;
}

}  // namespace vcut
