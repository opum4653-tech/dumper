// Il2Cpp.cpp - runtime binding and traversal for the il2cpp API.
//
// Target: Android arm64-v8a / armeabi-v7a, C++17.
//
// Three things this file guarantees:
//
//   1. Every API pointer comes from an export libil2cpp.so actually publishes.
//      Nothing is fabricated and a missing export stays null.
//   2. Class enumeration has three real routes, tried in order, and the route
//      that succeeded is reported so the dump can state it:
//        a) il2cpp_image_get_class_count / il2cpp_image_get_class  (2018.3+)
//        b) il2cpp_class_for_each                                  (newer)
//        c) System.Reflection.Assembly::Load + GetTypes            (always)
//   3. The method code pointer is read from MethodInfo's first field, which is
//      methodPointer in every il2cpp version, and is never invented.

#include <android/log.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

#include "Il2Cpp.h"
#include "Il2Cpp/Il2CppApiTable.h"
#include "xdl.h"
#include "../Core/SafeMemory.h"
#include "../Core/ElfImage.h"

#define NINMODS_TAG "NINMODS"

namespace {

#define NINMODS_API_STORAGE(ret, name, params) ret (*name) params = nullptr;
NINMODS_IL2CPP_API_LIST(NINMODS_API_STORAGE)
#undef NINMODS_API_STORAGE

uintptr_t  g_base = 0;        // load bias: runtime_address = g_base + vaddr
uintptr_t  g_moduleBase = 0;  // lowest mapping: where the ELF header actually sits
bool       g_attached = false;      // every required export bound
bool       g_bindComplete = false;  // the bind pass actually ran to completion
std::mutex g_attachMutex;
std::mutex g_cacheMutex;

std::string g_missing;

// Every class the runtime knows about, in enumeration order.
std::vector<const void*> g_allClasses;
bool g_allClassesBuilt = false;

// Classes attributed to each image, built lazily.
std::map<const void*, std::vector<const void*>> g_imageClasses;

// Enumeration route that actually worked: 0 = none, 1 = image_get_class,
// 2 = class_for_each, 3 = reflection.
int g_enumMode = 0;

// Set when per-image attribution was impossible: g_allClasses then holds every
// class once, and exactly one image is allowed to expose that list. Returning it
// from every image would emit the entire class set once per assembly.
bool g_globalListOnly = false;
const void* g_globalListOwner = nullptr;

bool CoreBound() {
    return il2cpp_domain_get && il2cpp_domain_get_assemblies &&
           il2cpp_assembly_get_image && il2cpp_image_get_name;
}

// ---------------------------------------------------------------------------
// Reflection route: Assembly.Load(name) -> GetTypes() -> class_from_system_type
// ---------------------------------------------------------------------------
struct ReflectionBridge {
    const void* loadMethod   = nullptr;
    void*       loadPointer  = nullptr;
    const void* typesMethod  = nullptr;
    void*       typesPointer = nullptr;
    const void* assemblyClass = nullptr;
    bool        ready        = false;
};

ReflectionBridge& Reflection() {
    static ReflectionBridge s_bridge;
    return s_bridge;
}

bool PrepareReflection() {
    ReflectionBridge& b = Reflection();
    if (b.ready) return b.loadPointer && b.typesPointer;
    b.ready = true;

    if (!il2cpp_get_corlib || !il2cpp_class_from_name || !il2cpp_class_get_method_from_name) return false;

    const void* corlib = il2cpp_get_corlib();
    if (!corlib) return false;

    const void* assemblyClass = il2cpp_class_from_name(corlib, "System.Reflection", "Assembly");
    if (!assemblyClass) return false;
    b.assemblyClass = assemblyClass;

    b.loadMethod  = il2cpp_class_get_method_from_name((void*)assemblyClass, "Load", 1);
    b.typesMethod = il2cpp_class_get_method_from_name((void*)assemblyClass, "GetTypes", 0);

    // MethodInfo's first field is methodPointer in every published layout. Read
    // it through SafeMemory so a layout surprise fails the route instead of
    // taking the process down.
    if (b.loadMethod) {
        void* code = nullptr;
        if (SafeMemory::Read((uintptr_t)b.loadMethod, &code, sizeof(code)) && code &&
            SafeMemory::IsExecutable((uintptr_t)code)) {
            b.loadPointer = code;
        }
    }
    if (b.typesMethod) {
        void* code = nullptr;
        if (SafeMemory::Read((uintptr_t)b.typesMethod, &code, sizeof(code)) && code &&
            SafeMemory::IsExecutable((uintptr_t)code)) {
            b.typesPointer = code;
        }
    }

    return b.loadPointer && b.typesPointer;
}

} // namespace

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------
void Il2CppSetBase(uintptr_t base) { g_base = base; }
uintptr_t Il2CppGetBase() { return g_base; }
uintptr_t Il2CppGetModuleBase() { return g_moduleBase; }

