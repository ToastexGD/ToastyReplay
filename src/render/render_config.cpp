#include "render/render_config.hpp"
#include "render/ffmpeg_events.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>
#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

using namespace geode::prelude;

static GpuVendor g_gpuVendor = GpuVendor::Unknown;

void setDetectedGpuVendor(GpuVendor vendor) { g_gpuVendor = vendor; }
GpuVendor detectedGpuVendor() { return g_gpuVendor; }

static std::string pickVendorEncoder(const std::vector<std::string>& codecs,
                                     const char* nvenc, const char* amf, const char* qsv) {
    auto has = [&](const char* c) {
        return c && std::find(codecs.begin(), codecs.end(), std::string(c)) != codecs.end();
    };
    switch (g_gpuVendor) {
        case GpuVendor::Nvidia: return has(nvenc) ? std::string(nvenc) : std::string{};
        case GpuVendor::Amd:    return has(amf)   ? std::string(amf)   : std::string{};
        case GpuVendor::Intel:  return has(qsv)   ? std::string(qsv)   : std::string{};
        case GpuVendor::Unknown: break;
    }
    for (const char* c : { nvenc, amf, qsv })
        if (has(c)) return c;
    return {};
}

std::string probeGpuEncoder(RenderCodecFamily family) {
    auto codecs = ffmpeg::events::Recorder::getAvailableCodecs();
    if (family == RenderCodecFamily::AV1)
        return pickVendorEncoder(codecs, "av1_nvenc", "av1_amf", "av1_qsv");
    if (family == RenderCodecFamily::H265)
        return pickVendorEncoder(codecs, "hevc_nvenc", "hevc_amf", "hevc_qsv");
    if (family == RenderCodecFamily::VP9)
        return pickVendorEncoder(codecs, nullptr, nullptr, "vp9_qsv");
    if (family == RenderCodecFamily::VP8 || family == RenderCodecFamily::VVC)
        return {};
    return pickVendorEncoder(codecs, "h264_nvenc", "h264_amf", "h264_qsv");
}

static std::vector<std::string> probeEncodersByType(const std::filesystem::path& ffmpegExe, char type) {
#ifdef GEODE_IS_WINDOWS
    if (ffmpegExe.empty()) return {};
    std::wstring cmd = L"\"" + ffmpegExe.wstring() + L"\" -hide_banner -encoders";

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return {};
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE hNull = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
                               &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput   = hNull != INVALID_HANDLE_VALUE ? hNull : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput  = hWrite;
    si.hStdError   = hNull != INVALID_HANDLE_VALUE ? hNull : GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);
    if (!ok) { CloseHandle(hRead); return {}; }

    std::string output;
    char buf[4096];
    DWORD n;
    while (ReadFile(hRead, buf, sizeof(buf) - 1, &n, nullptr) && n > 0) {
        buf[n] = '\0';
        output += buf;
    }
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::vector<std::string> result;
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 8 || line[0] != ' ' || line[1] != type) continue;
        size_t pos = 8;
        while (pos < line.size() && line[pos] == ' ') ++pos;
        if (pos >= line.size() || !std::islower(static_cast<unsigned char>(line[pos]))) continue;
        size_t end = pos;
        while (end < line.size() && line[end] != ' ') ++end;
        result.emplace_back(line.substr(pos, end - pos));
    }
    return result;
#else
    (void)type;
    return {};
#endif
}

std::vector<std::string> probeAudioCodecs(const std::filesystem::path& ffmpegExe) {
    return probeEncodersByType(ffmpegExe, 'A');
}

std::vector<std::string> probeVideoCodecs(const std::filesystem::path& ffmpegExe) {
    return probeEncodersByType(ffmpegExe, 'V');
}

static std::string buildEncoderTuning(const std::string& codec, RenderQualityTier tier, bool preferSpeed) {
    int t = static_cast<int>(tier);
    if (codec.find("nvenc") != std::string::npos) {
        if (t <= 0) return "";
        if (t == 1) return preferSpeed ? "-rc-lookahead 8 -spatial_aq 1"
                                       : "-rc-lookahead 20 -spatial_aq 1";
        return preferSpeed ? "-rc-lookahead 20 -spatial_aq 1 -temporal_aq 1 -multipass qres"
                           : "-rc-lookahead 32 -spatial_aq 1 -temporal_aq 1 -multipass fullres";
    }
    if (codec.find("amf") != std::string::npos) {
        if (t <= 0) return "-quality speed";
        if (t == 1) return preferSpeed ? "-quality speed" : "-quality balanced";
        return preferSpeed ? "-quality balanced" : "-quality quality -preanalysis 1";
    }
    if (codec.find("qsv") != std::string::npos) {
        if (t <= 0) return "-preset veryfast";
        if (t == 1) return preferSpeed ? "-preset faster" : "-preset medium";
        return preferSpeed ? "-preset faster" : "-preset veryslow";
    }
    return "";
}

