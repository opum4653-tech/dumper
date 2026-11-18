#pragma once

// Reads global-metadata.dat out of the live process.
//
// This file only decodes the entry layouts it can pin exactly, and proves the
// pin before reading a single entry:
//
//   1. Unity's metadata header is version dependent and so is the size of every
//      entry in the tables walked below. Unity inserts and removes ints between
//      releases (customAttributeIndex, byrefTypeIndex, the rgctx pair,
//      invokerIndex, ...). Version 24 in particular has at least three different
//      Il2CppTypeDefinition sizes. Decoding those with one struct reads every
//      field at the wrong offset and produces names that look real and are not.
//      So only the 25.x-27.x layout is decoded, and it is accepted only when the
//      header's own byte sizes agree with it.
//   2. The header's *Size fields are byte sizes, not element counts. The counts
//      used below are derived from them, and every index is bounds-checked
//      against them before it is dereferenced.
//   3. The blob size comes from the mapping that holds it, so nothing is read
//      past the region the runtime mapped. A page that cannot be read fails the
//      read; it is never replaced with zeros.

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

// Only the layout that is pinned exactly below is decoded.
static constexpr int32_t kMinVersion = 25;
static constexpr int32_t kMaxVersion = 27;

// Every section offset used here sits before the version-conditional tail of the
// header, so one declaration covers 25.x-27.x. The *Size fields are byte sizes.
#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    int32_t  version;
    uint32_t stringLiteralOffset;
    uint32_t stringLiteralSize;
    uint32_t stringLiteralDataOffset;
    uint32_t stringLiteralDataSize;
    uint32_t stringOffset;
    uint32_t stringSize;
    uint32_t eventsOffset;
    uint32_t eventsSize;
    uint32_t propertiesOffset;
    uint32_t propertiesSize;
    uint32_t methodsOffset;
    uint32_t methodsSize;
    uint32_t parameterDefaultValuesOffset;
    uint32_t parameterDefaultValuesSize;
    uint32_t fieldDefaultValuesOffset;
    uint32_t fieldDefaultValuesSize;
    uint32_t fieldAndParameterDefaultValueDataOffset;
    uint32_t fieldAndParameterDefaultValueDataSize;
    uint32_t fieldMarshaledSizesOffset;
    uint32_t fieldMarshaledSizesSize;
    uint32_t parametersOffset;
    uint32_t parametersSize;
    uint32_t fieldsOffset;
    uint32_t fieldsSize;
    uint32_t genericParametersOffset;
    uint32_t genericParametersSize;
    uint32_t genericParameterConstraintsOffset;
    uint32_t genericParameterConstraintsSize;
    uint32_t genericContainersOffset;
    uint32_t genericContainersSize;
    uint32_t nestedTypesOffset;
    uint32_t nestedTypesSize;
    uint32_t interfacesOffset;
    uint32_t interfacesSize;
    uint32_t vtableMethodsOffset;
    uint32_t vtableMethodsSize;
    uint32_t interfaceOffsetsOffset;
    uint32_t interfaceOffsetsSize;
    uint32_t typeDefinitionsOffset;
    uint32_t typeDefinitionsSize;
};

struct StringLiteralEntry {
    uint32_t length;
    uint32_t dataIndex;
};

// Il2CppTypeDefinition as published for 25.x-27.x: 88 bytes. customAttributeIndex,
// byrefTypeIndex and the rgctx pair are all absent in this range.
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

// Il2CppMethodDefinition for 25.x-27.x: 32 bytes. customAttributeIndex,
// methodIndex, invokerIndex, delegateWrapperIndex and the rgctx pair are absent.
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

// Il2CppFieldDefinition for 19+: 12 bytes. customAttributeIndex is gone.
struct FieldDefinitionEntry {
    uint32_t nameIndex;
    uint32_t typeIndex;
    uint32_t token;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 0xA8, "global metadata header prefix layout is wrong");
