#include "gui/cocos/tr_replay_actions_popup.hpp"

#include "gui/cocos/tr_frame_editor_popup.hpp"
#include "conversion/macro_converter.hpp"
#include "ToastyReplay.hpp"
#include "utils.hpp"
#include "lang/localization.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

using namespace geode::prelude;

namespace toasty::frontend {

namespace {
    constexpr float kPopupWidth = 300.f;
    constexpr float kPopupHeight = 240.f;

    std::string localized(std::string_view text) {
        return std::string(toasty::lang::tr(text));
    }

    std::string macroExtension(std::string const& name, bool isTTR) {
        namespace fs = std::filesystem;
        auto directory = ReplayStorage::getReplayDirectoryPath();
        std::error_code ec;
        for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) {
                continue;
            }
            if (toasty::pathToUtf8(it->path().stem()) == name) {
                return toasty::pathToUtf8(it->path().extension());
            }
        }
        return isTTR ? ".ttr" : ".gdr";
    }
}

bool TRReplayActionsPopup::init() {
    if (!Popup::init(kPopupWidth, kPopupHeight, "GJ_square01.png")) {
        return false;
    }
    this->setID("cocos-replay-actions"_spr);
    if (m_closeBtn) {
        m_closeBtn->setScale(0.8f);
    }

    auto* title = CCLabelBMFont::create(m_name.c_str(), "goldFont.fnt");
    title->setScale(0.5f);
    title->limitLabelWidth(kPopupWidth - 60.f, 0.5f, 0.2f);
    title->setPosition({ kPopupWidth * 0.5f, kPopupHeight - 22.f });
    m_mainLayer->addChild(title, 10);

    auto renameText = localized("Rename");
    auto* renameLabel = CCLabelBMFont::create(renameText.c_str(), "bigFont.fnt");
    renameLabel->setScale(0.42f);
    renameLabel->setAnchorPoint({ 0.f, 0.5f });
    renameLabel->setPosition({ 20.f, kPopupHeight - 52.f });
    m_mainLayer->addChild(renameLabel);

    auto newNameText = localized("New name");
    m_renameInput = TextInput::create(150.f, newNameText.c_str(), "bigFont.fnt");
    m_renameInput->setScale(0.85f);
    m_renameInput->setString(m_name);
    m_renameInput->setPosition({ 130.f, kPopupHeight - 52.f });
    m_mainLayer->addChild(m_renameInput);

    auto* renameMenu = CCMenu::create();
    renameMenu->setPosition({ 0.f, 0.f });
    auto saveText = localized("Save");
    auto* renameSpr = ButtonSprite::create(saveText.c_str(), 30, 0, 0.5f, false, "bigFont.fnt", "GJ_button_01.png", 24.f);
    auto* renameItem = geode::cocos::CCMenuItemExt::createSpriteExtra(renameSpr, [this](CCMenuItemSpriteExtra*) {
        this->doRename();
    });
    renameItem->setPosition({ kPopupWidth - 46.f, kPopupHeight - 52.f });
    renameMenu->addChild(renameItem);
    m_mainLayer->addChild(renameMenu);

    auto* actionMenu = CCMenu::create();
    actionMenu->setPosition({ 0.f, 0.f });

    float actionY = kPopupHeight - 92.f;
    auto addAction = [&](const char* label, const char* bg, std::function<void()> action) {
        auto displayLabel = localized(label);
        auto* spr = ButtonSprite::create(displayLabel.c_str(), 140, 0, 0.6f, false, "bigFont.fnt", bg, 28.f);
        auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(spr, [action](CCMenuItemSpriteExtra*) {
            action();
        });
        item->setPosition({ kPopupWidth * 0.5f, actionY });
        actionMenu->addChild(item);
        actionY -= 34.f;
    };

    addAction("Edit", "GJ_button_04.png", [this]() { this->doEdit(); });
    if (!m_isTTR) {
        addAction("Convert to TTR3", "GJ_button_03.png", [this]() { this->doConvert(); });
    } else {
        addAction("Convert to GDR", "GJ_button_03.png", [this]() { this->doConvertToGdr(); });
    }
    addAction("Upload", "GJ_button_02.png", [this]() {
        auto onUpload = m_onUpload;
        this->removeFromParentAndCleanup(true);
        if (onUpload) {
            onUpload();
        }
    });
    addAction("Delete", "GJ_button_06.png", [this]() { this->doDelete(); });

    m_mainLayer->addChild(actionMenu);

    return true;
}

