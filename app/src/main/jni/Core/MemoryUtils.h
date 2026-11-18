#ifndef MEMORY_UTILS_H
#define MEMORY_UTILS_H

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <algorithm>
#include "../KittyMemory/MemoryPatch.h"
#include "../Struct/obfuscate.h"
#include "../xdl/include/xdl.h"

// Check if pointer is safely readable
inline bool IsValidPointer(void* ptr) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
#if defined(__LP64__) || defined(_LP64)
    return (addr >= 0x10000000 && addr <= 0x7FFFFFFFFFFF);
#else
    return (addr >= 0x10000000 && addr <= 0xFFFFFFFF);
#endif
}

// Clamp float utility
inline float ClampFloat(float val, float minVal, float maxVal) {
    return std::clamp(val, minVal, maxVal);
}

// Universal module base resolver (Linker xdl -> Kernel /proc/self/maps fallback)
inline uintptr_t GetModuleBase(const char* moduleName) {
    if (!moduleName || moduleName[0] == '\0') return 0;

    // 1. Linker resolution via xdl
    void* handle = xdl_open(moduleName, 0);
    if (handle) {
        xdl_info_t info;
        if (xdl_info(handle, XDL_DI_DLINFO, &info) == 0 && info.dli_fbase) {
            xdl_close(handle);
            return (uintptr_t)info.dli_fbase;
        }
        xdl_close(handle);
    }

    // 2. Fallback: Parse /proc/self/maps
    FILE* fp = fopen("/proc/self/maps", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, moduleName)) {
                uintptr_t start = 0;
                if (sscanf(line, "%lx-", &start) == 1) {
                    fclose(fp);
                    return start;
                }
            }
        }
        fclose(fp);
    }
    return 0;
}

#endif // MEMORY_UTILS_H
