#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>
#include "SafeMemory.h"
#include "JsonWriter.h"

namespace MetadataParser {

#pragma pack(push, 1)
struct MetadataSectionEntry {
    uint32_t offset;
    uint32_t size;
};

struct Il2CppMetadataHeader {
    uint32_t magic;                    // 0xFAB11BAF
    int32_t  version;                  // Typically 24, 27, 29, 31
    uint32_t stringLiteralOffset;
    uint32_t stringLiteralCount;
    uint32_t stringLiteralDataOffset;
    uint32_t stringLiteralDataCount;
    uint32_t stringOffset;
    uint32_t stringCount;
    uint32_t eventsOffset;
    uint32_t eventsCount;
    uint32_t propertiesOffset;
    uint32_t propertiesCount;
    uint32_t methodsOffset;
    uint32_t methodsCount;
    uint32_t parameterDefaultValuesOffset;
    uint32_t parameterDefaultValuesCount;
    uint32_t fieldDefaultValuesOffset;
    uint32_t fieldDefaultValuesCount;
    uint32_t fieldAndParameterDefaultValueDataOffset;
    uint32_t fieldAndParameterDefaultValueDataCount;
    uint32_t fieldMarshaledSizesOffset;
    uint32_t fieldMarshaledSizesCount;
    uint32_t parametersOffset;
    uint32_t parametersCount;
    uint32_t fieldsOffset;
    uint32_t fieldsCount;
    uint32_t genericParametersOffset;
    uint32_t genericParametersCount;
    uint32_t genericParameterConstraintsOffset;
    uint32_t genericParameterConstraintsCount;
    uint32_t genericContainersOffset;
    uint32_t genericContainersCount;
    uint32_t nestedTypesOffset;
    uint32_t nestedTypesCount;
    uint32_t interfacesOffset;
    uint32_t interfacesCount;
    uint32_t vtableMethodsOffset;
    uint32_t vtableMethodsCount;
    uint32_t interfaceOffsetsOffset;
    uint32_t interfaceOffsetsCount;
    uint32_t typeDefinitionsOffset;
    uint32_t typeDefinitionsCount;
};

struct Il2CppStringLiteralEntry {
    uint32_t length;
    uint32_t dataIndex;
};
#pragma pack(pop)

inline size_t CalculateExactMetadataSize(uintptr_t headerAddr) {
    Il2CppMetadataHeader header;
    if (!SafeMemory::Read(headerAddr, &header, sizeof(header))) return 0;
    if (header.magic != 0xFAB11BAF) return 0;

    // Scan all offset+size pairs in the header to find the maximum extent of the file
    const uint32_t* rawFields = reinterpret_cast<const uint32_t*>(&header.stringLiteralOffset);
    size_t fieldCount = (sizeof(Il2CppMetadataHeader) - offsetof(Il2CppMetadataHeader, stringLiteralOffset)) / sizeof(uint32_t);

    uint32_t maxExtent = sizeof(Il2CppMetadataHeader);
    for (size_t i = 0; i + 1 < fieldCount; i += 2) {
        uint32_t off = rawFields[i];
        uint32_t sz  = rawFields[i + 1];
        if (off > 0 && off < 0x20000000 && sz < 0x20000000) {
            uint32_t extent = off + sz;
            if (extent > maxExtent && extent < 0x30000000) {
                maxExtent = extent;
            }
        }
    }
    return maxExtent;
}

inline bool DumpStringLiteralsJson(uintptr_t metadataAddr, const char* outJsonPath) {
    Il2CppMetadataHeader header;
    if (!SafeMemory::Read(metadataAddr, &header, sizeof(header))) return false;
    if (header.magic != 0xFAB11BAF) return false;

    if (header.stringLiteralOffset == 0 || header.stringLiteralCount == 0 ||
        header.stringLiteralDataOffset == 0 || header.stringLiteralDataCount == 0) {
        return false;
    }

    int fd = open(outJsonPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;

    write(fd, "[\n", 2);

    size_t litCount = header.stringLiteralCount / sizeof(Il2CppStringLiteralEntry);
    if (litCount > 100000) litCount = 100000; // Bound check

    uintptr_t litTableAddr = metadataAddr + header.stringLiteralOffset;
    uintptr_t dataBaseAddr = metadataAddr + header.stringLiteralDataOffset;

    std::vector<Il2CppStringLiteralEntry> entries(litCount);
    if (!SafeMemory::Read(litTableAddr, entries.data(), litCount * sizeof(Il2CppStringLiteralEntry))) {
        close(fd);
        return false;
    }

    std::string lineBuf;
    lineBuf.reserve(512);
    char strBuffer[2048];
    bool first = true;

    for (size_t i = 0; i < litCount; ++i) {
        uint32_t len = entries[i].length;
        uint32_t idx = entries[i].dataIndex;
        if (len == 0 || len >= sizeof(strBuffer)) continue;
        if (idx + len > header.stringLiteralDataCount) continue;

        if (SafeMemory::Read(dataBaseAddr + idx, strBuffer, len)) {
            strBuffer[len] = '\0';
            std::string escaped = JsonWriter::EscapeString(strBuffer);
            
            lineBuf.clear();
            if (!first) lineBuf += ",\n";
            lineBuf += "  {\n    \"value\": \"";
            lineBuf += escaped;
            lineBuf += "\",\n    \"length\": ";
            lineBuf += std::to_string(len);
            lineBuf += "\n  }";
            write(fd, lineBuf.data(), lineBuf.size());
            first = false;
        }
    }

    write(fd, "\n]\n", 3);
    close(fd);
    return true;
}

} // namespace MetadataParser
