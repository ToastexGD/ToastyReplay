#include "gui.hpp"
#include "ToastyReplay.hpp"
#include <Geode/Bindings.hpp>
#include <Geode/modify/LoadingLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>
#include <cmath>

using namespace geode::prelude;

void ToastyReplay::handleKeybinds() {
    bool keyIsPressed = (keybind_frameAdvance != 0 && ImGui::IsKeyPressed((ImGuiKey)keybind_frameAdvance, false));

    // Check for frame advance keybind
    if (keyIsPressed) {
        if (!frameAdvanceKeyPressed) { // Only process on key press (not holding)
            frameAdvanceKeyPressed = true;

            if (!frameAdvance) {
                // First time pressing: enable frame advance mode
                frameAdvance = true;
            } else {
                // Already in frame advance mode: step a frame
                stepFrame = true;
            }
        }
    } else {
        // Key is not pressed, reset the tracking
        frameAdvanceKeyPressed = false;
    }

    // Check for speedhack audio keybind
    if (keybind_speedhackAudio != 0 && ImGui::IsKeyPressed((ImGuiKey)keybind_speedhackAudio, false)) {
        speedHackAudio = !speedHackAudio;
    }

    // Check for safe mode keybind
    if (keybind_safeMode != 0 && ImGui::IsKeyPressed((ImGuiKey)keybind_safeMode, false)) {
        safeMode = !safeMode;
    }

    // Check for trajectory keybind
    if (keybind_trajectory != 0 && ImGui::IsKeyPressed((ImGuiKey)keybind_trajectory, false)) {
        showTrajectory = !showTrajectory;
    }

    // Check for noclip keybind
    if (keybind_noclip != 0 && ImGui::IsKeyPressed((ImGuiKey)keybind_noclip, false)) {
        noclip = !noclip;
    }

    // Check for seed keybind
    if (keybind_seed != 0 && ImGui::IsKeyPressed((ImGuiKey)keybind_seed, false)) {
        seedEnabled = !seedEnabled;
    }
}

static ImVec4 getRainbowColor(float speed) {
    static float hue = 0.0f;
    hue += speed * 0.001f;
    if (hue > 1.0f) hue -= 1.0f;
    
    float h = hue * 6.0f;
    float c = 1.0f;
    float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
    
    float r = 0, g = 0, b = 0;
    if (h < 1.0f) { r = c; g = x; }
    else if (h < 2.0f) { r = x; g = c; }
    else if (h < 3.0f) { g = c; b = x; }
    else if (h < 4.0f) { g = x; b = c; }
    else if (h < 5.0f) { r = x; b = c; }
    else { r = c; b = x; }
    
    return ImVec4(r, g, b, 1.0f);
}

void GUI::renderReplayInfo() {
    ToastyReplay* mgr = ToastyReplay::get();
    
    if (mgr->currentReplay) {
        ImGui::Text("Currently Recording: ");
        ImGui::SameLine();
        ImGui::TextColored({ 0,255,255,255 }, "%s", mgr->currentReplay->name.c_str());
    }
}

void GUI::renderStateSwitcher() {
    ToastyReplay* mgr = ToastyReplay::get();

    if (ImGui::RadioButton("Disable", mgr->state == NONE)) {
        mgr->state = NONE;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Disable recording and playback");
    }
    ImGui::SameLine();

    bool recordDisabled = mgr->lastUnsavedReplay != nullptr;
    if (recordDisabled) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }
    
    if (ImGui::RadioButton("Record", mgr->state == RECORD) && !recordDisabled) {
        if (PlayLayer::get()) {
            mgr->startRecording(PlayLayer::get()->m_level);
            mgr->currentReplay->purgeAfter(PlayLayer::get()->m_gameState.m_currentProgress);
        } else {
            mgr->state = RECORD;
        }
    }
    
    if (recordDisabled) {
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Save or Discard your last attempt before recording a new one!");
        }
    } else if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Record your inputs to create a replay");
    }

    ImGui::SameLine();
    if (ImGui::RadioButton("Playback", mgr->state == PLAYBACK)) {
        if (mgr->currentReplay) {
            mgr->state = PLAYBACK;
        } else {
            mgr->state = NONE;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Play back a saved replay");
    }
}

