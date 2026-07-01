#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

enum class RenderCodecFamily { H264 = 0, AV1 = 1, H265 = 2, VP9 = 3, VP8 = 4, VVC = 5 };
enum class RenderQualityTier { Fast = 0, Balanced = 1, Quality = 2, Lossless = 3 };

enum class GpuVendor { Unknown, Intel, Amd, Nvidia };
void      setDetectedGpuVendor(GpuVendor vendor);
GpuVendor detectedGpuVendor();

inline constexpr const char* kDefaultVideoArgs = "colorspace=all=bt709:iall=bt470bg:fast=1";
inline constexpr const char* kFastColorTags = "-colorspace bt709 -color_primaries bt709 -color_trc bt709";
struct RenderConfig {
    RenderQualityTier tier      = RenderQualityTier::Balanced;
    bool              useGpu    = true;
    std::string       gpuEncoder;  // "h264_nvenc", "h264_amf", "h264_qsv", or "" (apply same with av1)
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
    std::string tuning;
    std::string videoArgs;   // -vf filter chain (no vflip; flip is done in capture)
    std::string colorTags;
    bool        colorFix = true;  // applies in both NV12 and RGB paths
    std::string audioArgs;
    std::string audioCodec;  // nullopt = "aac"
    std::string ext;
    std::string maxBitrate;  // empty = no cap
    int64_t     apiBitrate;  // bps for API path
};

std::string              probeGpuEncoder(RenderCodecFamily family = RenderCodecFamily::H264,
                                          const std::filesystem::path& ffmpegExe = {});
std::vector<std::string> probeAudioCodecs(const std::filesystem::path& ffmpegExe);
std::vector<std::string> probeVideoCodecs(const std::filesystem::path& ffmpegExe);
ResolvedEncodeParams     resolve(const RenderConfig&);
void                     saveRenderConfig(const RenderConfig&);
RenderConfig             loadRenderConfig();
