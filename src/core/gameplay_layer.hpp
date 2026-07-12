#ifndef _toasty_gameplay_layer_hpp
#define _toasty_gameplay_layer_hpp

#ifdef TOASTYREPLAY_GAMEPLAY_LAYER_TEST
class PlayLayer;
class LevelEditorLayer;
#else
#include <Geode/Bindings.hpp>
#endif

namespace toasty::gameplay {
enum class Mode {
    None,
    PlayLayerActive,
    EditorBuildMode,
    EditorPlaytest,
};

namespace detail {
#ifdef TOASTYREPLAY_GAMEPLAY_LAYER_TEST
    extern PlayLayer* (*playLayerGetter)();
    extern LevelEditorLayer* (*editorGetter)();
#else
    inline PlayLayer* playLayerGet() { return PlayLayer::get(); }
    inline LevelEditorLayer* editorGet() { return LevelEditorLayer::get(); }
#endif

    inline bool& editorPlaytestActiveRef() {
        static bool active = false;
        return active;
    }
}

inline void setEditorPlaytestActive(bool active) {
    detail::editorPlaytestActiveRef() = active;
}

inline PlayLayer* asPlayLayer() {
#ifdef TOASTYREPLAY_GAMEPLAY_LAYER_TEST
    return detail::playLayerGetter ? detail::playLayerGetter() : nullptr;
#else
    return detail::playLayerGet();
#endif
}

inline LevelEditorLayer* asEditor() {
#ifdef TOASTYREPLAY_GAMEPLAY_LAYER_TEST
    return detail::editorGetter ? detail::editorGetter() : nullptr;
#else
    return detail::editorGet();
#endif
}

inline Mode mode() {
    if (asPlayLayer()) return Mode::PlayLayerActive;
    if (asEditor()) {
        return detail::editorPlaytestActiveRef() ? Mode::EditorPlaytest : Mode::EditorBuildMode;
    }
    return Mode::None;
}

#ifdef TOASTYREPLAY_GAMEPLAY_LAYER_TEST
inline void* activeLayer() {
    if (auto* play = asPlayLayer()) {
        return play;
    }
    if (auto* editor = asEditor()) {
        return editor;
    }
    return nullptr;
}
#else
inline GJBaseGameLayer* activeLayer() {
    if (auto* play = asPlayLayer()) {
        return static_cast<GJBaseGameLayer*>(play);
    }
    if (auto* editor = asEditor()) {
        return static_cast<GJBaseGameLayer*>(editor);
    }
    return nullptr;
}
#endif

inline bool isGameplayActive() {
    auto m = mode();
    return m == Mode::PlayLayerActive || m == Mode::EditorPlaytest;
}

inline bool isEditorPlaytest() { return mode() == Mode::EditorPlaytest; }
inline bool isEditorBuildMode() { return mode() == Mode::EditorBuildMode; }
}

#endif
