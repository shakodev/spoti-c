#include "media_controls_winrt.h"
#include <string.h>
#include <wchar.h>

#define DROP(p) do { if (p) { (p)->lpVtbl->Release(p); (p)=NULL; } } while (0)
static HRESULT factory(const wchar_t *name, REFIID id, void **out)
{
    HSTRING_HEADER header; HSTRING string;
    HRESULT hr=WindowsCreateStringReference(name,(UINT32)wcslen(name),&header,&string);
    return FAILED(hr) ? hr : RoGetActivationFactory(string,id,out);
}
static HRESULT string(const char *utf8, HSTRING *out)
{
    *out=NULL; wchar_t buffer[512]; size_t bytes=strlen(utf8);
    if (!bytes) { return WindowsCreateString(L"",0,out); }
    if (bytes>=sizeof(buffer)/sizeof(buffer[0])) { return E_INVALIDARG; }
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8,(int)bytes,buffer,512);
    return n ? WindowsCreateString(buffer,(UINT32)n,out) : E_INVALIDARG;
}
static HRESULT timeline_create(MediaWinrt *w)
{
    HSTRING_HEADER header; HSTRING name;
    const wchar_t class_name[]=L"Windows.Media.SystemMediaTransportControlsTimelineProperties";
    HRESULT hr=WindowsCreateStringReference(class_name,(UINT32)(sizeof(class_name)/sizeof(class_name[0])-1),&header,&name);
    IInspectable *instance=NULL;
    if (SUCCEEDED(hr)) { hr=RoActivateInstance(name,&instance); }
    if (SUCCEEDED(hr)) { hr=instance->lpVtbl->QueryInterface(instance,&mc_iid_timeline,(void **)&w->timeline); }
    DROP(instance); return hr;
}
MediaResult media_winrt_close(void *context)
{
    MediaWinrt *w=context; if (w->owner && w->owner!=GetCurrentThreadId()) { return MEDIA_INVALID; }
    media_delegate_detach(w->button); media_delegate_detach(w->seek);
    HRESULT hr=S_OK;
    if (w->button_registered) {
        hr=w->transport->lpVtbl->remove_ButtonPressed(w->transport,w->button_token);
        if (SUCCEEDED(hr)) { w->button_registered=false; }
    }
    if (w->seek_registered) {
        HRESULT r=w->transport2->lpVtbl->remove_PlaybackPositionChangeRequested(w->transport2,w->seek_token);
        if (SUCCEEDED(r)) { w->seek_registered=false; } else { hr=r; }
    }
    if (w->button_registered || w->seek_registered) { w->last_error=hr; return MEDIA_PLATFORM_ERROR; }
    media_delegate_release(w->button); w->button=NULL; media_delegate_release(w->seek); w->seek=NULL;
    if (w->transport) { (void)w->transport->lpVtbl->put_IsEnabled(w->transport,false); }
    if (w->display) { (void)w->display->lpVtbl->ClearAll(w->display); (void)w->display->lpVtbl->Update(w->display); }
    DROP(w->art); DROP(w->uri); DROP(w->timeline); DROP(w->music); DROP(w->display); DROP(w->transport2); DROP(w->transport);
    if (w->runtime) { RoUninitialize(); w->runtime=false; } return MEDIA_OK;
}
static MediaResult open(void *context, MediaControls *controls)
{
    MediaWinrt *w=context; w->owner=GetCurrentThreadId();
    if (!w->window || !IsWindow(w->window) || GetAncestor(w->window,GA_ROOT)!=w->window ||
        GetWindowThreadProcessId(w->window,NULL)!=w->owner) { w->last_error=E_INVALIDARG; return MEDIA_INVALID; }
    w->stage=1; HRESULT hr=RoInitialize(RO_INIT_SINGLETHREADED); w->runtime=SUCCEEDED(hr);
    ISystemMediaTransportControlsInterop *interop=NULL;
    if (SUCCEEDED(hr)) { w->stage=2; hr=factory(L"Windows.Media.SystemMediaTransportControls",&mc_iid_interop,(void **)&interop); }
    if (SUCCEEDED(hr)) { w->stage=3; hr=interop->lpVtbl->GetForWindow(interop,w->window,&mc_iid_transport,(void **)&w->transport); }
    DROP(interop);
    if (SUCCEEDED(hr)) { w->stage=4; hr=w->transport->lpVtbl->get_DisplayUpdater(w->transport,&w->display); }
    if (SUCCEEDED(hr)) { w->stage=5; hr=w->display->lpVtbl->put_Type(w->display,MC_MUSIC); }
    if (SUCCEEDED(hr)) { w->stage=6; hr=w->display->lpVtbl->get_MusicProperties(w->display,&w->music); }
    if (SUCCEEDED(hr)) {
        w->button=media_delegate_create(controls,false); hr=w->button ? S_OK : E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr)) {
        w->stage=7; hr=w->transport->lpVtbl->add_ButtonPressed(w->transport,media_delegate_button(w->button),&w->button_token);
        w->button_registered=SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr) && SUCCEEDED(w->transport->lpVtbl->QueryInterface(w->transport,&mc_iid_transport2,(void **)&w->transport2))) {
        w->stage=8; hr=timeline_create(w);
        if (SUCCEEDED(hr)) { w->seek=media_delegate_create(controls,true); hr=w->seek ? S_OK : E_OUTOFMEMORY; }
        if (SUCCEEDED(hr)) {
            w->stage=9; hr=w->transport2->lpVtbl->add_PlaybackPositionChangeRequested(w->transport2,media_delegate_seek(w->seek),&w->seek_token);
            w->seek_registered=SUCCEEDED(hr);
        }
    }
    w->last_error=hr;
    if (FAILED(hr)) {
        MediaResult closed=media_winrt_close(w);
        return closed!=MEDIA_OK ? closed : hr==E_OUTOFMEMORY ? MEDIA_FULL : MEDIA_UNSUPPORTED;
    }
    return MEDIA_OK;
}
static HRESULT metadata(MediaWinrt *w, const MediaPublication *p)
{
    HSTRING title=NULL, artist=NULL, identity=NULL;
    char key[33]; const char digits[]="0123456789abcdef";
    for (unsigned i=0;i<16;++i) { key[i*2]=digits[p->metadata.track_id[i]>>4]; key[i*2+1]=digits[p->metadata.track_id[i]&15]; } key[32]=0;
    HRESULT hr=string(p->metadata.title,&title);
    if (SUCCEEDED(hr)) { hr=string(p->metadata.artist,&artist); }
    if (SUCCEEDED(hr)) { hr=string(key,&identity); }
    if (SUCCEEDED(hr)) { hr=w->display->lpVtbl->ClearAll(w->display); }
    if (SUCCEEDED(hr)) { hr=w->display->lpVtbl->put_Type(w->display,MC_MUSIC); }
    if (SUCCEEDED(hr)) { hr=w->display->lpVtbl->put_AppMediaId(w->display,identity); }
    if (SUCCEEDED(hr)) { hr=w->music->lpVtbl->put_Title(w->music,title); }
    if (SUCCEEDED(hr)) { hr=w->music->lpVtbl->put_Artist(w->music,artist); }
    if (SUCCEEDED(hr) && p->metadata.artwork[0]) {
        if (!w->uri) { hr=factory(L"Windows.Foundation.Uri",&mc_iid_uri,(void **)&w->uri); }
        if (SUCCEEDED(hr) && !w->art) { hr=factory(L"Windows.Storage.Streams.RandomAccessStreamReference",&mc_iid_art,(void **)&w->art); }
        HSTRING url=NULL; McUri *uri=NULL; McArt *art=NULL;
        if (SUCCEEDED(hr)) { hr=string(p->metadata.artwork,&url); }
        if (SUCCEEDED(hr)) { hr=w->uri->lpVtbl->CreateUri(w->uri,url,&uri); }
        if (SUCCEEDED(hr)) { hr=w->art->lpVtbl->CreateFromUri(w->art,uri,&art); }
        if (SUCCEEDED(hr)) { hr=w->display->lpVtbl->put_Thumbnail(w->display,art); }
        DROP(art); DROP(uri); WindowsDeleteString(url);
    }
    if (SUCCEEDED(hr)) { hr=w->display->lpVtbl->Update(w->display); }
    WindowsDeleteString(title); WindowsDeleteString(artist); WindowsDeleteString(identity); return hr;
}
static HRESULT state(MediaWinrt *w, const MediaPublication *p)
{
    HRESULT hr=w->transport->lpVtbl->put_PlaybackStatus(w->transport,(McStatus)p->status);
#define CAP(Name,Flag) if (SUCCEEDED(hr)) { hr=w->transport->lpVtbl->put_Is##Name##Enabled(w->transport,(p->capabilities&(Flag))!=0); }
    CAP(Play,MEDIA_CAN_PLAY) CAP(Pause,MEDIA_CAN_PAUSE) CAP(Stop,MEDIA_CAN_STOP)
    CAP(Next,MEDIA_CAN_NEXT) CAP(Previous,MEDIA_CAN_PREVIOUS)
    CAP(Record,0) CAP(FastForward,0) CAP(Rewind,0) CAP(ChannelUp,0) CAP(ChannelDown,0)
#undef CAP
    if (SUCCEEDED(hr)) { hr=w->transport->lpVtbl->put_IsEnabled(w->transport,p->capabilities!=0); } return hr;
}
static HRESULT timeline(MediaWinrt *w, const MediaPublication *p)
{
    if (!w->transport2 || !w->timeline) { return S_OK; }
    McTime zero={0}, end={(INT64)p->duration}, position={(INT64)p->position};
    McTime maximum=(p->capabilities&MEDIA_CAN_SEEK) ? end : zero;
    HRESULT hr=w->timeline->lpVtbl->put_StartTime(w->timeline,zero);
    if (SUCCEEDED(hr)) { hr=w->timeline->lpVtbl->put_EndTime(w->timeline,end); }
    if (SUCCEEDED(hr)) { hr=w->timeline->lpVtbl->put_MinSeekTime(w->timeline,zero); }
    if (SUCCEEDED(hr)) { hr=w->timeline->lpVtbl->put_MaxSeekTime(w->timeline,maximum); }
    if (SUCCEEDED(hr)) { hr=w->timeline->lpVtbl->put_Position(w->timeline,position); }
    if (SUCCEEDED(hr)) { hr=w->transport2->lpVtbl->UpdateTimelineProperties(w->transport2,w->timeline); } return hr;
}
static MediaResult publish(void *context, const MediaPublication *p, uint32_t changes)
{
    MediaWinrt *w=context; if (w->owner!=GetCurrentThreadId()) { return MEDIA_INVALID; }
    if (!w->transport) { return MEDIA_OK; } /* Explicit unsupported, window-key-only binding. */
    HRESULT hr=S_OK;
    if (changes&MEDIA_METADATA) { hr=metadata(w,p); }
    if (SUCCEEDED(hr) && (changes&MEDIA_STATE)) { hr=state(w,p); }
    if (SUCCEEDED(hr) && (changes&MEDIA_TIMELINE)) { hr=timeline(w,p); }
    w->last_error=hr; return SUCCEEDED(hr) ? MEDIA_OK : MEDIA_PLATFORM_ERROR;
}
MediaAdapter media_winrt_adapter(MediaWinrt *w, HWND window)
{
    if (!w) { return (MediaAdapter){0}; }
    memset(w,0,sizeof(*w)); w->window=window;
    return (MediaAdapter){open,publish,media_winrt_close,w};
}