void TRReplayActionsPopup::notifyChanged() {
    if (m_onChanged) {
        m_onChanged();
    }
}

void TRReplayActionsPopup::doRename() {
    if (!m_renameInput) {
        return;
    }

    auto* engine = ReplayEngine::get();
    if (engine && engine->engineMode == MODE_CAPTURE) {
        Notification::create("Stop recording first", NotificationIcon::Warning, 0.8f)->show();
        return;
    }

    std::string requested = ReplayStorage::sanitizeReplayName(m_renameInput->getString());
    if (requested.empty()) {
        Notification::create("Name cannot be empty", NotificationIcon::Warning, 0.9f)->show();
        return;
    }

    namespace fs = std::filesystem;
    auto directory = ReplayStorage::getReplayDirectoryPath();
    std::error_code ec;
    fs::path sourcePath;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && toasty::pathToUtf8(it->path().stem()) == m_name) {
            sourcePath = it->path();
            break;
        }
    }
    if (sourcePath.empty()) {
        Notification::create("Replay file not found", NotificationIcon::Error, 1.0f)->show();
        return;
    }

    std::string resolved = ReplayStorage::makeUniqueReplayName(requested, m_name);
    auto destPath = sourcePath.parent_path() / (resolved + toasty::pathToUtf8(sourcePath.extension()));
    if (destPath != sourcePath) {
        fs::rename(sourcePath, destPath, ec);
        if (ec) {
            Notification::create("Failed to rename", NotificationIcon::Error, 1.0f)->show();
            return;
        }
    }

    if (engine && engine->hasMacro() && engine->getMacroName() == m_name) {
        engine->discardActiveMacro();
        if (m_isTTR) {
            if (TTRMacro* loaded = TTRMacro::loadFromDisk(resolved)) {
                engine->activeTTR = loaded;
                engine->ttrMode = true;
                engine->clearActiveMacroDirty();
            }
        } else {
            if (MacroSequence* loaded = MacroSequence::loadFromDisk(resolved)) {
                engine->activeMacro = loaded;
                engine->ttrMode = false;
                engine->clearActiveMacroDirty();
            }
        }
    }

    Notification::create("Renamed to " + resolved, NotificationIcon::Success, 0.9f)->show();
    notifyChanged();
    this->removeFromParentAndCleanup(true);
}

void TRReplayActionsPopup::doConvert() {
    auto* engine = ReplayEngine::get();
    if (engine && engine->engineMode == MODE_CAPTURE) {
        Notification::create("Stop recording first", NotificationIcon::Warning, 0.8f)->show();
        return;
    }

    auto directory = ReplayStorage::getReplayDirectoryPath();
    auto sourcePath = directory / (m_name + macroExtension(m_name, m_isTTR));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(sourcePath, ec) || ec) {
        Notification::create("Replay file not found", NotificationIcon::Error, 1.0f)->show();
        return;
    }

    std::string author = GJAccountManager::get() ? GJAccountManager::get()->m_username : "";
    auto result = toasty::conversion::convertNativeGDRToTTRDuplicate(sourcePath, author, directory);
    if (result.ok) {
        Notification::create("Converted to " + result.outputName, NotificationIcon::Success, 1.2f)->show();
        notifyChanged();
        this->removeFromParentAndCleanup(true);
    } else {
        std::string message = result.message.empty() ? "Conversion failed" : result.message;
        Notification::create(message, NotificationIcon::Error, 1.4f)->show();
    }
}

