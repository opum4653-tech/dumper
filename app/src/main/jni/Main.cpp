// Main.cpp - NINMODS in-memory Il2Cpp dumper.
//
// Target: Android arm64-v8a / armeabi-v7a, NDK r21e, C++17.
//
// The overlay renders inside the game's own EGL context by hooking
// eglSwapBuffers. Touch input is read through UnityEngine.Input rather than by
// hooking the input pipeline, so nothing in the game's input path is modified.

#include <android/log.h>
#include <unistd.h>
#include <jni.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <atomic>
#include <mutex>
#include <thread>

#include <dobby.h>
#include <Il2Cpp.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_android.h"

#include "Core/MemoryUtils.h"
#include "Protection/CrashLogger.h"
#include "Struct/Core/DumperLifecycle.h"
#include "Struct/Dumper/Il2CppDumper.h"
#include "Struct/Menu/MenuRender.h"
#include "UI/FloatingIconUI.h"

static int g_currentTab = 0;
static bool g_menuOpen = false;
static bool g_iconVisible = true;
static bool g_imGuiReady = false;

static std::mutex g_renderMutex;

// ---------------------------------------------------------------------------
// UnityEngine.Input bridge. Layout matches UnityEngine.Touch as returned by
// Input.GetTouch(int); only the fields the overlay reads are declared.
// ---------------------------------------------------------------------------
struct UnityTouch {
    int32_t m_FingerId;
    float   m_Position_x;
    float   m_Position_y;
    float   m_RawPosition_x;
    float   m_RawPosition_y;
    float   m_PositionDelta_x;
    float   m_PositionDelta_y;
    float   m_TimeDelta;
    int32_t m_TapCount;
    int32_t m_Phase;
    float   m_Pressure;
    float   m_MaximumPossiblePressure;
    int32_t m_Type;
    float   m_AltitudeAngle;
    float   m_AzimuthAngle;
    float   m_Radius;
    float   m_RadiusVariance;
};

namespace {

struct TouchBridge {
    void* touchCount = nullptr;
    void* getTouch   = nullptr;
    bool  resolved   = false;
};

TouchBridge& Touch() {
    static TouchBridge s_bridge;
    return s_bridge;
}

bool ResolveTouchBridge() {
    TouchBridge& bridge = Touch();
    if (bridge.resolved) return true;
    if (!g_Il2CppReady.load()) return false;

    static const char* kImages[] = {
        "UnityEngine.InputLegacyModule.dll",
        "UnityEngine.CoreModule.dll",
        "UnityEngine.dll",
    };

    for (const char* image : kImages) {
        if (!bridge.touchCount) {
            bridge.touchCount = Il2CppGetMethodOffset(image, "UnityEngine", "Input", "get_touchCount", 0);
        }
        if (!bridge.getTouch) {
            bridge.getTouch = Il2CppGetMethodOffset(image, "UnityEngine", "Input", "GetTouch", 1);
        }
        if (bridge.touchCount && bridge.getTouch) break;
    }

    if (bridge.touchCount && bridge.getTouch) {
        bridge.resolved = true;
        Logger::Log("touch bridge resolved through il2cpp");
        return true;
    }
    return false;
}

void InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui_ImplOpenGL3_Init("#version 300 es");
    ImGui_ImplAndroid_Init(nullptr);

    Logger::Log("imgui ready (%dx%d)", g_GlWidth, g_GlHeight);
}

void ShutdownImGui() {
    if (ImGui::GetCurrentContext() == nullptr) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    g_imGuiReady = false;
}

