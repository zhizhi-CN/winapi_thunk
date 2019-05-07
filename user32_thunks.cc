#include <windows.h>
#include <crtdbg.h>

//////////////////////////////////////////////////////////////////////////
#define APPLY_TO_LATE_BOUND_MODULES(APPLY)                                               \
        APPLY(user32,                      "user32"                                     )
        
#define APPLY_TO_LATE_BOUND_FUNCTIONS(APPLY)                                              \
        APPLY(MessageBoxW,                 ({ user32                                  }))

#include "winapi_thunk_helper.inc"

namespace user32_thunk {
#define _USER32_THUNKS_IAT_SYMBOL(f, prefixed) _LCRT_DEFINE_IAT_SYMBOL(user32_thunk, f, prefixed)

int
WINAPI
MessageBoxW(
    _In_opt_ HWND hWnd,
    _In_opt_ LPCWSTR lpText,
    _In_opt_ LPCWSTR lpCaption,
    _In_ UINT uType)
{
    if (auto messagebox_w = try_get_MessageBoxW()) {
        return messagebox_w(hWnd, lpText, lpCaption, uType);
    }    
    return MB_OK;
}

_USER32_THUNKS_IAT_SYMBOL(MessageBoxW, _16);
}