#ifndef _frame_editor_commit_model_hpp
#define _frame_editor_commit_model_hpp

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace toasty::frame_editor {

inline bool parseNonNegativeFrameText(char const* text, int32_t& out) {
    if (!text) return false;
    while (*text == ' ' || *text == '\t') ++text;
    if (*text == '\0') return false;

    errno = 0;
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (text == end || errno == ERANGE) return false;
    while (end && (*end == ' ' || *end == '\t')) ++end;
    if (end && *end != '\0') return false;

    out = static_cast<int32_t>(std::clamp<long>(value, 0, std::numeric_limits<int32_t>::max()));
    return true;
}

template <class Input, class Segment>
bool commitSelectedStartFrame(std::vector<Input>& inputs, Segment const& segment, int32_t frame) {
    if (segment.pressIndex >= inputs.size()) return false;

    frame = std::max<int32_t>(0, frame);
    int32_t oldFrame = inputs[segment.pressIndex].frame;
    int32_t delta = frame - oldFrame;
    if (delta == 0) return false;

    inputs[segment.pressIndex].frame = frame;
    if (segment.hasRelease && segment.releaseIndex != segment.pressIndex && segment.releaseIndex < inputs.size()) {
        inputs[segment.releaseIndex].frame = std::max<int32_t>(
            frame + 1,
            inputs[segment.releaseIndex].frame + delta
        );
    }
    return true;
}

template <class Input, class Segment>
bool commitSelectedEndFrame(std::vector<Input>& inputs, Segment const& segment, int32_t frame) {
    if (!segment.hasRelease || segment.releaseIndex == segment.pressIndex ||
        segment.releaseIndex >= inputs.size() || segment.pressIndex >= inputs.size()) {
        return false;
    }

    frame = std::max<int32_t>(inputs[segment.pressIndex].frame + 1, frame);
    if (inputs[segment.releaseIndex].frame == frame) return false;

    inputs[segment.releaseIndex].frame = frame;
    return true;
}

template <class Input, class Segment>
bool commitSelectedDuration(std::vector<Input>& inputs, Segment const& segment, int32_t duration) {
    if (segment.pressIndex >= inputs.size()) return false;
    return commitSelectedEndFrame(inputs, segment, inputs[segment.pressIndex].frame + std::max<int32_t>(1, duration));
}

template <class Input, class Segment>
bool commitSelectedStartFrameText(std::vector<Input>& inputs, Segment const& segment, char const* text) {
    int32_t frame = 0;
    if (!parseNonNegativeFrameText(text, frame)) return false;
    return commitSelectedStartFrame(inputs, segment, frame);
}

template <class Input, class Segment>
bool commitSelectedEndFrameText(std::vector<Input>& inputs, Segment const& segment, char const* text) {
    int32_t frame = 0;
    if (!parseNonNegativeFrameText(text, frame)) return false;
    return commitSelectedEndFrame(inputs, segment, frame);
}

template <class Input, class Segment>
bool commitSelectedDurationText(std::vector<Input>& inputs, Segment const& segment, char const* text) {
    int32_t duration = 0;
    if (!parseNonNegativeFrameText(text, duration)) return false;
    return commitSelectedDuration(inputs, segment, duration);
}

}

#endif
