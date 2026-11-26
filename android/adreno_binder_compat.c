// Qualcomm's custom drivers on Android 15: built into libadrenotools' hook_impl (android/totkrecompandroid/app/src/main/cpp/
// CMakeLists.txt), which libadrenotools preloads into the driver's own linker namespace.
//
// A recent Qualcomm driver package (891.x) has a helper that imports AIBinder_Class_setTransactionCodeToFunctionNameMap, a
// Binder tracing annotation of API 36. On Android 15 libbinder_ndk.so lacks it, the driver fails to load ("cannot locate
// symbol"), and the Vulkan loader silently falls back to the system driver: the custom driver chosen in the launcher is not
// the one running. Here hook_impl (linked -z global, so the driver's libraries see its symbols) defines the function: it
// forwards to libbinder_ndk.so's when the system has it, and on older systems only that optional annotation (a name for each
// transaction code, used in traces) is left out - Binder transactions themselves are untouched.
//
// From igawa6/peanuts-native's notes (2026-11-11, Odin 3, Adreno 830, Android 15: the same 891.14 driver went from
// Vulkan 1.3.284 / the system's 0x80320048 to 1.3.351 / 0x8037b00e); this is our own implementation.
#include <dlfcn.h>
#include <stddef.h>

typedef struct AIBinder_Class AIBinder_Class;
typedef void (*set_map_fn)(AIBinder_Class *, const char **, size_t);

__attribute__((visibility("default"))) void AIBinder_Class_setTransactionCodeToFunctionNameMap(
    AIBinder_Class *clazz, const char **transactionCodeToFunctionMap, size_t length) {
    static set_map_fn real;
    static int looked;
    if (!__atomic_load_n(&looked, __ATOMIC_ACQUIRE)) {
        void *h = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_NOLOAD);
        if (!h) h = dlopen("libbinder_ndk.so", RTLD_NOW);
        set_map_fn f = h ? (set_map_fn)dlsym(h, "AIBinder_Class_setTransactionCodeToFunctionNameMap") : NULL;
        if (f == AIBinder_Class_setTransactionCodeToFunctionNameMap) f = NULL;  // (ourselves, through the global lookup)
        __atomic_store_n(&real, f, __ATOMIC_RELAXED);
        __atomic_store_n(&looked, 1, __ATOMIC_RELEASE);
    }
    set_map_fn f = __atomic_load_n(&real, __ATOMIC_RELAXED);
    if (f) f(clazz, transactionCodeToFunctionMap, length);
}
