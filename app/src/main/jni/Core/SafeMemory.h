#pragma once

// Address-space safe memory access for the current process.
//
// A dumper walks pointers it does not own, so a raw dereference eventually hits
// an unmapped page and takes the game down with it. Two layers prevent that:
//
//   1. A snapshot of /proc/self/maps, kept sorted, answers "is this range mapped
//      and readable?" without touching the memory itself.
//   2. A read goes through std::memcpy() only when the range check passes.
//      Otherwise /proc/self/mem is tried, and if that fails too the read is
//      reported as failed instead of faulting.
//
// The snapshot must be refreshed when the target maps new regions; Refresh()
// does that and is cheap enough to call between dump phases.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>

namespace SafeMemory {

struct Mapping {
    uintptr_t start;
    uintptr_t end;
    bool      readable;
    bool      writable;
    bool      executable;
};

namespace detail {

inline std::vector<Mapping>& Mappings() {
    static std::vector<Mapping> s_maps;
    return s_maps;
}

inline bool& Loaded() {
    static bool s_loaded = false;
    return s_loaded;
}

inline int& MemFd() {
    static int s_fd = -1;
    return s_fd;
}

inline bool ParseMaps(std::vector<Mapping>& out) {
    out.clear();

    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0, end = 0;
        char perms[5] = {0};
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) != 3) continue;
        if (end <= start) continue;

        Mapping m{};
        m.start      = start;
        m.end        = end;
        m.readable   = (perms[0] == 'r');
        m.writable   = (perms[1] == 'w');
        m.executable = (perms[2] == 'x');
        out.push_back(m);
    }

    fclose(fp);

    std::sort(out.begin(), out.end(),
              [](const Mapping& a, const Mapping& b) { return a.start < b.start; });
    return !out.empty();
}

} // namespace detail

// Re-reads /proc/self/maps. Call after the runtime maps new regions.
inline bool Refresh() {
    bool ok = detail::ParseMaps(detail::Mappings());
    detail::Loaded() = ok;
    return ok;
}

inline const std::vector<Mapping>& All() {
    if (!detail::Loaded()) Refresh();
    return detail::Mappings();
}

// True when [addr, addr+size) is fully inside one readable mapping.
inline bool IsReadable(uintptr_t addr, size_t size) {
    if (addr == 0 || size == 0) return false;
    const uintptr_t end = addr + size;
    if (end < addr) return false; // wrapped

    const std::vector<Mapping>& maps = All();
    auto it = std::upper_bound(maps.begin(), maps.end(), addr,
                               [](uintptr_t value, const Mapping& m) { return value < m.start; });
    if (it == maps.begin()) return false;
    --it;
    return it->readable && addr >= it->start && end <= it->end;
}

inline bool IsExecutable(uintptr_t addr) {
    const std::vector<Mapping>& maps = All();
    auto it = std::upper_bound(maps.begin(), maps.end(), addr,
                               [](uintptr_t value, const Mapping& m) { return value < m.start; });
    if (it == maps.begin()) return false;
    --it;
    return addr >= it->start && addr < it->end && it->executable;
}

// Returns the mapping containing addr, or nullptr.
inline const Mapping* Find(uintptr_t addr) {
    const std::vector<Mapping>& maps = All();
    auto it = std::upper_bound(maps.begin(), maps.end(), addr,
                               [](uintptr_t value, const Mapping& m) { return value < m.start; });
    if (it == maps.begin()) return nullptr;
    --it;
    return (addr >= it->start && addr < it->end) ? &(*it) : nullptr;
}

inline int MemFd() {
    if (detail::MemFd() < 0) {
        detail::MemFd() = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
    }
    return detail::MemFd();
}

// Crash-proof read. Never dereferences an address that is not checked first.
inline bool Read(uintptr_t addr, void* dest, size_t size) {
    if (!dest || size == 0 || addr < 0x1000) return false;

    if (IsReadable(addr, size)) {
        std::memcpy(dest, reinterpret_cast<const void*>(addr), size);
        return true;
    }

    // Fallback for ranges that span two mappings (a segment can legitimately do
    // this when the loader splits it): walk per byte-run.
    const std::vector<Mapping>& maps = All();
    uintptr_t cursor = addr;
    size_t remaining = size;
    uint8_t* out = static_cast<uint8_t*>(dest);

    auto it = std::lower_bound(maps.begin(), maps.end(), cursor,
                               [](const Mapping& m, uintptr_t value) { return m.start < value; });
    if (it != maps.begin()) --it;
    while (it != maps.end() && it->end <= cursor) ++it;

    while (remaining > 0 && it != maps.end() && it->start <= cursor && it->readable) {
        const uintptr_t avail = it->end - cursor;
        const size_t step = (avail < remaining) ? (size_t)avail : remaining;
        std::memcpy(out, reinterpret_cast<const void*>(cursor), step);
        out += step;
        cursor += step;
        remaining -= step;
        if (remaining == 0) return true;
        ++it;
        while (it != maps.end() && it->end <= cursor) ++it;
        if (it == maps.end() || it->start != cursor) break;
    }

    const int fd = MemFd();
    if (fd >= 0) {
        ssize_t n = pread(fd, dest, size, (off_t)addr);
        return n == (ssize_t)size;
    }
    return false;
}

template <typename T>
inline bool Read(uintptr_t addr, T* out) {
    return out && Read(addr, static_cast<void*>(out), sizeof(T));
}

inline bool ReadUint32(uintptr_t addr, uint32_t* outVal) {
    return Read(addr, outVal, sizeof(uint32_t));
}

inline bool ReadUint64(uintptr_t addr, uint64_t* outVal) {
    return Read(addr, outVal, sizeof(uint64_t));
}

inline bool ReadPtr(uintptr_t addr, uintptr_t* outVal) {
    return Read(addr, outVal, sizeof(uintptr_t));
}

// Null-terminated string copy with a hard length ceiling.
inline bool ReadCString(uintptr_t addr, char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    if (addr == 0) return false;

    size_t written = 0;
    while (written + 1 < outSize) {
        char c = 0;
        if (!Read(addr + written, &c, 1)) break;
        out[written++] = c;
        if (c == '\0') return true;
    }
    out[written] = '\0';
    return written > 0;
}

inline bool IsValidReadablePtr(uintptr_t addr, size_t size = sizeof(void*)) {
    if (addr < 0x1000) return false;
    return IsReadable(addr, size);
}

inline bool IsValidStringPtr(const char* str, size_t maxCheck = 128) {
    if (!str || reinterpret_cast<uintptr_t>(str) < 0x1000) return false;
    char buf[128];
    if (!ReadCString(reinterpret_cast<uintptr_t>(str), buf, maxCheck)) return false;
    for (size_t i = 0; buf[i] != '\0' && i < maxCheck; ++i) {
        if ((unsigned char)buf[i] < 32 || (unsigned char)buf[i] > 126) return false;
    }
    return buf[0] != '\0';
}

} // namespace SafeMemory
