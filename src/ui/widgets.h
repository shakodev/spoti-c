#ifndef SPOTIC_UI_WIDGETS_H
#define SPOTIC_UI_WIDGETS_H
#include "virtual_list.h"
#include "clay.h"
#include <stddef.h>

#define UI_CLAY_BYTES (1216u * 1024u)
#define UI_SCRATCH_BYTES (64u * 1024u)
#define UI_TEXT_BYTES 2048u
#define UI_COMPOSITION_BYTES 512u
#define UI_WIDGET_TEXTS 8u
#define UI_WIDGET_SEEKS 32u
#define UI_WIDGET_LISTS 8u
#define UI_CARET_NEVER UINT64_MAX

typedef struct UiTextInput {
    char text[UI_TEXT_BYTES], composition[UI_COMPOSITION_BYTES];
    uint32_t length, anchor, caret, composition_length;
    uint32_t composition_caret; /* UTF-8 byte offset. */
    uint64_t blink;
    bool composing, focused, caret_visible;
} UiTextInput;
typedef enum UiTextKey { UI_TEXT_LEFT, UI_TEXT_RIGHT, UI_TEXT_HOME, UI_TEXT_END,
    UI_TEXT_BACKSPACE, UI_TEXT_DELETE, UI_TEXT_SELECT_ALL } UiTextKey;
/* Clipboard callbacks execute only on input events, outside layout/render.
 * A failed read/write never changes the committed text or selection. */
typedef struct UiClipboard {
    UiWidgetResult (*read)(void *context, char *text, uint32_t capacity, uint32_t *length);
    UiWidgetResult (*write)(void *context, const char *text, uint32_t length);
    void *context;
} UiClipboard;
typedef struct UiSeekSlider {
    double value, origin, preview, committed;
    bool dragging, has_preview, has_commit;
} UiSeekSlider;
typedef struct UiWidgetInput {
    float x, y;
    bool down, pressed, released, activate, cancel;
} UiWidgetInput;
typedef void (*UiWidgetError)(void *context, UiWidgetResult error);
typedef struct UiWidgets {
    Clay_Context *clay;
    unsigned char *scratch;
    size_t scratch_used, scratch_bytes;
    UiTextInput texts[UI_WIDGET_TEXTS];
    UiSeekSlider seeks[UI_WIDGET_SEEKS];
    UiVirtualList lists[UI_WIDGET_LISTS];
    UiWidgetInput input;
    uint32_t focus, active, hover, declarations;
    UiWidgetResult error;
    UiWidgetError report;
    void *report_context;
    bool in_frame;
} UiWidgets;
_Static_assert(sizeof(UiWidgets) <= 256u * 1024u, "widget state budget");
_Static_assert(UI_CLAY_BYTES + UI_SCRATCH_BYTES == 1280u * 1024u, "Clay + scratch budget");

/* All storage belongs to the caller; UI-thread only, no copies while live.
 * Clay arena must be 64-byte aligned. One context/arena for the UI lifetime.
 * Slots above are explicit fixed-capacity persistent widget state, assigned by
 * the screen. Item keys passed to row callbacks must be unique and nonzero.
 * report is mandatory: exhaustion must be visible even before layout exists. */
UiWidgetResult ui_widgets_init(UiWidgets *ui, void *clay_memory, size_t clay_bytes,
    void *scratch, size_t scratch_bytes, UiWidgetError report, void *context);
/* Detaches the global Clay context before caller storage leaves scope. */
UiWidgetResult ui_widgets_dispose(UiWidgets *ui);
UiWidgetResult ui_widgets_begin(UiWidgets *ui, float width, float height, UiWidgetInput input);
Clay_RenderCommandArray ui_widgets_end(UiWidgets *ui);
void *ui_widgets_scratch(UiWidgets *ui, size_t bytes, size_t alignment);
void ui_widgets_fail(UiWidgets *ui, UiWidgetResult error);
void ui_widgets_focus(UiWidgets *ui, Clay_ElementId id);
bool ui_button(UiWidgets *ui, Clay_ElementId id, Clay_String label, float width, float height, bool disabled);
void ui_seek_slider(UiWidgets *ui, Clay_ElementId id, UiSeekSlider *slider, float width);
void ui_level_slider(UiWidgets *ui, Clay_ElementId id, UiSeekSlider *slider, float width, bool disabled);
void ui_text_input(UiWidgets *ui, Clay_ElementId id, UiTextInput *text, float width);
typedef void (*UiListRow)(void *model, uint32_t row, uint64_t key);
UiWidgetResult ui_virtual_list(UiWidgets *ui, Clay_ElementId id, UiVirtualList *list,
    uint32_t total, double row_height, double viewport, uint32_t overscan,
    UiListKey key_at, UiListRow render_row, void *model, UiListRange *range);

/* Text offsets always lie at Unicode scalar boundaries. Overflow/invalid UTF-8
 * is transactional. Navigation is scalar based, not grapheme/bidi based. */
UiWidgetResult ui_text_set(UiTextInput *text, const char *utf8, uint32_t length);
UiWidgetResult ui_text_select(UiTextInput *text, uint32_t anchor, uint32_t caret);
UiWidgetResult ui_text_replace(UiTextInput *text, const char *utf8, uint32_t length);
UiWidgetResult ui_text_key(UiTextInput *text, UiTextKey key, bool extend);
UiWidgetResult ui_text_clipboard(UiTextInput *text, const UiClipboard *clipboard, bool paste, bool cut);
UiWidgetResult ui_text_compose(UiTextInput *text, const char *utf8, uint32_t length, uint32_t caret);
UiWidgetResult ui_text_composition_commit(UiTextInput *text, const char *utf8, uint32_t length);
void ui_text_composition_cancel(UiTextInput *text);
void ui_text_focus(UiTextInput *text, bool focused, uint64_t now);
uint64_t ui_text_tick(UiTextInput *text, uint64_t now);
/* Native adapters: owner is a borrowed HWND. Call only from input routing,
 * never measurement/layout. UTF-16 lengths/caret are code units, no terminator. */
UiClipboard ui_clipboard_win32(void *owner);
UiWidgetResult ui_text_ime_utf16(UiTextInput *text, const uint16_t *utf16,
    uint32_t length, uint32_t caret, bool commit);
UiWidgetResult ui_text_ime_message(UiTextInput *text, void *hwnd, intptr_t flags);

UiWidgetResult ui_seek_begin(UiSeekSlider *slider, double value);
UiWidgetResult ui_seek_move(UiSeekSlider *slider, double value);
UiWidgetResult ui_seek_commit(UiSeekSlider *slider);
UiWidgetResult ui_seek_cancel(UiSeekSlider *slider);
UiWidgetResult ui_seek_key(UiSeekSlider *slider, double value);
bool ui_seek_take_preview(UiSeekSlider *slider, double *value);
bool ui_seek_take_commit(UiSeekSlider *slider, double *value);
#endif
