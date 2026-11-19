#ifndef FLOATING_ICON_UI_H
#define FLOATING_ICON_UI_H

#include "../imgui/imgui.h"

inline void RenderFloatingIconUI(bool &menuOpen, bool &iconVisible) {
    if (menuOpen || !iconVisible) return;

    ImGui::SetNextWindowPos(ImVec2(20.0f, 140.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(90.0f, 40.0f), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.12f, 0.18f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.40f, 0.75f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.50f, 0.90f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.60f, 1.00f, 1.00f));

    if (ImGui::Begin("##NINMODS_ICON", nullptr, flags)) {
        if (ImGui::Button("DUMPER", ImVec2(-1.0f, -1.0f))) {
            menuOpen = true;
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
}

#endif // FLOATING_ICON_UI_H
