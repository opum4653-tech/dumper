// Zygisk injection lifecycle: waits for the Il2Cpp runtime, then unlocks live inspection.
#pragma once

#include <atomic>
#include <mutex>
#include <unistd.h>

#include "../../Unity/Il2Cpp.h"
#include "../../Protection/CrashLogger.h"
#include "../../Core/MemoryUtils.h"

extern uintptr_t il2cpp_base;

inline int g_GlWidth = 0;
inline int g_GlHeight = 0;
inline std::atomic<bool> g_Il2CppReady{false};
inline std::atomic<bool> g_EglHooked{false};
inline std::atomic<bool> g_AssemblyLoaded{false};
inline std::atomic<bool> g_UserInteracted{false};

inline void EnsureIl2CppReady() {
    static std::once_flag s_once;
    std::call_once(s_once, []() {
        Logger::Log("attaching to live Il2Cpp runtime");

        uintptr_t base = 0;
        for (int i = 0; i < 60 && base == 0; ++i) {
            base = GetModuleBase("libil2cpp.so");
            if (!base) usleep(50000);
        }
        if (!base) {
            Logger::Log("libil2cpp.so never mapped, aborting attach");
            return;
        }
        il2cpp_base = base;
        Logger::Log("libil2cpp.so at 0x%lx", (unsigned long)base);

        const void* assembly = nullptr;
        for (int i = 0; i < 60 && !assembly; ++i) {
            Il2CppAttach();
            assembly = Il2CppGetImageByName("Assembly-CSharp.dll");
            if (!assembly) usleep(50000);
        }

        if (assembly) {
            g_AssemblyLoaded.store(true);
            g_Il2CppReady.store(true);
            Logger::Log("Assembly-CSharp.dll ready, live inspection unlocked");
        } else {
            Logger::Log("Assembly-CSharp.dll never resolved, inspect disabled");
        }
    });
}
