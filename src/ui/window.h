#ifndef SPOTIC_UI_WINDOW_H
#define SPOTIC_UI_WINDOW_H

#include "core/event_queue.h"
#include "ui_scheduler.h"

typedef struct UiWindow UiWindow;
#define UI_WINDOW_OCCLUSION_MESSAGE (WM_APP + 26)
typedef void (*UiFrame)(void *context, UiWindow *window, uint32_t dirty);
typedef void (*UiWorkerEvent)(void *context, UiWindow *window, const Event *event);
typedef void (*UiDeviceChange)(void *context, UiWindow *window);
typedef bool (*UiInput)(void *context, UiWindow *window, UINT message, WPARAM wp, LPARAM lp, LRESULT *result);

typedef struct UiWindowConfig {
    HINSTANCE instance;
    const wchar_t *title;
    int width, height;
    bool show;
    EventQueue *workers; /* Borrowed; close/stop producers before destroying. */
    CoreClock clock;
    Metrics *metrics;
    UiFrame frame; /* Nonblocking, no allocation; invoked only if dirty. */
    UiFrame prepare; /* Optional resource resize/recovery BEFORE frame submit. */
    UiWorkerEvent worker;
    UiDeviceChange device;
    UiInput input; /* Optional UI-owner router; true consumes this message. */
    void *context;
} UiWindowConfig;

typedef struct UiWindowStats {
    uint64_t waits, wakes, timeouts, messages, worker_events;
    uint32_t last_wait_ms;
} UiWindowStats;

struct UiWindow {
    HWND hwnd;
    DWORD owner;
    UINT dpi;
    bool closed, failed, class_owned, sizing;
    UiScheduler scheduler;
    UiWindowConfig config;
    UiWindowStats stats;
    MetricsHandle hwnd_metric;
};

/* Caller storage starts at {0}. Creation/destruction, callbacks and scheduler
 * mutations belong to this thread. WM_CLOSE marks closed; destroy does cleanup.
 * DPI awareness must be established before creating the process's first HWND. */
bool ui_window_create(UiWindow *window, const UiWindowConfig *config);
bool ui_window_destroy(UiWindow *window);
/* One queue drain and dirty frame, then event/message/deadline wait. max_wait_ms
 * is a harness bound; production passes INFINITE. False means closed or error. */
bool ui_window_step(UiWindow *window, uint32_t max_wait_ms);
void ui_window_set_occluded(UiWindow *window, bool occluded);
/* Standard nonclient layout, painting and hit testing belong to Windows. The
 * renderer and application input coordinates cover only GetClientRect(). */

#endif
