// MenuRender.h - the overlay window.
//
// Two panels: the dump engine and the live inspector. Every value the inspector
// shows is queried from the running runtime at draw time, so a field offset or
// an RVA that appears here is the same number the dump writes to disk.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

#include "../../imgui/imgui.h"
#include "../../imgui/imgui_internal.h"
#include "../../Icon/TextureLoader.h"
#include "../Core/DumperLifecycle.h"
#include "../Dumper/Il2CppDumper.h"

namespace MenuTheme {

inline ImVec2 g_position(30.0f, 30.0f);
inline float  g_width  = 440.0f;
inline float  g_height = 520.0f;

inline float Clamp(float value, float low, float high) {
    return (value < low) ? low : (value > high) ? high : value;
}

inline void Push() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.2f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 18.0f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg,             ImVec4(0.02f, 0.03f, 0.05f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_Border,               ImVec4(0.35f, 0.55f, 0.85f, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Header,               ImVec4(0.14f, 0.24f, 0.38f, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,        ImVec4(0.20f, 0.35f, 0.55f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,         ImVec4(0.25f, 0.45f, 0.70f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,              ImVec4(0.09f, 0.14f, 0.22f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,       ImVec4(0.14f, 0.22f, 0.34f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,        ImVec4(0.18f, 0.28f, 0.42f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Button,               ImVec4(0.14f, 0.24f, 0.38f, 0.75f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,        ImVec4(0.20f, 0.36f, 0.56f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,         ImVec4(0.26f, 0.46f, 0.72f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          ImVec4(0.05f, 0.07f, 0.10f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        ImVec4(0.25f, 0.35f, 0.50f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.35f, 0.48f, 0.68f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  ImVec4(0.45f, 0.60f, 0.82f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,         ImVec4(0.50f, 0.55f, 0.62f, 0.85f));
}

inline void Pop() {
    ImGui::PopStyleColor(16);
    ImGui::PopStyleVar(7);
}

} // namespace MenuTheme

// ---------------------------------------------------------------------------
// Live class search
// ---------------------------------------------------------------------------
struct ClassHit {
    const void* klass = nullptr;
    char name[96] = {0};
    char namespaze[96] = {0};
    char image[64] = {0};
};

inline bool ContainsInsensitive(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    if (!needle[0]) return true;
    for (const char* h = haystack; *h; ++h) {
        const char* a = h;
        const char* b = needle;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
            if (ca != cb) break;
            ++a;
            ++b;
        }
        if (!*b) return true;
    }
    return false;
}

// Searches every image the runtime exposes. Enumeration is cached inside the
// il2cpp layer, so repeating a query is cheap.
inline void SearchClasses(const char* query, size_t limit, std::vector<ClassHit>& out) {
    out.clear();
    if (!query || !query[0]) return;
    if (!Il2CppIsReady()) return;

    const size_t assemblyCount = Il2CppGetAssemblyCount();
    for (size_t a = 0; a < assemblyCount && out.size() < limit; ++a) {
        const void* image = Il2CppAssemblyGetImage(Il2CppGetAssembly(a));
        if (!image) continue;

        const char* imageName = Il2CppImageGetName(image);
        const size_t classCount = Il2CppImageGetClassCount(image);

        for (size_t c = 0; c < classCount && out.size() < limit; ++c) {
            const void* klass = Il2CppImageGetClass(image, c);
            if (!klass) continue;

            const char* name = Il2CppClassGetName(klass);
            if (!name || !name[0]) continue;
            if (!ContainsInsensitive(name, query)) continue;

            ClassHit hit;
            hit.klass = klass;
            snprintf(hit.name, sizeof(hit.name), "%s", name);
            const char* ns = Il2CppClassGetNamespace(klass);
            snprintf(hit.namespaze, sizeof(hit.namespaze), "%s", ns ? ns : "");
            snprintf(hit.image, sizeof(hit.image), "%s", imageName ? imageName : "");
            out.push_back(hit);
        }
    }
}

// ---------------------------------------------------------------------------
// Dump controls
// ---------------------------------------------------------------------------
inline void DrawDumpControls() {
    if (Dumper::Busy()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.20f, 0.55f));
        ImGui::Button("DUMPING...", ImVec2(-1, 38.0f));
        ImGui::PopStyleColor();
        ImGui::ProgressBar((float)Dumper::Progress() / 100.0f, ImVec2(-1, 18.0f));
        ImGui::TextColored(ImVec4(0.40f, 1.00f, 0.60f, 1.00f), "%s", Dumper::Status());
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.22f, 0.12f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.30f, 0.16f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.80f, 0.36f, 0.20f, 1.00f));
    if (ImGui::Button("RUN FULL DUMP", ImVec2(-1, 40.0f))) {
        Dumper::TriggerFullDump();
    }
    ImGui::PopStyleColor(3);

    const char* status = Dumper::Status();
    if (status[0] && strcmp(status, "IDLE") != 0) {
        ImGui::TextColored(ImVec4(0.90f, 0.85f, 0.40f, 1.00f), "%s", status);
    }
}

// ---------------------------------------------------------------------------
// Live inspector
// ---------------------------------------------------------------------------
inline void DrawInspector() {
    static char query[64] = "Player";
    static const void* selected = nullptr;
    static char selectedName[96] = "";
    static char selectedNamespace[96] = "";

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##query", "class name filter", query, sizeof(query));

    static const char* kPresets[] = { "Player", "Weapon", "Camera", "Manager" };
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::SmallButton(kPresets[i])) {
            snprintf(query, sizeof(query), "%s", kPresets[i]);
            selected = nullptr;
        }
    }

    std::vector<ClassHit> hits;
    SearchClasses(query, 24, hits);

    if (hits.empty()) {
        ImGui::TextColored(ImVec4(0.85f, 0.45f, 0.45f, 0.85f), "no class matched");
    } else {
        ImGui::BeginChild("##hits", ImVec2(-1, 96.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        for (const ClassHit& hit : hits) {
            char label[320];
            snprintf(label, sizeof(label), "%s%s%s  [%s]",
                     hit.namespaze, hit.namespaze[0] ? "." : "", hit.name, hit.image);
            if (ImGui::Selectable(label, selected == hit.klass)) {
                selected = hit.klass;
                snprintf(selectedName, sizeof(selectedName), "%s", hit.name);
                snprintf(selectedNamespace, sizeof(selectedNamespace), "%s", hit.namespaze);
            }
        }
        ImGui::EndChild();
    }

    if (!selected) return;

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.35f, 1.00f, 0.65f, 1.00f), "%s%s%s",
                       selectedNamespace, selectedNamespace[0] ? "." : "", selectedName);

    if (ImGui::CollapsingHeader("FIELDS", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("##fields", ImVec2(-1, 132.0f), true);
        void* iter = nullptr;
        int count = 0;
        while (void* field = Il2CppClassGetFields(selected, &iter)) {
            const char* name = Il2CppFieldGetName(field);
            if (!name || !name[0]) continue;

            const void* type = Il2CppFieldGetType(field);
            const char* typeName = Il2CppTypeGetName(type);

            ImGui::TextColored(ImVec4(1.00f, 0.85f, 0.35f, 1.00f), "+0x%zx", Il2CppFieldGetOffset(field));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.60f, 0.80f, 1.00f, 1.00f), "[%s]", (typeName && typeName[0]) ? typeName : "?");
            ImGui::SameLine();
            ImGui::Text("%s", name);
            ++count;
        }
        if (!count) {
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 0.85f), "no instance fields");
        }
        ImGui::EndChild();
    }

    if (ImGui::CollapsingHeader("METHODS", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("##methods", ImVec2(-1, 132.0f), true);
        void* iter = nullptr;
        int count = 0;
        while (const void* method = Il2CppClassGetMethods(selected, &iter)) {
            const char* name = Il2CppMethodGetName(method);
            if (!name || !name[0]) continue;

            void* code = Il2CppMethodGetPointer(method);
            if (code) {
                const RvaResolver::Location location = RvaResolver::Resolve((uintptr_t)code);
                if (location.insideModule) {
                    ImGui::TextColored(ImVec4(0.30f, 1.00f, 0.55f, 1.00f), "RVA 0x%llx",
                                       (unsigned long long)location.rva);
                } else {
                    ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.00f), "EXT %s",
                                       location.moduleName.c_str());
                }
            } else {
                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 0.85f), "no body");
            }

            ImGui::SameLine();
            ImGui::Text("%s(%u)", name, Il2CppMethodGetParamCount(method));
            ++count;
        }
        if (!count) {
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 0.85f), "no methods");
        }
        ImGui::EndChild();
    }
}

