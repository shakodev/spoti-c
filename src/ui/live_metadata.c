#include "live_internal.h"
#include "api/metadata.h"
#include "extension_kind.pb.h"
#include "pb_common.h"
#include <string.h>
#include <wchar.h>

static bool copy(MetadataSpan s, char *out, size_t cap)
{
    if (memchr(s.data,0,s.bytes) || (s.bytes && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char *)s.data,(int)s.bytes,NULL,0))) { return false; }
    /* Presentation fields may truncate at a UTF-8 boundary. Identity never does. */
    size_t n=s.bytes<cap ? s.bytes : cap-1;
    while (n && n<s.bytes && (s.data[n]&0xc0)==0x80) { --n; }
    memcpy(out,s.data,n); out[n]=0; return pb_validate_utf8(out);
}
static bool field_name(MetadataSpan s, char *out, size_t cap)
{
    MetadataReader r={s,MEDIA_OK,0}; uint32_t tag; pb_wire_type_t wire;
    while (metadata_next(&r,&tag,&wire)) {
        if (tag==2) { MetadataSpan value; if (!metadata_span(&r,wire,&value) || !copy(value,out,cap)) { return false; } }
        else if (!metadata_skip(&r,wire)) { break; }
    }
    return r.result==MEDIA_OK;
}
static bool track(MetadataSpan s, TrackRecord *row)
{
    MetadataReader r={s,MEDIA_OK,0}; uint32_t tag; pb_wire_type_t wire; uint64_t seen=0;
    while (metadata_next(&r,&tag,&wire)) {
        if (tag==1 || tag==2 || tag==3 || tag==4) {
            MetadataSpan value;
            if (tag!=4 && !metadata_once(&r,&seen,tag)) { return false; }
            if (!metadata_span(&r,wire,&value)) { return false; }
            if (tag==1 && (value.bytes!=16 || memcmp(value.data,row->id,16))) { return false; }
            if (tag==2 && !copy(value,row->title,sizeof(row->title))) { return false; }
            if (tag==3 && !field_name(value,row->album,sizeof(row->album))) { return false; }
            if (tag==4 && !row->artist[0] && !field_name(value,row->artist,sizeof(row->artist))) { return false; }
        } else if (tag==7) {
            uint64_t v;
            if (!metadata_once(&r,&seen,tag) || !metadata_uint(&r,wire,&v) || v>UINT32_MAX || (v&1)) { return false; }
            row->duration_ms=(uint32_t)(v>>1);
        } else if (!metadata_skip(&r,wire)) { break; }
    }
    if (r.result!=MEDIA_OK || (seen&6)!=6) { return false; }
    row->flags|=TRACK_METADATA; return true;
}
static bool entity(MetadataSpan s, TrackRecord *rows, uint32_t count)
{
    MetadataReader r={s,MEDIA_OK,0}; MetadataSpan uri={0}, any={0}; bool header=false, success=false;
    uint32_t tag; pb_wire_type_t wire; uint64_t seen=0;
    while (metadata_next(&r,&tag,&wire)) {
        if (tag>=1 && tag<=3) {
            MetadataSpan value;
            if (!metadata_once(&r,&seen,tag) || !metadata_span(&r,wire,&value)) { return false; }
            if (tag==2) { uri=value; }
            else if (tag==3) { any=value; }
            else {
                MetadataReader hr={value,MEDIA_OK,0}; uint32_t ht; pb_wire_type_t hw;
                while (metadata_next(&hr,&ht,&hw)) {
                    if (ht==1) {
                        uint64_t status; if (header || !metadata_uint(&hr,hw,&status)) { return false; }
                        header=true; success=status==200;
                    } else if (!metadata_skip(&hr,hw)) { break; }
                }
                if (hr.result!=MEDIA_OK) { return false; }
            }
        } else if (!metadata_skip(&r,wire)) { break; }
    }
    if (r.result!=MEDIA_OK || !header || !uri.bytes || uri.bytes>=MEDIA_URI_BYTES) { return false; }
    if (!success) { return true; } /* Unavailable metadata is explicit per row. */
    char text[MEDIA_URI_BYTES]; uint8_t gid[16]; memcpy(text,uri.data,uri.bytes); text[uri.bytes]=0;
    if (memchr(text,0,uri.bytes) || media_gid_from_uri(text,gid)!=MEDIA_OK) { return false; }
    MetadataReader ar={any,MEDIA_OK,0}; MetadataSpan payload={0}; bool typed=false;
    while (metadata_next(&ar,&tag,&wire)) {
        if (tag==1 || tag==2) {
            MetadataSpan value; if (!metadata_span(&ar,wire,&value)) { return false; }
            if (tag==1) { typed=metadata_equal(value,"type.googleapis.com/spotify.metadata.Track"); }
            else { payload=value; }
        } else if (!metadata_skip(&ar,wire)) { break; }
    }
    if (ar.result!=MEDIA_OK || !typed || !payload.bytes) { return false; }
    bool matched=false;
    for (uint32_t i=0;i<count;++i) {
        if (!memcmp(rows[i].id,gid,16)) {
            TrackRecord value=rows[i];
            if (!track(payload,&value)) { return false; }
            rows[i]=value; matched=true;
        }
    }
    return matched;
}
bool live_metadata_decode(const void *data, size_t bytes, TrackRecord *rows, uint32_t count)
{
    if (!data || !bytes || bytes>256u*1024u || !rows || !count || count>LIVE_ROWS) { return false; }
    MetadataReader r={{data,bytes},MEDIA_OK,0}; uint32_t tag; pb_wire_type_t wire;
    bool found=false;
    while (metadata_next(&r,&tag,&wire)) {
        if (tag==2) {
            MetadataSpan array; if (!metadata_span(&r,wire,&array)) { return false; }
            found=true;
            MetadataReader a={array,MEDIA_OK,0}; uint32_t at; pb_wire_type_t aw; bool kind=false;
            while (metadata_next(&a,&at,&aw)) {
                if (at==2) {
                    uint64_t v; if (!metadata_uint(&a,aw,&v) || v!=spotify_extendedmetadata_ExtensionKind_TRACK_V4) { return false; } kind=true;
                } else if (at==3) {
                    MetadataSpan e; if (!metadata_span(&a,aw,&e) || !entity(e,rows,count)) { return false; }
                } else if (!metadata_skip(&a,aw)) { break; }
            }
            if (a.result!=MEDIA_OK || !kind) { return false; }
        } else if (!metadata_skip(&r,wire)) { break; }
    }
    return found && r.result==MEDIA_OK;
}
bool live_track_metadata(AppLive *h, TrackRecord *rows, uint32_t count, const CancelToken *cancel)
{
    wchar_t url[HTTP_URL_CHARS];
    if (swprintf_s(url,HTTP_URL_CHARS,L"%ls/extended-metadata/v0/extended-metadata",h->origin)<=0) { return false; }
    for (uint32_t first=0;first<count;first+=8) {
        uint32_t length=count-first; if (length>8) { length=8; }
        uint8_t upload[1024]; size_t used=0;
        for (uint32_t i=0;i<length;++i) {
            TrackRecord *row=&rows[first+i]; if (!(row->flags&TRACK_ID)) { continue; }
            char uri[MEDIA_URI_BYTES]; size_t written;
            if (media_gid_to_uri(row->id,uri)!=MEDIA_OK ||
                metadata_request_body(uri,upload+used,sizeof(upload)-used,&written)!=MEDIA_OK) { return false; }
            used+=written;
        }
        if (!used) { continue; }
        HttpCompletion completion;
        if (!live_http(h,url,HTTP_POST,upload,used,L"application/protobuf",h->response,sizeof(h->response),&completion,cancel,true) ||
            completion.status!=200 || !live_metadata_decode(h->response,completion.bytes,rows+first,length)) { return false; }
    }
    return true;
}
