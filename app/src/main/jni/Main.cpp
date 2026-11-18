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
// UnityEngine.Input bridge. Reads mousePosition (Vector3) and touchCount/GetMouseButton.
// ---------------------------------------------------------------------------
struct UnityVector3 {
    float x;
    float y;
    float z;
};

namespace {

struct TouchBridge {
    void* touchCount     = nullptr;
    void* mousePosition  = nullptr;
    void* getMouseButton = nullptr;
    bool  resolved       = false;
};

TouchBridge& Touch() {
    static TouchBridge s_bridge;
    return s_bridge;
}

bool ResolveTouchBridge() {
    TouchBridge& bridge = Touch();
    if (bridge.resolved) return true;
    if (!g_Il2CppReady.load()) return false;

    // Attach current thread to IL2CPP runtime before querying symbols
    if (il2cpp_domain_get && il2cpp_thread_attach) {
        void* domain = il2cpp_domain_get();
        if (domain) il2cpp_thread_attach(domain);
    }

    static const char* kImages[] = {
        "UnityEngine.InputLegacyModule.dll",
        "UnityEngine.CoreModule.dll",
        "UnityEngine.dll",
    };

    for (const char* image : kImages) {
        if (!bridge.touchCount) {
            bridge.touchCount = Il2CppGetMethodOffset(image, "UnityEngine", "Input", "get_touchCount", 0);
        }
        if (!bridge.mousePosition) {
            bridge.mousePosition = Il2CppGetMethodOffset(image, "UnityEngine", "Input", "get_mousePosition", 0);
        }
        if (!bridge.getMouseButton) {
            bridge.getMouseButton = Il2CppGetMethodOffset(image, "UnityEngine", "Input", "GetMouseButton", 1);
        }
        if (bridge.mousePosition && bridge.touchCount) break;
    }

    if (bridge.mousePosition || bridge.touchCount) {
        bridge.resolved = true;
        Logger::Log("touch bridge resolved through il2cpp (mousePos=%p touchCount=%p)",
                    bridge.mousePosition, bridge.touchCount);
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

    static bool s_threadAttached = false;
    if (!s_threadAttached && il2cpp_domain_get && il2cpp_thread_attach) {
        void* domain = il2cpp_domain_get();
        if (domain) {
            il2cpp_thread_attach(domain);
            s_threadAttached = true;
        }
    }

    static bool s_touching = false;
    static float s_touchStart = 0.0f;

    TouchBridge& bridge = Touch();

    int count = 0;
    if (bridge.touchCount) {
        count = ((int (*)())bridge.touchCount)();
    }

    bool buttonDown = false;
    if (bridge.getMouseButton) {
        buttonDown = ((bool (*)(int))bridge.getMouseButton)(0);
    }

    if ((count > 0 && count <= 10) || buttonDown) {
        if (bridge.mousePosition) {
            const UnityVector3 pos = ((UnityVector3 (*)())bridge.mousePosition)();
            if (pos.x >= 0.0f && pos.x <= (float)g_GlWidth &&
                pos.y >= 0.0f && pos.y <= (float)g_GlHeight) {
                io.MousePos = ImVec2(pos.x, io.DisplaySize.y - pos.y);
                if (!s_touching) s_touchStart = ImGui::GetTime();
                io.MouseDown[0] = true;
                s_touching = true;
                return;
            }
        }
    }

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
    Logger::Log("watcher started (pid %d)", (int)getpid());

    // 1. Delayed start: Wait 4 seconds for Android activity, surface creation, and engine loader (OB55 pattern)
    sleep(4);

    // 2. Poll for libil2cpp.so to be fully mapped and initialized
    for (int tick = 0; tick < 180; ++tick) {
        if (!g_Il2CppReady.load()) {
            EnsureIl2CppReady();
        }

        // 3. Only install eglSwapBuffers hook AFTER runtime is fully ready and stable
        if (g_Il2CppReady.load() && !g_EglHooked.load()) {
            if (InstallSwapBuffersHook()) {
                g_EglHooked.store(true);
                Logger::Log("watcher: render hook and runtime both ready");
                return;
            }
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

    Logger::Init();
    Logger::Log("NINMODS loaded, pid %d", (int)getpid());
    std::thread(Watcher).detach();
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)vm;
    (void)reserved;
    NinModsConstructor();
    return JNI_VERSION_1_6;
}
