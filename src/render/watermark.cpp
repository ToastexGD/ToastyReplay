#include "render/watermark_bridge.hpp"

#if defined(TOASTYREPLAY_HAS_PRIVATE_WATERMARK)
#include "toasty_watermark.hpp"
#endif

#include <algorithm>
#include <cfloat>
#include <string>

namespace toasty::watermark {

void drawPlaybackWatermark(PlaybackWatermarkFrame const& frame) {
#if defined(TOASTYREPLAY_HAS_PRIVATE_WATERMARK)
    toastyreplay_private_draw_playback_watermark(&frame);
#elif defined(TOASTYREPLAY_ALLOW_WATERMARK_STUB)
    if (!frame.drawList || !frame.font || frame.displaySize.x <= 0.0f || frame.displaySize.y <= 0.0f) {
        return;
    }

    std::string text = frame.productName ? frame.productName : "ToastyReplay";
    text += " ";
    text += frame.versionText ? frame.versionText : "";
    text += " ";
    text += frame.editionLabel ? frame.editionLabel : "";

    float fontSize = frame.fontSize > 0.0f ? frame.fontSize : frame.font->FontSize;
    ImVec2 textSize = frame.font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
    ImVec2 position(
        (frame.displaySize.x - textSize.x) * 0.5f,
        frame.displaySize.y - 25.0f
    );

    float alpha = std::clamp(frame.alpha, 0.0f, 1.0f);
    frame.drawList->AddText(
        frame.font,
        fontSize,
        position,
        IM_COL32(255, 184, 56, static_cast<int>(alpha * 255.0f)),
        text.c_str()
    );
    return;
#else
    static_cast<void>(frame);
    return;
#endif
}

}
