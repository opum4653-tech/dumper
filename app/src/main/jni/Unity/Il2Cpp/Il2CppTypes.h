#ifndef IL2CPP_TYPES_H
#define IL2CPP_TYPES_H

// Type definitions for the il2cpp runtime objects this dumper actually touches.
//
// Only three layouts are declared, and each is limited to the fields that sit at
// a fixed position in every published Unity release:
//
//   Il2CppObject        klass, monitor
//   Il2CppString        klass, monitor, length, chars[]
//   Il2CppArray         klass, monitor, bounds, max_length, data[]
//
// Il2CppClass, FieldInfo and MethodInfo are deliberately NOT laid out. Their
// field order changes between Unity releases, so every access goes through an
// il2cpp export (class_get_name, field_get_offset, method_get_name, ...). The
// single raw read the project makes into MethodInfo is the first field,
// methodPointer, which is stable and is expressed here as MethodInfoPrefix.

#include <cstdint>
#include <cstddef>

typedef void (*Il2CppMethodPointer)();
typedef uintptr_t il2cpp_array_size_t;

struct Il2CppClass;
struct FieldInfo;

struct Il2CppObject {
    void* klass;
    void* monitor;
};

// The one stable part of MethodInfo: methodPointer is the first member in every
// published layout. Nothing else in the struct is read.
struct MethodInfoPrefix {
    Il2CppMethodPointer methodPointer;
};

struct Il2CppString {
    void*    klass;
    void*    monitor;
    int32_t  length;
    uint16_t chars[1];

    static Il2CppString* Create(const char* s);
    static Il2CppString* Create(const wchar_t* s, int len);
};

typedef Il2CppString String;

struct Il2CppArrayBounds {
    il2cpp_array_size_t length;
    intptr_t            lower_bound;
};

template <typename T>
struct Il2CppArray {
    void*               klass;
    void*               monitor;
    Il2CppArrayBounds*  bounds;
    il2cpp_array_size_t max_length;
    T                   vector[1];

    il2cpp_array_size_t Length() const { return max_length; }
    T* Data() { return vector; }
    T& At(il2cpp_array_size_t i) { return vector[i]; }
};

#endif // IL2CPP_TYPES_H