void Il2CppAttach() {
    std::lock_guard<std::mutex> lock(g_attachMutex);
    if (g_bindComplete) return;

    void* handle = xdl_open("libil2cpp.so", 0);
    if (!handle) {
        // The module is normally already mapped into the game process. A forced
        // load is a last resort and is only tried when the plain open failed.
        handle = xdl_open("libil2cpp.so", XDL_TRY_FORCE_LOAD);
    }
    if (!handle) {
        __android_log_print(ANDROID_LOG_ERROR, NINMODS_TAG,
                            "libil2cpp.so not mapped; attach skipped");
        return;
    }

    if (!g_base) {
        // dli_fbase is the lowest mapping of the object, which is
        // loadBias + align_down(first PT_LOAD p_vaddr, page). Running the ELF
        // header parse on it converts that back to the load bias that turns a
        // vaddr into a runtime address.
        void* anchor = xdl_sym(handle, "il2cpp_domain_get", nullptr);
        if (!anchor) anchor = xdl_sym(handle, "il2cpp_class_from_name", nullptr);

        Dl_info dli;
        if (anchor && dladdr(anchor, &dli) && dli.dli_fbase) {
            const uintptr_t lowest = (uintptr_t)dli.dli_fbase;
            g_moduleBase = lowest;

            ElfImage::Info info;
            if (ElfImage::Parse(lowest, info)) {
                g_base = (uintptr_t)info.loadBias;
                __android_log_print(ANDROID_LOG_INFO, NINMODS_TAG,
                                    "libil2cpp lowest=0x%lx firstVaddr=0x%llx bias=0x%lx",
                                    (unsigned long)lowest, (unsigned long long)info.minVaddr,
                                    (unsigned long)g_base);
            } else {
                // Header unreadable: the lowest mapping is the only anchor left,
                // and for a normal linker layout (first PT_LOAD at vaddr 0) it is
                // also the bias.
                g_base = lowest;
            }
        }
    }

    g_missing.clear();
    auto bind = [&](const char* name) -> void* {
        void* p = xdl_sym(handle, name, nullptr);
        if (!p) p = xdl_dsym(handle, name, nullptr);
        if (!p) {
            if (!g_missing.empty()) g_missing += "\n";
            g_missing += name;
        }
        return p;
    };

#define NINMODS_API_BIND(ret, name, params) *(void**)(&name) = bind(#name);
    NINMODS_IL2CPP_API_LIST(NINMODS_API_BIND)
#undef NINMODS_API_BIND

    xdl_close(handle);

    g_bindComplete = true;
    g_attached = CoreBound();

    __android_log_print(g_attached ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, NINMODS_TAG,
                        "attach base=0x%lx core=%s",
                        (unsigned long)g_base, g_attached ? "ok" : "fail");

    if (g_attached && il2cpp_domain_get && il2cpp_thread_attach) {
        void* domain = il2cpp_domain_get();
        if (domain) il2cpp_thread_attach(domain);
    }
}

void Il2CppThreadAttach() {
    if (!Il2CppIsReady()) return;
    if (il2cpp_domain_get && il2cpp_thread_attach) {
        void* domain = il2cpp_domain_get();
        if (domain) il2cpp_thread_attach(domain);
    }
}

bool Il2CppIsReady() {
    if (!g_bindComplete) Il2CppAttach();
    return g_attached;
}

void* Il2CppApi(const char* symbolName) {
    if (!symbolName) return nullptr;
    Il2CppAttach();

    struct Entry { const char* name; void* value; };
#define NINMODS_API_ENTRY(ret, name, params) { #name, (void*)(name) },
    static const Entry kEntries[] = {
        NINMODS_IL2CPP_API_LIST(NINMODS_API_ENTRY)
    };
#undef NINMODS_API_ENTRY

    for (const Entry& e : kEntries) {
        if (strcmp(e.name, symbolName) == 0) return e.value;
    }
    return nullptr;
}

