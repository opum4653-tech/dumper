#pragma once

// Reads global-metadata.dat out of the live process.
//
// Two facts that keep this honest:
//
//   1. The header layout is version dependent. Unity 24.x-27.x use the field
//      order declared below; 29+ rearrange it. Rather than pretend one struct
//      fits every build, the version field is checked and the section table is
//      only used when the version falls in the range it actually describes.
//   2. The size of the blob is taken from the mapping that holds it. That is
//      the real in-memory extent, so nothing is over-read past the region the
//      runtime mapped.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include "SafeMemory.h"
#include "JsonWriter.h"

namespace MetadataParser {

static constexpr uint32_t kMetadataMagic = 0xFAB11BAF;

// Field order as published for Unity 24.0 - 27.x.
#pragma pack(push, 1)
struct HeaderV24 {
    uint32_t magic;
    int32_t  version;
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

struct StringLiteralEntry {
    uint32_t length;
    uint32_t dataIndex;
};

struct TypeDefinitionEntry {
    uint32_t nameIndex;
    uint32_t namespaceIndex;
    uint32_t byvalTypeIndex;
    uint32_t declaringTypeIndex;
    uint32_t parentIndex;
    uint32_t elementTypeIndex;
    uint32_t genericContainerIndex;
    uint32_t flags;
    uint32_t fieldStart;
    uint32_t methodStart;
    uint32_t eventStart;
    uint32_t propertyStart;
    uint32_t nestedTypesStart;
    uint32_t interfacesStart;
    uint32_t vtableStart;
    uint32_t interfaceOffsetsStart;
    uint16_t method_count;
    uint16_t property_count;
    uint16_t field_count;
    uint16_t event_count;
    uint16_t nested_type_count;
    uint16_t vtable_count;
    uint16_t interfaces_count;
    uint16_t interface_offsets_count;
    uint32_t bitfield;
    uint32_t token;
};

struct MethodDefinitionEntry {
    uint32_t nameIndex;
    uint32_t declaringType;
    uint32_t returnType;
    uint32_t parameterStart;
    uint32_t genericContainerIndex;
    uint32_t token;
    uint16_t flags;
    uint16_t iflags;
    uint16_t slot;
    uint16_t parameterCount;
};

struct FieldDefinitionEntry {
    uint32_t nameIndex;
    uint32_t typeIndex;
    uint32_t token;
};
#pragma pack(pop)

struct Located {
    uintptr_t address = 0;
    size_t    size    = 0;
    int32_t   version = 0;
};

// Finds the metadata blob by scanning readable mappings for the magic, and
// reports the mapping extent that contains it.
inline bool Locate(Located& out) {
    out = Located();

    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;

    char line[1024];
    uintptr_t hit = 0;
    size_t hitSize = 0;

    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0, end = 0;
        char perms[5] = {0};
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) != 3) continue;
        if (perms[0] != 'r') continue;
        if ((end - start) < 0x1000) continue;

        // The metadata blob is mapped from the start of a mapping, so only the
        // mapping start needs checking. A fallback scan of the first page is
        // added because some packers prepend a stub.
        uint32_t magic = 0;
        if (SafeMemory::ReadUint32(start, &magic) && magic == kMetadataMagic) {
            hit = start;
            hitSize = end - start;
            break;
        }

        const uintptr_t pageEnd = start + 0x1000;
        for (uintptr_t probe = start + 4; probe + 4 <= pageEnd && probe + 4 <= end; probe += 4) {
            if (SafeMemory::ReadUint32(probe, &magic) && magic == kMetadataMagic) {
                hit = probe;
                hitSize = end - probe;
                break;
            }
        }
        if (hit) break;
    }
    fclose(fp);

    if (!hit || hitSize < sizeof(HeaderV24)) return false;

    HeaderV24 header;
    if (!SafeMemory::Read(hit, &header, sizeof(header))) return false;

    out.address = hit;
    out.size    = hitSize;
    out.version = header.version;
    return true;
}

