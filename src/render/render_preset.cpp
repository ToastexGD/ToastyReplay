#include "render/render_preset.hpp"
#include "utils.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

#include <zlib.h>

static constexpr uint8_t kPresetVersion = 1;
static constexpr size_t kMaxPayloadSize = 64 * 1024;

static bool presetCompress(std::string const& input, std::vector<uint8_t>& output) {
    uLongf bound = compressBound(static_cast<uLong>(input.size()));
    output.resize(bound);
    int r = compress2(output.data(), &bound,
        reinterpret_cast<Bytef const*>(input.data()),
        static_cast<uLong>(input.size()),
        Z_DEFAULT_COMPRESSION);
    if (r != Z_OK) { output.clear(); return false; }
    output.resize(bound);
    return true;
}

static bool presetDecompress(uint8_t const* data, size_t length, std::vector<uint8_t>& output) {
    output.resize(kMaxPayloadSize);
    uLongf destLen = kMaxPayloadSize;
    int r = uncompress(output.data(), &destLen, data, static_cast<uLong>(length));
    if (r != Z_OK) { output.clear(); return false; }
    output.resize(destLen);
    return true;
}

static bool parseFloat(std::string_view text, float& out) {
    auto const* begin = text.data();
    auto result = std::from_chars(begin, begin + text.size(), out);
    return result.ec == std::errc{} && result.ptr == begin + text.size();
}

static const char* familyToString(RenderCodecFamily f) {
    return (f == RenderCodecFamily::AV1) ? "AV1" : "H264";
}

static RenderCodecFamily familyFromString(std::string_view s) {
    return (s == "AV1") ? RenderCodecFamily::AV1 : RenderCodecFamily::H264;
}

static const char* tierToString(RenderQualityTier t) {
    switch (t) {
        case RenderQualityTier::Fast:     return "Fast";
        case RenderQualityTier::Quality:  return "Quality";
        case RenderQualityTier::Lossless: return "Lossless";
        default:                          return "Balanced";
    }
}

static RenderQualityTier tierFromString(std::string_view s) {
    if (s == "Fast")     return RenderQualityTier::Fast;
    if (s == "Quality")  return RenderQualityTier::Quality;
    if (s == "Lossless") return RenderQualityTier::Lossless;
    return RenderQualityTier::Balanced;
}

static std::string serializePreset(RenderPreset const& p) {
    std::ostringstream ss;
    ss << "ttrp:1\n"
       << "name="         << p.name          << "\n"
       << "res="          << p.width << "x" << p.height << "\n"
       << "fps="          << p.fps           << "\n"
       << "codec="        << p.codec         << "\n"
       << "bitrate="      << p.bitrate       << "\n"
       << "ext="          << p.ext           << "\n"
       << "args="         << p.extraArgs     << "\n"
       << "vargs="        << p.videoArgs     << "\n"
       << "aargs="        << p.audioArgs     << "\n"
       << "acodec="       << p.audioCodec    << "\n"
       << "after="        << p.secondsAfter  << "\n"
       << "audio="        << (p.includeAudio       ? "true" : "false") << "\n"
       << "clicks="       << (p.includeClicks      ? "true" : "false") << "\n"
       << "sfx="          << p.sfxVol        << "\n"
       << "music="        << p.musicVol      << "\n"
       << "hide_end="     << (p.hideEndscreen    ? "true" : "false") << "\n"
       << "hide_complete=" << (p.hideLevelComplete ? "true" : "false") << "\n"
       << "tier="         << tierToString(p.tier) << "\n"
       << "use_gpu="      << (p.useGpu ? "true" : "false") << "\n"
       << "codec_family=" << familyToString(p.codecFamily) << "\n";
    return ss.str();
}

static std::optional<RenderPreset> parsePreset(std::string_view text) {
    RenderPreset p;
    bool hasName = false;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();

        std::string_view line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        pos = end + 1;

        if (line.empty() || line[0] == '#') continue;

        size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;

        std::string_view key = line.substr(0, eq);
        std::string_view val = line.substr(eq + 1);

        if (key == "name") {
            p.name = std::string(val);
            hasName = true;
        } else if (key == "res") {
            size_t x = val.find('x');
            if (x != std::string_view::npos) {
                auto w = toasty::parseInteger<int>(val.substr(0, x));
                auto h = toasty::parseInteger<int>(val.substr(x + 1));
                if (w && h) { p.width = *w; p.height = *h; }
            }
        } else if (key == "fps")            { auto v = toasty::parseInteger<int>(val); if (v) p.fps = *v; }
        else if (key == "codec")             p.codec      = std::string(val);
        else if (key == "bitrate")           p.bitrate    = std::string(val);
        else if (key == "ext")               p.ext        = std::string(val);
        else if (key == "args")              p.extraArgs  = std::string(val);
        else if (key == "vargs")             p.videoArgs  = std::string(val);
        else if (key == "aargs")             p.audioArgs  = std::string(val);
        else if (key == "acodec")            p.audioCodec = std::string(val);
        else if (key == "after")             { float v = 0; if (parseFloat(val, v)) p.secondsAfter = v; }
        else if (key == "audio")             p.includeAudio      = (val == "true");
        else if (key == "clicks")            p.includeClicks     = (val == "true");
        else if (key == "sfx")               { float v = 0; if (parseFloat(val, v)) p.sfxVol   = v; }
        else if (key == "music")             { float v = 0; if (parseFloat(val, v)) p.musicVol = v; }
        else if (key == "hide_end")          p.hideEndscreen     = (val == "true");
        else if (key == "hide_complete")     p.hideLevelComplete = (val == "true");
        else if (key == "tier")              p.tier        = tierFromString(val);
        else if (key == "use_gpu")           p.useGpu      = (val == "true");
        else if (key == "codec_family")      p.codecFamily = familyFromString(val);
    }

    if (!hasName || p.name.empty()) return std::nullopt;
    return p;
}

