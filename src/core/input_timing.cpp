#include "core/input_timing.hpp"
#include "ToastyReplay.hpp"
#include "hacks/autoclicker.hpp"

#include <Geode/modify/CCTouchDispatcher.hpp>

#include <chrono>

using namespace geode::prelude;

namespace {
    using Clock = std::chrono::steady_clock;
}

uint64_t InputTiming::nowMicros() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count()
    );
}

void InputTiming::queueTimestamp(uint64_t micros) {
    auto* engine = ReplayEngine::get();
    if (!engine || engine->selectedAccuracyMode != AccuracyMode::CBF || !PlayLayer::get()) {
        return;
    }

    engine->queueRawInputTimestamp(micros);
}

void InputTiming::queueCurrentTimestamp() {
    queueTimestamp(nowMicros());
}

class $modify(TimedTouchDispatcher, CCTouchDispatcher) {
    void touches(cocos2d::CCSet* touches, cocos2d::CCEvent* event, unsigned int index) {
        InputTiming::queueCurrentTimestamp();
        if (PlayLayer::get()) {
            Autoclicker::get()->trackUserInput(index == 0, false);
        }
        CCTouchDispatcher::touches(touches, event, index);
    }
};
