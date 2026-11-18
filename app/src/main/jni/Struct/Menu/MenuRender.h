#pragma once
#include <cstdint>
#if !defined(__uintptr_t_defined) && !defined(_UINTPTR_T_DEFINED)
#ifdef __UINTPTR_TYPE__
typedef __UINTPTR_TYPE__ uintptr_t;
#else
typedef unsigned long uintptr_t;
#endif
#define __uintptr_t_defined
#endif

#include "../../imgui/imgui.h"
#include "../../imgui/imgui_internal.h"
#include "../Core/DumperLifecycle.h"
#include "../Dumper/Il2CppRealDumper.h"
#include "../Dumper/OfficialDumperEngine.h"
#include "../../Icon/TextureLoader.h"

template<typename T>
static inline T ImClampVal(T v, T mn, T mx) { return (v < mn) ? mn : (v > mx) ? mx : v; }

struct TouchState {
    volatile bool val;
    void store(bool v) { val = v; }
    bool load() const { return val; }
};
static TouchState g_TouchOnMenu = {false};
static ImVec2 g_MenuPos(30.0f, 30.0f);
static float g_MenuWidth = 440.0f;
static float g_MenuHeight = 520.0f;

inline void RenderMainModMenu(bool &menuOpen, int &currentTab) {
    void EnsureIl2CppReady();
    EnsureIl2CppReady();

    float screenW = (ImGui::GetIO().DisplaySize.x > 0.0f) ? ImGui::GetIO().DisplaySize.x : (float)g_GlWidth;
    float screenH = (ImGui::GetIO().DisplaySize.y > 0.0f) ? ImGui::GetIO().DisplaySize.y : (float)g_GlHeight;
    g_MenuHeight = (screenH > 0.0f) ? (screenH * 0.72f) : 480.0f;
    if (g_MenuHeight < 360.0f) g_MenuHeight = 360.0f;
    g_MenuWidth = (screenW > 460.0f) ? 440.0f : (screenW - 20.0f);

    float maxX = (screenW > g_MenuWidth) ? (screenW - g_MenuWidth) : 0.0f;
    float maxY = (screenH > g_MenuHeight) ? (screenH - g_MenuHeight) : 0.0f;
    g_MenuPos.x = ImClamp(g_MenuPos.x, 0.0f, maxX);
    g_MenuPos.y = ImClamp(g_MenuPos.y, 40.0f, maxY);

    ImGui::SetNextWindowPos(g_MenuPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(g_MenuWidth, g_MenuHeight), ImGuiCond_Always);

    static bool s_wasMenuOpen = false;
    if (menuOpen && !s_wasMenuOpen) {
        ImGui::SetScrollY(0.0f);
    }
    s_wasMenuOpen = menuOpen;

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
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,          ImVec4(0.95f, 0.98f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,    ImVec4(0.25f, 0.65f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Button,               ImVec4(0.14f, 0.24f, 0.38f, 0.75f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,        ImVec4(0.20f, 0.36f, 0.56f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,         ImVec4(0.26f, 0.46f, 0.72f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          ImVec4(0.05f, 0.07f, 0.10f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        ImVec4(0.25f, 0.35f, 0.50f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.35f, 0.48f, 0.68f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  ImVec4(0.45f, 0.60f, 0.82f, 1.00f));

    if (ImGui::Begin("##NINMODS_REAL_INSPECTOR", &menuOpen, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove)) {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        ImVec2 winPos = ImGui::GetWindowPos(), winSize = ImGui::GetWindowSize(), mousePos = ImGui::GetIO().MousePos;
        float headerH = 44.0f;
        ImVec2 closeBtnPos(winPos.x + winSize.x - 34.0f, winPos.y + (headerH - 24.0f) * 0.5f);
        bool overClose = (mousePos.x >= closeBtnPos.x && mousePos.x <= closeBtnPos.x + 24.0f &&
                          mousePos.y >= closeBtnPos.y && mousePos.y <= closeBtnPos.y + 24.0f);
        bool overTitleBar = (mousePos.y >= winPos.y - 48.0f && mousePos.y <= winPos.y + headerH &&
                             mousePos.x >= winPos.x && mousePos.x < closeBtnPos.x);
        bool overMenu = (mousePos.x >= winPos.x && mousePos.x <= winPos.x + winSize.x &&
                         mousePos.y >= winPos.y && mousePos.y <= winPos.y + winSize.y);

        if (ImGui::IsMouseClicked(0) && overClose) { menuOpen = false; }

        static bool s_dragT = false, s_dragB = false;
        if (ImGui::IsMouseClicked(0)) {
            if (overTitleBar) { s_dragT = true; s_dragB = false; }
            else if (overMenu && !overClose) { s_dragT = false; s_dragB = true; }
        }
        if (ImGui::IsMouseReleased(0)) { s_dragT = false; s_dragB = false; }

        if (s_dragT && ImGui::IsMouseDragging(0, 1.0f)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            g_MenuPos.x = ImClamp(g_MenuPos.x + delta.x, 0.0f, maxX);
            g_MenuPos.y = ImClamp(g_MenuPos.y + delta.y, 0.0f, maxY);
        } else if (s_dragB && ImGui::IsMouseDragging(0, 2.0f)) {
            ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Background Logo watermark
        if (g_NinLogoTexture == 0) LoadNinLogoTexture();
        if (g_NinLogoTexture != 0) {
            float bodyW = winSize.x, bodyH = winSize.y - headerH;
            float aspect = (g_NinLogoHeight > 0) ? ((float)g_NinLogoWidth / (float)g_NinLogoHeight) : 1.0f;
            float logoW = bodyW * 0.88f, logoH = logoW / aspect;
            float logoX = winPos.x + (bodyW - logoW) * 0.5f, logoY = winPos.y + headerH + (bodyH - logoH) * 0.5f;

            drawList->PushClipRect(ImVec2(winPos.x, winPos.y + headerH), ImVec2(winPos.x + winSize.x, winPos.y + winSize.y), true);
            drawList->AddImage((ImTextureID)(uintptr_t)g_NinLogoTexture, ImVec2(logoX, logoY), ImVec2(logoX + logoW, logoY + logoH), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 60));
            drawList->PopClipRect();
        }

        ImGui::SetCursorPosY(headerH + 6.0f);

        // ====================================================================
        // 1. LIVE SYSTEM & MEMORY STATUS (UNIVERSAL)
        // ====================================================================
        static std::string s_pkg = OfficialDumper::GetCurrentPackageName();
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "IL2CPP UNIVERSAL LIVE DUMPER");
        ImGui::TextColored(ImVec4(0.40f, 1.0f, 0.60f, 1.0f), "Target: %s (PID: %d)", s_pkg.c_str(), getpid());
        ImGui::TextColored(ImVec4(0.80f, 0.85f, 0.90f, 0.90f), "Base: 0x%lx | Assemblies: %zu",
            (unsigned long)il2cpp_base, Il2CppGetAssemblyCount());
        ImGui::Separator();

        // ====================================================================
        // 2. OFFICIAL DUMP ENGINE (Direct Storage Output)
        // ====================================================================
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.30f, 1.0f), "OFFICIAL DUMP ENGINE (Zero PC Tools Required):");

        float btnW = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
        bool dumpingNow = OfficialDumper::IsDumping() || RealDumper::IsDumping();

        if (dumpingNow) {
            int pct = OfficialDumper::IsDumping() ? OfficialDumper::GetProgress() : RealDumper::GetProgress();
            const char* st = OfficialDumper::IsDumping() ? OfficialDumper::GetStatus() : RealDumper::GetStatus();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
            ImGui::Button("DUMPING IN PROGRESS...", ImVec2(-1, 36.0f));
            ImGui::PopStyleColor();

            ImGui::ProgressBar((float)pct / 100.0f, ImVec2(-1, 18.0f));
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "%s", st);
        } else {
            // Master 1-Click All-In-One Official Dump Button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.22f, 0.12f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.30f, 0.16f, 1.00f));
            if (ImGui::Button("★ DUMP OFFICIAL SUITE (CS + SCRIPT.JSON + SDK + PYTHON) ★", ImVec2(-1, 38.0f))) {
                OfficialDumper::TriggerOfficialDumpSuiteAsync();
            }
            ImGui::PopStyleColor(2);

            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.42f, 0.25f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.56f, 0.32f, 0.95f));
            if (ImGui::Button("DUMP DUMP.CS", ImVec2(btnW, 34.0f))) {
                RealDumper::TriggerFullCsDumpAsync();
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.32f, 0.58f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.42f, 0.72f, 0.95f));
            if (ImGui::Button("DUMP GROUPED (C++)", ImVec2(btnW, 34.0f))) {
                RealDumper::TriggerGroupedOffsetsDumpAsync();
            }
            ImGui::PopStyleColor(2);

            if (OfficialDumper::GetStatus()[0] != '\0' && strcmp(OfficialDumper::GetStatus(), "IDLE") != 0) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", OfficialDumper::GetStatus());
            } else if (RealDumper::GetStatus()[0] != '\0' && strcmp(RealDumper::GetStatus(), "IDLE") != 0) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", RealDumper::GetStatus());
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ====================================================================
        // 3. LIVE IN-GAME CLASS & OFFSET TRACKER
        // ====================================================================
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "LIVE OFFSET & CLASS INSPECTOR:");

        static char s_searchQuery[64] = "Player";
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputTextWithHint("##SearchQuery", "Type class name (e.g. Player, Weapon, Camera)...", s_searchQuery, sizeof(s_searchQuery));

        static const void* s_selectedKlass = nullptr;
        static char s_selectedClassName[128] = "";
        static char s_selectedNamespace[128] = "";

        // Fast Preset Class Buttons
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 0.8f), "Presets:");
        ImGui::SameLine();
        if (ImGui::SmallButton("Player")) {
            strncpy(s_searchQuery, "Player", sizeof(s_searchQuery));
            s_selectedKlass = nullptr;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Weapon")) {
            strncpy(s_searchQuery, "Weapon", sizeof(s_searchQuery));
            s_selectedKlass = nullptr;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Camera")) {
            strncpy(s_searchQuery, "Camera", sizeof(s_searchQuery));
            s_selectedKlass = nullptr;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("GameFacade")) {
            strncpy(s_searchQuery, "GameFacade", sizeof(s_searchQuery));
            s_selectedKlass = nullptr;
        }

        // Search Results List
        if (s_searchQuery[0] != '\0') {
            auto results = RealDumper::SearchClassesLive(s_searchQuery, 15);
            if (!results.empty()) {
                ImGui::BeginChild("##SearchResults", ImVec2(-1, 100.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
                for (size_t i = 0; i < results.size(); ++i) {
                    char label[256];
                    snprintf(label, sizeof(label), "%s%s%s (%s)",
                        results[i].namespaze,
                        (results[i].namespaze[0] != '\0') ? "." : "",
                        results[i].name,
                        results[i].imageName);

                    bool isSelected = (s_selectedKlass == results[i].klass);
                    if (ImGui::Selectable(label, isSelected)) {
                        s_selectedKlass = results[i].klass;
                        snprintf(s_selectedClassName, sizeof(s_selectedClassName), "%s", results[i].name);
                        snprintf(s_selectedNamespace, sizeof(s_selectedNamespace), "%s", results[i].namespaze);
                    }
                }
                ImGui::EndChild();
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 0.8f), "No classes matched '%s'", s_searchQuery);
            }
        }

        // ====================================================================
        // 4. DETAILED LIVE INSPECTION OF SELECTED CLASS
        // ====================================================================
        if (s_selectedKlass != nullptr) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.6f, 1.0f), "INSPECTING: %s%s%s",
                s_selectedNamespace, (s_selectedNamespace[0] != '\0') ? "." : "", s_selectedClassName);

            if (ImGui::CollapsingHeader("REAL FIELDS (OFFSETS)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BeginChild("##FieldsChild", ImVec2(-1, 140.0f), true);
                void* fIter = nullptr;
                int fieldCount = 0;
                while (void* field = Il2CppClassGetFields(s_selectedKlass, &fIter)) {
                    const char* fName = Il2CppFieldGetName(field);
                    size_t fOff = Il2CppFieldGetOffset(field);
                    const void* fType = Il2CppFieldGetType(field);
                    const char* fTypeName = fType ? Il2CppTypeGetName(fType) : "var";

                    if (fName && fName[0] != '\0') {
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "+0x%zx", fOff);
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "[%s]", fTypeName ? fTypeName : "var");
                        ImGui::SameLine();
                        ImGui::Text("%s", fName);
                        fieldCount++;
                    }
                }
                if (fieldCount == 0) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 0.8f), "(No instance fields)");
                }
                ImGui::EndChild();
            }

            if (ImGui::CollapsingHeader("REAL METHODS (RVAS & MEMORY)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BeginChild("##MethodsChild", ImVec2(-1, 140.0f), true);
                void* mIter = nullptr;
                int methodCount = 0;
                while (void* method = Il2CppClassGetMethods(s_selectedKlass, &mIter)) {
                    const char* mName = Il2CppMethodGetName(method);
                    uint32_t pCount = Il2CppMethodGetParamCount(method);
                    void* codePtr = *(void**)method;
                    uintptr_t rva = RealDumper::GetRva(codePtr);

                    if (mName && mName[0] != '\0') {
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "RVA: 0x%lx", (unsigned long)rva);
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 0.8f), "VA: 0x%lx", (unsigned long)codePtr);
                        ImGui::SameLine();
                        ImGui::Text("%s(%u args)", mName, pCount);
                        methodCount++;
                    }
                }
                if (methodCount == 0) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 0.8f), "(No methods found)");
                }
                ImGui::EndChild();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.25f, 0.35f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.38f, 0.52f, 0.95f));
        if (ImGui::Button("HIDE MENU / CLOSE", ImVec2(-1, 38.0f))) {
            menuOpen = false;
        }
        ImGui::PopStyleColor(2);

        // Pinned Header Bar
        drawList->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + headerH), IM_COL32(14, 18, 26, 250), 10.0f, ImDrawFlags_RoundCornersTop);
        drawList->AddLine(ImVec2(winPos.x, winPos.y + headerH), ImVec2(winPos.x + winSize.x, winPos.y + headerH), IM_COL32(60, 90, 140, 180), 1.0f);

        if (g_NinLogoTexture == 0) LoadNinLogoTexture();
        if (g_NinLogoTexture != 0) {
            float aspect = (g_NinLogoHeight > 0) ? ((float)g_NinLogoWidth / (float)g_NinLogoHeight) : 2.5f;
            float hLogoH = 92.0f, hLogoW = hLogoH * aspect;
            float hLogoX = winPos.x + (winSize.x - hLogoW) * 0.5f, hLogoY = winPos.y + headerH - hLogoH;
            drawList->PushClipRectFullScreen();
            drawList->AddImage((ImTextureID)(uintptr_t)g_NinLogoTexture, ImVec2(hLogoX, hLogoY), ImVec2(hLogoX + hLogoW, hLogoY + hLogoH), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
            drawList->PopClipRect();
        }

        drawList->AddRectFilled(closeBtnPos, ImVec2(closeBtnPos.x + 24.0f, closeBtnPos.y + 24.0f), overClose ? IM_COL32(220, 50, 60, 200) : IM_COL32(50, 65, 88, 140), 6.0f);
        drawList->AddLine(ImVec2(closeBtnPos.x + 7.0f, closeBtnPos.y + 7.0f), ImVec2(closeBtnPos.x + 17.0f, closeBtnPos.y + 17.0f), IM_COL32(255, 255, 255, 240), 2.0f);
        drawList->AddLine(ImVec2(closeBtnPos.x + 17.0f, closeBtnPos.y + 7.0f), ImVec2(closeBtnPos.x + 7.0f, closeBtnPos.y + 17.0f), IM_COL32(255, 255, 255, 240), 2.0f);

        ImGui::PopItemWidth();
    }
    ImGui::End();
    ImGui::PopStyleColor(19);
    ImGui::PopStyleVar(7);
}