const char* Il2CppMissingApis() {
    Il2CppAttach();
    return g_missing.c_str();
}

// ---------------------------------------------------------------------------
// Assemblies and images
// ---------------------------------------------------------------------------
size_t Il2CppGetAssemblyCount() {
    if (!Il2CppIsReady()) return 0;
    if (!il2cpp_domain_get || !il2cpp_domain_get_assemblies) return 0;

    void* domain = il2cpp_domain_get();
    if (!domain) return 0;

    size_t count = 0;
    void** assemblies = il2cpp_domain_get_assemblies(domain, &count);
    if (!assemblies || count == 0 || count > 10000) return 0;
    return count;
}

const void* Il2CppGetAssembly(size_t index) {
    if (!Il2CppIsReady()) return nullptr;
    if (!il2cpp_domain_get || !il2cpp_domain_get_assemblies) return nullptr;

    void* domain = il2cpp_domain_get();
    if (!domain) return nullptr;

    size_t count = 0;
    void** assemblies = il2cpp_domain_get_assemblies(domain, &count);
    if (!assemblies || index >= count) return nullptr;
    return assemblies[index];
}

const void* Il2CppAssemblyGetImage(const void* assembly) {
    // Il2CppAssembly's image field position is not stable across versions, so
    // it is only read through the export. No layout guess is made here.
    if (!assembly || !il2cpp_assembly_get_image) return nullptr;
    return il2cpp_assembly_get_image(assembly);
}

const char* Il2CppImageGetName(const void* image) {
    if (!image) return "";
    if (!il2cpp_image_get_name) return "";
    const char* name = il2cpp_image_get_name(image);
    return name ? name : "";
}

const void* Il2CppGetImageByName(const char* imageName) {
    if (!imageName) return nullptr;
    if (!Il2CppIsReady()) return nullptr;

    const size_t count = Il2CppGetAssemblyCount();
    for (size_t i = 0; i < count; ++i) {
        const void* image = Il2CppAssemblyGetImage(Il2CppGetAssembly(i));
        if (!image) continue;
        const char* name = Il2CppImageGetName(image);
        if (name && strcmp(name, imageName) == 0) return image;
    }
    return nullptr;
}