static std::string fasterPreset(RenderCodecFamily family, const std::string& preset) {
    if (family == RenderCodecFamily::AV1) {
        int p = 0;
        auto res = std::from_chars(preset.data(), preset.data() + preset.size(), p);
        if (res.ec != std::errc{}) return preset;
        return std::to_string(std::min(p + 2, 12));
    }
    if (family == RenderCodecFamily::VP8 || family == RenderCodecFamily::VP9) {
        // libvpx preset is the -cpu-used value: higher = faster. VP8 tops out at 5
        // under -deadline good; VP9 allows up to 8.
        int p = 0;
        auto res = std::from_chars(preset.data(), preset.data() + preset.size(), p);
        if (res.ec != std::errc{}) return preset;
        int cap = family == RenderCodecFamily::VP8 ? 5 : 8;
        return std::to_string(std::min(p + 1, cap));
    }
    if (family == RenderCodecFamily::VVC) {
        static constexpr const char* ladder[] = {
            "faster", "fast", "medium", "slow", "slower",
        };
        for (size_t i = 0; i < std::size(ladder); ++i)
            if (preset == ladder[i])
                return ladder[i == 0 ? 0 : i - 1];
        return preset;
    }
    static constexpr const char* ladder[] = {
        "ultrafast", "superfast", "veryfast", "faster", "fast",
        "medium", "slow", "slower", "veryslow",
    };
    for (size_t i = 0; i < std::size(ladder); ++i)
        if (preset == ladder[i])
            return ladder[i == 0 ? 0 : i - 1];
    return preset;
}

