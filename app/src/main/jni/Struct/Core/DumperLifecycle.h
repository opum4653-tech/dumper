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

inline void EnsureIl2CppReady() {
    static std::once_flag s_once;
    std::call_once(s_once, []() {
        Logger::Log("attaching to the il2cpp runtime");

        // Attach resolves the module base itself, so a single call is enough;
        // repeat it while the loader has not mapped the module yet.
        bool ready = false;
        for (int attempt = 0; attempt < 120 && !ready; ++attempt) {
            Il2CppAttach();
            MetadataParser::Located loc;
            ready = Il2CppIsReady() || MetadataParser::Locate(loc);
            if (!ready) usleep(100 * 1000);
        }

        if (!ready) {
            Logger::Log("il2cpp never became ready; dump and inspect stay disabled");
            return;
        }

        SafeMemory::Refresh();

        g_Il2CppReady.store(true);
        Logger::Log("il2cpp ready: base 0x%lx, assemblies %zu, enumeration %s",
                    (unsigned long)Il2CppGetBase(),
                    Il2CppGetAssemblyCount(),
                    Il2CppEnumerationMode());

        const char* missing = Il2CppMissingApis();
        if (missing && missing[0]) {
            Logger::Log("exports not present on this build:\n%s", missing);
        }
    });
}
