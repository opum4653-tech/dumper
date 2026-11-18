#include <android/log.h>
#include <map>
#include <jni.h>
#include <unistd.h>
#include "Il2Cpp.h"
#include "xdl.h"

#define g_LogTag "NINMODS"

namespace {
	const void *(*il2cpp_assembly_get_image)(const void *assembly) = nullptr;
	void *(*il2cpp_domain_get)() = nullptr;
	void **(*il2cpp_domain_get_assemblies)(const void *domain, size_t *size) = nullptr;
	const char *(*il2cpp_image_get_name)(void *image) = nullptr;
    void *(*il2cpp_thread_attach)(void *domain) = nullptr;
    void *(*il2cpp_class_from_name)(const void *image, const char *namespaze, const char *name) = nullptr;
    void *(*il2cpp_class_get_field_from_name)(void *klass, const char *name) = nullptr;
    void *(*il2cpp_class_get_method_from_name)(void *klass, const char *name, int argsCount) = nullptr;
    size_t (*il2cpp_field_get_offset)(void *field) = nullptr;
    void *(*il2cpp_object_new)(void *klass) = nullptr;
    void *(*il2cpp_array_new)(void *elementTypeInfo, size_t length) = nullptr;
    Il2CppString *(*il2cpp_string_new)(const char *str) = nullptr;
    Il2CppString *(*il2cpp_string_new_utf16)(const wchar_t *str, int32_t length) = nullptr;
    const char *(*il2cpp_class_get_name)(void *klass) = nullptr;
    void *(*il2cpp_class_get_fields)(void *klass, void **iter) = nullptr;
    const char *(*il2cpp_field_get_name)(void *field) = nullptr;
    void *(*il2cpp_class_get_methods)(void *klass, void **iter) = nullptr;
    const char *(*il2cpp_method_get_name)(void *method) = nullptr;
    uint32_t (*il2cpp_method_get_param_count)(void *method) = nullptr;

    size_t (*il2cpp_image_get_class_count)(const void *image) = nullptr;
    const void *(*il2cpp_image_get_class)(const void *image, size_t index) = nullptr;
    const char *(*il2cpp_class_get_namespace)(const void *klass) = nullptr;
    const void *(*il2cpp_class_get_parent)(const void *klass) = nullptr;
    const void *(*il2cpp_field_get_type)(const void *field) = nullptr;
    const char *(*il2cpp_type_get_name)(const void *type) = nullptr;
    const void *(*il2cpp_method_get_return_type)(const void *method) = nullptr;
    const char *(*il2cpp_method_get_param_name)(const void *method, uint32_t index) = nullptr;
}

