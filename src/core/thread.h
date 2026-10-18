#ifndef SPOTIC_CORE_THREAD_H
#define SPOTIC_CORE_THREAD_H
#include <stdbool.h>
#include <string.h>
#include <windows.h>

static inline bool core_thread_is_gui(void)
{
    /* A headless process need not initialize USER32 to check its owner.
     * Loading USER32 here can itself turn the calling thread into a GUI thread. */
    HMODULE module = GetModuleHandleW(L"user32.dll");
    if (!module) { return false; }
    FARPROC address = GetProcAddress(module, "IsGUIThread");
    if (!address) { return true; }
    typedef BOOL (WINAPI *IsGuiThread)(BOOL);
    IsGuiThread query; memcpy(&query, &address, sizeof(query));
    return query(FALSE) != FALSE;
}
#endif
