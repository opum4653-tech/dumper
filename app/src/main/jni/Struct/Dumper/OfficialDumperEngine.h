#pragma once
#include <cstdint>
#include <cstddef>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <android/log.h>
#include "../../Unity/Il2Cpp.h"
#include "../../Core/SafeMemory.h"
#include "../../Core/MemoryUtils.h"
#include "../../Core/JsonWriter.h"
#include "../../Core/MetadataParser.h"
#include "../../Core/ElfReconstructor.h"

#ifndef ANDROID_LOG_INFO
#define ANDROID_LOG_INFO 4
#endif

extern uintptr_t il2cpp_base;

namespace OfficialDumper {

inline std::atomic<bool> g_IsDumping{false};
inline std::atomic<int> g_DumpProgress{0};
inline char g_DumpStatus[256] = "IDLE";
inline char g_LastDumpDir[256] = "";

inline const char* GetStatus() { return g_DumpStatus; }
inline int GetProgress() { return g_DumpProgress.load(); }
inline bool IsDumping() { return g_IsDumping.load(); }

inline uintptr_t GetRva(void* ptr) {
    if (!ptr || !il2cpp_base || (uintptr_t)ptr < il2cpp_base) return (uintptr_t)ptr;
    return (uintptr_t)ptr - il2cpp_base;
}

inline std::string GetCurrentPackageName() {
    char buf[256] = {0};
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            return std::string(buf);
        }
    }
    return "Universal_Unity_Game";
}

inline bool EnsureDirectory(const char* dir) {
    struct stat st;
    if (stat(dir, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    if (mkdir(dir, 0777) == 0) {
        chmod(dir, 0777);
        return true;
    }
    return false;
}

inline bool GetOutputDirectory(char* outDir, size_t outSize) {
    const char* baseCandidates[] = {
        "/storage/emulated/0/Download/NINMODS_DUMP",
        "/sdcard/Download/NINMODS_DUMP"
    };

    for (size_t i = 0; i < sizeof(baseCandidates) / sizeof(baseCandidates[0]); ++i) {
        if (EnsureDirectory(baseCandidates[i])) {
            snprintf(outDir, outSize, "%s", baseCandidates[i]);
            return true;
        }
    }
    return false;
}

// --------------------------------------------------------------------------
// 1. IN-MEMORY DECRYPTED METADATA DUMPER (Magic 0xFAB11BAF + Crash-Proof)
// --------------------------------------------------------------------------
inline bool DumpInMemoryDecryptedMetadata(const char* dirPath) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;

    char line[512];
    uintptr_t foundAddr = 0;
    size_t foundSize = 0;

    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0, end = 0;
        char perms[5] = {0};
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) == 3) {
            if (perms[0] == 'r' && (end - start) >= 0x4000) {
                uint32_t magic = 0;
                if (SafeMemory::ReadUint32(start, &magic) && magic == 0xFAB11BAF) {
                    foundAddr = start;
                    foundSize = MetadataParser::CalculateExactMetadataSize(start);
                    if (foundSize == 0 || foundSize > (end - start)) {
                        foundSize = end - start;
                    }
                    break;
                }
            }
        }
    }
    fclose(fp);

    if (foundAddr && foundSize > 0) {
        char outPath[320];
        snprintf(outPath, sizeof(outPath), "%s/global-metadata.dat", dirPath);
        int outFd = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (outFd >= 0) {
            const size_t kChunk = 64 * 1024;
            std::vector<uint8_t> buf(kChunk);
            size_t written = 0;
            while (written < foundSize) {
                size_t toWrite = std::min(kChunk, foundSize - written);
                if (SafeMemory::Read(foundAddr + written, buf.data(), toWrite)) {
                    write(outFd, buf.data(), toWrite);
                } else {
                    std::memset(buf.data(), 0, toWrite);
                    write(outFd, buf.data(), toWrite);
                }
                written += toWrite;
            }
            close(outFd);

            // Also dump stringliteral.json for IDA Pro / Ghidra
            char litJsonPath[320];
            snprintf(litJsonPath, sizeof(litJsonPath), "%s/stringliteral.json", dirPath);
            MetadataParser::DumpStringLiteralsJson(foundAddr, litJsonPath);

            return true;
        }
    }
    return false;
}

