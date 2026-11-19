// DumperLifecycle.h - waits for the runtime, then unlocks the overlay.
//
// The library can be loaded before libil2cpp.so is even mapped (a constructor
// runs early). Everything here is a bounded poll: it gives up cleanly and says
// so in the log instead of spinning forever or assuming success.

#pragma once

#include <atomic>
#include <mutex>
#include <unistd.h>

#include "../../Unity/Il2Cpp.h"
#include "../../Protection/CrashLogger.h"
#include "../../Core/SafeMemory.h"
#include "../../Core/MetadataParser.h"

inline int g_GlWidth = 0;
inline int g_GlHeight = 0;
inline std::atomic<bool> g_Il2CppReady{false};
inline std::atomic<bool> g_EglHooked{false};
inline std::atomic<bool> g_UserInteracted{false};

inline bool TryInitIl2Cpp() {
    if (g_Il2CppReady.load(std::memory_order_acquire)) return true;

    static std::mutex s_initMutex;
    std::unique_lock<std::mutex> lock(s_initMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return g_Il2CppReady.load(std::memory_order_acquire);
    }

    if (g_Il2CppReady.load(std::memory_order_relaxed)) return true;

    Il2CppAttach();
    MetadataParser::Located loc;
    const size_t asmCount = Il2CppGetAssemblyCount();
    const bool apiReady = Il2CppIsReady() && (asmCount > 0);
    const bool metadataReady = !apiReady && MetadataParser::Locate(loc);
    if (!apiReady && !metadataReady) {
        return false;
    }

    SafeMemory::Refresh();
    g_Il2CppReady.store(true, std::memory_order_release);
    Logger::Log("ready: api=%d metadata=%d base 0x%lx",
                (int)apiReady, (int)metadataReady,
                (unsigned long)Il2CppGetBase());

    const char* missing = Il2CppMissingApis();
    if (missing && missing[0]) {
        Logger::Log("exports not present on this build:\n%s", missing);
    }
    return true;
}

inline void EnsureIl2CppReady() {
    if (g_Il2CppReady.load(std::memory_order_acquire)) return;
    TryInitIl2Cpp();
}