ResolvedEncodeParams resolve(const RenderConfig& cfg) {
    struct TierParams {
        const char* cpuCodec;
        const char* cpuPreset;
        const char* nvencPreset;
        int         crf;
        int64_t     apiBitrate;
    };
    // indexed by RenderQualityTier: Fast=0, Balanced=1, Quality=2, Lossless=3
    static constexpr TierParams kH264Tiers[] = {
        { "libx264",    "veryfast",  "p1", 28,  20'000'000LL },
        { "libx264",    "medium",    "p4", 20,  30'000'000LL },
        { "libx264",    "slow",      "p7", 16,  50'000'000LL },
        { "libx264rgb", "ultrafast", "p1",  0, 100'000'000LL },
    };
    // AV1 CRF: 0-63 scale (libsvtav1). Lossless falls back to libx264rgb (AV1 lossless is impractical)
    static constexpr TierParams kAv1Tiers[] = {
        { "libsvtav1",  "10",        "p1", 50,  20'000'000LL },
        { "libsvtav1",  "8",         "p4", 35,  30'000'000LL },
        { "libsvtav1",  "5",         "p7", 22,  50'000'000LL },
        { "libx264rgb", "ultrafast", "p1",  0, 100'000'000LL },
    };
    // H.265/HEVC: libx265 shares x264 preset names; CRF scale ~0-51 (default 28).
    // Slightly higher CRF than the H.264 tiers because HEVC is more efficient, so files
    // stay reasonable at equivalent quality. Lossless falls back to libx264rgb.
    static constexpr TierParams kH265Tiers[] = {
        { "libx265",    "veryfast",  "p1", 28,  15'000'000LL },
        { "libx265",    "medium",    "p4", 23,  22'000'000LL },
        { "libx265",    "slow",      "p7", 19,  40'000'000LL },
        { "libx264rgb", "ultrafast", "p1",  0, 100'000'000LL },
    };
    // VP9 (libvpx-vp9): constant-quality via -crf + -b:v 0 (CRF 0-63, lower = better).
    // cpuPreset holds the -cpu-used value (higher = faster). vp9_qsv (Intel) uses the
    // shared QSV path. nvencPreset unused. Lossless falls back to libx264rgb.
    static constexpr TierParams kVp9Tiers[] = {
        { "libvpx-vp9", "5",         "p1", 36,  15'000'000LL },
        { "libvpx-vp9", "2",         "p4", 31,  22'000'000LL },
        { "libvpx-vp9", "1",         "p7", 24,  40'000'000LL },
        { "libx264rgb", "ultrafast", "p1",  0, 100'000'000LL },
    };
    // VP8 (libvpx): CRF 4-63 with -b:v 0; cpuPreset is -cpu-used (0-5 under -deadline good).
    // No GPU encoder. Lossless falls back to libx264rgb.
    static constexpr TierParams kVp8Tiers[] = {
        { "libvpx",     "4",         "p1", 30,  18'000'000LL },
        { "libvpx",     "2",         "p4", 22,  28'000'000LL },
        { "libvpx",     "1",         "p7", 14,  45'000'000LL },
        { "libx264rgb", "ultrafast", "p1",  0, 100'000'000LL },
    };
    // H.266/VVC (libvvenc): quality via -qp (0-63, lower = better); cpuPreset is the named
    // libvvenc preset. CPU only, very slow. Lossless falls back to libx264rgb.
    static constexpr TierParams kVvcTiers[] = {
        { "libvvenc",   "faster",    "p1", 38,  10'000'000LL },
        { "libvvenc",   "medium",    "p4", 32,  15'000'000LL },
        { "libvvenc",   "slow",      "p7", 26,  25'000'000LL },
        { "libx264rgb", "ultrafast", "p1",  0, 100'000'000LL },
    };

    bool isAv1 = cfg.codecFamily == RenderCodecFamily::AV1;
    bool isVp8 = cfg.codecFamily == RenderCodecFamily::VP8;
    bool isVp9 = cfg.codecFamily == RenderCodecFamily::VP9;
    bool isVvc = cfg.codecFamily == RenderCodecFamily::VVC;
    const TierParams* tierArray =
        cfg.codecFamily == RenderCodecFamily::AV1  ? kAv1Tiers  :
        cfg.codecFamily == RenderCodecFamily::H265 ? kH265Tiers :
        cfg.codecFamily == RenderCodecFamily::VP9  ? kVp9Tiers  :
        cfg.codecFamily == RenderCodecFamily::VP8  ? kVp8Tiers  :
        cfg.codecFamily == RenderCodecFamily::VVC  ? kVvcTiers  :
                                                     kH264Tiers;
    auto tierIdx = static_cast<size_t>(std::clamp(static_cast<int>(cfg.tier), 0,
                                                   static_cast<int>(RenderQualityTier::Lossless)));
    auto const& td = tierArray[tierIdx];

    bool isLossless = cfg.tier == RenderQualityTier::Lossless;
    bool useGpu     = cfg.useGpu && !cfg.gpuEncoder.empty() && !isLossless;

    ResolvedEncodeParams r;
    r.codec      = cfg.codec.value_or(useGpu ? cfg.gpuEncoder : td.cpuCodec);
    r.tuning     = buildEncoderTuning(r.codec, cfg.tier, cfg.preferSpeed && !isLossless);
    r.crf        = cfg.crf.value_or(td.crf);
    r.extraArgs  = cfg.extraArgs.value_or(isLossless ? "-pix_fmt rgb24" : "-pix_fmt yuv420p");

    // colorspace: an explicit non-default videoArgs override always wins. Otherwise
    // Color Fix tags the stream BT.709 so players don't misread the matrix (the actual
    // RGB->YCbCr is already BT.709 in both the NV12 shader and the RGB path). On top of
    // that, qualityColorspace=accurate adds the full ffmpeg colorspace conversion filter
    // for the CPU/RGB path (the NV12 path drops it); fast leaves tagging-only. Lossless
    // never touches color.
    bool userFilter = cfg.videoArgs.has_value()
        && !cfg.videoArgs->empty()
        && *cfg.videoArgs != kDefaultVideoArgs;
    r.colorFix = cfg.colorFix;
    if (isLossless) {
        r.videoArgs = cfg.videoArgs.value_or("");
        r.colorTags = "";
    } else if (userFilter) {
        r.videoArgs = *cfg.videoArgs;
        r.colorTags = "";
    } else {
        r.videoArgs = cfg.qualityColorspace ? kDefaultVideoArgs : "";
        r.colorTags = cfg.colorFix ? kFastColorTags : "";
    }

    r.audioArgs  = cfg.audioArgs.value_or("");
    r.audioCodec = cfg.audioCodec.value_or("aac");

    bool rIsNvenc = r.codec.find("nvenc") != std::string::npos;
    bool rIsGpu   = rIsNvenc
                 || r.codec.find("amf")   != std::string::npos
                 || r.codec.find("qsv")   != std::string::npos;
    bool rIsAv1   = r.codec.find("av1")   != std::string::npos;
    bool rIsVp9   = r.codec.find("vp9")   != std::string::npos;
    bool rIsVp8   = r.codec == "libvpx";
    bool rIsVvc   = r.codec.find("vvenc") != std::string::npos;

    bool noBitrateCap = rIsAv1 || rIsVp8 || rIsVp9 || rIsVvc;
    r.maxBitrate = (noBitrateCap || isLossless) ? "" : cfg.maxBitrate.value_or("");
    if (!r.maxBitrate.empty() && std::all_of(r.maxBitrate.begin(), r.maxBitrate.end(),
            [](unsigned char c) { return std::isdigit(c); }))
        r.maxBitrate += "M";

    r.ext = cfg.ext.value_or(".mp4");
    if (r.ext == ".mp4") {
        for (const char* c : { "libopus", "opus", "libvorbis", "vorbis", "flac" })
            if (r.audioCodec == c) { r.ext = ".mkv"; break; }
    }

    if ((rIsVp8 || rIsVp9) && (r.ext == ".mp4" || r.ext == ".mov" || r.ext == ".m4v"))
        r.ext = ".mkv";
    r.apiBitrate = td.apiBitrate;

    // NVENC uses -preset pN; CPU x264/svtav1 uses -preset name; AMF/QSV have no standard flag
    if (rIsNvenc)
        r.x264Preset = td.nvencPreset;
    else if (!rIsGpu) {
        r.x264Preset = td.cpuPreset;
        if (cfg.preferSpeed && !isLossless)
            r.x264Preset = fasterPreset(cfg.codecFamily, r.x264Preset);
    }

    return r;
}

