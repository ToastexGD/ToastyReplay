#include "gui/cocos/frontend.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {

constexpr const char* kSavedX = "mobile_float_x";
constexpr const char* kSavedY = "mobile_float_y";

class MobileFloatButton : public CCNode, public CCTouchDelegate {
protected:
    bool m_dragging = false;
    bool m_moved = false;
    CCPoint m_grabOffset = CCPointZero;

    CCPoint clampToScreen(CCPoint position) {
        auto win = CCDirector::sharedDirector()->getWinSize();
        float halfW = getContentSize().width * 0.5f;
        float halfH = getContentSize().height * 0.5f;
        position.x = std::clamp(position.x, halfW, std::max(halfW, win.width - halfW));
        position.y = std::clamp(position.y, halfH, std::max(halfH, win.height - halfH));
        return position;
    }

    bool touchInside(CCTouch* touch) {
        CCPoint local = convertToNodeSpace(touch->getLocation());
        CCRect bounds = { 0.f, 0.f, getContentSize().width, getContentSize().height };
        return bounds.containsPoint(local);
    }

public:
    static MobileFloatButton* create() {
        auto* node = new MobileFloatButton();
        if (node && node->init()) {
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) {
            return false;
        }

        auto* bg = CCScale9Sprite::create("GJ_button_01.png");
        bg->setContentSize({ 44.f, 44.f });
        setContentSize(bg->getContentSize());
        bg->setPosition({ getContentSize().width * 0.5f, getContentSize().height * 0.5f });
        addChild(bg);

        auto* label = CCLabelBMFont::create("TR", "bigFont.fnt");
        label->setScale(0.5f);
        label->setPosition(bg->getPosition());
        addChild(label);

        auto win = CCDirector::sharedDirector()->getWinSize();
        float x = static_cast<float>(Mod::get()->getSavedValue<double>(kSavedX, win.width - 40.0));
        float y = static_cast<float>(Mod::get()->getSavedValue<double>(kSavedY, win.height * 0.5));
        setPosition(clampToScreen({ x, y }));

        CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -600, true);
        return true;
    }

    void onExit() override {
        CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);
        CCNode::onExit();
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        if (!touchInside(touch)) {
            return false;
        }
        m_dragging = true;
        m_moved = false;
        m_grabOffset = getPosition() - touch->getLocation();
        return true;
    }

    void ccTouchMoved(CCTouch* touch, CCEvent*) override {
        if (!m_dragging) {
            return;
        }
        CCPoint next = clampToScreen(touch->getLocation() + m_grabOffset);
        if (ccpDistance(next, getPosition()) > 4.f) {
            m_moved = true;
        }
        setPosition(next);
    }

    void ccTouchEnded(CCTouch*, CCEvent*) override {
        m_dragging = false;
        if (!m_moved) {
            toasty::frontend::toggleMenu();
            return;
        }
        Mod::get()->setSavedValue<double>(kSavedX, getPosition().x);
        Mod::get()->setSavedValue<double>(kSavedY, getPosition().y);
    }

    void ccTouchCancelled(CCTouch*, CCEvent*) override {
        m_dragging = false;
    }
};

}

class $modify(MobileFloatPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }
#ifdef GEODE_IS_ANDROID
        if (toasty::frontend::isCocos() && !this->getChildByID("cocos-mobile-float"_spr)) {
            if (auto* button = MobileFloatButton::create()) {
                button->setID("cocos-mobile-float"_spr);
                button->setZOrder(1000);
                this->addChild(button);
            }
        }
#endif
        return true;
    }
};