void Il2CppAttach() {
    void *handle = xdl_open("libil2cpp.so", 0);
    if (!handle) return;
    *(void **)&il2cpp_assembly_get_image = xdl_sym(handle, "il2cpp_assembly_get_image", nullptr);
    *(void **)&il2cpp_domain_get = xdl_sym(handle, "il2cpp_domain_get", nullptr);
    *(void **)&il2cpp_domain_get_assemblies = xdl_sym(handle, "il2cpp_domain_get_assemblies", nullptr);
    *(void **)&il2cpp_image_get_name = xdl_sym(handle, "il2cpp_image_get_name", nullptr);
    *(void **)&il2cpp_thread_attach = xdl_sym(handle, "il2cpp_thread_attach", nullptr);
    *(void **)&il2cpp_class_from_name = xdl_sym(handle, "il2cpp_class_from_name", nullptr);
    *(void **)&il2cpp_class_get_field_from_name = xdl_sym(handle, "il2cpp_class_get_field_from_name", nullptr);
    *(void **)&il2cpp_class_get_method_from_name = xdl_sym(handle, "il2cpp_class_get_method_from_name", nullptr);
    *(void **)&il2cpp_field_get_offset = xdl_sym(handle, "il2cpp_field_get_offset", nullptr);
    *(void **)&il2cpp_object_new = xdl_sym(handle, "il2cpp_object_new", nullptr);
    *(void **)&il2cpp_array_new = xdl_sym(handle, "il2cpp_array_new", nullptr);
    *(void **)&il2cpp_string_new = xdl_sym(handle, "il2cpp_string_new", nullptr);
    *(void **)&il2cpp_string_new_utf16 = xdl_sym(handle, "il2cpp_string_new_utf16", nullptr);
    *(void **)&il2cpp_class_get_name = xdl_sym(handle, "il2cpp_class_get_name", nullptr);
    *(void **)&il2cpp_class_get_fields = xdl_sym(handle, "il2cpp_class_get_fields", nullptr);
    *(void **)&il2cpp_field_get_name = xdl_sym(handle, "il2cpp_field_get_name", nullptr);
    *(void **)&il2cpp_class_get_methods = xdl_sym(handle, "il2cpp_class_get_methods", nullptr);
    *(void **)&il2cpp_method_get_name = xdl_sym(handle, "il2cpp_method_get_name", nullptr);
    *(void **)&il2cpp_method_get_param_count = xdl_sym(handle, "il2cpp_method_get_param_count", nullptr);

    *(void **)&il2cpp_image_get_class_count = xdl_sym(handle, "il2cpp_image_get_class_count", nullptr);
    *(void **)&il2cpp_image_get_class = xdl_sym(handle, "il2cpp_image_get_class", nullptr);
    *(void **)&il2cpp_class_get_namespace = xdl_sym(handle, "il2cpp_class_get_namespace", nullptr);
    *(void **)&il2cpp_class_get_parent = xdl_sym(handle, "il2cpp_class_get_parent", nullptr);
    *(void **)&il2cpp_field_get_type = xdl_sym(handle, "il2cpp_field_get_type", nullptr);
    *(void **)&il2cpp_type_get_name = xdl_sym(handle, "il2cpp_type_get_name", nullptr);
    *(void **)&il2cpp_method_get_return_type = xdl_sym(handle, "il2cpp_method_get_return_type", nullptr);
    *(void **)&il2cpp_method_get_param_name = xdl_sym(handle, "il2cpp_method_get_param_name", nullptr);
}

const void *Il2CppGetImageByName(const char *image) {
    if (!image) return nullptr;
    if (!il2cpp_domain_get || !il2cpp_domain_get_assemblies || !il2cpp_assembly_get_image || !il2cpp_image_get_name) {
        Il2CppAttach();
    }
    if (!il2cpp_domain_get || !il2cpp_domain_get_assemblies || !il2cpp_assembly_get_image || !il2cpp_image_get_name) {
        return nullptr;
    }
    void *domain = il2cpp_domain_get();
    if (!domain) return nullptr;
    if (il2cpp_thread_attach) il2cpp_thread_attach(domain);

    size_t size = 0;
    void **assemblies = il2cpp_domain_get_assemblies(domain, &size);
    if (!assemblies || size == 0) return nullptr;

    for (size_t i = 0; i < size; ++i) {
        if (!assemblies[i]) continue;
        const void *img = il2cpp_assembly_get_image(assemblies[i]);
        if (!img) continue;
        const char *name = il2cpp_image_get_name((void *) img);
        if (name && strcmp(name, image) == 0) {
            return img;
        }
    }
    return nullptr;
}

const void *Il2CppGetClassType(const void *image, const char *namespaze, const char *clazz) {
    if (!il2cpp_class_from_name) Il2CppAttach();
    if (!il2cpp_class_from_name || !image || !clazz) return nullptr;
    return il2cpp_class_from_name(image, namespaze ? namespaze : "", clazz);
}

void *Il2CppGetMethodOffset(const void *image, const char *namespaze, const char *clazz, const char *name, int argsCount) {
    if (!image || !clazz || !name) return nullptr;
    const void *klass = Il2CppGetClassType(image, namespaze, clazz);
    if (!klass) return nullptr;
    if (!il2cpp_class_get_method_from_name) Il2CppAttach();
    if (!il2cpp_class_get_method_from_name) return nullptr;
    void *method = il2cpp_class_get_method_from_name((void *) klass, name, argsCount);
    if (!method) return nullptr;
    return *(void **) method;
}

void *Il2CppGetMethodOffset(const char *image, const char *namespaze, const char *clazz, const char *name, int argsCount) {
    if (!image) return nullptr;
    const void *img = Il2CppGetImageByName(image);
    if (!img) return nullptr;
    return Il2CppGetMethodOffset(img, namespaze, clazz, name, argsCount);
}

