#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <functional>
#include <string>

namespace toasty::frontend {

class TRReplayActionsPopup : public geode::Popup {
protected:
    std::string m_name;
    bool m_isTTR = false;
    std::function<void()> m_onChanged;
    std::function<void()> m_onUpload;
    geode::TextInput* m_renameInput = nullptr;

    bool init() override;
    void doRename();
    void doConvert();
    void doConvertToGdr();
    void doEdit();
    void doDelete();
    void notifyChanged();

public:
    static TRReplayActionsPopup* create(std::string name, bool isTTR, std::function<void()> onChanged, std::function<void()> onUpload);
};

}
