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
    bool keyIsHeld = (keybind_frameAdvance != 0 && ImGui::IsKeyDown((ImGuiKey)keybind_frameAdvance));

    if (keyIsPressed) {
        if (!frameAdvanceKeyPressed) {
            frameAdvanceKeyPressed = true;

            if (!frameAdvance) {
                frameAdvance = true;
            } else {
                stepFrame = true;
            }
        }
    } else if (keyIsHeld && frameAdvance) {
        stepFrame = true;
    } else {
        frameAdvanceKeyPressed = false;
    }

    if (keybind_speedhackAudio != 0 && ImGui::IsKeyPressed((ImGuiKey)keybind_speedhackAudio, false)) {
        speedHackAudio = !speedHackAudio;
    }

    if (keybind_safeMode != 0 && ImGui::IsKeyPressed((ImGuiKey)keybind_safeMode, false)) {
        safeMode = !safeMode;
    }

    if (keybind_trajectory != 0 && ImGui::IsKeyPressed((ImGuiKey)keybind_trajectory, false)) {
        showTrajectory = !showTrajectory;
    }

    if (keybind_noclip != 0 && ImGui::IsKeyPressed((ImGuiKey)keybind_noclip, false)) {
        noclip = !noclip;
    }

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
        if (mgr->state == RECORD && mgr->currentReplay) {
            delete mgr->currentReplay;
            mgr->currentReplay = nullptr;
        }
        mgr->state = NONE;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Disable recording and playback");
    }
    ImGui::SameLine();

    if (ImGui::RadioButton("Record", mgr->state == RECORD)) {
        if (PlayLayer::get()) {
            mgr->startRecording(PlayLayer::get()->m_level);
        } else {
            mgr->state = RECORD;
        }
    }
    if (ImGui::IsItemHovered()) {
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
    
    GUI::get()->infoPanelSize = ImGui::GetWindowSize();
    
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
    
    GUI::get()->hackPanelSize = ImGui::GetWindowSize();
    
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
            
            float accuracy = 100.0f;
            if (mgr->noclipTotalFrames > 0) {
                accuracy = 100.0f * (1.0f - (float)mgr->noclipDeaths / (float)mgr->noclipTotalFrames);
            }
            
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
                
                const char* accuracyOptions[] = { "Disabled", "50%", "60%", "70%", "75%", "80%", "85%", "90%", "95%", "99%" };
                float accuracyValues[] = { 0.0f, 50.0f, 60.0f, 70.0f, 75.0f, 80.0f, 85.0f, 90.0f, 95.0f, 99.0f };
                
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
            ImGui::SetTooltip("Use a fixed seed for consistent RNG");
        }
        
        if (mgr->seedEnabled) {
            ImGui::Indent();
            
            static char seedBuffer[32] = "1";
            static bool seedBufferInit = false;
            if (!seedBufferInit) {
                snprintf(seedBuffer, sizeof(seedBuffer), "%u", mgr->seedValue);
                seedBufferInit = true;
            }
            
            ImGui::Text("Seed Value:");
            if (ImGui::InputText("##seedValue", seedBuffer, sizeof(seedBuffer), ImGuiInputTextFlags_CharsDecimal)) {
                try {
                    unsigned long long ull = std::stoull(seedBuffer);
                    mgr->seedValue = static_cast<unsigned int>(ull);
                } catch (...) {
                    mgr->seedValue = 1;
                }
            }
            
            ImGui::Unindent();
        }

        ImGui::NewLine();
        ImGui::EndChild();
    }
    if (GUI::get()->s_font) ImGui::PopFont();
    
    ImGui::End();
}

