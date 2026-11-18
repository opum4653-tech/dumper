#ifndef MEMORY_UTILS_H
#define MEMORY_UTILS_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>

// Module base resolver for the current process.
// Primary path: /proc/self/maps (no dependency on a linker wrapper).
// The first read-executable map matching the name is the load bias.
inline uintptr_t GetModuleBase(const char* moduleName) {
    if (!moduleName || moduleName[0] == '\0') return 0;

    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return 0;

    char line[512];
    uintptr_t result = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (!strstr(line, moduleName)) continue;

        uintptr_t start = 0;
        char perms[5] = {0};
        if (sscanf(line, "%lx-%*lx %4s", &start, perms) != 2) continue;

        // Load bias is the start of the first mapping that owns the file.
        if (strchr(perms, 'x') || start != 0) {
            result = start;
            break;
        }
    }
    fclose(fp);
    return result;
}

// Overload used by the lifecycle bootstrap (pid is always self here).
inline uintptr_t get_module_base(pid_t pid, const char* moduleName) {
    if (pid != getpid()) return 0;
    return GetModuleBase(moduleName);
}

#endif // MEMORY_UTILS_H
