#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

enum class RenderCodecFamily { H264, AV1, H265, VP9, VP8, VVC };
enum class RenderQualityTier { Fast, Balanced, Quality, Lossless };

inline constexpr const char* kDefaultVideoArgs = "colorspace=all=bt709:iall=bt470bg:fast=1";
inline constexpr const char* kFastColorTags = "-colorspace bt709 -color_primaries bt709 -color_trc bt709";
struct RenderConfig {
    RenderQualityTier tier      = RenderQualityTier::Balanced;
    bool              useGpu    = true;
    std::string       gpuEncoder;
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
    bool  colorFix          = true;
    bool  preferSpeed       = false;

    std::optional<std::string> codec;
    std::optional<int>         crf;
    std::optional<std::string> maxBitrate;
    std::optional<std::string> extraArgs;
    std::optional<std::string> videoArgs;
    std::optional<std::string> audioArgs;
    std::optional<std::string> audioCodec;
    std::optional<std::string> ext;
};

struct ResolvedEncodeParams {
    std::string codec;
    int         crf;
    std::string x264Preset;
    std::string extraArgs;
    std::string tuning;
    std::string videoArgs;
    std::string colorTags;
    bool        colorFix = true;
    std::string audioArgs;
    std::string audioCodec;
    std::string ext;
    std::string maxBitrate;
    int64_t     apiBitrate;
};

std::string              probeGpuEncoder(RenderCodecFamily family = RenderCodecFamily::H264);
std::vector<std::string> probeAudioCodecs(const std::filesystem::path& ffmpegExe);
std::vector<std::string> probeVideoCodecs(const std::filesystem::path& ffmpegExe);
ResolvedEncodeParams     resolve(const RenderConfig&);
void                     saveRenderConfig(const RenderConfig&);
RenderConfig             loadRenderConfig();