void TRReplayActionsPopup::doConvertToGdr() {
    auto* engine = ReplayEngine::get();
    if (engine && engine->engineMode == MODE_CAPTURE) {
        Notification::create("Stop recording first", NotificationIcon::Warning, 0.8f)->show();
        return;
    }

    auto directory = ReplayStorage::getReplayDirectoryPath();
    auto sourcePath = directory / (m_name + macroExtension(m_name, m_isTTR));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(sourcePath, ec) || ec) {
        Notification::create("Replay file not found", NotificationIcon::Error, 1.0f)->show();
        return;
    }

    std::string author = GJAccountManager::get() ? GJAccountManager::get()->m_username : "";
    auto result = toasty::conversion::convertReplay(
        sourcePath,
        toasty::conversion::ConversionTarget::GDR,
        m_name,
        author,
        directory);
    if (result.ok) {
        Notification::create("Converted to " + result.outputName, NotificationIcon::Success, 1.2f)->show();
        notifyChanged();
        this->removeFromParentAndCleanup(true);
    } else {
        std::string message = result.message.empty() ? "Conversion failed" : result.message;
        Notification::create(message, NotificationIcon::Error, 1.4f)->show();
    }
}

void TRReplayActionsPopup::doEdit() {
    auto* engine = ReplayEngine::get();
    if (engine && engine->engineMode == MODE_CAPTURE) {
        Notification::create("Stop recording first", NotificationIcon::Warning, 0.8f)->show();
        return;
    }
    engine->reloadMacroList();
    if (engine->cbsMacros.count(m_name)) {
        Notification::create("CBS macros cannot be edited", NotificationIcon::Warning, 1.2f)->show();
        return;
    }

    auto* editor = TRFrameEditorPopup::create(m_name, m_isTTR);
    if (editor) {
        this->removeFromParentAndCleanup(true);
        editor->show();
    } else {
        Notification::create("Could not open editor", NotificationIcon::Error, 1.0f)->show();
    }
}

void TRReplayActionsPopup::doDelete() {
    auto* engine = ReplayEngine::get();
    if (engine && engine->engineMode == MODE_CAPTURE) {
        Notification::create("Stop recording first", NotificationIcon::Warning, 0.8f)->show();
        return;
    }

    std::string name = m_name;
    bool isTTR = m_isTTR;
    auto onChanged = m_onChanged;
    geode::WeakRef<TRReplayActionsPopup> source(this);

    geode::createQuickPopup(
        "Delete Macro",
        "Delete <cr>" + name + "</c>? This cannot be undone.",
        "Cancel",
        "Delete",
        [source, name, isTTR, onChanged](FLAlertLayer*, bool confirm) {
            if (!confirm) {
                return;
            }

            namespace fs = std::filesystem;
            auto directory = ReplayStorage::getReplayDirectoryPath();
            std::error_code ec;
            bool deleted = false;
            for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file() || toasty::pathToUtf8(it->path().stem()) != name) {
                    continue;
                }
                std::string ext = toasty::pathToUtf8(it->path().extension());
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                bool match = isTTR ? (ext == ".ttr" || ext == ".ttr2" || ext == ".ttr3") : (ext == ".gdr");
                if (match) {
                    std::error_code removeEc;
                    if (fs::remove(it->path(), removeEc)) {
                        deleted = true;
                    }
                }
            }

            if (deleted) {
                auto* e = ReplayEngine::get();
                if (e && e->hasMacro() && e->getMacroName() == name) {
                    e->discardActiveMacro();
                }
                Notification::create("Deleted " + name, NotificationIcon::Success, 0.9f)->show();
                if (onChanged) {
                    onChanged();
                }
                if (auto popup = source.lock()) {
                    popup->removeFromParentAndCleanup(true);
                }
            } else {
                Notification::create("Failed to delete", NotificationIcon::Error, 1.0f)->show();
            }
        }
    );
}

TRReplayActionsPopup* TRReplayActionsPopup::create(std::string name, bool isTTR, std::function<void()> onChanged, std::function<void()> onUpload) {
    auto* popup = new TRReplayActionsPopup();
    popup->m_name = std::move(name);
    popup->m_isTTR = isTTR;
    popup->m_onChanged = std::move(onChanged);
    popup->m_onUpload = std::move(onUpload);
    if (popup->init()) {
        popup->autorelease();
        return popup;
    }
    CC_SAFE_DELETE(popup);
    return nullptr;
}

}
