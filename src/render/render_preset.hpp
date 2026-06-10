#pragma once

#include "render_config.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct RenderPreset {
    std::string name;
    int width = 1920;
    int height = 1080;
    int fps = 60;
    std::string codec;
    std::string bitrate = "30";
    std::string ext = ".mp4";
    std::string extraArgs = "-pix_fmt yuv420p";
    std::string videoArgs = "colorspace=all=bt709:iall=bt470bg:fast=1";
    std::string audioArgs;
    std::string audioCodec;  // empty = "aac"
    float secondsAfter = 3.0f;
    bool includeAudio = true;
    bool includeClicks = false;
    float sfxVol = 1.0f;
    float musicVol = 1.0f;
    bool hideEndscreen = false;
    bool hideLevelComplete = false;

    // Experimental renderer fields — ignored by old render path
    RenderQualityTier tier = RenderQualityTier::Balanced;
    bool useGpu = true;
    RenderCodecFamily codecFamily = RenderCodecFamily::H264;

    RenderConfig toRenderConfig() const;
    static RenderPreset fromRenderConfig(const RenderConfig& cfg, std::string name = {});
};

namespace RenderPresetIO {
    std::filesystem::path pathForName(std::filesystem::path const& presetsDir, std::string const& name);
    bool save(std::filesystem::path const& path, RenderPreset const& preset);
    std::optional<RenderPreset> load(std::filesystem::path const& path);
    std::vector<std::string> listNames(std::filesystem::path const& presetsDir);
}
