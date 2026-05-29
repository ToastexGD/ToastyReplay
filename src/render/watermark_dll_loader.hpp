#ifndef TOASTYREPLAY_RENDER_WATERMARK_DLL_LOADER_HPP
#define TOASTYREPLAY_RENDER_WATERMARK_DLL_LOADER_HPP

#include "render/watermark_bridge.hpp"
#include <string>

namespace toasty::watermark {
bool loadWatermarkDll(std::string& outError);
bool isWatermarkDllReady();

/// Call the DLL's draw function
void callDllDrawPlaybackWatermark(PlaybackWatermarkFrame const& frame);
void unloadWatermarkDll();

} // namespace toasty::watermark

#endif
