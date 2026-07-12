#pragma once

#include <Geode/ui/Popup.hpp>
#include <functional>
#include <string>
#include <vector>

namespace toasty::frontend {

inline constexpr float kCellWidth = 326.f;

class TRCell : public cocos2d::CCNode {
protected:
    cocos2d::CCNode* m_bg = nullptr;
    bool initCell(float width, float height, bool withBackground = true);

public:
    void applySubStyle();
};

class SectionHeaderCell : public TRCell {
protected:
    bool init(std::string const& text);

public:
    static SectionHeaderCell* create(std::string const& text);
};

class ToggleCell : public TRCell {
protected:
    std::function<void(bool)> m_callback;
    bool m_value = false;
    bool init(std::string const& label, std::string const& description, bool on, std::function<void(bool)> callback);

public:
    static ToggleCell* create(std::string const& label, std::string const& description, bool on, std::function<void(bool)> callback);
};

class ButtonCell : public TRCell {
protected:
    std::function<void()> m_callback;
    bool init(std::string const& label, std::string const& buttonText, std::function<void()> callback);

public:
    static ButtonCell* create(std::string const& label, std::string const& buttonText, std::function<void()> callback);
};

class InputCell : public TRCell {
protected:
    std::function<void(std::string const&)> m_callback;
    bool init(std::string const& label, std::string const& value, std::string const& placeholder, bool numeric, std::function<void(std::string const&)> callback);

public:
    static InputCell* create(std::string const& label, std::string const& value, std::string const& placeholder, bool numeric, std::function<void(std::string const&)> callback);
};

class ComboCell : public TRCell {
protected:
    std::vector<std::string> m_options;
    int m_index = 0;
    std::function<void(int)> m_callback;
    cocos2d::CCNode* m_valueNode = nullptr;
    bool init(std::string const& label, std::vector<std::string> options, int index, std::function<void(int)> callback);

public:
    static ComboCell* create(std::string const& label, std::vector<std::string> options, int index, std::function<void(int)> callback);
    void setIndex(int index);
    int index() const { return m_index; }
};

class SliderCell : public TRCell {
protected:
    cocos2d::CCNode* m_sliderNode = nullptr;
    cocos2d::CCNode* m_valueNode = nullptr;
    float m_min = 0.f;
    float m_max = 1.f;
    int m_decimals = 2;
    std::function<void(float)> m_callback;
    bool init(std::string const& label, float value, float minValue, float maxValue, std::function<void(float)> callback);
    void refreshValueLabel(float value);

public:
    static SliderCell* create(std::string const& label, float value, float minValue, float maxValue, std::function<void(float)> callback);
    void onSlider(cocos2d::CCObject* sender);
    void setValue(float value);
    float value() const;
};

class ColorCell : public TRCell {
protected:
    cocos2d::CCNode* m_swatch = nullptr;
    std::function<void(cocos2d::ccColor4B)> m_callback;
    bool init(std::string const& label, cocos2d::ccColor4B color, std::function<void(cocos2d::ccColor4B)> callback);

public:
    static ColorCell* create(std::string const& label, cocos2d::ccColor4B color, std::function<void(cocos2d::ccColor4B)> callback);
};

class KeybindCell : public TRCell {
protected:
    std::string m_settingKey;
    cocos2d::CCNode* m_button = nullptr;
    std::string m_display;
    bool init(std::string const& label, std::string settingKey);
    void refreshLabel();
    void pollSetting(float dt);

public:
    static KeybindCell* create(std::string const& label, std::string settingKey);
    void onTap(cocos2d::CCObject* sender);
};

}
