#include "ToastyReplay.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GJGroundLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/ShaderLayer.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace {
    enum class LayoutDisposition {
        Visible,
        HiddenDecoration,
        SuppressedTrigger
    };

    struct IdRun {
        int first;
        int last;
    };

    bool isLayoutEnabled() {
        return ReplayEngine::get()->layoutMode;
    }

    class LayoutClassifier {
    public:
        LayoutDisposition classify(GameObject* object) const {
            auto disposition = classifyByBehavior(object);
            if (disposition != LayoutDisposition::Visible) {
                return disposition;
            }
            return classifyByFallbackCatalog(object);
        }

    private:
        static constexpr int kVisibleExceptions[] = { 38, 44, 747, 749 };

        static constexpr IdRun kDecorationRuns[] = {
            {5, 5}, {15, 21}, {41, 41}, {48, 54}, {60, 60}, {73, 73}, {80, 80}, {85, 87}, {97, 97},
            {106, 107}, {110, 110}, {113, 115}, {120, 120}, {123, 134}, {136, 139}, {148, 159},
            {180, 182}, {190, 190}, {211, 211}, {222, 242}, {245, 246}, {259, 259}, {266, 266},
            {273, 273}, {277, 285}, {296, 297}, {324, 325}, {358, 358}, {373, 378}, {394, 396},
            {405, 414}, {419, 420}, {448, 457}, {460, 460}, {472, 474}, {476, 482}, {485, 491},
            {494, 650}, {653, 659}, {668, 672}, {681, 708}, {713, 716}, {719, 719}, {721, 724},
            {730, 736}, {738, 739}, {752, 759}, {762, 767}, {769, 775}, {807, 833}, {841, 848},
            {850, 850}, {853, 857}, {859, 859}, {861, 863}, {867, 874}, {877, 878}, {880, 885},
            {888, 891}, {893, 896}, {902, 911}, {916, 917}, {920, 921}, {923, 961}, {964, 977},
            {980, 988}, {990, 990}, {992, 992}, {997, 1005}, {1009, 1021}, {1024, 1048}, {1050, 1071},
            {1075, 1118}, {1120, 1120}, {1122, 1127}, {1132, 1153}, {1158, 1201}, {1205, 1207},
            {1223, 1225}, {1228, 1259}, {1261, 1261}, {1263, 1263}, {1265, 1267}, {1269, 1274},
            {1276, 1320}, {1322, 1322}, {1325, 1326}, {1348, 1395}, {1431, 1464}, {1471, 1473},
            {1496, 1496}, {1507, 1507}, {1510, 1519}, {1521, 1540}, {1552, 1560}, {1586, 1586},
            {1588, 1588}, {1590, 1593}, {1596, 1597}, {1599, 1610}, {1617, 1618}, {1621, 1700},
            {1737, 1742}, {1752, 1754}, {1756, 1810}, {1820, 1821}, {1823, 1828}, {1830, 1858},
            {1860, 1902}, {1908, 1909}, {1919, 1928}, {1936, 1939}, {2020, 2055}, {2064, 2065},
            {2070, 2700}, {2703, 2704}, {2708, 2770}, {2773, 2773}, {2776, 2807}, {2838, 2865},
            {2867, 2897}, {2927, 2998}, {3000, 3002}, {3032, 3032}, {3038, 3097}, {3101, 3599},
            {3621, 3639}, {3646, 3654}, {3656, 3659}, {3700, 3799}, {3801, 4385}
        };

        static constexpr IdRun kTriggerRuns[] = {
            {29, 30}, {32, 33}, {104, 105}, {221, 221}, {717, 718}, {743, 744}, {899, 900}, {915, 915},
            {1006, 1007}, {1520, 1520}, {1612, 1613}, {1818, 1819}, {2903, 2903}, {2999, 2999},
            {3009, 3010}, {3014, 3015}, {3020, 3021}, {3029, 3031}, {3606, 3606}, {3608, 3608},
            {3612, 3612}
        };

        bool isException(int objectId) const {
            for (int id : kVisibleExceptions) {
                if (id == objectId) return true;
            }
            return false;
        }

        bool matchesDecorationRun(int objectId) const {
            for (auto const& run : kDecorationRuns) {
                if (objectId >= run.first && objectId <= run.last) return true;
            }
            return false;
        }

        bool matchesTriggerRun(int objectId) const {
            for (auto const& run : kTriggerRuns) {
                if (objectId >= run.first && objectId <= run.last) return true;
            }
            return false;
        }

        bool isExplicitGameplayObject(GameObject* object) const {
            switch (object->m_objectType) {
                case GameObjectType::Solid:
                case GameObjectType::Slope:
                case GameObjectType::Hazard:
                case GameObjectType::AnimatedHazard:
                case GameObjectType::SecretCoin:
                case GameObjectType::UserCoin:
                case GameObjectType::Collectible:
                case GameObjectType::CollisionObject:
                    return true;
                case GameObjectType::Modifier:
                    switch (object->m_objectID) {
                        case 200:
                        case 201:
                        case 202:
                        case 203:
                        case 1334:
                            return true;
                        default:
                            return false;
                    }
                default:
                    return false;
            }
        }

        LayoutDisposition classifyByBehavior(GameObject* object) const {
            if (isException(object->m_objectID)) {
                return LayoutDisposition::Visible;
            }

            if (isExplicitGameplayObject(object)) {
                return LayoutDisposition::Visible;
            }

            if (object->m_objectType == GameObjectType::Decoration) {
                return LayoutDisposition::HiddenDecoration;
            }

            if (object->m_isNoTouch) {
                return LayoutDisposition::HiddenDecoration;
            }

            return LayoutDisposition::Visible;
        }

        LayoutDisposition classifyByFallbackCatalog(GameObject* object) const {
            if (matchesTriggerRun(object->m_objectID)) {
                return LayoutDisposition::SuppressedTrigger;
            }

            if (matchesDecorationRun(object->m_objectID)) {
                return LayoutDisposition::HiddenDecoration;
            }

            return LayoutDisposition::Visible;
        }
    };

    static const LayoutClassifier s_classifier;

    void normalizeVisibleObject(GameObject* object) {
        auto* engine = ReplayEngine::get();
        object->m_hasNoGlow = true;
        object->m_hasNoAudioScale = false;
        object->m_isDontEnter = true;
        object->m_isDontFade = true;
        object->m_ignoreFade = true;
        object->m_ignoreEnter = true;
        object->m_hasParticles = false;
        object->m_hasNoParticles = true;
        object->m_hasNoEffects = engine && engine->noEffect &&
            object->m_objectType != GameObjectType::InverseMirrorPortal &&
            object->m_objectType != GameObjectType::NormalMirrorPortal;
        object->m_activeMainColorID = -1;
        object->m_activeDetailColorID = -1;
        object->m_baseUsesHSV = false;
        object->m_detailUsesHSV = false;
        object->setOpacity(255);
    }

    static cocos2d::ccColor3B sanitizeColor(int r, int g, int b) {
        return {
            static_cast<GLubyte>(std::clamp(r, 0, 255)),
            static_cast<GLubyte>(std::clamp(g, 0, 255)),
            static_cast<GLubyte>(std::clamp(b, 0, 255))
        };
    }

    static cocos2d::ccColor3B layoutBackgroundColor() {
        auto* engine = ReplayEngine::get();
        if (!engine) {
            return { 160, 160, 160 };
        }
        return sanitizeColor(
            engine->layoutModeBackgroundR,
            engine->layoutModeBackgroundG,
            engine->layoutModeBackgroundB
        );
    }

    static cocos2d::ccColor3B layoutGroundColor() {
        auto* engine = ReplayEngine::get();
        if (!engine) {
            return { 160, 160, 160 };
        }
        return sanitizeColor(
            engine->layoutModeGroundR,
            engine->layoutModeGroundG,
            engine->layoutModeGroundB
        );
    }

    static void applyLayoutColor(int colorID, cocos2d::ccColor3B& color) {
        if (colorID == 1000) {
            color = layoutBackgroundColor();
        } else if (colorID == 1001) {
            color = layoutGroundColor();
        } else {
            color = { 255, 255, 255 };
        }
    }
}

