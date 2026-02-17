#include "ToastyReplay.hpp"
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

void refreshRngState(bool isRestart = false);

static int computeCurrentTick() {
    PlayLayer* pl = PlayLayer::get();
    if (!pl) return 0;

    auto* engine = ReplayEngine::get();
    int tick = static_cast<int>(pl->m_gameState.m_levelTime * engine->tickRate);
    tick++;

    if (tick < 0) return 0;
    return tick;
}

static bool flipControls() {
    PlayLayer* pl = PlayLayer::get();
    if (!pl) return GameManager::get()->getGameVariable("0010");

    return pl->m_levelSettings->m_platformerMode ? false : GameManager::get()->getGameVariable("0010");
}

class $modify(MacroEngineBaseLayer, GJBaseGameLayer) {

  struct Fields {
    bool macroInput = false;
  };

  void processCommands(float dt, bool isHalfTick, bool isLastTick) {
    auto* engine = ReplayEngine::get();

    PlayLayer* pl = PlayLayer::get();

    if (!pl) {
      return GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }

    refreshRngState();

    if (engine->engineMode != MODE_DISABLED) {

      if (!engine->initialRun) {
      }

      int tick = computeCurrentTick();
      if (tick > 2 && engine->initialRun && engine->activeMacro) {
        engine->initialRun = false;

        if (m_levelSettings->m_platformerMode && !m_levelEndAnimationStarted)
          return pl->resetLevelFromStart();
        else if (!m_levelEndAnimationStarted)
          return pl->resetLevel();
      }

      if (engine->lastTickIndex == tick && tick != 0 && engine->activeMacro)
        return GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

    }

    GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

    if (engine->engineMode == MODE_DISABLED)
      return;

    int tick = computeCurrentTick();
    engine->lastTickIndex = tick;

    if (engine->activeMacro && engine->levelRestarting && !m_levelEndAnimationStarted) {
      if ((m_levelSettings->m_platformerMode && engine->engineMode != MODE_DISABLED))
        return pl->resetLevelFromStart();
      else
        return pl->resetLevel();
    }

    if (engine->engineMode == MODE_CAPTURE)
      processCapture(tick);

    if (engine->engineMode == MODE_EXECUTE)
      processExecution(tick);

  }

  void processCapture(int tick) {
    auto* engine = ReplayEngine::get();

    if (!engine->activeMacro) return;

    if (engine->skipTickIndex != -1) {
      if (engine->skipTickIndex < tick) engine->skipTickIndex = -1;
    }

    bool twoPlayers = m_levelSettings->m_twoPlayerMode;

    if (engine->deferredInputTick[0] == tick) {
      engine->deferredInputTick[0] = -1;
      GJBaseGameLayer::handleButton(true, 1, true);
    }

    if (engine->deferredInputTick[1] == tick) {
      engine->deferredInputTick[1] = -1;
      GJBaseGameLayer::handleButton(true, 1, false);
    }

    if (tick > engine->skipActionTick && engine->skipActionTick != -1)
      engine->skipActionTick = -1;

    for (int x = 0; x < 2; x++) {
      if (engine->deferredReleaseA[x] == tick) {
        bool player2 = x == 0;
        engine->deferredReleaseA[x] = -1;
        GJBaseGameLayer::handleButton(false, 1, twoPlayers ? player2 : false);
      }

      if (!m_levelSettings->m_platformerMode)
        continue;

      for (int y = 0; y < 2; y++) {
        if (engine->deferredReleaseB[x][y] == tick) {
          int button = y == 0 ? 2 : 3;
          bool player2 = x == 0;
          engine->deferredReleaseB[x][y] = -1;
          GJBaseGameLayer::handleButton(false, button, player2);
        }
      }
    }

    if (!engine->positionCorrection || engine->activeMacro->inputs.empty()) return;

    auto& corrections = engine->activeMacro->corrections;
    if (!corrections.empty()) {
      int lastCorrectionTick = corrections.back().tick;
      float timeSinceLastCorrection = (tick - lastCorrectionTick) / engine->tickRate;
      float correctionFrequency = 1.f / engine->correctionInterval;

      if (timeSinceLastCorrection < correctionFrequency)
        return;
    }

    auto clampAngleTo360 = [](float degrees) -> float {
      while (degrees < 0.f) degrees += 360.f;
      while (degrees > 360.f) degrees -= 360.f;
      return degrees;
    };

    float angle1 = clampAngleTo360(m_player1->getRotation());
    float angle2 = clampAngleTo360(m_player2->getRotation());

    PositionCorrection currentCorrection;
    currentCorrection.tick = tick;
    currentCorrection.player1Data.coordinates = m_player1->getPosition();
    currentCorrection.player1Data.angle = angle1;
    currentCorrection.player2Data.coordinates = m_player2->getPosition();
    currentCorrection.player2Data.angle = angle2;

    corrections.push_back(currentCorrection);

  }

