// Il2CppDumper.h - the dump engine.
//
// Every value written to disk comes from a runtime query:
//
//   class name / namespace   il2cpp_class_get_name, il2cpp_class_get_namespace
//   parent                   il2cpp_class_get_parent
//   field offset             il2cpp_field_get_offset
//   field type               il2cpp_field_get_type + il2cpp_type_get_name
//   method code pointer      MethodInfo::methodPointer (first field)
//   method params            il2cpp_method_get_param_count / _get_param
//   instance size            il2cpp_class_instance_size
//
// An RVA is written only when the code pointer actually falls inside
// libil2cpp.so's PT_LOAD range. Pointers into a thunk, a generic instantiation
// or an AOT module owned by another file are reported as what they are, never
// subtracted into a plausible-looking number.

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <atomic>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <android/log.h>

#include "../../Unity/Il2Cpp.h"
#include "../../Unity/Il2Cpp/Il2CppFlags.h"
#include "../../Core/SafeMemory.h"
#include "../../Core/ElfImage.h"
#include "../../Core/ElfReconstructor.h"
#include "../../Core/MetadataParser.h"
#include "../../Core/RvaResolver.h"
#include "../../Core/JsonWriter.h"
#include "DumpWriter.h"

namespace Dumper {

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
inline std::atomic<bool> g_busy{false};
inline std::atomic<int>  g_progress{0};
inline std::mutex        g_statusMutex;

inline char g_status[192] = "IDLE";
inline char g_outputDir[192] = "";

inline void SetStatus(const char* text) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    snprintf(g_status, sizeof(g_status), "%s", text ? text : "");
}

inline void SetStatusf(const char* fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    SetStatus(buf);
}

inline const char* Status() { return g_status; }
inline int Progress() { return g_progress.load(); }
inline bool Busy() { return g_busy.load(); }
inline const char* OutputDir() { return g_outputDir; }

// ---------------------------------------------------------------------------
// Output location
// ---------------------------------------------------------------------------
inline std::string TargetPackage() {
    char buf[192] = {0};
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            return std::string(buf);
        }
    }
    return std::string("unknown_package");
}

