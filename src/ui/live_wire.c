#include "live_wire.h"
#include "pb_common.h"
#include <string.h>

static bool text(MetadataSpan s, char *out, size_t cap)
{
    if (s.bytes>=cap || memchr(s.data,0,s.bytes)) { return false; }
    memcpy(out,s.data,s.bytes); out[s.bytes]=0; return pb_validate_utf8(out);
}
static bool name(MetadataSpan span, char *out, size_t cap)
{
    MetadataReader r={span,MEDIA_OK,0}; uint32_t tag; pb_wire_type_t wire; bool seen=false;
    while (metadata_next(&r,&tag,&wire)) {
        if (tag==1) {
            MetadataSpan s;
            if (seen || !metadata_span(&r,wire,&s) || !text(s,out,cap)) { return false; }
            seen=true;
        } else if (!metadata_skip(&r,wire)) { break; }
    }
    return r.result==MEDIA_OK;
}
static bool item(MetadataSpan span, LiveRow *out)
{
    MetadataReader r={span,MEDIA_OK,0}; uint32_t tag; pb_wire_type_t wire; bool seen=false;
    while (metadata_next(&r,&tag,&wire)) {
        if (tag==1) {
            MetadataSpan s;
            if (seen || !metadata_span(&r,wire,&s) || !s.bytes || !text(s,out->uri,sizeof(out->uri))) { return false; }
            seen=true;
        } else if (!metadata_skip(&r,wire)) { break; }
    }
    return seen && r.result==MEDIA_OK;
}
static bool meta(MetadataSpan span, LiveRow *out)
{
    MetadataReader r={span,MEDIA_OK,0}; uint32_t tag; pb_wire_type_t wire;
    while (metadata_next(&r,&tag,&wire)) {
        if (tag==2) {
            MetadataSpan s;
            if (!metadata_span(&r,wire,&s) || !name(s,out->name,sizeof(out->name))) { return false; }
        } else if (!metadata_skip(&r,wire)) { break; }
    }
    return r.result==MEDIA_OK;
}
static bool contents(MetadataSpan span, LiveList *out)
{
    MetadataReader r={span,MEDIA_OK,0}; uint32_t tag, seen=0, metas=0; pb_wire_type_t wire;
    while (metadata_next(&r,&tag,&wire)) {
        if (tag==1 || tag==2) {
            uint64_t v;
            if ((seen&(1u<<tag)) || !metadata_uint(&r,wire,&v) || v>(tag==1 ? INT32_MAX : 1u)) { return false; }
            seen|=1u<<tag;
            if (tag==1) { out->position=(uint32_t)v; } else { out->truncated=v!=0; }
        } else if (tag==3 || tag==4) {
            MetadataSpan s;
            if (!metadata_span(&r,wire,&s)) { return false; }
            if (tag==3) {
                if (out->count==LIVE_ROWS || !item(s,&out->rows[out->count++])) { return false; }
            } else {
                if (metas==LIVE_ROWS || !meta(s,&out->rows[metas++])) { return false; }
            }
        } else if (!metadata_skip(&r,wire)) { break; }
    }
    return r.result==MEDIA_OK && seen==6 && (!metas || metas==out->count);
}
bool live_list_decode(const void *data, size_t bytes, LiveList *out)
{
    if (!data || !out || bytes>256u*1024u) { return false; }
    memset(out,0,sizeof(*out));
    MetadataReader r={{data,bytes},MEDIA_OK,0}; uint32_t tag; uint64_t seen=0; pb_wire_type_t wire;
    while (metadata_next(&r,&tag,&wire)) {
        if (tag==1 || tag==2 || tag==3 || tag==5) {
            if (!metadata_once(&r,&seen,tag)) { return false; }
            if (tag==2) {
                uint64_t v; if (!metadata_uint(&r,wire,&v) || v>INT32_MAX) { return false; } out->total=(uint32_t)v;
            } else {
                MetadataSpan s; if (!metadata_span(&r,wire,&s)) { return false; }
                if (tag==1) {
                    if (!s.bytes || s.bytes>32) { return false; }
                    static const char hex[]="0123456789abcdef";
                    for (size_t i=0;i<s.bytes;++i) { out->revision[2*i]=hex[s.data[i]>>4]; out->revision[2*i+1]=hex[s.data[i]&15]; }
                    out->revision[2*s.bytes]=0;
                } else if (tag==3) {
                    if (!name(s,out->name,sizeof(out->name))) { return false; }
                } else {
                    if (!contents(s,out)) { return false; } out->contents=true;
                }
            }
        } else if (!metadata_skip(&r,wire)) { break; }
    }
    return r.result==MEDIA_OK && (seen&4)!=0 && out->position<=out->total && out->count<=out->total-out->position;
}
bool live_url_component(const char *in, wchar_t *out, size_t cap)
{
    static const wchar_t hex[]=L"0123456789ABCDEF"; size_t n=0;
    if (!in || !out || !cap) { return false; }
    for (const unsigned char *p=(const unsigned char *)in; *p; ++p) {
        bool plain=(*p>='a'&&*p<='z') || (*p>='A'&&*p<='Z') || (*p>='0'&&*p<='9') || *p=='-' || *p=='_' || *p=='.' || *p=='~';
        if (n+(plain ? 1u : 3u)>=cap) { return false; }
        if (plain) { out[n++]=*p; }
        else { out[n++]=L'%'; out[n++]=hex[*p>>4]; out[n++]=hex[*p&15]; }
    }
    out[n]=0; return true;
}
