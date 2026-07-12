#include "gui/credits_popup.hpp"
#include "gui/cocos/moving_background.hpp"
#include "utils.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {
constexpr float kPanelWidth = 500.0f;

const ccColor3B kColorGold = ccc3(255, 218, 72);
const ccColor3B kColorWhite = ccc3(245, 250, 255);
const ccColor3B kColorMuted = ccc3(185, 198, 220);

class CreditsPage;
CreditsPage* s_openPage = nullptr;
bool s_openPageQueued = false;

std::vector<std::string> wrapText(std::string message, size_t maxLineLength, size_t maxLines) {
    std::vector<std::string> lines;
    while (message.size() > maxLineLength && lines.size() + 1 < maxLines) {
        auto split = message.rfind(' ', maxLineLength);
        if (split == std::string::npos || split < maxLineLength / 2) {
            split = message.find(' ', maxLineLength);
        }
        if (split == std::string::npos) break;
        lines.push_back(message.substr(0, split));
        message.erase(0, split + 1);
    }
    if (!message.empty() && lines.size() < maxLines) {
        lines.push_back(std::move(message));
    }
    return lines;
}

CCSprite* createLogo(float maxSize) {
    auto path = toasty::pathToUtf8(Mod::get()->getResourcesDir() / "toastyreplay-logo.png");
    auto* logo = CCSprite::create(path.c_str());
    if (!logo) logo = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
    if (!logo) return nullptr;
    auto size = logo->getContentSize();
    auto largest = std::max(size.width, size.height);
    if (largest > 0.0f) logo->setScale(maxSize / largest);
    return logo;
}

void applyCapInsets(CCScale9Sprite* sprite) {
    if (!sprite) return;
    auto size = sprite->getContentSize();
    sprite->setCapInsets(CCRect(
        8.0f,
        8.0f,
        std::max(1.0f, size.width - 16.0f),
        std::max(1.0f, size.height - 16.0f)
    ));
}

class CreditsPage : public CCLayer {
protected:
    CCLayer* m_mainLayer = nullptr;
    CCSize m_size;
    CCLayer* m_contentLayer = nullptr;
    CCMenu* m_actionMenu = nullptr;
    bool m_closeQueued = false;

    bool init() {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        if (!CCLayer::init()) return false;
        m_size = winSize;
        m_mainLayer = this;
        setContentSize(m_size);
        setKeyboardEnabled(true);
        setKeypadEnabled(true);

        addPageBackground();
        addHeader();
        addMobileBackButton();

        auto* panel = CCScale9Sprite::create("GJ_square05.png");
        applyCapInsets(panel);
        panel->setContentSize({ kPanelWidth, m_size.height - 104.0f });
        panel->setPosition(ccp(m_size.width * 0.5f, (m_size.height - 54.0f) * 0.5f + 25.0f));
        panel->setColor(ccc3(54, 38, 31));
        panel->setOpacity(242);
        m_mainLayer->addChild(panel, 5);

        m_contentLayer = CCLayer::create();
        m_contentLayer->setContentSize(m_size);
        m_contentLayer->ignoreAnchorPointForPosition(false);
        m_contentLayer->setAnchorPoint(ccp(0.5f, 0.5f));
        m_contentLayer->setPosition(ccp(m_size.width * 0.5f, m_size.height * 0.5f));
        m_mainLayer->addChild(m_contentLayer, 10);

        m_actionMenu = CCMenu::create();
        m_actionMenu->setContentSize(m_size);
        m_actionMenu->ignoreAnchorPointForPosition(false);
        m_actionMenu->setAnchorPoint(ccp(0.5f, 0.5f));
        m_actionMenu->setPosition(ccp(m_size.width * 0.5f, m_size.height * 0.5f));
        m_mainLayer->addChild(m_actionMenu, 100);

        buildCredits();
        buildFooter();
        return true;
    }

    void addPageBackground() {
        toasty::cocos::addOrangeMovingBackground(m_mainLayer, m_size);
    }

    void addMobileBackButton() {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        auto* close = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        if (!close) return;
        close->setScale(0.72f);
        auto* item = CCMenuItemSpriteExtra::create(
            close,
            this,
            menu_selector(CreditsPage::onBack)
        );
        auto* menu = CCMenu::create();
        menu->setPosition(CCPointZero);
        item->setPosition(ccp(24.0f, m_size.height - 24.0f));
        menu->addChild(item);
        m_mainLayer->addChild(menu, 200);
#endif
    }

