#include "message_test.h"
#include <dwmapi.h>

/* Explicit visible-desktop opt-in. The worker inspects native EDIT output and
 * delivers keyboard messages through the dialog manager while the UI owner is
 * running the production modal dialog. No account or audio device is used. */
typedef struct NativeTest { HWND owner; const char *message; bool escape; } NativeTest;
static void keyboard(HWND window, UINT key)
{
    CHECK(PostMessageW(window,WM_KEYDOWN,key,0)); CHECK(PostMessageW(window,WM_KEYUP,key,0));
}
static void capture(HWND window)
{
    RECT r; CHECK(GetWindowRect(window,&r)); int w=r.right-r.left, h=r.bottom-r.top;
    CHECK(w>0 && h>0 && w<=1600 && h<=1200);
    HDC screen=GetDC(NULL), dc=CreateCompatibleDC(screen); CHECK(screen && dc);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,w,h); CHECK(bitmap);
    HGDIOBJ previous=SelectObject(dc,bitmap); CHECK(previous);
    CHECK(RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW));
    CHECK(SUCCEEDED(DwmFlush()));
    CHECK(BitBlt(dc,0,0,w,h,screen,r.left,r.top,SRCCOPY)); CHECK(SelectObject(dc,previous));
    static unsigned char pixels[1600u*1200u*4u]; BITMAPINFO info={0};
    info.bmiHeader=(BITMAPINFOHEADER){sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB,0,0,0,0,0};
    CHECK(GetDIBits(dc,bitmap,0,(UINT)h,pixels,&info,DIB_RGB_COLORS)==h);
    CHECK(DeleteObject(bitmap) && DeleteDC(dc) && ReleaseDC(NULL,screen));
    BITMAPFILEHEADER header={0}; header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);
    header.bfSize=header.bfOffBits+(DWORD)w*(DWORD)h*4;
    FILE *file=NULL; CHECK(!fopen_s(&file,"message-details-native.bmp","wb"));
    CHECK(fwrite(&header,sizeof(header),1,file)==1 && fwrite(&info.bmiHeader,sizeof(info.bmiHeader),1,file)==1);
    CHECK(fwrite(pixels,1,(size_t)w*(size_t)h*4,file)==(size_t)w*(size_t)h*4 && !fclose(file));
}
static DWORD WINAPI inspect(void *context)
{
    NativeTest *test=context; HWND window=NULL; uint64_t end=GetTickCount64()+10000;
    while (!window && GetTickCount64()<end) {
        window=FindWindowW(L"#32770",L"spoti-c | Messages");
        if (window && GetWindow(window,GW_OWNER)!=test->owner) { window=NULL; } Sleep(10);
    }
    CHECK(window); HWND edit=NULL, close=NULL;
    while ((!edit || !close) && GetTickCount64()<end) {
        edit=GetDlgItem(window,101); close=GetDlgItem(window,IDOK); Sleep(10);
    }
    CHECK(edit && close);
    wchar_t expected[APP_MESSAGE_BYTES]={0}, actual[APP_MESSAGE_BYTES]={0};
    int units=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,test->message,-1,expected,APP_MESSAGE_BYTES); CHECK(units>0);
    CHECK(GetWindowTextW(edit,actual,APP_MESSAGE_BYTES)==units-1 && !wcscmp(expected,actual));
    if (test->escape) { keyboard(edit,VK_ESCAPE); return 0; }
    CHECK(GetWindowLongPtrW(edit,GWL_STYLE)&ES_READONLY);
    CHECK(!(GetWindowLongPtrW(edit,GWL_STYLE)&ES_AUTOHSCROLL));
    CHECK(!IsWindowEnabled(test->owner));
    CHECK(SetWindowPos(window,HWND_TOPMOST,50,50,400,360,0));
    LRESULT narrow=SendMessageW(edit,EM_GETLINECOUNT,0,0); CHECK(narrow>20);
    CHECK(SetWindowPos(window,NULL,50,50,800,500,SWP_NOZORDER));
    CHECK(SendMessageW(edit,EM_GETLINECOUNT,0,0)<narrow);
    SendMessageW(edit,EM_LINESCROLL,0,10000); CHECK(SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0)>0);
    SendMessageW(edit,EM_LINESCROLL,0,-10000); CHECK(!SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0));
    /* Exercise the same handler as a monitor DPI change, without claiming this
     * machine has a physical 192-DPI monitor. */
    RECT target={50,50,850,650}; SendMessageW(window,WM_DPICHANGED,MAKELONG(192,192),(LPARAM)&target);
    LOGFONTW font={0}; CHECK(GetObjectW((HFONT)SendMessageW(edit,WM_GETFONT,0,0),sizeof(font),&font)==sizeof(font));
    CHECK(font.lfHeight==-28);
    target=(RECT){50,50,650,450}; SendMessageW(window,WM_DPICHANGED,MAKELONG(96,96),(LPARAM)&target);
    RECT client, button; CHECK(GetClientRect(window,&client) && GetWindowRect(close,&button));
    CHECK(MapWindowPoints(NULL,window,(POINT *)&button,2));
    printf("native close bounds: %ld,%ld %ld,%ld client %ld,%ld\n",button.left,button.top,button.right,button.bottom,client.right,client.bottom);
    CHECK(button.left>=0 && button.top>=0 && button.right<=client.right && button.bottom<=client.bottom);
    CHECK(GetWindowTextW(edit,actual,APP_MESSAGE_BYTES)==units-1 && !wcscmp(expected,actual));
    SendMessageW(edit,EM_SETSEL,0,-1); SendMessageW(edit,WM_CHAR,'Z',0);
    CHECK(GetWindowTextW(edit,actual,APP_MESSAGE_BYTES)==units-1 && !wcscmp(expected,actual));
    SendMessageW(edit,EM_SETSEL,0,0);
    SendMessageW(window,WM_NEXTDLGCTL,(WPARAM)edit,TRUE);
    Sleep(120); /* Let native child paints reach the compositor before capture. */
    capture(window);
    keyboard(edit,VK_TAB);
    DWORD thread=GetWindowThreadProcessId(window,NULL); GUITHREADINFO gui={.cbSize=sizeof(gui)};
    end=GetTickCount64()+2000;
    do { CHECK(GetGUIThreadInfo(thread,&gui)); if (gui.hwndFocus==close) { break; } Sleep(10); } while (GetTickCount64()<end);
    CHECK(gui.hwndFocus==close); keyboard(close,VK_RETURN); return 0;
}
int main(void)
{
    CHECK(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    HWND owner=CreateWindowExW(0,L"STATIC",L"spoti-c message acceptance",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        20,20,900,700,NULL,NULL,GetModuleHandleW(NULL),NULL); CHECK(owner);
    char message[APP_MESSAGE_BYTES]="Warning: " HOME_NOTICE "\r\n" RTL_NOTICE "\r\n";
    char token[401]; memset(token,'W',400); token[400]=0; strcat_s(message,sizeof(message),token);
    for (unsigned i=0;i<45;++i) { strcat_s(message,sizeof(message),"\r\nService detail. Retry the action."); }
    strcat_s(message,sizeof(message),"\r\nEND OF DIAGNOSTIC: Sign in again.");
    NativeTest test={.owner=owner,.message=message}; HANDLE worker=CreateThread(NULL,0,inspect,&test,0,NULL); CHECK(worker);
    DWORD objects=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    CHECK(app_messages_dialog(owner,message)); CHECK(IsWindowEnabled(owner));
    CHECK(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0 && CloseHandle(worker));
    test.message="Information: Ready."; test.escape=true;
    worker=CreateThread(NULL,0,inspect,&test,0,NULL); CHECK(worker);
    CHECK(app_messages_dialog(owner,test.message) && IsWindowEnabled(owner));
    CHECK(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0 && CloseHandle(worker));
    CHECK(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=objects);
    CHECK(DestroyWindow(owner));
    puts("native details: exact UTF-16, RTL, unbroken wrapping, scroll, resize/DPI handler, Tab/Enter and cleanup passed"); return 0;
}