inline bool IsWritableDir(const char* dir) {
    if (!dir || !dir[0]) return false;
    if (!DumpWriter::EnsureDirectory(dir)) return false;
    char testPath[512];
    snprintf(testPath, sizeof(testPath), "%s/.probe_%d", dir, (int)getpid());
    int fd = open(testPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;
    close(fd);
    unlink(testPath);
    return true;
}

inline bool ResolveOutputDir(char* out, size_t outSize) {
    const std::string pkg = TargetPackage();

    std::vector<std::string> candidates;
    if (pkg != "unknown_package" && !pkg.empty()) {
        candidates.push_back("/sdcard/Android/data/" + pkg + "/files/NINMODS_DUMP");
        candidates.push_back("/storage/emulated/0/Android/data/" + pkg + "/files/NINMODS_DUMP");
        candidates.push_back("/data/data/" + pkg + "/files/NINMODS_DUMP");
        candidates.push_back("/data/user/0/" + pkg + "/files/NINMODS_DUMP");
    }
    candidates.push_back("/storage/emulated/0/Download/NINMODS_DUMP");
    candidates.push_back("/sdcard/Download/NINMODS_DUMP");
    candidates.push_back("/data/local/tmp/NINMODS_DUMP");

    for (const std::string& candidate : candidates) {
        if (IsWritableDir(candidate.c_str())) {
            snprintf(out, outSize, "%s", candidate.c_str());
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Type helpers
// ---------------------------------------------------------------------------
inline const char* KindKeyword(int kind) {
    switch (kind) {
        case 0x01: return "void";
        case 0x02: return "bool";
        case 0x03: return "char";
        case 0x04: return "signed char";
        case 0x05: return "unsigned char";
        case 0x06: return "short";
        case 0x07: return "unsigned short";
        case 0x08: return "int";
        case 0x09: return "unsigned int";
        case 0x0a: return "long long";
        case 0x0b: return "unsigned long long";
        case 0x0c: return "float";
        case 0x0d: return "double";
        case 0x18: return "intptr_t";
        case 0x19: return "uintptr_t";
        default:   return nullptr;
    }
}

// Size of a field type in bytes. Primitives have fixed widths; reference types
// and pointers are pointer sized; value types ask the runtime through
// il2cpp_class_value_size. Unknown kinds return 0 so the caller pads instead of
// emitting a wrong layout.
inline size_t TypeSize(const void* type) {
    if (!type) return 0;
    const int kind = Il2CppTypeGetKind(type);

    switch (kind) {
        case 0x02: case 0x04: case 0x05: return 1;              // bool, i1, u1
        case 0x03: case 0x06: case 0x07: return 2;              // char, i2, u2
        case 0x08: case 0x09: case 0x0c: return 4;              // i4, u4, r4
        case 0x0a: case 0x0b: case 0x0d: return 8;              // i8, u8, r8
        case 0x0e: case 0x0f: case 0x12: case 0x14:             // string, ptr, class, array
        case 0x15: case 0x1c: case 0x1d: case 0x1b:             // genericinst, object, szarray, fnptr
        case 0x18: case 0x19:                                   // i, u
            return sizeof(void*);
        case 0x11:                                              // valuetype
        case 0x55: {                                            // enum
            if (il2cpp_class_from_type && il2cpp_class_value_size) {
                void* klass = il2cpp_class_from_type(type);
                if (klass) {
                    uint32_t align = 0;
                    const int32_t size = il2cpp_class_value_size(klass, &align);
                    if (size > 0 && size < 4096) return (size_t)size;
                }
            }
            if (il2cpp_class_from_type && il2cpp_class_instance_size) {
                void* klass = il2cpp_class_from_type(type);
                if (klass) {
                    const int32_t size = il2cpp_class_instance_size(klass);
                    if (size > 0 && size < 65536) return (size_t)size;
                }
            }
            return 0;
        }
        default:
            return 0;
    }
}

// A C++-safe identifier from a CLR name.
inline std::string SanitizeIdentifier(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 1);
    for (char c : text) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        out.push_back(ok ? c : '_');
    }
    if (out.empty()) out = "_";
    if (out[0] >= '0' && out[0] <= '9') out.insert(out.begin(), '_');
    return out;
}

inline std::string QualifiedName(const std::string& ns, const std::string& name) {
    if (ns.empty()) return name;
    return ns + "." + name;
}

// Copies a type name out of the thread-local buffer Il2CppTypeGetName returns.
// The copy must happen before the next call, which is why nothing here keeps the
// raw pointer.
inline std::string CopyTypeName(const void* type) {
    if (!type) return std::string();
    const char* name = Il2CppTypeGetName(type);
    return name ? std::string(name) : std::string();
}

// ---------------------------------------------------------------------------
// Snapshot of the runtime, collected once and reused by every output file.
// ---------------------------------------------------------------------------
struct FieldSnapshot {
    std::string name;
    std::string typeName;
    size_t      offset = 0;
    bool        hasRealOffset = false;
    size_t      size   = 0;
    uint32_t    flags  = 0;
    bool        isStatic = false;
    bool        isLiteral = false;
};

struct ParamSnapshot {
    std::string name;
    std::string typeName;
};

struct MethodSnapshot {
    std::string              name;
    std::string              returnType;
    std::vector<ParamSnapshot> params;
    uintptr_t                va = 0;
    uint64_t                 rva = 0;
    bool                     hasRva = false;
    bool                     isExternal = false;   // pointer outside the module
    bool                     hasBody = false;
    uint32_t                 flags = 0;
    uint32_t                 paramCount = 0;
    std::string              ownerModule;           // when isExternal
};

struct ClassSnapshot {
    std::string              name;
    std::string              namespaze;
    std::string              parentName;
    std::string              imageName;
    int32_t                  instanceSize = 0;
    uint32_t                 flags = 0;
    bool                     isEnum = false;
    bool                     isValueType = false;
    bool                     isInterface = false;
    bool                     isAbstract = false;
    bool                     isGeneric = false;
    std::vector<FieldSnapshot>  fields;
    std::vector<MethodSnapshot> methods;
};

struct ImageSnapshot {
    std::string                name;
    std::vector<ClassSnapshot> classes;
};

using Snapshot = std::vector<ImageSnapshot>;

inline void CollectMethod(const void* method, MethodSnapshot& out) {
    out.name = Il2CppMethodGetName(method);
    out.paramCount = Il2CppMethodGetParamCount(method);
    out.flags = Il2CppMethodGetFlags(method);

    if (il2cpp_type_get_name) {
        out.returnType = CopyTypeName(Il2CppMethodGetReturnType(method));
        if (out.returnType.empty()) out.returnType = "void";
    } else {
        out.returnType = "void";
    }

    if (out.paramCount > 0 && out.paramCount < 64) {
        out.params.reserve(out.paramCount);
        for (uint32_t i = 0; i < out.paramCount; ++i) {
            ParamSnapshot param;
            param.name = Il2CppMethodGetParamName(method, i);
            if (param.name.empty()) {
                char generated[24];
                snprintf(generated, sizeof(generated), "arg%u", i);
                param.name = generated;
            }
            param.typeName = CopyTypeName(Il2CppMethodGetParamType(method, i));
            if (param.typeName.empty()) param.typeName = "object";
            out.params.push_back(param);
        }
    }

    void* code = Il2CppMethodGetPointer(method);
    if (!code) {
        out.hasBody = false;
        return;
    }
    out.hasBody = true;
    out.va = (uintptr_t)code;

    const RvaResolver::Location location = RvaResolver::Resolve((uintptr_t)code);
    if (location.insideModule) {
        out.rva = location.rva;
        out.hasRva = true;
    } else {
        out.isExternal = true;
        out.ownerModule = location.moduleName;
    }
}

inline bool CollectClass(const void* klass, const char* imageName, ClassSnapshot& out) {
    if (!klass) return false;

    out.name = Il2CppClassGetName(klass);
    if (out.name.empty()) return false;
    out.namespaze = Il2CppClassGetNamespace(klass);
    out.imageName = imageName ? imageName : "";
    out.flags = (uint32_t)Il2CppClassGetFlags(klass);
    out.isEnum = Il2CppClassIsEnum(klass);
    out.isValueType = Il2CppClassIsValueType(klass);
    out.isInterface = Il2CppClassIsInterface(klass);
    out.isAbstract = Il2CppClassIsAbstract(klass);
    out.isGeneric = Il2CppClassIsGeneric(klass);

    if (il2cpp_class_instance_size) out.instanceSize = il2cpp_class_instance_size((void*)klass);

    const void* parent = Il2CppClassGetParent(klass);
    if (parent) out.parentName = Il2CppClassGetName(parent);

    void* fieldIter = nullptr;
    int guard = 0;
    while (void* field = Il2CppClassGetFields(klass, &fieldIter)) {
        if (++guard > 8192) break;

        const char* fieldName = Il2CppFieldGetName(field);
        if (!fieldName || !fieldName[0]) continue;

        FieldSnapshot snapshot;
        snapshot.name = fieldName;
        snapshot.offset = Il2CppFieldGetOffset(field);
        snapshot.flags = (uint32_t)Il2CppFieldGetFlags(field);
        snapshot.isStatic = (snapshot.flags & Il2CppFlags::kFieldStatic) != 0;
        snapshot.isLiteral = (snapshot.flags & Il2CppFlags::kFieldLiteral) != 0;
        snapshot.hasRealOffset = (!snapshot.isStatic && !snapshot.isLiteral && snapshot.offset > 0);

        const void* fieldType = Il2CppFieldGetType(field);
        snapshot.typeName = CopyTypeName(fieldType);
        if (snapshot.typeName.empty()) snapshot.typeName = "object";
        snapshot.size = TypeSize(fieldType);

        out.fields.push_back(std::move(snapshot));
    }

    void* methodIter = nullptr;
    guard = 0;
    while (const void* method = Il2CppClassGetMethods(klass, &methodIter)) {
        if (++guard > 16384) break;

        MethodSnapshot snapshot;
        CollectMethod(method, snapshot);
        if (snapshot.name.empty()) continue;
        out.methods.push_back(std::move(snapshot));
    }

    // Deterministic output: members in declaration order as the runtime walks
    // them, which is metadata order, and stable between runs.
    return true;
}

inline bool CollectFromMetadata(Snapshot& out) {
    MetadataParser::Located loc;
    if (!MetadataParser::Locate(loc)) return false;

    MetadataParser::HeaderV24 header;
    if (!SafeMemory::Read(loc.address, &header, sizeof(header))) return false;
    if (header.magic != MetadataParser::kMetadataMagic) return false;
    if (header.typeDefinitionsOffset == 0 || header.typeDefinitionsCount == 0) return false;

    const size_t typeCount = header.typeDefinitionsCount / sizeof(MetadataParser::TypeDefinitionEntry);
    if (typeCount == 0 || typeCount > 200000) return false;

    const uintptr_t typesAddr = loc.address + header.typeDefinitionsOffset;
    const uintptr_t fieldsAddr = loc.address + header.fieldsOffset;
    const uintptr_t methodsAddr = loc.address + header.methodsOffset;
    const uintptr_t stringsAddr = loc.address + header.stringOffset;

    auto readString = [&](uint32_t index) -> std::string {
        if (index >= header.stringCount) return "";
        char buf[256];
        if (SafeMemory::ReadCString(stringsAddr + index, buf, sizeof(buf))) {
            return std::string(buf);
        }
        return "";
    };

    ImageSnapshot fallbackImage;
    fallbackImage.name = "Assembly-CSharp.dll";

    for (size_t i = 0; i < typeCount; ++i) {
        MetadataParser::TypeDefinitionEntry td{};
        if (!SafeMemory::Read(typesAddr + i * sizeof(MetadataParser::TypeDefinitionEntry), &td, sizeof(td))) break;

        std::string kName = readString(td.nameIndex);
        if (kName.empty()) continue;

        ClassSnapshot cls;
        cls.name = kName;
        cls.namespaze = readString(td.namespaceIndex);
        cls.imageName = fallbackImage.name;
        cls.flags = td.flags;

        // Fields
        for (uint16_t f = 0; f < td.field_count; ++f) {
            uint32_t fIndex = td.fieldStart + f;
            MetadataParser::FieldDefinitionEntry fd{};
            if (!SafeMemory::Read(fieldsAddr + fIndex * sizeof(MetadataParser::FieldDefinitionEntry), &fd, sizeof(fd))) break;
            std::string fName = readString(fd.nameIndex);
            if (!fName.empty()) {
                FieldSnapshot field;
                field.name = fName;
                field.typeName = "var";
                field.offset = 0;
                field.hasRealOffset = false;
                cls.fields.push_back(field);
            }
        }

        // Methods
        for (uint16_t m = 0; m < td.method_count; ++m) {
            uint32_t mIndex = td.methodStart + m;
            MetadataParser::MethodDefinitionEntry md{};
            if (!SafeMemory::Read(methodsAddr + mIndex * sizeof(MetadataParser::MethodDefinitionEntry), &md, sizeof(md))) break;
            std::string mName = readString(md.nameIndex);
            if (!mName.empty()) {
                MethodSnapshot method;
                method.name = mName;
                method.returnType = "void";
                method.paramCount = md.parameterCount;
                method.flags = md.flags;
                method.hasRva = false;
                cls.methods.push_back(method);
            }
        }

        fallbackImage.classes.push_back(std::move(cls));
    }

    if (!fallbackImage.classes.empty()) {
        out.push_back(std::move(fallbackImage));
        return true;
    }
    return false;
}

inline bool Collect(Snapshot& out) {
    out.clear();
    if (!Il2CppIsReady() || Il2CppGetAssemblyCount() == 0) {
        __android_log_print(ANDROID_LOG_INFO, "NINMODS", "API classes unavailable; activating metadata enumeration fallback...");
        return CollectFromMetadata(out);
    }

    const size_t assemblyCount = Il2CppGetAssemblyCount();
    for (size_t a = 0; a < assemblyCount; ++a) {
        const void* image = Il2CppAssemblyGetImage(Il2CppGetAssembly(a));
        if (!image) continue;

        ImageSnapshot imageSnapshot;
        imageSnapshot.name = Il2CppImageGetName(image);

        const size_t classCount = Il2CppImageGetClassCount(image);
        for (size_t c = 0; c < classCount; ++c) {
            const void* klass = Il2CppImageGetClass(image, c);
            if (!klass) continue;

            ClassSnapshot classSnapshot;
            if (!CollectClass(klass, imageSnapshot.name.c_str(), classSnapshot)) continue;
            imageSnapshot.classes.push_back(std::move(classSnapshot));
        }

        if (!imageSnapshot.classes.empty()) out.push_back(std::move(imageSnapshot));
    }

    if (out.empty()) {
        __android_log_print(ANDROID_LOG_INFO, "NINMODS", "Runtime returned 0 classes; falling back to in-memory metadata...");
        return CollectFromMetadata(out);
    }

    return true;
}

// ---------------------------------------------------------------------------
// dump.cs
// ---------------------------------------------------------------------------
inline void WriteCsClass(DumpWriter::File& file, const ClassSnapshot& cls) {
    const bool isStruct = cls.isValueType && !cls.isEnum;

    const char* keyword = cls.isInterface ? "interface"
                        : cls.isEnum      ? "enum"
                        : isStruct        ? "struct"
                        :                   "class";

    file.Printf("// Namespace: %s\n", cls.namespaze.c_str());
    file.Printf("%s%s %s", Il2CppFlags::TypeAccess(cls.flags), keyword, cls.name.c_str());

    if (!cls.parentName.empty() && !cls.isEnum && !cls.isValueType && cls.parentName != "Object") {
        file.Printf(" : %s", cls.parentName.c_str());
    }
    file.Put(" // Dll: ");
    file.Put(cls.imageName);
    file.Printf(" Size: 0x%x\n{\n", cls.instanceSize);

    if (!cls.fields.empty()) {
        file.Put("\t// Fields\n");
        for (const FieldSnapshot& field : cls.fields) {
            file.Put("\t");
            file.Put(Il2CppFlags::FieldAccess(field.flags));
            if (field.isLiteral) {
                file.Put("const ");
            } else {
                if (field.isStatic) file.Put("static ");
                if (field.flags & Il2CppFlags::kFieldInitOnly) file.Put("readonly ");
            }
            if (field.hasRealOffset) {
                file.Printf("%s %s; // 0x%zx\n", field.typeName.c_str(), field.name.c_str(), field.offset);
            } else {
                file.Printf("%s %s;\n", field.typeName.c_str(), field.name.c_str());
            }
        }
        file.Put("\n");
    }

    if (!cls.methods.empty()) {
        file.Put("\t// Methods\n");
        for (const MethodSnapshot& method : cls.methods) {
            uint32_t flags = method.flags;
            if (flags & Il2CppFlags::kMethodAbstract) {
                file.Put("\t// (ABSTRACT)\n");
            } else if (method.hasRva && method.rva > 0) {
                file.Printf("\t// RVA: 0x%llx VA: 0x%lx\n",
                            (unsigned long long)method.rva, (unsigned long)method.va);
            } else if (method.isExternal) {
                file.Printf("\t// RVA: EXTERNAL (%s) VA: 0x%lx\n",
                            method.ownerModule.c_str(), (unsigned long)method.va);
            } else {
                file.Put("\t// (NO BODY)\n");
            }

            file.Put("\t");
            file.Put(Il2CppFlags::MethodAccess(flags));
            if (flags & Il2CppFlags::kMethodStatic) file.Put("static ");
            if (flags & Il2CppFlags::kMethodVirtual) file.Put("virtual ");
            if (flags & Il2CppFlags::kMethodAbstract) file.Put("abstract ");
            if (flags & Il2CppFlags::kMethodPinvoke) file.Put("extern ");

            file.Printf("%s %s(", method.returnType.c_str(), method.name.c_str());
            for (size_t i = 0; i < method.params.size(); ++i) {
                if (i) file.Put(", ");
                file.Printf("%s %s", method.params[i].typeName.c_str(), method.params[i].name.c_str());
            }
            file.Put(") { }\n");
        }
    }

    file.Put("}\n\n");
}

inline bool WriteCs(const std::string& path, const Snapshot& snapshot) {
    DumpWriter::File file;
    if (!file.Open(path.c_str())) return false;

    file.Printf("// NINMODS in-memory IL2CPP dump\n");
    file.Printf("// Package : %s\n", TargetPackage().c_str());
    file.Printf("// PID     : %d\n", (int)getpid());
    file.Printf("// Base    : 0x%lx\n", (unsigned long)Il2CppGetBase());
    file.Printf("// Classes : enumerated via %s\n", Il2CppEnumerationMode());
    file.Printf("// Note    : RVA is relative to libil2cpp.so. EXTERNAL means the\n");
    file.Printf("//           code pointer lives in another mapped module.\n\n");

    for (const ImageSnapshot& image : snapshot) {
        file.Printf("\n// ==========================================================\n");
        file.Printf("// Assembly: %s (%zu classes)\n", image.name.c_str(), image.classes.size());
        file.Printf("// ==========================================================\n\n");
        for (const ClassSnapshot& cls : image.classes) {
            WriteCsClass(file, cls);
        }
    }

    const bool ok = file.Ok();
    file.Close();
    return ok;
}

// ---------------------------------------------------------------------------
// il2cpp.h - layout-accurate C++ structs
// ---------------------------------------------------------------------------
inline void WriteSdkClass(DumpWriter::File& file, const ClassSnapshot& cls) {
    // Only classes with a real instance layout are emitted; interfaces and
    // static-only types have no fields to describe.
    if (cls.isInterface) return;
    if (cls.fields.empty() && cls.instanceSize <= 0) return;

    const std::string owner = SanitizeIdentifier(QualifiedName(cls.namespaze, cls.name));

    file.Printf("// %s (Dll: %s, size 0x%x)\n", QualifiedName(cls.namespaze, cls.name).c_str(),
                cls.imageName.c_str(), cls.instanceSize);

    // Base first: an instance of a derived class begins with its parent.
    const bool hasParent = !cls.parentName.empty() && cls.parentName != "Object" &&
                           !cls.isEnum && !cls.isValueType;

    file.Printf("struct %s", owner.c_str());
    if (hasParent) {
        file.Printf(" /* : %s */", cls.parentName.c_str());
    }
    file.Put(" {\n");

    size_t cursor = hasParent ? sizeof(void*) : 0;

    for (const FieldSnapshot& field : cls.fields) {
        if (field.isStatic || field.isLiteral) continue;   // not part of the instance
        if (field.size == 0) continue;                     // unknown width: skip, keep offsets intact

        const size_t offset = field.offset;
        if (offset > (1u << 20)) continue;

        if (offset > cursor) {
            file.Printf("\tuint8_t _pad_0x%zx[0x%zx];\n", cursor, offset - cursor);
            cursor = offset;
        } else if (offset < cursor) {
            // Overlapping offset means the snapshot is not a plain instance
            // layout (a union or an explicit-layout struct). Annotate instead
            // of writing something that would be wrong.
            file.Printf("\t// NOTE: %s declared at 0x%zx, previous field ends at 0x%zx\n",
                        field.name.c_str(), offset, cursor);
            continue;
        }

        const std::string fieldName = SanitizeIdentifier(field.name);
        file.Printf("\tuint8_t %s[0x%zx]; // +0x%zx %s\n",
                    fieldName.c_str(), field.size, offset, field.typeName.c_str());
        cursor = offset + field.size;
    }

    if (cls.instanceSize > 0 && (size_t)cls.instanceSize > cursor) {
        file.Printf("\tuint8_t _pad_tail[0x%zx];\n", (size_t)cls.instanceSize - cursor);
        cursor = (size_t)cls.instanceSize;
    }

    file.Put("};\n");
    if (cls.instanceSize > 0) {
        file.Printf("static_assert(sizeof(%s) == 0x%x, \"layout mismatch\");\n\n",
                    owner.c_str(), cls.instanceSize);
    } else {
        file.Put("\n");
    }
}

inline bool WriteSdk(const std::string& path, const Snapshot& snapshot) {
    DumpWriter::File file;
    if (!file.Open(path.c_str())) return false;

    file.Put("// ==========================================================================\n");
    file.Put("// NINMODS Modern Il2Cpp C++ SDK Header (Unity 2019 - 2023+)\n");
    file.Printf("// Target Package: %s\n", TargetPackage().c_str());
    file.Put("// ==========================================================================\n");
    file.Put("#pragma once\n");
    file.Put("#include <cstdint>\n");
    file.Put("#include <cstddef>\n\n");

    file.Put("typedef void(*Il2CppMethodPointer)();\n");
    file.Put("typedef uintptr_t il2cpp_array_size_t;\n\n");
    file.Put("struct Il2CppType { void* data; unsigned int bits; };\n");
    file.Put("struct Il2CppClass;\n");
    file.Put("struct MethodInfo;\n");
    file.Put("struct FieldInfo;\n\n");
    file.Put("struct Il2CppObject {\n    Il2CppClass *klass;\n    void *monitor;\n};\n\n");
    file.Put("struct Il2CppString : public Il2CppObject {\n    int32_t length;\n    uint16_t chars[1];\n};\n\n");
    file.Put("struct Il2CppArrayBounds {\n    il2cpp_array_size_t length;\n    int32_t lower_bound;\n};\n\n");
    file.Put("struct Il2CppArray : public Il2CppObject {\n    Il2CppArrayBounds *bounds;\n    il2cpp_array_size_t max_length;\n};\n\n");
    file.Put("struct MethodInfo {\n");
    file.Put("    Il2CppMethodPointer methodPointer;\n");
    file.Put("    Il2CppMethodPointer virtualMethodPointer;\n");
    file.Put("    void* invoker_method;\n");
    file.Put("    const char* name;\n");
    file.Put("    Il2CppClass *klass;\n");
    file.Put("    const Il2CppType *return_type;\n");
    file.Put("    const void* parameters;\n");
    file.Put("    void* methodDefinition;\n");
    file.Put("    void* genericContainer;\n");
    file.Put("    uint32_t token;\n");
    file.Put("    uint16_t flags;\n");
    file.Put("    uint16_t iflags;\n");
    file.Put("    uint16_t slot;\n");
    file.Put("    uint8_t parameters_count;\n};\n\n");
    file.Put("struct FieldInfo {\n");
    file.Put("    const char* name;\n");
    file.Put("    const Il2CppType* type;\n");
    file.Put("    Il2CppClass *parent;\n");
    file.Put("    int32_t offset;\n");
    file.Put("    uint32_t token;\n};\n\n");
    file.Put("// --------------------------------------------------------------------------\n");
    file.Put("// Game Classes & Structures\n");
    file.Put("// --------------------------------------------------------------------------\n\n");

    std::set<std::string> emitted;
    for (const ImageSnapshot& image : snapshot) {
        for (const ClassSnapshot& cls : image.classes) {
            const std::string owner = SanitizeIdentifier(QualifiedName(cls.namespaze, cls.name));
            if (!emitted.insert(owner).second) continue;   // same class in two images
            WriteSdkClass(file, cls);
        }
    }

    const bool ok = file.Ok();
    file.Close();
    return ok;
}

// ---------------------------------------------------------------------------
// ninmods_real_offsets.h - flat macros
// ---------------------------------------------------------------------------
inline bool WriteOffsets(const std::string& path, const Snapshot& snapshot) {
    DumpWriter::File file;
    if (!file.Open(path.c_str())) return false;

    file.Put("// NINMODS real offset macros\n");
    file.Printf("// Package: %s\n", TargetPackage().c_str());
    file.Put("// OFFSET_ macros are field offsets, RVA_ macros are code offsets from\n");
    file.Put("// the libil2cpp.so load bias. A method with no RVA is not emitted.\n");
    file.Put("#pragma once\n#include <cstdint>\n\n");

    for (const ImageSnapshot& image : snapshot) {
        file.Printf("// ---------------- %s ----------------\n\n", image.name.c_str());
        for (const ClassSnapshot& cls : image.classes) {
            const std::string owner = SanitizeIdentifier(QualifiedName(cls.namespaze, cls.name));
            if (cls.fields.empty() && cls.methods.empty()) continue;

            file.Printf("// %s\n", QualifiedName(cls.namespaze, cls.name).c_str());

            for (const FieldSnapshot& field : cls.fields) {
                if (field.isStatic || field.isLiteral) continue;
                if (!field.hasRealOffset || field.offset == 0) continue; // Real offsets only
                const std::string fieldName = SanitizeIdentifier(field.name);
                file.Printf("#define OFFSET_%s_%s 0x%zxULL\n", owner.c_str(), fieldName.c_str(), field.offset);
            }

            for (const MethodSnapshot& method : cls.methods) {
                if (!method.hasRva || method.rva == 0) continue; // Real RVAs only
                const std::string methodName = SanitizeIdentifier(method.name);
                file.Printf("#define RVA_%s_%s 0x%llxULL // params: %u\n",
                            owner.c_str(), methodName.c_str(),
                            (unsigned long long)method.rva, method.paramCount);
            }
            file.Put("\n");
        }
    }

    const bool ok = file.Ok();
    file.Close();
    return ok;
}

// ---------------------------------------------------------------------------
// script.json - IDA / Ghidra symbol map
// ---------------------------------------------------------------------------
inline bool WriteScriptJson(const std::string& path, const Snapshot& snapshot) {
    DumpWriter::File file;
    if (!file.Open(path.c_str())) return false;

    file.Put("{\n  \"ScriptMethod\": [\n");

    bool first = true;
    for (const ImageSnapshot& image : snapshot) {
        for (const ClassSnapshot& cls : image.classes) {
            const std::string owner = QualifiedName(cls.namespaze, cls.name);
            for (const MethodSnapshot& method : cls.methods) {
                if (!method.hasRva) continue;

                // Build a readable signature for the disassembler.
                std::string signature = method.returnType + " " + owner + "$$" + method.name + "(";
                for (size_t i = 0; i < method.params.size(); ++i) {
                    if (i) signature += ", ";
                    signature += method.params[i].typeName;
                }
                signature += ")";

                file.Printf("%s    {\n      \"Address\": %llu,\n      \"Name\": \"%s$$%s\",\n      \"Signature\": \"%s\"\n    }",
                            first ? "" : ",\n",
                            (unsigned long long)method.rva,
                            JsonWriter::EscapeString(owner.c_str()).c_str(),
                            JsonWriter::EscapeString(method.name.c_str()).c_str(),
                            JsonWriter::EscapeString(signature.c_str()).c_str());
                first = false;
            }
        }
    }

    file.Put("\n  ],\n  \"ScriptString\": [],\n  \"ScriptMetadata\": []\n}\n");

    const bool ok = file.Ok();
    file.Close();
    return ok;
}

// ---------------------------------------------------------------------------
// Metadata + ELF + tooling
// ---------------------------------------------------------------------------
inline bool DumpMetadata(const std::string& dir, std::string& note) {
    MetadataParser::Located located;
    if (!MetadataParser::Locate(located)) {
        note = "global-metadata.dat not found in memory";
        return false;
    }

    const std::string rawPath = dir + "/global-metadata.dat";
    if (!MetadataParser::WriteRaw(located.address, located.size, rawPath.c_str())) {
        note = "global-metadata.dat write failed";
        return false;
    }

    char summary[160];
    snprintf(summary, sizeof(summary), "global-metadata.dat %s, %zu bytes",
             MetadataParser::DescribeVersion(located.version).c_str(), located.size);
    note = summary;

    // String literals are only decoded for the header layout that is verified.
    const std::string literalPath = dir + "/stringliteral.json";
    MetadataParser::DumpStringLiteralsJson(located.address, located.version, literalPath.c_str());
    return true;
}

inline bool DumpElf(const std::string& dir, std::string& note) {
    const uintptr_t base = Il2CppGetBase();
    if (!base) {
        note = "libil2cpp.so not mapped";
        return false;
    }

    std::vector<uint8_t> image;
    ElfImage::Info info;
    if (!ElfReconstructor::BuildImage(base, image, info)) {
        note = "ELF reconstruction failed";
        return false;
    }

    const std::string outPath = dir + "/libil2cpp_dumped.so";
    if (!ElfReconstructor::WriteImage(image, outPath.c_str())) {
        note = "ELF write failed";
        return false;
    }

    const uint64_t firstVaddr = info.minVaddr;
    char summary[192];
    snprintf(summary, sizeof(summary),
             "libil2cpp_dumped.so %zu bytes (first PT_LOAD vaddr 0x%llx, sections %s)",
             image.size(), (unsigned long long)firstVaddr,
             info.sectionsResident ? "rebuilt" : "dropped (not resident)");
    note = summary;
    return true;
}

inline void WriteIdaScript(const std::string& path) {
    DumpWriter::File file;
    if (!file.Open(path.c_str())) return;

    file.Put(
        "# IDA Pro 7.x / 8.x / 9.x - apply the NINMODS symbol map\n"
        "# Load libil2cpp_dumped.so, then run this from File > Script file.\n"
        "import json\n"
        "import os\n"
        "import ida_bytes\n"
        "import ida_name\n"
        "import ida_nalt\n"
        "import ida_kernwin\n"
        "\n"
        "def apply_symbols():\n"
        "    script_dir = os.path.dirname(os.path.realpath(__file__))\n"
        "    json_path = os.path.join(script_dir, 'script.json')\n"
        "    if not os.path.exists(json_path):\n"
        "        json_path = ida_kernwin.ask_file(False, '*.json', 'Select script.json')\n"
        "    if not json_path or not os.path.exists(json_path):\n"
        "        print('[!] script.json not found')\n"
        "        return\n"
        "    with open(json_path, 'r', encoding='utf-8') as handle:\n"
        "        data = json.load(handle)\n"
        "    image_base = ida_nalt.get_imagebase()\n"
        "    methods = data.get('ScriptMethod', [])\n"
        "    print('[*] applying %d methods (image base %s)' % (len(methods), hex(image_base)))\n"
        "    applied = 0\n"
        "    for entry in methods:\n"
        "        rva = entry.get('Address', 0)\n"
        "        name = entry.get('Name', '')\n"
        "        if rva > 0 and name:\n"
        "            if ida_name.set_name(image_base + rva, name, ida_name.SN_NOWARN):\n"
        "                applied += 1\n"
        "    print('[+] applied %d symbols' % applied)\n"
        "\n"
        "if __name__ == '__main__':\n"
        "    apply_symbols()\n");

    file.Close();
}

inline void WriteGhidraScript(const std::string& path) {
    DumpWriter::File file;
    if (!file.Open(path.c_str())) return;

    file.Put(
        "# Ghidra - apply the NINMODS symbol map\n"
        "# @category NINMODS\n"
        "import json\n"
        "\n"
        "def run():\n"
        "    path = askFile('Select script.json', 'Open')\n"
        "    if not path:\n"
        "        return\n"
        "    with open(path.getAbsolutePath(), 'r') as handle:\n"
        "        data = json.load(handle)\n"
        "    base = currentProgram.getImageBase()\n"
        "    methods = data.get('ScriptMethod', [])\n"
        "    applied = 0\n"
        "    for entry in methods:\n"
        "        rva = entry.get('Address', 0)\n"
        "        name = entry.get('Name', '')\n"
        "        if rva <= 0 or not name:\n"
        "            continue\n"
        "        address = base.add(rva)\n"
        "        createLabel(address, name, True)\n"
        "        applied += 1\n"
        "    print('applied %d symbols' % applied)\n"
        "\n"
        "run()\n");

    file.Close();
}

inline void WriteHookGenerator(const std::string& path) {
    DumpWriter::File file;
    if (!file.Open(path.c_str())) return;

    file.Put(
        "# NINMODS hook generator - reads dump.cs and prints a DobbyHook skeleton.\n"
        "import os\n"
        "import re\n"
        "\n"
        "RVA_LINE = re.compile(r'// RVA: 0x([0-9a-fA-F]+)')\n"
        "CLASS_LINE = re.compile(r'^\\S*\\s*(?:class|struct|enum|interface)\\s+(\\w+)')\n"
        "METHOD_LINE = re.compile(r'^\\s*(?:public|private|protected|internal)\\s+.*?\\s(\\w+)\\(')\n"
        "\n"
        "def run():\n"
        "    script_dir = os.path.dirname(os.path.realpath(__file__))\n"
        "    cs_path = os.path.join(script_dir, 'dump.cs')\n"
        "    if not os.path.exists(cs_path):\n"
        "        print('[!] dump.cs not found next to this script')\n"
        "        return\n"
        "    query = input('[?] class or method name: ').strip().lower()\n"
        "    if not query:\n"
        "        return\n"
        "    current_class = 'Unknown'\n"
        "    pending_rva = None\n"
        "    hits = 0\n"
        "    with open(cs_path, 'r', encoding='utf-8', errors='ignore') as handle:\n"
        "        for line in handle:\n"
        "            class_match = CLASS_LINE.match(line.strip())\n"
        "            if class_match:\n"
        "                current_class = class_match.group(1)\n"
        "                continue\n"
        "            rva_match = RVA_LINE.search(line)\n"
        "            if rva_match:\n"
        "                pending_rva = rva_match.group(1)\n"
        "                continue\n"
        "            method_match = METHOD_LINE.match(line)\n"
        "            if method_match and pending_rva:\n"
        "                name = method_match.group(1)\n"
        "                signature = (current_class + '::' + name).lower()\n"
        "                if query in signature:\n"
        "                    hits += 1\n"
        "                    print('=' * 62)\n"
        "                    print('// %s::%s  RVA 0x%s' % (current_class, name, pending_rva))\n"
        "                    print('void* (*orig_%s_%s)(void* instance) = nullptr;' % (current_class, name))\n"
        "                    print('void* hook_%s_%s(void* instance) {' % (current_class, name))\n"
        "                    print('    // logic')\n"
        "                    print('    return orig_%s_%s(instance);' % (current_class, name))\n"
        "                    print('}')\n"
        "                    print('// DobbyHook((void*)(il2cpp_base + 0x%s),' % pending_rva)\n")
        "                    print('//            (void*)hook_%s_%s,' % (current_class, name))\n")
        "                    print('//            (void**)&orig_%s_%s);' % (current_class, name))\n"
        "                pending_rva = None\n"
        "    print('matches: %d' % hits)\n"
        "\n"
        "if __name__ == '__main__':\n"
        "    run()\n");

    file.Close();
}

