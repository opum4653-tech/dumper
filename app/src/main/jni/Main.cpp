// Main.cpp - NINMODS in-memory Il2Cpp dumper.
//
// Target: Android arm64-v8a / armeabi-v7a, NDK r21e, C++17.
//
// The overlay renders inside the game's own EGL context by hooking
// eglSwapBuffers. Touch input is safely read through UnityEngine.Input
// with runtime invoke protection and multi-finger gestures.

#include <android/log.h>
#include <unistd.h>
#include <jni.h>
#include <pthread.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <atomic>
#include <mutex>

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

static bool g_menuOpen = true;
static bool g_iconVisible = true;
static bool g_imGuiReady = false;

static std::mutex g_renderMutex;

// ---------------------------------------------------------------------------
// UnityEngine.Input bridge. Reads mousePosition (Vector3) and touchCount/GetMouseButton/GetTouch.
// ---------------------------------------------------------------------------
struct UnityVector3 {
    float x;
    float y;
    float z;
};

struct UnityTouch {
    int   fingerId;           // 0x00
    float posX;               // 0x04
    float posY;               // 0x08
    float rawPosX;            // 0x0C
    float rawPosY;            // 0x10
    float deltaX;             // 0x14
    float deltaY;             // 0x18
    float deltaTime;          // 0x1C
    int   tapCount;           // 0x20
    int   phase;              // 0x24 (0=Began, 1=Moved, 2=Stationary, 3=Ended, 4=Canceled)
};