void saveRenderConfig(const RenderConfig& cfg) {
    auto* mod = Mod::get();
    mod->setSavedValue("exp_render_tier",               static_cast<int64_t>(cfg.tier));
    mod->setSavedValue("exp_render_use_gpu",            cfg.useGpu);
    mod->setSavedValue("exp_render_gpu_encoder",        cfg.gpuEncoder);
    mod->setSavedValue("exp_render_width",              static_cast<int64_t>(cfg.width));
    mod->setSavedValue("exp_render_height",             static_cast<int64_t>(cfg.height));
    mod->setSavedValue("exp_render_fps",                static_cast<int64_t>(cfg.fps));
    mod->setSavedValue("exp_render_include_audio",      cfg.includeAudio);
    mod->setSavedValue("exp_render_include_clicks",     cfg.includeClicks);
    mod->setSavedValue("exp_render_music_vol",          static_cast<double>(cfg.musicVol));
    mod->setSavedValue("exp_render_sfx_vol",            static_cast<double>(cfg.sfxVol));
    mod->setSavedValue("exp_render_seconds_after",      static_cast<double>(cfg.secondsAfter));
    mod->setSavedValue("exp_render_hide_endscreen",     cfg.hideEndscreen);
    mod->setSavedValue("exp_render_hide_levelcomplete", cfg.hideLevelComplete);
    mod->setSavedValue("exp_render_quality_colorspace", cfg.qualityColorspace);
    mod->setSavedValue("exp_render_color_fix",          cfg.colorFix);
    mod->setSavedValue("exp_render_prefer_speed",       cfg.preferSpeed);
    mod->setSavedValue("exp_render_adv_codec",          cfg.codec.value_or(""));
    mod->setSavedValue("exp_render_adv_crf",            static_cast<int64_t>(cfg.crf.value_or(-1)));
    mod->setSavedValue("exp_render_adv_max_bitrate",    cfg.maxBitrate.value_or(""));
    mod->setSavedValue("exp_render_adv_extra_args",     cfg.extraArgs.value_or(""));
    mod->setSavedValue("exp_render_adv_video_args",     cfg.videoArgs.value_or(""));
    mod->setSavedValue("exp_render_adv_audio_args",     cfg.audioArgs.value_or(""));
    mod->setSavedValue("exp_render_adv_audio_codec",    cfg.audioCodec.value_or(""));
    mod->setSavedValue("exp_render_adv_ext",            cfg.ext.value_or(""));
    mod->setSavedValue("exp_render_codec_family",       static_cast<int64_t>(cfg.codecFamily));
}

