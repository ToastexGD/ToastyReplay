#include <Geode/modify/PlayLayer.hpp>
#include "ToastyReplay.hpp"
#include "gui.hpp"

using namespace geode::prelude;

class $modify(CBFIntegrationLayer, PlayLayer) {
    void resetLevel() {
        static Mod* cbfMod = geode::Loader::get()->getLoadedMod("syzzi.click_between_frames");

        if (cbfMod) {
            cbfMod->setSettingValue<bool>("soft-toggle", ReplayEngine::get()->engineMode != MODE_DISABLED);
        }

        PlayLayer::resetLevel();
    }
};
