#ifndef PROTECTION_NATIVE_SECURITY_H
#define PROTECTION_NATIVE_SECURITY_H

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdint.h>

namespace NativeSecurity {

    // 1. Direct raw kernel syscall for mprotect (bypasses libc hooks & detections)
    inline int DirectMprotect(void* addr, size_t len, int prot) {
#if defined(__aarch64__)
        register long x0 __asm__("x0") = reinterpret_cast<long>(addr);
        register long x1 __asm__("x1") = static_cast<long>(len);
        register long x2 __asm__("x2") = static_cast<long>(prot);
        register long x8 __asm__("x8") = 226; // __NR_mprotect ARM64
        __asm__ __volatile__("svc #0" : "=r"(x0) : "r"(x0), "r"(x1), "r"(x2), "r"(x8) : "memory");
        return static_cast<int>(x0);
#elif defined(__arm__)
        register long r0 __asm__("r0") = reinterpret_cast<long>(addr);
        register long r1 __asm__("r1") = static_cast<long>(len);
        register long r2 __asm__("r2") = static_cast<long>(prot);
        register long r7 __asm__("r7") = 125; // __NR_mprotect ARM32
        __asm__ __volatile__("svc #0" : "=r"(r0) : "r"(r0), "r"(r1), "r"(r2), "r"(r7) : "memory");
        return static_cast<int>(r0);
#else
        return mprotect(addr, len, prot);
#endif
    }

    // 2. Disguise background thread as a legitimate engine thread
    inline void DisguiseThread(const char* name = "UnityMain") {
        pthread_setname_np(pthread_self(), name);
    }

    // 3. Prevent external debuggers (gdb/lldb) from attaching via ptrace
    inline void BlockDebuggerAttach() {
#if defined(__arm__) || defined(__aarch64__)
        ptrace(PTRACE_TRACEME, 0, 0, 0);
#endif
    }

    // 4. In-Memory Anti-Dump: Wipes ELF header magic bytes to stop memory dumpers
    inline void WipeElfHeader(void* symbolInModule) {
        if (!symbolInModule) return;
        Dl_info info;
        if (dladdr(symbolInModule, &info) && info.dli_fbase) {
            uintptr_t base = reinterpret_cast<uintptr_t>(info.dli_fbase);
            const long pageSize = sysconf(_SC_PAGESIZE);
            uintptr_t pageStart = base & ~(pageSize - 1);
            if (DirectMprotect(reinterpret_cast<void*>(pageStart), pageSize, PROT_READ | PROT_WRITE) == 0) {
                std::memset(reinterpret_cast<void*>(base), 0, 16);
                DirectMprotect(reinterpret_cast<void*>(pageStart), pageSize, PROT_READ);
            }
        }
    }

    // 5. Check if an address is mapped and safe to read
    inline bool IsAddressReadable(const void* addr) {
        if (!addr || reinterpret_cast<uintptr_t>(addr) < 0x10000) return false;
        unsigned char vec = 0;
        uintptr_t page = reinterpret_cast<uintptr_t>(addr) & ~(sysconf(_SC_PAGESIZE) - 1);
        return (mincore(reinterpret_cast<void*>(page), 1, &vec) == 0);
    }

    // 6. Master Security Initialization
    inline void Init(void* symbolInModule = nullptr) {
        DisguiseThread("UnityMain");
        BlockDebuggerAttach();
        if (symbolInModule) {
            WipeElfHeader(symbolInModule);
        }
    }
}

#endif // PROTECTION_NATIVE_SECURITY_H
