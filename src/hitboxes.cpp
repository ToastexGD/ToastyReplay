#include "ToastyReplay.hpp"
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <deque>
#include <cmath>

std::deque<cocos2d::CCRect> s_trail1, s_trail2;

static std::array<cocos2d::CCPoint, 4> rectPoints(const cocos2d::CCRect& r) {
    return {
        cocos2d::CCPoint(r.getMinX(), r.getMinY()),
        cocos2d::CCPoint(r.getMinX(), r.getMaxY()),
        cocos2d::CCPoint(r.getMaxX(), r.getMaxY()),
        cocos2d::CCPoint(r.getMaxX(), r.getMinY())
    };
}

static std::array<cocos2d::CCPoint, 4> rotatedRect(const cocos2d::CCRect& r, float degrees) {
    float cx = (r.getMinX() + r.getMaxX()) * 0.5f;
    float cy = (r.getMinY() + r.getMaxY()) * 0.5f;
    auto pts = rectPoints(r);
    float rad = CC_DEGREES_TO_RADIANS(-degrees);
    float c = cosf(rad), s = sinf(rad);
    for (auto& p : pts) {
        float dx = p.x - cx, dy = p.y - cy;
        p.x = cx + dx * c - dy * s;
        p.y = cy + dx * s + dy * c;
    }
    return pts;
}

class $modify(HitboxBase, GJBaseGameLayer) {
    void updateDebugDraw() {
        auto* eng = ReplayEngine::get();

        GJBaseGameLayer::updateDebugDraw();

        if (!eng->showHitboxes) return;

        auto* pl = GameManager::sharedState()->getPlayLayer();
        if (eng->hitboxOnDeath && pl && !pl->m_player1->m_isDead) {
            m_debugDrawNode->clear();
            return;
        }

        cocos2d::ccColor4F noFill = {0.f, 0.f, 0.f, 0.f};
        cocos2d::ccColor4F playerCol = {1.f, 1.f, 0.f, 0.75f};
        cocos2d::ccColor4F hazardCol = {1.f, 0.f, 0.f, 1.f};
        cocos2d::ccColor4F trailCol = {1.f, 1.f, 0.f, 1.f};

        auto drawPlayerBox = [&](PlayerObject* player) {
            float angle = player->getRotation();
            auto outer = player->getObjectRect();
            auto outerPts = rotatedRect(outer, angle);
            m_debugDrawNode->drawPolygon(outerPts.data(), 4, noFill, 0.25f, playerCol);
        };

        drawPlayerBox(m_player1);

        if (m_gameState.m_isDualMode && m_player2) {
            drawPlayerBox(m_player2);
        }

        if (m_anticheatSpike) {
            auto ptsAC = rectPoints(m_anticheatSpike->getObjectRect());
            m_debugDrawNode->drawPolygon(ptsAC.data(), 4, noFill, 0.25f, hazardCol);
        }

        for (auto& rect : s_trail1) {
            auto pts = rectPoints(rect);
            m_debugDrawNode->drawPolygon(pts.data(), 4, noFill, 0.25f, trailCol);
        }

        for (auto& rect : s_trail2) {
            auto pts = rectPoints(rect);
            m_debugDrawNode->drawPolygon(pts.data(), 4, noFill, 0.25f, trailCol);
        }
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        auto* eng = ReplayEngine::get();
        if (!eng->showHitboxes || !eng->hitboxTrail) return;
        if (m_player1->m_isDead) return;

        s_trail1.push_back(m_player1->getObjectRect());
        s_trail2.push_back(m_player2->getObjectRect());

        auto maxLen = static_cast<size_t>(eng->hitboxTrailLength);
        while (s_trail1.size() > maxLen) s_trail1.pop_front();
        while (s_trail2.size() > maxLen) s_trail2.pop_front();
    }
};

class $modify(HitboxPlay, PlayLayer) {
    void updateProgressbar() {
        PlayLayer::updateProgressbar();

        auto* eng = ReplayEngine::get();
        if (eng->showHitboxes) {
            if (!(m_isPracticeMode && GameManager::get()->getGameVariable("0166")))
                PlayLayer::updateDebugDraw();
            m_debugDrawNode->setVisible(true);
        } else {
            m_debugDrawNode->clear();
            m_debugDrawNode->setVisible(false);
        }
    }

    void resetLevel() {
        s_trail1.clear();
        s_trail2.clear();
        PlayLayer::resetLevel();
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        s_trail1.clear();
        s_trail2.clear();
    }

    void onQuit() {
        s_trail1.clear();
        s_trail2.clear();
        PlayLayer::onQuit();
    }
};
