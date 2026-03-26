#include "ToastyReplay.hpp"
#include "hacks/autoclicker.hpp"
#include "hacks/physicsbypass.hpp"

#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

using namespace geode::prelude;

namespace {
    struct RenderResolutionScope {
        Renderer& renderer;

        explicit RenderResolutionScope(Renderer& activeRenderer)
            : renderer(activeRenderer) {
            renderer.changeRes(false);
        }

        ~RenderResolutionScope() {
            renderer.changeRes(true);
        }
    };

#ifdef GEODE_IS_WINDOWS
    using ExpectedTicksType = uint32_t;

    static ExpectedTicksType g_expectedTicks = 0;
    static geode::Patch* g_expectedTicksPatch = nullptr;
    static bool g_expectedTicksPatchAttempted = false;

    static size_t getModuleSize(uint8_t* base) {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) {
            return 0;
        }

        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) {
            return 0;
        }

        return static_cast<size_t>(nt->OptionalHeader.SizeOfImage);
    }

    template <size_t N>
    static uintptr_t findPattern(uint8_t* base, size_t size, std::array<int, N> const& pattern) {
        if (!base || size < N) {
            return 0;
        }

        for (size_t i = 0; i + N <= size; ++i) {
            bool matched = true;
            for (size_t j = 0; j < N; ++j) {
                if (pattern[j] != -1 && base[i + j] != static_cast<uint8_t>(pattern[j])) {
                    matched = false;
                    break;
                }
            }

            if (matched) {
                return reinterpret_cast<uintptr_t>(base + i);
            }
        }

        return 0;
    }

    static std::vector<uint8_t> buildExpectedTicksPatch(uintptr_t patchAddress) {
        std::vector<uint8_t> bytes;
        bytes.reserve(22);

        bytes.push_back(0x48);
        bytes.push_back(0xB8);

        uint64_t pointerBits = reinterpret_cast<uint64_t>(&g_expectedTicks);
        for (size_t i = 0; i < sizeof(pointerBits); ++i) {
            bytes.push_back(static_cast<uint8_t>((pointerBits >> (i * 8)) & 0xFF));
        }

        bytes.push_back(0x44);
        bytes.push_back(0x8B);
        bytes.push_back(0x18);

        bytes.push_back(0xE9);
        int32_t jumpOffset = static_cast<int32_t>((patchAddress + 0x43) - (patchAddress + 18));
        for (size_t i = 0; i < sizeof(jumpOffset); ++i) {
            bytes.push_back(static_cast<uint8_t>((jumpOffset >> (i * 8)) & 0xFF));
        }

        bytes.push_back(0x90);
        bytes.push_back(0x90);
        bytes.push_back(0x90);
        bytes.push_back(0x90);

        return bytes;
    }

    static bool ensureExpectedTicksPatch() {
        if (g_expectedTicksPatchAttempted) {
            return g_expectedTicksPatch != nullptr;
        }

        g_expectedTicksPatchAttempted = true;

        auto* base = reinterpret_cast<uint8_t*>(geode::base::get());
        size_t size = getModuleSize(base);
        if (!base || size == 0) {
            log::error("TPS bypass: failed to read module size");
            return false;
        }

        constexpr std::array<int, 27> kPattern = {
            0xFF, 0x90, -1,   -1,   -1,   -1,
            0xF3, 0x0F, 0x10, -1,   -1,   -1,   -1,   -1,
            0xF3, 0x44, 0x0F, 0x10, -1,   -1,   -1,   -1,
            0x00, 0xF3, 0x41, 0x0F, 0x5D
        };

        uintptr_t match = findPattern(base, size, kPattern);
        if (!match) {
            log::error("TPS bypass: failed to find expected-ticks pattern");
            return false;
        }

        uintptr_t patchAddress = match + 6;
        auto patchBytes = buildExpectedTicksPatch(patchAddress);
        auto result = Mod::get()->patch(reinterpret_cast<void*>(patchAddress), patchBytes);
        if (!result) {
            log::error("TPS bypass: failed to patch expected ticks: {}", result.unwrapErr());
            return false;
        }

        g_expectedTicksPatch = result.unwrap();
        log::info("TPS bypass: installed expected-ticks patch at 0x{:X}", patchAddress - reinterpret_cast<uintptr_t>(base));
        return true;
    }

    static bool setExpectedTicksPatchEnabled(bool enabled) {
        if (enabled && !ensureExpectedTicksPatch()) {
            return false;
        }

        if (!g_expectedTicksPatch) {
            return !enabled;
        }

        if (g_expectedTicksPatch->isEnabled() == enabled) {
            return true;
        }

        auto result = enabled
            ? g_expectedTicksPatch->enable()
            : g_expectedTicksPatch->disable();
        if (!result) {
            log::error(
                "TPS bypass: failed to {} expected-ticks patch: {}",
                enabled ? "enable" : "disable",
                result.unwrapErr()
            );
            return false;
        }

        return true;
    }

    static void setExpectedTicks(int64_t steps) {
        g_expectedTicks = static_cast<ExpectedTicksType>(std::clamp<int64_t>(
            steps,
            0,
            static_cast<int64_t>(std::numeric_limits<ExpectedTicksType>::max())
        ));
    }
