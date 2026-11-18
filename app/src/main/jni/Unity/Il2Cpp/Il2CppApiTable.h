#pragma once

// Master list of the il2cpp API surface resolved at runtime.
//
// Every entry mirrors a real export from il2cpp-api-functions.h. The name
// strings are the exact symbols libil2cpp.so exports; the parameter lists are
// the documented signatures. An export that is absent on a given Unity build
// stays null, and the caller then takes an explicit fallback path or reports the
// gap. Nothing here is invented.

#define NINMODS_IL2CPP_API_LIST(X)                                                       \
    /* domain / thread */                                                                \
    X(void *,            il2cpp_domain_get,                 (void))                      \
    X(void **,           il2cpp_domain_get_assemblies,      (const void *domain, size_t *size)) \
    X(void *,            il2cpp_thread_attach,              (void *domain))              \
    X(bool,              il2cpp_is_vm_thread,               (void *thread))              \
    /* assembly / image */                                                               \
    X(const void *,      il2cpp_assembly_get_image,         (const void *assembly))      \
    X(const char *,      il2cpp_image_get_name,             (const void *image))         \
    X(const char *,      il2cpp_image_get_filename,         (const void *image))         \
    X(size_t,            il2cpp_image_get_class_count,      (const void *image))         \
    X(const void *,      il2cpp_image_get_class,            (const void *image, size_t index)) \
    X(const void *,      il2cpp_get_corlib,                 (void))                      \
    X(const void *,      il2cpp_class_get_image,            (void *klass))               \
    /* class */                                                                          \
    X(void *,            il2cpp_class_from_name,            (const void *image, const char *namespaze, const char *name)) \
    X(void *,            il2cpp_class_from_system_type,     (void *reflectionType))      \
    X(void *,            il2cpp_class_from_type,            (const void *type))          \
    X(const void *,      il2cpp_class_get_type,             (void *klass))               \
    X(const char *,      il2cpp_class_get_name,             (void *klass))               \
    X(const char *,      il2cpp_class_get_namespace,        (void *klass))               \
    X(void *,            il2cpp_class_get_parent,           (void *klass))               \
    X(void *,            il2cpp_class_get_declaring_type,   (void *klass))               \
    X(void *,            il2cpp_class_get_nested_types,     (void *klass, void **iter))  \
    X(void *,            il2cpp_class_get_interfaces,       (void *klass, void **iter))  \
    X(void *,            il2cpp_class_get_fields,           (void *klass, void **iter))  \
    X(const void *,      il2cpp_class_get_properties,       (void *klass, void **iter))  \
    X(const void *,      il2cpp_class_get_events,           (void *klass, void **iter))  \
    X(const void *,      il2cpp_class_get_methods,          (void *klass, void **iter))  \
    X(const void *,      il2cpp_class_get_method_from_name, (void *klass, const char *name, int argsCount)) \
    X(void *,            il2cpp_class_get_field_from_name,  (void *klass, const char *name)) \
    X(int,               il2cpp_class_get_flags,            (const void *klass))         \
    X(bool,              il2cpp_class_is_enum,              (const void *klass))         \
    X(bool,              il2cpp_class_is_valuetype,         (const void *klass))         \
    X(bool,              il2cpp_class_is_interface,         (const void *klass))         \
    X(bool,              il2cpp_class_is_abstract,          (const void *klass))         \
    X(bool,              il2cpp_class_is_generic,           (const void *klass))         \
    X(bool,              il2cpp_class_is_inflated,          (const void *klass))         \
    X(int32_t,           il2cpp_class_instance_size,        (void *klass))               \
    X(int32_t,           il2cpp_class_value_size,           (void *klass, uint32_t *align)) \
    X(int,               il2cpp_class_get_rank,             (const void *klass))         \
    /* field */                                                                          \
    X(const char *,      il2cpp_field_get_name,             (void *field))               \
    X(const void *,      il2cpp_field_get_type,             (void *field))               \
    X(size_t,            il2cpp_field_get_offset,           (void *field))               \
    X(int,               il2cpp_field_get_flags,            (void *field))               \
    X(void *,            il2cpp_field_get_parent,           (void *field))               \
    X(bool,              il2cpp_field_is_literal,           (void *field))               \
    X(void,              il2cpp_field_static_get_value,     (void *field, void *value))  \
    /* method */                                                                         \
    X(const char *,      il2cpp_method_get_name,            (const void *method))        \
    X(uint32_t,          il2cpp_method_get_param_count,     (const void *method))        \
    X(const void *,      il2cpp_method_get_param,           (const void *method, uint32_t index)) \
    X(const char *,      il2cpp_method_get_param_name,      (const void *method, uint32_t index)) \
    X(const void *,      il2cpp_method_get_return_type,     (const void *method))        \
    X(uint32_t,          il2cpp_method_get_flags,           (const void *method, uint32_t *iflags)) \
    X(bool,              il2cpp_method_is_generic,          (const void *method))        \
    X(bool,              il2cpp_method_is_inflated,         (const void *method))        \
    X(bool,              il2cpp_method_is_instance,         (const void *method))        \
    /* property */                                                                       \
    X(const char *,      il2cpp_property_get_name,          (void *prop))                \
    X(const void *,      il2cpp_property_get_get_method,    (void *prop))                \
    X(const void *,      il2cpp_property_get_set_method,    (void *prop))                \
    /* event */                                                                          \
    X(const char *,      il2cpp_event_get_name,             (void *event))               \
    X(const void *,      il2cpp_event_get_add_method,       (void *event))               \
    X(const void *,      il2cpp_event_get_remove_method,    (void *event))               \
    /* type / runtime */                                                                 \
    X(char *,            il2cpp_type_get_name,              (const void *type))          \
    X(int,               il2cpp_type_get_type,              (const void *type))          \
    X(bool,              il2cpp_type_is_byref,              (const void *type))          \
    X(uint32_t,          il2cpp_type_get_attrs,             (const void *type))          \
    X(void,              il2cpp_free,                       (void *ptr))                 \
    X(void *,            il2cpp_object_new,                 (const void *klass))         \
    X(void *,            il2cpp_array_new,                  (void *elementTypeInfo, size_t length)) \
    X(void *,            il2cpp_string_new,                 (const char *str))           \
    X(void *,            il2cpp_string_new_utf16,           (const uint16_t *text, int32_t len))
