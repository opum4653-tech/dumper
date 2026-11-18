#pragma once
#include <cstdint>
#include <cstddef>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <android/log.h>
#include "../../Unity/Il2Cpp.h"

#ifndef ANDROID_LOG_INFO
#define ANDROID_LOG_INFO 4
#endif

extern uintptr_t il2cpp_base;

namespace RealDumper {

inline std::atomic<bool> g_IsDumping{false};
inline std::atomic<int> g_DumpProgress{0};
inline char g_DumpStatus[256] = "IDLE";
inline char g_LastDumpPath[256] = "";

inline const char* GetStatus() { return g_DumpStatus; }
inline int GetProgress() { return g_DumpProgress.load(); }
inline bool IsDumping() { return g_IsDumping.load(); }

inline uintptr_t GetRva(void* ptr) {
    if (!ptr || !il2cpp_base || (uintptr_t)ptr < il2cpp_base) return (uintptr_t)ptr;
    return (uintptr_t)ptr - il2cpp_base;
}

inline int OpenDumpFile(const char* filename, char* outChosenPath, size_t pathSize) {
    const char* basePaths[] = {
        "/storage/emulated/0/Download/NINMODS_DUMP/",
        "/sdcard/Download/NINMODS_DUMP/"
    };

    mkdir("/storage/emulated/0/Download/NINMODS_DUMP", 0777);
    mkdir("/sdcard/Download/NINMODS_DUMP", 0777);

    for (size_t i = 0; i < sizeof(basePaths) / sizeof(basePaths[0]); ++i) {
        char fullPath[320];
        snprintf(fullPath, sizeof(fullPath), "%s%s", basePaths[i], filename);
        int fd = open(fullPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) {
            if (outChosenPath && pathSize > 0) {
                snprintf(outChosenPath, pathSize, "%s", fullPath);
            }
            return fd;
        }
    }
    return -1;
}

// -------------------------------------------------------------
// 1. FULL DUMP.CS GENERATOR (100% In-Memory Real Reflection)
// -------------------------------------------------------------
inline void ExecuteFullCsDump() {
    g_IsDumping.store(true);
    g_DumpProgress.store(0);
    snprintf(g_DumpStatus, sizeof(g_DumpStatus), "INITIALIZING DUMP.CS...");

    char chosenPath[256] = "";
    int fd = OpenDumpFile("dump.cs", chosenPath, sizeof(chosenPath));
    if (fd < 0) {
        snprintf(g_DumpStatus, sizeof(g_DumpStatus), "ERROR: CANNOT OPEN STORAGE FOR DUMP.CS");
        g_IsDumping.store(false);
        return;
    }
    snprintf(g_LastDumpPath, sizeof(g_LastDumpPath), "%s", chosenPath);

    char banner[512];
    int blen = snprintf(banner, sizeof(banner),
        "// ==========================================================================\n"
        "// NINMODS In-Game IL2CPP Live Dump\n"
        "// libil2cpp Base: 0x%lx\n"
        "// Generated directly inside running game process without external PC tools\n"
        "// ==========================================================================\n\n",
        (unsigned long)il2cpp_base);
    write(fd, banner, blen);

    size_t asmCount = Il2CppGetAssemblyCount();
    if (asmCount == 0) {
        write(fd, "// ERROR: No assemblies found in il2cpp domain\n", 47);
        close(fd);
        snprintf(g_DumpStatus, sizeof(g_DumpStatus), "ERROR: 0 ASSEMBLIES LOADED");
        g_IsDumping.store(false);
        return;
    }

    size_t totalClassesAcrossAll = 0;
    for (size_t a = 0; a < asmCount; ++a) {
        const void* assem = Il2CppGetAssembly(a);
        if (!assem) continue;
        const void* img = Il2CppAssemblyGetImage(assem);
        if (!img) continue;
        totalClassesAcrossAll += Il2CppImageGetClassCount(img);
    }
    if (totalClassesAcrossAll == 0) totalClassesAcrossAll = 1;

    size_t processedClasses = 0;
    char chunkBuf[4096];

    for (size_t a = 0; a < asmCount; ++a) {
        const void* assem = Il2CppGetAssembly(a);
        if (!assem) continue;
        const void* img = Il2CppAssemblyGetImage(assem);
        if (!img) continue;

        const char* imgName = Il2CppImageGetName(img);
        size_t classCount = Il2CppImageGetClassCount(img);

        int imgHeaderLen = snprintf(chunkBuf, sizeof(chunkBuf),
            "\n// ==========================================================================\n"
            "// Assembly: %s (Classes: %zu)\n"
            "// ==========================================================================\n\n",
            imgName ? imgName : "Unknown", classCount);
        write(fd, chunkBuf, imgHeaderLen);

        for (size_t c = 0; c < classCount; ++c) {
            const void* klass = Il2CppImageGetClass(img, c);
            if (!klass) continue;

            const char* ns = Il2CppClassGetNamespace(klass);
            const char* kName = Il2CppClassGetName(klass);
            if (!kName || kName[0] == '\0') kName = "UnnamedClass";
            if (!ns) ns = "";

            const void* parent = Il2CppClassGetParent(klass);
            const char* parentName = parent ? Il2CppClassGetName(parent) : nullptr;

            int clen = 0;
            if (parentName && parentName[0] != '\0') {
                clen = snprintf(chunkBuf, sizeof(chunkBuf),
                    "// Namespace: %s\npublic class %s : %s\n{\n",
                    ns, kName, parentName);
            } else {
                clen = snprintf(chunkBuf, sizeof(chunkBuf),
                    "// Namespace: %s\npublic class %s\n{\n",
                    ns, kName);
            }
            write(fd, chunkBuf, clen);

            // Dump Fields
            write(fd, "\t// Fields\n", 11);
            void* fIter = nullptr;
            while (void* field = Il2CppClassGetFields(klass, &fIter)) {
                const char* fName = Il2CppFieldGetName(field);
                size_t fOff = Il2CppFieldGetOffset(field);
                const void* fType = Il2CppFieldGetType(field);
                const char* fTypeName = fType ? Il2CppTypeGetName(fType) : "var";

                if (fName && fName[0] != '\0') {
                    int flen = snprintf(chunkBuf, sizeof(chunkBuf),
                        "\tpublic %s %s; // 0x%zx\n",
                        fTypeName ? fTypeName : "var", fName, fOff);
                    write(fd, chunkBuf, flen);
                }
            }

            // Dump Methods
            write(fd, "\n\t// Methods\n", 13);
            void* mIter = nullptr;
            while (void* method = Il2CppClassGetMethods(klass, &mIter)) {
                const char* mName = Il2CppMethodGetName(method);
                uint32_t pCount = Il2CppMethodGetParamCount(method);
                void* codePtr = *(void**)method;
                uintptr_t rva = GetRva(codePtr);

                if (mName && mName[0] != '\0') {
                    int mlen = snprintf(chunkBuf, sizeof(chunkBuf),
                        "\t// RVA: 0x%lx Offset: 0x%lx VA: 0x%lx (Params: %u)\n\tpublic void %s();\n",
                        (unsigned long)rva, (unsigned long)rva, (unsigned long)codePtr, pCount, mName);
                    write(fd, chunkBuf, mlen);
                }
            }

            write(fd, "}\n\n", 3);

            processedClasses++;
            if ((processedClasses % 50) == 0 || processedClasses == totalClassesAcrossAll) {
                int pct = (int)((processedClasses * 100) / totalClassesAcrossAll);
                g_DumpProgress.store(pct);
                snprintf(g_DumpStatus, sizeof(g_DumpStatus), "Dumping %s (%d%%)...", imgName ? imgName : "", pct);
            }
        }
    }

    close(fd);
    g_DumpProgress.store(100);
    snprintf(g_DumpStatus, sizeof(g_DumpStatus), "SAVED: %s", chosenPath);
    g_IsDumping.store(false);
}

// -------------------------------------------------------------
// 2. GROUPED C++ HEADER DUMPER (Categorized & Zero Stubs)
// -------------------------------------------------------------
inline void DumpClassGroupCpp(int fd, const void* klass, const char* groupName) {
    if (!klass) return;
    const char* ns = Il2CppClassGetNamespace(klass);
    const char* kName = Il2CppClassGetName(klass);
    if (!kName || kName[0] == '\0') return;
    if (!ns) ns = "";

    char buf[512];
    int len = snprintf(buf, sizeof(buf),
        "\n// --------------------------------------------------------\n"
        "// [%s] CLASS: %s%s%s\n"
        "// --------------------------------------------------------\n",
        groupName, ns, (ns[0] != '\0') ? "." : "", kName);
    write(fd, buf, len);

    // Fields
    void* fIter = nullptr;
    while (void* field = Il2CppClassGetFields(klass, &fIter)) {
        const char* fName = Il2CppFieldGetName(field);
        size_t fOff = Il2CppFieldGetOffset(field);
        if (fName && fName[0] != '\0') {
            int flen = snprintf(buf, sizeof(buf), "#define OFFSET_%s_%s 0x%zxULL\n", kName, fName, fOff);
            write(fd, buf, flen);
        }
    }

    // Methods
    void* mIter = nullptr;
    while (void* method = Il2CppClassGetMethods(klass, &mIter)) {
        const char* mName = Il2CppMethodGetName(method);
        uint32_t pCount = Il2CppMethodGetParamCount(method);
        void* codePtr = *(void**)method;
        uintptr_t rva = GetRva(codePtr);
        if (mName && mName[0] != '\0' && rva > 0) {
            int mlen = snprintf(buf, sizeof(buf), "#define RVA_%s_%s 0x%lxULL // args: %u\n", kName, mName, (unsigned long)rva, pCount);
            write(fd, buf, mlen);
        }
    }
}

inline void ExecuteGroupedOffsetsDump() {
    g_IsDumping.store(true);
    g_DumpProgress.store(0);
    snprintf(g_DumpStatus, sizeof(g_DumpStatus), "DUMPING GROUPED OFFSETS...");

    char chosenPath[256] = "";
    int fd = OpenDumpFile("ninmods_real_offsets.h", chosenPath, sizeof(chosenPath));
    if (fd < 0) {
        snprintf(g_DumpStatus, sizeof(g_DumpStatus), "ERROR: CANNOT OPEN STORAGE FILE");
        g_IsDumping.store(false);
        return;
    }
    snprintf(g_LastDumpPath, sizeof(g_LastDumpPath), "%s", chosenPath);

    char headBuf[384];
    int headLen = snprintf(headBuf, sizeof(headBuf),
        "// ==========================================================================\n"
        "// NINMODS 100%% Dynamic Live Memory Offset Dump (Grouped)\n"
        "// libil2cpp Base: 0x%lx\n"
        "#pragma once\n#include <cstdint>\n\n",
        (unsigned long)il2cpp_base);
    write(fd, headBuf, headLen);

    const void* assemImg = Il2CppGetImageByName("Assembly-CSharp.dll");
    const void* unityImg = Il2CppGetImageByName("UnityEngine.CoreModule.dll");
    if (!unityImg) unityImg = Il2CppGetImageByName("UnityEngine.dll");

    size_t total = 0;
    if (assemImg) total += Il2CppImageGetClassCount(assemImg);
    if (unityImg) total += Il2CppImageGetClassCount(unityImg);
    if (total == 0) total = 1;

    size_t processed = 0;

    auto ProcessImageGroup = [&](const void* img, const char* defGroupName) {
        if (!img) return;
        size_t count = Il2CppImageGetClassCount(img);
        for (size_t c = 0; c < count; ++c) {
            const void* klass = Il2CppImageGetClass(img, c);
            if (!klass) continue;
            const char* kName = Il2CppClassGetName(klass);
            const char* ns = Il2CppClassGetNamespace(klass);
            if (!kName) continue;
            if (!ns) ns = "";

            const char* group = defGroupName;
            if (strstr(kName, "Player") || strstr(kName, "Character") || strstr(kName, "Entity") || strstr(ns, "COW.GamePlay")) {
                group = "COMBAT & ENTITY";
            } else if (strstr(kName, "Weapon") || strstr(kName, "Bullet") || strstr(kName, "Gun") || strstr(kName, "Aim")) {
                group = "WEAPON & COMBAT";
            } else if (strstr(kName, "Camera") || strstr(kName, "Transform") || strstr(kName, "Component") || strstr(ns, "UnityEngine")) {
                group = "ENGINE & CAMERA";
            } else if (strstr(kName, "GameFacade") || strstr(kName, "Network") || strstr(kName, "Protocol") || strstr(kName, "Config")) {
                group = "NETWORK & CORE";
            } else if (strstr(kName, "UI") || strstr(kName, "HUD") || strstr(kName, "Controller")) {
                group = "UI & CONTROLLER";
            }

            DumpClassGroupCpp(fd, klass, group);

            processed++;
            if ((processed % 40) == 0 || processed == total) {
                int pct = (int)((processed * 100) / total);
                g_DumpProgress.store(pct);
                snprintf(g_DumpStatus, sizeof(g_DumpStatus), "Grouping Offsets (%d%%)...", pct);
            }
        }
    };

    ProcessImageGroup(assemImg, "ASSEMBLY-CSHARP");
    ProcessImageGroup(unityImg, "UNITY ENGINE");

    close(fd);
    g_DumpProgress.store(100);
    snprintf(g_DumpStatus, sizeof(g_DumpStatus), "SAVED: %s", chosenPath);
    g_IsDumping.store(false);
}

inline void TriggerFullCsDumpAsync() {
    if (g_IsDumping.load()) return;
    std::thread(ExecuteFullCsDump).detach();
}

inline void TriggerGroupedOffsetsDumpAsync() {
    if (g_IsDumping.load()) return;
    std::thread(ExecuteGroupedOffsetsDump).detach();
}

// -------------------------------------------------------------
// 3. LIVE IN-GAME CLASS SEARCH HELPER
// -------------------------------------------------------------
struct LiveClassResult {
    const void* klass;
    char name[128];
    char namespaze[128];
    char imageName[64];
};

inline bool CiContains(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    if (needle[0] == '\0') return true;
    for (; *haystack; ++haystack) {
        const char *h = haystack, *n = needle;
        while (*h && *n && (tolower((unsigned char)*h) == tolower((unsigned char)*n))) {
            ++h; ++n;
        }
        if (!*n) return true;
    }
    return false;
}

inline std::vector<LiveClassResult> SearchClassesLive(const char* query, size_t maxCount = 25) {
    std::vector<LiveClassResult> results;
    if (!query || query[0] == '\0') return results;

    const char* targetImages[] = {
        "Assembly-CSharp.dll",
        "UnityEngine.CoreModule.dll",
        "UnityEngine.dll"
    };

    for (const char* imgName : targetImages) {
        const void* img = Il2CppGetImageByName(imgName);
        if (!img) continue;
        size_t count = Il2CppImageGetClassCount(img);
        for (size_t i = 0; i < count && results.size() < maxCount; ++i) {
            const void* klass = Il2CppImageGetClass(img, i);
            if (!klass) continue;
            const char* kName = Il2CppClassGetName(klass);
            if (!kName) continue;
            if (CiContains(kName, query)) {
                LiveClassResult res{};
                res.klass = klass;
                snprintf(res.name, sizeof(res.name), "%s", kName);
                const char* ns = Il2CppClassGetNamespace(klass);
                snprintf(res.namespaze, sizeof(res.namespaze), "%s", ns ? ns : "");
                snprintf(res.imageName, sizeof(res.imageName), "%s", imgName);
                results.push_back(res);
            }
        }
    }
    return results;
}

} // namespace RealDumper
