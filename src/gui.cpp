#include "gui.hpp"
#include "ToastyReplay.hpp"
#include <Geode/Bindings.hpp>
#include <Geode/modify/LoadingLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>
#include <cmath>

using namespace geode::prelude;

void ReplayEngine::processHotkeys() {
    bool keyPressed = (hotkey_tickStep != 0 && ImGui::IsKeyPressed((ImGuiKey)hotkey_tickStep, false));
    bool keyHeld = (hotkey_tickStep != 0 && ImGui::IsKeyDown((ImGuiKey)hotkey_tickStep));

    if (keyPressed) {
        if (!stepKeyActive) {
            stepKeyActive = true;

            if (!tickStepping) {
                tickStepping = true;
            } else {
                singleTickStep = true;
            }
        }
    } else if (keyHeld && tickStepping) {
        singleTickStep = true;
    } else {
        stepKeyActive = false;
    }

    if (hotkey_audioPitch != 0 && ImGui::IsKeyPressed((ImGuiKey)hotkey_audioPitch, false)) {
        audioPitchEnabled = !audioPitchEnabled;
    }

    if (hotkey_protected != 0 && ImGui::IsKeyPressed((ImGuiKey)hotkey_protected, false)) {
        protectedMode = !protectedMode;
    }

    if (hotkey_pathPreview != 0 && ImGui::IsKeyPressed((ImGuiKey)hotkey_pathPreview, false)) {
        pathPreview = !pathPreview;
    }

    if (hotkey_collision != 0 && ImGui::IsKeyPressed((ImGuiKey)hotkey_collision, false)) {
        collisionBypass = !collisionBypass;
    }

    if (hotkey_rngLock != 0 && ImGui::IsKeyPressed((ImGuiKey)hotkey_rngLock, false)) {
        rngLocked = !rngLocked;
    }
}