namespace {

struct TouchBridge {
    const void* methodTouchCount     = nullptr;
    const void* methodMousePosition  = nullptr;
    const void* methodGetMouseButton = nullptr;
    const void* methodGetTouch       = nullptr;
    void*       codeTouchCount       = nullptr;
    void*       codeMousePosition    = nullptr;
    void*       codeGetMouseButton   = nullptr;
    void*       codeGetTouch         = nullptr;
    bool        resolved             = false;
};

TouchBridge& Touch() {
    static TouchBridge s_bridge;
    return s_bridge;
}

bool ResolveTouchBridge() {
    TouchBridge& bridge = Touch();
    if (bridge.resolved) return true;
    if (!g_Il2CppReady.load()) return false;

    Il2CppThreadAttach();

    const size_t asmCount = Il2CppGetAssemblyCount();
    const void* foundKlass = nullptr;
    const void* foundImage = nullptr;

    // Scan all loaded assemblies dynamically for class UnityEngine.Input
    for (size_t i = 0; i < asmCount; ++i) {
        const void* asmPtr = Il2CppGetAssembly(i);
        if (!asmPtr) continue;
        const void* image = Il2CppAssemblyGetImage(asmPtr);
        if (!image) continue;

        const void* klass = Il2CppGetClassType(image, "UnityEngine", "Input");
        if (klass) {
            foundKlass = klass;
            foundImage = image;
            break;
        }
    }

    // Fallback: check standard image names explicitly
    if (!foundKlass) {
        static const char* kImages[] = {
            "UnityEngine.InputLegacyModule.dll",
            "UnityEngine.InputLegacyModule",
            "UnityEngine.CoreModule.dll",
            "UnityEngine.CoreModule",
            "UnityEngine.dll",
            "UnityEngine"
        };
        for (const char* imgName : kImages) {
            const void* img = Il2CppGetImageByName(imgName);
            if (!img) continue;
            const void* klass = Il2CppGetClassType(img, "UnityEngine", "Input");
            if (klass) {
                foundKlass = klass;
                foundImage = img;
                break;
            }
        }
    }

    if (!foundKlass) {
        static int s_failLog = 0;
        if (++s_failLog % 60 == 1) {
            Logger::Log("ResolveTouchBridge: UnityEngine.Input class not found (assemblies: %zu)", asmCount);
        }
        return false;
    }

    if (il2cpp_class_get_method_from_name) {
        bridge.methodTouchCount     = il2cpp_class_get_method_from_name((void*)foundKlass, "get_touchCount", 0);
        bridge.methodMousePosition  = il2cpp_class_get_method_from_name((void*)foundKlass, "get_mousePosition", 0);
        bridge.methodGetMouseButton = il2cpp_class_get_method_from_name((void*)foundKlass, "GetMouseButton", 1);
        bridge.methodGetTouch       = il2cpp_class_get_method_from_name((void*)foundKlass, "GetTouch", 1);
    }

    bridge.codeTouchCount     = bridge.methodTouchCount ? Il2CppMethodGetPointer(bridge.methodTouchCount) : nullptr;
    bridge.codeMousePosition  = bridge.methodMousePosition ? Il2CppMethodGetPointer(bridge.methodMousePosition) : nullptr;
    bridge.codeGetMouseButton = bridge.methodGetMouseButton ? Il2CppMethodGetPointer(bridge.methodGetMouseButton) : nullptr;
    bridge.codeGetTouch       = bridge.methodGetTouch ? Il2CppMethodGetPointer(bridge.methodGetTouch) : nullptr;

    if (bridge.methodTouchCount || bridge.methodMousePosition || bridge.codeTouchCount) {
        bridge.resolved = true;
        const char* imgName = Il2CppImageGetName(foundImage);
        Logger::Log("touch bridge resolved in [%s]: class=%p touchCount=%p mousePos=%p getTouch=%p getMouseBtn=%p",
                    imgName ? imgName : "unknown",
                    foundKlass,
                    bridge.methodTouchCount,
                    bridge.methodMousePosition,
                    bridge.methodGetTouch,
                    bridge.methodGetMouseButton);
        return true;
    }
    return false;
}

int GetSafeTouchCount(const TouchBridge& bridge) {
    if (bridge.methodTouchCount && il2cpp_runtime_invoke) {
        void* exc = nullptr;
        void* res = il2cpp_runtime_invoke(bridge.methodTouchCount, nullptr, nullptr, &exc);
        if (!exc && res) {
            void* unboxed = il2cpp_object_unbox ? il2cpp_object_unbox(res)
                                                : (void*)((uintptr_t)res + sizeof(void*) * 2);
            if (unboxed) return *(int*)unboxed;
        }
    }
    if (bridge.codeTouchCount) {
        typedef int (*FnTouchCount)();
        return ((FnTouchCount)bridge.codeTouchCount)();
    }
    return 0;
}

bool GetSafeMouseButton(const TouchBridge& bridge, int button) {
    if (bridge.methodGetMouseButton && il2cpp_runtime_invoke) {
        void* exc = nullptr;
        void* args[1] = { &button };
        void* res = il2cpp_runtime_invoke(bridge.methodGetMouseButton, nullptr, args, &exc);
        if (!exc && res) {
            void* unboxed = il2cpp_object_unbox ? il2cpp_object_unbox(res)
                                                : (void*)((uintptr_t)res + sizeof(void*) * 2);
            if (unboxed) return *(bool*)unboxed;
        }
    }
    if (bridge.codeGetMouseButton) {
        typedef bool (*FnGetMouseButton)(int);
        return ((FnGetMouseButton)bridge.codeGetMouseButton)(button);
    }
    return false;
}

bool GetSafeMousePosition(const TouchBridge& bridge, UnityVector3& outPos) {
    if (bridge.methodMousePosition && il2cpp_runtime_invoke) {
        void* exc = nullptr;
        void* res = il2cpp_runtime_invoke(bridge.methodMousePosition, nullptr, nullptr, &exc);
        if (!exc && res) {
            void* unboxed = il2cpp_object_unbox ? il2cpp_object_unbox(res)
                                                : (void*)((uintptr_t)res + sizeof(void*) * 2);
            if (unboxed) {
                outPos = *(UnityVector3*)unboxed;
                return true;
            }
        }
    }
    return false;
}

bool GetSafeTouch(const TouchBridge& bridge, int index, UnityTouch& outTouch) {
    if (bridge.methodGetTouch && il2cpp_runtime_invoke) {
        void* exc = nullptr;
        void* args[1] = { &index };
        void* res = il2cpp_runtime_invoke(bridge.methodGetTouch, nullptr, args, &exc);
        if (!exc && res) {
            void* unboxed = il2cpp_object_unbox ? il2cpp_object_unbox(res)
                                                : (void*)((uintptr_t)res + sizeof(void*) * 2);
            if (unboxed) {
                outTouch = *(UnityTouch*)unboxed;
                return true;
            }
        }
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
        if (++s_retryFrame % 30 == 1) {
            ResolveTouchBridge();
        }
        if (!Touch().resolved) return;
    }

    static bool s_threadAttached = false;
    if (!s_threadAttached) {
        Il2CppThreadAttach();
        s_threadAttached = true;
    }

    TouchBridge& bridge = Touch();
    int count = GetSafeTouchCount(bridge);
    bool buttonDown = GetSafeMouseButton(bridge, 0);

    // Multi-touch Gesture (OB55 pattern): 3-finger or 4-finger tap triggers dump automatically!
    static double s_lastMultiTap = 0.0;
    double nowTime = ImGui::GetTime();
    if (count >= 3 && (nowTime - s_lastMultiTap > 3.0)) {
        s_lastMultiTap = nowTime;
        Logger::Log("Multi-finger gesture (count=%d) -> triggering full dump!", count);
        Dumper::TriggerFullDump();
    }

    bool hasPosition = false;
    float posX = 0.0f;
    float posY = 0.0f;
    bool isDown = false;

    // Route A: Direct Unity Touch struct when screen is touched
    if (count > 0 && bridge.methodGetTouch) {
        UnityTouch touch{};
        if (GetSafeTouch(bridge, 0, touch)) {
            posX = touch.posX;
            posY = touch.posY;
            hasPosition = true;
            // Phase: 0 = Began, 1 = Moved, 2 = Stationary
            isDown = (touch.phase == 0 || touch.phase == 1 || touch.phase == 2);
        }
    }

    // Route B: mousePosition fallback (mouse simulation from touch)
    if (!hasPosition) {
        UnityVector3 mousePos{};
        if (GetSafeMousePosition(bridge, mousePos)) {
            if (mousePos.x > 0.0f || mousePos.y > 0.0f) {
                posX = mousePos.x;
                posY = mousePos.y;
                hasPosition = true;
            }
        }
        if (count > 0 || buttonDown) {
            isDown = true;
        }
    }

    if (hasPosition && io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f) {
        if (posX < 0.0f) posX = 0.0f;
        if (posX > io.DisplaySize.x) posX = io.DisplaySize.x;
        if (posY < 0.0f) posY = 0.0f;
        if (posY > io.DisplaySize.y) posY = io.DisplaySize.y;

        // Invert Y: Unity origin is bottom-left, ImGui origin is top-left
        io.MousePos = ImVec2(posX, io.DisplaySize.y - posY);
    }

    io.MouseDown[0] = (isDown && hasPosition);
}

void RenderFrame() {
    if (!g_menuOpen) {
        RenderFloatingIconUI(g_menuOpen, g_iconVisible);
        return;
    }
    g_iconVisible = true;
    RenderMainModMenu(g_menuOpen);
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

    Logger::Log("eglSwapBuffers hooked successfully at %p", target);
    return true;
}

void* Watcher(void* arg) {
    (void)arg;
    Logger::Init();
    Logger::Log("NINMODS Watcher started (pid %d)", (int)getpid());

    // 1. Initial grace period: wait 6 seconds for Activity startup and initial load
    sleep(6);

    // 2. Poll for libil2cpp.so to be fully mapped in /proc/self/maps naturally (OB55 pattern)
    uintptr_t il2cpp_base = 0;
    for (int i = 0; i < 30 && !il2cpp_base; ++i) {
        il2cpp_base = memutils::GetModuleBase("libil2cpp.so");
        if (!il2cpp_base) sleep(2);
    }

    if (!il2cpp_base) {
        Logger::Log("watcher: libil2cpp.so not found after 66s; aborting");
        return nullptr;
    }
    Logger::Log("watcher: libil2cpp.so mapped at 0x%lx", (unsigned long)il2cpp_base);

    // 3. Wait for Unity to call il2cpp_init() and load assemblies
    for (int tick = 0; tick < 120; ++tick) {
        if (!g_Il2CppReady.load()) {
            EnsureIl2CppReady();
        }

        // 4. Install eglSwapBuffers hook only when runtime and assemblies are verified ready
        if (g_Il2CppReady.load() && !g_EglHooked.load()) {
            if (InstallSwapBuffersHook()) {
                g_EglHooked.store(true);
                Logger::Log("watcher: render hook installed, overlay active");
                return nullptr;
            }
        }
        sleep(1);
    }

    Logger::Log("watcher: timed out (egl=%d il2cpp=%d)",
                (int)g_EglHooked.load(), (int)g_Il2CppReady.load());
    return nullptr;
}

} // namespace

JavaVM* g_jvm = nullptr;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    g_jvm = vm;

    static std::atomic<bool> s_started{false};
    if (!s_started.exchange(true)) {
        pthread_t t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&t, &attr, Watcher, nullptr);
        pthread_attr_destroy(&attr);
    }
    return JNI_VERSION_1_6;
}

// Optional JNI touch bridge: if invoked from Java onTouchEvent
extern "C" JNIEXPORT void JNICALL
Java_com_ninmods_dumper_MenuService_onTouch(JNIEnv* env, jclass clazz, jint action, jfloat x, jfloat y) {
    (void)env;
    (void)clazz;
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2(x, y);
    if (action == 0) io.MouseDown[0] = true;
    else if (action == 1) io.MouseDown[0] = false;
}
