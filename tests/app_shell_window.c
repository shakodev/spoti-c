#include "app_shell_support.h"
#include "ui/app_desktop.h"
#include "support/ui_heap_watch.h"
#include <windowsx.h>

static AppDesktop desktop;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES], core_memory[MEMORY_BUDGET_CORE];
static void frame(void *context, UiWindow *w, uint32_t dirty)
{
    AppDesktop *d=context; (void)dirty; RECT r; CHECK(GetClientRect(w->hwnd,&r));
    CHECK(app_layout(&d->app,(float)r.right,(float)r.bottom,(float)w->dpi,(UiWidgetInput){0}).length>0);
}
void shell_native(void)
{
    Arena core={0}; EventQueue *queue=NULL; UiWindow window={0};
    CHECK(arena_init(&core,core_memory,sizeof(core_memory)) && event_queue_create(&core,16,&queue)==CORE_OK);
    UiWindowConfig config={.instance=GetModuleHandleW(NULL),.title=L"spoti-c shell input test",.width=900,.height=650,
        .workers=queue,.frame=frame,.input=app_desktop_input,.context=&desktop};
    CHECK(ui_window_create(&window,&config));
    AppServices services={0}; CHECK(app_init(&desktop.app,&services,&window.scheduler,clay,sizeof(clay),scratch,sizeof(scratch))==APP_OK);
    (void)app_session(&desktop.app,true,false,0);
    RECT r; CHECK(GetClientRect(window.hwnd,&r)); frame(&desktop,&window,0);
    desktop.app.widgets.focus=CLAY_ID("NavLibrary").id;
    (void)SendMessageW(window.hwnd,WM_KEYDOWN,VK_RETURN,0); (void)app_update(&desktop.app,0);
    CHECK(desktop.app.model.location.screen==APP_LIBRARY);
    frame(&desktop,&window,0); Clay_ElementData home=Clay_GetElementData(CLAY_ID("NavHome")); CHECK(home.found);
    int x=(int)((home.boundingBox.x+8)*window.dpi/96), y=(int)((home.boundingBox.y+8)*window.dpi/96);
    (void)SendMessageW(window.hwnd,WM_MOUSEMOVE,0,MAKELPARAM(x,y));
    (void)SendMessageW(window.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
    (void)SendMessageW(window.hwnd,WM_LBUTTONUP,0,MAKELPARAM(x,y));
    uint32_t count=desktop.app.input_count; CHECK(count>=3);
    for (uint32_t i=0;i<count;++i) {
        (void)app_layout(&desktop.app,(float)r.right,(float)r.bottom,(float)window.dpi,desktop.app.inputs[i]);
        (void)app_update(&desktop.app,0);
    }
    desktop.app.input_count=0;
    CHECK(desktop.app.model.location.screen==APP_HOME && !desktop.app.widgets.active);
    (void)SendMessageW(window.hwnd,WM_MOUSELEAVE,0,0);
    CHECK(desktop.app.inputs[0].x==-1 && desktop.app.inputs[0].y==-1);
    app_dispose(&desktop.app); CHECK(ui_window_destroy(&window));
    CHECK(event_queue_close(queue)==CORE_OK); Event events[16]; size_t n;
    (void)event_drain(queue,events,16,&n); CHECK(event_queue_destroy(queue)==CORE_OK); arena_dispose(&core);
    puts("native shell: real HWND keyboard/pointer/capture routing passed");
}
static FontCache cache;
static UiRenderFrame render_frame;
void shell_font_frames(void)
{
    shell_open(NULL); CHECK(app_session(&app,true,true,0)==APP_PENDING); shell_deliver("home");
    CHECK(font_cache_create(&cache,NULL,NULL)==FONT_OK);
    FontMeasureContext text={.cache=&cache,.config={.dpi=120,.weight=400,.max_width=260,.ellipsis=true}};
    app_bind_fonts(&app,&text); UiClayBridge bridge={.custom=app_render_custom,.context=&app};
    for (unsigned i=0;i<24;++i) {
        CHECK(font_prepare_pending(&cache)==FONT_OK); CHECK(font_atlas_commit(&cache,1)==FONT_OK);
        Clay_RenderCommandArray commands=shell_layout(1375,900,120);
        CHECK(ui_frame_begin(&render_frame,1375,900,1.25f)==UI_RENDER_OK);
        CHECK(ui_frame_clay(&render_frame,commands,&bridge)==UI_RENDER_OK);
    }
    CHECK(font_stats(&cache).pending==0); uint64_t prepared=font_stats(&cache).prepared;
    ui_heap_begin();
    for (unsigned i=0;i<1000;++i) {
        Clay_RenderCommandArray commands=shell_layout(1375,900,120);
        CHECK(ui_frame_begin(&render_frame,1375,900,1.25f)==UI_RENDER_OK);
        CHECK(ui_frame_clay(&render_frame,commands,&bridge)==UI_RENDER_OK);
        CHECK(font_stats(&cache).pending==0 && font_stats(&cache).prepared==prepared);
    }
    ui_heap_end();
    PlayerSnapshot snapshot={.state=PLAYER_PLAYING,.generation=1,.rate=48000,
        .duration_frames=48000u*1000u,.timeline_valid=true,.volume=1};
    for (unsigned i=0;i<320;++i) {
        snapshot.position_frames=(uint64_t)i*48000;
        CHECK(app_publish_player(&app,snapshot,(uint64_t)i*1000)==APP_OK);
        for (unsigned pass=0;pass<4;++pass) {
            CHECK(font_prepare_pending(&cache)==FONT_OK); CHECK(font_atlas_commit(&cache,1)==FONT_OK);
            Clay_RenderCommandArray commands=shell_layout(1375,900,120);
            CHECK(ui_frame_begin(&render_frame,1375,900,1.25f)==UI_RENDER_OK);
            CHECK(ui_frame_clay(&render_frame,commands,&bridge)==UI_RENDER_OK);
        }
    }
    CHECK(app.measure_count==APP_MEASURES && app.measure_next>0 && font_stats(&cache).pending==0);
    font_cache_destroy(&cache); app.fonts=NULL; shell_close();
    puts("1000 DirectWrite/Clay/renderer warm frames: no heap calls, no pending text, no new shaping");
}
