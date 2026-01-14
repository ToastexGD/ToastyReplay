#include "gui.hpp"
#include <Geode/Bindings.hpp>

using namespace geode::prelude;

// Watermark rendering function to be called from GUI renderer
void renderWatermarkOverlay() {
    // Only render watermark when GUI is visible (menu is open)
    if (!GUI::get()->visible) {
        return;
    }
    
    // Get ImGui IO for screen dimensions
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 screenSize = io.DisplaySize;
    
    // Calculate text and position
    std::string watermarkText = "ToastyReplay v" MOD_VERSION "-beta";
    ImVec2 textSize = ImGui::CalcTextSize(watermarkText.c_str());
    ImVec2 centeredPos = ImVec2((screenSize.x - textSize.x) / 2, screenSize.y - 25);
    
    // Get the foreground draw list (draws on top of everything)
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    
    // Draw text with semi-transparent white color
    drawList->AddText(centeredPos, IM_COL32(200, 200, 200, 150), watermarkText.c_str());
}