void RenderThemePanel() {
    GUI* gui = GUI::get();
    
    ImGui::SetNextWindowSize(gui->themePanelSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(835, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(220, 250), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("theme", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    
    gui->themePanelSize = ImGui::GetWindowSize();
    
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
        
        gui->mainPanelSize = ImVec2(350, 525);
        gui->infoPanelSize = ImVec2(200, 320);
        gui->hackPanelSize = ImVec2(200, 380);
        gui->themePanelSize = ImVec2(200, 320);
        
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

    float cappedScale = fontSize;
    if (cappedScale > 1.2f) cappedScale = 1.2f;
    ImGui::SetWindowFontScale(cappedScale);

    mainPanelSize = ImGui::GetWindowSize();

    if (l_font) ImGui::PushFont(l_font);
    ImGui::TextColored(ImVec4(1.f, 0.78f, 0.17f, 1.f), "ToastyReplay");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.f), "v1.0.0-Beta");
    if (l_font) ImGui::PopFont();

    if (s_font) ImGui::PushFont(s_font);

    ToastyReplay* mgr = ToastyReplay::get();

    if (ImGui::BeginChild("MainContent", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {
        
        if (mgr->state == PLAYBACK && mgr->currentReplay) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "PLAYING");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), ": %s", mgr->currentReplay->name.c_str());
            ImGui::SameLine();
            ImGui::Text("| Inputs: %zu", mgr->currentReplay->inputs.size());
            
            if (ImGui::Button("Stop Playback", ImVec2(150, 0))) {
                mgr->state = NONE;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Stop playing the macro");
            }
            
            ImGui::Separator();
            ImGui::NewLine();
        }
        
        if (mgr->state == RECORD && mgr->currentReplay) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "RECORDING");
            ImGui::SameLine();
            ImGui::Text("| Inputs: %zu", mgr->currentReplay->inputs.size());
            
            if (!tempReplayNameInitialized) {
                strncpy(tempReplayName, mgr->currentReplay->name.c_str(), sizeof(tempReplayName) - 1);
                tempReplayName[sizeof(tempReplayName) - 1] = '\0';
                tempReplayNameInitialized = true;
            }
            
            ImGui::Text("Macro Name:");
            if (ImGui::InputText("##recordingName", tempReplayName, sizeof(tempReplayName))) {
                mgr->currentReplay->name = tempReplayName;
            }
            
            if (ImGui::Button("Save Macro", ImVec2(150, 0))) {
                if (!mgr->currentReplay->inputs.empty()) {
                    mgr->currentReplay->save();
                    mgr->refreshReplays();
                    log::info("Saved macro: {}", mgr->currentReplay->name);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Save the current recording to file");
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Stop Recording", ImVec2(150, 0))) {
                delete mgr->currentReplay;
                mgr->currentReplay = nullptr;
                mgr->state = NONE;
                tempReplayNameInitialized = false;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Stop recording and discard unsaved inputs");
            }
            
            ImGui::Separator();
            ImGui::NewLine();
        } else {
            tempReplayNameInitialized = false;
        }

        renderStateSwitcher();

        ImGui::NewLine();

        ImGui::Text("Saved Replays");

        std::string currentReplayName = (mgr->currentReplay && mgr->state != RECORD) ? mgr->currentReplay->name : "Select a replay...";
        if (ImGui::BeginCombo("##ReplayCombo", currentReplayName.c_str())) {
            auto savedReplaysCopy = mgr->savedReplays;
            for (const auto& replayName : savedReplaysCopy) {
                bool isSelected = (currentReplayName == replayName);

                ImGui::PushID(replayName.c_str());
                if (ImGui::Selectable(replayName.c_str(), isSelected, 0, ImVec2(ImGui::GetContentRegionAvail().x - 30, 0))) {
                
                    if (mgr->state != RECORD) {
                        zReplay* rec = zReplay::fromFile(replayName);
                        if (rec) {
                            if (mgr->currentReplay) delete mgr->currentReplay;
                            mgr->currentReplay = rec;
                            mgr->state = PLAYBACK;
                        }
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

        if (mgr->currentReplay && mgr->state != RECORD) {
            ImGui::NewLine();
            ImGui::Text("Loaded: %s", mgr->currentReplay->name.c_str());
            ImGui::Text("Inputs: %zu", mgr->currentReplay->inputs.size());

            if (mgr->state == PLAYBACK) {
                ImGui::Checkbox("Ignore Manual Input", &mgr->ignoreManualInput);
            }
        }

        ImGui::EndChild();
    }

    if (s_font) ImGui::PopFont();

    ImGui::End();
}

void GUI::renderWatermarkOverlay() {
    ToastyReplay* mgr = ToastyReplay::get();
    
    bool shouldShowWatermark = visible || (PlayLayer::get() && mgr && mgr->state == PLAYBACK);
    
    if (!shouldShowWatermark) return;
    
    ImGuiIO& io = ImGui::GetIO();
    
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y - 30), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##watermark", nullptr, 
        ImGuiWindowFlags_NoDecoration | 
        ImGuiWindowFlags_NoInputs | 
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    
    if (s_font) ImGui::PushFont(s_font);
    
    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.17f, 0.8f), "ToastyReplay v1.0.0-Beta");
    
    if (s_font) ImGui::PopFont();
    
    ImGui::End();
}

void GUI::renderer() {
    ToastyReplay* mgr = ToastyReplay::get();
    if (mgr) {
        mgr->handleKeybinds();
    }

    renderWatermarkOverlay();

    if (!visible) {
        if (lastVisible && mgr) {
            mgr->refreshReplays();
        }
        lastVisible = false;
        return;
    }

    log::info("GUI::renderer() - visible is true, rendering panels");

    if (!lastVisible && mgr) {
        mgr->refreshReplays();
        lastVisible = true;
    }

    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4 currentTextColor = textColor;
    if (rgbTextColor) {
        currentTextColor = getRainbowColor(rgbSpeed);
    }
    style->Colors[ImGuiCol_Text] = currentTextColor;
    style->Colors[ImGuiCol_WindowBg] = ImVec4(backgroundColor.x, backgroundColor.y, backgroundColor.z, menuOpacity);
    style->Colors[ImGuiCol_PopupBg] = ImVec4(backgroundColor.x * 1.1f, backgroundColor.y * 1.1f, backgroundColor.z * 1.1f, menuOpacity);

    PlatformToolbox::showCursor();

    renderMainPanel();
    RenderInfoPanel();
    RenderHackPanel();
    RenderThemePanel();

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

    auto path = (Mod::get()->getResourcesDir() / "Roboto-Bold.ttf").string();
    
    if (std::filesystem::exists(path)) {
        s_font = io.Fonts->AddFontFromFileTTF(path.c_str(), 18.0f);
        l_font = io.Fonts->AddFontFromFileTTF(path.c_str(), 28.0f);
        vl_font = io.Fonts->AddFontFromFileTTF(path.c_str(), 100.0f);
    } else {
        s_font = io.Fonts->AddFontDefault();
        l_font = io.Fonts->AddFontDefault();
        vl_font = io.Fonts->AddFontDefault();
    }
    io.Fonts->Build();
}

$on_mod(Loaded) {
    log::info("ToastyReplay: Setting up ImGuiCocos from $on_mod(Loaded)");
    
    ImGuiCocos::get().setup([] {
        log::info("ToastyReplay: ImGuiCocos setup callback running");
        GUI::get()->setup();
    }).draw([] {
        GUI::get()->renderer();
    });
}