namespace {

// Route (a): per-image class API. Returns false when the build lacks it.
bool BuildClassesViaImageApi() {
    if (!il2cpp_image_get_class_count || !il2cpp_image_get_class) return false;

    const size_t asmCount = Il2CppGetAssemblyCount();
    if (asmCount == 0) return false;

    bool any = false;
    for (size_t a = 0; a < asmCount; ++a) {
        const void* image = Il2CppAssemblyGetImage(Il2CppGetAssembly(a));
        if (!image) continue;

        const size_t classCount = il2cpp_image_get_class_count(image);
        if (classCount == 0 || classCount > 500000) continue;

        std::vector<const void*>& bucket = g_imageClasses[image];
        bucket.reserve(classCount);
        for (size_t c = 0; c < classCount; ++c) {
            const void* klass = il2cpp_image_get_class(image, c);
            if (!klass) continue;
            bucket.push_back(klass);
            g_allClasses.push_back(klass);
            any = true;
        }
    }
    return any;
}

// Route (b): il2cpp_class_for_each. Attributes classes to images through
// il2cpp_class_get_image, or by verifying class_from_name when that export is
// absent.
void ClassForEachCallback(void* klass, void* userData) {
    auto* sink = static_cast<std::vector<const void*>*>(userData);
    if (sink && klass) sink->push_back(klass);
}

bool BuildClassesViaForEach() {
    if (!il2cpp_class_for_each) return false;

    std::vector<const void*> collected;
    collected.reserve(8192);
    il2cpp_class_for_each(&ClassForEachCallback, &collected);
    if (collected.empty()) return false;

    g_allClasses = collected;

    // Attribute to images. get_image is exact; the fallback verifies each class
    // through class_from_name, which is correct but only used when needed.
    const size_t asmCount = Il2CppGetAssemblyCount();
    for (size_t a = 0; a < asmCount; ++a) {
        const void* image = Il2CppAssemblyGetImage(Il2CppGetAssembly(a));
        if (!image) continue;
        g_imageClasses[image]; // create the bucket
    }

    if (il2cpp_class_get_image) {
        size_t attributed = 0;
        for (const void* klass : collected) {
            const void* image = il2cpp_class_get_image((void*)klass);
            if (!image) continue;
            g_imageClasses[image].push_back(klass);
            ++attributed;
        }
        if (attributed == 0 && !g_imageClasses.empty()) {
            g_globalListOnly = true;
            g_globalListOwner = g_imageClasses.begin()->first;
        }
        return true;
    }

    if (!il2cpp_class_from_name || !il2cpp_class_get_name || !il2cpp_class_get_namespace) {
        // No attribution possible. The list is still real, so it is handed out
        // exactly once through g_globalListOwner instead of once per assembly.
        if (!g_imageClasses.empty()) {
            g_globalListOnly = true;
            g_globalListOwner = g_imageClasses.begin()->first;
        }
        return true;
    }

    for (const void* klass : collected) {
        const char* ns = il2cpp_class_get_namespace((void*)klass);
        const char* nm = il2cpp_class_get_name((void*)klass);
        if (!nm) continue;
        for (auto& pair : g_imageClasses) {
            const void* resolved = il2cpp_class_from_name(pair.first, ns ? ns : "", nm);
            if (resolved == klass) {
                pair.second.push_back(klass);
                break;
            }
        }
    }

    // class_from_name may resolve to nothing on a build that renames or trims
    // types. Fall back to the single-owner handoff rather than losing the dump.
    bool anyAttributed = false;
    for (const auto& pair : g_imageClasses) {
        if (!pair.second.empty()) { anyAttributed = true; break; }
    }
    if (!anyAttributed && !g_imageClasses.empty()) {
        g_globalListOnly = true;
        g_globalListOwner = g_imageClasses.begin()->first;
        __android_log_print(ANDROID_LOG_WARN, NINMODS_TAG,
                            "class_for_each: per-image attribution failed, "
                            "emitting %zu classes once under one image",
                            collected.size());
    }
    return true;
}

// Route (c): reflection. Assembly.Load(simpleName).GetTypes() gives the
// reflection type array, and class_from_system_type converts each entry.
bool BuildClassesViaReflection() {
    if (!PrepareReflection()) return false;
    if (!il2cpp_class_from_system_type || !il2cpp_string_new) return false;

    const ReflectionBridge& bridge = Reflection();
    // Assembly.Load(string) is static: (Il2CppString*, const MethodInfo*).
    // GetTypes() is an instance method: (this, const MethodInfo*).
    typedef void* (*LoadFn)(Il2CppString*, void*);
    typedef Il2CppArray<void*>* (*GetTypesFn)(void*, void*);

    const size_t asmCount = Il2CppGetAssemblyCount();
    bool any = false;

    for (size_t a = 0; a < asmCount; ++a) {
        const void* image = Il2CppAssemblyGetImage(Il2CppGetAssembly(a));
        if (!image) continue;

        const char* imageName = Il2CppImageGetName(image);
        if (!imageName || !imageName[0]) continue;

        // Strip the .dll suffix: Assembly.Load takes the simple name.
        std::string simpleName(imageName);
        const size_t dot = simpleName.rfind('.');
        if (dot != std::string::npos) simpleName.resize(dot);

        Il2CppString* managedName = (Il2CppString*)il2cpp_string_new(simpleName.c_str());
        if (!managedName) continue;

        void* reflectionAssembly = ((LoadFn)bridge.loadPointer)(managedName, nullptr);
        if (!reflectionAssembly) continue;

        Il2CppArray<void*>* reflectionTypes = ((GetTypesFn)bridge.typesPointer)(reflectionAssembly, nullptr);
        if (!reflectionTypes) continue;

        std::vector<const void*>& bucket = g_imageClasses[image];
        const uintptr_t length = reflectionTypes->max_length;
        if (length == 0 || length > 500000) continue;

        // The element array is only readable through SafeMemory: an unexpected
        // array layout would otherwise dereference a bad pointer.
        std::vector<void*> elements(length);
        if (!SafeMemory::Read((uintptr_t)reflectionTypes->vector, elements.data(),
                              length * sizeof(void*))) {
            continue;
        }

        for (uintptr_t i = 0; i < length; ++i) {
            void* reflectionType = elements[i];
            if (!reflectionType) continue;
            void* klass = il2cpp_class_from_system_type(reflectionType);
            if (!klass) continue;
            bucket.push_back(klass);
            g_allClasses.push_back(klass);
            any = true;
        }
    }
    return any;
}

void EnsureClassCache() {
    if (g_allClassesBuilt) return;
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    if (g_allClassesBuilt) return;

    // A failed pass is retried, because the overlay can render before the game
    // has loaded its assemblies. Each success is cheap to detect and each failure
    // is expensive (a reflection route walks every assembly), so the retry is
    // rate limited: one attempt per second at most.
    using Clock = std::chrono::steady_clock;
    static Clock::time_point s_lastAttempt{};
    const Clock::time_point now = Clock::now();
    if (s_lastAttempt.time_since_epoch().count() != 0 &&
        (now - s_lastAttempt) < std::chrono::seconds(1)) {
        return;
    }
    s_lastAttempt = now;

    g_allClasses.clear();
    g_imageClasses.clear();
    g_globalListOnly = false;
    g_globalListOwner = nullptr;

    // The flag is only set once a route actually produced classes. An early call
    // would otherwise pin the cache to "no route" for the rest of the process and
    // the dump would report zero classes forever.
    if (BuildClassesViaImageApi())      { g_enumMode = 1; g_allClassesBuilt = true; return; }
    if (BuildClassesViaForEach())       { g_enumMode = 2; g_allClassesBuilt = true; return; }
    if (BuildClassesViaReflection())    { g_enumMode = 3; g_allClassesBuilt = true; return; }

    g_enumMode = 0;
    __android_log_print(ANDROID_LOG_WARN, NINMODS_TAG,
                        "no class enumeration route available yet; will retry");
}

} // namespace