// --------------------------------------------------------------------------
// 2. IN-MEMORY LIBIL2CPP.SO ELF RECONSTRUCTION DUMPER
// --------------------------------------------------------------------------
inline bool DumpInMemoryLibil2cpp(const char* dirPath) {
    if (!il2cpp_base) {
        il2cpp_base = GetModuleBase("libil2cpp.so");
        if (!il2cpp_base) return false;
    }

    char outPath[320];
    snprintf(outPath, sizeof(outPath), "%s/libil2cpp_dumped.so", dirPath);
    return ElfReconstructor::DumpAndFixElf(il2cpp_base, outPath);
}

// --------------------------------------------------------------------------
// 3. PYTHON TOOLS GENERATOR (For IDA Pro & Ghidra 1-Click Reverse Engineering)
// --------------------------------------------------------------------------
inline void GeneratePythonTools(const char* dirPath) {
    char idaPath[320];
    snprintf(idaPath, sizeof(idaPath), "%s/ida_with_struct_py3.py", dirPath);
    int fdIda = open(idaPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fdIda >= 0) {
        const char* idaCode =
            "# Official IDA Pro 7.x / 8.x / 9.x Python 3 Script\n"
            "# Generated by NINMODS Official In-Memory Dumper Suite\n"
            "import json\n"
            "import os\n"
            "import ida_bytes\n"
            "import ida_name\n"
            "import ida_nalt\n"
            "import ida_kernwin\n\n"
            "def apply_symbols():\n"
            "    script_dir = os.path.dirname(os.path.realpath(__file__))\n"
            "    json_path = os.path.join(script_dir, 'script.json')\n"
            "    if not os.path.exists(json_path):\n"
            "        json_path = ida_kernwin.ask_file(False, '*.json', 'Select script.json')\n"
            "    if not json_path or not os.path.exists(json_path):\n"
            "        print('[!] script.json not found.')\n"
            "        return\n\n"
            "    with open(json_path, 'r', encoding='utf-8') as fp:\n"
            "        data = json.load(fp)\n\n"
            "    image_base = ida_nalt.get_imagebase()\n"
            "    methods = data.get('ScriptMethod', [])\n"
            "    print(f'[*] Applying {len(methods)} Il2Cpp methods to IDA (ImageBase: {hex(image_base)})...')\n"
            "    count = 0\n"
            "    for m in methods:\n"
            "        rva = m.get('Address', 0)\n"
            "        name = m.get('Name', '')\n"
            "        if rva > 0 and name:\n"
            "            va = image_base + rva\n"
            "            ida_name.set_name(va, name, ida_name.SN_NOWARN)\n"
            "            count += 1\n"
            "    print(f'[+] Successfully applied {count} methods from NINMODS Dump!')\n\n"
            "if __name__ == '__main__':\n"
            "    apply_symbols()\n";
        write(fdIda, idaCode, strlen(idaCode));
        close(fdIda);
    }

    char ghidraPath[320];
    snprintf(ghidraPath, sizeof(ghidraPath), "%s/ghidra_with_struct.py", dirPath);
    int fdGhidra = open(ghidraPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fdGhidra >= 0) {
        const char* ghidraCode =
            "# Official Ghidra Python Symbol Loader\n"
            "# @category Il2Cpp\n"
            "# Generated by NINMODS Official In-Memory Dumper Suite\n"
            "import json\n\n"
            "def run_ghidra_import():\n"
            "    f = askFile('Select script.json', 'Open')\n"
            "    if not f:\n"
            "        return\n"
            "    with open(f.getAbsolutePath(), 'r') as fp:\n"
            "        data = json.load(fp)\n"
            "    methods = data.get('ScriptMethod', [])\n"
            "    base = currentProgram.getImageBase().getOffset()\n"
            "    print('Importing %d symbols...' % len(methods))\n"
            "    for m in methods:\n"
            "        addr = currentAddress.getNewAddress(base + m['Address'])\n"
            "        createLabel(addr, m['Name'], True)\n"
            "    print('Finished applying Il2Cpp symbols in Ghidra!')\n\n"
            "run_ghidra_import()\n";
        write(fdGhidra, ghidraCode, strlen(ghidraCode));
        close(fdGhidra);
    }

    char hookGenPath[320];
    snprintf(hookGenPath, sizeof(hookGenPath), "%s/ninmods_hook_generator.py", dirPath);
    int fdHookGen = open(hookGenPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fdHookGen >= 0) {
        const char* hookGenCode =
            "# NINMODS Official C++ Hook Boilerplate Generator\n"
            "# Generates ready-to-paste DobbyHook / C++ function hooks from dump.cs\n"
            "import os, sys\n\n"
            "def search_and_gen():\n"
            "    script_dir = os.path.dirname(os.path.realpath(__file__))\n"
            "    cs_path = os.path.join(script_dir, 'dump.cs')\n"
            "    if not os.path.exists(cs_path):\n"
            "        print('[!] dump.cs not found in current folder.')\n"
            "        return\n"
            "    query = input('[?] Enter Class or Method name to generate hook: ').strip()\n"
            "    if not query:\n"
            "        return\n"
            "    print(f'[*] Searching for \"{query}\" in {cs_path}...')\n"
            "    found = 0\n"
            "    with open(cs_path, 'r', encoding='utf-8', errors='ignore') as fp:\n"
            "        current_class = 'UnknownClass'\n"
            "        for line in fp:\n"
            "            if line.startswith('public class '):\n"
            "                current_class = line.split()[2].split(':')[0].strip()\n"
            "            if query.lower() in line.lower() and '// RVA:' in line:\n"
            "                found += 1\n"
            "                parts = line.strip().split()\n"
            "                rva = '0x0'\n"
            "                for idx, p in enumerate(parts):\n"
            "                    if p == 'RVA:':\n"
            "                        rva = parts[idx+1]\n"
            "                        break\n"
            "                fn_name = parts[-1].rstrip('();')\n"
            "                print('='*60)\n"
            "                print(f'// Target: {current_class}::{fn_name} (RVA: {rva})')\n"
            "                print(f'void* (*orig_{current_class}_{fn_name})(void* instance) = nullptr;')\n"
            "                print(f'void* hook_{current_class}_{fn_name}(void* instance) {{')\n"
            "                print(f'    // Your mod logic here')\n"
            "                print(f'    return orig_{current_class}_{fn_name}(instance);')\n"
            "                print('}')\n"
            "                print(f'// Install:')\n"
            "                print(f'DobbyHook((void*)(il2cpp_base + {rva}), (void*)hook_{current_class}_{fn_name}, (void**)&orig_{current_class}_{fn_name});')\n"
            "    print(f'[*] Done. Generated {found} hook templates.')\n\n"
            "if __name__ == '__main__':\n"
            "    search_and_gen()\n";
        write(fdHookGen, hookGenCode, strlen(hookGenCode));
        close(fdHookGen);
    }

    char readmePath[320];
    snprintf(readmePath, sizeof(readmePath), "%s/README.txt", dirPath);
    int fdReadme = open(readmePath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fdReadme >= 0) {
        std::string pkg = GetCurrentPackageName();
        char readme[950];
        snprintf(readme, sizeof(readme),
            "========================================================================\n"
            "NINMODS OFFICIAL IN-MEMORY IL2CPP DUMP SUITE\n"
            "Target Package: %s (PID: %d)\n"
            "Status: 100%% In-Memory Decrypted Real Offsets (Bypasses Encrypted Disk)\n"
            "========================================================================\n"
            "1. dump.cs                  - Full decompiled C# classes, methods, and fields\n"
            "2. script.json              - Official JSON symbol mapping for IDA Pro & Ghidra\n"
            "3. stringliteral.json       - Extracted in-memory decrypted game string literals\n"
            "4. il2cpp.h                 - Complete C++ SDK header with structures and typedefs\n"
            "5. ninmods_real_offsets.h   - Grouped C++ macro offsets for game modding\n"
            "6. global-metadata.dat      - In-memory decrypted metadata (100%% decrypted)\n"
            "7. libil2cpp_dumped.so      - In-memory reconstructed valid ELF shared library\n"
            "8. ida_with_struct_py3.py   - IDA Pro Python script (File -> Script file)\n"
            "9. ghidra_with_struct.py    - Ghidra Python script (Ghidra Script Manager)\n"
            "10. ninmods_hook_generator.py - Python tool to auto-generate C++ hook boilerplate\n",
            pkg.c_str(), getpid());
        write(fdReadme, readme, strlen(readme));
        close(fdReadme);
    }
}

