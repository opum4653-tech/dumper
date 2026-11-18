#ifndef IL2CPP_API_H
#define IL2CPP_API_H

#include <cstddef>
#include <cstdint>
#include "Il2CppTypes.h"

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------
// Binds every entry of the API table against libil2cpp.so in this process and
// records the module load bias. Safe to call repeatedly; the work happens once.
void Il2CppAttach();

// Attaches the calling thread to the IL2CPP domain so managed methods
// and runtime APIs can be invoked safely without assertions or crashes.
void Il2CppThreadAttach();

// True once the core API (domain, assemblies, image, class) is bound.
bool Il2CppIsReady();

// Load bias of libil2cpp.so: the value that turns a vaddr into a runtime
// address (runtime_address = bias + vaddr). 0 when the module is not mapped.
uintptr_t Il2CppGetBase();
void      Il2CppSetBase(uintptr_t base);

// Lowest mapping of libil2cpp.so, i.e. the address the ELF header is mapped at.
// This is NOT the load bias: it equals bias + align_down(first PT_LOAD vaddr,
// page). Reading the ELF header, dumping the image and parsing program headers
// all need this value, never the bias.
uintptr_t Il2CppGetModuleBase();

// Value of an entry from the API table, by name. Returns nullptr for a name the
// table does not carry or an export this Unity build does not provide.
void* Il2CppApi(const char* symbolName);

// Human-readable list of the exports that failed to bind, one per line.
// Empty when everything resolved.
const char* Il2CppMissingApis();

// ---------------------------------------------------------------------------
// Assemblies and images
// ---------------------------------------------------------------------------
size_t        Il2CppGetAssemblyCount();
const void*   Il2CppGetAssembly(size_t index);
const void*   Il2CppAssemblyGetImage(const void* assembly);
const char*   Il2CppImageGetName(const void* image);
const void*   Il2CppGetImageByName(const char* imageName);

// Number of classes an image declares, and the class at an index. Uses
// il2cpp_image_get_class_count / il2cpp_image_get_class when the build exports
// them, and otherwise reads the image's type range through the metadata API.
size_t        Il2CppImageGetClassCount(const void* image);
const void*   Il2CppImageGetClass(const void* image, size_t index);

// True when class enumeration is available by any route.
bool          Il2CppCanEnumerateClasses();
// What route enumeration uses: "image_get_class", "class_for_each", "reflection"
// or "none".
const char*   Il2CppEnumerationMode();

// ---------------------------------------------------------------------------
// Class
// ---------------------------------------------------------------------------
const void*   Il2CppGetClassType(const void* image, const char* namespaze, const char* clazz);
const void*   Il2CppClassFromType(const void* type);
int32_t       Il2CppClassGetValueSize(const void* klass, uint32_t* align);
int32_t       Il2CppClassGetInstanceSize(const void* klass);
const char*   Il2CppClassGetName(const void* klass);
const char*   Il2CppClassGetNamespace(const void* klass);
const void*   Il2CppClassGetParent(const void* klass);
int           Il2CppClassGetFlags(const void* klass);
bool          Il2CppClassIsEnum(const void* klass);
bool          Il2CppClassIsValueType(const void* klass);
bool          Il2CppClassIsInterface(const void* klass);
bool          Il2CppClassIsAbstract(const void* klass);
bool          Il2CppClassIsGeneric(const void* klass);
const void*   Il2CppClassGetImage(const void* klass);

// ---------------------------------------------------------------------------
// Field
// ---------------------------------------------------------------------------
void*         Il2CppClassGetFields(const void* klass, void** iter);
const char*   Il2CppFieldGetName(const void* field);
size_t        Il2CppFieldGetOffset(const void* field);
// True when the build exports il2cpp_field_get_offset. When it does not, no
// field has a runtime offset and none may be invented.
bool          Il2CppFieldOffsetAvailable();
const void*   Il2CppFieldGetType(const void* field);
int           Il2CppFieldGetFlags(const void* field);
bool          Il2CppFieldIsLiteral(const void* field);

// ---------------------------------------------------------------------------
// Method
// ---------------------------------------------------------------------------
void*         Il2CppClassGetMethods(const void* klass, void** iter);
const char*   Il2CppMethodGetName(const void* method);
uint32_t      Il2CppMethodGetParamCount(const void* method);
uint32_t      Il2CppMethodGetFlags(const void* method);
const void*   Il2CppMethodGetReturnType(const void* method);
const void*   Il2CppMethodGetParamType(const void* method, uint32_t index);
const char*   Il2CppMethodGetParamName(const void* method, uint32_t index);
bool          Il2CppMethodIsGeneric(const void* method);
bool          Il2CppMethodIsInflated(const void* method);

// Raw code pointer a MethodInfo holds. Null for abstract, generic and
// runtime-provided methods; the dumper writes no RVA for those.
void*         Il2CppMethodGetPointer(const void* method);

// ---------------------------------------------------------------------------
// Type
// ---------------------------------------------------------------------------
const char*   Il2CppTypeGetName(const void* type);
int           Il2CppTypeGetKind(const void* type);

// ---------------------------------------------------------------------------
// Convenience lookups used by the overlay's touch bridge
// ---------------------------------------------------------------------------
void*         Il2CppGetMethodOffset(const void* image, const char* namespaze, const char* clazz, const char* name, int argsCount = 0);
void*         Il2CppGetMethodOffset(const char* image, const char* namespaze, const char* clazz, const char* name, int argsCount = 0);
void*         Il2CppGetFieldOffset(const void* image, const char* namespaze, const char* clazz, const char* name);
void*         Il2CppGetFieldOffset(const char* image, const char* namespaze, const char* clazz, const char* name);

#endif // IL2CPP_API_H
