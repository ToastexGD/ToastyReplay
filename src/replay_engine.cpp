#include "ToastyReplay.hpp"
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

void updateSeed(bool isRestart = false);

static int getCurrentFrame() {
    PlayLayer* pl = PlayLayer::get();
    if (!pl) return 0;

    auto* mgr = ToastyReplay::get();
    int frame = static_cast<int>(pl->m_gameState.m_levelTime * mgr->tps);
    frame++;

    if (frame < 0) return 0;
    return frame;
}

static bool flipControls() {
    PlayLayer* pl = PlayLayer::get();
    if (!pl) return GameManager::get()->getGameVariable("0010");

    return pl->m_levelSettings->m_platformerMode ? false : GameManager::get()->getGameVariable("0010");
}

class $modify(ToastyReplayBGLHook, GJBaseGameLayer) {

  struct Fields {
    bool macroInput = false;
  };

  void processCommands(float dt) {
    auto* mgr = ToastyReplay::get();

    PlayLayer* pl = PlayLayer::get();

    if (!pl) {
      return GJBaseGameLayer::processCommands(dt);
    }

    updateSeed();

    if (mgr->state != NONE) {

      if (!mgr->firstAttempt) {
      }

      int frame = getCurrentFrame();
      if (frame > 2 && mgr->firstAttempt && mgr->currentReplay) {
        mgr->firstAttempt = false;

        if (m_levelSettings->m_platformerMode && !m_levelEndAnimationStarted)
          return pl->resetLevelFromStart();
        else if (!m_levelEndAnimationStarted)
          return pl->resetLevel();
      }

      if (mgr->previousFrame == frame && frame != 0 && mgr->currentReplay)
        return GJBaseGameLayer::processCommands(dt);

    }

    GJBaseGameLayer::processCommands(dt);

    if (mgr->state == NONE)
      return;

    int frame = getCurrentFrame();
    mgr->previousFrame = frame;

    if (mgr->currentReplay && mgr->restart && !m_levelEndAnimationStarted) {
      if ((m_levelSettings->m_platformerMode && mgr->state != NONE))
        return pl->resetLevelFromStart();
      else
        return pl->resetLevel();
    }

    if (mgr->state == RECORD)
      handleRecording(frame);

    if (mgr->state == PLAYBACK)
      handlePlaying(frame);

  }

  void handleRecording(int frame) {
    auto* mgr = ToastyReplay::get();

    if (!mgr->currentReplay) return;

    if (mgr->ignoreFrame != -1) {
      if (mgr->ignoreFrame < frame) mgr->ignoreFrame = -1;
    }

    bool twoPlayers = m_levelSettings->m_twoPlayerMode;

    if (mgr->delayedFrameInput[0] == frame) {
      mgr->delayedFrameInput[0] = -1;
      GJBaseGameLayer::handleButton(true, 1, true);
    }

    if (mgr->delayedFrameInput[1] == frame) {
      mgr->delayedFrameInput[1] = -1;
      GJBaseGameLayer::handleButton(true, 1, false);
    }

    if (frame > mgr->ignoreJumpButton && mgr->ignoreJumpButton != -1)
      mgr->ignoreJumpButton = -1;

    for (int x = 0; x < 2; x++) {
      if (mgr->delayedFrameReleaseMain[x] == frame) {
        bool player2 = x == 0;
        mgr->delayedFrameReleaseMain[x] = -1;
        GJBaseGameLayer::handleButton(false, 1, twoPlayers ? player2 : false);
      }

      if (!m_levelSettings->m_platformerMode)
        continue;

      for (int y = 0; y < 2; y++) {
        if (mgr->delayedFrameRelease[x][y] == frame) {
          int button = y == 0 ? 2 : 3;
          bool player2 = x == 0;
          mgr->delayedFrameRelease[x][y] = -1;
          GJBaseGameLayer::handleButton(false, button, player2);
        }
      }
    }

    if (!mgr->frameFixes || mgr->currentReplay->inputs.empty()) return;

    auto& positionSnapshots = mgr->currentReplay->frameFixes;
    if (!positionSnapshots.empty()) {
      int lastSnapshotFrame = positionSnapshots.back().frame;
      float timeSinceLastSnapshot = (frame - lastSnapshotFrame) / mgr->tps;
      float snapshotInterval = 1.f / mgr->frameFixesLimit;

      if (timeSinceLastSnapshot < snapshotInterval)
        return;
    }

    auto clampAngleTo360 = [](float degrees) -> float {
      while (degrees < 0.f) degrees += 360.f;
      while (degrees > 360.f) degrees -= 360.f;
      return degrees;
    };

    float angle1 = clampAngleTo360(m_player1->getRotation());
    float angle2 = clampAngleTo360(m_player2->getRotation());

    FrameFix currentSnapshot;
    currentSnapshot.frame = frame;
    currentSnapshot.p1.pos = m_player1->getPosition();
    currentSnapshot.p1.rotation = angle1;
    currentSnapshot.p2.pos = m_player2->getPosition();
    currentSnapshot.p2.rotation = angle2;

    positionSnapshots.push_back(currentSnapshot);

  }