size_t Il2CppImageGetClassCount(const void* image) {
    if (!image || !Il2CppIsReady()) return 0;
    EnsureClassCache();

    // When per-image attribution failed the buckets exist but are empty, so the
    // lookup below would report 0 for the owner too and the whole class set
    // would be dropped. The global-list handoff is therefore checked first.
    if (g_globalListOnly) {
        return (image == g_globalListOwner) ? g_allClasses.size() : 0;
    }

    auto it = g_imageClasses.find(image);
    return (it != g_imageClasses.end()) ? it->second.size() : 0;
}

const void* Il2CppImageGetClass(const void* image, size_t index) {
    if (!image || !Il2CppIsReady()) return nullptr;
    EnsureClassCache();

    if (g_globalListOnly) {
        if (image != g_globalListOwner) return nullptr;
        return (index < g_allClasses.size()) ? g_allClasses[index] : nullptr;
    }

    auto it = g_imageClasses.find(image);
    if (it == g_imageClasses.end()) return nullptr;
    return (index < it->second.size()) ? it->second[index] : nullptr;
}

bool Il2CppCanEnumerateClasses() {
    if (!Il2CppIsReady()) return false;
    EnsureClassCache();
    return g_enumMode != 0;
}

const char* Il2CppEnumerationMode() {
    if (!Il2CppIsReady()) return "none";
    EnsureClassCache();
    switch (g_enumMode) {
        case 1: return "image_get_class";
        case 2: return "class_for_each";
        case 3: return "reflection";
        default: return "none";
    }
}

// ---------------------------------------------------------------------------
// Class
// ---------------------------------------------------------------------------
const void* Il2CppGetClassType(const void* image, const char* namespaze, const char* clazz) {
    if (!Il2CppIsReady() || !image || !clazz || !il2cpp_class_from_name) return nullptr;
    return il2cpp_class_from_name(image, namespaze ? namespaze : "", clazz);
}

const char* Il2CppClassGetName(const void* klass) {
    if (!klass || !il2cpp_class_get_name) return "";
    const char* name = il2cpp_class_get_name((void*)klass);
    return name ? name : "";
}

const char* Il2CppClassGetNamespace(const void* klass) {
    if (!klass || !il2cpp_class_get_namespace) return "";
    const char* ns = il2cpp_class_get_namespace((void*)klass);
    return ns ? ns : "";
}

const void* Il2CppClassGetParent(const void* klass) {
    if (!klass || !il2cpp_class_get_parent) return nullptr;
    return il2cpp_class_get_parent((void*)klass);
}

