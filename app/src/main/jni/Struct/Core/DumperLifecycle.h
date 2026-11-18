#pragma once
#include <atomic>
#include <mutex>
#include <unistd.h>
#include "../../Unity/Il2Cpp.h"
#include "../../Protection/CrashLogger.h"
#include "../../KittyMemory/KittyMemory.h"
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
        Logger::Log("EnsureIl2CppReady: Attaching to live Il2Cpp runtime...");
        uintptr_t base = 0;
        int tries = 0;
        while (base == 0 && tries < 60) {
            base = GetModuleBase("libil2cpp.so");
            if (!base) { usleep(50000); tries++; }
        }
        if (!base) {
            Logger::Log("EnsureIl2CppReady: Failed to find libil2cpp.so base");
            return;
        }
        il2cpp_base = base;
        Logger::Log("libil2cpp.so found at 0x%lx", base);

        const void* assembly = nullptr;
        tries = 0;
        while (!assembly && tries < 60) {
            Il2CppAttach();
            assembly = Il2CppGetImageByName("Assembly-CSharp.dll");
            if (!assembly) { usleep(50000); tries++; }
        }

        if (assembly) {
            g_AssemblyLoaded.store(true);
            g_Il2CppReady.store(true);
            Logger::Log("EnsureIl2CppReady: Assembly-CSharp.dll ready. Il2Cpp live inspection unlocked.");
        } else {
            Logger::Log("EnsureIl2CppReady: Assembly-CSharp.dll timeout.");
        }
    });
}