void RenderInfoPanel() {
    ToastyReplay* mgr = ToastyReplay::get();

    ImGuiCond sizeCondition = GUI::get()->themeResetRequested ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowSize(GUI::get()->infoPanelSize, sizeCondition);
    ImGui::SetNextWindowPos(ImVec2(385, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(220, 300), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("utilities", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    
    // Store the current window size
    GUI::get()->infoPanelSize = ImGui::GetWindowSize();
    
    // Cap font size at 1.3 to prevent text overflow in this compact window
    float cappedScale = GUI::get()->fontSize;
    if (cappedScale > 1.3f) cappedScale = 1.3f;
    ImGui::SetWindowFontScale(cappedScale);
    
    if (GUI::get()->s_font) ImGui::PushFont(GUI::get()->s_font);

    if (ImGui::BeginChild("InfoContent", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::Text("TPS: ");
        ImGui::SameLine();
        ImGui::TextColored({ 0,255,255,255 }, "%.0f", mgr->tps);

        ImGui::Text("Speed: ");
        ImGui::SameLine();
        ImGui::TextColored({ 0,255,255,255 }, "%.2f", mgr->speed);
        
        ImGui::Text("Frame: ");
        ImGui::SameLine();
        ImGui::TextColored({ 0,255,255,255 }, "%i", PlayLayer::get() ? PlayLayer::get()->m_gameState.m_currentProgress : 0);
        

        static float tempTPS = mgr->tps;
        ImGui::Text("Set TPS: ");
        ImGui::InputFloat("##tps", &tempTPS);
        if (ImGui::Button("Apply TPS")) {
            if (mgr->state == NONE || !PlayLayer::get()) mgr->tps = tempTPS;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Set ticks per second (physics rate)");
        }

        ImGui::NewLine();

        static float tempSpeed = 1;
        ImGui::Text("Set Speed: ");
        ImGui::InputFloat("##speed", &tempSpeed);
        if (ImGui::Button("Apply Speedhack")) {
            mgr->speed = tempSpeed;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Change game speed multiplier");
        }
        ImGui::EndChild();
    }
    if (GUI::get()->s_font) ImGui::PopFont();
    
    ImGui::End();
}

void RenderHackPanel() {
    ToastyReplay* mgr = ToastyReplay::get();
    
    ImGui::SetNextWindowSize(GUI::get()->hackPanelSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(610, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(220, 200), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("hacks", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    
    // Store the current window size
    GUI::get()->hackPanelSize = ImGui::GetWindowSize();
    
    // Cap font size at 1.3 to prevent text overflow in this compact window
    float cappedScale = GUI::get()->fontSize;
    if (cappedScale > 1.3f) cappedScale = 1.3f;
    ImGui::SetWindowFontScale(cappedScale);
    
    if (GUI::get()->s_font) ImGui::PushFont(GUI::get()->s_font);

    if (ImGui::BeginChild("HackContent", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::Checkbox("Frame Advance", &mgr->frameAdvance);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pause the game and advance frame by frame");
        }
        
        ImGui::Checkbox("Speedhack Audio", &mgr->speedHackAudio);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Apply speedhack to game audio as well");
        }
        
        ImGui::Checkbox("Safe Mode", &mgr->safeMode);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Prevents stats and percentage gain");
        }
        
        ImGui::Checkbox("Show Trajectory", &mgr->showTrajectory);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Display predicted player trajectory");
        }
        
        if (mgr->showTrajectory) {
            ImGui::Indent();
            static int trajectoryLength = 312;
            ImGui::Text("Trajectory Length");
            if (ImGui::InputInt("##trajLength", &trajectoryLength, 10, 50)) {
                if (trajectoryLength < 50) trajectoryLength = 50;
                if (trajectoryLength > 480) trajectoryLength = 480;
                mgr->trajectoryLength = trajectoryLength;
            }
            ImGui::Unindent();
        }
        
        ImGui::Checkbox("Noclip", &mgr->noclip);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Disable player collision with obstacles");
        }
        
        if (mgr->noclip) {
            ImGui::Indent();
            
            // Display current accuracy
            float accuracy = 100.0f;
            if (mgr->noclipTotalFrames > 0) {
                accuracy = 100.0f * (1.0f - (float)mgr->noclipDeaths / (float)mgr->noclipTotalFrames);
            }
            
            // Color based on accuracy
            ImVec4 accuracyColor;
            if (accuracy >= 90.0f) {
                accuracyColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
            } else if (accuracy >= 70.0f) {
                accuracyColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
            } else {
                accuracyColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
            }
            
            ImGui::Text("Accuracy: ");
            ImGui::SameLine();
            ImGui::TextColored(accuracyColor, "%.2f%%", accuracy);
            
            ImGui::Text("Deaths: %d | Frames: %d", mgr->noclipDeaths, mgr->noclipTotalFrames);
            
            ImGui::Checkbox("Accuracy Limit", &mgr->noclipAccuracyEnabled);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Kill player if accuracy drops below limit");
            }
            
            if (mgr->noclipAccuracyEnabled) {
                ImGui::Text("Min Accuracy:");
                
                // Dropdown for common accuracy limits
                const char* accuracyOptions[] = { "Disabled", "50%", "60%", "70%", "75%", "80%", "85%", "90%", "95%", "99%" };
                float accuracyValues[] = { 0.0f, 50.0f, 60.0f, 70.0f, 75.0f, 80.0f, 85.0f, 90.0f, 95.0f, 99.0f };
                
                // Find current selection
                int currentSelection = 0;
                for (int i = 0; i < 10; i++) {
                    if (std::abs(mgr->noclipAccuracyLimit - accuracyValues[i]) < 0.1f) {
                        currentSelection = i;
                        break;
                    }
                }
                
                if (ImGui::Combo("##accuracyLimit", &currentSelection, accuracyOptions, 10)) {
                    mgr->noclipAccuracyLimit = accuracyValues[currentSelection];
                }
            }
            
            ImGui::Unindent();
        }
        
        ImGui::Checkbox("Seed", &mgr->seedEnabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Use a fixed seed for consistent RNG (enables Safe Mode)");
        }
        
        if (mgr->seedEnabled) {
            ImGui::Indent();
            
            static int tempSeed = static_cast<int>(mgr->seedValue);
            ImGui::Text("Seed Value:");
            if (ImGui::InputInt("##seedValue", &tempSeed)) {
                if (tempSeed < 1) tempSeed = 1;
                mgr->seedValue = static_cast<unsigned int>(tempSeed);
            }
            
            ImGui::Unindent();
        }

        ImGui::NewLine();
        ImGui::EndChild();
    }
    if (GUI::get()->s_font) ImGui::PopFont();
    
    ImGui::End();
}

void RenderKeybindsPanel() {
    GUI* gui = GUI::get();
    ToastyReplay* mgr = ToastyReplay::get();
    
    ImGui::SetNextWindowSize(gui->keybindsPanelSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(1060, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(250, 300), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("keybinds", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    
    // Store the current window size
    gui->keybindsPanelSize = ImGui::GetWindowSize();
    
    // Cap font size at 1.3 to prevent text overflow
    float cappedScale = gui->fontSize;
    if (cappedScale > 1.3f) cappedScale = 1.3f;
    ImGui::SetWindowFontScale(cappedScale);
    
    if (gui->s_font) ImGui::PushFont(gui->s_font);

    if (ImGui::BeginChild("KeybindsContent", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.17f, 1.0f), "Keybinds");
        ImGui::Separator();
        ImGui::NewLine();
        
        // Helper lambda to render keybind button
        auto renderKeybindButton = [&](const char* label, int& keybind, const char* id) {
            ImGui::Text("%s", label);
            ImGui::SameLine(ImGui::GetWindowWidth() - 80);
            
            std::string buttonText = keybind == 0 ? "None" : std::string(1, (char)keybind);
            if (gui->isCapturingKeybind && gui->capturingKeybind == id) {
                buttonText = "...";
            }
            
            ImGui::PushID(id);
            if (ImGui::Button(buttonText.c_str(), ImVec2(60, 0))) {
                gui->isCapturingKeybind = true;
                gui->capturingKeybind = id;
            }
            ImGui::PopID();
            
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to set keybind");
            }
        };
        
        renderKeybindButton("Frame Advance", mgr->keybind_frameAdvance, "kb_frameadvance");
        renderKeybindButton("Speedhack Audio", mgr->keybind_speedhackAudio, "kb_speedhackaudio");
        renderKeybindButton("Safe Mode", mgr->keybind_safeMode, "kb_safemode");
        renderKeybindButton("Trajectory", mgr->keybind_trajectory, "kb_trajectory");
        renderKeybindButton("Noclip", mgr->keybind_noclip, "kb_noclip");
        renderKeybindButton("Seed", mgr->keybind_seed, "kb_seed");
        
        ImGui::NewLine();
        ImGui::Separator();
        ImGui::TextWrapped("Click a button and press any key to bind it. Press ESC to clear.");
        
        ImGui::EndChild();
    }
    
    if (gui->s_font) ImGui::PopFont();
    
    ImGui::End();
}

void RenderThemePanel() {
    GUI* gui = GUI::get();
    
    ImGui::SetNextWindowSize(gui->themePanelSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(835, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(220, 250), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("theme", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    
    // Store the current window size
    gui->themePanelSize = ImGui::GetWindowSize();
    
    // Cap font size at 1.3 to prevent text overflow in this compact window
    float cappedScale = gui->fontSize;
    if (cappedScale > 1.3f) cappedScale = 1.3f;
    ImGui::SetWindowFontScale(cappedScale);
    
    if (gui->s_font) ImGui::PushFont(gui->s_font);

    if (ImGui::BeginChild("ThemeContent", ImVec2(0, -40), false, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::Text("Text Color");
        if (!gui->rgbTextColor) {
            ImGui::ColorEdit4("##textColor", (float*)&gui->textColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        }
        ImGui::SameLine();
        ImGui::Checkbox("RGB##text", &gui->rgbTextColor);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable rainbow cycling text color");
        }
        
        ImGui::Text("Background Color");
        ImGui::ColorEdit4("##bgColor", (float*)&gui->backgroundColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        
        if (gui->rgbTextColor) {
            ImGui::NewLine();
            ImGui::Text("RGB Speed");
            ImGui::SliderFloat("##rgbSpeed", &gui->rgbSpeed, 0.1f, 5.0f, "%.1f");
        }
        
        ImGui::NewLine();
        ImGui::Text("Text Size");
        ImGui::SliderFloat("##fontSize", &gui->fontSize, 0.5f, 2.5f, "%.1f");
        
        ImGui::NewLine();
        ImGui::Text("Menu Opacity");
        ImGui::SliderFloat("##opacity", &gui->menuOpacity, 0.1f, 1.0f, "%.1f");
        
        ImGui::EndChild();
    }
    
    if (gui->s_font) ImGui::PopFont();

    if (ImGui::Button("Reset Theme", ImVec2(-1, 0))) {
        gui->textColor = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
        gui->backgroundColor = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
        gui->menuOpacity = 1.0f;
        gui->fontSize = 1.0f;
        gui->rgbTextColor = false;
        gui->rgbSpeed = 1.0f;
        gui->themeResetRequested = true;
        
        // Reset panel sizes too
        gui->mainPanelSize = ImVec2(350, 525);
        gui->infoPanelSize = ImVec2(200, 320);
        gui->hackPanelSize = ImVec2(200, 380);
        gui->themePanelSize = ImVec2(200, 320);
        
        // Immediately apply the reset
        ImGuiStyle* style = &ImGui::GetStyle();
        style->Colors[ImGuiCol_Text] = gui->textColor;
        style->Colors[ImGuiCol_WindowBg] = ImVec4(gui->backgroundColor.x, gui->backgroundColor.y, gui->backgroundColor.z, gui->menuOpacity);
        style->Colors[ImGuiCol_PopupBg] = ImVec4(gui->backgroundColor.x * 1.1f, gui->backgroundColor.y * 1.1f, gui->backgroundColor.z * 1.1f, gui->menuOpacity);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reset all theme settings to default");
    }
    
    ImGui::End();
}

void GUI::renderMainPanel() {
    ImGuiCond sizeCondition = themeResetRequested ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowSize(mainPanelSize, sizeCondition);
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(350, 500), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("info", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // Cap font size at 1.2 for main panel to prevent text from going off screen
    float cappedScale = fontSize;
    if (cappedScale > 1.2f) cappedScale = 1.2f;
    ImGui::SetWindowFontScale(cappedScale);

    // Store the current window size
    mainPanelSize = ImGui::GetWindowSize();

    if (l_font) ImGui::PushFont(l_font);
    ImGui::TextColored(ImVec4(1.f, 0.78f, 0.17f, 1.f), "ToastyReplay");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.f), "v" MOD_VERSION);
    if (l_font) ImGui::PopFont();

    if (s_font) ImGui::PushFont(s_font);

    ToastyReplay* mgr = ToastyReplay::get();

    if (ImGui::BeginChild("MainContent", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {
        if (mgr->lastUnsavedReplay) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Last Unsaved Attempt:");
            zReplay* replay = mgr->lastUnsavedReplay;
            ImGui::PushID(replay);

            ImGui::Text("%s", replay->name.c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 75);
            if (ImGui::Button("Save")) {
                replay->save();
                mgr->refreshReplays();
                delete replay;
                mgr->lastUnsavedReplay = nullptr;
                ImGui::PopID();
                goto end_unsaved;
            }

            ImGui::InputText("##unsavedname", tempReplayName, sizeof(tempReplayName));
            ImGui::SameLine();
            if (ImGui::Button("Apply")) {
                replay->name = tempReplayName;
                memset(tempReplayName, 0, sizeof(tempReplayName));
            }

            if (ImGui::Button("Discard", ImVec2(-1, 0))) {
                delete replay;
                mgr->lastUnsavedReplay = nullptr;
                ImGui::PopID();
                goto end_unsaved;
            }

            ImGui::PopID();
            end_unsaved:
            ImGui::Separator();
            ImGui::NewLine();
        }

        renderReplayInfo();
        renderStateSwitcher();

        ImGui::NewLine();

        ImGui::Text("Saved Replays");

        std::string currentReplayName = mgr->currentReplay ? mgr->currentReplay->name : "Select a replay...";
        if (ImGui::BeginCombo("##ReplayCombo", currentReplayName.c_str())) {
            auto savedReplaysCopy = mgr->savedReplays; // Copy to avoid iterator invalidation on refresh
            for (const auto& replayName : savedReplaysCopy) {
                bool isSelected = (currentReplayName == replayName);

                ImGui::PushID(replayName.c_str());
                if (ImGui::Selectable(replayName.c_str(), isSelected, 0, ImVec2(ImGui::GetContentRegionAvail().x - 30, 0))) {
                    zReplay* rec = zReplay::fromFile(replayName);
                    if (rec) {
                        if (mgr->currentReplay) delete mgr->currentReplay;
                        mgr->currentReplay = rec;
                        mgr->state = PLAYBACK;
                    }
                }

                ImGui::SameLine(ImGui::GetWindowWidth() - 35);
                if (ImGui::Button("X", ImVec2(20, 0))) {
                    auto path = Mod::get()->getSaveDir() / "replays" / (replayName + ".gdr");
                    if (std::filesystem::exists(path)) {
                        std::filesystem::remove(path);
                        mgr->refreshReplays();
                    }
                }
                ImGui::PopID();

                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Refresh List")) {
            mgr->refreshReplays();
        }

        ImGui::SameLine();
        if (ImGui::Button("Open Replays Folder")) {
            auto dir = Mod::get()->getSaveDir() / "replays";
            if (std::filesystem::exists(dir) || std::filesystem::create_directory(dir)) {
                utils::file::openFolder(dir);
            }
        }

        if (mgr->currentReplay) {
            ImGui::NewLine();
            ImGui::Text("Override Recording Name");
            ImGui::InputText("##replayname", tempReplayName, sizeof(tempReplayName));
            if (ImGui::Button("Apply")) {
                mgr->currentReplay->name = tempReplayName;
                memset(tempReplayName, 0, sizeof(tempReplayName));
            }

            if (mgr->state == PLAYBACK) {
                ImGui::Checkbox("Ignore Manual Input", &mgr->ignoreManualInput);
            }
        }

        ImGui::EndChild();
    }

    if (s_font) ImGui::PopFont();

    ImGui::End();
}


void GUI::renderer() {
    if (!visible) {
        if (lastVisible) {
             ToastyReplay::get()->refreshReplays();
        }
        lastVisible = false;
        return;
    }

    ToastyReplay* mgr = ToastyReplay::get();

    if (!lastVisible) {
        mgr->refreshReplays();
        lastVisible = true;
    }


    // Handle custom keybinds
    mgr->handleKeybinds();

    // Update colors dynamically every frame
    ImGuiStyle* style = &ImGui::GetStyle();

    ImVec4 currentTextColor = textColor;

    if (rgbTextColor) {
        ImVec4 rgb = getRainbowColor(rgbSpeed);
        currentTextColor = ImVec4(rgb.x, rgb.y, rgb.z, textColor.w);
    }

    style->Colors[ImGuiCol_Text] = currentTextColor;
    style->Colors[ImGuiCol_WindowBg] = ImVec4(backgroundColor.x, backgroundColor.y, backgroundColor.z, menuOpacity);
    style->Colors[ImGuiCol_PopupBg] = ImVec4(backgroundColor.x * 1.1f, backgroundColor.y * 1.1f, backgroundColor.z * 1.1f, menuOpacity);

    PlatformToolbox::showCursor();

    renderMainPanel();
    RenderInfoPanel();
    RenderHackPanel();
    RenderThemePanel();

    // Render watermark overlay
    renderWatermarkOverlay();

    if (keyCheckFailed) {
        keyCheckFailed = false;
        ImGui::OpenPopup("Key Check Failed");
    }

    ImGui::SetNextWindowSize(ImVec2(500, 140), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - 250, ImGui::GetIO().DisplaySize.y / 2 - 70), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Key Check Failed", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Your key is invalid or has been linked to a different computer.");
        ImGui::Text("Please enter a valid key or contact support.");
        if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (showCBFMessage && !shownCBFMessage) {
        shownCBFMessage = true;
        ImGui::OpenPopup("CBF Detected!");
    }

    ImGui::SetNextWindowSize(ImVec2(500, 140), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - 250, ImGui::GetIO().DisplaySize.y / 2 - 70), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("CBF Detected!", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Click between frames has been detected!");
        ImGui::Text("Even when disabled in options, playback may be affected.");
        if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    themeResetRequested = false;
}

void GUI::setup() {
    ImGuiStyle* style = &ImGui::GetStyle();

    style->WindowPadding = ImVec2(15, 15);
    style->WindowRounding = 5.0f;
    style->FramePadding = ImVec2(5, 5);
    style->FrameRounding = 4.0f;
    style->ItemSpacing = ImVec2(12, 8);
    style->ItemInnerSpacing = ImVec2(8, 6);
    style->IndentSpacing = 25.0f;
    style->ScrollbarSize = 15.0f;
    style->ScrollbarRounding = 9.0f;
    style->GrabMinSize = 5.0f;
    style->GrabRounding = 3.0f;

    style->Colors[ImGuiCol_Text] = textColor;
    style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_WindowBg] = ImVec4(backgroundColor.x, backgroundColor.y, backgroundColor.z, menuOpacity);
    style->Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.09f, menuOpacity);
    style->Colors[ImGuiCol_Border] = ImVec4(0,0,0,0);
    style->Colors[ImGuiCol_BorderShadow] = ImVec4(0,0,0,0);
    style->Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 0.98f, 0.95f, 0.75f);
    style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    style->Colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    style->Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    style->Colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
    style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
    style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
    style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.98f, 0.95f, 0.73f);


    ImGuiIO& io = ImGui::GetIO();

    auto resourceDir = Mod::get()->getResourcesDir();
    auto path = (resourceDir / "font.ttf").string();
    
    // Only load custom font if the file exists
    if (std::filesystem::exists(path)) {
        s_font = io.Fonts->AddFontFromFileTTF(path.c_str(), 18.0f);
        l_font = io.Fonts->AddFontFromFileTTF(path.c_str(), 28.0f);
        vl_font = io.Fonts->AddFontFromFileTTF(path.c_str(), 100.0f);
        io.Fonts->Build();
    } else {
        s_font = nullptr;
        l_font = nullptr;
        vl_font = nullptr;
    }
}

class $modify(LoadingLayer) {
    bool init(bool fromReload) {
        ImGuiCocos::get().setup([] {
            GUI::get()->setup();
        }).draw([] {
            GUI::get()->renderer();
        });

        return LoadingLayer::init(fromReload);
    }
};
