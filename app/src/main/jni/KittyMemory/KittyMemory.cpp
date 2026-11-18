#include "KittyMemory.h"
#include <map>
#include <inttypes.h>
#include <android/log.h>

namespace KittyMemory {

    int setAddressProtection(void *address, size_t length, int protection)
    {
        uintptr_t pageStart = _PAGE_START_OF_(address);
        uintptr_t pageLen = _PAGE_LEN_OF_(address, length);
        return mprotect(reinterpret_cast<void *>(pageStart), pageLen, protection);
    }

    bool memWrite(void *address, const void *buffer, size_t len)
    {
        if (!address || !buffer || len < 1 || len > INT_MAX) return false;
        ProcMap addressMap = getAddressMap(address);
        if (!addressMap.isValid()) return false;

        if (addressMap.protection & PROT_WRITE) {
            memcpy(address, buffer, len);
            return true;
        }

        if (setAddressProtection(address, len, addressMap.protection | PROT_WRITE) == 0) {
            memcpy(address, buffer, len);
            setAddressProtection(address, len, addressMap.protection);
            return true;
        }
        return false;
    }

    bool memRead(void *buffer, const void *address, size_t len)
    {
        if (!address || !buffer || len < 1 || len > INT_MAX) return false;
        ProcMap addressMap = getAddressMap(address);
        if (!addressMap.isValid()) return false;

        if (addressMap.protection & PROT_READ) {
            memcpy(buffer, address, len);
            return true;
        }

        if (setAddressProtection(const_cast<void *>(address), len, addressMap.protection | PROT_READ) == 0) {
            memcpy(buffer, address, len);
            setAddressProtection(const_cast<void *>(address), len, addressMap.protection);
            return true;
        }
        return false;
    }

    std::string read2HexStr(const void *address, size_t len)
    {
        if (!address || len < 1) return "";
        std::string hexStr;
        hexStr.reserve(len * 2);
        const unsigned char *p = static_cast<const unsigned char *>(address);
        for (size_t i = 0; i < len; ++i) {
            char buf[3];
            snprintf(buf, sizeof(buf), "%02X", p[i]);
            hexStr.append(buf);
        }
        return hexStr;
    }

    ProcMap getAddressMap(const void *address)
    {
        if (!address) return ProcMap();
        std::vector<ProcMap> maps = getAllMaps();
        uintptr_t addr = reinterpret_cast<uintptr_t>(address);
        for (size_t i = 0; i < maps.size(); ++i) {
            if (addr >= maps[i].startAddress && addr < maps[i].endAddress) {
                return maps[i];
            }
        }
        return ProcMap();
    }

    ProcMap getLibraryMap(const char *libraryName)
    {
        if (!libraryName) return ProcMap();
        std::vector<ProcMap> maps = getAllMaps();
        for (size_t i = 0; i < maps.size(); ++i) {
            if (strstr(maps[i].pathname.c_str(), libraryName)) {
                return maps[i];
            }
        }
        return ProcMap();
    }

    std::vector<ProcMap> getAllMaps()
    {
        std::vector<ProcMap> maps;
        FILE *fp = fopen("/proc/self/maps", "re");
        if (!fp) return maps;

        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            ProcMap map;
            char perms[5];
            char dev[12];
            char pathname[256] = "";
            unsigned long inode = 0;

            int fields = sscanf(line, "%" PRIxPTR "-%" PRIxPTR " %4s %" PRIxPTR " %11s %lu %255s",
                                &map.startAddress, &map.endAddress, perms, &map.offset, dev, &inode, pathname);

            if (fields >= 2) {
                map.length = map.endAddress - map.startAddress;
                map.protection = PROT_NONE;
                if (perms[0] == 'r') map.protection |= PROT_READ;
                if (perms[1] == 'w') map.protection |= PROT_WRITE;
                if (perms[2] == 'x') map.protection |= PROT_EXEC;
                map.is_shared = (perms[3] == 's');
                map.is_private = (perms[3] == 'p');
                map.pathname = pathname;
                maps.push_back(map);
            }
        }
        fclose(fp);
        return maps;
    }

}