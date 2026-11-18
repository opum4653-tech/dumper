#pragma once

// Attribute bit values taken from the ECMA-335 metadata tables, which is what
// il2cpp reports through il2cpp_class_get_flags / il2cpp_field_get_flags /
// il2cpp_method_get_flags. These are the same numbers as in
// il2cpp-tabledefs.h, restated here so the dumper does not need that header.

#include <cstdint>

namespace Il2CppFlags {

// Field attributes
static constexpr uint32_t kFieldAccessMask   = 0x0007;
static constexpr uint32_t kFieldPrivate      = 0x0001;
static constexpr uint32_t kFieldFamAndAssem  = 0x0002;
static constexpr uint32_t kFieldAssembly     = 0x0003;
static constexpr uint32_t kFieldFamily       = 0x0004;
static constexpr uint32_t kFieldFamOrAssem   = 0x0005;
static constexpr uint32_t kFieldPublic       = 0x0006;
static constexpr uint32_t kFieldStatic       = 0x0010;
static constexpr uint32_t kFieldInitOnly     = 0x0020;
static constexpr uint32_t kFieldLiteral      = 0x0040;

// Method attributes
static constexpr uint32_t kMethodAccessMask  = 0x0007;
static constexpr uint32_t kMethodPrivate     = 0x0001;
static constexpr uint32_t kMethodFamAndAssem = 0x0002;
static constexpr uint32_t kMethodAssem       = 0x0003;
static constexpr uint32_t kMethodFamily      = 0x0004;
static constexpr uint32_t kMethodFamOrAssem  = 0x0005;
static constexpr uint32_t kMethodPublic      = 0x0006;
static constexpr uint32_t kMethodStatic      = 0x0010;
static constexpr uint32_t kMethodFinal       = 0x0020;
static constexpr uint32_t kMethodVirtual     = 0x0040;
static constexpr uint32_t kMethodAbstract    = 0x0400;
static constexpr uint32_t kMethodPinvoke     = 0x2000;

// Type attributes
static constexpr uint32_t kTypeVisibilityMask = 0x00000007;
static constexpr uint32_t kTypePublic         = 0x00000001;
static constexpr uint32_t kTypeNestedPublic   = 0x00000002;
static constexpr uint32_t kTypeNestedPrivate  = 0x00000003;
static constexpr uint32_t kTypeNestedFamily   = 0x00000004;
static constexpr uint32_t kTypeNestedAssembly = 0x00000005;
static constexpr uint32_t kTypeNestedFamOrAssem = 0x00000007;
static constexpr uint32_t kTypeInterface      = 0x00000020;
static constexpr uint32_t kTypeAbstract        = 0x00000080;
static constexpr uint32_t kTypeSealed          = 0x00000100;

inline const char* FieldAccess(uint32_t flags) {
    switch (flags & kFieldAccessMask) {
        case kFieldPrivate:     return "private ";
        case kFieldPublic:      return "public ";
        case kFieldFamily:      return "protected ";
        case kFieldAssembly:
        case kFieldFamAndAssem: return "internal ";
        case kFieldFamOrAssem:  return "protected internal ";
        default:                return "";
    }
}

inline const char* MethodAccess(uint32_t flags) {
    switch (flags & kMethodAccessMask) {
        case kMethodPrivate:     return "private ";
        case kMethodPublic:      return "public ";
        case kMethodFamily:      return "protected ";
        case kMethodAssem:
        case kMethodFamAndAssem: return "internal ";
        case kMethodFamOrAssem:  return "protected internal ";
        default:                 return "";
    }
}

// Types are emitted at namespace scope, so a nested type's original visibility
// (private / protected) is not expressible there. Reporting it verbatim would
// put `private class X` at namespace level, which C# rejects. Nested and
// non-public visibility therefore map to internal; only public stays public.
inline const char* TypeAccess(uint32_t flags) {
    switch (flags & kTypeVisibilityMask) {
        case kTypePublic:
        case kTypeNestedPublic:     return "public ";
        default:                    return "internal ";
    }
}

} // namespace Il2CppFlags
