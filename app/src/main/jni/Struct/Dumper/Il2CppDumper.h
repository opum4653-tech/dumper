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
#include <cstdarg>
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
        case 0x0e: case 0x0f: case 0x10: case 0x12: case 0x14:  // string, ptr, byref, class, array
        case 0x15: case 0x1b: case 0x1c: case 0x1d:             // genericinst, fnptr, object, szarray
        case 0x18: case 0x19:                                   // i, u
            return sizeof(void*);
        case 0x11: {                                            // valuetype (enum included)
            const void* klass = Il2CppClassFromType(type);
            if (klass) {
                uint32_t align = 0;
                const int32_t valSize = Il2CppClassGetValueSize(klass, &align);
                if (valSize > 0 && valSize < 4096) return (size_t)valSize;

                const int32_t instSize = Il2CppClassGetInstanceSize(klass);
                if (instSize > 0 && instSize < 65536) return (size_t)instSize;
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

// C++ keywords. A CLR member may legally be named any of these, so a
// sanitized identifier that collides gets a trailing underscore.
inline bool IsCppKeyword(const std::string& s) {
    static const char* const kKeywords[] = {
        "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case",
        "catch", "char", "class", "const", "constexpr", "continue", "default",
        "delete", "do", "double", "else", "enum", "explicit", "export", "extern",
        "false", "float", "for", "friend", "goto", "if", "inline", "int", "long",
        "mutable", "namespace", "new", "noexcept", "not", "nullptr", "operator",
        "or", "private", "protected", "public", "register", "reinterpret_cast",
        "return", "short", "signed", "sizeof", "static", "struct", "switch",
        "template", "this", "throw", "true", "try", "typedef", "typeid",
        "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
        "wchar_t", "while", "xor"
    };
    for (const char* k : kKeywords) {
        if (s == k) return true;
    }
    return false;
}

inline std::string CppIdentifier(const std::string& text) {
    std::string out = SanitizeIdentifier(text);
    if (IsCppKeyword(out)) out += '_';
    return out;
}

// C# keywords. Same idea, but C# escapes them with a leading '@' instead of a
// suffix, and that form is legal in any context a plain identifier is.
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
    int32_t                  parentInstanceSize = 0;
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

    // An unresolved type stays empty. Substituting a placeholder here would be
    // written to disk as though the runtime had reported it.
    out.returnType = CopyTypeName(Il2CppMethodGetReturnType(method));

    if (out.paramCount > 0 && out.paramCount < 64) {
        out.params.reserve(out.paramCount);
        for (uint32_t i = 0; i < out.paramCount; ++i) {
            ParamSnapshot param;
            param.name = Il2CppMethodGetParamName(method, i);
            if (param.name.empty()) {
                // The runtime exposes no parameter name. The metadata keeps the
                // real one, but the parameter table is not decoded here, so only
                // the position is known. argN states exactly that.
                char generated[24];
                snprintf(generated, sizeof(generated), "arg%u", i);
                param.name = generated;
            }
            param.typeName = CopyTypeName(Il2CppMethodGetParamType(method, i));
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

    out.instanceSize = Il2CppClassGetInstanceSize(klass);

    const void* parent = Il2CppClassGetParent(klass);
    if (parent) {
        out.parentName = Il2CppClassGetName(parent);
        // The parent's instance size is where this class's own fields may start.
        // Without it the emitted struct cannot describe the layout correctly.
        out.parentInstanceSize = Il2CppClassGetInstanceSize(parent);
    }

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
        // il2cpp_field_get_offset is authoritative for instance fields, and 0 is
        // a legitimate offset (the first field of a base-less layout). The flag
        // only means "the runtime answered", not "the answer was non-zero".
        snapshot.hasRealOffset =
            !snapshot.isStatic && !snapshot.isLiteral && Il2CppFieldOffsetAvailable();

        const void* fieldType = Il2CppFieldGetType(field);
        // Empty means the runtime did not answer. The writer states that rather
        // than printing a type the runtime never reported.
        snapshot.typeName = CopyTypeName(fieldType);
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

// Fallback used only when the runtime exposes no classes at all. It reads
// global-metadata.dat directly, which is where the names and flags the runtime
// itself reads come from. What it cannot provide is the field type, the field
// offset and the method code pointer, because all three live in the runtime's
// Il2CppType table and class layout, not in the metadata. Those stay empty
// instead of being filled with a plausible-looking lie, and the writers mark
// them as unavailable.
inline bool CollectFromMetadata(Snapshot& out) {
    MetadataParser::Located loc;
    if (!MetadataParser::Locate(loc)) {
        __android_log_print(ANDROID_LOG_INFO, "NINMODS",
                            "metadata fallback: no decodable global-metadata.dat "
                            "(supported versions %d-%d)",
                            MetadataParser::kMinVersion, MetadataParser::kMaxVersion);
        return false;
    }

    __android_log_print(ANDROID_LOG_INFO, "NINMODS",
                        "metadata fallback: %s, %zu types, %zu fields, %zu methods",
                        MetadataParser::DescribeVersion(loc.version).c_str(),
                        loc.typeCount, loc.fieldCount, loc.methodCount);

    const uintptr_t typesAddr   = loc.address + loc.header.typeDefinitionsOffset;
    const uintptr_t fieldsAddr  = loc.address + loc.header.fieldsOffset;
    const uintptr_t methodsAddr = loc.address + loc.header.methodsOffset;

    auto readString = [&](uint32_t index) -> std::string {
        char buffer[512];
        if (!MetadataParser::ReadString(loc, index, buffer, sizeof(buffer))) return std::string();
        return std::string(buffer);
    };

    ImageSnapshot fallbackImage;
    fallbackImage.name = "global-metadata.dat";

    for (size_t i = 0; i < loc.typeCount; ++i) {
        MetadataParser::TypeDefView td;
        if (!MetadataParser::ReadTypeDef(typesAddr + i * sizeof(MetadataParser::TypeDefinitionEntry),
                                         td)) {
            break;
        }

        const std::string className = readString(td.nameIndex);
        if (className.empty()) continue;

        ClassSnapshot cls;
        cls.name        = className;
        cls.namespaze   = readString(td.namespaceIndex);
        cls.imageName   = fallbackImage.name;
        cls.flags       = td.flags;
        cls.isInterface = (td.flags & Il2CppFlags::kTypeInterface) != 0;
        cls.isAbstract  = (td.flags & Il2CppFlags::kTypeAbstract) != 0;
        cls.isValueType = td.ValueType();
        cls.isEnum      = td.EnumType();

        if (loc.fieldCount != 0) {
            const size_t fieldEnd = (size_t)td.fieldStart + td.fieldCount;
            if (fieldEnd <= loc.fieldCount) {
                for (uint16_t f = 0; f < td.fieldCount; ++f) {
                    uint32_t nameIndex = 0;
                    const uint32_t fieldIndex = td.fieldStart + f;
                    if (!MetadataParser::ReadFieldDef(
                            fieldsAddr + (uintptr_t)fieldIndex * sizeof(MetadataParser::FieldDefinitionEntry),
                            nameIndex)) {
                        break;
                    }

                    const std::string fieldName = readString(nameIndex);
                    if (fieldName.empty()) continue;

                    FieldSnapshot field;
                    field.name = fieldName;
                    field.hasRealOffset = false;   // not derivable without the runtime
                    field.size = 0;
                    cls.fields.push_back(std::move(field));
                }
            }
        }

        if (loc.methodCount != 0) {
            const size_t methodEnd = (size_t)td.methodStart + td.methodCount;
            if (methodEnd <= loc.methodCount) {
                for (uint16_t m = 0; m < td.methodCount; ++m) {
                    MetadataParser::MethodDefView md;
                    const uint32_t methodIndex = td.methodStart + m;
                    if (!MetadataParser::ReadMethodDef(
                            methodsAddr + (uintptr_t)methodIndex * sizeof(MetadataParser::MethodDefinitionEntry),
                            md)) {
                        break;
                    }

                    const std::string methodName = readString(md.nameIndex);
                    if (methodName.empty()) continue;

                    MethodSnapshot method;
                    method.name       = methodName;
                    method.paramCount = md.parameterCount;
                    method.flags      = md.flags;
                    method.hasBody    = false;   // no code pointer without the runtime
                    method.hasRva     = false;
                    cls.methods.push_back(std::move(method));
                }
            }
        }

        fallbackImage.classes.push_back(std::move(cls));
    }

    if (fallbackImage.classes.empty()) return false;
    out.push_back(std::move(fallbackImage));
    return true;
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

    // An interface's base list is other interfaces. A class base name there is
    // the runtime's Object root, which C# rejects, so it is only printed for
    // classes and structs.
    if (!cls.isInterface && !cls.parentName.empty() && !cls.isEnum &&
        !cls.isValueType && cls.parentName != "Object") {
        file.Printf(" : %s", cls.parentName.c_str());
    }
    file.Put(" // Dll: ");
    file.Put(cls.imageName);
    if (cls.instanceSize > 0) {
        file.Printf(" Size: 0x%x\n{\n", cls.instanceSize);
    } else {
        file.Put(" Size: unavailable\n{\n");
    }

    // An enum's members are compile-time constants stored in the metadata
    // constant blob, not instance fields. Declaring the runtime's instance
    // fields inside an enum body would not compile, and the values cannot be
    // recovered from the runtime, so the member list is stated as unavailable.
    if (cls.isEnum) {
        file.Put("\t// members unavailable: enum constants live in the metadata\n");
        file.Put("\t// constant blob, which the runtime does not expose\n");
        file.Put("}\n\n");
        return;
    }

    // C# interfaces cannot declare fields at all. The runtime still reports them
    // (they are the static backing values of properties), so they are recorded
    // as comments: emitting them as fields would make the file fail to compile.
    const bool interfaceBody = cls.isInterface;

    if (interfaceBody) {
        if (!cls.fields.empty()) {
            file.Put("\t// fields unavailable: C# interfaces cannot declare fields\n");
            for (const FieldSnapshot& field : cls.fields) {
                if (field.name.empty()) continue;
                file.Printf("\t//   %s%s\n",
                            field.typeName.empty() ? "" : (field.typeName + " ").c_str(),
                            field.name.c_str());
            }
        }
    } else if (!cls.fields.empty()) {
        file.Put("\t// Fields\n");
        for (const FieldSnapshot& field : cls.fields) {
            if (field.typeName.empty()) {
                // Metadata-only dump: the field type lives in the runtime type
                // table, not in the metadata header. Say so instead of printing
                // a placeholder type that would read as a real one.
                file.Printf("\t// %s -- type unavailable (metadata-only dump)\n", field.name.c_str());
                continue;
            }
            if (field.isLiteral) {
                // A literal is a const whose value sits in the metadata constant
                // blob. C# requires an initializer on a const, and the value is
                // not available from the runtime, so the field is stated.
                file.Printf("\t// const %s %s -- value unavailable (metadata constant)\n",
                            field.typeName.c_str(), field.name.c_str());
                continue;
            }
            file.Put("\t");
            file.Put(Il2CppFlags::FieldAccess(field.flags));
            if (field.isStatic) file.Put("static ");
            if (field.flags & Il2CppFlags::kFieldInitOnly) file.Put("readonly ");
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
            } else if (method.hasRva) {
                file.Printf("\t// RVA: 0x%llx VA: 0x%lx\n",
                            (unsigned long long)method.rva, (unsigned long)method.va);
            } else if (method.isExternal) {
                file.Printf("\t// RVA: EXTERNAL (%s) VA: 0x%lx\n",
                            method.ownerModule.c_str(), (unsigned long)method.va);
            } else {
                // No code pointer means no RVA. Writing 0x0 would collide with a
                // real address, so the absence is stated instead.
                file.Put("\t// no code pointer (no RVA)\n");
            }

            if (method.returnType.empty()) {
                // No return type from the runtime means the declaration cannot be
                // stated. Printing a guessed one would read as a real signature.
                file.Printf("\t// %s(%u params) -- signature unavailable: return type not reported\n",
                            method.name.c_str(), method.paramCount);
                continue;
            }

            // A parameter whose type the runtime did not report cannot be
            // declared. The whole declaration is downgraded to a comment so the
            // file still parses and the gap is visible.
            bool typesKnown = true;
            for (const ParamSnapshot& param : method.params) {
                if (param.typeName.empty()) { typesKnown = false; break; }
            }
            if (!typesKnown) {
                file.Printf("\t// %s(%u params) -- signature unavailable: parameter type not reported\n",
                            method.name.c_str(), method.paramCount);
                continue;
            }

            file.Put("\t");
            // Interface members are implicitly public and cannot carry an access
            // modifier, static, virtual, abstract or extern. Emitting any of them
            // inside an interface makes the file fail to parse.
            if (!cls.isInterface) {
                file.Put(Il2CppFlags::MethodAccess(flags));
                if (flags & Il2CppFlags::kMethodStatic) file.Put("static ");
                // abstract implies virtual in the metadata, but C# rejects the
                // pair, so only one of the two is ever printed.
                if (flags & Il2CppFlags::kMethodAbstract)      file.Put("abstract ");
                else if (flags & Il2CppFlags::kMethodVirtual)  file.Put("virtual ");
                if (flags & Il2CppFlags::kMethodPinvoke) file.Put("extern ");
            }

            file.Printf("%s %s(", method.returnType.c_str(), method.name.c_str());
            for (size_t i = 0; i < method.params.size(); ++i) {
                if (i) file.Put(", ");
                file.Printf("%s %s", method.params[i].typeName.c_str(),
                            method.params[i].name.c_str());
            }

            // A body is only valid where the method actually has a code pointer.
            // abstract, extern (pinvoke) and interface members end with a
            // semicolon: giving them { } is a compile error in C#.
            const bool needsBody = !cls.isInterface &&
                                   !(flags & Il2CppFlags::kMethodAbstract) &&
                                   !(flags & Il2CppFlags::kMethodPinvoke);
            file.Put(needsBody ? ") { }\n" : ");\n");
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
    // A struct body is only emitted when every instance field it describes has a
    // runtime-provided offset. A metadata-only snapshot has names but no
    // offsets, and laying those out at guessed positions would produce a header
    // that compiles and then reads the wrong bytes at runtime.
    //
    // Interfaces have no instance layout, and an enum's real C++ representation
    // is the underlying integer type, which the runtime exposes only as the type
    // of its single value__ field. Neither is emitted as a struct.
    if (cls.isInterface || cls.isEnum) return;

    bool anyRealOffset = false;
    for (const FieldSnapshot& field : cls.fields) {
        if (field.isStatic || field.isLiteral) continue;
        if (field.hasRealOffset && field.size > 0) { anyRealOffset = true; break; }
    }
    if (!anyRealOffset) return;
    if (cls.instanceSize <= 0) return;

    const std::string owner = CppIdentifier(QualifiedName(cls.namespaze, cls.name));

    file.Printf("// %s (Dll: %s, size 0x%x)\n", QualifiedName(cls.namespaze, cls.name).c_str(),
                cls.imageName.c_str(), cls.instanceSize);

    // An instance of a derived class begins with its parent, so the first byte
    // available to a declared field is the parent's full instance size, not a
    // pointer. A wrong base size shifts every field that follows it.
    const bool hasParent = !cls.parentName.empty() && cls.parentName != "Object" &&
                           !cls.isEnum && !cls.isValueType;

    file.Printf("struct %s", owner.c_str());
    if (hasParent) {
        file.Printf(" /* derives from %s, base occupies 0x%x bytes */",
                    cls.parentName.c_str(), cls.parentInstanceSize);
    }
    file.Put(" {\n");

    size_t cursor = 0;
    if (hasParent) {
        if (cls.parentInstanceSize <= 0) {
            // The parent size is unknown, so no byte position after it can be
            // stated. Emitting a body here would place every field at a wrong
            // offset, so the layout is left out and the reason is recorded.
            file.Printf("\t// layout unavailable: parent %s instance size unknown\n};\n\n",
                        cls.parentName.c_str());
            return;
        }
        // A parent larger than the derived instance cannot be a plain layout: the
        // base alone would overflow the struct and the static_assert below would
        // fail to compile. That happens for generic or inflated parents, where
        // il2cpp_class_instance_size reports an uninstantiated size.
        if ((size_t)cls.parentInstanceSize > (size_t)cls.instanceSize) {
            file.Printf("\t// layout unavailable: parent %s size 0x%x exceeds instance size 0x%x\n};\n\n",
                        cls.parentName.c_str(), cls.parentInstanceSize, cls.instanceSize);
            return;
        }
        file.Printf("\tuint8_t _base_%s[0x%x];\n",
                    CppIdentifier(cls.parentName).c_str(), cls.parentInstanceSize);
        cursor = (size_t)cls.parentInstanceSize;
    }

    for (const FieldSnapshot& field : cls.fields) {
        if (field.isStatic || field.isLiteral) continue;   // not part of the instance
        if (!field.hasRealOffset) continue;                // no runtime offset: cannot lay out
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

        const std::string fieldName = CppIdentifier(field.name);
        file.Printf("\tuint8_t %s[0x%zx]; // +0x%zx %s\n",
                    fieldName.c_str(), field.size, offset, field.typeName.c_str());
        cursor = offset + field.size;
    }

    if (cls.instanceSize > 0 && (size_t)cls.instanceSize > cursor) {
        file.Printf("\tuint8_t _pad_tail[0x%zx];\n", (size_t)cls.instanceSize - cursor);
        cursor = (size_t)cls.instanceSize;
    }

    file.Put("};\n");
    // The assertion is only emitted when the emitted body actually reaches the
    // runtime-reported size. A field whose offset lands past that size (an
    // inflated or trimmed class) makes the two disagree, and asserting then
    // would fail to compile a header that faithfully records the runtime.
    if (cls.instanceSize > 0 && cursor == (size_t)cls.instanceSize) {
        file.Printf("static_assert(sizeof(%s) == 0x%x, \"layout mismatch\");\n\n",
                    owner.c_str(), cls.instanceSize);
    } else if (cls.instanceSize > 0) {
        file.Printf("// sizeof(%s) is 0x%zx, runtime reports 0x%x: a field offset lies\n",
                    owner.c_str(), cursor, cls.instanceSize);
        file.Put("// past the reported instance size, so no assertion is emitted.\n\n");
    } else {
        file.Put("\n");
    }
}

inline bool WriteSdk(const std::string& path, const Snapshot& snapshot) {
    DumpWriter::File file;
    if (!file.Open(path.c_str())) return false;

    file.Put("// ==========================================================================\n");
    file.Put("// NINMODS Il2Cpp C++ SDK Header\n");
    file.Printf("// Target Package: %s\n", TargetPackage().c_str());
    file.Put("// ==========================================================================\n");
    file.Put("#pragma once\n");
    file.Put("#include <cstdint>\n");
    file.Put("#include <cstddef>\n\n");

    file.Put("typedef void (*Il2CppMethodPointer)();\n");
    file.Put("typedef uintptr_t il2cpp_array_size_t;\n\n");
    file.Put("// Il2CppClass, FieldInfo, MethodInfo and Il2CppType keep their fields in an\n");
    file.Put("// order that changes between Unity releases. They stay incomplete types so\n");
    file.Put("// that reaching into them is a compile error rather than a silent misread.\n");
    file.Put("// Query them through the il2cpp exports.\n");
    file.Put("struct Il2CppClass;\n");
    file.Put("struct MethodInfo;\n");
    file.Put("struct FieldInfo;\n");
    file.Put("struct Il2CppType;\n\n");
    file.Put("struct Il2CppObject {\n    Il2CppClass *klass;\n    void *monitor;\n};\n\n");
    file.Put("struct Il2CppString : public Il2CppObject {\n    int32_t length;\n    uint16_t chars[1];\n};\n\n");
    file.Put("struct Il2CppArrayBounds {\n    il2cpp_array_size_t length;\n    int32_t lower_bound;\n};\n\n");
    file.Put("struct Il2CppArray : public Il2CppObject {\n    Il2CppArrayBounds *bounds;\n    il2cpp_array_size_t max_length;\n};\n\n");
    file.Put("// MethodInfo's first field is methodPointer in every published il2cpp\n");
    file.Put("// version, which is the only part of the layout this project relies on.\n");
    file.Put("struct MethodInfoPrefix {\n");
    file.Put("    Il2CppMethodPointer methodPointer;\n");
    file.Put("};\n\n");
    file.Put("// --------------------------------------------------------------------------\n");
    file.Put("// Game Classes & Structures\n");
    file.Put("// --------------------------------------------------------------------------\n\n");

    std::set<std::string> emitted;
    for (const ImageSnapshot& image : snapshot) {
        for (const ClassSnapshot& cls : image.classes) {
            const std::string owner = CppIdentifier(QualifiedName(cls.namespaze, cls.name));
            // The same class can appear in two images, and two different CLR names
            // can sanitize to one identifier. Both would be a redefinition in a
            // header that is meant to compile, so only the first is emitted and
            // the collision is recorded instead of dropped silently.
            if (!emitted.insert(owner).second) {
                file.Printf("// %s (Dll: %s) not emitted: its identifier %s is already\n",
                            QualifiedName(cls.namespaze, cls.name).c_str(),
                            cls.imageName.c_str(), owner.c_str());
                file.Put("// defined by an earlier class.\n\n");
                continue;
            }
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
            const std::string owner = CppIdentifier(QualifiedName(cls.namespaze, cls.name));
            if (cls.fields.empty() && cls.methods.empty()) continue;

            // A macro name has to be unique in the translation unit that includes
            // this header. Two things collide here: the same class appearing in
            // two images, and two overloads of one method. Both are deduplicated
            // by the full macro name, and the first definition wins because the
            // runtime returned the same offset/RVA for the same name.
            std::set<std::string> emitted;

            file.Printf("// %s\n", QualifiedName(cls.namespaze, cls.name).c_str());

            for (const FieldSnapshot& field : cls.fields) {
                if (field.isStatic || field.isLiteral) continue;
                if (!field.hasRealOffset) continue;   // no runtime offset: no macro
                const std::string fieldName = CppIdentifier(field.name);
                const std::string macro = "OFFSET_" + owner + "_" + fieldName;
                if (!emitted.insert(macro).second) continue;
                file.Printf("#define %s 0x%zxULL\n", macro.c_str(), field.offset);
            }

            for (const MethodSnapshot& method : cls.methods) {
                if (!method.hasRva) continue;         // no code pointer: no macro
                const std::string methodName = CppIdentifier(method.name);
                const std::string macro = "RVA_" + owner + "_" + methodName;
                if (!emitted.insert(macro).second) continue;
                file.Printf("#define %s 0x%llxULL // params: %u\n",
                            macro.c_str(), (unsigned long long)method.rva, method.paramCount);
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
    // Two overloads share a class and a name but not an address. Emitting the
    // same plain name twice makes the disassembler keep only the last one, so a
    // repeated name gets the parameter count appended, and a further tie gets a
    // numeric suffix. The address is unchanged; the suffix only disambiguates.
    std::set<std::string> usedNames;

    for (const ImageSnapshot& image : snapshot) {
        for (const ClassSnapshot& cls : image.classes) {
            const std::string owner = QualifiedName(cls.namespaze, cls.name);
            for (const MethodSnapshot& method : cls.methods) {
                if (!method.hasRva) continue;

                const std::string base = owner + "$$" + method.name;
                std::string symbol = base;
                if (usedNames.count(symbol) != 0) {
                    symbol = base + "/" + std::to_string(method.paramCount) + "p";
                    int tie = 2;
                    while (usedNames.count(symbol) != 0) {
                        symbol = base + "/" + std::to_string(method.paramCount) + "p#" + std::to_string(tie);
                        ++tie;
                    }
                }
                usedNames.insert(symbol);

                // Build a readable signature for the disassembler.
                std::string signature = method.returnType + " " + symbol + "(";
                for (size_t i = 0; i < method.params.size(); ++i) {
                    if (i) signature += ", ";
                    signature += method.params[i].typeName;
                }
                signature += ")";

                file.Printf("%s    {\n      \"Address\": %llu,\n      \"Name\": \"%s\",\n      \"Signature\": \"%s\"\n    }",
                            first ? "" : ",\n",
                            (unsigned long long)method.rva,
                            JsonWriter::EscapeString(symbol.c_str()).c_str(),
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
    // The image must be read from the lowest mapping, where the ELF header is.
    // The load bias is a different number whenever the first PT_LOAD does not
    // start at vaddr 0, and parsing the header at the bias then reads garbage.
    const uintptr_t moduleBase = Il2CppGetModuleBase();
    if (!moduleBase) {
        note = "libil2cpp.so not mapped";
        return false;
    }

    std::vector<uint8_t> image;
    ElfImage::Info info;
    if (!ElfReconstructor::BuildImage(moduleBase, image, info)) {
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
        "            # Any other comment ends the pending RVA: (ABSTRACT), no code\n"
        "            # pointer and signature-unavailable markers all mean the last\n"
        "            # method is finished. Keeping the value would attach it to the\n"
        "            # next method, which is a different address entirely.\n"
        "            if line.lstrip().startswith('//'):\n"
        "                pending_rva = None\n"
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
        "                    print('// DobbyHook((void*)(il2cpp_base + 0x%s),' % pending_rva)\n"
        "                    print('//            (void*)hook_%s_%s,' % (current_class, name))\n"
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

    SafeMemory::Refresh();
    RvaResolver::SetTargetModule("libil2cpp.so");

    SetStatus("enumerating classes");
    g_progress.store(10);

    Snapshot snapshot;
    if (!Collect(snapshot)) {
        // Collect() already fell back to the metadata reader; reaching here means
        // neither route produced a single real class.
        SetStatus("ERROR: no classes enumerated (api and metadata both failed)");
        return;
    }

    g_progress.store(45);
    SetStatus("writing dump.cs");
    const bool csOk = WriteCs(dir + "/dump.cs", snapshot);

    SetStatus("writing il2cpp.h");
    g_progress.store(60);
    const bool sdkOk = WriteSdk(dir + "/il2cpp.h", snapshot);

    SetStatus("writing offsets and symbols");
    g_progress.store(70);
    const bool offsetsOk = WriteOffsets(dir + "/ninmods_real_offsets.h", snapshot);
    const bool scriptOk  = WriteScriptJson(dir + "/script.json", snapshot);

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

    // A dump that could not be written must not be reported as a success: the
    // status line is the only feedback the user gets.
    const bool coreOk = csOk && sdkOk && offsetsOk && scriptOk;
    if (!coreOk) {
        SetStatusf("INCOMPLETE: write failed (cs=%d sdk=%d offsets=%d script=%d)",
                   (int)csOk, (int)sdkOk, (int)offsetsOk, (int)scriptOk);
        __android_log_print(ANDROID_LOG_ERROR, "NINMODS",
                            "dump incomplete in %s: cs=%d sdk=%d offsets=%d script=%d",
                            dir.c_str(), (int)csOk, (int)sdkOk, (int)offsetsOk, (int)scriptOk);
        return;
    }

    SetStatusf("DONE: %s", dir.c_str());
    __android_log_print(ANDROID_LOG_INFO, "NINMODS", "dump complete: %s", dir.c_str());
}

inline void TriggerFullDump() {
    if (g_busy.load()) return;
    std::thread(RunFullDump).detach();
}

} // namespace Dumper