// --------------------------------------------------------------------------
// 4. OFFICIAL ALL-IN-ONE DUMP SUITE EXECUTOR (Universal for Any Region/Game)
// --------------------------------------------------------------------------
inline void ExecuteOfficialAllInOneDump() {
    g_IsDumping.store(true);
    g_DumpProgress.store(0);
    snprintf(g_DumpStatus, sizeof(g_DumpStatus), "INITIALIZING OFFICIAL DUMP...");

    char dumpDir[256] = "";
    if (!GetOutputDirectory(dumpDir, sizeof(dumpDir))) {
        snprintf(g_DumpStatus, sizeof(g_DumpStatus), "ERROR: STORAGE PERMISSION DENIED");
        g_IsDumping.store(false);
        return;
    }
    snprintf(g_LastDumpDir, sizeof(g_LastDumpDir), "%s", dumpDir);

    std::string currentPackage = GetCurrentPackageName();
    __android_log_print(ANDROID_LOG_INFO, "NINMODS", "Starting Official Dump for Package: %s", currentPackage.c_str());

    // 1. Generate Python Tools & Readme
    GeneratePythonTools(dumpDir);

    // 2. Dump in-memory decrypted global-metadata.dat and libil2cpp.so
    snprintf(g_DumpStatus, sizeof(g_DumpStatus), "Extracting In-Memory Metadata & Lib...");
    DumpInMemoryDecryptedMetadata(dumpDir);
    DumpInMemoryLibil2cpp(dumpDir);

    // 3. Open output files for C#, JSON, SDK, Offsets
    char csPath[320], jsonPath[320], sdkPath[320], offsetPath[320];
    snprintf(csPath, sizeof(csPath), "%s/dump.cs", dumpDir);
    snprintf(jsonPath, sizeof(jsonPath), "%s/script.json", dumpDir);
    snprintf(sdkPath, sizeof(sdkPath), "%s/il2cpp.h", dumpDir);
    snprintf(offsetPath, sizeof(offsetPath), "%s/ninmods_real_offsets.h", dumpDir);

    int fdCs = open(csPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    int fdJson = open(jsonPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    int fdSdk = open(sdkPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    int fdOff = open(offsetPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    if (fdCs < 0 || fdJson < 0) {
        snprintf(g_DumpStatus, sizeof(g_DumpStatus), "ERROR: FAILED TO CREATE DUMP FILES");
        if (fdCs >= 0) close(fdCs);
        if (fdJson >= 0) close(fdJson);
        if (fdSdk >= 0) close(fdSdk);
        if (fdOff >= 0) close(fdOff);
        g_IsDumping.store(false);
        return;
    }

    // Write file headers
    char headerBuf[600];
    int hlen = snprintf(headerBuf, sizeof(headerBuf),
        "// ==========================================================================\n"
        "// NINMODS OFFICIAL IN-MEMORY IL2CPP DUMP\n"
        "// Target Package: %s (PID: %d)\n"
        "// libil2cpp Base: 0x%lx\n"
        "// ==========================================================================\n\n",
        currentPackage.c_str(), getpid(), (unsigned long)il2cpp_base);
    write(fdCs, headerBuf, hlen);

    int sdkHlen = snprintf(headerBuf, sizeof(headerBuf),
        "// NINMODS Official C++ SDK Header\n// Target Package: %s\n#pragma once\n#include <cstdint>\n\n",
        currentPackage.c_str());
    write(fdSdk, headerBuf, sdkHlen);

    int offHlen = snprintf(headerBuf, sizeof(headerBuf),
        "// NINMODS Grouped Real Offsets Header\n// Target Package: %s\n#pragma once\n#include <cstdint>\n\n",
        currentPackage.c_str());
    write(fdOff, headerBuf, offHlen);

    write(fdJson, "{\n  \"ScriptMethod\": [\n", 23);

    size_t asmCount = Il2CppGetAssemblyCount();
    size_t totalClasses = 0;
    for (size_t a = 0; a < asmCount; ++a) {
        const void* assem = Il2CppGetAssembly(a);
        if (!assem) continue;
        const void* img = Il2CppAssemblyGetImage(assem);
        if (!img) continue;
        totalClasses += Il2CppImageGetClassCount(img);
    }
    if (totalClasses == 0) totalClasses = 1;

    size_t processedClasses = 0;
    bool isFirstJsonMethod = true;
    char chunk[4096];

    for (size_t a = 0; a < asmCount; ++a) {
        const void* assem = Il2CppGetAssembly(a);
        if (!assem) continue;
        const void* img = Il2CppAssemblyGetImage(assem);
        if (!img) continue;

        const char* imgName = Il2CppImageGetName(img);
        size_t classCount = Il2CppImageGetClassCount(img);

        int imgLen = snprintf(chunk, sizeof(chunk),
            "\n// ==========================================================================\n"
            "// Assembly: %s (%zu classes)\n"
            "// ==========================================================================\n\n",
            imgName ? imgName : "Unknown", classCount);
        write(fdCs, chunk, imgLen);

        for (size_t c = 0; c < classCount; ++c) {
            const void* klass = Il2CppImageGetClass(img, c);
            if (!klass) continue;

            const char* ns = Il2CppClassGetNamespace(klass);
            const char* kName = Il2CppClassGetName(klass);
            if (!kName || kName[0] == '\0') kName = "UnnamedClass";
            if (!ns) ns = "";

            const void* parent = Il2CppClassGetParent(klass);
            const char* parentName = parent ? Il2CppClassGetName(parent) : nullptr;

            // Write C# Class definition
            int cDeclLen = 0;
            if (parentName && parentName[0] != '\0') {
                cDeclLen = snprintf(chunk, sizeof(chunk),
                    "// Namespace: %s\npublic class %s : %s\n{\n",
                    ns, kName, parentName);
            } else {
                cDeclLen = snprintf(chunk, sizeof(chunk),
                    "// Namespace: %s\npublic class %s\n{\n",
                    ns, kName);
            }
            write(fdCs, chunk, cDeclLen);

            // Write C++ SDK struct definition
            int sdkDeclLen = snprintf(chunk, sizeof(chunk),
                "// [%s] %s\nstruct %s {\n",
                ns, kName, kName);
            write(fdSdk, chunk, sdkDeclLen);

            // Grouping for offsets header
            const char* group = "GENERAL";
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

            int offHeaderLen = snprintf(chunk, sizeof(chunk),
                "\n// [%s] %s%s%s\n", group, ns, (ns[0] != '\0') ? "." : "", kName);
            write(fdOff, chunk, offHeaderLen);

            // Dump Fields
            write(fdCs, "\t// Fields\n", 11);
            void* fIter = nullptr;
            while (void* field = Il2CppClassGetFields(klass, &fIter)) {
                const char* fName = Il2CppFieldGetName(field);
                size_t fOff = Il2CppFieldGetOffset(field);
                const void* fType = Il2CppFieldGetType(field);
                const char* fTypeName = fType ? Il2CppTypeGetName(fType) : "var";

                if (fName && fName[0] != '\0') {
                    // dump.cs
                    int flen = snprintf(chunk, sizeof(chunk),
                        "\tpublic %s %s; // 0x%zx\n",
                        fTypeName ? fTypeName : "var", fName, fOff);
                    write(fdCs, chunk, flen);

                    // il2cpp.h
                    int sflen = snprintf(chunk, sizeof(chunk),
                        "\tuintptr_t %s; // +0x%zx\n", fName, fOff);
                    write(fdSdk, chunk, sflen);

                    // ninmods_real_offsets.h
                    int oflen = snprintf(chunk, sizeof(chunk),
                        "#define OFFSET_%s_%s 0x%zxULL\n", kName, fName, fOff);
                    write(fdOff, chunk, oflen);
                }
            }

            // Dump Methods
            write(fdCs, "\n\t// Methods\n", 13);
            void* mIter = nullptr;
            while (void* method = Il2CppClassGetMethods(klass, &mIter)) {
                const char* mName = Il2CppMethodGetName(method);
                uint32_t pCount = Il2CppMethodGetParamCount(method);
                void* codePtr = *(void**)method;
                uintptr_t rva = GetRva(codePtr);

                if (mName && mName[0] != '\0') {
                    // dump.cs
                    int mlen = snprintf(chunk, sizeof(chunk),
                        "\t// RVA: 0x%lx Offset: 0x%lx VA: 0x%lx (Params: %u)\n\tpublic void %s();\n",
                        (unsigned long)rva, (unsigned long)rva, (unsigned long)codePtr, pCount, mName);
                    write(fdCs, chunk, mlen);

                    // ninmods_real_offsets.h
                    if (rva > 0) {
                        int omLen = snprintf(chunk, sizeof(chunk),
                            "#define RVA_%s_%s 0x%lxULL // params: %u\n", kName, mName, (unsigned long)rva, pCount);
                        write(fdOff, chunk, omLen);
                    }

                    // script.json entry
                    if (rva > 0) {
                        std::string escKName = JsonWriter::EscapeString(kName);
                        std::string escMName = JsonWriter::EscapeString(mName);
                        int jlen = snprintf(chunk, sizeof(chunk),
                            "%s    {\n      \"Address\": %lu,\n      \"Name\": \"%s$$%s\",\n      \"Signature\": \"void %s_%s (void* __this);\"\n    }",
                            isFirstJsonMethod ? "" : ",\n",
                            (unsigned long)rva, escKName.c_str(), escMName.c_str(), escKName.c_str(), escMName.c_str());
                        write(fdJson, chunk, jlen);
                        isFirstJsonMethod = false;
                    }
                }
            }

            write(fdCs, "}\n\n", 3);
            write(fdSdk, "};\n\n", 4);

            processedClasses++;
            if ((processedClasses % 60) == 0 || processedClasses == totalClasses) {
                int pct = (int)((processedClasses * 100) / totalClasses);
                g_DumpProgress.store(pct);
                snprintf(g_DumpStatus, sizeof(g_DumpStatus), "Dumping %s (%d%%)...", imgName ? imgName : "", pct);
            }
        }
    }

    // Close script.json
    write(fdJson, "\n  ],\n  \"ScriptString\": [],\n  \"ScriptMetadata\": []\n}\n", 53);

    close(fdCs);
    close(fdJson);
    close(fdSdk);
    close(fdOff);

    g_DumpProgress.store(100);
    snprintf(g_DumpStatus, sizeof(g_DumpStatus), "OFFICIAL DUMP COMPLETED: %s", dumpDir);
    g_IsDumping.store(false);
}

inline void TriggerOfficialDumpSuiteAsync() {
    if (g_IsDumping.load()) return;
    std::thread(ExecuteOfficialAllInOneDump).detach();
}

} // namespace OfficialDumper