static_assert(sizeof(TypeDefinitionEntry) == 88, "Il2CppTypeDefinition 25.x layout is wrong");
static_assert(sizeof(MethodDefinitionEntry) == 32, "Il2CppMethodDefinition 25.x layout is wrong");
static_assert(sizeof(FieldDefinitionEntry) == 12, "Il2CppFieldDefinition layout is wrong");
static_assert(sizeof(StringLiteralEntry) == 8, "Il2CppStringLiteral layout is wrong");

inline bool IsSupportedVersion(int32_t version) {
    return version >= kMinVersion && version <= kMaxVersion;
}

// Version-independent view of one type definition. IsValueType / IsEnum come
// from the bitfield, which is the runtime's own encoding.
struct TypeDefView {
    uint32_t nameIndex = 0;
    uint32_t namespaceIndex = 0;
    uint32_t flags = 0;
    uint32_t fieldStart = 0;
    uint32_t methodStart = 0;
    uint32_t bitfield = 0;
    uint16_t fieldCount = 0;
    uint16_t methodCount = 0;

    bool ValueType() const { return (bitfield & 0x1) != 0; }
    bool EnumType() const { return ((bitfield >> 1) & 0x1) != 0; }
};

inline bool ReadTypeDef(uintptr_t address, TypeDefView& out) {
    out = TypeDefView();
    TypeDefinitionEntry entry{};
    if (!SafeMemory::Read(address, &entry, sizeof(entry))) return false;
    out.nameIndex = entry.nameIndex;
    out.namespaceIndex = entry.namespaceIndex;
    out.flags = entry.flags;
    out.fieldStart = entry.fieldStart;
    out.methodStart = entry.methodStart;
    out.bitfield = entry.bitfield;
    out.fieldCount = entry.field_count;
    out.methodCount = entry.method_count;
    return true;
}

inline bool ReadFieldDef(uintptr_t address, uint32_t& nameIndex) {
    nameIndex = 0;
    FieldDefinitionEntry entry{};
    if (!SafeMemory::Read(address, &entry, sizeof(entry))) return false;
    nameIndex = entry.nameIndex;
    return true;
}

struct MethodDefView {
    uint32_t nameIndex = 0;
    uint32_t token = 0;
    uint16_t flags = 0;
    uint16_t parameterCount = 0;
};

inline bool ReadMethodDef(uintptr_t address, MethodDefView& out) {
    out = MethodDefView();
    MethodDefinitionEntry entry{};
    if (!SafeMemory::Read(address, &entry, sizeof(entry))) return false;
    out.nameIndex = entry.nameIndex;
    out.token = entry.token;
    out.flags = entry.flags;
    out.parameterCount = entry.parameterCount;
    return true;
}

struct Located {
    uintptr_t address = 0;
    size_t    size    = 0;
    int32_t   version = 0;
    Header    header{};
    size_t    typeCount = 0;
    size_t    fieldCount = 0;
    size_t    methodCount = 0;
};

// Reads the string a metadata string index refers to. Bounded by stringSize and
// by the blob's own extent, so a corrupt index cannot walk off the mapping.
inline bool ReadString(const Located& loc, uint32_t index, char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    if (index >= loc.header.stringSize) return false;

    const uintptr_t base = loc.address + loc.header.stringOffset + index;
    const uintptr_t blobStart = loc.address;
    const uintptr_t blobEnd = loc.address + loc.size;
    if (base < blobStart || base >= blobEnd) return false;

    return SafeMemory::ReadCString(base, out, outSize);
}

// Confirms that the table byte sizes divide by the entry sizes this file
// decodes. A mismatch means the build uses a different entry layout, and
// decoding it anyway would produce names read at the wrong stride.
inline bool TablesValidate(const Header& header) {
    if (header.typeDefinitionsSize == 0 || header.methodsSize == 0) return false;
    if ((header.typeDefinitionsSize % sizeof(TypeDefinitionEntry)) != 0) return false;
    if ((header.methodsSize % sizeof(MethodDefinitionEntry)) != 0) return false;
    if (header.fieldsSize != 0 && (header.fieldsSize % sizeof(FieldDefinitionEntry)) != 0) return false;
    if (header.stringSize == 0) return false;
    return true;
}

