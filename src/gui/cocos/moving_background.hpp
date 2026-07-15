#pragma once

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>

namespace toasty::cocos {
inline void addOrangeMovingBackground(cocos2d::CCNode* parent, cocos2d::CCSize size) {
    using namespace cocos2d;

    auto* base = CCLayerColor::create(ccc4(112, 42, 10, 255), size.width, size.height);
    base->setPosition(CCPointZero);
    parent->addChild(base, -30);

    auto* glow = CCLayerColor::create(ccc4(255, 128, 28, 92), size.width, size.height * 0.58f);
    glow->setPosition(ccp(0.0f, size.height * 0.21f));
    parent->addChild(glow, -29);

    constexpr float stride = 88.0f;
    auto* strip = CCNode::create();
    strip->setPosition(ccp(-stride, 0.0f));
    int tileCount = std::max(3, static_cast<int>(std::ceil(size.width / stride)) + 3);
    for (int index = 0; index < tileCount; ++index) {
        float x = static_cast<float>(index) * stride;
        auto* band = CCLayerColor::create(ccc4(255, 171, 76, 42), 58.0f, size.height);
        band->setPosition(ccp(x, 0.0f));
        strip->addChild(band);

        auto* edge = CCLayerColor::create(ccc4(255, 221, 158, 50), 2.0f, size.height);
        edge->setPosition(ccp(x + 6.0f, 0.0f));
        strip->addChild(edge);
    }
    strip->runAction(CCRepeatForever::create(CCSequence::create(
        CCMoveBy::create(8.0f, ccp(stride, 0.0f)),
        CCPlace::create(ccp(-stride, 0.0f)),
        nullptr
    )));
    parent->addChild(strip, -28);

    auto* shade = CCLayerColor::create(ccc4(38, 13, 5, 72), size.width, size.height);
    shade->setPosition(CCPointZero);
    parent->addChild(shade, -27);

    auto* accent = CCLayerColor::create(ccc4(255, 176, 76, 210), size.width, 2.0f);
    accent->setPosition(ccp(0.0f, size.height - 2.0f));
    parent->addChild(accent, -26);
}
}