  void handlePlaying(int frame) {
    auto* mgr = ToastyReplay::get();
    if (m_levelEndAnimationStarted) return;

    if (!mgr->currentReplay) return;

    if (m_player1->m_isDead) {
      m_player1->releaseAllButtons();
      m_player2->releaseAllButtons();
      return;
    }

    m_fields->macroInput = true;

    size_t& inputIdx = mgr->playbackIndex;
    auto& inputList = mgr->currentReplay->inputs;

    while (inputIdx < inputList.size() && frame >= inputList[inputIdx].frame) {
      auto input = inputList[inputIdx];

      if (frame != mgr->respawnFrame) {
        if (flipControls())
          input.player2 = !input.player2;

        GJBaseGameLayer::handleButton(input.down, input.button, input.player2);
      }

      inputIdx++;
    }

    mgr->respawnFrame = -1;
    m_fields->macroInput = false;

    if (inputIdx == inputList.size()) {
    }

    if ((!mgr->frameFixes && !mgr->inputFixes) || !PlayLayer::get()) return;

    size_t& snapshotIdx = mgr->frameFixIndex;
    auto& snapshotList = mgr->currentReplay->frameFixes;

    while (snapshotIdx < snapshotList.size() && frame >= snapshotList[snapshotIdx].frame) {
      const FrameFix& snapshot = snapshotList[snapshotIdx];

      PlayerObject* player1 = m_player1;
      PlayerObject* player2 = m_player2;

      bool hasValidP1Position = (snapshot.p1.pos.x != 0.f && snapshot.p1.pos.y != 0.f);
      bool hasValidP1Rotation = (snapshot.p1.rotate && snapshot.p1.rotation != 0.f);

      if (hasValidP1Position)
        player1->setPosition(snapshot.p1.pos);

      if (hasValidP1Rotation)
        player1->setRotation(snapshot.p1.rotation);

      if (m_gameState.m_isDualMode) {
        bool hasValidP2Position = (snapshot.p2.pos.x != 0.f && snapshot.p2.pos.y != 0.f);
        bool hasValidP2Rotation = (snapshot.p2.rotate && snapshot.p2.rotation != 0.f);

        if (hasValidP2Position)
          player2->setPosition(snapshot.p2.pos);

        if (hasValidP2Rotation)
          player2->setRotation(snapshot.p2.rotation);
      }

      snapshotIdx++;
    }

  }

  void handleButton(bool hold, int button, bool player2) {
    auto* mgr = ToastyReplay::get();

    if (mgr->state == NONE)
      return GJBaseGameLayer::handleButton(hold, button, player2);

    if (mgr->state == PLAYBACK) {
      if (mgr->ignoreManualInput && !m_fields->macroInput)
        return;
      else return GJBaseGameLayer::handleButton(hold, button, player2);
    }
    else if (mgr->ignoreFrame != -1 && hold)
      return;

    int frame = getCurrentFrame();

    bool isDelayedInput = mgr->delayedFrameInput[(m_levelSettings->m_twoPlayerMode ? static_cast<int>(!player2) : 0)] != -1;
    bool isDelayedRelease = mgr->delayedFrameReleaseMain[(m_levelSettings->m_twoPlayerMode ? static_cast<int>(!player2) : 0)] != -1;

    if ((isDelayedInput || mgr->ignoreJumpButton == frame || isDelayedRelease) && button == 1) {
      if (mgr->ignoreJumpButton >= frame)
        mgr->delayedFrameInput[(m_levelSettings->m_twoPlayerMode ? static_cast<int>(!player2) : 0)] = mgr->ignoreJumpButton + 1;

      return;
    }

    if (mgr->state != RECORD) return GJBaseGameLayer::handleButton(hold, button, player2);

    if (!mgr->currentReplay) return GJBaseGameLayer::handleButton(hold, button, player2);

    if (mgr->inputFixes) {
      auto normalizeRotation = [](float rot) -> float {
        while (rot < 0.f) rot += 360.f;
        while (rot > 360.f) rot -= 360.f;
        return rot;
      };

      float rotation1 = normalizeRotation(m_player1->getRotation());
      float rotation2 = normalizeRotation(m_player2->getRotation());

      FrameFix inputSnapshot;
      inputSnapshot.frame = frame;
      inputSnapshot.p1.pos = m_player1->getPosition();
      inputSnapshot.p1.rotation = rotation1;
      inputSnapshot.p2.pos = m_player2->getPosition();
      inputSnapshot.p2.rotation = rotation2;

      mgr->currentReplay->frameFixes.push_back(inputSnapshot);
    }

    GJBaseGameLayer::handleButton(hold, button, player2);

    if (!m_levelSettings->m_twoPlayerMode)
      player2 = false;

    if (!mgr->ignoreRecordAction && !mgr->creatingTrajectory && !m_player1->m_isDead) {
      mgr->currentReplay->addInput(frame, button, player2, hold);
      mgr->isReplayInput = true;
    }
  }
};