int Il2CppClassGetFlags(const void* klass) {
    if (!klass || !il2cpp_class_get_flags) return 0;
    return il2cpp_class_get_flags(klass);
}

bool Il2CppClassIsEnum(const void* klass) {
    return (klass && il2cpp_class_is_enum) ? il2cpp_class_is_enum(klass) : false;
}

bool Il2CppClassIsValueType(const void* klass) {
    return (klass && il2cpp_class_is_valuetype) ? il2cpp_class_is_valuetype(klass) : false;
}

bool Il2CppClassIsInterface(const void* klass) {
    return (klass && il2cpp_class_is_interface) ? il2cpp_class_is_interface(klass) : false;
}

bool Il2CppClassIsAbstract(const void* klass) {
    return (klass && il2cpp_class_is_abstract) ? il2cpp_class_is_abstract(klass) : false;
}

bool Il2CppClassIsGeneric(const void* klass) {
    return (klass && il2cpp_class_is_generic) ? il2cpp_class_is_generic(klass) : false;
}

const void* Il2CppClassGetImage(const void* klass) {
    if (!klass) return nullptr;
    if (il2cpp_class_get_image) return il2cpp_class_get_image((void*)klass);
    return nullptr;
}

const void* Il2CppClassFromType(const void* type) {
    if (!type || !il2cpp_class_from_type) return nullptr;
    return il2cpp_class_from_type(type);
}

int32_t Il2CppClassGetValueSize(const void* klass, uint32_t* align) {
    if (!klass || !il2cpp_class_value_size) return 0;
    return il2cpp_class_value_size((void*)klass, align);
}

int32_t Il2CppClassGetInstanceSize(const void* klass) {
    if (!klass || !il2cpp_class_instance_size) return 0;
    return il2cpp_class_instance_size((void*)klass);
}

// ---------------------------------------------------------------------------
// Field
// ---------------------------------------------------------------------------
void* Il2CppClassGetFields(const void* klass, void** iter) {
    if (!klass || !iter || !il2cpp_class_get_fields) return nullptr;
    return il2cpp_class_get_fields((void*)klass, iter);
}

const char* Il2CppFieldGetName(const void* field) {
    if (!field || !il2cpp_field_get_name) return "";
    const char* name = il2cpp_field_get_name((void*)field);
    return name ? name : "";
}

size_t Il2CppFieldGetOffset(const void* field) {
    if (!field || !il2cpp_field_get_offset) return 0;
    return il2cpp_field_get_offset((void*)field);
}

bool Il2CppFieldOffsetAvailable() {
    return il2cpp_field_get_offset != nullptr;
}

const void* Il2CppFieldGetType(const void* field) {
    if (!field || !il2cpp_field_get_type) return nullptr;
    return il2cpp_field_get_type((void*)field);
}

int Il2CppFieldGetFlags(const void* field) {
    if (!field || !il2cpp_field_get_flags) return 0;
    return il2cpp_field_get_flags((void*)field);
}

bool Il2CppFieldIsLiteral(const void* field) {
    return (field && il2cpp_field_is_literal) ? il2cpp_field_is_literal((void*)field) : false;
}

// ---------------------------------------------------------------------------
// Method
// ---------------------------------------------------------------------------
void* Il2CppClassGetMethods(const void* klass, void** iter) {
    if (!klass || !iter || !il2cpp_class_get_methods) return nullptr;
    return (void*)il2cpp_class_get_methods((void*)klass, iter);
}

const char* Il2CppMethodGetName(const void* method) {
    if (!method || !il2cpp_method_get_name) return "";
    const char* name = il2cpp_method_get_name(method);
    return name ? name : "";
}

uint32_t Il2CppMethodGetParamCount(const void* method) {
    if (!method || !il2cpp_method_get_param_count) return 0;
    return il2cpp_method_get_param_count(method);
}

uint32_t Il2CppMethodGetFlags(const void* method) {
    if (!method || !il2cpp_method_get_flags) return 0;
    uint32_t iflags = 0;
    return il2cpp_method_get_flags(method, &iflags);
}

const void* Il2CppMethodGetReturnType(const void* method) {
    if (!method || !il2cpp_method_get_return_type) return nullptr;
    return il2cpp_method_get_return_type(method);
}

