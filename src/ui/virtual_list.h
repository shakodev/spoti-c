#ifndef SPOTIC_UI_VIRTUAL_LIST_H
#define SPOTIC_UI_VIRTUAL_LIST_H
#include <stdbool.h>
#include <stdint.h>

#define UI_LIST_OVERSCAN 16u
#define UI_LIST_ROWS 256u
typedef enum UiWidgetResult { UI_WIDGET_OK, UI_WIDGET_FULL, UI_WIDGET_INVALID,
    UI_WIDGET_CANCEL, UI_WIDGET_OS_ERROR } UiWidgetResult;
typedef enum UiNav { UI_NAV_UP, UI_NAV_DOWN, UI_NAV_HOME, UI_NAV_END,
    UI_NAV_PAGE_UP, UI_NAV_PAGE_DOWN } UiNav;
typedef struct UiListRange {
    uint32_t first, end, visible_first, visible_end;
    double scroll, extent, before, after, row_height, viewport;
    double thumb_position, thumb_size; /* Normalized [0,1]. */
} UiListRange;
/* Key zero means none. Keys identify model items, never viewport slots.
 * The model provides O(1) key_at/index_of (or its own indexed lookup).
 * Reset explicitly when removing the focused/selected item. */
typedef uint64_t (*UiListKey)(void *model, uint32_t index);
typedef bool (*UiListIndex)(void *model, uint64_t key, uint32_t *index);
typedef struct UiVirtualList {
    uint64_t focus, selection, hover;
    double scroll;
} UiVirtualList;
UiWidgetResult ui_virtual_range(uint32_t total_rows, double row_height,
    double scroll, double viewport, uint32_t overscan, UiListRange *out);
UiWidgetResult ui_virtual_navigate(UiVirtualList *list, uint32_t total_rows,
    double row_height, double viewport, UiNav nav, bool select,
    UiListKey key_at, UiListIndex index_of, void *model);
bool ui_virtual_hit(const UiListRange *range, double y, uint32_t *row);
UiWidgetResult ui_virtual_scrollbar(UiVirtualList *list, const UiListRange *range,
    double normalized_top);
#endif