std::filesystem::path RenderPresetIO::pathForName(
    std::filesystem::path const& presetsDir, std::string const& name)
{
    std::string filename = name;
    std::replace(filename.begin(), filename.end(), ' ', '_');
    return presetsDir / (filename + ".ttrp");
}

bool RenderPresetIO::save(std::filesystem::path const& path, RenderPreset const& preset) {
    std::vector<uint8_t> compressed;
    if (!presetCompress(serializePreset(preset), compressed)) return false;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;

    std::ofstream file(path, std::ios::binary);
    if (!file) return false;

    static constexpr uint8_t kMagic[4] = {'T', 'T', 'R', 'P'};
    file.write(reinterpret_cast<char const*>(kMagic), sizeof(kMagic));
    file.write(reinterpret_cast<char const*>(&kPresetVersion), 1);
    file.write(reinterpret_cast<char const*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
    return file.good();
}

std::optional<RenderPreset> RenderPresetIO::load(std::filesystem::path const& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;

    auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize < 5) return std::nullopt;
    file.seekg(0);

    std::vector<uint8_t> buf(fileSize);
    if (!file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(fileSize)))
        return std::nullopt;

    if (buf[0] != 'T' || buf[1] != 'T' || buf[2] != 'R' || buf[3] != 'P') return std::nullopt;
    if (buf[4] != kPresetVersion) return std::nullopt;

    std::vector<uint8_t> plain;
    if (!presetDecompress(buf.data() + 5, fileSize - 5, plain)) return std::nullopt;

    return parsePreset(std::string_view(reinterpret_cast<char const*>(plain.data()), plain.size()));
}

std::vector<std::string> RenderPresetIO::listNames(std::filesystem::path const& presetsDir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(presetsDir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".ttrp") continue;
        auto preset = RenderPresetIO::load(entry.path());
        if (preset) names.push_back(std::move(preset->name));
    }
    std::sort(names.begin(), names.end());
    return names;
}

RenderConfig RenderPreset::toRenderConfig() const {
    RenderConfig cfg;
    cfg.tier        = tier;
    cfg.useGpu      = useGpu;
    cfg.codecFamily = codecFamily;
    cfg.width  = static_cast<unsigned>(width);
    cfg.height = static_cast<unsigned>(height);
    cfg.fps    = static_cast<unsigned>(fps);
    cfg.includeAudio    = includeAudio;
    cfg.includeClicks   = includeClicks;
    cfg.musicVol        = musicVol;
    cfg.sfxVol          = sfxVol;
    cfg.secondsAfter    = secondsAfter;
    cfg.hideEndscreen   = hideEndscreen;
    cfg.hideLevelComplete = hideLevelComplete;

    // stock default values of the old render path are not promoted as overrides, they would
    // shadow tier defaults and "30" (no M suffix) would generate -maxrate 30 bps and break encoding.
    static constexpr std::string_view kDefaultExtraArgs = "-pix_fmt yuv420p";
    static constexpr std::string_view kDefaultVideoArgs = "colorspace=all=bt709:iall=bt470bg:fast=1";
    static constexpr std::string_view kDefaultBitrate   = "30";

    if (!codec.empty())                              cfg.codec      = codec;
    if (!bitrate.empty()   && bitrate   != kDefaultBitrate)   cfg.maxBitrate = bitrate;
    if (!ext.empty()       && ext       != ".mp4")            cfg.ext        = ext;
    if (!extraArgs.empty() && extraArgs != kDefaultExtraArgs) cfg.extraArgs  = extraArgs;
    if (!videoArgs.empty() && videoArgs != kDefaultVideoArgs) cfg.videoArgs  = videoArgs;
    if (!audioArgs.empty())  cfg.audioArgs  = audioArgs;
    if (!audioCodec.empty()) cfg.audioCodec = audioCodec;

    return cfg;
}

RenderPreset RenderPreset::fromRenderConfig(const RenderConfig& cfg, std::string name) {
    RenderPreset p;
    p.name        = std::move(name);
    p.tier        = cfg.tier;
    p.useGpu      = cfg.useGpu;
    p.codecFamily = cfg.codecFamily;
    p.width  = static_cast<int>(cfg.width);
    p.height = static_cast<int>(cfg.height);
    p.fps    = static_cast<int>(cfg.fps);
    p.includeAudio    = cfg.includeAudio;
    p.includeClicks   = cfg.includeClicks;
    p.musicVol        = cfg.musicVol;
    p.sfxVol          = cfg.sfxVol;
    p.secondsAfter    = cfg.secondsAfter;
    p.hideEndscreen   = cfg.hideEndscreen;
    p.hideLevelComplete = cfg.hideLevelComplete;
    p.codec     = cfg.codec.value_or("");
    p.bitrate   = cfg.maxBitrate.value_or("");
    p.ext       = cfg.ext.value_or(".mp4");
    p.extraArgs = cfg.extraArgs.value_or("");
    p.videoArgs = cfg.videoArgs.value_or("");
    p.audioArgs  = cfg.audioArgs.value_or("");
    p.audioCodec = cfg.audioCodec.value_or("");
    return p;
}
