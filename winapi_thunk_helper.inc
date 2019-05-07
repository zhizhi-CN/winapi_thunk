#include "winapi_thunk_helper.h"

#pragma section(".CRT$XID",    long, read) // CRT C Initializers


namespace {
// Generate enumerators for each of the modules:
enum module_id : unsigned {
#define APPLY(_SYMBOL, _NAME) _SYMBOL,
    APPLY_TO_LATE_BOUND_MODULES(APPLY)
#undef APPLY

    module_id_count
};

// Generate a table of module names that can be indexed by the module_id
// enumerators:
static wchar_t const* const module_names[module_id_count] =
{
#define APPLY(_SYMBOL, _NAME) _CRT_WIDE(_NAME),
    APPLY_TO_LATE_BOUND_MODULES(APPLY)
#undef APPLY
};

// Generate enumerators for each of the functions:
enum function_id : unsigned {
#define APPLY(_FUNCTION, _MODULES) _CRT_CONCATENATE(_FUNCTION, _id),
    APPLY_TO_LATE_BOUND_FUNCTIONS(APPLY)
#undef APPLY

    function_id_count
};

// Generate a typedef for each function of the form function_pft.
#define APPLY(_FUNCTION, _MODULES) \
            using _CRT_CONCATENATE(_FUNCTION, _pft) = decltype(_FUNCTION)*;
APPLY_TO_LATE_BOUND_FUNCTIONS(APPLY)
#undef APPLY
}

// This table stores the module handles that we have obtained via LoadLibrary.
// If a handle is null, we have not yet attempted to load that module.  If a
// handle is -1 (INVALID_HANDLE_VALUE), we have attempted to load the module
// but the attempt failed.
static HMODULE module_handles[module_id_count];

// This table stores the function pointers that we have loaded dynamically.  The
// function pointers are stored in encoded form via __crt_fast_encode_ponter.  If
// a function pointer is an encoded null pointer, we have not yet attempted to
// get that function pointer.  If a function pointer is an encoded -1, we have
// attempted to get that function pointer but the attempt failed.
static void* encoded_function_pointers[function_id_count];

static void __cdecl __uninitialize_winapi_thunks() {
    for (HMODULE& module : module_handles) {
        if (module) {
            if (module != INVALID_HANDLE_VALUE) {
                FreeLibrary(module);
            }
            module = nullptr;
        }
    }
}

static void __cdecl __initialize_winapi_thunks() {
    void* const encoded_nullptr = __crt_fast_encode_pointer(nullptr);

    for (void*& p : encoded_function_pointers) {
        p = encoded_nullptr;
    }

    atexit(__uninitialize_winapi_thunks);
}

__declspec(allocate(".CRT$XID")) static decltype(__initialize_winapi_thunks)* ___Initialization = __initialize_winapi_thunks;


static HMODULE __cdecl try_get_module(module_id const id) throw() {
    // First check to see if we've cached the module handle:
    if (HMODULE const cached_handle = __crt_interlocked_read_pointer(module_handles + id)) {
        if (cached_handle == INVALID_HANDLE_VALUE) {
            return nullptr;
        }

        return cached_handle;
    }

    // If we haven't yet cached the module handle, try to load the library.  If
    // this fails, cache the sentinel handle value INVALID_HANDLE_VALUE so that
    // we don't attempt to load the module again:
    HMODULE const new_handle = try_load_library_from_system_directory(module_names[id]);
    if (!new_handle) {
        if (HMODULE const cached_handle = __crt_interlocked_exchange_pointer(module_handles + id, INVALID_HANDLE_VALUE)) {
            _ASSERTE(cached_handle == INVALID_HANDLE_VALUE);
        }

        return nullptr;
    }

    // Swap the new handle into the cache.  If the cache no longer contained a
    // null handle, then some other thread loaded the module and cached the
    // handle while we were doing the same.  In that case, we free the handle
    // once to maintain the reference count:
    if (HMODULE const cached_handle = __crt_interlocked_exchange_pointer(module_handles + id, new_handle)) {
        _ASSERTE(cached_handle == new_handle);
        FreeLibrary(new_handle);
    }

    return new_handle;
}

static HMODULE __cdecl try_get_first_available_module(
    module_id const* const first,
    module_id const* const last
) throw() {
    for (module_id const* it = first; it != last; ++it) {
        HMODULE const handle = try_get_module(*it);
        if (handle) {
            return handle;
        }
    }

    return nullptr;
}

static __forceinline void* __cdecl try_get_proc_address_from_first_available_module(
    char      const* const name,
    module_id const* const first_module_id,
    module_id const* const last_module_id
) throw() {
    HMODULE const module_handle = try_get_first_available_module(first_module_id, last_module_id);
    if (!module_handle) {
        return nullptr;
    }

    return reinterpret_cast<void*>(GetProcAddress(module_handle, name));
}

static void* __cdecl try_get_function(
    function_id      const id,
    char      const* const name,
    module_id const* const first_module_id,
    module_id const* const last_module_id
) throw() {
    // First check to see if we've cached the function pointer:
    {
        void* const cached_fp = __crt_fast_decode_pointer(
            __crt_interlocked_read_pointer(encoded_function_pointers + id));

        if (cached_fp == invalid_function_sentinel()) {
            return nullptr;
        }

        if (cached_fp) {
            return cached_fp;
        }
    }

    // If we haven't yet cached the function pointer, try to import it from any
    // of the modules in which it might be defined.  If this fails, cache the
    // sentinel pointer so that we don't attempt to load this function again:
    void* const new_fp = try_get_proc_address_from_first_available_module(name, first_module_id, last_module_id);
    if (!new_fp) {
        void* const cached_fp = __crt_fast_decode_pointer(
            __crt_interlocked_exchange_pointer(
                encoded_function_pointers + id,
                __crt_fast_encode_pointer(invalid_function_sentinel())));

        if (cached_fp) {
            _ASSERTE(cached_fp == invalid_function_sentinel());
        }

        return nullptr;
    }

    // Swap the newly obtained function pointer into the cache.  The cache may
    // no longer contain an encoded null pointer if another thread obtained the
    // function address while we were doing the same (both threads should have
    // gotten the same function pointer):
    {
        void* const cached_fp = __crt_fast_decode_pointer(
            __crt_interlocked_exchange_pointer(
                encoded_function_pointers + id,
                __crt_fast_encode_pointer(new_fp)));

        if (cached_fp) {
            _ASSERTE(cached_fp == new_fp);
        }
    }
    return new_fp;
}

// Generate accessors that wrap the general try_get_function for each function,
// passing the correct set of candidate modules and returning a function pointer
// of the correct type:
#define APPLY(_FUNCTION, _MODULES)                                                                        \
        static _CRT_CONCATENATE(_FUNCTION, _pft) __cdecl _CRT_CONCATENATE(try_get_, _FUNCTION)() throw()  \
        {                                                                                                 \
            static module_id const candidate_modules[] = _CRT_UNPARENTHESIZE(_MODULES);                   \
                                                                                                          \
            return reinterpret_cast<_CRT_CONCATENATE(_FUNCTION, _pft)>(try_get_function(                  \
                _CRT_CONCATENATE(_FUNCTION, _id),                                                         \
                _CRT_STRINGIZE(_FUNCTION),                                                                \
                candidate_modules,                                                                        \
                candidate_modules + __crt_countof(candidate_modules)));                                   \
        }
APPLY_TO_LATE_BOUND_FUNCTIONS(APPLY)
#undef APPLY
