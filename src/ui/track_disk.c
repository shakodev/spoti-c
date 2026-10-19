#include "track_disk.h"
#include "core/thread.h"
#include <string.h>

typedef struct DiskHeader { char magic[8]; uint32_t size, count, complete, record_bytes; char uri[192], revision[64]; } DiskHeader;
_Static_assert(sizeof(TrackRecord)==264, "private index v1 fixed record layout");
_Static_assert(sizeof(DiskHeader)==280, "private index v1 fixed header layout");
static TrackResult header(TrackDisk *d)
{
    DiskHeader h={.magic="SPCTRK1",.size=sizeof(DiskHeader),.count=d->source.total,
        .complete=d->source.complete,.record_bytes=sizeof(TrackRecord)};
    memcpy(h.uri,d->source.uri,sizeof(h.uri)); memcpy(h.revision,d->source.revision,sizeof(h.revision));
    LARGE_INTEGER zero={0}; DWORD n;
    return SetFilePointerEx(d->file,zero,NULL,FILE_BEGIN) && WriteFile(d->file,&h,sizeof(h),&n,NULL) && n==sizeof(h) ? TRACK_OK : TRACK_IO;
}
static TrackResult read(void *context, uint32_t first, uint32_t count, TrackRecord *records)
{
    TrackDisk *d=context;
    if (core_thread_is_gui() || !d || !records || d->writing || count>TRACK_BATCH ||
        first>d->source.total || count>d->source.total-first) { return TRACK_INVALID; }
    LARGE_INTEGER offset; offset.QuadPart=(LONGLONG)sizeof(DiskHeader)+(LONGLONG)first*sizeof(TrackRecord);
    DWORD n, bytes=count*(DWORD)sizeof(TrackRecord);
    return SetFilePointerEx(d->file,offset,NULL,FILE_BEGIN) && ReadFile(d->file,records,bytes,&n,NULL) && n==bytes ? TRACK_OK : TRACK_IO;
}
TrackResult track_disk_create(TrackDisk *d, const wchar_t *path, const char *uri, const char *revision, bool complete)
{
    if (!d || !path || !uri || !revision || !*revision || strlen(uri)>=192 || strlen(revision)>=64 || core_thread_is_gui()) { return TRACK_INVALID; }
    *d=(TrackDisk){.file=INVALID_HANDLE_VALUE,.writing=true};
    memcpy(d->source.uri,uri,strlen(uri)+1); memcpy(d->source.revision,revision,strlen(revision)+1); d->source.complete=complete;
    d->file=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if (d->file==INVALID_HANDLE_VALUE) { return TRACK_IO; }
    TrackResult r=header(d); if (r!=TRACK_OK) { (void)track_disk_close(d); } return r;
}
TrackResult track_disk_append(TrackDisk *d, const TrackRecord *r, uint32_t count)
{
    if (!d || !r || !d->writing || count>TRACK_BATCH || count>UINT32_MAX-d->source.total || core_thread_is_gui()) { return TRACK_INVALID; }
    for (uint32_t i=0;i<count;++i) { if (!track_record_valid(&r[i])) { return TRACK_INVALID; } }
    LARGE_INTEGER offset; offset.QuadPart=(LONGLONG)sizeof(DiskHeader)+(LONGLONG)d->source.total*sizeof(TrackRecord);
    DWORD n, bytes=count*(DWORD)sizeof(TrackRecord);
    if (!SetFilePointerEx(d->file,offset,NULL,FILE_BEGIN) || !WriteFile(d->file,r,bytes,&n,NULL) || n!=bytes) { return TRACK_IO; }
    d->source.total+=count; return TRACK_OK;
}
TrackResult track_disk_finish(TrackDisk *d)
{
    if (!d || !d->writing || core_thread_is_gui()) { return TRACK_INVALID; }
    TrackResult r=header(d);
    if (r==TRACK_OK) { d->writing=false; d->source.read=read; d->source.context=d; } return r;
}
TrackResult track_disk_open(TrackDisk *d, const wchar_t *path)
{
    if (!d || !path || core_thread_is_gui()) { return TRACK_INVALID; }
    *d=(TrackDisk){.file=INVALID_HANDLE_VALUE};
    d->file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (d->file==INVALID_HANDLE_VALUE) { return TRACK_IO; }
    DiskHeader h; DWORD n; LARGE_INTEGER size;
    if (!ReadFile(d->file,&h,sizeof(h),&n,NULL) || n!=sizeof(h) || memcmp(h.magic,"SPCTRK1",8) ||
        h.size!=sizeof(h) || h.record_bytes!=sizeof(TrackRecord) || h.complete>1 ||
        !memchr(h.uri,0,sizeof(h.uri)) || !memchr(h.revision,0,sizeof(h.revision)) || !h.revision[0] ||
        !GetFileSizeEx(d->file,&size) || size.QuadPart!=(LONGLONG)sizeof(h)+(LONGLONG)h.count*(LONGLONG)sizeof(TrackRecord)) {
        (void)track_disk_close(d); return TRACK_INVALID;
    }
    d->source=(TrackSource){.read=read,.context=d,.total=h.count,.complete=h.complete!=0};
    memcpy(d->source.uri,h.uri,sizeof(h.uri)); memcpy(d->source.revision,h.revision,sizeof(h.revision)); return TRACK_OK;
}
bool track_disk_close(TrackDisk *d)
{
    if (!d || !d->file || d->file==INVALID_HANDLE_VALUE) { return true; }
    if (!CloseHandle(d->file)) { return false; } *d=(TrackDisk){.file=INVALID_HANDLE_VALUE}; return true;
}
