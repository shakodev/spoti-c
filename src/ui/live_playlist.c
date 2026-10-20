#include "live_internal.h"
#include <string.h>
#include <stdio.h>

bool live_playlist(AppLive *h, LiveSlot *slot, const CancelToken *cancel)
{
    if (!track_disk_close(&h->disk)) { return false; }
    if (GetFileAttributesW(h->index_path)!=INVALID_FILE_ATTRIBUTES && !DeleteFileW(h->index_path)) { return false; }
    uint32_t offset=0, total=0; char revision[64]={0}; bool opened=false;
    for (;;) {
        if (worker_cancelled(cancel) || !live_list_fetch(h,slot->request.uri,offset,cancel)) {
            strcpy_s(slot->message,sizeof(slot->message),h->failure[0] ? h->failure : "Playlist page could not be decoded."); goto fail;
        }
        LiveList *list=&h->list;
        if (list->total>100000) { strcpy_s(slot->message,sizeof(slot->message),"This playlist exceeds the 100,000-track local index limit."); goto fail; }
        if (!opened) {
            if (!list->revision[0] || strlen(list->revision)>=sizeof(revision)) { goto fail; }
            strcpy_s(revision,sizeof(revision),list->revision); total=list->total;
            TrackResult created=track_disk_create(&h->disk,h->index_path,slot->request.uri,revision,true);
            if (created!=TRACK_OK) { (void)snprintf(slot->message,sizeof(slot->message),"Index open %u (OS %lu).",(unsigned)created,GetLastError()); goto fail; }
            opened=true;
        } else if (strcmp(revision,list->revision) || total!=list->total) { strcpy_s(slot->message,sizeof(slot->message),"The playlist changed while loading. Please retry."); goto fail; }
        TrackRecord rows[LIVE_ROWS]={0};
        for (uint32_t i=0;i<list->count;++i) {
            LiveRow *item=&list->rows[i];
            if (media_gid_from_uri(item->uri,rows[i].id)==MEDIA_OK) { rows[i].flags=TRACK_ID|TRACK_PLAYABLE; }
            if (strlen(item->uri)<sizeof(rows[i].title)) { strcpy_s(rows[i].title,sizeof(rows[i].title),item->uri); }
        }
        if (list->count && !live_track_metadata(h,rows,list->count,cancel)) { strcpy_s(slot->message,sizeof(slot->message),h->failure[0] ? h->failure : "Track metadata could not be decoded."); goto fail; }
        TrackResult appended=track_disk_append(&h->disk,rows,list->count);
        if (appended!=TRACK_OK) { (void)snprintf(slot->message,sizeof(slot->message),"Index write %u (OS %lu).",(unsigned)appended,GetLastError()); goto fail; }
        offset+=list->count;
        if (offset>=total) { break; }
        if (!list->count) { goto fail; }
    }
    if (track_disk_finish(&h->disk)!=TRACK_OK) { goto fail; }
    slot->source=h->disk.source; return true;
fail:
    (void)track_disk_close(&h->disk); (void)DeleteFileW(h->index_path); return false;
}