    void addHeader() {
        auto* title = CCLabelBMFont::create("Credits & Support", "goldFont.fnt");
        title->limitLabelWidth(m_size.width - 100.0f, 0.58f, 0.42f);
        title->setPosition(ccp(m_size.width * 0.5f, m_size.height - 25.0f));
        m_mainLayer->addChild(title, 3);

        auto* subtitle = CCLabelBMFont::create("THE PEOPLE BEHIND TOASTYREPLAY", "bigFont.fnt");
        subtitle->limitLabelWidth(m_size.width - 120.0f, 0.32f, 0.24f);
        subtitle->setColor(ccc3(255, 206, 154));
        subtitle->setPosition(ccp(m_size.width * 0.5f, m_size.height - 50.0f));
        m_mainLayer->addChild(subtitle, 3);
    }

    CCLabelBMFont* addCardText(CCNode* card, std::string const& text, float y,
                               char const* font, float scale, ccColor3B color, float maxWidth) {
        auto* label = CCLabelBMFont::create(text.c_str(), font);
        label->setColor(color);
        label->limitLabelWidth(maxWidth, scale, scale * 0.7f);
        label->setPosition(ccp(card->getContentWidth() * 0.5f, y));
        card->addChild(label, 2);
        return label;
    }

    CCNode* createCreditCard(std::string const& name, std::string const& title,
                             float width, float height, bool founder = false) {
        auto* card = CCNode::create();
        card->setContentSize({ width, height });
        card->setAnchorPoint(ccp(0.5f, 0.5f));

        auto* bg = CCScale9Sprite::create("GJ_square05.png");
        applyCapInsets(bg);
        bg->setContentSize({ width, height });
        bg->setPosition(ccp(width * 0.5f, height * 0.5f));
        bg->setColor(founder ? ccc3(95, 51, 27) : ccc3(44, 32, 29));
        bg->setOpacity(founder ? 225 : 205);
        card->addChild(bg);

        if (founder) {
            if (auto* logo = createLogo(26.0f)) {
                logo->setPosition(ccp(width * 0.5f, height - 20.0f));
                card->addChild(logo, 2);
            }
            addCardText(card, name, height - 47.0f, "goldFont.fnt", 0.46f, kColorGold, width - 36.0f);
            addCardText(card, title, 16.0f, "chatFont.fnt", 0.58f, kColorWhite, width - 36.0f);
        } else {
            addCardText(card, name, height - 15.0f, "goldFont.fnt", 0.36f, kColorGold, width - 32.0f);
            addCardText(card, title, 11.0f, "chatFont.fnt", 0.52f, kColorMuted, width - 32.0f);
        }
        return card;
    }

    CCNode* createCreditsMessage(float width, float height) {
        auto* card = CCNode::create();
        card->setContentSize({ width, height });
        card->setAnchorPoint(ccp(0.5f, 0.5f));

        auto* bg = CCScale9Sprite::create("GJ_square05.png");
        applyCapInsets(bg);
        bg->setContentSize({ width, height });
        bg->setPosition(ccp(width * 0.5f, height * 0.5f));
        bg->setColor(ccc3(38, 27, 25));
        bg->setOpacity(210);
        card->addChild(bg);

        auto lines = wrapText(
            "Thank you to my dev's, playtesters, and supporters of ToastyReplay, this project started out as slop and after long awaited time and patience from my team, we have made it a reality. - ToastexGD ToastyReplay Developer and Founder.",
            76,
            5
        );
        float spacing = 11.0f;
        float firstY = height * 0.5f + (static_cast<float>(lines.size()) - 1.0f) * spacing * 0.5f;
        for (size_t i = 0; i < lines.size(); ++i) {
            addCardText(card, lines[i], firstY - static_cast<float>(i) * spacing,
                        "chatFont.fnt", 0.50f, kColorWhite, width - 32.0f);
        }
        return card;
    }

