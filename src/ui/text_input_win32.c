#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imm.h>
#include "widgets.h"

static UiWidgetResult clipboard_read(void *owner, char *text, uint32_t capacity, uint32_t *length)
{
    if (!text || !capacity || !length) { return UI_WIDGET_INVALID; }
    *length = 0;
    if (!OpenClipboard((HWND)owner)) { return UI_WIDGET_OS_ERROR; }
    UiWidgetResult result = UI_WIDGET_OS_ERROR;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
        SIZE_T bytes = GlobalSize(data); const wchar_t *s = GlobalLock(data);
        if (s) {
            SIZE_T units = bytes / sizeof(wchar_t), n = 0;
            while (n < units && n < UI_TEXT_BYTES && s[n]) { ++n; }
            if (n == units || n == UI_TEXT_BYTES) { result = UI_WIDGET_FULL; }
            else if (n == 0) { text[0] = 0; result = UI_WIDGET_OK; }
            else {
                int count = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,(int)n,NULL,0,NULL,NULL);
                if (!count) { result = UI_WIDGET_INVALID; }
                else if ((uint32_t)count >= capacity) { result = UI_WIDGET_FULL; }
                else if (WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,(int)n,text,count,NULL,NULL) == count) {
                    *length = (uint32_t)count; text[count] = 0; result = UI_WIDGET_OK;
                }
            }
            GlobalUnlock(data);
        }
    }
    CloseClipboard(); return result;
}
static UiWidgetResult clipboard_write(void *owner, const char *text, uint32_t length)
{
    if ((!text && length) || length >= UI_TEXT_BYTES) { return UI_WIDGET_INVALID; }
    int count = length ? MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,(int)length,NULL,0) : 0;
    if (length && !count) { return UI_WIDGET_INVALID; }
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE,((SIZE_T)count + 1) * sizeof(wchar_t));
    if (!data) { return UI_WIDGET_OS_ERROR; }
    wchar_t *out = GlobalLock(data);
    if (!out) { GlobalFree(data); return UI_WIDGET_OS_ERROR; }
    if (length) { (void)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,(int)length,out,count); }
    out[count] = 0; GlobalUnlock(data);
    UiWidgetResult result = UI_WIDGET_OS_ERROR;
    if (OpenClipboard((HWND)owner)) {
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,data)) { data = NULL; result = UI_WIDGET_OK; }
        CloseClipboard();
    }
    if (data) { GlobalFree(data); } /* On success ownership belongs to Windows. */
    return result;
}
UiClipboard ui_clipboard_win32(void *owner)
{
    return (UiClipboard){clipboard_read,clipboard_write,owner};
}
UiWidgetResult ui_text_ime_utf16(UiTextInput *text, const uint16_t *s,
    uint32_t length, uint32_t caret, bool commit)
{
    if (!text || (!s && length) || caret > length) { return UI_WIDGET_INVALID; }
    if (length >= UI_TEXT_BYTES) { return UI_WIDGET_FULL; }
    char utf8[UI_TEXT_BYTES];
    int n = length ? WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,(const wchar_t *)s,
        (int)length,utf8,sizeof(utf8) - 1,NULL,NULL) : 0;
    if (length && !n) { return GetLastError() == ERROR_INSUFFICIENT_BUFFER ? UI_WIDGET_FULL : UI_WIDGET_INVALID; }
    int p = caret ? WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,(const wchar_t *)s,
        (int)caret,NULL,0,NULL,NULL) : 0;
    if (caret && !p) { return UI_WIDGET_INVALID; }
    return commit ? ui_text_composition_commit(text,utf8,(uint32_t)n) :
        ui_text_compose(text,utf8,(uint32_t)n,(uint32_t)p);
}
UiWidgetResult ui_text_ime_message(UiTextInput *text, void *hwnd, intptr_t flags)
{
    if (!text || !hwnd) { return UI_WIDGET_INVALID; }
    if (!(flags & (GCS_COMPSTR | GCS_RESULTSTR | GCS_CURSORPOS))) { return UI_WIDGET_CANCEL; }
    HIMC ime = ImmGetContext((HWND)hwnd);
    if (!ime) { return UI_WIDGET_OS_ERROR; }
    /* A single message can commit one phrase and start another. Apply both
     * transactionally; a rejected preedit must not duplicate the result. */
    UiTextInput candidate = *text;
    uint16_t utf16[UI_TEXT_BYTES];
    UiWidgetResult result = UI_WIDGET_OK;
    const DWORD fields[2] = {GCS_RESULTSTR,GCS_COMPSTR};
    for (unsigned i = 0; i < 2 && result == UI_WIDGET_OK; ++i) {
        if (!(flags & fields[i]) && !(i == 1 && (flags & GCS_CURSORPOS))) { continue; }
        LONG bytes = ImmGetCompositionStringW(ime,fields[i],NULL,0);
        if (bytes >= (LONG)sizeof(utf16)) { result = UI_WIDGET_FULL; }
        else if (bytes < 0 || bytes % 2 || ImmGetCompositionStringW(ime,fields[i],utf16,(DWORD)bytes) != bytes) {
            result = UI_WIDGET_INVALID;
        } else {
            LONG caret = i == 0 ? bytes / 2 : ImmGetCompositionStringW(ime,GCS_CURSORPOS,NULL,0);
            result = caret < 0 ? UI_WIDGET_INVALID :
                ui_text_ime_utf16(&candidate,utf16,(uint32_t)bytes / 2,(uint32_t)caret,i == 0);
        }
    }
    if (result == UI_WIDGET_OK) { *text = candidate; }
    ImmReleaseContext((HWND)hwnd,ime); return result;
}
