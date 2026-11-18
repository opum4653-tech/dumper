#ifndef STEALTH_BYPASS_H
#define STEALTH_BYPASS_H

#include <chrono>
#include <cstdint>
#include "../Core/MemoryUtils.h"
#include "../Struct/obfuscate.h"
#include "../Unity/Il2Cpp.h"

extern bool GuestResetZ;

// Guest Account Reset Patch with state caching
inline void UpdateResetGuestBypass() {
    static bool lastGuestState = false;
    static uintptr_t cachedResetGuestAddr = 0;
    static bool addrResolved = false;

    if (addrResolved && (GuestResetZ == lastGuestState)) return;

    if (!addrResolved) {
        cachedResetGuestAddr = (uintptr_t)Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW"), OBFUSCATE("GameConfig"), OBFUSCATE("get_ResetGuest"), 0);
        if (cachedResetGuestAddr == 0) return;
        addrResolved = true;
    }

    if (GuestResetZ != lastGuestState) {
        if (GuestResetZ) {
            MemoryPatch::createWithHex(cachedResetGuestAddr, OBFUSCATE("20 00 80 52 C0 03 5F D6")).Modify();
        } else {
            MemoryPatch::createWithHex(cachedResetGuestAddr, OBFUSCATE("00 00 80 52 C0 03 5F D6")).Modify();
        }
        lastGuestState = GuestResetZ;
    }
}

// Rate Limiter Helper to prevent packet flood & server flag
class RateLimiter {
public:
    static bool CanSendPacket(uint64_t intervalMs) {
        static auto lastSendTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSendTime).count();
        if (elapsed >= intervalMs) {
            lastSendTime = now;
            return true;
        }
        return false;
    }
};

#endif // STEALTH_BYPASS_H
