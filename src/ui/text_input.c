#include "widgets.h"
#include <string.h>

static bool continuation(unsigned char c) { return (c & 0xc0u) == 0x80u; }
static bool valid(const char *s, uint32_t n)
{
    if (!s && n) { return false; }
    for (uint32_t i = 0; i < n;) {
        unsigned char c = (unsigned char)s[i++];
        uint32_t cp, count, minimum;
        if (c < 0x80) { if (!c) { return false; } continue; }
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31u; count = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15u; count = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7u; count = 3; minimum = 0x10000; }
        else { return false; }
        if (count > n - i) { return false; }
        while (count--) {
            c = (unsigned char)s[i++]; if (!continuation(c)) { return false; }
            cp = (cp << 6) | (c & 63u);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) { return false; }
    }
    return true;
}
static bool boundary(const char *s, uint32_t n, uint32_t p)
{
    return p <= n && (p == n || !continuation((unsigned char)s[p]));
}
static bool sane(const UiTextInput *t)
{
    return t && t->length < UI_TEXT_BYTES && valid(t->text,t->length) &&
        boundary(t->text,t->length,t->caret) && boundary(t->text,t->length,t->anchor);
}
static uint32_t low(const UiTextInput *t) { return t->caret < t->anchor ? t->caret : t->anchor; }
static uint32_t high(const UiTextInput *t) { return t->caret > t->anchor ? t->caret : t->anchor; }
static uint32_t previous(const UiTextInput *t, uint32_t p)
{
    if (p) { --p; while (p && continuation((unsigned char)t->text[p])) { --p; } } return p;
}
static uint32_t next(const UiTextInput *t, uint32_t p)
{
    if (p < t->length) { ++p; while (p < t->length && continuation((unsigned char)t->text[p])) { ++p; } } return p;
}
void ui_text_composition_cancel(UiTextInput *t)
{
    if (t) { t->composing = false; t->composition_length = 0; t->composition_caret = 0; t->composition[0] = 0; }
}
UiWidgetResult ui_text_set(UiTextInput *t, const char *s, uint32_t n)
{
    if (!t) { return UI_WIDGET_INVALID; }
    if (n >= UI_TEXT_BYTES) { return UI_WIDGET_FULL; }
    if (!valid(s,n)) { return UI_WIDGET_INVALID; }
    if (n) { memmove(t->text,s,n); } t->text[n] = 0; t->length = n;
    t->anchor = t->caret = n; ui_text_composition_cancel(t); return UI_WIDGET_OK;
}
UiWidgetResult ui_text_select(UiTextInput *t, uint32_t anchor, uint32_t caret)
{
    if (!sane(t) || !boundary(t->text,t->length,anchor) || !boundary(t->text,t->length,caret)) {
        return UI_WIDGET_INVALID;
    }
    t->anchor = anchor; t->caret = caret; ui_text_composition_cancel(t); return UI_WIDGET_OK;
}
UiWidgetResult ui_text_replace(UiTextInput *t, const char *s, uint32_t n)
{
    if (!sane(t)) { return UI_WIDGET_INVALID; }
    uint32_t a = low(t), b = high(t), retained = t->length - (b - a);
    if (n >= UI_TEXT_BYTES - retained) { return UI_WIDGET_FULL; }
    if (!valid(s,n)) { return UI_WIDGET_INVALID; }
    /* Copy first to support insertion of a slice of the existing buffer. */
    char copy[UI_TEXT_BYTES]; if (n) { memcpy(copy,s,n); }
    memmove(t->text + a + n,t->text + b,t->length - b);
    if (n) { memcpy(t->text + a,copy,n); }
    t->length = retained + n; t->text[t->length] = 0;
    t->anchor = t->caret = a + n; t->caret_visible = true;
    ui_text_composition_cancel(t); return UI_WIDGET_OK;
}
UiWidgetResult ui_text_key(UiTextInput *t, UiTextKey key, bool extend)
{
    if (!sane(t) || key < UI_TEXT_LEFT || key > UI_TEXT_SELECT_ALL) { return UI_WIDGET_INVALID; }
    if (t->composing) { return UI_WIDGET_CANCEL; } /* IME owns navigation. */
    uint32_t p = t->caret;
    if (key == UI_TEXT_SELECT_ALL) { t->anchor = 0; t->caret = t->length; return UI_WIDGET_OK; }
    if (key == UI_TEXT_BACKSPACE || key == UI_TEXT_DELETE) {
        if (t->anchor == p) { t->anchor = key == UI_TEXT_BACKSPACE ? previous(t,p) : next(t,p); }
        return ui_text_replace(t,NULL,0);
    }
    if (key == UI_TEXT_HOME) { p = 0; }
    else if (key == UI_TEXT_END) { p = t->length; }
    else if (!extend && t->anchor != p) { p = key == UI_TEXT_LEFT ? low(t) : high(t); }
    else { p = key == UI_TEXT_LEFT ? previous(t,p) : next(t,p); }
    t->caret = p; if (!extend) { t->anchor = p; } t->caret_visible = true; return UI_WIDGET_OK;
}
UiWidgetResult ui_text_clipboard(UiTextInput *t, const UiClipboard *c, bool paste, bool cut)
{
    if (!sane(t) || !c || (paste && cut)) { return UI_WIDGET_INVALID; }
    if (t->composing) { return UI_WIDGET_CANCEL; }
    if (paste) {
        if (!c->read) { return UI_WIDGET_INVALID; }
        char copy[UI_TEXT_BYTES]; uint32_t n = 0;
        UiWidgetResult r = c->read(c->context,copy,sizeof(copy),&n);
        if (r != UI_WIDGET_OK) { return r; }
        if (n >= sizeof(copy)) { return UI_WIDGET_FULL; }
        return ui_text_replace(t,copy,n);
    }
    if (!c->write) { return UI_WIDGET_INVALID; }
    uint32_t a = low(t), b = high(t);
    UiWidgetResult r = c->write(c->context,t->text + a,b - a);
    return r == UI_WIDGET_OK && cut ? ui_text_replace(t,NULL,0) : r;
}
UiWidgetResult ui_text_compose(UiTextInput *t, const char *s, uint32_t n, uint32_t caret)
{
    if (!sane(t)) { return UI_WIDGET_INVALID; }
    if (n >= UI_COMPOSITION_BYTES) { return UI_WIDGET_FULL; }
    if (!valid(s,n) || !boundary(s,n,caret)) { return UI_WIDGET_INVALID; }
    if (n) { memmove(t->composition,s,n); } t->composition[n] = 0;
    t->composition_length = n; t->composition_caret = caret;
    t->composing = true; return UI_WIDGET_OK;
}
UiWidgetResult ui_text_composition_commit(UiTextInput *t, const char *s, uint32_t n)
{
    return ui_text_replace(t,s,n);
}
void ui_text_focus(UiTextInput *t, bool focused, uint64_t now)
{
    if (!t) { return; } t->focused = focused; t->caret_visible = focused;
    t->blink = focused && now < UINT64_MAX - 500 ? now + 500 : UI_CARET_NEVER;
    if (!focused) { ui_text_composition_cancel(t); }
}
uint64_t ui_text_tick(UiTextInput *t, uint64_t now)
{
    if (!t || !t->focused || t->composing) { return UI_CARET_NEVER; }
    if (now >= t->blink) {
        t->caret_visible = !t->caret_visible;
        t->blink = now < UINT64_MAX - 500 ? now + 500 : UI_CARET_NEVER;
    }
    return t->blink;
}
