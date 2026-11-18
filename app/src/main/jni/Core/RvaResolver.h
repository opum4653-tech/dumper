#pragma once

// Turns a runtime code pointer into a real RVA.
//
// An RVA is only defined relative to the module that contains the address. A
// method pointer that lands in libil2cpp.so has an RVA against libil2cpp.so's
// load bias; a pointer that lands elsewhere has no RVA against libil2cpp at all
// and must be reported as external rather than subtracted blindly.
//
// The lookup is done through /proc/self/maps: the containing mapping names the
// file, the file's lowest mapping is the module base, and the ELF program
// headers give the bias correction when the first PT_LOAD does not start at
// virtual address 0.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <unistd.h>
#include "SafeMemory.h"
#include "ElfImage.h"

namespace RvaResolver {

struct Location {
    bool        insideModule = false; // address belongs to libil2cpp.so itself
    uint64_t    rva          = 0;     // valid only when insideModule
    uintptr_t   va           = 0;
    std::string moduleName;           // "libil2cpp.so" or whatever owns the page
};

namespace detail {

struct ModuleInfo {
    uintptr_t  base = 0;
    uint64_t   bias = 0;
    uint64_t   minVaddr = 0;
    uint64_t   maxVaddr = 0;
    bool       parsed = false;
};

inline std::map<std::string, ModuleInfo>& ModuleCache() {
    static std::map<std::string, ModuleInfo> s_cache;
    return s_cache;
}

inline std::string& TargetModule() {
    static std::string s_target = "libil2cpp.so";
    return s_target;
}

// Basename of a /proc/self/maps pathname.
inline std::string Basename(const char* path) {
    if (!path) return std::string();
    const char* slash = strrchr(path, '/');
    return std::string(slash ? slash + 1 : path);
}

} // namespace detail

inline void SetTargetModule(const char* moduleName) {
    if (moduleName && *moduleName) {
        detail::TargetModule() = moduleName;
        detail::ModuleCache().clear();
    }
}

inline const std::string& TargetModule() { return detail::TargetModule(); }

// Lowest mapping of the named module, or 0.
inline uintptr_t FindModuleBase(const std::string& moduleName) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return 0;

    char line[1024];
    uintptr_t lowest = 0;

    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0;
        char perms[5] = {0};
        if (sscanf(line, "%lx-%*lx %4s %*s %*s %*s", &start, perms) < 2) continue;

        // Pathname is the sixth whitespace-separated token onward.
        const char* p = line;
        int fields = 0;
        while (*p && fields < 5) {
            while (*p == ' ' || *p == '\t') ++p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') ++p;
            ++fields;
        }
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p || *p == '\n') continue;

        if (detail::Basename(p) != moduleName) continue;
        if (lowest == 0 || start < lowest) lowest = start;
    }

    fclose(fp);
    return lowest;
}

inline const detail::ModuleInfo& Module(const std::string& moduleName) {
    auto& cache = detail::ModuleCache();
    auto it = cache.find(moduleName);
    if (it != cache.end()) return it->second;

    detail::ModuleInfo info{};
    info.base = FindModuleBase(moduleName);
    if (info.base) {
        ElfImage::Info elf;
        if (ElfImage::Parse(info.base, elf)) {
            info.bias = elf.loadBias;
            info.minVaddr = elf.minVaddr;
            info.maxVaddr = elf.maxVaddr;
            info.parsed = true;
        } else {
            info.bias = info.base;
        }
    }
    return cache.emplace(moduleName, info).first->second;
}

// Name of the module that owns a runtime address, empty when anonymous.
inline std::string ModuleOwning(uintptr_t address) {
    const SafeMemory::Mapping* mapping = SafeMemory::Find(address);
    if (!mapping) return std::string();

    // Find() gives the range; the pathname has to come from the maps line, so
    // re-scan once. This runs only when a method pointer is outside libil2cpp,
    // which is rare enough to afford the rescan.
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return std::string();

    char line[1024];
    std::string found;
    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0, end = 0;
        if (sscanf(line, "%lx-%lx", &start, &end) != 2) continue;
        if (address < start || address >= end) continue;

        const char* p = line;
        int fields = 0;
        while (*p && fields < 5) {
            while (*p == ' ' || *p == '\t') ++p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') ++p;
            ++fields;
        }
        while (*p == ' ' || *p == '\t') ++p;
        if (*p && *p != '\n') found = detail::Basename(p);
        break;
    }
    fclose(fp);
    return found;
}

// Resolves a code pointer. insideModule is true only when the address belongs
// to the target module and falls inside one of its PT_LOAD ranges.
inline Location Resolve(uintptr_t address, const std::string& moduleName) {
    Location loc{};
#if defined(__arm__)
    address &= ~1ULL; // Strip ARM Thumb bit on 32-bit ARM
#endif
    loc.va = address;
    if (!address) return loc;

    const detail::ModuleInfo& info = Module(moduleName);
    if (!info.base) return loc;

    if (info.parsed) {
        const uintptr_t modStart = (uintptr_t)(info.bias + info.minVaddr);
        const uintptr_t modEnd   = (uintptr_t)(info.bias + info.maxVaddr);
        if (address >= modStart && address < modEnd) {
            loc.insideModule = true;
            loc.moduleName   = moduleName;
            loc.rva          = (uint64_t)address - info.bias;
            return loc;
        }
    }

    // Only scan /proc/self/maps when the address is genuinely outside target module
    loc.moduleName = ModuleOwning(address);
    if (loc.moduleName.empty()) loc.moduleName = "unknown";
    loc.insideModule = false;
    loc.rva = 0;
    return loc;
}

inline Location Resolve(uintptr_t address) {
    return Resolve(address, detail::TargetModule());
}

} // namespace RvaResolver
