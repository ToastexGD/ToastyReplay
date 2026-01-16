#include "gui.hpp"
#include <Geode/Bindings.hpp>

using namespace geode::prelude;

void renderWatermarkOverlay() {
    if (!GUI::get()->visible) {
        return;
    }
    
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 screenSize = io.DisplaySize;
    
    std::string watermarkText = "ToastyReplay v" MOD_VERSION "-beta";
    ImVec2 textSize = ImGui::CalcTextSize(watermarkText.c_str());
    ImVec2 centeredPos = ImVec2((screenSize.x - textSize.x) / 2, screenSize.y - 25);

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    
    drawList->AddText(centeredPos, IM_COL32(200, 200, 200, 150), watermarkText.c_str());
}