class $modify(LayoutGameObject, GameObject) {
    void addGlow(gd::string path) {
        GameObject::addGlow(path);

        if (!PlayLayer::get() || !isLayoutEnabled()) {
            return;
        }

        m_isHide = s_classifier.classify(this) == LayoutDisposition::HiddenDecoration;
    }
};

class $modify(LayoutBaseLayer, GJBaseGameLayer) {
    void updateColor(
        cocos2d::ccColor3B& color,
        float fadeTime,
        int colorID,
        bool blending,
        float opacity,
        cocos2d::ccHSVValue& copyHSV,
        int colorIDToCopy,
        bool copyOpacity,
        EffectGameObject* callerObject,
        int unk1,
        int unk2
    ) {
        if (PlayLayer::get() && isLayoutEnabled()) {
            applyLayoutColor(colorID, color);
        }

        GJBaseGameLayer::updateColor(
            color,
            fadeTime,
            colorID,
            blending,
            opacity,
            copyHSV,
            colorIDToCopy,
            copyOpacity,
            callerObject,
            unk1,
            unk2
        );
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (isLayoutEnabled()) {
            toggleGlitter(false);
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }

    void createBackground(int background) {
        if (isLayoutEnabled()) {
            background = 13;
        }
        GJBaseGameLayer::createBackground(background);
    }

    void createMiddleground(int middleground) {
        if (isLayoutEnabled()) {
            middleground = 0;
        }
        GJBaseGameLayer::createMiddleground(middleground);
    }
};

class $modify(LayoutGroundLayer, GJGroundLayer) {
    static GJGroundLayer* create(int groundID, int lineType) {
        if (isLayoutEnabled()) {
            groundID = 1;
            lineType = 1;
        }
        return GJGroundLayer::create(groundID, lineType);
    }
};

class $modify(LayoutShaderLayer, ShaderLayer) {
    void performCalculations() {
        if (isLayoutEnabled()) {
            return;
        }
        ShaderLayer::performCalculations();
    }
};

class $modify(LayoutPlayLayer, PlayLayer) {
    void addObject(GameObject* object) {
        if (!isLayoutEnabled()) {
            return PlayLayer::addObject(object);
        }

        auto disposition = s_classifier.classify(object);
        if (disposition == LayoutDisposition::SuppressedTrigger) {
            object->m_isHide = true;
            return;
        }

        normalizeVisibleObject(object);
        object->m_isHide = disposition == LayoutDisposition::HiddenDecoration;
        PlayLayer::addObject(object);
    }
};
