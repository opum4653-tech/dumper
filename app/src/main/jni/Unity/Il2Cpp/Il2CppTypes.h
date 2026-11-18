#ifndef IL2CPP_TYPES_H
#define IL2CPP_TYPES_H

// Minimal type definitions for the il2cpp runtime objects this dumper touches.
//
// Only structures that are actually read are declared, and each one is limited
// to the fields at positions that are stable across Unity versions:
//
//   Il2CppObject   klass, monitor
//   Il2CppString   klass, monitor, length, chars[]
//   Il2CppArray    klass, monitor, bounds, max_length, data[]
//
// Class, field, method and image objects are always handled through the il2cpp
// API (class_get_name, field_get_offset, method_get_name, ...), never through a
// hand-rolled Il2CppClass struct. That removes the entire class of bugs that
// comes from a hard-coded Il2CppClass layout drifting between Unity releases.

#include <cstdint>
#include <cstddef>

typedef void (*Il2CppMethodPointer)();

struct Il2CppObject {
    void* klass;
    void* monitor;
};

struct Il2CppType {
    void* data;
    unsigned int bits;
};

struct Il2CppClass;
struct MethodInfo;
struct FieldInfo;

struct MethodInfo {
    Il2CppMethodPointer methodPointer;
    Il2CppMethodPointer virtualMethodPointer;
    void* invoker_method;
    const char* name;
    Il2CppClass *klass;
    const Il2CppType *return_type;
    const void* parameters;
    void* methodDefinition;
    void* genericContainer;
    uint32_t token;
    uint16_t flags;
    uint16_t iflags;
    uint16_t slot;
    uint8_t parameters_count;
};

struct FieldInfo {
    const char* name;
    const Il2CppType* type;
    Il2CppClass *parent;
    int32_t offset;
    uint32_t token;
};

struct Il2CppClass {
    const void* image;
    void* gc_desc;
    const char* name;
    const char* namespaze;
    Il2CppType byval_arg;
    Il2CppType this_arg;
    Il2CppClass* element_class;
    Il2CppClass* castClass;
    Il2CppClass* declaringType;
    Il2CppClass* parent;
    void* generic_class;
    void* typeDefinition;
    void* interopData;
    Il2CppClass* klass;
    void* fields;
    void* events;
    void* properties;
    const MethodInfo** methods;
    Il2CppClass** nestedTypes;
    Il2CppClass** implementedInterfaces;
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
    uintptr_t length;
    intptr_t  lower_bound;
};

template <typename T>
struct Il2CppArray {
    void*             klass;
    void*             monitor;
    Il2CppArrayBounds* bounds;
    uintptr_t          max_length;
    T                  vector[1];

    uintptr_t Length() const { return max_length; }
    T* Data() { return vector; }
    T& At(uintptr_t i) { return vector[i]; }
};

#endif // IL2CPP_TYPES_H