#else
    static bool ensureExpectedTicksPatch() {
        return true;
    }

    static bool setExpectedTicksPatchEnabled(bool) {
        return true;
    }

    static void setExpectedTicks(int64_t) {}
#endif

    static bool nearlyEqual(double lhs, double rhs, double epsilon) {
        return std::abs(lhs - rhs) <= epsilon;
    }

    static bool shouldUseCustomTiming(ReplayEngine const& engine) {
        return engine.tickStepping
            || engine.singleTickStep
            || engine.renderer.recording
            || !nearlyEqual(engine.runtimeTickRate(), ReplayEngine::kBaseTickRate, 0.01)
            || !nearlyEqual(engine.gameSpeed, 1.0, 0.0001);
    }

    struct SimulationTimingController {
        float schedulerCarry = 0.0f;

        float fixedDelta(ReplayEngine const& engine) const {
            return engine.fixedSimulationDelta();
        }

        float speedDelta(ReplayEngine const& engine, float rawDt) const {
            return rawDt * static_cast<float>(engine.gameSpeed);
        }

        void resetSimulation(ReplayEngine& engine) {
            engine.tickAccumulator = 0.0f;
            engine.renderingDisabled = false;
            engine.recentlyInitialized = true;
        }

        void resetSchedulingCarry() {
            schedulerCarry = 0.0f;
        }

        void advanceSingleStep(CCScheduler* scheduler, ReplayEngine& engine) {
            resetSimulation(engine);
            resetSchedulingCarry();

            if (!engine.singleTickStep) {
                return;
            }

            engine.singleTickStep = false;
            scheduler->CCScheduler::update(fixedDelta(engine));
        }

        void advanceRecordedScheduler(CCScheduler* scheduler, ReplayEngine& engine, Renderer& renderer, float rawDt) {
            RenderResolutionScope resolution(renderer);

            float step = fixedDelta(engine);
            schedulerCarry += rawDt;

            while (schedulerCarry + step * 0.01f >= step) {
                schedulerCarry -= step;
                scheduler->CCScheduler::update(step);
            }

            if (schedulerCarry < 0.0f) {
                schedulerCarry = 0.0f;
            }
        }

        void advanceNormalScheduler(CCScheduler* scheduler, ReplayEngine& engine, float rawDt) {
            engine.recentlyInitialized = false;
            engine.tickAccumulator = 0.0f;
            scheduler->CCScheduler::update(speedDelta(engine, rawDt));
        }

        void resetVanillaState(ReplayEngine& engine, double* extraDelta = nullptr) {
            resetSchedulingCarry();
            setExpectedTicks(0);
            engine.tickAccumulator = 0.0f;
            engine.recentlyInitialized = false;
            if (extraDelta) {
                *extraDelta = 0.0;
            }
        }

        void tickAutoclickerSteps(GJBaseGameLayer* layer, ReplayEngine& engine, int64_t steps) {
            auto* ac = Autoclicker::get();
            if (!ac->enabled || engine.engineMode == MODE_EXECUTE || steps <= 0) {
                return;
            }

            auto* playLayer = PlayLayer::get();
            if (!playLayer || playLayer->m_player1->m_isDead) {
                return;
            }

            ac->isAutoclickerInput = true;
            for (int64_t i = 0; i < steps; ++i) {
                auto actions = ac->processTick();
                if (actions.p1Fire) {
                    layer->handleButton(actions.p1Press, 1, false);
                }
                if (actions.p2Fire) {
                    layer->handleButton(actions.p2Press, 1, true);
                }
            }
            ac->isAutoclickerInput = false;
        }

        void consumeSingleTick(GJBaseGameLayer* layer, ReplayEngine& engine) {
            resetSimulation(engine);
            setExpectedTicks(1);

            if (engine.collisionBypass) {
                engine.totalTickCount++;
            }

            tickAutoclickerSteps(layer, engine, 1);
            layer->GJBaseGameLayer::update(fixedDelta(engine));
        }
    };

    static SimulationTimingController& timingController() {
        static SimulationTimingController controller;
        return controller;
    }
}

void resetSimulationTimingState() {
    auto& controller = timingController();
    controller.resetSchedulingCarry();
    setExpectedTicks(0);

    if (auto* engine = ReplayEngine::get()) {
        controller.resetSimulation(*engine);
    }
}