static ImVec4 computeCycleColor(float rate) {
    static float hueVal = 0.0f;
    hueVal += rate * 0.001f;
    if (hueVal > 1.0f) hueVal -= 1.0f;

    float h = hueVal * 6.0f;
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

void MenuInterface::displayMacroDetails() {
    ReplayEngine* engine = ReplayEngine::get();

    if (engine->activeMacro) {
        ImGui::Text("Currently Recording: ");
        ImGui::SameLine();
        ImGui::TextColored({ 0,255,255,255 }, "%s", engine->activeMacro->name.c_str());
    }
}

void MenuInterface::displayModeSelector() {
    ReplayEngine* engine = ReplayEngine::get();

    if (ImGui::RadioButton("Disable", engine->engineMode == MODE_DISABLED)) {
        if (engine->engineMode == MODE_CAPTURE && engine->activeMacro) {
            delete engine->activeMacro;
            engine->activeMacro = nullptr;
        }
        engine->engineMode = MODE_DISABLED;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Disable recording and playback");
    }
    ImGui::SameLine();

    if (ImGui::RadioButton("Record", engine->engineMode == MODE_CAPTURE)) {
        if (PlayLayer::get()) {
            engine->beginCapture(PlayLayer::get()->m_level);
        } else {
            engine->engineMode = MODE_CAPTURE;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Record your inputs to create a replay");
    }

    ImGui::SameLine();
    if (ImGui::RadioButton("Playback", engine->engineMode == MODE_EXECUTE)) {
        if (engine->activeMacro) {
            engine->engineMode = MODE_EXECUTE;
        } else {
            engine->engineMode = MODE_DISABLED;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Play back a saved replay");
    }
}

void DrawUtilityPanel() {
    ReplayEngine* engine = ReplayEngine::get();

    ImGuiCond sizeRule = MenuInterface::get()->layoutReset ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowSize(MenuInterface::get()->utilityPanelDims, sizeRule);
    ImGui::SetNextWindowPos(ImVec2(385, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(220, 300), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("utilities", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    MenuInterface::get()->utilityPanelDims = ImGui::GetWindowSize();

    float cappedScale = MenuInterface::get()->textScale;
    if (cappedScale > 1.3f) cappedScale = 1.3f;
    ImGui::SetWindowFontScale(cappedScale);

    if (MenuInterface::get()->smallTypeface) ImGui::PushFont(MenuInterface::get()->smallTypeface);

    if (ImGui::BeginChild("InfoContent", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::Text("TPS: ");
        ImGui::SameLine();
        ImGui::TextColored({ 0,255,255,255 }, "%.0f", engine->tickRate);

        ImGui::Text("Speed: ");
        ImGui::SameLine();
        ImGui::TextColored({ 0,255,255,255 }, "%.2f", engine->gameSpeed);

        ImGui::Text("Frame: ");
        ImGui::SameLine();
        ImGui::TextColored({ 0,255,255,255 }, "%i", PlayLayer::get() ? PlayLayer::get()->m_gameState.m_currentProgress : 0);


        static float tempTickRate = engine->tickRate;
        ImGui::Text("Set TPS: ");
        ImGui::InputFloat("##tps", &tempTickRate);
        if (ImGui::Button("Apply TPS")) {
            if (engine->engineMode == MODE_DISABLED || !PlayLayer::get()) engine->tickRate = tempTickRate;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Set ticks per second (physics rate)");
        }

        ImGui::NewLine();

        static float tempGameSpeed = 1;
        ImGui::Text("Set Speed: ");
        ImGui::InputFloat("##speed", &tempGameSpeed);
        if (ImGui::Button("Apply Speedhack")) {
            engine->gameSpeed = tempGameSpeed;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Change game speed multiplier");
        }
        ImGui::EndChild();
    }
    if (MenuInterface::get()->smallTypeface) ImGui::PopFont();

    ImGui::End();
}

void DrawToolsPanel() {
    ReplayEngine* engine = ReplayEngine::get();

    ImGui::SetNextWindowSize(MenuInterface::get()->toolsPanelDims, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(610, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(220, 200), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("hacks", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    MenuInterface::get()->toolsPanelDims = ImGui::GetWindowSize();

    float cappedScale = MenuInterface::get()->textScale;
    if (cappedScale > 1.3f) cappedScale = 1.3f;
    ImGui::SetWindowFontScale(cappedScale);

    if (MenuInterface::get()->smallTypeface) ImGui::PushFont(MenuInterface::get()->smallTypeface);

    if (ImGui::BeginChild("HackContent", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::Checkbox("Frame Advance", &engine->tickStepping);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pause the game and advance frame by frame");
        }

        ImGui::Checkbox("Speedhack Audio", &engine->audioPitchEnabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Apply speedhack to game audio as well");
        }

        ImGui::Checkbox("Safe Mode", &engine->protectedMode);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Prevents stats and percentage gain");
        }

        ImGui::Checkbox("Show Trajectory", &engine->pathPreview);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Display predicted player trajectory");
        }

        if (engine->pathPreview) {
            ImGui::Indent();
            static int pathLen = 312;
            ImGui::Text("Trajectory Length");
            if (ImGui::InputInt("##trajLength", &pathLen, 10, 50)) {
                if (pathLen < 50) pathLen = 50;
                if (pathLen > 480) pathLen = 480;
                engine->pathLength = pathLen;
            }
            ImGui::Unindent();
        }

        ImGui::Checkbox("Noclip", &engine->collisionBypass);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Disable player collision with obstacles");
        }

        if (engine->collisionBypass) {
            ImGui::Indent();

            float hitRate = 100.0f;
            if (engine->totalTickCount > 0) {
                hitRate = 100.0f * (1.0f - (float)engine->bypassedCollisions / (float)engine->totalTickCount);
            }

            ImVec4 hitRateColor;
            if (hitRate >= 90.0f) {
                hitRateColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            } else if (hitRate >= 70.0f) {
                hitRateColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
            } else {
                hitRateColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            }

            ImGui::Text("Accuracy: ");
            ImGui::SameLine();
            ImGui::TextColored(hitRateColor, "%.2f%%", hitRate);

            ImGui::Text("Deaths: %d | Frames: %d", engine->bypassedCollisions, engine->totalTickCount);

            ImGui::Checkbox("Accuracy Limit", &engine->collisionLimitActive);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Kill player if accuracy drops below limit");
            }

            if (engine->collisionLimitActive) {
                ImGui::Text("Min Accuracy:");

                const char* thresholdOptions[] = { "Disabled", "50%", "60%", "70%", "75%", "80%", "85%", "90%", "95%", "99%" };
                float thresholdValues[] = { 0.0f, 50.0f, 60.0f, 70.0f, 75.0f, 80.0f, 85.0f, 90.0f, 95.0f, 99.0f };

                int currentIdx = 0;
                for (int i = 0; i < 10; i++) {
                    if (std::abs(engine->collisionThreshold - thresholdValues[i]) < 0.1f) {
                        currentIdx = i;
                        break;
                    }
                }

                if (ImGui::Combo("##accuracyLimit", &currentIdx, thresholdOptions, 10)) {
                    engine->collisionThreshold = thresholdValues[currentIdx];
                }
            }

            ImGui::Unindent();
        }

        ImGui::Checkbox("Seed", &engine->rngLocked);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Use a fixed seed for consistent RNG");
        }

        if (engine->rngLocked) {
            ImGui::Indent();

            static char rngBuffer[32] = "1";
            static bool rngBufferInit = false;
            if (!rngBufferInit) {
                snprintf(rngBuffer, sizeof(rngBuffer), "%u", engine->rngSeedVal);
                rngBufferInit = true;
            }

            ImGui::Text("Seed Value:");
            if (ImGui::InputText("##seedValue", rngBuffer, sizeof(rngBuffer), ImGuiInputTextFlags_CharsDecimal)) {
                try {
                    unsigned long long parsed = std::stoull(rngBuffer);
                    engine->rngSeedVal = static_cast<unsigned int>(parsed);
                } catch (...) {
                    engine->rngSeedVal = 1;
                }
            }

            ImGui::Unindent();
        }

        ImGui::NewLine();
        ImGui::EndChild();
    }
    if (MenuInterface::get()->smallTypeface) ImGui::PopFont();

    ImGui::End();
}

void DrawStylePanel() {
    MenuInterface* ui = MenuInterface::get();

    ImGui::SetNextWindowSize(ui->stylePanelDims, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(835, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(220, 250), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("theme", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    ui->stylePanelDims = ImGui::GetWindowSize();

    float cappedScale = ui->textScale;
    if (cappedScale > 1.3f) cappedScale = 1.3f;
    ImGui::SetWindowFontScale(cappedScale);

    if (ui->smallTypeface) ImGui::PushFont(ui->smallTypeface);

    if (ImGui::BeginChild("ThemeContent", ImVec2(0, -40), false, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::Text("Text Color");
        if (!ui->cyclingColors) {
            ImGui::ColorEdit4("##textColor", (float*)&ui->fontColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        }
        ImGui::SameLine();
        ImGui::Checkbox("RGB##text", &ui->cyclingColors);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable rainbow cycling text color");
        }

        ImGui::Text("Background Color");
        ImGui::ColorEdit4("##bgColor", (float*)&ui->bgColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

        if (ui->cyclingColors) {
            ImGui::NewLine();
            ImGui::Text("RGB Speed");
            ImGui::SliderFloat("##rgbSpeed", &ui->cycleRate, 0.1f, 5.0f, "%.1f");
        }

        ImGui::NewLine();
        ImGui::Text("Text Size");
        ImGui::SliderFloat("##fontSize", &ui->textScale, 0.5f, 2.5f, "%.1f");

        ImGui::NewLine();
        ImGui::Text("Menu Opacity");
        ImGui::SliderFloat("##opacity", &ui->windowAlpha, 0.1f, 1.0f, "%.1f");

        ImGui::EndChild();
    }

    if (ui->smallTypeface) ImGui::PopFont();

    if (ImGui::Button("Reset Theme", ImVec2(-1, 0))) {
        ui->fontColor = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
        ui->bgColor = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
        ui->windowAlpha = 1.0f;
        ui->textScale = 1.0f;
        ui->cyclingColors = false;
        ui->cycleRate = 1.0f;
        ui->layoutReset = true;

        ui->primaryPanelDims = ImVec2(350, 525);
        ui->utilityPanelDims = ImVec2(200, 320);
        ui->toolsPanelDims = ImVec2(200, 380);
        ui->stylePanelDims = ImVec2(200, 320);

        ImGuiStyle* style = &ImGui::GetStyle();
        style->Colors[ImGuiCol_Text] = ui->fontColor;
        style->Colors[ImGuiCol_WindowBg] = ImVec4(ui->bgColor.x, ui->bgColor.y, ui->bgColor.z, ui->windowAlpha);
        style->Colors[ImGuiCol_PopupBg] = ImVec4(ui->bgColor.x * 1.1f, ui->bgColor.y * 1.1f, ui->bgColor.z * 1.1f, ui->windowAlpha);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reset all theme settings to default");
    }

    ImGui::End();
}

void MenuInterface::displayPrimaryPanel() {
    ImGuiCond sizeRule = layoutReset ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowSize(primaryPanelDims, sizeRule);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(350, 500), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("info", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    float cappedScale = textScale;
    if (cappedScale > 1.2f) cappedScale = 1.2f;
    ImGui::SetWindowFontScale(cappedScale);

    primaryPanelDims = ImGui::GetWindowSize();

    if (largeTypeface) ImGui::PushFont(largeTypeface);
    ImGui::TextColored(ImVec4(1.f, 0.78f, 0.17f, 1.f), "ToastyReplay");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.f), "v1.0.0-Beta");
    if (largeTypeface) ImGui::PopFont();

    if (smallTypeface) ImGui::PushFont(smallTypeface);

    ReplayEngine* engine = ReplayEngine::get();

    if (ImGui::BeginChild("MainContent", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {

        if (engine->engineMode == MODE_EXECUTE && engine->activeMacro) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "PLAYING");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), ": %s", engine->activeMacro->name.c_str());
            ImGui::SameLine();
            ImGui::Text("| Inputs: %zu", engine->activeMacro->inputs.size());

            if (ImGui::Button("Stop Playback", ImVec2(150, 0))) {
                engine->engineMode = MODE_DISABLED;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Stop playing the macro");
            }

            ImGui::Separator();
            ImGui::NewLine();
        }

        if (engine->engineMode == MODE_CAPTURE && engine->activeMacro) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "RECORDING");
            ImGui::SameLine();
            ImGui::Text("| Inputs: %zu", engine->activeMacro->inputs.size());

            if (!macroNameReady) {
                strncpy(macroNameBuffer, engine->activeMacro->name.c_str(), sizeof(macroNameBuffer) - 1);
                macroNameBuffer[sizeof(macroNameBuffer) - 1] = '\0';
                macroNameReady = true;
            }

            ImGui::Text("Macro Name:");
            if (ImGui::InputText("##recordingName", macroNameBuffer, sizeof(macroNameBuffer))) {
                engine->activeMacro->name = macroNameBuffer;
            }

            if (ImGui::Button("Save Macro", ImVec2(150, 0))) {
                if (!engine->activeMacro->inputs.empty()) {
                    engine->activeMacro->persist();
                    engine->reloadMacroList();
                    log::info("Saved macro: {}", engine->activeMacro->name);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Save the current recording to file");
            }

            ImGui::SameLine();
            if (ImGui::Button("Stop Recording", ImVec2(150, 0))) {
                delete engine->activeMacro;
                engine->activeMacro = nullptr;
                engine->engineMode = MODE_DISABLED;
                macroNameReady = false;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Stop recording and discard unsaved inputs");
            }

            ImGui::Separator();
            ImGui::NewLine();
        } else {
            macroNameReady = false;
        }

        displayModeSelector();

        ImGui::NewLine();

        ImGui::Text("Saved Replays");

        std::string currentMacroName = (engine->activeMacro && engine->engineMode != MODE_CAPTURE) ? engine->activeMacro->name : "Select a replay...";
        if (ImGui::BeginCombo("##ReplayCombo", currentMacroName.c_str())) {
            auto macroListCopy = engine->storedMacros;
            for (const auto& macroName : macroListCopy) {
                bool isSelected = (currentMacroName == macroName);

                ImGui::PushID(macroName.c_str());
                if (ImGui::Selectable(macroName.c_str(), isSelected, 0, ImVec2(ImGui::GetContentRegionAvail().x - 30, 0))) {

                    if (engine->engineMode != MODE_CAPTURE) {
                        MacroSequence* loaded = MacroSequence::loadFromDisk(macroName);
                        if (loaded) {
                            if (engine->activeMacro) delete engine->activeMacro;
                            engine->activeMacro = loaded;
                            engine->engineMode = MODE_EXECUTE;
                        }
                    }
                }

                ImGui::SameLine(ImGui::GetWindowWidth() - 35);
                if (ImGui::Button("X", ImVec2(20, 0))) {
                    auto path = Mod::get()->getSaveDir() / "replays" / (macroName + ".gdr");
                    if (std::filesystem::exists(path)) {
                        std::filesystem::remove(path);
                        engine->reloadMacroList();
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
            engine->reloadMacroList();
        }

        ImGui::SameLine();
        if (ImGui::Button("Open Replays Folder")) {
            auto dir = Mod::get()->getSaveDir() / "replays";
            if (std::filesystem::exists(dir) || std::filesystem::create_directory(dir)) {
                utils::file::openFolder(dir);
            }
        }

        if (engine->activeMacro) {
            ImGui::NewLine();
            ImGui::Text("Loaded: %s", engine->activeMacro->name.c_str());
            ImGui::Text("Inputs: %zu", engine->activeMacro->inputs.size());

            if (engine->engineMode == MODE_EXECUTE) {
                ImGui::Checkbox("Ignore Manual Input", &engine->userInputIgnored);
            }

            if (engine->engineMode == MODE_CAPTURE || engine->engineMode == MODE_EXECUTE) {
                const char* correctionModes[] = { "None", "Input Adjustments", "Frame Replacement" };
                int currentCorrectionMode = engine->positionCorrection ? 2 : (engine->inputCorrection ? 1 : 0);

                ImGui::Text("Accuracy Mode:");
                if (ImGui::BeginCombo("##AccuracyMode", correctionModes[currentCorrectionMode])) {
                    if (ImGui::Selectable("None", currentCorrectionMode == 0)) {
                        engine->positionCorrection = false;
                        engine->inputCorrection = false;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("No position correction.");
                    }

                    if (ImGui::Selectable("Input Adjustments", currentCorrectionMode == 1)) {
                        engine->positionCorrection = false;
                        engine->inputCorrection = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Records position on each click. Efficient and accurate for cube/ball modes");
                    }

                    if (ImGui::Selectable("Frame Replacement", currentCorrectionMode == 2)) {
                        engine->positionCorrection = true;
                        engine->inputCorrection = false;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Records position continuously. Required for ship/wave and ufo modes");
                    }

                    ImGui::EndCombo();
                }

                if (engine->positionCorrection) {
                    ImGui::SliderInt("Frame Replacement Rate", &engine->correctionInterval, 30, 240);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Lower = Less Lag, Higher = More Accurate");
                    }
                }
            }
        }

        ImGui::EndChild();
    }

    if (smallTypeface) ImGui::PopFont();

    ImGui::End();
}

void MenuInterface::displayOverlayBranding() {
    ReplayEngine* engine = ReplayEngine::get();

    bool shouldDisplay = shown || (PlayLayer::get() && engine && engine->engineMode == MODE_EXECUTE);

    if (!shouldDisplay) return;

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

    if (smallTypeface) ImGui::PushFont(smallTypeface);

    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.17f, 0.8f), "ToastyReplay v1.0.0-Beta");

    if (smallTypeface) ImGui::PopFont();

    ImGui::End();
}

void MenuInterface::drawInterface() {
    ReplayEngine* engine = ReplayEngine::get();
    if (engine) {
        engine->processHotkeys();
    }

    displayOverlayBranding();

    if (!shown) {
        if (previouslyShown && engine) {
            engine->reloadMacroList();
        }
        previouslyShown = false;
        return;
    }

    log::info("GUI::renderer() - visible is true, rendering panels");

    if (!previouslyShown && engine) {
        engine->reloadMacroList();
        previouslyShown = true;
    }

    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4 activeFontColor = fontColor;
    if (cyclingColors) {
        activeFontColor = computeCycleColor(cycleRate);
    }
    style->Colors[ImGuiCol_Text] = activeFontColor;
    style->Colors[ImGuiCol_WindowBg] = ImVec4(bgColor.x, bgColor.y, bgColor.z, windowAlpha);
    style->Colors[ImGuiCol_PopupBg] = ImVec4(bgColor.x * 1.1f, bgColor.y * 1.1f, bgColor.z * 1.1f, windowAlpha);

    PlatformToolbox::showCursor();

    displayPrimaryPanel();
    DrawUtilityPanel();
    DrawToolsPanel();
    DrawStylePanel();

    if (validationFailed) {
        validationFailed = false;
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

    layoutReset = false;
}

void MenuInterface::initialize() {
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

    style->Colors[ImGuiCol_Text] = fontColor;
    style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_WindowBg] = ImVec4(bgColor.x, bgColor.y, bgColor.z, windowAlpha);
    style->Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.09f, windowAlpha);
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
        smallTypeface = io.Fonts->AddFontFromFileTTF(path.c_str(), 18.0f);
        largeTypeface = io.Fonts->AddFontFromFileTTF(path.c_str(), 28.0f);
        extraLargeTypeface = io.Fonts->AddFontFromFileTTF(path.c_str(), 100.0f);
    } else {
        smallTypeface = io.Fonts->AddFontDefault();
        largeTypeface = io.Fonts->AddFontDefault();
        extraLargeTypeface = io.Fonts->AddFontDefault();
    }
    io.Fonts->Build();
}

$on_mod(Loaded) {
    log::info("ToastyReplay: Setting up ImGuiCocos from $on_mod(Loaded)");

    ImGuiCocos::get().setup([] {
        log::info("ToastyReplay: ImGuiCocos setup callback running");
        MenuInterface::get()->initialize();
    }).draw([] {
        MenuInterface::get()->drawInterface();
    });
}
