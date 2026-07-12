#ifndef TOASTY_HITBOX_OVERLAY_MODEL_HPP
#define TOASTY_HITBOX_OVERLAY_MODEL_HPP

namespace toasty::hitbox_overlay {
inline bool shouldRenderEditorOverlay(bool editorPlaytest, bool hasPlayer) {
    return editorPlaytest && hasPlayer;
}
}

#endif
