#pragma once
#include <cstdint>
#include <cstddef>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

namespace SafeMemory {

inline int GetMemFd() {
    static int s_memFd = -1;
    if (s_memFd < 0) {
        s_memFd = open("/proc/self/mem", O_RDONLY);
    }
    return s_memFd;
}

// Crash-proof memory read: If addr is unmapped or protected, returns false instead of crashing with SIGSEGV
inline bool Read(uintptr_t addr, void* dest, size_t size) {
    if (!dest || size == 0 || addr < 0x1000) return false;
    int fd = GetMemFd();
    if (fd >= 0) {
        ssize_t n = pread(fd, dest, size, (off_t)addr);
        return (n == (ssize_t)size);
    }
    return false;
}

inline bool ReadUint32(uintptr_t addr, uint32_t* outVal) {
    return Read(addr, outVal, sizeof(uint32_t));
}

inline bool ReadUint64(uintptr_t addr, uint64_t* outVal) {
    return Read(addr, outVal, sizeof(uint64_t));
}

} // namespace SafeMemory
