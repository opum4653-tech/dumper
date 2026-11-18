#include <android/log.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <jni.h>
#include <xdl.h>
#include <KittyUtils.h>
#include <KittyMemory.h>
#include <Il2Cpp.h>
#include "MemoryPatcher.h"
#include "Core/MemoryUtils.h"
#include "Protection/CrashLogger.h"
#include "Struct/Core/DumperLifecycle.h"
#include "UI/FloatingIconUI.h"
#include "Struct/Menu/MenuRender.h"

static int currentTab = 0;
uintptr_t il2cpp_base = 0;
inline bool menuOpen = false, iconVisible = true;
std::mutex g_RenderMutex;

void MkbMot() {
    if (!menuOpen) { RenderFloatingIconUI(menuOpen, iconVisible); return; }
    iconVisible = true;
    RenderMainModMenu(menuOpen, currentTab);
}

inline EGLBoolean (*old_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface) = nullptr;
inline EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    if (!old_eglSwapBuffers) return EGL_TRUE;
    if (dpy == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) return old_eglSwapBuffers(dpy, surface);
    EGLContext currentContext = eglGetCurrentContext();
    if (currentContext == EGL_NO_CONTEXT) return old_eglSwapBuffers(dpy, surface);

    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) return old_eglSwapBuffers(dpy, surface);
    g_GlWidth = w; g_GlHeight = h;

    std::lock_guard<std::mutex> lock(g_RenderMutex);
    static EGLContext s_lastContext = EGL_NO_CONTEXT;
    if (s_lastContext != currentContext) {
        if (g_IsSetup && ImGui::GetCurrentContext() != nullptr) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext();
        }
        s_lastContext = currentContext;
        g_IsSetup = false;
        g_NinLogoTexture = 0;
    }

    if (!g_IsSetup) {
        Logger::Step("SetupImgui start");
        prevWidth = g_GlWidth; prevHeight = g_GlHeight;
        SetupImgui();
        g_IsSetup = true;
        Logger::Step("SetupImgui done");
        Logger::Log("ImGui setup complete on EGL context %p (%dx%d)", currentContext, w, h);
    }

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)g_GlWidth, (float)g_GlHeight);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(g_GlWidth, g_GlHeight);
    ImGui::NewFrame();

    static uintptr_t s_touchCount = 0, s_getTouch = 0;
    static bool s_resolved = false;
    static int s_frameDelay = 0;

    if (!s_resolved && ++s_frameDelay > 30) {
        if (!il2cpp_base) il2cpp_base = GetModuleBase("libil2cpp.so");
        if (il2cpp_base) {
            s_touchCount = (uintptr_t)Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Input"), OBFUSCATE("get_touchCount"), 0);
            if (!s_touchCount) s_touchCount = (uintptr_t)Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.InputLegacyModule.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Input"), OBFUSCATE("get_touchCount"), 0);
            if (!s_touchCount) s_touchCount = (uintptr_t)Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.CoreModule.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Input"), OBFUSCATE("get_touchCount"), 0);

            s_getTouch = (uintptr_t)Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Input"), OBFUSCATE("GetTouch"), 1);
            if (!s_getTouch) s_getTouch = (uintptr_t)Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.InputLegacyModule.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Input"), OBFUSCATE("GetTouch"), 1);
            if (!s_getTouch) s_getTouch = (uintptr_t)Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.CoreModule.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Input"), OBFUSCATE("GetTouch"), 1);

            if (s_touchCount && s_getTouch) {
                s_resolved = true;
                g_Il2CppReady.store(true);
                Logger::Log("Touch bridge: Unity Input methods resolved directly (Zero hook collisions)");
            }
        }
    }

    static bool isTouching = false;
    static float touchStartTime = 0.0f;

    if (s_resolved && s_touchCount && s_getTouch) {
        int touchCount = ((int (*)())(s_touchCount))();
        if (touchCount > 0 && touchCount <= 10) {
            UnityEngine_Touch_Fields touch = ((UnityEngine_Touch_Fields(*)(int))(s_getTouch))(0);
            float reverseY = io.DisplaySize.y - touch.m_Position.fields.y;
            io.MousePos = ImVec2(touch.m_Position.fields.x, reverseY);
            if (!isTouching) {
                touchStartTime = ImGui::GetTime();
            }
            io.MouseDown[0] = true;
            isTouching = true;
        } else {
            if (isTouching && (ImGui::GetTime() - touchStartTime < 0.06f)) {
                io.MouseDown[0] = true;
            } else {
                io.MouseDown[0] = false;
                isTouching = false;
            }
        }
    }

    MkbMot();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return old_eglSwapBuffers(dpy, surface);
}

inline bool StartGUI() {
    void *p = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!p) { void *h = xdl_open("libEGL.so", 0); if (h) { p = xdl_sym(h, "eglSwapBuffers", nullptr); xdl_close(h); } }
    if (!p) p = DobbySymbolResolver("/system/lib64/libEGL.so", "eglSwapBuffers");
    if (!p) p = DobbySymbolResolver("/system/lib/libEGL.so", "eglSwapBuffers");
    if (p) {
        DobbyHook(p, (void *)hook_eglSwapBuffers, (void **)&old_eglSwapBuffers);
        Logger::Log("Hooked eglSwapBuffers at %p", p);
        return true;
    }
    return false;
}

void mod_watcher() {
    Logger::Log("mod_watcher: Starting internal game lifecycle watcher...");
    
    // Persistent polling up to 120 seconds for EGL graphics and Il2Cpp base
    for (int i = 0; i < 120; i++) {
        if (!g_EglHooked.load() && StartGUI()) {
            g_EglHooked.store(true);
            Logger::Log("mod_watcher: eglSwapBuffers successfully hooked!");
        }

        if (!il2cpp_base) {
            il2cpp_base = GetModuleBase("libil2cpp.so");
            if (il2cpp_base) {
                Logger::Log("mod_watcher: libil2cpp.so base detected at 0x%lx", (unsigned long)il2cpp_base);
            }
        }

        if (g_EglHooked.load() && il2cpp_base) {
            Logger::Log("mod_watcher: Both EGL and Il2Cpp ready. Watcher cycle complete.");
            break;
        }

        sleep(1);
    }
}

// Internal Game APK Injection Constructor (Triggered automatically by OS linker on dlopen / DT_NEEDED)
__attribute__((constructor)) void NinModsInternalEntry() {
    static std::atomic<bool> s_initialized{false};
    if (s_initialized.exchange(true)) return;

    Logger::InstallCrashHandler();
    Logger::Log("NinMods: Injected directly inside game process. Starting watcher thread...");
    std::thread(mod_watcher).detach();
}

extern "C" JNIEXPORT void JNICALL Java_com_ninmods_MenuBridge_requestMenu(JNIEnv*, jclass) {
    g_UserInteracted.store(true);
}
extern "C" void RequestModMenu() {
    g_UserInteracted.store(true);
}

// JNI_OnLoad (Triggered when loaded via System.loadLibrary("NINMODS") in Smali)
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    jvm = vm;
    NinModsInternalEntry();
    return JNI_VERSION_1_6;
}