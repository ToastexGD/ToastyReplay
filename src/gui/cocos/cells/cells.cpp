#include "gui/cocos/cells/cells.hpp"
#include "gui/cocos/frontend.hpp"
#include "gui/cocos/localized_label.hpp"
#include "lang/localization.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <algorithm>
#include <cstdio>

using namespace geode::prelude;

namespace toasty::frontend {

namespace {
    std::string localized(std::string_view text) {
        return std::string(toasty::lang::tr(text));
    }

    ccColor3B toColor(CocosColor const& color) {
        return ccc3(color[0], color[1], color[2]);
    }

    CCScale9Sprite* makeSurface(float width, float height, ccColor3B color, GLubyte opacity) {
        auto* surface = CCScale9Sprite::create("GJ_square05.png");
        CCSize natural = surface->getContentSize();
        constexpr float inset = 8.f;
        surface->setCapInsets(CCRect(inset, inset, std::max(1.f, natural.width - inset * 2.f), std::max(1.f, natural.height - inset * 2.f)));
        surface->setContentSize({ width, height });
        surface->setColor(color);
        surface->setOpacity(opacity);
        return surface;
    }

    CCNode* makeToggleTrack(bool active) {
        auto theme = cocosTheme();
        auto* node = CCNode::create();
        node->setContentSize({ 48.f, 24.f });
        node->setAnchorPoint({ 0.5f, 0.5f });

        auto* track = makeSurface(48.f, 22.f, toColor(active ? theme.accent : theme.inactive), 255);
        track->setPosition({ 24.f, 12.f });
        node->addChild(track);

        auto* knob = makeSurface(17.f, 17.f, toColor(active ? theme.sectionText : theme.mutedText), 255);
        knob->setPosition({ active ? 36.f : 12.f, 12.f });
        node->addChild(knob);

        auto stateText = localized(active ? "ON" : "OFF");
        auto* state = LocalizedLabel::create(stateText);
        state->setScale(active ? 0.25f : 0.22f);
        state->setColor(toColor(active ? theme.sectionText : theme.mutedText));
        state->setPosition({ active ? 13.f : 34.f, 12.f });
        node->addChild(state);
        return node;
    }
}

bool TRCell::initCell(float width, float height, bool withBackground) {
    if (!CCNode::init()) {
        return false;
    }
    setContentSize({ width, height });
    setAnchorPoint({ 0.5f, 0.5f });

    if (withBackground) {
        auto theme = cocosTheme();
        auto* border = makeSurface(width, height, toColor(theme.cellBorder), 225);
        border->setPosition({ width * 0.5f, height * 0.5f });
        addChild(border, -2);

        auto* bg = makeSurface(width - 2.f, height - 2.f, toColor(theme.cell), 245);
        bg->setPosition({ width * 0.5f, height * 0.5f });
        addChild(bg, -1);
        m_bg = bg;
    }
    return true;
}

void TRCell::applySubStyle() {
    auto theme = cocosTheme();
    if (m_bg) {
        auto* bg = static_cast<CCScale9Sprite*>(m_bg);
        bg->setColor(toColor(theme.subCell));
        bg->setOpacity(250);
    }
    auto size = getContentSize();
    auto accentColor = toColor(theme.secondary);
    auto* accent = CCLayerColor::create(ccc4(accentColor.r, accentColor.g, accentColor.b, 245), 3.f, std::max(2.f, size.height - 12.f));
    accent->setPosition({ 6.f, 6.f });
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
    if (!initCell(kCellWidth, 24.f, false)) {
        return false;
    }
    auto size = getContentSize();
    auto theme = cocosTheme();
    auto* marker = CCSprite::create("smallDot.png");
    if (marker) {
        marker->setScale(6.f / std::max(1.f, marker->getContentSize().width));
        marker->setColor(toColor(theme.accent));
        marker->setPosition({ 10.f, size.height * 0.5f });
        addChild(marker);
    }

    auto displayText = localized(text);
    auto* label = LocalizedLabel::create(displayText);
    label->setAnchorPoint({ 0.f, 0.5f });
    label->setScale(0.43f);
    label->setColor(toColor(theme.sectionText));
    label->limitLabelWidth(size.width - 34.f, 0.43f, 0.2f);
    label->setPosition({ 19.f, size.height * 0.5f + 1.f });
    addChild(label);
    auto lineColor = toColor(theme.secondary);
    auto* line = CCLayerColor::create(ccc4(lineColor.r, lineColor.g, lineColor.b, 125), size.width - 18.f, 1.f);
    line->setPosition({ 9.f, 1.f });
    addChild(line);
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
    if (!initCell(kCellWidth, hasDesc ? 40.f : 32.f)) {
        return false;
    }
    m_callback = std::move(callback);
    m_value = on;
    auto size = getContentSize();

    auto displayLabel = localized(label);
    auto* title = LocalizedLabel::create(displayLabel);
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(size.width - 96.f, 0.46f, 0.2f);
    title->setPosition({ 14.f, hasDesc ? size.height * 0.68f : size.height * 0.5f });
    addChild(title);

    if (hasDesc) {
        auto displayDescription = localized(description);
        auto* desc = LocalizedLabel::create(displayDescription);
        desc->setAnchorPoint({ 0.f, 0.5f });
        desc->setScale(0.3f);
        desc->setColor(toColor(cocosTheme().mutedText));
        desc->limitLabelWidth(size.width - 96.f, 0.3f, 0.16f);
        desc->setPosition({ 14.f, size.height * 0.28f });
        addChild(desc);
    }

    auto* menu = CCMenu::create();
    menu->setContentSize({ 0.f, 0.f });
    menu->setPosition({ size.width - 34.f, size.height * 0.5f });
    addChild(menu);

    auto* toggler = geode::cocos::CCMenuItemExt::createToggler(
        makeToggleTrack(true),
        makeToggleTrack(false),
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
    if (!initCell(kCellWidth, 34.f)) {
        return false;
    }
    m_callback = std::move(callback);
    auto size = getContentSize();

    auto displayLabel = localized(label);
    auto* title = LocalizedLabel::create(displayLabel);
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(size.width - 140.f, 0.46f, 0.2f);
    title->setPosition({ 14.f, size.height * 0.5f });
    addChild(title);

    auto* menu = CCMenu::create();
    menu->setContentSize({ 0.f, 0.f });
    menu->setPosition({ size.width - 66.f, size.height * 0.5f });
    addChild(menu);

    auto displayButtonText = localized(buttonText);
    auto* spr = createLocalizedButtonSprite(displayButtonText, 100, 0, 0.5f, true, "GJ_button_01.png", 24.f);
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
    if (!initCell(kCellWidth, 36.f)) {
        return false;
    }
    m_callback = std::move(callback);
    auto size = getContentSize();

    auto displayLabel = localized(label);
    auto* title = LocalizedLabel::create(displayLabel);
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(size.width - 150.f, 0.46f, 0.2f);
    title->setPosition({ 14.f, size.height * 0.5f });
    addChild(title);

    auto displayPlaceholder = localized(placeholder);
    auto* input = TextInput::create(118.f, displayPlaceholder.c_str(), "bigFont.fnt");
    input->setScale(0.85f);
    input->setString(value);
    input->getBGSprite()->setColor(toColor(cocosTheme().content));
    input->getBGSprite()->setOpacity(245);
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
    if (!initCell(kCellWidth, 34.f)) {
        return false;
    }
    m_options.reserve(options.size());
    for (auto const& option : options) {
        m_options.push_back(localized(option));
    }
    m_callback = std::move(callback);
    if (m_options.empty()) {
        m_options.push_back("");
    }
    m_index = std::clamp(index, 0, static_cast<int>(m_options.size()) - 1);
    auto size = getContentSize();

    auto displayLabel = localized(label);
    auto* title = LocalizedLabel::create(displayLabel);
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(size.width - 150.f, 0.46f, 0.2f);
    title->setPosition({ 14.f, size.height * 0.5f });
    addChild(title);

    auto* menu = CCMenu::create();
    menu->setContentSize({ 0.f, 0.f });
    menu->setPosition({ size.width - 76.f, size.height * 0.5f });
    addChild(menu);

    auto* button = createLocalizedButtonSprite(m_options[m_index], 130, 0, 0.46f, true, "GJ_button_04.png", 26.f);
    m_valueNode = button;
    auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(button, [this](CCMenuItemSpriteExtra*) {
        if (m_options.empty()) {
            return;
        }
        m_index = (m_index + 1) % static_cast<int>(m_options.size());
        setLocalizedButtonString(static_cast<ButtonSprite*>(m_valueNode), m_options[m_index]);
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
        setLocalizedButtonString(static_cast<ButtonSprite*>(m_valueNode), m_options[m_index]);
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
    if (!initCell(kCellWidth, 34.f)) {
        return false;
    }
    m_callback = std::move(callback);
    m_min = minValue;
    m_max = maxValue;
    m_decimals = (m_max - m_min) <= 3.0f ? 2 : 0;
    auto size = getContentSize();

    auto displayLabel = localized(label);
    auto* title = LocalizedLabel::create(displayLabel);
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(120.f, 0.46f, 0.18f);
    title->setPosition({ 14.f, size.height * 0.5f });
    addChild(title);

    auto* slider = Slider::create(this, menu_selector(SliderCell::onSlider), 0.42f);
    slider->setPosition({ 232.f, size.height * 0.5f });
    addChild(slider);
    m_sliderNode = slider;

    auto* valueLabel = CCLabelBMFont::create("", "bigFont.fnt");
    valueLabel->setAnchorPoint({ 1.f, 0.5f });
    valueLabel->setScale(0.4f);
    valueLabel->setColor(toColor(cocosTheme().secondary));
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
    if (!initCell(kCellWidth, 34.f)) {
        return false;
    }
    m_callback = std::move(callback);
    auto size = getContentSize();

    auto displayLabel = localized(label);
    auto* title = LocalizedLabel::create(displayLabel);
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(size.width - 90.f, 0.46f, 0.2f);
    title->setPosition({ 14.f, size.height * 0.5f });
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
        geode::WeakRef<ColorCell> cell(this);
        popup->setCallback([cell](ccColor4B picked) {
            auto locked = cell.lock();
            if (!locked) {
                return;
            }
            if (locked->m_swatch) {
                auto* node = static_cast<CCLayerColor*>(locked->m_swatch);
                node->setColor(ccc3(picked.r, picked.g, picked.b));
                node->setOpacity(picked.a);
            }
            if (locked->m_callback) {
                locked->m_callback(picked);
            }
        });
        popup->show();
    });
    menu->addChild(item);
    addChild(menu);

    return true;
}

KeybindCell* KeybindCell::create(std::string const& label, std::string settingKey) {
    auto* cell = new KeybindCell();
    if (cell && cell->init(label, std::move(settingKey))) {
        cell->autorelease();
        return cell;
    }
    CC_SAFE_DELETE(cell);
    return nullptr;
}

bool KeybindCell::init(std::string const& label, std::string settingKey) {
    if (!initCell(kCellWidth, 34.f)) {
        return false;
    }
    m_settingKey = std::move(settingKey);
    auto size = getContentSize();

    auto displayLabel = localized(label);
    auto* title = LocalizedLabel::create(displayLabel);
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setScale(0.46f);
    title->limitLabelWidth(size.width - 130.f, 0.46f, 0.2f);
    title->setPosition({ 14.f, size.height * 0.5f });
    addChild(title);

    auto* menu = CCMenu::create();
    menu->setContentSize({ 0.f, 0.f });
    menu->setPosition({ size.width - 62.f, size.height * 0.5f });
    addChild(menu);

    m_display = toasty::frontend::keybindDisplay(m_settingKey);
    auto* spr = createLocalizedButtonSprite(m_display, 96, 0, 0.46f, true, "GJ_button_04.png", 26.f);
    m_button = spr;
    auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(spr, [this](CCMenuItemSpriteExtra* sender) {
        this->onTap(sender);
    });
    menu->addChild(item);
    this->schedule(schedule_selector(KeybindCell::pollSetting), 0.25f);

    return true;
}

void KeybindCell::onTap(CCObject*) {
    toasty::frontend::openKeybindEditor();
}

void KeybindCell::pollSetting(float) {
    refreshLabel();
}

void KeybindCell::refreshLabel() {
    if (!m_button) {
        return;
    }
    std::string current = toasty::frontend::keybindDisplay(m_settingKey);
    if (current != m_display) {
        m_display = std::move(current);
        setLocalizedButtonString(static_cast<ButtonSprite*>(m_button), m_display);
    }
}

}