void *Il2CppGetFieldOffset(const void *image, const char *namespaze, const char *clazz, const char *name) {
    if (!image) return nullptr;
    const void *klass = Il2CppGetClassType(image, namespaze, clazz);
    if (!klass) return nullptr;
    if (!il2cpp_class_get_field_from_name || !il2cpp_field_get_offset) Il2CppAttach();
    if (!il2cpp_class_get_field_from_name || !il2cpp_field_get_offset) return nullptr;
    void *field = il2cpp_class_get_field_from_name((void *) klass, name);
    if (!field) return nullptr;
    return (void *) il2cpp_field_get_offset(field);
}

void *Il2CppGetFieldOffset(const char *image, const char *namespaze, const char *clazz, const char *name) {
    const void *img = Il2CppGetImageByName(image);
    if (!img) return nullptr;
    return Il2CppGetFieldOffset(img, namespaze, clazz, name);
}

void *Il2CppGetMethodOffset(const void *clazz, const char *name, int argsCount) {
    if (!clazz) return nullptr;
    if (!il2cpp_class_get_method_from_name) Il2CppAttach();
    if (!il2cpp_class_get_method_from_name) return nullptr;
    void *method = il2cpp_class_get_method_from_name((void *) clazz, name, argsCount);
    if (!method) return nullptr;
    return *(void **) method;
}

void *Il2CppCreateClassInstance(const char *image, const char *namespaze, const char *clazz) {
    const void *img = Il2CppGetImageByName(image);
    if (!img) return nullptr;
    const void *klass = Il2CppGetClassType(img, namespaze, clazz);
    if (!klass) return nullptr;
    if (!il2cpp_object_new) Il2CppAttach();
    if (!il2cpp_object_new) return nullptr;
    return il2cpp_object_new((void *) klass);
}

void *Il2CppCreateArray(const char *image, const char *namespaze, const char *clazz, size_t length) {
    const void *img = Il2CppGetImageByName(image);
    if (!img) return nullptr;
    const void *klass = Il2CppGetClassType(img, namespaze, clazz);
    if (!klass) return nullptr;
    if (!il2cpp_array_new) Il2CppAttach();
    if (!il2cpp_array_new) return nullptr;
    return il2cpp_array_new((void *) klass, length);
}

Il2CppString *Il2CppString::Create(const char *s) {
    if (!il2cpp_string_new) Il2CppAttach();
    return il2cpp_string_new ? il2cpp_string_new(s) : nullptr;
}

Il2CppString *Il2CppString::Create(const wchar_t *s, int len) {
    if (!il2cpp_string_new_utf16) Il2CppAttach();
    return il2cpp_string_new_utf16 ? il2cpp_string_new_utf16(s, len) : nullptr;
}

const char *Il2CppClassGetName(const void *klass) {
    if (!il2cpp_class_get_name) Il2CppAttach();
    return (il2cpp_class_get_name && klass) ? il2cpp_class_get_name((void*)klass) : "";
}

void *Il2CppClassGetFields(const void *klass, void **iter) {
    if (!il2cpp_class_get_fields) Il2CppAttach();
    return (il2cpp_class_get_fields && klass) ? il2cpp_class_get_fields((void*)klass, iter) : nullptr;
}

const char *Il2CppFieldGetName(const void *field) {
    if (!il2cpp_field_get_name) Il2CppAttach();
    return (il2cpp_field_get_name && field) ? il2cpp_field_get_name((void*)field) : "";
}

size_t Il2CppFieldGetOffset(const void *field) {
    if (!il2cpp_field_get_offset) Il2CppAttach();
    return (il2cpp_field_get_offset && field) ? il2cpp_field_get_offset((void*)field) : 0;
}

void *Il2CppClassGetMethods(const void *klass, void **iter) {
    if (!il2cpp_class_get_methods) Il2CppAttach();
    return (il2cpp_class_get_methods && klass) ? il2cpp_class_get_methods((void*)klass, iter) : nullptr;
}

const char *Il2CppMethodGetName(const void *method) {
    if (!il2cpp_method_get_name) Il2CppAttach();
    return (il2cpp_method_get_name && method) ? il2cpp_method_get_name((void*)method) : "";
}

