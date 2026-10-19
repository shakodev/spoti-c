#include "playlist_fixture.h"
#include <stdio.h>
#include <string.h>

void playlist_fixture_record(uint32_t ordinal, TrackRecord *r)
{
    *r=(TrackRecord){.duration_ms=1000*(180+ordinal%97),.flags=TRACK_ID|TRACK_METADATA|TRACK_PLAYABLE};
    uint32_t id=ordinal/2+1; /* Deliberate duplicate songs, distinct row occurrences. */
    r->id[12]=(uint8_t)(id>>24); r->id[13]=(uint8_t)(id>>16); r->id[14]=(uint8_t)(id>>8); r->id[15]=(uint8_t)id;
    CHECK(snprintf(r->title,sizeof(r->title),"Song %05u",ordinal)>0);
    CHECK(snprintf(r->artist,sizeof(r->artist),"Artist %u",ordinal%17)>0);
    CHECK(snprintf(r->album,sizeof(r->album),"Album %u",ordinal%23)>0);
    if (ordinal%137==0) { r->flags&=~(uint32_t)TRACK_PLAYABLE; }
    if (ordinal==777) { strcpy_s(r->title,sizeof(r->title),"Bj\xc3\xb6rk - \xe6\x97\xa5\xe6\x9c\xac"); }
}
void playlist_fixture_disk(TrackDisk *disk, wchar_t path[MAX_PATH], uint32_t count)
{
    wchar_t directory[MAX_PATH]; CHECK(GetTempPathW(MAX_PATH,directory));
    CHECK(GetTempFileNameW(directory,L"spt",0,path) && DeleteFileW(path));
    CHECK(track_disk_create(disk,path,"spotify:playlist:fixture","fixture-revision-1",true)==TRACK_OK);
    TrackRecord rows[TRACK_BATCH];
    for (uint32_t first=0;first<count;) {
        uint32_t n=count-first; if (n>TRACK_BATCH) { n=TRACK_BATCH; }
        for (uint32_t i=0;i<n;++i) { playlist_fixture_record(first+i,&rows[i]); }
        CHECK(track_disk_append(disk,rows,n)==TRACK_OK); first+=n;
    }
    CHECK(track_disk_finish(disk)==TRACK_OK);
}
