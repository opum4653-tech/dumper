// Main.cpp - NINMODS in-memory Il2Cpp dumper.
// Target: Android (arm64-v8a / armeabi-v7a), NDK r21e, C++17.
// Entry points: JNI_OnLoad (System.loadLibrary) and a DT_NEEDED constructor.

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
#include "Struct/Menu/MenuRender.h"
#include "UI/FloatingIconUI.h"

static int currentTab = 0;
uintptr_t il2cpp_base = 0;
inline bool menuOpen = false;
inline bool iconVisible = true;
std::mutex g_RenderMutex;

static bool g_IsSetup = false;

// ---------------------------------------------------------------------------
// UnityEngine.Input bridge - resolved through il2cpp, no hook collisions.
// Layout matches UnityEngine.Touch as returned by Input.GetTouch(int).
// ---------------------------------------------------------------------------
struct UnityTouch {
    int32_t m_FingerId;
    float   m_Position_x;   // Vector2
    float   m_Position_y;
    float   m_RawPosition_x;
    float   m_RawPosition_y;
    float   m_PositionDelta_x;
    float   m_PositionDelta_y;
    float   m_TimeDelta;
    int32_t m_TapCount;
    int32_t m_Phase;
    int32_t m_Pressure;
    int32_t m_MaximumPossiblePressure;
    int32_t m_Type;
    float   m_AltitudeAngle;
    float   m_AzimuthAngle;
    float   m_Radius;
    float   m_RadiusVariance;
};

static void InitImGui(EGLDisplay dpy, EGLSurface surface) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui_ImplOpenGL3_Init("#version 300 es");
    ImGui_ImplAndroid_Init(nullptr);

    Logger::Log("ImGui context ready (%dx%d)", g_GlWidth, g_GlHeight);
    (void)dpy;
    (void)surface;
}

static void ShutdownImGui() {
    if (ImGui::GetCurrentContext() == nullptr) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
}

static void RenderFrame() {
    if (!menuOpen) {
        RenderFloatingIconUI(menuOpen, iconVisible);
        return;
    }
    iconVisible = true;
    RenderMainModMenu(menuOpen, currentTab);
}

static EGLBoolean (*old_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;

static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    if (!old_eglSwapBuffers) return EGL_TRUE;
    if (dpy == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) return old_eglSwapBuffers(dpy, surface);

    EGLContext ctx = eglGetCurrentContext();
    if (ctx == EGL_NO_CONTEXT) return old_eglSwapBuffers(dpy, surface);

    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) return old_eglSwapBuffers(dpy, surface);
    g_GlWidth = w;
    g_GlHeight = h;

    std::lock_guard<std::mutex> lock(g_RenderMutex);

    static EGLContext s_lastContext = EGL_NO_CONTEXT;
    if (s_lastContext != ctx) {
        ShutdownImGui();
        s_lastContext = ctx;
        g_IsSetup = false;
        g_NinLogoTexture = 0;   // texture belongs to the old context
    }

    if (!g_IsSetup) {
        InitImGui(dpy, surface);
        g_IsSetup = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(w, h);
    ImGui::NewFrame();

    // Touch: resolve Input.get_touchCount / Input.GetTouch once libil2cpp is up.
    static bool s_resolved = false;
    static int s_frameDelay = 0;
    static void* s_touchCount = nullptr;
    static void* s_getTouch = nullptr;

    if (!s_resolved && ++s_frameDelay > 30) {
        if (!il2cpp_base) il2cpp_base = GetModuleBase("libil2cpp.so");
        if (il2cpp_base) {
            static const char* kImages[] = {
                "UnityEngine.dll",
                "UnityEngine.InputLegacyModule.dll",
                "UnityEngine.CoreModule.dll"
            };
            for (const char* img : kImages) {
                if (!s_touchCount) {
                    s_touchCount = Il2CppGetMethodOffset(img, "UnityEngine", "Input", "get_touchCount", 0);
                }
                if (!s_getTouch) {
                    s_getTouch = Il2CppGetMethodOffset(img, "UnityEngine", "Input", "GetTouch", 1);
                }
                if (s_touchCount && s_getTouch) break;
            }
            if (s_touchCount && s_getTouch) {
                s_resolved = true;
                g_Il2CppReady.store(true);
                Logger::Log("Touch bridge resolved via il2cpp (no hook collision)");
            }
        }
    }

    static bool s_touching = false;
    static float s_touchStart = 0.0f;

    if (s_resolved) {
        int count = ((int (*)())(s_touchCount))();
        if (count > 0 && count <= 10) {
            UnityTouch t = ((UnityTouch(*)(int))(s_getTouch))(0);
            io.MousePos = ImVec2(t.m_Position_x, io.DisplaySize.y - t.m_Position_y);
            if (!s_touching) s_touchStart = ImGui::GetTime();
            io.MouseDown[0] = true;
            s_touching = true;
        } else if (s_touching && (ImGui::GetTime() - s_touchStart) < 0.06f) {
            io.MouseDown[0] = true;          // debounce the release frame
        } else {
            io.MouseDown[0] = false;
            s_touching = false;
        }
    }

    RenderFrame();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return old_eglSwapBuffers(dpy, surface);
}

static bool InstallEglHook() {
    void* p = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!p) p = DobbySymbolResolver("/system/lib64/libEGL.so", "eglSwapBuffers");
    if (!p) p = DobbySymbolResolver("/system/lib/libEGL.so", "eglSwapBuffers");
    if (!p) return false;

    if (DobbyHook(p, (void*)hook_eglSwapBuffers, (void**)&old_eglSwapBuffers) != 0) return false;

    Logger::Log("eglSwapBuffers hooked at %p", p);
    return true;
}

static void ModWatcher() {
    Logger::Log("watcher: waiting for EGL surface and libil2cpp.so");

    for (int i = 0; i < 120; ++i) {
        if (!g_EglHooked.load() && InstallEglHook()) {
            g_EglHooked.store(true);
            Logger::Log("watcher: eglSwapBuffers hooked");
        }

        if (!il2cpp_base) {
            il2cpp_base = GetModuleBase("libil2cpp.so");
            if (il2cpp_base) {
                Logger::Log("watcher: libil2cpp.so at 0x%lx", (unsigned long)il2cpp_base);
            }
        }

        if (g_EglHooked.load() && il2cpp_base) {
            Logger::Log("watcher: both ready, polling finished");
            return;
        }
        sleep(1);
    }
    Logger::Log("watcher: timed out after 120s");
}

__attribute__((constructor)) static void NinModsConstructor() {
    static std::atomic<bool> s_init{false};
    if (s_init.exchange(true)) return;

    Logger::InstallCrashHandler();
    Logger::Log("NINMODS loaded inside game process, starting watcher");
    std::thread(ModWatcher).detach();
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)vm;
    (void)reserved;
    NinModsConstructor();
    return JNI_VERSION_1_6;
}
