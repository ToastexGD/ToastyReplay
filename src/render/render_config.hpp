#pragma once

#include <cstdint>
#include <optional>
#include <string>

enum class RenderCodecFamily { H264, AV1 };
enum class RenderQualityTier { Fast, Balanced, Quality, Lossless };

inline constexpr const char* kDefaultVideoArgs = "colorspace=all=bt709:iall=bt470bg:fast=1";
inline constexpr const char* kFastColorTags = "-colorspace bt709 -color_primaries bt709 -color_trc bt709";

struct RenderConfig {
    RenderQualityTier tier      = RenderQualityTier::Balanced;
    bool              useGpu    = true;
    std::string       gpuEncoder;  // "h264_nvenc", "h264_amf", "h264_qsv", or ""
    RenderCodecFamily codecFamily = RenderCodecFamily::H264;

    unsigned width          = 1920;
    unsigned height         = 1080;
    unsigned fps            = 60;

    bool  includeAudio      = true;
    bool  includeClicks     = false;
    float musicVol          = 1.f;
    float sfxVol            = 1.f;
    float secondsAfter      = 3.f;
    bool  hideEndscreen     = false;
    bool  hideLevelComplete = false;
    bool  qualityColorspace = true;

    // nullopt = use tier defaults
    std::optional<std::string> codec;
    std::optional<int>         crf;
    std::optional<std::string> maxBitrate;
    std::optional<std::string> extraArgs;
    std::optional<std::string> videoArgs;
    std::optional<std::string> audioArgs;
    std::optional<std::string> audioCodec;  // nullopt = "aac"
    std::optional<std::string> ext;
};

struct ResolvedEncodeParams {
    std::string codec;
    int         crf;
    std::string x264Preset;
    std::string extraArgs;
    std::string videoArgs;   // -vf filter chain (no vflip; flip is done in capture)
    std::string colorTags;   // output color-metadata flags for fast colorspace mode
    std::string audioArgs;
    std::string audioCodec;  // resolved value, always "aac" or user-specified
    std::string ext;
    std::string maxBitrate;  // empty = no cap
    int64_t     apiBitrate;  // bps for API path
};

// Probes available codecs and returns the first detected GPU encoder, or "".
std::string          probeGpuEncoder(RenderCodecFamily family = RenderCodecFamily::H264);
ResolvedEncodeParams resolve(const RenderConfig&);
void                 saveRenderConfig(const RenderConfig&);  // writes exp_render_* keys
RenderConfig         loadRenderConfig();