// ---------------------------------------------------------------------------
// Chrome
// ---------------------------------------------------------------------------
inline void DrawHeaderBar(ImDrawList* drawList, ImVec2 windowPos, ImVec2 windowSize,
                          ImVec2 closePos, bool overClose, float headerHeight) {
    drawList->AddRectFilled(windowPos,
                            ImVec2(windowPos.x + windowSize.x, windowPos.y + headerHeight),
                            IM_COL32(14, 18, 26, 250), 10.0f, ImDrawFlags_RoundCornersTop);
    drawList->AddLine(ImVec2(windowPos.x, windowPos.y + headerHeight),
                      ImVec2(windowPos.x + windowSize.x, windowPos.y + headerHeight),
                      IM_COL32(60, 90, 140, 180), 1.0f);

    if (g_NinLogoTexture == 0) LoadNinLogoTexture();
    if (g_NinLogoTexture != 0) {
        const float aspect = (g_NinLogoHeight > 0)
            ? ((float)g_NinLogoWidth / (float)g_NinLogoHeight) : 2.5f;
        const float logoHeight = 92.0f;
        const float logoWidth = logoHeight * aspect;
        const float logoX = windowPos.x + (windowSize.x - logoWidth) * 0.5f;
        const float logoY = windowPos.y + headerHeight - logoHeight;
        drawList->PushClipRectFullScreen();
        drawList->AddImage((ImTextureID)(intptr_t)g_NinLogoTexture,
                           ImVec2(logoX, logoY), ImVec2(logoX + logoWidth, logoY + logoHeight),
                           ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
        drawList->PopClipRect();
    }

    drawList->AddRectFilled(closePos, ImVec2(closePos.x + 24.0f, closePos.y + 24.0f),
                            overClose ? IM_COL32(220, 50, 60, 200) : IM_COL32(50, 65, 88, 140), 6.0f);
    drawList->AddLine(ImVec2(closePos.x + 7.0f,  closePos.y + 7.0f),
                      ImVec2(closePos.x + 17.0f, closePos.y + 17.0f),
                      IM_COL32(255, 255, 255, 240), 2.0f);
    drawList->AddLine(ImVec2(closePos.x + 17.0f, closePos.y + 7.0f),
                      ImVec2(closePos.x + 7.0f,  closePos.y + 17.0f),
                      IM_COL32(255, 255, 255, 240), 2.0f);
}

inline void RenderMainModMenu(bool& menuOpen) {
    const float screenWidth = (ImGui::GetIO().DisplaySize.x > 0.0f)
        ? ImGui::GetIO().DisplaySize.x : (float)g_GlWidth;
    const float screenHeight = (ImGui::GetIO().DisplaySize.y > 0.0f)
        ? ImGui::GetIO().DisplaySize.y : (float)g_GlHeight;

    MenuTheme::g_height = (screenHeight > 0.0f) ? (screenHeight * 0.74f) : 500.0f;
    if (MenuTheme::g_height < 380.0f) MenuTheme::g_height = 380.0f;
    MenuTheme::g_width = (screenWidth > 460.0f) ? 440.0f : (screenWidth - 20.0f);
    if (MenuTheme::g_width < 260.0f) MenuTheme::g_width = 260.0f;

    const float maxX = (screenWidth > MenuTheme::g_width) ? (screenWidth - MenuTheme::g_width) : 0.0f;
    const float maxY = (screenHeight > MenuTheme::g_height) ? (screenHeight - MenuTheme::g_height) : 0.0f;
    MenuTheme::g_position.x = MenuTheme::Clamp(MenuTheme::g_position.x, 0.0f, maxX);
    MenuTheme::g_position.y = MenuTheme::Clamp(MenuTheme::g_position.y, 20.0f, maxY);

    ImGui::SetNextWindowPos(MenuTheme::g_position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(MenuTheme::g_width, MenuTheme::g_height), ImGuiCond_Always);

    MenuTheme::Push();

    if (ImGui::Begin("##NINMODS", &menuOpen,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {

        ImGui::PushItemWidth(-1.0f);

        constexpr float kHeaderHeight = 44.0f;
        const ImVec2 windowPos  = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const ImVec2 mouse      = ImGui::GetIO().MousePos;
        const ImVec2 closePos(windowPos.x + windowSize.x - 34.0f,
                              windowPos.y + (kHeaderHeight - 24.0f) * 0.5f);

        const bool overClose = (mouse.x >= closePos.x && mouse.x <= closePos.x + 24.0f &&
                                mouse.y >= closePos.y && mouse.y <= closePos.y + 24.0f);
        const bool overHeader = (mouse.y >= windowPos.y - 40.0f && mouse.y <= windowPos.y + kHeaderHeight &&
                                 mouse.x >= windowPos.x && mouse.x < closePos.x);
        const bool overBody = (mouse.x >= windowPos.x && mouse.x <= windowPos.x + windowSize.x &&
                               mouse.y >= windowPos.y && mouse.y <= windowPos.y + windowSize.y);

        if (ImGui::IsMouseClicked(0) && overClose) menuOpen = false;

        static bool draggingHeader = false;
        static bool scrollingBody = false;
        if (ImGui::IsMouseClicked(0)) {
            draggingHeader = overHeader;
            scrollingBody = overBody && !overHeader;
        }
        if (ImGui::IsMouseReleased(0)) {
            draggingHeader = false;
            scrollingBody = false;
        }

        if (draggingHeader && ImGui::IsMouseDragging(0, 1.0f)) {
            MenuTheme::g_position.x = MenuTheme::Clamp(MenuTheme::g_position.x + ImGui::GetIO().MouseDelta.x, 0.0f, maxX);
            MenuTheme::g_position.y = MenuTheme::Clamp(MenuTheme::g_position.y + ImGui::GetIO().MouseDelta.y, 20.0f, maxY);
        } else if (scrollingBody && ImGui::IsMouseDragging(0, 2.0f)) {
            ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        if (g_NinLogoTexture == 0) LoadNinLogoTexture();
        if (g_NinLogoTexture != 0) {
            const float bodyWidth = windowSize.x;
            const float bodyHeight = windowSize.y - kHeaderHeight;
            const float aspect = (g_NinLogoHeight > 0)
                ? ((float)g_NinLogoWidth / (float)g_NinLogoHeight) : 1.0f;
            const float logoWidth = bodyWidth * 0.88f;
            const float logoHeight = logoWidth / aspect;
            const float logoX = windowPos.x + (bodyWidth - logoWidth) * 0.5f;
            const float logoY = windowPos.y + kHeaderHeight + (bodyHeight - logoHeight) * 0.5f;

            drawList->PushClipRect(ImVec2(windowPos.x, windowPos.y + kHeaderHeight),
                                   ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y), true);
            drawList->AddImage((ImTextureID)(intptr_t)g_NinLogoTexture,
                               ImVec2(logoX, logoY), ImVec2(logoX + logoWidth, logoY + logoHeight),
                               ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 55));
            drawList->PopClipRect();
        }

        ImGui::SetCursorPosY(kHeaderHeight + 6.0f);

        static const std::string s_package = Dumper::TargetPackage();
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.00f, 1.00f), "IL2CPP LIVE DUMPER");
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.95f, 0.90f), "%s  (pid %d)",
                           s_package.c_str(), (int)getpid());
        if (Il2CppIsReady()) {
            ImGui::TextColored(ImVec4(0.75f, 0.80f, 0.85f, 0.90f),
                               "base 0x%lx   assemblies %zu",
                               (unsigned long)Il2CppGetBase(),
                               Il2CppGetAssemblyCount());
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.60f, 0.30f, 1.00f), "IL2CPP runtime initializing...");
        }
        ImGui::Separator();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.30f, 1.00f), "DUMP ENGINE");
        DrawDumpControls();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.00f, 1.00f), "LIVE INSPECTOR");
        DrawInspector();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.25f, 0.35f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.38f, 0.52f, 0.95f));
        if (ImGui::Button("HIDE", ImVec2(-1, 36.0f))) menuOpen = false;
        ImGui::PopStyleColor(2);

        DrawHeaderBar(drawList, windowPos, windowSize, closePos, overClose, kHeaderHeight);
        ImGui::PopItemWidth();
    }
    ImGui::End();

    MenuTheme::Pop();
}