void PumpTouch(ImGuiIO& io) {
    if (!Touch().resolved) {
        static int s_retryFrame = 0;
        if (++s_retryFrame % 60 == 0) {
            ResolveTouchBridge();
        }
        if (!Touch().resolved) return;
    }
    if (!Touch().touchCount || !Touch().getTouch) return;

    static bool s_touching = false;
    static float s_touchStart = 0.0f;

    const int count = ((int (*)())Touch().touchCount)();
    if (count > 0 && count <= 10) {
        const UnityTouch touch = ((UnityTouch (*)(int))Touch().getTouch)(0);
        io.MousePos = ImVec2(touch.m_Position_x, io.DisplaySize.y - touch.m_Position_y);
        if (!s_touching) s_touchStart = ImGui::GetTime();
        io.MouseDown[0] = true;
        s_touching = true;
        return;
    }

    // A one-frame release debounce keeps a tap from being read as a click plus
    // a drag release.
    if (s_touching && (ImGui::GetTime() - s_touchStart) < 0.06f) {
        io.MouseDown[0] = true;
        return;
    }
    io.MouseDown[0] = false;
    s_touching = false;
}

void RenderFrame() {
    if (!g_menuOpen) {
        RenderFloatingIconUI(g_menuOpen, g_iconVisible);
        return;
    }
    g_iconVisible = true;
    RenderMainModMenu(g_menuOpen, g_currentTab);
}

EGLBoolean (*g_originalSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;

EGLBoolean HookedSwapBuffers(EGLDisplay display, EGLSurface surface) {
    if (!g_originalSwapBuffers) return EGL_TRUE;
    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) {
        return g_originalSwapBuffers(display, surface);
    }

    EGLContext context = eglGetCurrentContext();
    if (context == EGL_NO_CONTEXT) return g_originalSwapBuffers(display, surface);

    EGLint width = 0, height = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &height);
    if (width <= 0 || height <= 0) return g_originalSwapBuffers(display, surface);

    g_GlWidth = width;
    g_GlHeight = height;

    std::lock_guard<std::mutex> lock(g_renderMutex);

    // A context switch invalidates every GL object, so the ImGui backend and the
    // logo texture are rebuilt for the new context.
    static EGLContext s_lastContext = EGL_NO_CONTEXT;
    if (s_lastContext != context) {
        ShutdownImGui();
        s_lastContext = context;
        g_NinLogoTexture = 0;
    }

    if (!g_imGuiReady) {
        InitImGui();
        g_imGuiReady = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(width, height);
    ImGui::NewFrame();

    PumpTouch(io);

    RenderFrame();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return g_originalSwapBuffers(display, surface);
}

bool InstallSwapBuffersHook() {
    void* target = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!target) target = DobbySymbolResolver("/system/lib64/libEGL.so", "eglSwapBuffers");
    if (!target) target = DobbySymbolResolver("/system/lib/libEGL.so", "eglSwapBuffers");
    if (!target) return false;

    if (DobbyHook(target, (void*)HookedSwapBuffers, (void**)&g_originalSwapBuffers) != 0) return false;

    Logger::Log("eglSwapBuffers hooked at %p", target);
    return true;
}

void Watcher() {
    Logger::Log("watcher started");

    for (int tick = 0; tick < 180; ++tick) {
        if (!g_EglHooked.load() && InstallSwapBuffersHook()) {
            g_EglHooked.store(true);
        }

        if (!g_Il2CppReady.load()) {
            EnsureIl2CppReady();
        }

        if (g_EglHooked.load() && g_Il2CppReady.load()) {
            Logger::Log("watcher: render hook and runtime both ready");
            return;
        }
        sleep(1);
    }
    Logger::Log("watcher: gave up after 180s (egl=%d il2cpp=%d)",
                (int)g_EglHooked.load(), (int)g_Il2CppReady.load());
}

} // namespace

__attribute__((constructor)) static void NinModsConstructor() {
    static std::atomic<bool> s_started{false};
    if (s_started.exchange(true)) return;

    Logger::InstallCrashHandler();
    Logger::Log("NINMODS loaded, pid %d", (int)getpid());
    std::thread(Watcher).detach();
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)vm;
    (void)reserved;
    NinModsConstructor();
    return JNI_VERSION_1_6;
}