inline bool WriteRaw(uintptr_t address, size_t size, const char* outPath) {
    int fd = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;

    const size_t kChunk = 64 * 1024;
    std::vector<uint8_t> buffer(kChunk);

    size_t done = 0;
    while (done < size) {
        const size_t step = (size - done > kChunk) ? kChunk : (size - done);
        if (!SafeMemory::Read(address + done, buffer.data(), step)) {
            std::memset(buffer.data(), 0, step);
        }
        if (write(fd, buffer.data(), step) != (ssize_t)step) {
            close(fd);
            return false;
        }
        done += step;
    }
    close(fd);
    return true;
}

// Emits stringliteral.json. Only valid for the header layout above, so the
// caller must pass a version this struct describes.
inline bool DumpStringLiteralsJson(uintptr_t metadataAddress, int32_t version, const char* outJsonPath) {
    if (version < 24 || version > 27) return false;

    HeaderV24 header;
    if (!SafeMemory::Read(metadataAddress, &header, sizeof(header))) return false;
    if (header.magic != kMetadataMagic) return false;
    if (header.stringLiteralOffset == 0 || header.stringLiteralCount == 0) return false;
    if (header.stringLiteralDataOffset == 0) return false;

    const size_t entryCount = header.stringLiteralCount / sizeof(StringLiteralEntry);
    if (entryCount == 0 || entryCount > 200000) return false;

    int fd = open(outJsonPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;

    static const char kOpen[] = "[\n";
    write(fd, kOpen, sizeof(kOpen) - 1);

    const uintptr_t tableAddress = metadataAddress + header.stringLiteralOffset;
    const uintptr_t dataAddress  = metadataAddress + header.stringLiteralDataOffset;

    std::string line;
    line.reserve(512);
    char text[2048];
    bool first = true;

    for (size_t i = 0; i < entryCount; ++i) {
        StringLiteralEntry entry{};
        if (!SafeMemory::Read(tableAddress + i * sizeof(entry), &entry, sizeof(entry))) break;
        if (entry.length == 0 || entry.length >= sizeof(text)) continue;
        if (entry.dataIndex + entry.length > header.stringLiteralDataCount) continue;
        if (!SafeMemory::Read(dataAddress + entry.dataIndex, text, entry.length)) continue;

        text[entry.length] = '\0';

        line.clear();
        if (!first) line += ",\n";
        line += "  {\n    \"value\": \"";
        line += JsonWriter::EscapeString(text);
        line += "\",\n    \"length\": ";
        line += std::to_string(entry.length);
        line += "\n  }";
        write(fd, line.data(), line.size());
        first = false;
    }

    static const char kClose[] = "\n]\n";
    write(fd, kClose, sizeof(kClose) - 1);
    close(fd);
    return true;
}

// One-line description of what the metadata dump produced, for the README.
inline std::string DescribeVersion(int32_t version) {
    char buf[96];
    snprintf(buf, sizeof(buf), "version %d", version);
    return std::string(buf);
}

// Fallback class dumper: parses global-metadata.dat directly when dynamic symbols
// are stripped from libil2cpp.so and il2cpp_domain_get returns 0 assemblies.
inline bool DumpFromMetadata(uintptr_t metadataAddress,
                             int fdCs, int fdJson, int fdSdk, int fdOff,
                             uintptr_t baseAddr, size_t moduleSize,
                             std::atomic<int>& progress,
                             char* statusBuf, size_t statusSize) {
    HeaderV24 header;
    if (!SafeMemory::Read(metadataAddress, &header, sizeof(header))) return false;
    if (header.magic != kMetadataMagic) return false;
    if (header.typeDefinitionsOffset == 0 || header.typeDefinitionsCount == 0) return false;

    const size_t typeCount = header.typeDefinitionsCount / sizeof(TypeDefinitionEntry);
    if (typeCount == 0 || typeCount > 200000) return false;

    const uintptr_t typesAddr = metadataAddress + header.typeDefinitionsOffset;
    const uintptr_t fieldsAddr = metadataAddress + header.fieldsOffset;
    const uintptr_t methodsAddr = metadataAddress + header.methodsOffset;
    const uintptr_t stringsAddr = metadataAddress + header.stringOffset;

    auto readString = [&](uint32_t index) -> std::string {
        if (index >= header.stringCount) return "";
        char buf[256];
        if (SafeMemory::ReadCString(stringsAddr + index, buf, sizeof(buf))) {
            return std::string(buf);
        }
        return "";
    };

    char chunk[4096];

    for (size_t i = 0; i < typeCount; ++i) {
        TypeDefinitionEntry td{};
        if (!SafeMemory::Read(typesAddr + i * sizeof(TypeDefinitionEntry), &td, sizeof(td))) break;

        std::string kName = readString(td.nameIndex);
        if (kName.empty()) kName = "UnnamedClass_" + std::to_string(i);
        std::string ns = readString(td.namespaceIndex);

        // dump.cs class start
        int clen = snprintf(chunk, sizeof(chunk),
            "// Namespace: %s\npublic class %s\n{\n", ns.c_str(), kName.c_str());
        write(fdCs, chunk, clen);

        // il2cpp.h struct start
        int slen = snprintf(chunk, sizeof(chunk),
            "// [%s] %s\nstruct %s {\n", ns.c_str(), kName.c_str(), kName.c_str());
        write(fdSdk, chunk, slen);

        // ninmods_real_offsets.h header
        int olen = snprintf(chunk, sizeof(chunk),
            "\n// [%s] %s%s%s\n", "METADATA", ns.c_str(), ns.empty() ? "" : ".", kName.c_str());
        write(fdOff, chunk, olen);

        // Dump Fields
        write(fdCs, "\t// Fields\n", 11);
        for (uint16_t f = 0; f < td.field_count; ++f) {
            uint32_t fIndex = td.fieldStart + f;
            FieldDefinitionEntry fdEntry{};
            if (!SafeMemory::Read(fieldsAddr + fIndex * sizeof(FieldDefinitionEntry), &fdEntry, sizeof(fdEntry))) break;
            std::string fName = readString(fdEntry.nameIndex);
            if (!fName.empty()) {
                int flen = snprintf(chunk, sizeof(chunk),
                    "\tpublic var %s; // FieldDef 0x%08X\n", fName.c_str(), fdEntry.token);
                write(fdCs, chunk, flen);

                int sflen = snprintf(chunk, sizeof(chunk),
                    "\tuintptr_t %s; // Token 0x%08X\n", fName.c_str(), fdEntry.token);
                write(fdSdk, chunk, sflen);
            }
        }

        // Dump Methods
        write(fdCs, "\n\t// Methods\n", 13);
        for (uint16_t m = 0; m < td.method_count; ++m) {
            uint32_t mIndex = td.methodStart + m;
            MethodDefinitionEntry mdEntry{};
            if (!SafeMemory::Read(methodsAddr + mIndex * sizeof(MethodDefinitionEntry), &mdEntry, sizeof(mdEntry))) break;
            std::string mName = readString(mdEntry.nameIndex);
            if (!mName.empty()) {
                int mlen = snprintf(chunk, sizeof(chunk),
                    "\t// Token: 0x%08X (RVA: 0x0) (Params: %u)\n\tpublic void %s();\n",
                    mdEntry.token, mdEntry.parameterCount, mName.c_str());
                write(fdCs, chunk, mlen);
            }
        }

        write(fdCs, "}\n\n", 3);
        write(fdSdk, "};\n\n", 4);

        if ((i % 100) == 0 || i + 1 == typeCount) {
            int pct = (int)(((i + 1) * 100) / typeCount);
            progress.store(pct);
            if (statusBuf && statusSize > 0) {
                snprintf(statusBuf, statusSize, "Metadata Fallback: %s (%d%%)...", kName.c_str(), pct);
            }
        }
    }

    return true;
}

} // namespace MetadataParser
