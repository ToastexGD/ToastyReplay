#ifndef TOASTYREPLAY_RENDER_WATERMARK_HASH_EMBEDDED_HPP
#define TOASTYREPLAY_RENDER_WATERMARK_HASH_EMBEDDED_HPP
namespace toasty::watermark::embedded {

inline constexpr char const* kWatermarkDllSha256 =
#if defined(TOASTYREPLAY_WATERMARK_DLL_SHA256)
    TOASTYREPLAY_WATERMARK_DLL_SHA256
#else
    "PLACEHOLDER_NO_HASH_BUILD_WITH_CI"
#endif
;

} // namespace toasty::watermark::embedded

#endif