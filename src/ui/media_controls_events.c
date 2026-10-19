#include "media_controls_winrt.h"
#include <string.h>

struct MediaDelegate {
    union { McButton button; McSeek seek; } iface;
    atomic_uint references;
    SRWLOCK gate;
    MediaControls *controls;
    bool seeking;
};
static atomic_uint live;
static const IID agile={0x94ea2b94,0xe9cc,0x49e0,{0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90}};
static ULONG add(MediaDelegate *d) { return atomic_fetch_add_explicit(&d->references,1,memory_order_relaxed)+1; }
static ULONG release(MediaDelegate *d)
{
    unsigned n=atomic_fetch_sub_explicit(&d->references,1,memory_order_acq_rel)-1;
    if (!n) { atomic_fetch_sub(&live,1); CoTaskMemFree(d); } return n;
}
static HRESULT query(MediaDelegate *d, REFIID id, void **out)
{
    if (!out) { return E_POINTER; } *out=NULL;
    if (!IsEqualIID(id,&IID_IUnknown) && !IsEqualIID(id,&agile) &&
        !IsEqualIID(id,d->seeking ? &mc_iid_seek : &mc_iid_button)) { return E_NOINTERFACE; }
    *out=d; (void)add(d); return S_OK;
}
#define DELEGATE_METHODS(Type,Name) \
static HRESULT STDMETHODCALLTYPE Name##_query(Type *self, REFIID id, void **out) { return query((MediaDelegate *)self,id,out); } \
static ULONG STDMETHODCALLTYPE Name##_add(Type *self) { return add((MediaDelegate *)self); } \
static ULONG STDMETHODCALLTYPE Name##_release(Type *self) { return release((MediaDelegate *)self); }
DELEGATE_METHODS(McButton,button)
DELEGATE_METHODS(McSeek,seek)
static HRESULT STDMETHODCALLTYPE button_invoke(McButton *self, McTransport *sender, McButtonArgs *args)
{
    (void)sender; MediaDelegate *d=(MediaDelegate *)self;
    if (!args) { return E_POINTER; }
    McButtonValue button; HRESULT hr=args->lpVtbl->get_Button(args,&button); if (FAILED(hr)) { return hr; }
    MediaAction action;
    switch (button) {
    case MC_BUTTON(Play): action=MEDIA_PLAY; break; case MC_BUTTON(Pause): action=MEDIA_PAUSE; break;
    case MC_BUTTON(Stop): action=MEDIA_STOP; break; case MC_BUTTON(Next): action=MEDIA_NEXT; break;
    case MC_BUTTON(Previous): action=MEDIA_PREVIOUS; break; default: return S_OK;
    }
    AcquireSRWLockShared(&d->gate);
    if (d->controls) { (void)media_controls_post(d->controls,action,MEDIA_SYSTEM,0,GetTickCount64()); }
    ReleaseSRWLockShared(&d->gate); return S_OK;
}
static HRESULT STDMETHODCALLTYPE seek_invoke(McSeek *self, McTransport *sender, McSeekArgs *args)
{
    (void)sender; MediaDelegate *d=(MediaDelegate *)self;
    if (!args) { return E_POINTER; }
    McTime position; HRESULT hr=args->lpVtbl->get_RequestedPlaybackPosition(args,&position); if (FAILED(hr)) { return hr; }
    AcquireSRWLockShared(&d->gate);
    if (d->controls) { (void)media_controls_post(d->controls,MEDIA_SEEK,MEDIA_SYSTEM,position.Duration,GetTickCount64()); }
    ReleaseSRWLockShared(&d->gate); return S_OK;
}
static McButtonVtbl button_vtable={button_query,button_add,button_release,button_invoke};
static McSeekVtbl seek_vtable={seek_query,seek_add,seek_release,seek_invoke};
MediaDelegate *media_delegate_create(MediaControls *controls, bool seeking)
{
    MediaDelegate *d=CoTaskMemAlloc(sizeof(*d)); if (!d) { return NULL; }
    memset(d,0,sizeof(*d)); atomic_init(&d->references,1); InitializeSRWLock(&d->gate);
    d->controls=controls; d->seeking=seeking;
    if (seeking) { d->iface.seek.lpVtbl=&seek_vtable; } else { d->iface.button.lpVtbl=&button_vtable; }
    atomic_fetch_add(&live,1); return d;
}
void media_delegate_detach(MediaDelegate *d)
{
    if (d) { AcquireSRWLockExclusive(&d->gate); d->controls=NULL; ReleaseSRWLockExclusive(&d->gate); }
}
void media_delegate_release(MediaDelegate *d) { if (d) { (void)release(d); } }
McButton *media_delegate_button(MediaDelegate *d) { return d ? &d->iface.button : NULL; }
McSeek *media_delegate_seek(MediaDelegate *d) { return d ? &d->iface.seek : NULL; }
unsigned media_delegate_live_count(void) { return atomic_load(&live); }
