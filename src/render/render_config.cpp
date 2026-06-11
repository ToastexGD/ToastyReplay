#include "render/render_config.hpp"
#include "render/ffmpeg_events.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>

using namespace geode::prelude;

std::string probeGpuEncoder(RenderCodecFamily family) {
    auto codecs = ffmpeg::events::Recorder::getAvailableCodecs();
    if (family == RenderCodecFamily::AV1) {
        for (const char* candidate : { "av1_nvenc", "av1_amf", "av1_qsv" }) {
            if (std::find(codecs.begin(), codecs.end(), std::string(candidate)) != codecs.end())
                return candidate;
        }
        return {};
    }
    for (const char* candidate : { "h264_nvenc", "h264_amf", "h264_qsv" }) {
        if (std::find(codecs.begin(), codecs.end(), std::string(candidate)) != codecs.end())
            return candidate;
    }
    return {};
}

ResolvedEncodeParams resolve(const RenderConfig& cfg) {
    struct TierParams {
        const char* cpuCodec;
        const char* cpuPreset;
        const char* nvencPreset;
        int         crf;
        int64_t     apiBitrate;
    };
    // Indexed by RenderQualityTier: Fast=0, Balanced=1, Quality=2, Lossless=3
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

    bool isAv1 = cfg.codecFamily == RenderCodecFamily::AV1;
    auto const& td = isAv1
        ? kAv1Tiers[static_cast<size_t>(cfg.tier)]
        : kH264Tiers[static_cast<size_t>(cfg.tier)];

    bool isLossless = cfg.tier == RenderQualityTier::Lossless;
    bool useGpu     = cfg.useGpu && !cfg.gpuEncoder.empty() && !isLossless;

    ResolvedEncodeParams r;
    r.codec      = cfg.codec.value_or(useGpu ? cfg.gpuEncoder : td.cpuCodec);
    r.crf        = cfg.crf.value_or(td.crf);
    r.extraArgs  = cfg.extraArgs.value_or(isLossless ? "-pix_fmt rgb24" : "-pix_fmt yuv420p");
    r.videoArgs  = cfg.videoArgs.value_or(isLossless ? "" : "colorspace=all=bt709:iall=bt470bg:fast=1");
    r.audioArgs  = cfg.audioArgs.value_or("");
    r.audioCodec = cfg.audioCodec.value_or("aac");
    r.maxBitrate = (isAv1 || isLossless) ? "" : cfg.maxBitrate.value_or("");

    r.ext = cfg.ext.value_or(".mp4");
    if (r.ext == ".mp4") {
        for (const char* c : { "libopus", "flac" })
            if (r.audioCodec == c) { r.ext = ".mkv"; break; }
    }
    r.apiBitrate = td.apiBitrate;

    // NVENC uses -preset pN; CPU x264/svtav1 uses -preset name; AMF/QSV have no standard flag
    bool isNvenc = useGpu && cfg.gpuEncoder.find("nvenc") != std::string::npos;
    if (isNvenc)
        r.x264Preset = td.nvencPreset;
    else if (!useGpu)
        r.x264Preset = td.cpuPreset;

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
    mod->setSavedValue("exp_render_adv_codec",          cfg.codec.value_or(""));
    mod->setSavedValue("exp_render_adv_crf",            static_cast<int64_t>(cfg.crf.value_or(-1)));
    mod->setSavedValue("exp_render_adv_max_bitrate",    cfg.maxBitrate.value_or(""));
    mod->setSavedValue("exp_render_adv_extra_args",     cfg.extraArgs.value_or(""));
    mod->setSavedValue("exp_render_adv_video_args",     cfg.videoArgs.value_or(""));
    mod->setSavedValue("exp_render_adv_audio_args",      cfg.audioArgs.value_or(""));
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
    cfg.codecFamily = (family == 1) ? RenderCodecFamily::AV1 : RenderCodecFamily::H264;
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
