#include "gui.hpp"
#include <Geode/Bindings.hpp>

using namespace geode::prelude;

void displayOverlayBranding() {
    if (!MenuInterface::get()->shown) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 screenDimensions = io.DisplaySize;

    std::string brandText = "ToastyReplay v" MOD_VERSION "-beta";
    ImVec2 textDimensions = ImGui::CalcTextSize(brandText.c_str());
    ImVec2 centeredCoord = ImVec2((screenDimensions.x - textDimensions.x) / 2, screenDimensions.y - 25);

    ImDrawList* drawList = ImGui::GetForegroundDrawList();

    drawList->AddText(centeredCoord, IM_COL32(200, 200, 200, 150), brandText.c_str());
}
