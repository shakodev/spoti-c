#include "bench_process.h"
#include "support/test_check.h"
#include <math.h>

/* Explicit opt-in: interact with the actual visible Release executable.
 * Offline demo uses the production player with synthetic network/fake audio;
 * no account, physical device, or Windows global volume is touched. */
static BenchProcess process;
static unsigned char pixels[4096u*2160u*4u];
static void foreground(void)
{
    (void)SetForegroundWindow(process.window);
    uint64_t until=GetTickCount64()+15000;
    puts("Waiting for the visible test window to own foreground input"); fflush(stdout);
    while (GetForegroundWindow()!=process.window && GetTickCount64()<until) { Sleep(50); }
    CHECK(GetForegroundWindow()==process.window);
}
static BenchSnapshot state(void)
{
    BenchSnapshot s; CHECK(bench_snapshot(&process,&s)); return s;
}
static void wait_level(float level)
{
    uint64_t until=GetTickCount64()+5000;
    do {
        BenchSnapshot s=state();
        if (fabsf(s.intended_volume-level)<0.011f && fabsf(s.player_volume-level)<0.011f) { return; }
        Sleep(20);
    } while (GetTickCount64()<until);
    BenchSnapshot s=state(); fprintf(stderr,"volume intended %.4f player %.4f expected %.4f\n",s.intended_volume,s.player_volume,level);
    CHECK(false);
}
static void mouse(DWORD flags)
{
    INPUT input={0}; input.type=INPUT_MOUSE; input.mi.dwFlags=flags;
    CHECK(SendInput(1,&input,sizeof(input))==1); Sleep(80);
}
static void move(Clay_BoundingBox box,float fraction)
{
    UINT dpi=GetDpiForWindow(process.window);
    POINT point={(LONG)((box.x+box.width*fraction)*dpi/96), (LONG)((box.y+box.height*0.5f)*dpi/96)};
    CHECK(ClientToScreen(process.window,&point)); CHECK(SetCursorPos(point.x,point.y)); Sleep(80);
}
static void click(Clay_BoundingBox box,float fraction)
{
    CHECK(box.width>0 && box.height>0); move(box,fraction); mouse(MOUSEEVENTF_LEFTDOWN); mouse(MOUSEEVENTF_LEFTUP);
}
static void key(WORD code)
{
    INPUT input[2]={{0}};
    input[0].type=INPUT_KEYBOARD; input[0].ki.wVk=code;
    input[0].ki.wScan=(WORD)MapVirtualKeyW(code,MAPVK_VK_TO_VSC);
    input[0].ki.dwFlags=KEYEVENTF_EXTENDEDKEY;
    input[1]=input[0]; input[1].ki.dwFlags|=KEYEVENTF_KEYUP;
    CHECK(SendInput(2,input,sizeof(INPUT))==2); Sleep(100);
}
static void capture(const wchar_t *directory,const wchar_t *name)
{
    Sleep(250); RECT r; CHECK(GetClientRect(process.window,&r)); POINT origin={0}; CHECK(ClientToScreen(process.window,&origin));
    int width=r.right,height=r.bottom; CHECK(width>0 && width<=4096 && height>0 && height<=2160);
    HDC screen=GetDC(NULL); CHECK(screen); HDC dc=CreateCompatibleDC(screen); CHECK(dc);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height); CHECK(bitmap); HGDIOBJ previous=SelectObject(dc,bitmap); CHECK(previous);
    CHECK(BitBlt(dc,0,0,width,height,screen,origin.x,origin.y,SRCCOPY)); CHECK(SelectObject(dc,previous));
    BITMAPINFO info={0}; info.bmiHeader=(BITMAPINFOHEADER){sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB,0,0,0,0,0};
    CHECK(GetDIBits(dc,bitmap,0,(UINT)height,pixels,&info,DIB_RGB_COLORS)==height);
    CHECK(DeleteObject(bitmap) && DeleteDC(dc)); CHECK(ReleaseDC(NULL,screen));
    wchar_t path[MAX_PATH]; CHECK(swprintf_s(path,MAX_PATH,L"%ls/%ls.bmp",directory,name)>0);
    FILE *file=NULL; CHECK(_wfopen_s(&file,path,L"wb")==0);
    BITMAPFILEHEADER header={0}; header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);
    DWORD bytes=(DWORD)width*(DWORD)height*4; header.bfSize=header.bfOffBits+bytes;
    CHECK(fwrite(&header,sizeof(header),1,file)==1 && fwrite(&info.bmiHeader,sizeof(info.bmiHeader),1,file)==1);
    CHECK(fwrite(pixels,bytes,1,file)==1 && fclose(file)==0);
    printf("native volume capture: %ls, %dx%d, DPI %u\n",name,width,height,GetDpiForWindow(process.window));
}
int wmain(int argc,wchar_t **argv)
{
    CHECK(argc==3); CHECK(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    POINT cursor; CHECK(GetCursorPos(&cursor));
    CHECK(bench_launch(&process,argv[1],true));
    CHECK(SetWindowPos(process.window,HWND_TOPMOST,40,40,1100,720,0)); foreground();
    CHECK(bench_command(&process,BENCH_LOAD)); Sleep(200); CHECK(bench_command(&process,BENCH_PLAY)); Sleep(300);
    BenchSnapshot s=state(); CHECK(s.volume_box.width>0); click(s.volume_box,0.37f); wait_level(0.37f);
    capture(argv[2],L"default-37");
    click(state().mute_box,0.5f); wait_level(0); capture(argv[2],L"muted");
    click(state().mute_box,0.5f); wait_level(0.37f);
    s=state(); move(s.volume_box,0.25f); mouse(MOUSEEVENTF_LEFTDOWN); wait_level(0.25f);
    move(s.volume_box,0.75f); wait_level(0.75f); /* Volume changes before release. */
    mouse(MOUSEEVENTF_LEFTUP); wait_level(0.75f);
    key(VK_HOME); wait_level(0); key(VK_END); wait_level(1); key(VK_LEFT); wait_level(0.99f);
    key(VK_DOWN); wait_level(0.98f); key(VK_UP); wait_level(0.99f); key(VK_RIGHT); wait_level(1);
    click(state().settings_box,0.5f); Sleep(150); CHECK(state().screen==APP_SETTINGS);
    click(state().volume_box,0.58f); wait_level(0.58f); capture(argv[2],L"settings-58");
    CHECK(SetWindowPos(process.window,NULL,40,40,420,720,SWP_NOZORDER)); Sleep(200);
    s=state(); CHECK(s.volume_box.width>=112); click(s.volume_box,0.24f); wait_level(0.24f);
    click(state().mute_box,0.5f); wait_level(0); click(state().mute_box,0.5f); wait_level(0.24f);
    capture(argv[2],L"narrow-24");
    CHECK(bench_close(&process));
    /* Benchmark launches use their own .cache/release-bench.state, keeping
     * this real process restart separate from the user's account preferences. */
    CHECK(bench_launch(&process,argv[1],true));
    CHECK(SetWindowPos(process.window,HWND_TOPMOST,40,40,420,720,0)); foreground();
    wait_level(0.24f); capture(argv[2],L"restart-24");
    CHECK(bench_close(&process)); CHECK(SetCursorPos(cursor.x,cursor.y));
    puts("Release app volume: real pointer drag, mute/restore, keyboard, Settings, compact controls and process restart passed; balanced shutdown");
    return 0;
}
