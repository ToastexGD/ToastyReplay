#include "gui/cocos/cells/cells.hpp"
#include "gui/cocos/frontend.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <algorithm>
#include <cstdio>

using namespace geode::prelude;

namespace toasty::frontend {

bool TRCell::initCell(float width, float height, bool withBackground) {
    if (!CCNode::init()) {
        return false;
    }
    setContentSize({ width, height });
    setAnchorPoint({ 0.5f, 0.5f });

    if (withBackground) {
        auto* bg = CCScale9Sprite::create("GJ_square05.png");
        CCSize natural = bg->getContentSize();
        constexpr float inset = 8.f;
        bg->setCapInsets(CCRect(inset, inset, std::max(1.f, natural.width - inset * 2.f), std::max(1.f, natural.height - inset * 2.f)));
        bg->setContentSize({ width, height });
        bg->setPosition({ width * 0.5f, height * 0.5f });
        bg->setColor(ccc3(0, 0, 0));
        bg->setOpacity(60);
        addChild(bg, -1);
        m_bg = bg;
    }
    return true;
}

void TRCell::applySubStyle() {
    if (m_bg) {
        auto* bg = static_cast<CCScale9Sprite*>(m_bg);
        bg->setColor(ccc3(48, 72, 108));
        bg->setOpacity(150);
    }
    auto size = getContentSize();
    auto* accent = CCLayerColor::create(ccc4(95, 190, 240, 230), 3.f, std::max(2.f, size.height - 8.f));
    accent->setPosition({ 6.f, 4.f });
    addChild(accent);
}

SectionHeaderCell* SectionHeaderCell::create(std::string const& text) {
    auto* cell = new SectionHeaderCell();
    if (cell && cell->init(text)) {
        cell->autorelease();
        return cell;
    }
    CC_SAFE_DELETE(cell);
    return nullptr;
}

bool SectionHeaderCell::init(std::string const& text) {
    if (!initCell(kCellWidth, 20.f, false)) {
        return false;
    }
    auto size = getContentSize();
    auto* label = CCLabelBMFont::create(text.c_str(), "goldFont.fnt");
    label->setAnchorPoint({ 0.f, 0.5f });
    label->setScale(0.48f);
    label->limitLabelWidth(size.width - 16.f, 0.48f, 0.2f);
    label->setPosition({ 8.f, size.height * 0.5f });
    addChild(label);
    return true;
}

ToggleCell* ToggleCell::create(std::string const& label, std::string const& description, bool on, std::function<void(bool)> callback) {
    auto* cell = new ToggleCell();
    if (cell && cell->init(label, description, on, std::move(callback))) {
        cell->autorelease();
        return cell;
    }
    CC_SAFE_DELETE(cell);
    return nullptr;
}

bool ToggleCell::init(std::string const& label, std::string const& description, bool on, std::function<void(bool)> callback) {
    bool hasDesc = !description.empty();
    if (!initCell(kCellWidth, hasDesc ? 34.f : 28.f)) {
        return false;
    }
    m_callback = std::move(callback);
    m_value = on;
    auto size = getContentSize();

    auto* title = CCLabelBMFont::create(label.c_str(), "bigFont.fnt");
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(size.width - 80.f, 0.46f, 0.2f);
    title->setPosition({ 12.f, hasDesc ? size.height * 0.66f : size.height * 0.5f });
    addChild(title);

    if (hasDesc) {
        auto* desc = CCLabelBMFont::create(description.c_str(), "bigFont.fnt");
        desc->setAnchorPoint({ 0.f, 0.5f });
        desc->setScale(0.3f);
        desc->setColor(ccc3(160, 170, 175));
        desc->limitLabelWidth(size.width - 80.f, 0.3f, 0.16f);
        desc->setPosition({ 12.f, size.height * 0.28f });
        addChild(desc);
    }

    auto* menu = CCMenu::create();
    menu->setContentSize({ 0.f, 0.f });
    menu->setPosition({ size.width - 24.f, size.height * 0.5f });
    addChild(menu);

    auto* toggler = geode::cocos::CCMenuItemExt::createTogglerWithStandardSprites(
        0.6f,
        [this](CCMenuItemToggler*) {
            m_value = !m_value;
            if (m_callback) {
                m_callback(m_value);
            }
        }
    );
    menu->addChild(toggler);
    toggler->toggle(on);

    return true;
}

ButtonCell* ButtonCell::create(std::string const& label, std::string const& buttonText, std::function<void()> callback) {
    auto* cell = new ButtonCell();
    if (cell && cell->init(label, buttonText, std::move(callback))) {
        cell->autorelease();
        return cell;
    }
    CC_SAFE_DELETE(cell);
    return nullptr;
}

bool ButtonCell::init(std::string const& label, std::string const& buttonText, std::function<void()> callback) {
    if (!initCell(kCellWidth, 32.f)) {
        return false;
    }
    m_callback = std::move(callback);
    auto size = getContentSize();

    auto* title = CCLabelBMFont::create(label.c_str(), "bigFont.fnt");
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(size.width - 140.f, 0.46f, 0.2f);
    title->setPosition({ 12.f, size.height * 0.5f });
    addChild(title);

    auto* menu = CCMenu::create();
    menu->setContentSize({ 0.f, 0.f });
    menu->setPosition({ size.width - 66.f, size.height * 0.5f });
    addChild(menu);

    auto* spr = ButtonSprite::create(buttonText.c_str(), 100, 0, 0.5f, true, "bigFont.fnt", "GJ_button_01.png", 24.f);
    auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(
        spr,
        [this](CCMenuItemSpriteExtra*) {
            if (m_callback) {
                m_callback();
            }
        }
    );
    menu->addChild(item);

    return true;
}

InputCell* InputCell::create(std::string const& label, std::string const& value, std::string const& placeholder, bool numeric, std::function<void(std::string const&)> callback) {
    auto* cell = new InputCell();
    if (cell && cell->init(label, value, placeholder, numeric, std::move(callback))) {
        cell->autorelease();
        return cell;
    }
    CC_SAFE_DELETE(cell);
    return nullptr;
}

bool InputCell::init(std::string const& label, std::string const& value, std::string const& placeholder, bool numeric, std::function<void(std::string const&)> callback) {
    if (!initCell(kCellWidth, 34.f)) {
        return false;
    }
    m_callback = std::move(callback);
    auto size = getContentSize();

    auto* title = CCLabelBMFont::create(label.c_str(), "bigFont.fnt");
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(size.width - 150.f, 0.46f, 0.2f);
    title->setPosition({ 12.f, size.height * 0.5f });
    addChild(title);

    auto* input = TextInput::create(118.f, placeholder.c_str(), "bigFont.fnt");
    input->setScale(0.85f);
    input->setString(value);
    if (numeric) {
        input->setCommonFilter(CommonFilter::Uint);
        input->setMaxCharCount(6);
    }
    input->setCallback([this](std::string const& text) {
        if (m_callback) {
            m_callback(text);
        }
    });
    input->setPosition({ size.width - 66.f, size.height * 0.5f });
    addChild(input);

    return true;
}

ComboCell* ComboCell::create(std::string const& label, std::vector<std::string> options, int index, std::function<void(int)> callback) {
    auto* cell = new ComboCell();
    if (cell && cell->init(label, std::move(options), index, std::move(callback))) {
        cell->autorelease();
        return cell;
    }
    CC_SAFE_DELETE(cell);
    return nullptr;
}

bool ComboCell::init(std::string const& label, std::vector<std::string> options, int index, std::function<void(int)> callback) {
    if (!initCell(kCellWidth, 32.f)) {
        return false;
    }
    m_options = std::move(options);
    m_callback = std::move(callback);
    if (m_options.empty()) {
        m_options.push_back("");
    }
    m_index = std::clamp(index, 0, static_cast<int>(m_options.size()) - 1);
    auto size = getContentSize();

    auto* title = CCLabelBMFont::create(label.c_str(), "bigFont.fnt");
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(size.width - 150.f, 0.46f, 0.2f);
    title->setPosition({ 12.f, size.height * 0.5f });
    addChild(title);

    auto* menu = CCMenu::create();
    menu->setContentSize({ 0.f, 0.f });
    menu->setPosition({ size.width - 76.f, size.height * 0.5f });
    addChild(menu);

    auto* button = ButtonSprite::create(m_options[m_index].c_str(), 130, 0, 0.46f, true, "bigFont.fnt", "GJ_button_04.png", 26.f);
    m_valueNode = button;
    auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(button, [this](CCMenuItemSpriteExtra*) {
        if (m_options.empty()) {
            return;
        }
        m_index = (m_index + 1) % static_cast<int>(m_options.size());
        static_cast<ButtonSprite*>(m_valueNode)->setString(m_options[m_index].c_str());
        if (m_callback) {
            m_callback(m_index);
        }
    });
    menu->addChild(item);

    return true;
}

void ComboCell::setIndex(int index) {
    if (m_options.empty()) {
        return;
    }
    m_index = std::clamp(index, 0, static_cast<int>(m_options.size()) - 1);
    if (m_valueNode) {
        static_cast<ButtonSprite*>(m_valueNode)->setString(m_options[m_index].c_str());
    }
}

SliderCell* SliderCell::create(std::string const& label, float value, float minValue, float maxValue, std::function<void(float)> callback) {
    auto* cell = new SliderCell();
    if (cell && cell->init(label, value, minValue, maxValue, std::move(callback))) {
        cell->autorelease();
        return cell;
    }
    CC_SAFE_DELETE(cell);
    return nullptr;
}

bool SliderCell::init(std::string const& label, float value, float minValue, float maxValue, std::function<void(float)> callback) {
    if (!initCell(kCellWidth, 30.f)) {
        return false;
    }
    m_callback = std::move(callback);
    m_min = minValue;
    m_max = maxValue;
    m_decimals = (m_max - m_min) <= 3.0f ? 2 : 0;
    auto size = getContentSize();

    auto* title = CCLabelBMFont::create(label.c_str(), "bigFont.fnt");
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(120.f, 0.46f, 0.18f);
    title->setPosition({ 12.f, size.height * 0.5f });
    addChild(title);

    auto* slider = Slider::create(this, menu_selector(SliderCell::onSlider), 0.42f);
    slider->setPosition({ 232.f, size.height * 0.5f });
    addChild(slider);
    m_sliderNode = slider;

    auto* valueLabel = CCLabelBMFont::create("", "bigFont.fnt");
    valueLabel->setAnchorPoint({ 1.f, 0.5f });
    valueLabel->setScale(0.4f);
    valueLabel->setColor(ccc3(180, 210, 235));
    valueLabel->setPosition({ size.width - 10.f, size.height * 0.5f });
    addChild(valueLabel);
    m_valueNode = valueLabel;

    setValue(value);
    return true;
}

void SliderCell::onSlider(CCObject*) {
    if (!m_sliderNode) {
        return;
    }
    float t = static_cast<Slider*>(m_sliderNode)->getValue();
    float v = m_min + t * (m_max - m_min);
    refreshValueLabel(v);
    if (m_callback) {
        m_callback(v);
    }
}

void SliderCell::setValue(float value) {
    value = std::clamp(value, m_min, m_max);
    if (m_sliderNode) {
        float t = (m_max > m_min) ? (value - m_min) / (m_max - m_min) : 0.f;
        auto* slider = static_cast<Slider*>(m_sliderNode);
        slider->setValue(std::clamp(t, 0.f, 1.f));
        slider->updateBar();
    }
    refreshValueLabel(value);
}

float SliderCell::value() const {
    if (!m_sliderNode) {
        return m_min;
    }
    float t = static_cast<Slider*>(m_sliderNode)->getValue();
    return m_min + t * (m_max - m_min);
}

void SliderCell::refreshValueLabel(float value) {
    if (!m_valueNode) {
        return;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", m_decimals, value);
    static_cast<CCLabelBMFont*>(m_valueNode)->setString(buffer);
}

ColorCell* ColorCell::create(std::string const& label, ccColor4B color, std::function<void(ccColor4B)> callback) {
    auto* cell = new ColorCell();
    if (cell && cell->init(label, color, std::move(callback))) {
        cell->autorelease();
        return cell;
    }
    CC_SAFE_DELETE(cell);
    return nullptr;
}

bool ColorCell::init(std::string const& label, ccColor4B color, std::function<void(ccColor4B)> callback) {
    if (!initCell(kCellWidth, 30.f)) {
        return false;
    }
    m_callback = std::move(callback);
    auto size = getContentSize();

    auto* title = CCLabelBMFont::create(label.c_str(), "bigFont.fnt");
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(size.width - 90.f, 0.46f, 0.2f);
    title->setPosition({ 12.f, size.height * 0.5f });
    addChild(title);

    auto* container = CCNode::create();
    container->setContentSize({ 46.f, 20.f });
    container->setAnchorPoint({ 0.5f, 0.5f });
    auto* border = CCLayerColor::create(ccc4(255, 255, 255, 110), 46.f, 20.f);
    container->addChild(border);
    auto* swatch = CCLayerColor::create(color, 42.f, 16.f);
    swatch->setPosition({ 2.f, 2.f });
    container->addChild(swatch);
    m_swatch = swatch;

    auto* menu = CCMenu::create();
    menu->setContentSize({ 0.f, 0.f });
    menu->setPosition({ size.width - 34.f, size.height * 0.5f });
    auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(container, [this](CCMenuItemSpriteExtra*) {
        auto* sw = static_cast<CCLayerColor*>(m_swatch);
        ccColor3B current = sw->getColor();
        ccColor4B start = ccc4(current.r, current.g, current.b, sw->getOpacity());
        auto* popup = geode::ColorPickPopup::create(start);
        popup->setCallback([this](ccColor4B picked) {
            if (m_swatch) {
                auto* node = static_cast<CCLayerColor*>(m_swatch);
                node->setColor(ccc3(picked.r, picked.g, picked.b));
                node->setOpacity(picked.a);
            }
            if (m_callback) {
                m_callback(picked);
            }
        });
        popup->show();
    });
    menu->addChild(item);
    addChild(menu);

    return true;
}

KeybindCell* KeybindCell::create(std::string const& label, int* keyPtr) {
    auto* cell = new KeybindCell();
    if (cell && cell->init(label, keyPtr)) {
        cell->autorelease();
        return cell;
    }
    CC_SAFE_DELETE(cell);
    return nullptr;
}

bool KeybindCell::init(std::string const& label, int* keyPtr) {
    if (!initCell(kCellWidth, 30.f)) {
        return false;
    }
    m_keyPtr = keyPtr;
    auto size = getContentSize();

    auto* title = CCLabelBMFont::create(label.c_str(), "bigFont.fnt");
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(size.width - 130.f, 0.46f, 0.2f);
    title->setPosition({ 12.f, size.height * 0.5f });
    addChild(title);

    auto* menu = CCMenu::create();
    menu->setContentSize({ 0.f, 0.f });
    menu->setPosition({ size.width - 62.f, size.height * 0.5f });
    addChild(menu);

    std::string current = m_keyPtr ? toasty::frontend::keyName(*m_keyPtr) : "None";
    auto* spr = ButtonSprite::create(current.c_str(), 96, 0, 0.46f, true, "bigFont.fnt", "GJ_button_04.png", 26.f);
    m_button = spr;
    auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(spr, [this](CCMenuItemSpriteExtra* sender) {
        this->onTap(sender);
    });
    menu->addChild(item);

    return true;
}

void KeybindCell::onTap(CCObject*) {
    if (!m_keyPtr) {
        return;
    }
    toasty::frontend::beginRebind(m_keyPtr);
    if (m_button) {
        static_cast<ButtonSprite*>(m_button)->setString("...");
    }
    this->unschedule(schedule_selector(KeybindCell::pollRebind));
    this->schedule(schedule_selector(KeybindCell::pollRebind), 0.08f);
}

void KeybindCell::pollRebind(float) {
    if (!m_keyPtr) {
        this->unschedule(schedule_selector(KeybindCell::pollRebind));
        return;
    }
    if (toasty::frontend::isRebinding(m_keyPtr)) {
        return;
    }
    this->unschedule(schedule_selector(KeybindCell::pollRebind));
    refreshLabel();
    toasty::frontend::persistSettings();
}

void KeybindCell::refreshLabel() {
    if (m_button && m_keyPtr) {
        static_cast<ButtonSprite*>(m_button)->setString(toasty::frontend::keyName(*m_keyPtr).c_str());
    }
}

}
