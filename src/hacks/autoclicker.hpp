#pragma once

struct Autoclicker {
    static Autoclicker* get();

    bool enabled = false;
    bool player1 = true;
    bool player2 = false;
    int holdTicks = 1;
    int releaseTicks = 1;
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
    void reset();
    void trackUserInput(bool pressed, bool isPlayer2);
};
