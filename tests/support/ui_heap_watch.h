#ifndef SPOTIC_UI_HEAP_WATCH_H
#define SPOTIC_UI_HEAP_WATCH_H
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static _Thread_local int ui_watch_heap;
static unsigned ui_heap_calls;
static int __cdecl ui_allocation_hook(int type, void *data, size_t bytes, int block,
    long request, const unsigned char *file, int line)
{
    (void)type; (void)data; (void)bytes; (void)block; (void)request; (void)file; (void)line;
    if (ui_watch_heap) { ++ui_heap_calls; } return 1;
}
static void ui_heap_begin(void) { ui_heap_calls = 0; _CrtSetAllocHook(ui_allocation_hook); ui_watch_heap = 1; }
static void ui_heap_end(void) { ui_watch_heap = 0; _CrtSetAllocHook(NULL); CHECK(ui_heap_calls == 0); }
#else
static void ui_heap_begin(void) { }
static void ui_heap_end(void) { }
#endif
#endif