RenderConfig loadRenderConfig() {
    auto* mod = Mod::get();
    RenderConfig cfg;

    int tier      = static_cast<int>(mod->getSavedValue<int64_t>("exp_render_tier", 1));
    cfg.tier      = static_cast<RenderQualityTier>(std::clamp(tier, 0, 3));
    cfg.useGpu    = mod->getSavedValue<bool>("exp_render_use_gpu", true);
    cfg.gpuEncoder = mod->getSavedValue<std::string>("exp_render_gpu_encoder", "");
    int family = static_cast<int>(mod->getSavedValue<int64_t>("exp_render_codec_family", 0));
    cfg.codecFamily = (family == 1) ? RenderCodecFamily::AV1
                    : (family == 2) ? RenderCodecFamily::H265
                    : (family == 3) ? RenderCodecFamily::VP9
                    : (family == 4) ? RenderCodecFamily::VP8
                    : (family == 5) ? RenderCodecFamily::VVC
                                    : RenderCodecFamily::H264;
    cfg.width     = static_cast<unsigned>(mod->getSavedValue<int64_t>("exp_render_width", 1920));
    cfg.height    = static_cast<unsigned>(mod->getSavedValue<int64_t>("exp_render_height", 1080));
    cfg.fps       = static_cast<unsigned>(mod->getSavedValue<int64_t>("exp_render_fps", 60));
    cfg.includeAudio    = mod->getSavedValue<bool>("exp_render_include_audio", true);
    cfg.includeClicks   = mod->getSavedValue<bool>("exp_render_include_clicks", false);
    cfg.musicVol        = static_cast<float>(mod->getSavedValue<double>("exp_render_music_vol", 1.0));
    cfg.sfxVol          = static_cast<float>(mod->getSavedValue<double>("exp_render_sfx_vol", 1.0));
    cfg.secondsAfter    = static_cast<float>(mod->getSavedValue<double>("exp_render_seconds_after", 3.0));
    cfg.hideEndscreen   = mod->getSavedValue<bool>("exp_render_hide_endscreen", false);
    cfg.hideLevelComplete = mod->getSavedValue<bool>("exp_render_hide_levelcomplete", false);
    cfg.qualityColorspace = mod->getSavedValue<bool>("exp_render_quality_colorspace", true);
    cfg.colorFix          = mod->getSavedValue<bool>("exp_render_color_fix", true);
    cfg.preferSpeed       = mod->getSavedValue<bool>("exp_render_prefer_speed", false);

    auto advCodec = mod->getSavedValue<std::string>("exp_render_adv_codec", "");
    if (!advCodec.empty()) cfg.codec = advCodec;

    int64_t advCrf = mod->getSavedValue<int64_t>("exp_render_adv_crf", -1);
    if (advCrf >= 0) cfg.crf = static_cast<int>(advCrf);

    auto advMaxBitrate = mod->getSavedValue<std::string>("exp_render_adv_max_bitrate", "");
    if (!advMaxBitrate.empty()) cfg.maxBitrate = advMaxBitrate;

    auto advExtraArgs = mod->getSavedValue<std::string>("exp_render_adv_extra_args", "");
    if (!advExtraArgs.empty()) cfg.extraArgs = advExtraArgs;

    auto advVideoArgs = mod->getSavedValue<std::string>("exp_render_adv_video_args", "");
    if (!advVideoArgs.empty()) cfg.videoArgs = advVideoArgs;

    auto advAudioArgs = mod->getSavedValue<std::string>("exp_render_adv_audio_args", "");
    if (!advAudioArgs.empty()) cfg.audioArgs = advAudioArgs;

    auto advAudioCodec = mod->getSavedValue<std::string>("exp_render_adv_audio_codec", "");
    if (!advAudioCodec.empty()) cfg.audioCodec = advAudioCodec;

    auto advExt = mod->getSavedValue<std::string>("exp_render_adv_ext", "");
    if (!advExt.empty()) cfg.ext = advExt;

    return cfg;
}
