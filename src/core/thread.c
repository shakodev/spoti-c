#include "thread.h"
#if defined(_MSC_VER)
static __declspec(thread) bool background_owner;
#else
static _Thread_local bool background_owner;
#endif
bool core_thread_background_owner(void) { return background_owner; }
void core_thread_register_background(void) { background_owner=true; }
