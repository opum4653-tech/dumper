// Main overlay window: dump suite buttons + live class/offset inspector.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "../../imgui/imgui.h"
#include "../../Icon/TextureLoader.h"
#include "../Core/DumperLifecycle.h"
#include "../Dumper/Il2CppRealDumper.h"
#include "../Dumper/OfficialDumperEngine.h"

static ImVec2 g_MenuPos(30.0f, 30.0f);
static float  g_MenuWidth  = 440.0f;
static float  g_MenuHeight = 520.0f;

static void PushMenuStyle() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.2f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 18.0f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg,             ImVec4(0.02f, 0.03f, 0.05f, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_Border,               ImVec4(0.35f, 0.55f, 0.85f, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,              ImVec4(0.07f, 0.10f, 0.16f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,        ImVec4(0.10f, 0.15f, 0.24f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Header,               ImVec4(0.14f, 0.24f, 0.38f, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,        ImVec4(0.20f, 0.35f, 0.55f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,         ImVec4(0.25f, 0.45f, 0.70f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,              ImVec4(0.09f, 0.14f, 0.22f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,       ImVec4(0.14f, 0.22f, 0.34f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,        ImVec4(0.18f, 0.28f, 0.42f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,           ImVec4(0.95f, 0.98f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,     ImVec4(0.25f, 0.65f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Button,               ImVec4(0.14f, 0.24f, 0.38f, 0.75f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,        ImVec4(0.20f, 0.36f, 0.56f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,         ImVec4(0.26f, 0.46f, 0.72f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          ImVec4(0.05f, 0.07f, 0.10f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        ImVec4(0.25f, 0.35f, 0.50f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.35f, 0.48f, 0.68f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  ImVec4(0.45f, 0.60f, 0.82f, 1.00f));
}

static void DrawMenuChrome(ImDrawList* dl, ImVec2 winPos, ImVec2 winSize,
                           ImVec2 closeBtnPos, bool overClose, float headerH) {
    dl->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + headerH),
                      IM_COL32(14, 18, 26, 250), 10.0f, ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(winPos.x, winPos.y + headerH),
                ImVec2(winPos.x + winSize.x, winPos.y + headerH),
                IM_COL32(60, 90, 140, 180), 1.0f);

    if (g_NinLogoTexture == 0) LoadNinLogoTexture();
    if (g_NinLogoTexture != 0) {
        float aspect = (g_NinLogoHeight > 0)
            ? ((float)g_NinLogoWidth / (float)g_NinLogoHeight) : 2.5f;
        float hLogoH = 92.0f;
        float hLogoW = hLogoH * aspect;
        float hLogoX = winPos.x + (winSize.x - hLogoW) * 0.5f;
        float hLogoY = winPos.y + headerH - hLogoH;
        dl->PushClipRectFullScreen();
        dl->AddImage((ImTextureID)(intptr_t)g_NinLogoTexture,
                     ImVec2(hLogoX, hLogoY), ImVec2(hLogoX + hLogoW, hLogoY + hLogoH),
                     ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
        dl->PopClipRect();
    }

    dl->AddRectFilled(closeBtnPos, ImVec2(closeBtnPos.x + 24.0f, closeBtnPos.y + 24.0f),
                      overClose ? IM_COL32(220, 50, 60, 200) : IM_COL32(50, 65, 88, 140), 6.0f);
    dl->AddLine(ImVec2(closeBtnPos.x + 7.0f,  closeBtnPos.y + 7.0f),
                ImVec2(closeBtnPos.x + 17.0f, closeBtnPos.y + 17.0f),
                IM_COL32(255, 255, 255, 240), 2.0f);
    dl->AddLine(ImVec2(closeBtnPos.x + 17.0f, closeBtnPos.y + 7.0f),
                ImVec2(closeBtnPos.x + 7.0f,  closeBtnPos.y + 17.0f),
                IM_COL32(255, 255, 255, 240), 2.0f);
}

static void DrawDumpControls() {
    float btnW = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
    const bool busy = OfficialDumper::IsDumping() || RealDumper::IsDumping();

    if (busy) {
        const int pct = OfficialDumper::IsDumping()
            ? OfficialDumper::GetProgress() : RealDumper::GetProgress();
        const char* st = OfficialDumper::IsDumping()
            ? OfficialDumper::GetStatus() : RealDumper::GetStatus();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
        ImGui::Button("DUMPING IN PROGRESS...", ImVec2(-1, 36.0f));
        ImGui::PopStyleColor();

        ImGui::ProgressBar((float)pct / 100.0f, ImVec2(-1, 18.0f));
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "%s", st);
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.22f, 0.12f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.30f, 0.16f, 1.00f));
    if (ImGui::Button("DUMP OFFICIAL SUITE (CS + JSON + SDK + PY)", ImVec2(-1, 38.0f))) {
        OfficialDumper::TriggerOfficialDumpSuiteAsync();
    }
    ImGui::PopStyleColor(2);

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.12f, 0.42f, 0.25f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.56f, 0.32f, 0.95f));
    if (ImGui::Button("DUMP.CS", ImVec2(btnW, 34.0f))) {
        RealDumper::TriggerFullCsDumpAsync();
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.32f, 0.58f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.42f, 0.72f, 0.95f));
    if (ImGui::Button("GROUPED C++", ImVec2(btnW, 34.0f))) {
        RealDumper::TriggerGroupedOffsetsDumpAsync();
    }
    ImGui::PopStyleColor(2);

    if (OfficialDumper::GetStatus()[0] != '\0' && strcmp(OfficialDumper::GetStatus(), "IDLE") != 0) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", OfficialDumper::GetStatus());
    } else if (RealDumper::GetStatus()[0] != '\0' && strcmp(RealDumper::GetStatus(), "IDLE") != 0) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", RealDumper::GetStatus());
    }
}

inline void RenderMainModMenu(bool& menuOpen, int& currentTab) {
    (void)currentTab;

    const float screenW = (ImGui::GetIO().DisplaySize.x > 0.0f)
        ? ImGui::GetIO().DisplaySize.x : (float)g_GlWidth;
    const float screenH = (ImGui::GetIO().DisplaySize.y > 0.0f)
        ? ImGui::GetIO().DisplaySize.y : (float)g_GlHeight;

    g_MenuHeight = (screenH > 0.0f) ? (screenH * 0.72f) : 480.0f;
    if (g_MenuHeight < 360.0f) g_MenuHeight = 360.0f;
    g_MenuWidth = (screenW > 460.0f) ? 440.0f : (screenW - 20.0f);

    const float maxX = (screenW > g_MenuWidth)  ? (screenW - g_MenuWidth)  : 0.0f;
    const float maxY = (screenH > g_MenuHeight) ? (screenH - g_MenuHeight) : 0.0f;
    g_MenuPos.x = ImClamp(g_MenuPos.x, 0.0f, maxX);
    g_MenuPos.y = ImClamp(g_MenuPos.y, 40.0f, maxY);

    ImGui::SetNextWindowPos(g_MenuPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(g_MenuWidth, g_MenuHeight), ImGuiCond_Always);

    PushMenuStyle();

    if (ImGui::Begin("##NINMODS_REAL_INSPECTOR", &menuOpen,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove)) {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

        const float headerH = 44.0f;
        const ImVec2 winPos  = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();
        const ImVec2 mouse   = ImGui::GetIO().MousePos;
        const ImVec2 closeBtnPos(winPos.x + winSize.x - 34.0f,
                                 winPos.y + (headerH - 24.0f) * 0.5f);

        const bool overClose =
            (mouse.x >= closeBtnPos.x && mouse.x <= closeBtnPos.x + 24.0f &&
             mouse.y >= closeBtnPos.y && mouse.y <= closeBtnPos.y + 24.0f);
        const bool overTitleBar =
            (mouse.y >= winPos.y - 48.0f && mouse.y <= winPos.y + headerH &&
             mouse.x >= winPos.x && mouse.x < closeBtnPos.x);
        const bool overBody =
            (mouse.x >= winPos.x && mouse.x <= winPos.x + winSize.x &&
             mouse.y >= winPos.y && mouse.y <= winPos.y + winSize.y);

        if (ImGui::IsMouseClicked(0) && overClose) menuOpen = false;

        static bool s_dragTitle = false, s_dragBody = false;
        if (ImGui::IsMouseClicked(0)) {
            if (overTitleBar)      { s_dragTitle = true;  s_dragBody = false; }
            else if (overBody)     { s_dragTitle = false; s_dragBody = true;  }
        }
        if (ImGui::IsMouseReleased(0)) { s_dragTitle = false; s_dragBody = false; }

        if (s_dragTitle && ImGui::IsMouseDragging(0, 1.0f)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            g_MenuPos.x = ImClamp(g_MenuPos.x + delta.x, 0.0f, maxX);
            g_MenuPos.y = ImClamp(g_MenuPos.y + delta.y, 0.0f, maxY);
        } else if (s_dragBody && ImGui::IsMouseDragging(0, 2.0f)) {
            ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Body watermark
        if (g_NinLogoTexture == 0) LoadNinLogoTexture();
        if (g_NinLogoTexture != 0) {
            const float bodyW = winSize.x, bodyH = winSize.y - headerH;
            const float aspect = (g_NinLogoHeight > 0)
                ? ((float)g_NinLogoWidth / (float)g_NinLogoHeight) : 1.0f;
            const float logoW = bodyW * 0.88f, logoH = logoW / aspect;
            const float logoX = winPos.x + (bodyW - logoW) * 0.5f;
            const float logoY = winPos.y + headerH + (bodyH - logoH) * 0.5f;

            dl->PushClipRect(ImVec2(winPos.x, winPos.y + headerH),
                             ImVec2(winPos.x + winSize.x, winPos.y + winSize.y), true);
            dl->AddImage((ImTextureID)(intptr_t)g_NinLogoTexture,
                         ImVec2(logoX, logoY), ImVec2(logoX + logoW, logoY + logoH),
                         ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 60));
            dl->PopClipRect();
        }

        ImGui::SetCursorPosY(headerH + 6.0f);

        // ---- runtime status ----
        static const std::string s_pkg = OfficialDumper::GetCurrentPackageName();
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "IL2CPP UNIVERSAL LIVE DUMPER");
        ImGui::TextColored(ImVec4(0.40f, 1.0f, 0.60f, 1.0f),
                           "Target: %s (PID: %d)", s_pkg.c_str(), (int)getpid());
        ImGui::TextColored(ImVec4(0.80f, 0.85f, 0.90f, 0.90f),
                           "Base: 0x%lx | Assemblies: %zu",
                           (unsigned long)il2cpp_base, Il2CppGetAssemblyCount());
        ImGui::Separator();

        // ---- dump engine ----
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.30f, 1.0f),
                           "DUMP ENGINE (no PC tools required):");
        DrawDumpControls();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ---- live class inspector ----
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "LIVE CLASS & OFFSET INSPECTOR:");

        static char s_query[64] = "Player";
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputTextWithHint("##Query", "class name (Player, Weapon, Camera)...",
                                 s_query, sizeof(s_query));

        static const void* s_klass = nullptr;
        static char s_klassName[128] = "";
        static char s_klassNs[128] = "";

        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 0.8f), "Presets:");
        static const char* kPresets[] = { "Player", "Weapon", "Camera", "GameFacade" };
        for (int i = 0; i < 4; ++i) {
            if (i > 0) ImGui::SameLine();
            if (ImGui::SmallButton(kPresets[i])) {
                strncpy(s_query, kPresets[i], sizeof(s_query) - 1);
                s_query[sizeof(s_query) - 1] = '\0';
                s_klass = nullptr;
            }
        }

        if (s_query[0] != '\0') {
            auto results = RealDumper::SearchClassesLive(s_query, 15);
            if (results.empty()) {
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 0.8f),
                                   "No classes matched '%s'", s_query);
            } else {
                ImGui::BeginChild("##Results", ImVec2(-1, 100.0f), true,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                for (size_t i = 0; i < results.size(); ++i) {
                    const auto& r = results[i];
                    char label[320];
                    snprintf(label, sizeof(label), "%s%s%s (%s)",
                             r.namespaze, r.namespaze[0] ? "." : "",
                             r.name, r.imageName);

                    if (ImGui::Selectable(label, s_klass == r.klass)) {
                        s_klass = r.klass;
                        snprintf(s_klassName, sizeof(s_klassName), "%s", r.name);
                        snprintf(s_klassNs, sizeof(s_klassNs), "%s", r.namespaze);
                    }
                }
                ImGui::EndChild();
            }
        }

        if (s_klass) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.6f, 1.0f), "INSPECTING: %s%s%s",
                               s_klassNs, s_klassNs[0] ? "." : "", s_klassName);

            if (ImGui::CollapsingHeader("FIELDS", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BeginChild("##Fields", ImVec2(-1, 140.0f), true);
                void* iter = nullptr;
                int count = 0;
                while (void* field = Il2CppClassGetFields(s_klass, &iter)) {
                    const char* fname = Il2CppFieldGetName(field);
                    if (!fname || fname[0] == '\0') continue;

                    const void* ftype = Il2CppFieldGetType(field);
                    const char* tname = ftype ? Il2CppTypeGetName(ftype) : "var";

                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f),
                                       "+0x%zx", Il2CppFieldGetOffset(field));
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "[%s]", tname);
                    ImGui::SameLine();
                    ImGui::Text("%s", fname);
                    ++count;
                }
                if (count == 0) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 0.8f), "(no instance fields)");
                }
                ImGui::EndChild();
            }

            if (ImGui::CollapsingHeader("METHODS", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BeginChild("##Methods", ImVec2(-1, 140.0f), true);
                void* iter = nullptr;
                int count = 0;
                while (void* method = Il2CppClassGetMethods(s_klass, &iter)) {
                    const char* mname = Il2CppMethodGetName(method);
                    if (!mname || mname[0] == '\0') continue;

                    void* code = *(void**)method;
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f),
                                       "RVA: 0x%lx", (unsigned long)RealDumper::GetRva(code));
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 0.8f),
                                       "VA: 0x%lx", (unsigned long)code);
                    ImGui::SameLine();
                    ImGui::Text("%s(%u args)", mname, Il2CppMethodGetParamCount(method));
                    ++count;
                }
                if (count == 0) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 0.8f), "(no methods found)");
                }
                ImGui::EndChild();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.25f, 0.35f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.38f, 0.52f, 0.95f));
        if (ImGui::Button("HIDE MENU", ImVec2(-1, 38.0f))) menuOpen = false;
        ImGui::PopStyleColor(2);

        DrawMenuChrome(dl, winPos, winSize, closeBtnPos, overClose, headerH);
        ImGui::PopItemWidth();
    }
    ImGui::End();

    ImGui::PopStyleColor(19);
    ImGui::PopStyleVar(7);
}