uint32_t Il2CppMethodGetParamCount(const void *method) {
    if (!il2cpp_method_get_param_count) Il2CppAttach();
    return (il2cpp_method_get_param_count && method) ? il2cpp_method_get_param_count((void*)method) : 0;
}

size_t Il2CppGetAssemblyCount() {
    if (!il2cpp_domain_get || !il2cpp_domain_get_assemblies) Il2CppAttach();
    if (!il2cpp_domain_get || !il2cpp_domain_get_assemblies) return 0;
    void *domain = il2cpp_domain_get();
    if (!domain) return 0;
    if (il2cpp_thread_attach) il2cpp_thread_attach(domain);
    size_t count = 0;
    il2cpp_domain_get_assemblies(domain, &count);
    return count;
}

const void *Il2CppGetAssembly(size_t index) {
    if (!il2cpp_domain_get || !il2cpp_domain_get_assemblies) Il2CppAttach();
    if (!il2cpp_domain_get || !il2cpp_domain_get_assemblies) return nullptr;
    void *domain = il2cpp_domain_get();
    if (!domain) return nullptr;
    size_t count = 0;
    void **assemblies = il2cpp_domain_get_assemblies(domain, &count);
    if (!assemblies || index >= count) return nullptr;
    return assemblies[index];
}

const void *Il2CppAssemblyGetImage(const void *assembly) {
    if (!il2cpp_assembly_get_image) Il2CppAttach();
    return (il2cpp_assembly_get_image && assembly) ? il2cpp_assembly_get_image(assembly) : nullptr;
}

const char *Il2CppImageGetName(const void *image) {
    if (!il2cpp_image_get_name) Il2CppAttach();
    return (il2cpp_image_get_name && image) ? il2cpp_image_get_name((void *)image) : "";
}

size_t Il2CppImageGetClassCount(const void *image) {
    if (!il2cpp_image_get_class_count) Il2CppAttach();
    return (il2cpp_image_get_class_count && image) ? il2cpp_image_get_class_count(image) : 0;
}

const void *Il2CppImageGetClass(const void *image, size_t index) {
    if (!il2cpp_image_get_class) Il2CppAttach();
    return (il2cpp_image_get_class && image) ? il2cpp_image_get_class(image, index) : nullptr;
}

const char *Il2CppClassGetNamespace(const void *klass) {
    if (!klass) return "";
    if (il2cpp_class_get_namespace) {
        const char *ns = il2cpp_class_get_namespace(klass);
        if (ns) return ns;
    }
    Il2CppClass_1 *c1 = (Il2CppClass_1 *)klass;
    return (c1 && c1->namespaze) ? c1->namespaze : "";
}

const void *Il2CppClassGetParent(const void *klass) {
    if (!klass) return nullptr;
    if (il2cpp_class_get_parent) return il2cpp_class_get_parent(klass);
    Il2CppClass_2 *c2 = (Il2CppClass_2 *)((uintptr_t)klass + sizeof(Il2CppClass_1));
    return c2 ? c2->parent : nullptr;
}

const void *Il2CppFieldGetType(const void *field) {
    if (!field) return nullptr;
    if (il2cpp_field_get_type) return il2cpp_field_get_type(field);
    return nullptr;
}

const char *Il2CppTypeGetName(const void *type) {
    if (!type) return "var";
    if (il2cpp_type_get_name) {
        const char *tn = il2cpp_type_get_name(type);
        if (tn) return tn;
    }
    return "var";
}

const void *Il2CppMethodGetReturnType(const void *method) {
    if (!method) return nullptr;
    if (il2cpp_method_get_return_type) return il2cpp_method_get_return_type(method);
    MethodInfo *m = (MethodInfo *)method;
    return m ? m->return_type : nullptr;
}

const char *Il2CppMethodGetParamName(const void *method, uint32_t index) {
    if (!method) return "";
    if (il2cpp_method_get_param_name) {
        const char *pn = il2cpp_method_get_param_name(method, index);
        if (pn) return pn;
    }
    MethodInfo *m = (MethodInfo *)method;
    if (m && m->parameters && index < m->parameters_count) {
        return m->parameters[index].name ? m->parameters[index].name : "";
    }
    return "";
}
