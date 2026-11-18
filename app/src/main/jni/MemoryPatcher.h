#pragma once
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <string>
#include <stdint.h>

#define NIN_PATCHER_VERSION "2.0.0"

class MemoryPatcher {
private:
    uintptr_t m_addr;
    uint8_t m_patchBytes[64];
    uint8_t m_origBytes[64];
    size_t m_len;
    bool m_origSaved;
    bool m_applied;

    static inline int hexCharToInt(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    static size_t parseHex(const std::string& hex, uint8_t* out, size_t maxLen) {
        size_t outIdx = 0;
        int high = -1;
        for (char c : hex) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
            int val = hexCharToInt(c);
            if (val < 0) continue;
            if (high == -1) {
                high = val;
            } else {
                if (outIdx < maxLen) {
                    out[outIdx++] = static_cast<uint8_t>((high << 4) | val);
                }
                high = -1;
            }
        }
        return outIdx;
    }

public:
    MemoryPatcher(uintptr_t addr, const std::string& hexBytes) 
        : m_addr(addr), m_len(0), m_origSaved(false), m_applied(false) {
        std::memset(m_patchBytes, 0, sizeof(m_patchBytes));
        std::memset(m_origBytes, 0, sizeof(m_origBytes));
        if (addr >= 0x10000) {
            m_len = parseHex(hexBytes, m_patchBytes, sizeof(m_patchBytes));
        }
    }

    bool Modify() {
        if (m_applied || m_addr < 0x10000 || m_len == 0) return false;

        if (!m_origSaved) {
            std::memcpy(m_origBytes, reinterpret_cast<const void*>(m_addr), m_len);
            m_origSaved = true;
        }

        int memFd = open("/proc/self/mem", O_RDWR);
        if (memFd != -1) {
            if (pwrite(memFd, m_patchBytes, m_len, (off_t)m_addr) == (ssize_t)m_len) {
                close(memFd);
                __builtin___clear_cache(reinterpret_cast<char*>(m_addr), reinterpret_cast<char*>(m_addr + m_len));
                m_applied = true;
                return true;
            }
            close(memFd);
        }

        static const long pageSize = sysconf(_SC_PAGESIZE);
        uintptr_t start = m_addr & ~(pageSize - 1);
        uintptr_t end = (m_addr + m_len + pageSize - 1) & ~(pageSize - 1);
        size_t totalLen = end - start;

        if (mprotect(reinterpret_cast<void*>(start), totalLen, PROT_READ | PROT_WRITE) == 0) {
            std::memcpy(reinterpret_cast<void*>(m_addr), m_patchBytes, m_len);
            mprotect(reinterpret_cast<void*>(start), totalLen, PROT_READ | PROT_EXEC);
            __builtin___clear_cache(reinterpret_cast<char*>(m_addr), reinterpret_cast<char*>(m_addr + m_len));
            m_applied = true;
            return true;
        }
        return false;
    }

    bool Restore() {
        if (!m_applied || !m_origSaved || m_addr < 0x10000 || m_len == 0) return false;

        static const long pageSize = sysconf(_SC_PAGESIZE);
        uintptr_t start = m_addr & ~(pageSize - 1);
        uintptr_t end = (m_addr + m_len + pageSize - 1) & ~(pageSize - 1);
        size_t totalLen = end - start;

        bool protOk = (mprotect(reinterpret_cast<void*>(start), totalLen, PROT_READ | PROT_WRITE | PROT_EXEC) == 0);
        if (!protOk) {
            protOk = (mprotect(reinterpret_cast<void*>(start), totalLen, PROT_READ | PROT_WRITE) == 0);
        }
        if (!protOk) return false;

        std::memcpy(reinterpret_cast<void*>(m_addr), m_origBytes, m_len);
        mprotect(reinterpret_cast<void*>(start), totalLen, PROT_READ | PROT_EXEC);
        __builtin___clear_cache(reinterpret_cast<char*>(m_addr), reinterpret_cast<char*>(m_addr + m_len));

        m_applied = false;
        return true;
    }

    static void Init() {}
    static void Cleanup() {}

    bool IsApplied() const { return m_applied; }
    bool IsValid() const { return (m_addr >= 0x10000 && m_len > 0); }
};

#define NIN_PATCH(addr, hex) MemoryPatcher((uintptr_t)(addr), hex).Modify()
#define NIN_PATCH_SAFE(addr, hex) do{ \
    uintptr_t __a = (uintptr_t)(addr); \
    if(__a >= 0x10000) MemoryPatcher(__a, hex).Modify(); \
}while(0)
