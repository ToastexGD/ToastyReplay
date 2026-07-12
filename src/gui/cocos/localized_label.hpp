#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <string>

namespace toasty::frontend {
class LocalizedLabel : public cocos2d::CCNodeRGBA {
protected:
    cocos2d::CCNode* m_labelNode = nullptr;
    cocos2d::CCLabelProtocol* m_label = nullptr;
    cocos2d::CCRGBAProtocol* m_rgba = nullptr;

    bool init(std::string const& text, char const* bitmapFont);
    void syncSize();

public:
    static LocalizedLabel* create(std::string const& text, char const* bitmapFont = "bigFont.fnt");

    void setString(std::string const& text);
    void limitLabelWidth(float width, float defaultScale, float minScale);
    void setColor(cocos2d::ccColor3B const& color) override;
    void setOpacity(GLubyte opacity) override;
};

ButtonSprite* createLocalizedButtonSprite(
    std::string const& text,
    int width,
    int minWidth,
    float scale,
    bool absolute,
    char const* background,
    float height
);

void setLocalizedButtonString(ButtonSprite* button, std::string const& text);
}