    void buildCredits() {
        struct Credit {
            char const* name;
            char const* title;
        };
        static constexpr std::array<Credit, 9> credits {{
            { "human0443", "Developer" },
            { "misko.bin", "Developer" },
            { "__hopeandmiracle", "Developer" },
            { "jimmybutlerfan", "Playtester and Marketing" },
            { "Therealkeanan00", "Playtester and Marketing" },
            { "peony", "Lead Developer of Silicate Bot" },
            { "chagh.dev", "Lead Developer of TCBot" },
            { "GWDdoS", "Owner of AstralBot" },
            { "kepe__", "Owner of Ybot" },
        }};

        float viewportBottom = 48.0f;
        float viewportTop = m_size.height - 61.0f;
        float viewportHeight = viewportTop - viewportBottom;
        float viewportWidth = kPanelWidth - 28.0f;
        float viewportLeft = (m_size.width - viewportWidth) * 0.5f;
        auto* scroll = ScrollLayer::create({ viewportWidth, viewportHeight });
        scroll->setPosition(ccp(viewportLeft, viewportBottom));
        scroll->setCancelTouchLimit(6.0f);
        m_contentLayer->addChild(scroll, 2);

        constexpr float gap = 5.0f;
        constexpr float founderHeight = 86.0f;
        constexpr float rowHeight = 42.0f;
        constexpr float messageHeight = 78.0f;
        float cardWidth = viewportWidth - 14.0f;
        float contentHeight = gap + founderHeight + gap
            + static_cast<float>(credits.size()) * (rowHeight + gap)
            + messageHeight + gap;
        scroll->m_contentLayer->setContentSize({ viewportWidth, contentHeight });

        float cursor = contentHeight - gap;
        auto addCard = [&](CCNode* card, float height) {
            cursor -= height;
            card->setPosition(ccp(viewportWidth * 0.5f, cursor + height * 0.5f));
            scroll->m_contentLayer->addChild(card);
            cursor -= gap;
        };

        addCard(createCreditCard(
            "ToastexGD",
            "Main Developer, ToastyReplay Developer and Founder",
            cardWidth,
            founderHeight,
            true
        ), founderHeight);
        for (auto const& credit : credits) {
            addCard(createCreditCard(credit.name, credit.title, cardWidth, rowHeight), rowHeight);
        }
        addCard(createCreditsMessage(cardWidth, messageHeight), messageHeight);
        scroll->scrollToTop();

        auto* scrollbar = Scrollbar::create(scroll);
        scrollbar->setPosition(ccp(viewportLeft + viewportWidth + 7.0f, viewportBottom + viewportHeight * 0.5f));
        m_contentLayer->addChild(scrollbar, 3);
    }

    CCMenuItemSpriteExtra* makeButton(char const* text, char const* background, SEL_MenuHandler callback, int width) {
        auto* sprite = ButtonSprite::create(text, width, 0, 0.46f, true, "goldFont.fnt", background, 24.0f);
        return CCMenuItemSpriteExtra::create(sprite, this, callback);
    }

    void buildFooter() {
        auto* buy = makeButton("Buy ToastyReplay Pro", "GJ_button_01.png",
                               menu_selector(CreditsPage::onBuyPro), 164);
        auto* support = makeButton("Support Me", "GJ_button_04.png",
                                   menu_selector(CreditsPage::onSupport), 112);
        constexpr float gap = 12.0f;
        float total = buy->getScaledContentSize().width + support->getScaledContentSize().width + gap;
        float left = -total * 0.5f;
        m_actionMenu->addChildAtPosition(
            buy,
            Anchor::Bottom,
            ccp(left + buy->getScaledContentSize().width * 0.5f, 24.0f)
        );
        m_actionMenu->addChildAtPosition(
            support,
            Anchor::Bottom,
            ccp(left + buy->getScaledContentSize().width + gap + support->getScaledContentSize().width * 0.5f, 24.0f)
        );
    }

    void onBuyPro(CCObject*) {
        geode::utils::web::openLinkInBrowser("https://toastyreplay.xyz/");
    }

    void onSupport(CCObject*) {
        geode::utils::web::openLinkInBrowser("https://ko-fi.com/toastexgd");
    }

    void closePage() {
        if (s_openPage != this || m_closeQueued) return;
        m_closeQueued = true;
        setKeyboardEnabled(false);
        setKeypadEnabled(false);
        retain();
        Loader::get()->queueInMainThread([this]() {
            if (s_openPage == this) {
                s_openPage = nullptr;
                CCDirector::sharedDirector()->popSceneWithTransition(
                    0.35f,
                    PopTransition::kPopTransitionFade
                );
            }
            release();
        });
    }

    void onBack(CCObject*) {
        closePage();
    }

    void keyBackClicked() override {
        closePage();
    }

    void onExit() override {
        if (s_openPage == this) s_openPage = nullptr;
        CCLayer::onExit();
    }

public:
    static CreditsPage* create() {
        auto* page = new CreditsPage();
        if (page && page->init()) {
            page->autorelease();
            return page;
        }
        delete page;
        return nullptr;
    }
};
}

namespace toasty::credits {
    void showCreditsPage() {
        if (s_openPage || s_openPageQueued) return;
        s_openPageQueued = true;
        Loader::get()->queueInMainThread([]() {
            s_openPageQueued = false;
            if (s_openPage) return;
            auto* page = CreditsPage::create();
            if (!page) return;
            auto* scene = CCScene::create();
            scene->addChild(page);
            s_openPage = page;
            CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.35f, scene));
        });
    }
}