inline void WriteReadme(const std::string& path, const std::string& metadataNote,
                        const std::string& elfNote, const Snapshot& snapshot) {
    DumpWriter::File file;
    if (!file.Open(path.c_str())) return;

    size_t classCount = 0, methodCount = 0, rvaCount = 0, externalCount = 0;

    for (const ImageSnapshot& image : snapshot) {
        classCount += image.classes.size();
        for (const ClassSnapshot& cls : image.classes) {
            for (const MethodSnapshot& method : cls.methods) {
                ++methodCount;
                if (method.hasRva) ++rvaCount;
                if (method.isExternal) ++externalCount;
            }
        }
    }

    file.Printf("NINMODS in-memory IL2CPP dump\n");
    file.Printf("=====================================================\n\n");
    file.Printf("Package        : %s\n", TargetPackage().c_str());
    file.Printf("PID            : %d\n", (int)getpid());
    file.Printf("il2cpp base    : 0x%lx\n", (unsigned long)Il2CppGetBase());
    file.Printf("Enumeration    : %s\n", Il2CppEnumerationMode());
    file.Printf("Classes        : %zu\n", classCount);
    file.Printf("Methods        : %zu (%zu with RVA, %zu external)\n\n", methodCount, rvaCount, externalCount);

    file.Printf("Files\n");
    file.Put("-----\n");
    file.Printf("dump.cs                classes, fields and methods with real RVAs\n");
    file.Printf("il2cpp.h               struct layouts with static_assert guards\n");
    file.Printf("ninmods_real_offsets.h OFFSET_ and RVA_ macros\n");
    file.Printf("script.json            symbol map for IDA and Ghidra\n");
    file.Printf("stringliteral.json     decoded string literals (when the metadata layout is known)\n");
    file.Printf("global-metadata.dat    in-memory metadata - %s\n", metadataNote.c_str());
    file.Printf("libil2cpp_dumped.so    reconstructed ELF - %s\n", elfNote.c_str());
    file.Printf("ida_with_struct_py3.py IDA Pro symbol loader\n");
    file.Printf("ghidra_with_struct.py  Ghidra symbol loader\n");
    file.Printf("ninmods_hook_generator.py  DobbyHook skeleton generator\n\n");

    file.Printf("Reading the RVAs\n");
    file.Put("----------------\n");
    file.Put("RVA values are offsets from the libil2cpp.so load bias, so\n");
    file.Put("  runtime_address = il2cpp_base + RVA\n");
    file.Put("EXTERNAL entries name the module that actually owns the code pointer;\n");
    file.Put("they have no RVA against libil2cpp.so and must be read from that module.\n");

    file.Close();
}

