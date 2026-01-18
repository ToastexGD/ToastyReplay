#include <Geode/modify/PlayLayer.hpp>
#include "ToastyReplay.hpp"
#include "gui.hpp"

using namespace geode::prelude;

class $modify(PlayLayer) {
    void resetLevel() {
        static Mod* cbf = geode::Loader::get()->getLoadedMod("syzzi.click_between_frames");

        if (cbf) {
            cbf->setSettingValue<bool>("soft-toggle", ToastyReplay::get()->state != NONE);
        }

        PlayLayer::resetLevel();
    }
};
