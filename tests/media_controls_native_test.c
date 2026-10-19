#include "media_controls_test.h"
#include <tlhelp32.h>
#include <stdio.h>
#include <wchar.h>

static bool cpp_runtime(void)
{
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId()); CHECK(snapshot!=INVALID_HANDLE_VALUE);
    MODULEENTRY32W module={.dwSize=sizeof(module)}; bool found=false;
    if (Module32FirstW(snapshot,&module)) {
        do {
            if (!_wcsnicmp(module.szModule,L"msvcp",5) || !_wcsnicmp(module.szModule,L"libstdc++",9) ||
                !_wcsnicmp(module.szModule,L"concrt",6)) {
                wchar_t inbox[MAX_PATH]; UINT n=GetSystemDirectoryW(inbox,MAX_PATH); CHECK(n && n<MAX_PATH-15);
                wcscat_s(inbox,MAX_PATH,L"\\msvcp_win.dll");
                if (!_wcsicmp(module.szExePath,inbox)) { puts("Windows SMTC loads the inbox System32/msvcp_win.dll; application redistributable C++ runtimes remain absent."); }
                else { fwprintf(stderr,L"Application C++ runtime loaded: %ls\n",module.szExePath); found=true; }
            }
        } while (Module32NextW(snapshot,&module));
    }
    CHECK(CloseHandle(snapshot)); return found;
}
static PlayerResult submit(void *context, const PlayerCommand *c) { (void)context; (void)c; return PLAYER_OK; }
static void wake(void *context) { (void)context; }
int mt_native(void)
{
    HWND window=CreateWindowExW(0,L"STATIC",L"spoti-c SMTC smoke",WS_OVERLAPPEDWINDOW,0,0,420,240,NULL,NULL,GetModuleHandleW(NULL),NULL);
    CHECK(window); MediaWinrt native; MediaControls controls;
    MediaResult r=media_controls_init(&controls,(MediaConfig){submit,wake,NULL},media_winrt_adapter(&native,window));
    if (r==MEDIA_UNSUPPORTED) {
        fprintf(stderr,"SMTC unavailable stage=%u HRESULT=0x%08lx\n",native.stage,(unsigned long)native.last_error);
        CHECK(media_controls_close(&controls)==MEDIA_OK && DestroyWindow(window)); return 77;
    }
    CHECK(r==MEDIA_OK && native.transport && native.display && native.timeline && native.seek_registered);
    MediaMetadata metadata={.track_id={1},.title="SMTC native smoke",.artist="Synthetic fixture"};
    PlayerSnapshot snapshot={.track_id={1},.generation=1,.state=PLAYER_PAUSED,.rate=48000,
        .position_frames=96000,.duration_frames=480000,.timeline_valid=true,.volume=1};
    CHECK(media_controls_publish(&controls,&metadata,snapshot,63,0)==MEDIA_OK);
    McStatus status; CHECK(native.transport->lpVtbl->get_PlaybackStatus(native.transport,&status)==S_OK && status==MC_STATUS(Paused));
    HSTRING title=NULL, artist=NULL; UINT32 length;
    CHECK(native.music->lpVtbl->get_Title(native.music,&title)==S_OK && native.music->lpVtbl->get_Artist(native.music,&artist)==S_OK);
    CHECK(!wcscmp(WindowsGetStringRawBuffer(title,&length),L"SMTC native smoke"));
    CHECK(!wcscmp(WindowsGetStringRawBuffer(artist,&length),L"Synthetic fixture")); WindowsDeleteString(title); WindowsDeleteString(artist);
    McTime position; CHECK(native.timeline->lpVtbl->get_Position(native.timeline,&position)==S_OK && position.Duration==20000000);
    CHECK(media_controls_publish(&controls,&metadata,snapshot,63,1)==MEDIA_OK && controls.stats.publications==1);
    snapshot.state=PLAYER_PLAYING; CHECK(media_controls_publish(&controls,&metadata,snapshot,63,2)==MEDIA_OK);
    CHECK(native.transport->lpVtbl->get_PlaybackStatus(native.transport,&status)==S_OK && status==MC_STATUS(Playing));
    metadata.track_id[0]=snapshot.track_id[0]=2; ++snapshot.generation;
    strcpy_s(metadata.title,sizeof(metadata.title),"Replacement track"); metadata.artist[0]=0;
    CHECK(media_controls_publish(&controls,&metadata,snapshot,63,3)==MEDIA_OK);
    CHECK(native.music->lpVtbl->get_Title(native.music,&title)==S_OK && native.music->lpVtbl->get_Artist(native.music,&artist)==S_OK);
    CHECK(!wcscmp(WindowsGetStringRawBuffer(title,&length),L"Replacement track"));
    (void)WindowsGetStringRawBuffer(artist,&length); CHECK(!length); WindowsDeleteString(title); WindowsDeleteString(artist);
    CHECK(media_controls_close(&controls)==MEDIA_OK && !native.transport && !native.runtime && !media_delegate_live_count());
    CHECK(DestroyWindow(window)); CHECK(!cpp_runtime());
    puts("Native HWND/WinRT SMTC: metadata/status/timeline and teardown passed; no application C++ runtime loaded. Visual flyout/listener/hardware-key checks remain manual."); return 0;
}
