#include "app.h"
#include <stdio.h>
#include <string.h>

void app_queue_panel(App *a)
{
    if (!a->now.queue_open) { return; }
    const PlayerQueueSnapshot *queue=&a->now.queue;
    CLAY(CLAY_ID("PlayerQueuePanel"),{.layout={.sizing={CLAY_SIZING_FIXED(328),CLAY_SIZING_FIT()},
        .padding=CLAY_PADDING_ALL(12),.childGap=8,.layoutDirection=CLAY_TOP_TO_BOTTOM},
        .floating={.attachTo=CLAY_ATTACH_TO_ELEMENT_WITH_ID,.parentId=CLAY_ID("AppNowPlaying").id,
            .attachPoints={.element=CLAY_ATTACH_POINT_RIGHT_BOTTOM,.parent=CLAY_ATTACH_POINT_RIGHT_TOP},
            .offset={0,-8},.zIndex=15},.backgroundColor={32,32,32,255},.cornerRadius=CLAY_CORNER_RADIUS(8)}) {
        CLAY_AUTO_ID({.layout={.childGap=12,.childAlignment={.y=CLAY_ALIGN_Y_CENTER}}}) {
            app_label(CLAY_STRING("Playback queue"),18,(Clay_Color){255,255,255,255});
            (void)app_button(a,CLAY_ID("QueueClose"),CLAY_STRING("Close"),64,(AppAction){.type=APP_QUEUE},false);
        }
        if (!queue->count) { app_label(CLAY_STRING("The queue is empty"),14,(Clay_Color){180,180,180,255}); }
        for (uint32_t i=a->now.queue_page;i<queue->count && i<a->now.queue_page+5;++i) {
            const char *title=NULL; char fallback[64];
            for (unsigned j=0;j<8;++j) {
                if (a->now.tracks[j].used && !memcmp(a->now.tracks[j].entity.id,queue->ids[i],16)) { title=a->now.tracks[j].entity.title; break; }
            }
            (void)snprintf(fallback,sizeof(fallback),"%s %u%s",i==queue->index ? "Playing" : "Track",i+1,title ? ":" : " (metadata pending)");
            app_label(app_text(a,(Clay_String){false,(int32_t)strlen(fallback),fallback}),12,(Clay_Color){180,180,180,255});
            if (title) {
                CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(300),CLAY_SIZING_FIXED(18)}},.clip={.horizontal=true}}) {
                    app_label((Clay_String){false,(int32_t)strlen(title),title},14,(Clay_Color){235,235,235,255});
                }
            }
        }
        CLAY_AUTO_ID({.layout={.childGap=8}}) {
            (void)app_button(a,CLAY_ID("QueueEarlier"),CLAY_STRING("Earlier"),92,
                (AppAction){.type=APP_QUEUE_PAGE,.value=a->now.queue_page>=5 ? a->now.queue_page-5 : 0},!a->now.queue_page);
            (void)app_button(a,CLAY_ID("QueueLater"),CLAY_STRING("Later"),92,
                (AppAction){.type=APP_QUEUE_PAGE,.value=a->now.queue_page+5},a->now.queue_page+5>=queue->count);
        }
    }
}
