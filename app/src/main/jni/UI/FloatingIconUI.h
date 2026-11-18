#ifndef FLOATING_ICON_UI_H
#define FLOATING_ICON_UI_H

#include "../imgui/imgui.h"
#include "../Icon/TextureLoader.h"
#include <atomic>

extern int g_GlWidth, g_GlHeight;
extern std::atomic<bool> g_UserInteracted;

inline void RenderFloatingIconUI(bool &menuOpen, bool &iconVisible) {
    if (menuOpen || !iconVisible) return;

    if (g_NinLogoTexture == 0) {
        LoadNinLogoTexture();
    }

    static ImVec2 iconPos(40.0f, 140.0f);
    ImGui::SetNextWindowPos(iconPos, ImGuiCond_FirstUseEver);

    ImGui::Begin("##NINMODS_FLOATING_ICON",
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float iconSize = 75.0f;

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.40f, 0.80f, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.25f, 0.60f, 1.00f, 0.45f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, iconSize * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

    bool clicked = false;
    if (g_NinLogoTexture != 0) {
        clicked = ImGui::ImageButton(
            (ImTextureID)(intptr_t)g_NinLogoTexture,
            ImVec2(iconSize, iconSize)
        );
    } else {
        clicked = ImGui::Button("NIN", ImVec2(iconSize, iconSize));
    }

    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 3.0f)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        iconPos.x += delta.x;
        iconPos.y += delta.y;

        float maxW = (float)g_GlWidth  - iconSize;
        float maxH = (float)g_GlHeight - iconSize;
        if (iconPos.x < 0.0f) iconPos.x = 0.0f;
        if (iconPos.y < 0.0f) iconPos.y = 0.0f;
        if (maxW > 0.0f && iconPos.x > maxW) iconPos.x = maxW;
        if (maxH > 0.0f && iconPos.y > maxH) iconPos.y = maxH;

        ImGui::SetWindowPos(iconPos);
    }

    if (clicked) {
        menuOpen = true;
        g_UserInteracted.store(true);
    }

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);

    ImGui::End();
}

#endif // FLOATING_ICON_UI_H
