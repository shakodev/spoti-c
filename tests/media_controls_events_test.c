#include "media_controls_test.h"
#if defined(SPOTIC_ASAN)
#include <sanitizer/asan_interface.h>
#endif
typedef struct Args { McButtonArgs iface; McButtonValue button; HANDLE entered, proceed; HRESULT result; } Args;
static HRESULT STDMETHODCALLTYPE button_value(McButtonArgs *self, McButtonValue *out)
{
    Args *a=(Args *)self;
    if (a->entered) { SetEvent(a->entered); CHECK(WaitForSingleObject(a->proceed,5000)==WAIT_OBJECT_0); }
    *out=a->button; return a->result;
}
static __x_ABI_CWindows_CMedia_CISystemMediaTransportControlsButtonPressedEventArgsVtbl args_vtable={.get_Button=button_value};
static HRESULT STDMETHODCALLTYPE position(McSeekArgs *self, McTime *out) { (void)self; out->Duration=20000000; return S_OK; }
static __x_ABI_CWindows_CMedia_CIPlaybackPositionChangeRequestedEventArgsVtbl position_vtable={.get_RequestedPlaybackPosition=position};
typedef struct Race {
    MediaControls controls;
    MediaDelegate *delegate;
    McButton *callback;
    Args args;
} Race;
static Race race;
static MediaResult race_open(void *context, MediaControls *controls)
{
    Race *r=context; r->delegate=media_delegate_create(controls,false); CHECK(r->delegate);
    r->callback=media_delegate_button(r->delegate); r->callback->lpVtbl->AddRef(r->callback); return MEDIA_OK;
}
static MediaResult race_publish(void *context, const MediaPublication *p, uint32_t flags) { (void)context; (void)p; (void)flags; return MEDIA_OK; }
static MediaResult race_close(void *context)
{
    Race *r=context; media_delegate_detach(r->delegate);
    r->callback->lpVtbl->Release(r->callback); media_delegate_release(r->delegate); r->delegate=NULL; return MEDIA_OK;
}
static void race_wake(void *context) { (void)context; CHECK(false); }
static PlayerResult race_submit(void *context, const PlayerCommand *c) { (void)context; (void)c; CHECK(false); return PLAYER_INVALID; }
static DWORD WINAPI callback(void *context)
{
    Race *r=context;
    CHECK(r->callback->lpVtbl->Invoke(r->callback,NULL,&r->args.iface)==S_OK);
    r->callback->lpVtbl->Release(r->callback); return 0;
}
static void teardown(void)
{
    memset(&race,0,sizeof(race));
    race.args=(Args){.iface={&args_vtable},.button=MC_BUTTON(Next)};
    race.args.entered=CreateEventW(NULL,true,false,NULL); race.args.proceed=CreateEventW(NULL,true,false,NULL);
    CHECK(race.args.entered && race.args.proceed);
    CHECK(media_controls_init(&race.controls,(MediaConfig){race_submit,race_wake,&race},
        (MediaAdapter){race_open,race_publish,race_close,&race})==MEDIA_OK);
    race.callback->lpVtbl->AddRef(race.callback); /* In-flight COM dispatch owns a reference. */
    HANDLE thread=CreateThread(NULL,0,callback,&race,0,NULL); CHECK(thread);
    CHECK(WaitForSingleObject(race.args.entered,5000)==WAIT_OBJECT_0);
    CHECK(media_controls_close(&race.controls)==MEDIA_OK && media_delegate_live_count()==1);
#if defined(SPOTIC_ASAN)
    __asan_poison_memory_region(&race.controls,sizeof(race.controls));
#endif
    SetEvent(race.args.proceed); CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
    CHECK(CloseHandle(thread) && media_delegate_live_count()==0);
#if defined(SPOTIC_ASAN)
    __asan_unpoison_memory_region(&race.controls,sizeof(race.controls));
#endif
    CHECK(CloseHandle(race.args.entered) && CloseHandle(race.args.proceed));
}
typedef struct FakeTransport { McTransport iface; unsigned removals,releases; bool fail; } FakeTransport;
typedef struct FakeTransport2 { McTransport2 iface; unsigned removals,releases; } FakeTransport2;
static HRESULT STDMETHODCALLTYPE remove_button(McTransport *self, EventRegistrationToken token)
{
    FakeTransport *f=(FakeTransport *)self; CHECK(token.value==12); ++f->removals; return f->fail ? E_FAIL : S_OK;
}
static HRESULT STDMETHODCALLTYPE enable(McTransport *self, boolean value) { (void)self; CHECK(!value); return S_OK; }
static ULONG STDMETHODCALLTYPE drop_transport(McTransport *self) { return ++((FakeTransport *)self)->releases; }
static HRESULT STDMETHODCALLTYPE remove_seek(McTransport2 *self, EventRegistrationToken token)
{
    FakeTransport2 *f=(FakeTransport2 *)self; CHECK(token.value==34); ++f->removals; return S_OK;
}
static ULONG STDMETHODCALLTYPE drop_transport2(McTransport2 *self) { return ++((FakeTransport2 *)self)->releases; }
static __x_ABI_CWindows_CMedia_CISystemMediaTransportControlsVtbl transport_vtable={.Release=drop_transport,.put_IsEnabled=enable,.remove_ButtonPressed=remove_button};
static __x_ABI_CWindows_CMedia_CISystemMediaTransportControls2Vtbl transport2_vtable={.Release=drop_transport2,.remove_PlaybackPositionChangeRequested=remove_seek};
static void tokens(void)
{
    mt_open(); FakeTransport t={.iface={&transport_vtable},.fail=true}; FakeTransport2 t2={.iface={&transport2_vtable}};
    MediaWinrt w={.transport=&t.iface,.transport2=&t2.iface,.owner=GetCurrentThreadId(),
        .button_registered=true,.seek_registered=true,.button_token={12},.seek_token={34}};
    w.button=media_delegate_create(&mc,false); w.seek=media_delegate_create(&mc,true); CHECK(w.button && w.seek);
    CHECK(media_winrt_close(&w)==MEDIA_PLATFORM_ERROR && w.button_registered && !w.seek_registered);
    CHECK(!t.releases && !t2.releases && media_delegate_live_count()==2);
    Args args={.iface={&args_vtable},.button=MC_BUTTON(Play)};
    McButton *handler=media_delegate_button(w.button); CHECK(handler->lpVtbl->Invoke(handler,NULL,&args.iface)==S_OK && !mc.count);
    t.fail=false; CHECK(media_winrt_close(&w)==MEDIA_OK && !w.button_registered && !w.seek_registered);
    CHECK(t.removals==2 && t2.removals==1 && t.releases==1 && t2.releases==1 && media_delegate_live_count()==0); mt_close();
}
void mt_events(void)
{
    mt_open(); mt_publish(0);
    MediaDelegate *d=media_delegate_create(&mc,false), *seek=media_delegate_create(&mc,true); CHECK(d && seek);
    McButton *button=media_delegate_button(d); McSeek *scrub=media_delegate_seek(seek); void *out=NULL;
    CHECK(button->lpVtbl->QueryInterface(button,&mc_iid_button,&out)==S_OK && out==button); button->lpVtbl->Release(button);
    CHECK(button->lpVtbl->QueryInterface(button,&mc_iid_seek,&out)==E_NOINTERFACE && !out);
    const McButtonValue values[]={MC_BUTTON(Play),MC_BUTTON(Pause),MC_BUTTON(Stop),MC_BUTTON(Next),MC_BUTTON(Previous)};
    const PlayerCommandType expected[]={PLAYER_PLAY,PLAYER_PAUSE,PLAYER_STOP,PLAYER_NEXT,PLAYER_PREVIOUS};
    Args args={.iface={&args_vtable}};
    for (unsigned i=0;i<5;++i) {
        mp.state=i==0 ? PLAYER_PAUSED : PLAYER_PLAYING; mt_publish(i*1000);
        args.button=values[i]; CHECK(button->lpVtbl->Invoke(button,NULL,&args.iface)==S_OK && media_controls_pump(&mc)==MEDIA_OK);
        CHECK(mf.commands[i].type==expected[i]);
    }
    McSeekArgs seek_args={&position_vtable}; CHECK(scrub->lpVtbl->Invoke(scrub,NULL,&seek_args)==S_OK);
    CHECK(media_controls_pump(&mc)==MEDIA_OK && mf.commands[5].position_frames==96000);
    args.button=MC_BUTTON(FastForward); CHECK(button->lpVtbl->Invoke(button,NULL,&args.iface)==S_OK && !mc.count);
    args.result=E_FAIL; CHECK(button->lpVtbl->Invoke(button,NULL,&args.iface)==E_FAIL && !mc.count);
    media_delegate_detach(d); media_delegate_detach(seek); media_delegate_release(d); media_delegate_release(seek);
    mt_close(); CHECK(media_delegate_live_count()==0); teardown(); tokens();
}
