#pragma once

#include <vector>

enum class AutoclickerMode : int {
    Legacy = 0,
    Timed = 1,
};

inline AutoclickerMode sanitizeAutoclickerMode(int rawMode) {
    switch (rawMode) {
        case static_cast<int>(AutoclickerMode::Timed):
            return AutoclickerMode::Timed;
        default:
            return AutoclickerMode::Legacy;
    }
}

struct TimedAutoclickerAction {
    float offset = 0.0f;
    bool player2 = false;
    bool pressed = false;
};

struct TimedAutoclickerPlayerState {
    bool holding = false;
    bool userHolding = false;
    double timeUntilNextEdge = 0.0;
};

struct Autoclicker {
    static Autoclicker* get();

    bool enabled = false;
    bool player1 = true;
    bool player2 = false;
    AutoclickerMode mode = AutoclickerMode::Legacy;
    int holdTicks = 1;
    int releaseTicks = 1;
    float targetCps = 1000.0f;
    float holdRatio = 0.5f;
    bool onlyWhileHolding = false;

    int tickCounterP1 = 0;
    int tickCounterP2 = 0;
    bool currentlyHoldingP1 = false;
    bool currentlyHoldingP2 = false;
    bool userHoldingP1 = false;
    bool userHoldingP2 = false;
    bool isAutoclickerInput = false;

    struct TickResult {
        bool p1Fire = false;
        bool p1Press = false;
        bool p2Fire = false;
        bool p2Press = false;
    };

    TickResult processTick();
    std::vector<TimedAutoclickerAction> buildTimedTickActions(double tickRate);
    void reset();
    void trackUserInput(bool pressed, bool isPlayer2);
    TimedAutoclickerPlayerState captureTimedPlayerState(bool isPlayer2) const;
    void restoreTimedPlayerState(bool isPlayer2, TimedAutoclickerPlayerState const& state);
    bool isTimedMode() const;
    float legacyClicksPerSecond(double tickRate) const;
    float timedClicksPerSecond() const;

private:
    double timeUntilNextEdgeP1 = 0.0;
    double timeUntilNextEdgeP2 = 0.0;
};
