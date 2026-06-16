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
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

using namespace geode::prelude;

namespace {
    struct ScopedRenderResolutionToggle {
        Renderer& renderer;

        explicit ScopedRenderResolutionToggle(Renderer& activeRenderer)
            : renderer(activeRenderer) {
            renderer.changeRes(false);
        }

        ~ScopedRenderResolutionToggle() {
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

    static std::optional<size_t> findHexPattern(uint8_t* base, size_t size, std::string_view textPattern) {
        std::vector<uint8_t> bytes;
        std::vector<bool> mask;

        for (size_t pos = 0; pos < textPattern.size();) {
            while (pos < textPattern.size() && textPattern[pos] == ' ') ++pos;
            if (pos >= textPattern.size()) break;

            if (textPattern[pos] == '?') {
                bytes.push_back(0);
                mask.push_back(false);
                while (pos < textPattern.size() && textPattern[pos] == '?') ++pos;
            } else {
                auto hi = textPattern[pos++];
                auto lo = textPattern[pos++];
                auto hexVal = [](char c) -> uint8_t {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
                    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
                    return 0;
                };
                bytes.push_back(static_cast<uint8_t>((hexVal(hi) << 4) | hexVal(lo)));
                mask.push_back(true);
            }
        }

        size_t patLen = bytes.size();
        if (!base || size < patLen) return std::nullopt;

        for (size_t i = 0; i + patLen <= size; ++i) {
            bool hit = true;
            for (size_t j = 0; j < patLen; ++j) {
                if (mask[j] && base[i + j] != bytes[j]) {
                    hit = false;
                    break;
                }
            }
            if (hit) return i;
        }

        return std::nullopt;
    }

#pragma pack(push, 1)
    struct PatchPayload {
        uint8_t movRaxOpcode[2];
        uint64_t pointer;
        uint8_t movR11dOpcode[3];
        uint8_t jmpOpcode;
        int32_t jmpOffset;
        uint8_t nopPad[4];
    };
#pragma pack(pop)

    static std::vector<uint8_t> buildExpectedTicksPatch(uintptr_t patchAddress) {
        PatchPayload payload;
        payload.movRaxOpcode[0] = 0x48;
        payload.movRaxOpcode[1] = 0xB8;
        payload.pointer = reinterpret_cast<uint64_t>(&g_expectedTicks);
        payload.movR11dOpcode[0] = 0x44;
        payload.movR11dOpcode[1] = 0x8B;
        payload.movR11dOpcode[2] = 0x18;
        payload.jmpOpcode = 0xE9;
        payload.jmpOffset = static_cast<int32_t>((patchAddress + 0x43) - (patchAddress + 18));
        payload.nopPad[0] = 0x90;
        payload.nopPad[1] = 0x90;
        payload.nopPad[2] = 0x90;
        payload.nopPad[3] = 0x90;

        std::vector<uint8_t> result(sizeof(payload));
        std::memcpy(result.data(), &payload, sizeof(payload));
        return result;
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

        static constexpr std::string_view kExpectedTicksSig =
            "FF 90 ?? ?? ?? ?? F3 0F 10 ?? ?? ?? ?? ?? F3 44 0F 10 ?? ?? ?? ?? 00 F3 41 0F 5D";

        auto match = findHexPattern(base, size, kExpectedTicksSig);
        if (!match) {
            log::error("TPS bypass: failed to find expected-ticks pattern");
            return false;
        }

        uintptr_t patchAddress = reinterpret_cast<uintptr_t>(base) + *match + 6;
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
            || !nearlyEqual(engine.gameSpeed, 1.0, 0.0001)
            || Autoclicker::get()->enabled;
    }

    struct TickStepPlanner {
        double accumulated = 0.0;

        void absorb(double dt) {
            accumulated += dt;
        }

        int planSteps(double timestep) const {
            if (timestep <= 0.0) return 0;
            return static_cast<int>(std::llround(accumulated / timestep));
        }

        double residual() const {
            return accumulated;
        }
    };

    struct DeferredFrameStepRelease {
        PlayerButton button;
        bool player2;
    };

    static std::vector<DeferredFrameStepRelease> g_frameStepDeferredReleases;

    static constexpr double kFrameStepPairTimestampTolerance = 1e-6;

    static bool isStretchableTapButton(PlayerButton button) {
        return button == PlayerButton::Jump;
    }

    static void stretchTrailingTapsForFrameStep(GJBaseGameLayer* layer) {
        if (!layer) {
            return;
        }

        auto& queue = layer->m_queuedButtons;
        while (queue.size() >= 2) {
            auto const& release = queue[queue.size() - 1];
            auto const& press = queue[queue.size() - 2];

            if (release.m_isPush || !press.m_isPush) break;
            if (release.m_button != press.m_button) break;
            if (release.m_isPlayer2 != press.m_isPlayer2) break;
            if (!isStretchableTapButton(release.m_button)) break;
            if (std::abs(release.m_timestamp - press.m_timestamp) > kFrameStepPairTimestampTolerance) break;

            g_frameStepDeferredReleases.push_back({ release.m_button, release.m_isPlayer2 });
            queue.pop_back();
        }
    }

    static void requeueDeferredFrameStepReleases(GJBaseGameLayer* layer) {
        if (!layer || g_frameStepDeferredReleases.empty()) {
            return;
        }

        double timestamp = layer->m_timestamp;
        for (auto const& deferred : g_frameStepDeferredReleases) {
            PlayerButtonCommand command = {};
            command.m_button = deferred.button;
            command.m_isPush = false;
            command.m_isPlayer2 = deferred.player2;
            command.m_timestamp = timestamp;
            layer->m_queuedButtons.push_back(command);
        }

        g_frameStepDeferredReleases.clear();
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

            auto* pl = PlayLayer::get();
            if (pl) {
                engine.bridgeUserHoldsToPlayer(pl);
                stretchTrailingTapsForFrameStep(pl);
            }

            scheduler->CCScheduler::update(fixedDelta(engine));

            if (pl) {
                requeueDeferredFrameStepReleases(pl);
            }
        }

        void advanceRecordedScheduler(CCScheduler* scheduler, ReplayEngine& engine, Renderer& renderer, float rawDt) {
            ScopedRenderResolutionToggle resolution(renderer);

            float step = fixedDelta(engine);
            schedulerCarry += rawDt;

            static constexpr int kMaxStepsPerFrame = 2;
            int stepsThisFrame = 0;

            while (schedulerCarry + step * 0.01f >= step) {
                if (stepsThisFrame >= kMaxStepsPerFrame) break;
                schedulerCarry -= step;
                scheduler->CCScheduler::update(step);
                ++stepsThisFrame;
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

        void consumeSingleTick(GJBaseGameLayer* layer, ReplayEngine& engine) {
            resetSimulation(engine);
            setExpectedTicks(1);

            layer->GJBaseGameLayer::update(fixedDelta(engine));
        }
    };

    static SimulationTimingController& timingController() {
        static SimulationTimingController controller;
        return controller;
    }

    static void applyCustomTickPlan(GJBaseGameLayer* layer, double* extraDelta, float dt, ReplayEngine* engine) {
        auto& controller = timingController();

        *extraDelta += dt;
        engine->recentlyInitialized = false;

        double timeWarp = std::clamp(static_cast<double>(layer->m_gameState.m_timeWarp), 0.0, 1.0);
        double timestep = timeWarp * static_cast<double>(controller.fixedDelta(*engine));

        TickStepPlanner planner;
        planner.accumulated = *extraDelta;
        int64_t steps = static_cast<int64_t>(planner.planSteps(timestep));
        double totalDelta = static_cast<double>(steps) * timestep;
        *extraDelta -= totalDelta;

        setExpectedTicks(steps);
        engine->tickAccumulator = static_cast<float>(std::max(0.0, *extraDelta));

        layer->GJBaseGameLayer::update(static_cast<float>(totalDelta));
    }
}

void resetSimulationTimingState() {
    auto& controller = timingController();
    controller.resetSchedulingCarry();
    setExpectedTicks(0);

    if (auto* engine = ReplayEngine::get()) {
        controller.resetSimulation(*engine);
        engine->clearQueuedSubstepState();
    }
}

class $modify(TickControlPlayLayer, PlayLayer) {
    struct Fields {
        double pulseFixAccum = 0.0;
    };

    void update(float dt) {
        ReplayEngine::get()->processHotkeys();
        PlayLayer::update(dt);
    }

    void updateVisibility(float dt) {
        auto* engine = ReplayEngine::get();
        if (engine->pulseFix && engine->renderer.recording
            && engine->runtimeTickRate() > ReplayEngine::kBaseTickRate) {
            double baseStep = 1.0 / ReplayEngine::kBaseTickRate;
            double& accum = m_fields->pulseFixAccum;
            accum += dt;
            int guard = 0;
            while (accum + 1e-9 >= baseStep && guard++ < 16) {
                PlayLayer::updateVisibility(static_cast<float>(baseStep));
                accum -= baseStep;
            }
            return;
        }
        m_fields->pulseFixAccum = 0.0;
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

        if (engine->renderer.recording) {
            controller.advanceRecordedScheduler(this, *engine, engine->renderer, dt);
            return;
        }

        if (engine->tickStepping) {
            controller.advanceSingleStep(this, *engine);
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

        TickStepPlanner planner;
        planner.accumulated = static_cast<double>(dt) + fields->extraDelta;
        double timestep = std::clamp(static_cast<double>(m_gameState.m_timeWarp), 0.0, 1.0) * secondsPerTick;
        if (timestep <= 0.0) {
            if (applyExtraDelta) {
                fields->extraDelta = 0.0;
            }
            return 0.0;
        }

        double steps = std::round(planner.accumulated / timestep);
        double newDelta = steps * timestep;
        if (applyExtraDelta) {
            fields->extraDelta = planner.accumulated - newDelta;
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
            GJBaseGameLayer::update(dt);
            return;
        }

        setExpectedTicksPatchEnabled(true);

        if (engine->tickStepping) {
            m_fields->extraDelta = 0.0;
            controller.consumeSingleTick(this, *engine);
            engine->requestFrameStepMusicSync(PlayLayer::get());
            engine->syncFrameStepAudio(FMODAudioEngine::sharedEngine());
        } else {
            applyCustomTickPlan(this, &m_fields->extraDelta, dt, engine);
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