const void* Il2CppMethodGetParamType(const void* method, uint32_t index) {
    if (!method || !il2cpp_method_get_param) return nullptr;
    return il2cpp_method_get_param(method, index);
}

const char* Il2CppMethodGetParamName(const void* method, uint32_t index) {
    if (!method || !il2cpp_method_get_param_name) return "";
    const char* name = il2cpp_method_get_param_name(method, index);
    return name ? name : "";
}

bool Il2CppMethodIsGeneric(const void* method) {
    return (method && il2cpp_method_is_generic) ? il2cpp_method_is_generic(method) : false;
}

bool Il2CppMethodIsInflated(const void* method) {
    return (method && il2cpp_method_is_inflated) ? il2cpp_method_is_inflated(method) : false;
}

void* Il2CppMethodGetPointer(const void* method) {
    if (!method) return nullptr;
    // MethodInfo::methodPointer is the first field in every published il2cpp
    // layout; the pointer is a code address in libil2cpp.so or in a thunk.
    void* code = nullptr;
    if (!SafeMemory::Read((uintptr_t)method, &code, sizeof(code))) return nullptr;

    // A null or non-executable pointer means the method has no compiled body
    // here (abstract, generic definition, runtime icall). Report it as such.
    if (!code) return nullptr;
    if (!SafeMemory::IsExecutable((uintptr_t)code)) return nullptr;
    return code;
}

// ---------------------------------------------------------------------------
// Type
// ---------------------------------------------------------------------------
const char* Il2CppTypeGetName(const void* type) {
    if (!type || !il2cpp_type_get_name) return "";

    char* name = il2cpp_type_get_name(type);
    if (!name) return "";

    // il2cpp hands back a heap string whose ownership differs between versions
    // (some builds allocate, some point into metadata). Copying into a
    // thread-local buffer and never freeing is the only choice that cannot
    // crash either way; the buffer is reused, so this allocates once per thread.
    static thread_local std::string s_buffer;
    s_buffer.assign(name);
    return s_buffer.c_str();
}

int Il2CppTypeGetKind(const void* type) {
    if (!type || !il2cpp_type_get_type) return -1;
    return il2cpp_type_get_type(type);
}

// ---------------------------------------------------------------------------
// Convenience lookups
// ---------------------------------------------------------------------------
void* Il2CppGetMethodOffset(const void* image, const char* namespaze, const char* clazz,
                            const char* name, int argsCount) {
    const void* klass = Il2CppGetClassType(image, namespaze, clazz);
    if (!klass || !name || !il2cpp_class_get_method_from_name) return nullptr;

    const void* method = il2cpp_class_get_method_from_name((void*)klass, name, argsCount);
    if (!method) return nullptr;
    return Il2CppMethodGetPointer(method);
}

void* Il2CppGetMethodOffset(const char* image, const char* namespaze, const char* clazz,
                            const char* name, int argsCount) {
    const void* img = Il2CppGetImageByName(image);
    if (!img) return nullptr;
    return Il2CppGetMethodOffset(img, namespaze, clazz, name, argsCount);
}

void* Il2CppGetFieldOffset(const void* image, const char* namespaze, const char* clazz, const char* name) {
    const void* klass = Il2CppGetClassType(image, namespaze, clazz);
    if (!klass || !name || !il2cpp_class_get_field_from_name) return nullptr;

    void* field = il2cpp_class_get_field_from_name((void*)klass, name);
    if (!field) return nullptr;
    return (void*)Il2CppFieldGetOffset(field);
}

void* Il2CppGetFieldOffset(const char* image, const char* namespaze, const char* clazz, const char* name) {
    const void* img = Il2CppGetImageByName(image);
    if (!img) return nullptr;
    return Il2CppGetFieldOffset(img, namespaze, clazz, name);
}

// ---------------------------------------------------------------------------
// Il2CppString
// ---------------------------------------------------------------------------
Il2CppString* Il2CppString::Create(const char* s) {
    if (!Il2CppIsReady() || !il2cpp_string_new || !s) return nullptr;
    return (Il2CppString*)il2cpp_string_new(s);
}

Il2CppString* Il2CppString::Create(const wchar_t* s, int len) {
    if (!Il2CppIsReady() || !il2cpp_string_new_utf16 || !s) return nullptr;
    return (Il2CppString*)il2cpp_string_new_utf16((const uint16_t*)s, len);
}
