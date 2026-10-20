#include "app_notice.h"
#include <string.h>

typedef struct MessageDialog {
    wchar_t text[APP_MESSAGE_BYTES];
    HWND edit, hint, close;
    HFONT font;
    WNDPROC edit_proc;
    UINT dpi;
} MessageDialog;

static LRESULT CALLBACK edit_input(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    MessageDialog *d=(MessageDialog *)GetWindowLongPtrW(GetParent(window),DWLP_USER);
    if (message==WM_KEYDOWN && wp=='A' && GetKeyState(VK_CONTROL)<0) {
        SendMessageW(window,EM_SETSEL,0,-1); return 0;
    }
    if (message==WM_CHAR && wp==1) { return 0; }
    return CallWindowProcW(d->edit_proc,window,message,wp,lp);
}
static void arrange(HWND window, MessageDialog *d)
{
    RECT r; GetClientRect(window,&r);
    int margin=MulDiv(12,(int)d->dpi,96), button=MulDiv(32,(int)d->dpi,96);
    int width=r.right-2*margin, height=r.bottom-4*margin-2*button;
    (void)MoveWindow(d->hint,margin,margin,width,button,TRUE);
    (void)MoveWindow(d->edit,margin,2*margin+button,width,height,TRUE);
    (void)MoveWindow(d->close,r.right-margin-MulDiv(96,(int)d->dpi,96),r.bottom-margin-button,
        MulDiv(96,(int)d->dpi,96),button,TRUE);
}
static bool font(HWND window, MessageDialog *d)
{
    HFONT next=CreateFontW(-MulDiv(14,(int)d->dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if (!next) { return false; }
    SendMessageW(d->hint,WM_SETFONT,(WPARAM)next,TRUE);
    SendMessageW(d->edit,WM_SETFONT,(WPARAM)next,TRUE);
    SendMessageW(d->close,WM_SETFONT,(WPARAM)next,TRUE);
    if (d->font) { DeleteObject(d->font); } d->font=next; arrange(window,d); return true;
}
static INT_PTR CALLBACK dialog(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    MessageDialog *d=(MessageDialog *)GetWindowLongPtrW(window,DWLP_USER);
    if (message==WM_INITDIALOG) {
        d=(MessageDialog *)lp; SetWindowLongPtrW(window,DWLP_USER,lp); d->dpi=GetDpiForWindow(window);
        SetWindowTextW(window,L"spoti-c | Messages");
        HINSTANCE instance=GetModuleHandleW(NULL);
        d->hint=CreateWindowExW(0,L"STATIC",L"Full messages. Select text to copy; scroll to read more.",
            WS_CHILD|WS_VISIBLE,0,0,1,1,window,(HMENU)100,instance,NULL);
        d->edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",d->text,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
            0,0,1,1,window,(HMENU)101,instance,NULL);
        d->close=CreateWindowExW(0,L"BUTTON",L"Close",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
            0,0,1,1,window,(HMENU)IDOK,instance,NULL);
        if (!d->hint || !d->edit || !d->close || !font(window,d)) { EndDialog(window,-1); return TRUE; }
        SendMessageW(d->edit,EM_SETLIMITTEXT,APP_MESSAGE_BYTES-1,0);
        d->edit_proc=(WNDPROC)SetWindowLongPtrW(d->edit,GWLP_WNDPROC,(LONG_PTR)edit_input);
        SendMessageW(d->edit,EM_SETSEL,0,0); SetFocus(d->edit); return FALSE;
    }
    if (!d) { return FALSE; }
    switch (message) {
    case WM_SIZE: arrange(window,d); return TRUE;
    case WM_DPICHANGED: {
        d->dpi=HIWORD(wp); RECT *r=(RECT *)lp;
        SetWindowPos(window,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        (void)font(window,d); return TRUE;
    }
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize=(POINT){MulDiv(320,(int)d->dpi,96),MulDiv(240,(int)d->dpi,96)}; return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp)==IDOK || LOWORD(wp)==IDCANCEL) { EndDialog(window,IDOK); return TRUE; } break;
    case WM_CLOSE: EndDialog(window,IDOK); return TRUE;
    case WM_DESTROY: if (d->font) { DeleteObject(d->font); d->font=NULL; } return TRUE;
    default: break;
    }
    return FALSE;
}
bool app_messages_dialog(HWND owner, const char *utf8)
{
    if (!owner || !utf8) { return false; }
    size_t length=strnlen_s(utf8,APP_MESSAGE_BYTES);
    if (length>=APP_MESSAGE_BYTES) { return false; }
    MessageDialog data={0};
    if (length && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8,(int)length,data.text,APP_MESSAGE_BYTES-1)) { return false; }
    /* Empty menu, default dialog class and initially empty title. */
    struct { DLGTEMPLATE root; WORD menu, class_name, title; } resource={
        .root={.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|DS_MODALFRAME,.cx=360,.cy=240}};
    return DialogBoxIndirectParamW(GetModuleHandleW(NULL),&resource.root,owner,dialog,(LPARAM)&data)==IDOK;
}
