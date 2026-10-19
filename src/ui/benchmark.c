#define COBJMACROS
#include "benchmark.h"
#include <d3d11.h>
#include <dxgi1_4.h>
#include <string.h>

bool bench_probe_open(BenchProbe *p,const wchar_t *name)
{
    if (!p || !name || !*name || !metrics_init(&p->metrics)) { return false; }
    p->mapping=OpenFileMappingW(FILE_MAP_WRITE,FALSE,name);
    if (!p->mapping) { return false; }
    p->shared=MapViewOfFile(p->mapping,FILE_MAP_WRITE,0,0,sizeof(*p->shared));
    if (!p->shared) { CloseHandle(p->mapping); p->mapping=NULL; return false; }
    memset(p->shared,0,sizeof(*p->shared)); p->shared->version=BENCH_VERSION; return true;
}
void bench_probe_close(BenchProbe *p,bool balanced)
{
    if (p->shared) { p->shared->balanced=balanced; UnmapViewOfFile(p->shared); p->shared=NULL; }
    if (p->mapping) { CloseHandle(p->mapping); p->mapping=NULL; }
}
static void gpu(UiRenderer *r,BenchSnapshot *out)
{
    IDXGIDevice *device=NULL; IDXGIAdapter *adapter=NULL; IDXGIAdapter3 *adapter3=NULL;
    if (!r->device || FAILED(ID3D11Device_QueryInterface(r->device,&IID_IDXGIDevice,(void **)&device))) { return; }
    if (SUCCEEDED(IDXGIDevice_GetAdapter(device,&adapter))) {
        DXGI_ADAPTER_DESC desc; LARGE_INTEGER version;
        if (SUCCEEDED(IDXGIAdapter_GetDesc(adapter,&desc))) {
            memcpy(out->adapter,desc.Description,sizeof(out->adapter));
            out->vendor=desc.VendorId; out->device=desc.DeviceId;
        }
        if (SUCCEEDED(IDXGIAdapter_CheckInterfaceSupport(adapter,&IID_IDXGIDevice,&version))) {
            out->driver_version=(uint64_t)version.QuadPart;
        }
        if (SUCCEEDED(IDXGIAdapter_QueryInterface(adapter,&IID_IDXGIAdapter3,(void **)&adapter3))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO local,nonlocal;
            if (SUCCEEDED(IDXGIAdapter3_QueryVideoMemoryInfo(adapter3,0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local)) &&
                SUCCEEDED(IDXGIAdapter3_QueryVideoMemoryInfo(adapter3,0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&nonlocal))) {
                out->gpu_local=local.CurrentUsage; out->gpu_nonlocal=nonlocal.CurrentUsage; out->gpu_valid=1;
            }
            IDXGIAdapter3_Release(adapter3);
        }
        IDXGIAdapter_Release(adapter);
    }
    IDXGIDevice_Release(device);
}
bool bench_probe_input(BenchProbe *p,AppDesktop *d,UiWindow *w,Headless *h,UINT message,WPARAM wp,LRESULT *result)
{
    if (!p->shared) { return false; }
    if ((message>=WM_MOUSEFIRST && message<=WM_MOUSELAST) || (message>=WM_KEYFIRST && message<=WM_KEYLAST)) {
        ++p->input_messages;
    }
    if (message!=BENCH_MESSAGE) { return false; }
    *result=0;
    if (wp==BENCH_SNAPSHOT) {
        BenchSnapshot out={.version=BENCH_VERSION,.window=w->stats,
            .renderer=ui_renderer_stats(&d->surface.renderer),.accounting=metrics_snapshot(&p->metrics)};
        out.ready=d->startup->services_started && d->startup->times.useful && d->surface.ready;
        out.startup=d->startup->times;
        out.input_messages=p->input_messages;
        if (h) {
            PlayerSnapshot player=headless_snapshot(h);
            (void)headless_metrics(h,&out.audio); out.player_state=(uint32_t)player.state;
            out.player_error=(uint32_t)player.error;
        }
        gpu(&d->surface.renderer,&out); *p->shared=out; *result=1; return true;
    }
    if (!d->app.alive || !d->startup->services_started) { return true; }
    if (wp==BENCH_LOAD && h) {
        PlayerCommand command={.type=PLAYER_LOAD,.play=false}; command.track_id[15]=1;
        bool ok=headless_submit(h,&command)==PLAYER_OK;
        command=(PlayerCommand){.type=PLAYER_QUEUE_APPEND}; command.track_id[15]=2;
        ok=(headless_submit(h,&command)==PLAYER_OK)&&ok;
        command=(PlayerCommand){.type=PLAYER_REPEAT,.value=PLAYER_REPEAT_ALL};
        *result=(headless_submit(h,&command)==PLAYER_OK)&&ok;
    } else if (wp==BENCH_PLAY && h) { *result=headless_command(h,"play"); }
    else if (wp==BENCH_PAUSE && h) { *result=headless_command(h,"pause"); }
    else if (wp==BENCH_SEEK && h) { *result=headless_command(h,"seek 1500"); }
    else if (wp==BENCH_SCROLL) { app_scroll(&d->app,96); *result=1; }
    else if (wp==BENCH_DEVICE_LOSS) {
        ui_renderer_invalidate(&d->surface.renderer); ui_window_set_occluded(w,false);
        ui_scheduler_dirty(&w->scheduler,UI_DIRTY_DEVICE); *result=1;
    }
    return true;
}
