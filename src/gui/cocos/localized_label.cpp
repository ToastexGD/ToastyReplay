#include "gui/cocos/localized_label.hpp"

#include "lang/localization.hpp"
#include "utils.hpp"

#include <algorithm>
#include <filesystem>

using namespace geode::prelude;

namespace toasty::frontend {
namespace {
    bool useTrueTypeFont() {
        return toasty::lang::getActiveLanguage() != toasty::lang::UiLanguage::English;
    }

    std::string trueTypeFontPath() {
        auto file = toasty::lang::getActiveLanguage() == toasty::lang::UiLanguage::Chinese
            ? "cjk.ttf"
            : "Inter-Bold.ttf";
        return toasty::pathToUtf8(Mod::get()->getResourcesDir() / file);
    }

    float trueTypeFontSize(char const* bitmapFont) {
        return std::string_view(bitmapFont) == "goldFont.fnt" ? 24.f : 32.f;
    }

    CCLabelTTF* createTrueTypeLabel(std::string const& text, char const* bitmapFont) {
        auto fontPath = trueTypeFontPath();
        auto* label = CCLabelTTF::create(text.c_str(), fontPath.c_str(), trueTypeFontSize(bitmapFont));
        if (label) {
            label->enableStroke(ccc3(0, 0, 0), 1.5f);
        }
        return label;
    }
}

LocalizedLabel* LocalizedLabel::create(std::string const& text, char const* bitmapFont) {
    auto* label = new LocalizedLabel();
    if (label && label->init(text, bitmapFont)) {
        label->autorelease();
        return label;
    }
    CC_SAFE_DELETE(label);
    return nullptr;
}

bool LocalizedLabel::init(std::string const& text, char const* bitmapFont) {
    if (!CCNodeRGBA::init()) {
        return false;
    }

    if (useTrueTypeFont()) {
        auto* label = createTrueTypeLabel(text, bitmapFont);
        if (!label) {
            return false;
        }
        m_labelNode = label;
        m_label = label;
        m_rgba = label;
    } else {
        auto* label = CCLabelBMFont::create(text.c_str(), bitmapFont);
        if (!label) {
            return false;
        }
        m_labelNode = label;
        m_label = label;
        m_rgba = label;
    }

    m_labelNode->setAnchorPoint({ 0.5f, 0.5f });
    addChild(m_labelNode);
    syncSize();
    return true;
}

void LocalizedLabel::syncSize() {
    if (!m_labelNode) {
        return;
    }
    auto size = m_labelNode->getContentSize();
    setContentSize(size);
    m_labelNode->setPosition({ size.width * 0.5f, size.height * 0.5f });
}

void LocalizedLabel::setString(std::string const& text) {
    if (!m_label) {
        return;
    }
    m_label->setString(text.c_str());
    syncSize();
}

void LocalizedLabel::limitLabelWidth(float width, float defaultScale, float minScale) {
    float scale = defaultScale;
    auto contentWidth = getContentSize().width;
    if (contentWidth > 0.f && contentWidth * scale > width) {
        scale = std::max(minScale, width / contentWidth);
    }
    setScale(scale);
}

void LocalizedLabel::setColor(ccColor3B const& color) {
    CCNodeRGBA::setColor(color);
    if (m_rgba) {
        m_rgba->setColor(color);
    }
}

void LocalizedLabel::setOpacity(GLubyte opacity) {
    CCNodeRGBA::setOpacity(opacity);
    if (m_rgba) {
        m_rgba->setOpacity(opacity);
    }
}

ButtonSprite* createLocalizedButtonSprite(
    std::string const& text,
    int width,
    int minWidth,
    float scale,
    bool absolute,
    char const* background,
    float height
) {
    if (!useTrueTypeFont()) {
        return ButtonSprite::create(
            text.c_str(), width, minWidth, scale, absolute,
            "bigFont.fnt", background, height
        );
    }

    auto* label = createTrueTypeLabel(text, "bigFont.fnt");
    if (!label) {
        return ButtonSprite::create(
            text.c_str(), width, minWidth, scale, absolute,
            "bigFont.fnt", background, height
        );
    }
    label->setID("localized-button-text");
    return ButtonSprite::create(label, width, minWidth, height, scale, absolute, background, false);
}

void setLocalizedButtonString(ButtonSprite* button, std::string const& text) {
    if (!button) {
        return;
    }
    if (auto* label = typeinfo_cast<CCLabelTTF*>(button->getChildByIDRecursive("localized-button-text"))) {
        label->setString(text.c_str());
        return;
    }
    button->setString(text.c_str());
}
}
