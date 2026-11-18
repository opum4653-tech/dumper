#ifndef IL2CPP_API_H
#define IL2CPP_API_H

#include "Il2CppTypes.h"

const void *Il2CppGetImageByName(const char *image);
const void *Il2CppGetClassType(const void *image, const char *namespaze, const char *clazz);
void *Il2CppGetMethodOffset(const void *image, const char *namespaze, const char *clazz, const char *name, int argsCount = 0);
void *Il2CppGetMethodOffset(const char *image, const char *namespaze, const char *clazz, const char *name, int argsCount = 0);
void *Il2CppGetMethodOffset(const void *image, const char *clazz, const char *name, int argsCount = 0);
void *Il2CppGetMethodOffset(const char *image, const char *clazz, const char *name, int argsCount = 0);
void *Il2CppGetFieldOffset(const void *image, const char *namespaze, const char *clazz, const char *name);
void *Il2CppGetFieldOffset(const char *image, const char *namespaze, const char *clazz, const char *name);
void *Il2CppGetFieldOffset(const void *image, const char *clazz, const char *name);
void *Il2CppGetFieldOffset(const char *image, const char *clazz, const char *name);
void *Il2CppGetMethodOffset(const void *clazz, const char *name, int argsCount = 0);

void Il2CppAttach();
void *Il2CppCreateClassInstance(const char *image, const char *namespaze, const char *clazz);
void *Il2CppCreateArray(const char *image, const char *namespaze, const char *clazz, size_t length);

const char *Il2CppClassGetName(const void *klass);
void *Il2CppClassGetFields(const void *klass, void **iter);
const char *Il2CppFieldGetName(const void *field);
size_t Il2CppFieldGetOffset(const void *field);
void *Il2CppClassGetMethods(const void *klass, void **iter);
const char *Il2CppMethodGetName(const void *method);
uint32_t Il2CppMethodGetParamCount(const void *method);

// Full In-Memory Traversal APIs
size_t Il2CppGetAssemblyCount();
const void *Il2CppGetAssembly(size_t index);
const void *Il2CppAssemblyGetImage(const void *assembly);
const char *Il2CppImageGetName(const void *image);
size_t Il2CppImageGetClassCount(const void *image);
const void *Il2CppImageGetClass(const void *image, size_t index);

const char *Il2CppClassGetNamespace(const void *klass);
const void *Il2CppClassGetParent(const void *klass);
const void *Il2CppFieldGetType(const void *field);
const char *Il2CppTypeGetName(const void *type);

const void *Il2CppMethodGetReturnType(const void *method);
const char *Il2CppMethodGetParamName(const void *method, uint32_t index);

#endif // IL2CPP_API_H
