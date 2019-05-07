#ifndef WINAPI_THUNK_HELPER_H_
#define WINAPI_THUNK_HELPER_H_

#include <windows.h>
#include <intrin.h>

#if defined(_M_IX86)
#define _LCRT_DEFINE_IAT_SYMBOL_MAKE_NAME(f,prefixed) _CRT_CONCATENATE(_CRT_CONCATENATE(_imp__, f), prefixed)
#elif defined(_M_AMD64)
#define _LCRT_DEFINE_IAT_SYMBOL_MAKE_NAME(f,prefixed) _CRT_CONCATENATE(__imp_, f)
#else
#error "Arch Not Support"
#endif

#define _LCRT_DEFINE_IAT_SYMBOL(ns, f, prefixed)                                                          \
    extern "C" __declspec(selectany) void const* const _LCRT_DEFINE_IAT_SYMBOL_MAKE_NAME(f,prefixed) \
        = reinterpret_cast<void const*>(ns::f)

enum : int {
    __crt_maximum_pointer_shift = sizeof(uintptr_t) * 8
};

inline unsigned int __crt_rotate_pointer_value(unsigned int const value, int const shift) throw() {
    return RotateRight32(value, shift);
}

inline unsigned __int64 __crt_rotate_pointer_value(unsigned __int64 const value, int const shift) throw() {
    return RotateRight64(value, shift);
}

// Fast alternatives to the encode/decode pointer functions that do not use
// the EncodePointer and DecodePointer functions.
template <typename T>
T __crt_fast_decode_pointer(T const p) throw() {
    return reinterpret_cast<T>(
        __crt_rotate_pointer_value(
            reinterpret_cast<uintptr_t>(p) ^ __security_cookie,
            __security_cookie % __crt_maximum_pointer_shift
        )
        );
}

template <typename T>
T __crt_fast_encode_pointer(T const p) throw() {
    return reinterpret_cast<T>(
        __crt_rotate_pointer_value(
            reinterpret_cast<uintptr_t>(p),
            __crt_maximum_pointer_shift - (__security_cookie % __crt_maximum_pointer_shift)
        ) ^ __security_cookie
        );
}

// The primary __crt_fast_encode_pointer template does not work properly 
// when it is called with the argument 'nullptr' because the encoded void*
// pointer is casted back to nullptr_t, and nullptr_t can only represent a
// single value:  the real, unencoded null pointer.  Therefore, we overload
// the function for nullptr_t, and defer the cast until we know the actual
// type that we need.
struct __crt_fast_encoded_nullptr_t {
    template <typename T>
    operator T*() const throw() { return __crt_fast_encode_pointer(static_cast<T*>(nullptr)); }
};

inline __crt_fast_encoded_nullptr_t __crt_fast_encode_pointer(decltype(nullptr)) throw() {
    return __crt_fast_encoded_nullptr_t();
}

template <typename T, typename V>
T* __crt_interlocked_exchange_pointer(T* const volatile* target, V const value) throw() {
    // This is required to silence a spurious unreferenced formal parameter
    // warning.
    UNREFERENCED_PARAMETER(value);

    return reinterpret_cast<T*>(_InterlockedExchangePointer((void**)target, (void*)value));
}

template <typename T, typename E, typename C>
T __crt_interlocked_compare_exchange(T* const volatile target, E const exchange, C const comparand) throw() {
    UNREFERENCED_PARAMETER(exchange);  // These are required to silence spurious
    UNREFERENCED_PARAMETER(comparand); // unreferenced formal parameter warnings.

    static_assert(sizeof(T) == sizeof(LONG), "Type being compared must be same size as a LONG.");
    return static_cast<T>(_InterlockedCompareExchange(
        reinterpret_cast<LONG*>(target), (LONG)exchange, (LONG)comparand));
}

template <typename T, typename E, typename C>
T* __crt_interlocked_compare_exchange_pointer(T* const volatile* target, E const exchange, C const comparand) throw() {
    UNREFERENCED_PARAMETER(exchange);  // These are required to silence spurious
    UNREFERENCED_PARAMETER(comparand); // unreferenced formal parameter warnings.

    return reinterpret_cast<T*>(_InterlockedCompareExchangePointer(
        (void**)target, (void*)exchange, (void*)comparand));
}

template <typename T>
T __crt_interlocked_read(T* const volatile target) throw() {
    static_assert(sizeof(T) == sizeof(LONG), "Type being read must be same size as a LONG.");
    return __crt_interlocked_compare_exchange(target, 0, 0);
}

template <typename T>
T* __crt_interlocked_read_pointer(T* const volatile* target) throw() {
    return __crt_interlocked_compare_exchange_pointer(target, nullptr, nullptr);
}


inline HMODULE __cdecl try_load_library_from_system_directory(wchar_t const* const name) throw() {
    HMODULE const handle = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (handle) {
        return handle;
    }

    // LOAD_LIBRARY_SEARCH_SYSTEM32 is only supported by Windows 7 and above; if
    // the OS does not support this flag, try again without it:
    if (GetLastError() == ERROR_INVALID_PARAMETER) {
        return LoadLibraryExW(name, nullptr, 0);
    }

    return nullptr;
}

inline void* __cdecl invalid_function_sentinel() throw() {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(-1));
}

#endif //WINAPI_THUNK_HELPER_H_