// ---------------------------------------------------------------------------
// Full suite
// ---------------------------------------------------------------------------
inline void RunFullDump() {
    if (g_busy.exchange(true)) return;
    g_progress.store(0);
    SetStatus("preparing");

    struct BusyGuard {
        ~BusyGuard() { g_busy.store(false); }
    } guard;

    char dirBuffer[192] = "";
    if (!ResolveOutputDir(dirBuffer, sizeof(dirBuffer))) {
        SetStatus("ERROR: no writable output directory");
        return;
    }
    snprintf(g_outputDir, sizeof(g_outputDir), "%s", dirBuffer);
    const std::string dir(dirBuffer);

    Il2CppAttach();
    if (!Il2CppIsReady()) {
        SetStatus("ERROR: il2cpp API not available");
        return;
    }

    SafeMemory::Refresh();
    RvaResolver::SetTargetModule("libil2cpp.so");

    SetStatus("enumerating classes");
    g_progress.store(10);

    Snapshot snapshot;
    if (!Collect(snapshot)) {
        SetStatus("ERROR: no classes enumerated");
        return;
    }

    g_progress.store(45);
    SetStatus("writing dump.cs");
    WriteCs(dir + "/dump.cs", snapshot);

    SetStatus("writing il2cpp.h");
    g_progress.store(60);
    WriteSdk(dir + "/il2cpp.h", snapshot);

    SetStatus("writing offsets and symbols");
    g_progress.store(70);
    WriteOffsets(dir + "/ninmods_real_offsets.h", snapshot);
    WriteScriptJson(dir + "/script.json", snapshot);

    SetStatus("dumping metadata");
    g_progress.store(80);
    std::string metadataNote;
    DumpMetadata(dir, metadataNote);

    SetStatus("reconstructing ELF");
    g_progress.store(88);
    std::string elfNote;
    DumpElf(dir, elfNote);

    SetStatus("writing tools");
    g_progress.store(95);
    WriteIdaScript(dir + "/ida_with_struct_py3.py");
    WriteGhidraScript(dir + "/ghidra_with_struct.py");
    WriteHookGenerator(dir + "/ninmods_hook_generator.py");
    WriteReadme(dir + "/README.txt", metadataNote, elfNote, snapshot);

    g_progress.store(100);
    SetStatusf("DONE: %s", dir.c_str());
    __android_log_print(ANDROID_LOG_INFO, "NINMODS", "dump complete: %s", dir.c_str());
}

inline void TriggerFullDump() {
    if (g_busy.load()) return;
    std::thread(RunFullDump).detach();
}

} // namespace Dumper
