#include "hacks/autoclicker.hpp"

#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <cmath>

namespace {
    constexpr double kMinTimedPhaseSeconds = 1e-7;

    double clampTimedCps(float cps) {
        if (!std::isfinite(cps)) {
            return 1000.0;
        }
        return std::clamp(static_cast<double>(cps), 1.0, 20000.0);
    }

    double clampHoldRatio(float ratio) {
        if (!std::isfinite(ratio)) {
            return 0.5;
        }
        return std::clamp(static_cast<double>(ratio), 0.05, 0.95);
    }
}

Autoclicker* Autoclicker::get() {
    static Autoclicker instance;
    return &instance;
}

void Autoclicker::reset() {
    tickCounterP1 = 0;
    tickCounterP2 = 0;
    currentlyHoldingP1 = false;
    currentlyHoldingP2 = false;
    timeUntilNextEdgeP1 = 0.5 / clampTimedCps(targetCps);
    timeUntilNextEdgeP2 = timeUntilNextEdgeP1;
}

void Autoclicker::trackUserInput(bool pressed, bool isPlayer2) {
    if (isPlayer2)
        userHoldingP2 = pressed;
    else
        userHoldingP1 = pressed;
}

Autoclicker::TickResult Autoclicker::processTick() {
    TickResult result;
    if (!enabled) return result;

    auto processPlayer = [&](bool userHolding, int& counter, bool& holding, bool& fire, bool& press) {
        if (onlyWhileHolding && !userHolding) {
            if (holding) {
                fire = true;
                press = false;
                holding = false;
            }
            counter = 0;
            return;
        }

        counter++;

        if (holding) {
            if (counter >= holdTicks) {
                fire = true;
                press = false;
                holding = false;
                counter = 0;
            }
        } else {
            if (counter >= releaseTicks) {
                fire = true;
                press = true;
                holding = true;
                counter = 0;
            }
        }
    };

    if (player1) processPlayer(userHoldingP1, tickCounterP1, currentlyHoldingP1, result.p1Fire, result.p1Press);
    if (player2) processPlayer(userHoldingP2, tickCounterP2, currentlyHoldingP2, result.p2Fire, result.p2Press);

    return result;
}

std::vector<TimedAutoclickerAction> Autoclicker::buildTimedTickActions(double tickRate) {
    std::vector<TimedAutoclickerAction> actions;
    if (!enabled || !isTimedMode() || tickRate <= 0.0) {
        return actions;
    }

    double tickDuration = 1.0 / std::max(1.0, tickRate);
    double cycleDuration = 1.0 / clampTimedCps(targetCps);
    double ratio = clampHoldRatio(holdRatio);
    double holdDuration = std::max(kMinTimedPhaseSeconds, cycleDuration * ratio);
    double releaseDuration = std::max(kMinTimedPhaseSeconds, cycleDuration * (1.0 - ratio));

    auto processPlayer = [&](bool enabledForPlayer, bool userHolding, bool& holding, double& timeUntilNextEdge, bool isPlayer2) {
        if (!enabledForPlayer) {
            if (holding) {
                actions.push_back({ 0.0f, isPlayer2, false });
                holding = false;
            }
            timeUntilNextEdge = releaseDuration;
            return;
        }

        if (onlyWhileHolding && !userHolding) {
            if (holding) {
                actions.push_back({ 0.0f, isPlayer2, false });
                holding = false;
            }
            timeUntilNextEdge = releaseDuration;
            return;
        }

        if (timeUntilNextEdge <= 0.0 || !std::isfinite(timeUntilNextEdge)) {
            timeUntilNextEdge = holding ? holdDuration : releaseDuration;
        }

        double elapsed = 0.0;
        double remaining = tickDuration;
        int safetyCounter = 0;

        while (remaining + kMinTimedPhaseSeconds >= timeUntilNextEdge && safetyCounter < 8192) {
            elapsed += timeUntilNextEdge;
            remaining -= timeUntilNextEdge;

            bool press = !holding;
            holding = press;
            actions.push_back({
                static_cast<float>(std::clamp(elapsed / tickDuration, 0.0, 1.0)),
                isPlayer2,
                press
            });

            timeUntilNextEdge = holding ? holdDuration : releaseDuration;
            ++safetyCounter;
        }

        timeUntilNextEdge = std::max(kMinTimedPhaseSeconds, timeUntilNextEdge - remaining);
    };

    processPlayer(player1, userHoldingP1, currentlyHoldingP1, timeUntilNextEdgeP1, false);
    processPlayer(player2, userHoldingP2, currentlyHoldingP2, timeUntilNextEdgeP2, true);

    std::stable_sort(actions.begin(), actions.end(), [](TimedAutoclickerAction const& lhs, TimedAutoclickerAction const& rhs) {
        return lhs.offset < rhs.offset;
    });
    return actions;
}

TimedAutoclickerPlayerState Autoclicker::captureTimedPlayerState(bool isPlayer2) const {
    if (isPlayer2) {
        return { currentlyHoldingP2, userHoldingP2, timeUntilNextEdgeP2 };
    }
    return { currentlyHoldingP1, userHoldingP1, timeUntilNextEdgeP1 };
}

void Autoclicker::restoreTimedPlayerState(bool isPlayer2, TimedAutoclickerPlayerState const& state) {
    if (isPlayer2) {
        currentlyHoldingP2 = state.holding;
        userHoldingP2 = state.userHolding;
        timeUntilNextEdgeP2 = state.timeUntilNextEdge;
    } else {
        currentlyHoldingP1 = state.holding;
        userHoldingP1 = state.userHolding;
        timeUntilNextEdgeP1 = state.timeUntilNextEdge;
    }
}

bool Autoclicker::isTimedMode() const {
    return mode == AutoclickerMode::Timed;
}

float Autoclicker::legacyClicksPerSecond(double tickRate) const {
    double denominator = std::max(1.0, static_cast<double>(holdTicks + releaseTicks));
    return static_cast<float>(std::max(1.0, tickRate) / denominator);
}

float Autoclicker::timedClicksPerSecond() const {
    return static_cast<float>(clampTimedCps(targetCps));
}

class $modify(AutoclickerPlayLayer, PlayLayer) {
    void resetLevel() {
        Autoclicker::get()->reset();
        PlayLayer::resetLevel();
    }

    void resetLevelFromStart() {
        Autoclicker::get()->reset();
        PlayLayer::resetLevelFromStart();
    }
};
