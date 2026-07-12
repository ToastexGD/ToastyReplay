#pragma once

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>

namespace toasty::cocos {
inline cocos2d::CCSprite* createBackgroundTile() {
    auto* sprite = cocos2d::CCSprite::createWithSpriteFrameName("game_bg_02_001.png");
    if (!sprite) {
        sprite = cocos2d::CCSprite::create("game_bg_02_001.png");
    }
    return sprite;
}

inline void addOrangeMovingBackground(cocos2d::CCNode* parent, cocos2d::CCSize size) {
    using namespace cocos2d;

    auto* base = CCLayerColor::create(ccc4(106, 43, 13, 255), size.width, size.height);
    base->setPosition(CCPointZero);
    parent->addChild(base, -30);

    auto* gradient = CCSprite::create("GJ_gradientBG.png");
    if (gradient) {
        auto sourceSize = gradient->getContentSize();
        gradient->setAnchorPoint(CCPointZero);
        gradient->setScaleX(sourceSize.width > 0.0f ? size.width / sourceSize.width : 1.0f);
        gradient->setScaleY(sourceSize.height > 0.0f ? size.height / sourceSize.height : 1.0f);
        gradient->setColor(ccc3(255, 116, 32));
        gradient->setOpacity(205);
        parent->addChild(gradient, -29);
    }

    auto* sample = createBackgroundTile();
    if (sample) {
        auto sourceSize = sample->getContentSize();
        float scale = sourceSize.height > 0.0f ? (size.height + 12.0f) / sourceSize.height : 1.0f;
        float tileWidth = std::max(1.0f, sourceSize.width * scale);
        int tileCount = std::max(2, static_cast<int>(std::ceil(size.width / tileWidth)) + 2);
        auto* strip = CCNode::create();
        strip->setPosition(CCPointZero);
        for (int index = 0; index < tileCount; ++index) {
            auto* tile = index == 0 ? sample : createBackgroundTile();
            if (!tile) continue;
            tile->setAnchorPoint(CCPointZero);
            tile->setScale(scale);
            tile->setPosition(ccp(static_cast<float>(index) * tileWidth, -6.0f));
            tile->setColor(ccc3(255, 151, 62));
            tile->setOpacity(145);
            strip->addChild(tile);
        }
        float duration = std::max(12.0f, tileWidth / 11.0f);
        strip->runAction(CCRepeatForever::create(CCSequence::create(
            CCMoveBy::create(duration, ccp(-tileWidth, 0.0f)),
            CCPlace::create(CCPointZero),
            nullptr
        )));
        parent->addChild(strip, -28);
    }

    auto* shade = CCLayerColor::create(ccc4(34, 15, 8, 112), size.width, size.height);
    shade->setPosition(CCPointZero);
    parent->addChild(shade, -27);

    auto* accent = CCLayerColor::create(ccc4(255, 176, 76, 210), size.width, 2.0f);
    accent->setPosition(ccp(0.0f, size.height - 2.0f));
    parent->addChild(accent, -26);
}
}