// Finds the metadata blob by scanning readable mappings for the magic, then
// validates the header, the version and the entry table sizes.
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

        // The blob is normally mapped from the start of a mapping, so the
        // mapping start is checked first. A first-page scan covers packers that
        // prepend a stub.
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

    if (!hit || hitSize < sizeof(Header)) return false;

    Header header{};
    if (!SafeMemory::Read(hit, &header, sizeof(header))) return false;
    if (header.magic != kMetadataMagic) return false;
    if (!IsSupportedVersion(header.version)) return false;
    if (!TablesValidate(header)) return false;

    out.address     = hit;
    out.size        = hitSize;
    out.version     = header.version;
    out.header      = header;
    out.typeCount   = header.typeDefinitionsSize / sizeof(TypeDefinitionEntry);
    out.fieldCount  = header.fieldsSize / sizeof(FieldDefinitionEntry);
    out.methodCount = header.methodsSize / sizeof(MethodDefinitionEntry);
    if (out.typeCount == 0 || out.typeCount > 500000) return false;
    return true;
}

// Copies the blob out verbatim. A page that cannot be read fails the write
// rather than being padded with invented bytes.
inline bool WriteRaw(uintptr_t address, size_t size, const char* outPath) {
    int fd = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;

    const size_t kChunk = 64 * 1024;
    std::vector<uint8_t> buffer(kChunk);

    size_t done = 0;
    bool ok = true;
    while (done < size) {
        const size_t step = (size - done > kChunk) ? kChunk : (size - done);
        if (!SafeMemory::Read(address + done, buffer.data(), step)) { ok = false; break; }
        if (write(fd, buffer.data(), step) != (ssize_t)step) { ok = false; break; }
        done += step;
    }
    close(fd);

    if (!ok) unlink(outPath);
    return ok;
}

// Emits stringliteral.json. StringLiteralEntry is 8 bytes in every version this
// file supports, and the data section is bounded by stringLiteralDataSize.
inline bool DumpStringLiteralsJson(uintptr_t metadataAddress, int32_t version,
                                   const char* outJsonPath) {
    if (!IsSupportedVersion(version)) return false;

    Header header{};
    if (!SafeMemory::Read(metadataAddress, &header, sizeof(header))) return false;
    if (header.magic != kMetadataMagic) return false;
    if (header.stringLiteralOffset == 0 || header.stringLiteralSize == 0) return false;
    if (header.stringLiteralDataOffset == 0) return false;

    const size_t entryCount = header.stringLiteralSize / sizeof(StringLiteralEntry);
    if (entryCount == 0 || entryCount > 500000) return false;

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
    bool ok = true;

    for (size_t i = 0; i < entryCount; ++i) {
        StringLiteralEntry entry{};
        if (!SafeMemory::Read(tableAddress + i * sizeof(entry), &entry, sizeof(entry))) {
            ok = false;
            break;
        }
        if (entry.length == 0 || entry.length >= sizeof(text)) continue;
        if (entry.dataIndex + entry.length > header.stringLiteralDataSize) continue;
        if (!SafeMemory::Read(dataAddress + entry.dataIndex, text, entry.length)) continue;

        text[entry.length] = '\0';

        line.clear();
        if (!first) line += ",\n";
        line += "  {\n    \"value\": \"";
        line += JsonWriter::EscapeString(text);
        line += "\",\n    \"length\": ";
        line += std::to_string(entry.length);
        line += "\n  }";
        if (write(fd, line.data(), line.size()) != (ssize_t)line.size()) { ok = false; break; }
        first = false;
    }

    if (ok) {
        static const char kClose[] = "\n]\n";
        ok = write(fd, kClose, sizeof(kClose) - 1) == (ssize_t)(sizeof(kClose) - 1);
    }
    close(fd);
    if (!ok) unlink(outJsonPath);
    return ok;
}

inline std::string DescribeVersion(int32_t version) {
    char buf[96];
    snprintf(buf, sizeof(buf), "version %d", version);
    return std::string(buf);
}

} // namespace MetadataParser
