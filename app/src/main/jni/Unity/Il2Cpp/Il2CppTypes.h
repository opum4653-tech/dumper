#ifndef IL2CPP_TYPES_H
#define IL2CPP_TYPES_H

#include <stdio.h>
#include <string>
#include <inttypes.h>
#include <dlfcn.h>

typedef void(*Il2CppMethodPointer)();
struct MethodInfo;

struct VirtualInvokeData {
    Il2CppMethodPointer methodPtr;
    const MethodInfo* method;
};

struct Il2CppType {
    void* data;
    unsigned int bits;
};

struct Il2CppClass;
struct Il2CppRuntimeInterfaceOffsetPair;

struct Il2CppObject {
    Il2CppClass *klass;
    void *monitor;
};

union Il2CppRGCTXData {
    void* rgctxDataDummy;
    const MethodInfo* method;
    const Il2CppType* type;
    Il2CppClass* klass;
};

struct Il2CppClass_1 {
    void* image;
    void* gc_desc;
    const char* name;
    const char* namespaze;
    Il2CppType* byval_arg;
    Il2CppType* this_arg;
    Il2CppClass* element_class;
    Il2CppClass* castClass;
    Il2CppClass* empty_class;
    Il2CppClass* vt;
};

struct Il2CppClass_2 {
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
    Il2CppRuntimeInterfaceOffsetPair* interfaceOffsets;

    void* static_fields;
    void* rgctx_data;

    Il2CppClass** typeHierarchy;
    void* initializationException;
    uint32_t cctor_started;
    uint32_t cctor_finished;
    size_t align_cctor_thread_id;
    size_t cctor_thread_id;

    void* genericContainerHandle;
    uint32_t instance_size;
    uint32_t actualSize;
    uint32_t element_size;
    int32_t native_size;
    uint32_t static_fields_size;
    uint32_t thread_static_fields_size;
    int32_t thread_static_fields_offset;

    uint32_t flags;
    uint32_t token;

    uint16_t method_count;
    uint16_t property_count;
    uint16_t field_count;
    uint16_t event_count;
    uint16_t nested_type_count;
    uint16_t vtable_count;
    uint16_t interfaces_count;
    uint16_t interface_offsets_count;

    uint8_t typeHierarchyDepth;
    uint8_t genericRecursionDepth;
    uint8_t rank;
    uint8_t minimumAlignment;

    uint8_t naturalAligment;
    uint8_t packingSize;

    uint8_t initialized;

    uint8_t enumtype;
    uint8_t is_generic;
    uint8_t has_references;
    uint8_t init_pending;

    uint8_t size_inited;
    uint8_t has_finalize;
    uint8_t has_cctor;
    uint8_t is_blittable;

    uint8_t is_import_or_windows_runtime;
    uint8_t is_vtable_initialized;
    uint8_t has_initialization_error;
};

struct ParameterInfo {
    const char* name;
    int32_t position;
    uint32_t token;
    const Il2CppType* parameter_type;
};

struct MethodInfo {
    Il2CppMethodPointer methodPointer;
    void* invoker_method;
    const char* name;
    Il2CppClass *klass;
    const Il2CppType *return_type;
    const ParameterInfo* parameters;
    union {
        const Il2CppRGCTXData* rgctx_data;
        void* methodDefinition;
    };
    union {
        void* genericMethod;
        void* genericContainerHandle;
    };
    uint32_t token;
    uint16_t flags;
    uint16_t iflags;
    uint16_t slot;
    uint8_t parameters_count;
    uint8_t is_generic;
    uint8_t is_inflated;
    uint8_t wrapper_type;
    uint8_t is_marshaled_from_native;
};

struct Il2CppString {
    Il2CppClass *klass;
    void *monitor;
    int32_t length;
    uint16_t start_char;

    static Il2CppString *Create(const char *s);
    static Il2CppString *Create(const wchar_t *s, int len);

    int getLength() {
        return length;
    }
};

typedef Il2CppString String;

#endif // IL2CPP_TYPES_H
