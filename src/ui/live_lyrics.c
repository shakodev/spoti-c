#include "live_internal.h"
#include <string.h>

void live_lyrics_request(AppLive *h, LiveSlot *slot, const CancelToken *cancel)
{
    if (!h->lyrics) {
        LyricsConfig config={0};
        if (lyrics_service_init(h->app->lyrics_storage,sizeof(h->app->lyrics_storage),h->pool,&config,&h->lyrics)!=LYRICS_OK) { return; }
        (void)lyrics_reset(h->lyrics,h->generation);
    }
    LyricsTrack track={.uri={slot->request.uri,strlen(slot->request.uri)},
        .artwork={slot->artwork,strlen(slot->artwork)},.client_language={"en",2}};
    LyricsRequest request;
    slot->lyrics_result=lyrics_request(h->lyrics,&track,slot->request.generation,&request);
    if (slot->lyrics_result!=LYRICS_PENDING || !request.ticket.serial) { return; }
    HttpCompletion completion={0};
    bool ok=live_http(h,request.url,HTTP_GET,NULL,0,L"application/json",request.body,request.capacity,&completion,cancel,true);
    LyricsResult result=worker_cancelled(cancel) ? LYRICS_CANCELLED : ok ? LYRICS_OK : LYRICS_ERROR;
    slot->lyrics_result=lyrics_complete(h->lyrics,request.ticket,result,completion.status,completion.bytes,&slot->lyrics);
}
