#include "app_fixture.h"
#include <stdio.h>
#include <string.h>

bool app_fixture_playlist_close(AppFixture *h)
{
    if (!track_disk_close(&h->disk)) { return false; }
    if (h->disk_path[0] && GetFileAttributesW(h->disk_path)!=INVALID_FILE_ATTRIBUTES && !DeleteFileW(h->disk_path)) { return false; }
    h->disk_path[0]=0; return true;
}

CollectionResult app_fixture_playlist_open(AppFixture *h, AppFixtureSlot *slot, const CancelToken *cancel)
{
    if (!h->disk.source.read) {
        if (!app_fixture_playlist_close(h)) { return COLLECTION_ERROR; }
        wchar_t directory[MAX_PATH];
        if (!GetTempPathW(MAX_PATH,directory) || !GetTempFileNameW(directory,L"spd",0,h->disk_path) || !DeleteFileW(h->disk_path)) { goto fail; }
        if (track_disk_create(&h->disk,h->disk_path,"fixture:playlist","offline-demo-1",true)!=TRACK_OK) { goto fail; }
        TrackRecord rows[TRACK_BATCH];
        for (uint32_t first=0;first<10005;) {
            if (worker_cancelled(cancel)) { (void)app_fixture_playlist_close(h); return COLLECTION_CANCELLED; }
            uint32_t count=10005-first; if (count>TRACK_BATCH) { count=TRACK_BATCH; }
            for (uint32_t i=0;i<count;++i) {
                uint32_t ordinal=first+i;
                rows[i]=(TrackRecord){.id={0},.flags=TRACK_ID|TRACK_METADATA|TRACK_PLAYABLE,.duration_ms=5000}; rows[i].id[15]=1;
                if (ordinal%137==0) { rows[i].flags&=~(uint32_t)TRACK_PLAYABLE; }
                (void)snprintf(rows[i].title,sizeof(rows[i].title),"Demo track %05u",ordinal+1);
                strcpy_s(rows[i].artist,sizeof(rows[i].artist),"Synthetic artist");
                strcpy_s(rows[i].album,sizeof(rows[i].album),"Offline fixture");
            }
            if (track_disk_append(&h->disk,rows,count)!=TRACK_OK) { goto fail; } first+=count;
        }
        if (track_disk_finish(&h->disk)!=TRACK_OK) { goto fail; }
    }
    slot->source=h->disk.source; memcpy(slot->source.uri,slot->request.uri,sizeof(slot->source.uri)); return COLLECTION_OK;
fail:
    (void)app_fixture_playlist_close(h); return COLLECTION_ERROR;
}
