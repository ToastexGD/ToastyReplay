#ifndef TOASTYREPLAY_RENDER_WATERMARK_BRIDGE_HPP
#define TOASTYREPLAY_RENDER_WATERMARK_BRIDGE_HPP

#include <imgui-cocos.hpp>

#include <cstdint>

namespace toasty::watermark {

inline constexpr std::uint32_t kPlaybackWatermarkAbiVersion = 1;

struct PlaybackWatermarkFrame final {
    std::uint32_t abiVersion = kPlaybackWatermarkAbiVersion;
    std::uint32_t structSize = 0;
    ImDrawList* drawList = nullptr;
    ImFont* font = nullptr;
    float fontSize = 0.0f;
    ImVec2 displaySize = ImVec2(0.0f, 0.0f);
    ImVec4 accent = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    float alpha = 0.6f;
    bool menuVisible = false;
    bool playbackVisible = false;
    const char* productName = "";
    const char* versionText = "";
    const char* editionLabel = "";
};

void drawPlaybackWatermark(PlaybackWatermarkFrame const& frame);

}

#endif
