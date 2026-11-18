#ifndef NINMODS_CORE_MEMORYUTILS_H
#define NINMODS_CORE_MEMORYUTILS_H

// Module base resolution for the current process.
//
// Primary path is /proc/self/maps: the load bias of a shared object is the start
// of its lowest mapping, and that is what every address in the module is relative
// to. The pathname field is compared on its basename so both
// "/data/app/.../lib/arm64/libil2cpp.so" and a bare "libil2cpp.so" match.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace memutils {

// Returns the basename pointer of a /proc/self/maps pathname (7th field).
inline const char* PathBasename(const char* path) {
    if (!path) return "";
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

inline bool BasenameEquals(const char* path, const char* moduleName) {
    const char* base = PathBasename(path);
    return strcmp(base, moduleName) == 0;
}

} // namespace memutils

// Lowest mapping address of the named module, or 0 when it is not mapped.
inline uintptr_t GetModuleBase(const char* moduleName) {
    if (!moduleName || moduleName[0] == '\0') return 0;

    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return 0;

    char line[1024];
    uintptr_t lowest = 0;

    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0;
        char perms[5] = {0};
        // address perms offset dev inode pathname
        if (sscanf(line, "%lx-%*lx %4s %*s %*s %*s", &start, perms) < 2) continue;

        // Pathname is everything after the inode field; locate it by skipping
        // the first five whitespace-separated tokens instead of relying on %s
        // into a fixed buffer, since paths can carry spaces.
        const char* p = line;
        int fields = 0;
        while (*p && fields < 5) {
            while (*p == ' ' || *p == '\t') ++p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') ++p;
            ++fields;
        }
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p || *p == '\n') continue; // anonymous mapping, no file backing

        if (!memutils::BasenameEquals(p, moduleName)) continue;

        if (lowest == 0 || start < lowest) lowest = start;
    }

    fclose(fp);
    return lowest;
}

// Same resolver, keyed on a pid. This library only ever inspects itself, so any
// pid other than the current one has no resolvable address space here.
inline uintptr_t get_module_base(pid_t pid, const char* moduleName) {
    if (pid != getpid()) return 0;
    return GetModuleBase(moduleName);
}

#endif // NINMODS_CORE_MEMORYUTILS_H