class $modify(TickControlPlayLayer, PlayLayer) {
    void update(float dt) {
        ReplayEngine::get()->processHotkeys();
        PlayLayer::update(dt);
    }

    void updateVisibility(float dt) {
        PlayLayer::updateVisibility(dt);
    }
};

class $modify(SpeedControlScheduler, CCScheduler) {
    void update(float dt) {
        if (!PlayLayer::get()) {
            return CCScheduler::update(dt);
        }

        auto* engine = ReplayEngine::get();
        auto& controller = timingController();

        if (!shouldUseCustomTiming(*engine)) {
            controller.resetVanillaState(*engine);
            setExpectedTicksPatchEnabled(false);
            return CCScheduler::update(dt);
        }

        setExpectedTicksPatchEnabled(true);

        if (engine->tickStepping) {
            controller.advanceSingleStep(this, *engine);
            return;
        }

        if (engine->renderer.recording) {
            controller.advanceRecordedScheduler(this, *engine, engine->renderer, dt);
            return;
        }

        controller.advanceNormalScheduler(this, *engine, dt);
    }
};

class $modify(PhysicsControlLayer, GJBaseGameLayer) {
    struct Fields {
        double extraDelta = 0.0;
    };

    double getCustomDelta(float dt, double tps, bool applyExtraDelta = true) {
        auto* fields = m_fields.self();
        double secondsPerTick = 1.0 / std::max(1.0, tps);

        if (applyExtraDelta && m_resumeTimer > 0) {
            --m_resumeTimer;
            dt = 0.0f;
        }

        double totalDelta = static_cast<double>(dt) + fields->extraDelta;
        double timestep = std::clamp(static_cast<double>(m_gameState.m_timeWarp), 0.0, 1.0) * secondsPerTick;
        if (timestep <= 0.0) {
            if (applyExtraDelta) {
                fields->extraDelta = 0.0;
            }
            return 0.0;
        }

        double steps = std::round(totalDelta / timestep);
        double newDelta = steps * timestep;
        if (applyExtraDelta) {
            fields->extraDelta = totalDelta - newDelta;
        }

        return newDelta;
    }

    void update(float dt) {
        if (!PlayLayer::get()) {
            return GJBaseGameLayer::update(dt);
        }

        auto* engine = ReplayEngine::get();
        auto& controller = timingController();
        auto& renderer = engine->renderer;

        if (!shouldUseCustomTiming(*engine)) {
            controller.resetVanillaState(*engine, &m_fields->extraDelta);
            setExpectedTicksPatchEnabled(false);
            return GJBaseGameLayer::update(dt);
        }

        setExpectedTicksPatchEnabled(true);

        if (engine->tickStepping) {
            m_fields->extraDelta = 0.0;
            controller.consumeSingleTick(this, *engine);
        } else {
            auto* fields = m_fields.self();
            fields->extraDelta += dt;
            engine->recentlyInitialized = false;

            double timeWarp = std::clamp(static_cast<double>(m_gameState.m_timeWarp), 0.0, 1.0);
            double timestep = timeWarp * static_cast<double>(controller.fixedDelta(*engine));
            int64_t steps = 0;
            double totalDelta = 0.0;

            if (timestep > 0.0) {
                steps = static_cast<int64_t>(std::llround(fields->extraDelta / timestep));
                totalDelta = static_cast<double>(steps) * timestep;
                fields->extraDelta -= totalDelta;
            }

            setExpectedTicks(steps);
            engine->tickAccumulator = static_cast<float>(std::max(0.0, fields->extraDelta));

            if (engine->collisionBypass && steps > 0) {
                int64_t total = static_cast<int64_t>(engine->totalTickCount) + steps;
                engine->totalTickCount = static_cast<int>(std::min<int64_t>(total, std::numeric_limits<int>::max()));
            }

            controller.tickAutoclickerSteps(this, *engine, steps);
            GJBaseGameLayer::update(static_cast<float>(totalDelta));
        }

        if (renderer.recording) {
            renderer.handleRecording(PlayLayer::get(), renderer.getCurrentFrame());
        }
    }

    double getModifiedDelta(float dt) {
        if (!PlayLayer::get()) {
            return GJBaseGameLayer::getModifiedDelta(dt);
        }

        auto* engine = ReplayEngine::get();
        if (!shouldUseCustomTiming(*engine)) {
            setExpectedTicksPatchEnabled(false);
            return GJBaseGameLayer::getModifiedDelta(dt);
        }

        setExpectedTicksPatchEnabled(true);
        return getCustomDelta(dt, ReplayEngine::get()->runtimeTickRate());
    }
};