  void processExecution(int tick) {
    auto* engine = ReplayEngine::get();
    if (m_levelEndAnimationStarted) return;

    if (!engine->activeMacro) return;

    if (m_player1->m_isDead) {
      m_player1->releaseAllButtons();
      m_player2->releaseAllButtons();
      return;
    }

    m_fields->macroInput = true;

    size_t& inputIdx = engine->executeIndex;
    auto& inputList = engine->activeMacro->inputs;

    while (inputIdx < inputList.size() && tick >= (int)inputList[inputIdx].frame) {
      auto input = inputList[inputIdx];

      if (tick != engine->respawnTickIndex) {
        if (flipControls())
          input.player2 = !input.player2;

        GJBaseGameLayer::handleButton(input.down, input.button, input.player2);
      }

      inputIdx++;
    }

    engine->respawnTickIndex = -1;
    m_fields->macroInput = false;

    if (inputIdx == inputList.size()) {
    }

    if ((!engine->positionCorrection && !engine->inputCorrection) || !PlayLayer::get()) return;

    size_t& correctionIdx = engine->correctionIndex;
    auto& correctionList = engine->activeMacro->corrections;

    while (correctionIdx < correctionList.size() && tick >= correctionList[correctionIdx].tick) {
      const PositionCorrection& correction = correctionList[correctionIdx];

      PlayerObject* player1 = m_player1;
      PlayerObject* player2 = m_player2;

      bool hasValidP1Position = (correction.player1Data.coordinates.x != 0.f && correction.player1Data.coordinates.y != 0.f);
      bool hasValidP1Rotation = (correction.player1Data.hasRotation && correction.player1Data.angle != 0.f);

      if (hasValidP1Position)
        player1->setPosition(correction.player1Data.coordinates);

      if (hasValidP1Rotation)
        player1->setRotation(correction.player1Data.angle);

      if (m_gameState.m_isDualMode) {
        bool hasValidP2Position = (correction.player2Data.coordinates.x != 0.f && correction.player2Data.coordinates.y != 0.f);
        bool hasValidP2Rotation = (correction.player2Data.hasRotation && correction.player2Data.angle != 0.f);

        if (hasValidP2Position)
          player2->setPosition(correction.player2Data.coordinates);

        if (hasValidP2Rotation)
          player2->setRotation(correction.player2Data.angle);
      }

      correctionIdx++;
    }

  }

  void handleButton(bool hold, int button, bool player2) {
    auto* engine = ReplayEngine::get();

    if (engine->engineMode == MODE_DISABLED)
      return GJBaseGameLayer::handleButton(hold, button, player2);

    if (engine->engineMode == MODE_EXECUTE) {
      if (engine->userInputIgnored && !m_fields->macroInput)
        return;
      else return GJBaseGameLayer::handleButton(hold, button, player2);
    }
    else if (engine->skipTickIndex != -1 && hold)
      return;

    int tick = computeCurrentTick();

    bool isDelayedInput = engine->deferredInputTick[(m_levelSettings->m_twoPlayerMode ? static_cast<int>(!player2) : 0)] != -1;
    bool isDelayedRelease = engine->deferredReleaseA[(m_levelSettings->m_twoPlayerMode ? static_cast<int>(!player2) : 0)] != -1;

    if ((isDelayedInput || engine->skipActionTick == tick || isDelayedRelease) && button == 1) {
      if (engine->skipActionTick >= tick)
        engine->deferredInputTick[(m_levelSettings->m_twoPlayerMode ? static_cast<int>(!player2) : 0)] = engine->skipActionTick + 1;

      return;
    }

    if (engine->engineMode != MODE_CAPTURE) return GJBaseGameLayer::handleButton(hold, button, player2);

    if (!engine->activeMacro) return GJBaseGameLayer::handleButton(hold, button, player2);

    if (engine->inputCorrection) {
      auto normalizeRotation = [](float rot) -> float {
        while (rot < 0.f) rot += 360.f;
        while (rot > 360.f) rot -= 360.f;
        return rot;
      };

      float rotation1 = normalizeRotation(m_player1->getRotation());
      float rotation2 = normalizeRotation(m_player2->getRotation());

      PositionCorrection inputCorrection;
      inputCorrection.tick = tick;
      inputCorrection.player1Data.coordinates = m_player1->getPosition();
      inputCorrection.player1Data.angle = rotation1;
      inputCorrection.player2Data.coordinates = m_player2->getPosition();
      inputCorrection.player2Data.angle = rotation2;

      engine->activeMacro->corrections.push_back(inputCorrection);
    }

    GJBaseGameLayer::handleButton(hold, button, player2);

    if (!m_levelSettings->m_twoPlayerMode)
      player2 = false;

    if (!engine->captureIgnored && !engine->simulatingPath && !m_player1->m_isDead) {
      engine->activeMacro->recordAction(tick, button, player2, hold);
      engine->macroInputActive = true;
    }
  }
};
