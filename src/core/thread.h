#ifndef SPOTIC_CORE_THREAD_H
#define SPOTIC_CORE_THREAD_H
#include <stdbool.h>
#include <string.h>
#include <windows.h>

/* Blocking owners can acquire a USER32 queue through OS networking/crypto
 * without becoming the application's UI owner. Register only at worker entry. */
bool core_thread_background_owner(void);
void core_thread_register_background(void);

static inline bool core_thread_is_gui(void)
{
    if (core_thread_background_owner()) { return false; }
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
