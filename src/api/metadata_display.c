#include "metadata_internal.h"
#include "metadata.pb.h"
#include "pb_common.h"
#include <string.h>

static bool text(MetadataSpan s, char *out, size_t cap)
{
    /* Validate the entire field, including bytes beyond the display cap. */
    for (size_t i=0;i<s.bytes;) {
        uint8_t c=s.data[i++]; uint32_t cp, rest, minimum;
        if (c<128) { if (!c) { return false; } continue; }
        if (c>=0xc2 && c<=0xdf) { cp=c&31u; rest=1; minimum=128; }
        else if (c>=0xe0 && c<=0xef) { cp=c&15u; rest=2; minimum=2048; }
        else if (c>=0xf0 && c<=0xf4) { cp=c&7u; rest=3; minimum=65536; }
        else { return false; }
        if (rest>s.bytes-i) { return false; }
        while (rest--) { c=s.data[i++]; if ((c&0xc0)!=0x80) { return false; } cp=(cp<<6)|(c&63); }
        if (cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) { return false; }
    }
    size_t n=s.bytes<cap-1 ? s.bytes : cap-1;
    while (n<s.bytes && n && (s.data[n]&0xc0)==0x80) { --n; }
    memcpy(out,s.data,n); out[n]=0; return pb_validate_utf8(out);
}
static MediaResult artist(MetadataSpan s, MediaDisplay *out)
{
    MetadataReader r={s,MEDIA_OK,0}; uint32_t tag; pb_wire_type_t wire; bool seen=false;
    while (metadata_next(&r,&tag,&wire)) {
        if (tag==spotify_metadata_Artist_name_tag) {
            MetadataSpan value;
            if (seen || !metadata_span(&r,wire,&value) || !text(value,out->artist,sizeof(out->artist))) { return MEDIA_MALFORMED; }
            seen=true;
        } else if (!metadata_skip(&r,wire)) { break; }
    }
    return r.result;
}
static MediaResult image(MetadataSpan s, MediaDisplay *out)
{
    MetadataReader r={s,MEDIA_OK,0}; uint32_t tag; pb_wire_type_t wire; uint64_t seen=0;
    uint8_t id[20]={0}; uint64_t size=0;
    while (metadata_next(&r,&tag,&wire)) {
        if (tag==spotify_metadata_Image_file_id_tag) {
            MetadataSpan value;
            if (!metadata_once(&r,&seen,tag) || !metadata_span(&r,wire,&value) || value.bytes!=20) { return MEDIA_MALFORMED; }
            memcpy(id,value.data,20);
        } else if (tag==spotify_metadata_Image_size_tag) {
            if (!metadata_once(&r,&seen,tag) || !metadata_uint(&r,wire,&size)) { return MEDIA_MALFORMED; }
        } else if (!metadata_skip(&r,wire)) { break; }
    }
    if (r.result==MEDIA_OK && (seen&2) && (!out->has_cover || size==spotify_metadata_Image_Size_SMALL)) {
        memcpy(out->cover_id,id,20); out->has_cover=true;
    }
    return r.result;
}
static MediaResult album(MetadataSpan s, MediaDisplay *out)
{
    MetadataReader r={s,MEDIA_OK,0}; uint32_t tag; pb_wire_type_t wire;
    while (metadata_next(&r,&tag,&wire)) {
        if (tag==spotify_metadata_Album_cover_tag || tag==spotify_metadata_Album_cover_group_tag) {
            MetadataSpan value; if (!metadata_span(&r,wire,&value)) { break; }
            if (tag==spotify_metadata_Album_cover_tag) { MediaResult result=image(value,out); if (result!=MEDIA_OK) { return result; } }
            else {
                MetadataReader group={value,MEDIA_OK,0}; uint32_t member; pb_wire_type_t type;
                while (metadata_next(&group,&member,&type)) {
                    if (member==spotify_metadata_ImageGroup_image_tag) {
                        MetadataSpan item; if (!metadata_span(&group,type,&item)) { break; }
                        MediaResult result=image(item,out); if (result!=MEDIA_OK) { return result; }
                    } else if (!metadata_skip(&group,type)) { break; }
                }
                if (group.result!=MEDIA_OK) { return group.result; }
            }
        } else if (!metadata_skip(&r,wire)) { break; }
    }
    return r.result;
}
MediaResult metadata_display(MetadataSpan s, MediaDisplay *out)
{
    MetadataReader r={s,MEDIA_OK,0}; uint32_t tag; pb_wire_type_t wire; uint64_t seen=0;
    bool first_artist=false;
    while (metadata_next(&r,&tag,&wire)) {
        if (tag==spotify_metadata_Track_name_tag || tag==spotify_metadata_Track_artist_tag || tag==spotify_metadata_Track_album_tag) {
            MetadataSpan value; if (!metadata_span(&r,wire,&value)) { break; }
            if (tag!=spotify_metadata_Track_artist_tag && !metadata_once(&r,&seen,tag)) { break; }
            MediaResult result=MEDIA_OK;
            if (tag==spotify_metadata_Track_name_tag) { if (!text(value,out->title,sizeof(out->title))) { return MEDIA_MALFORMED; } }
            else if (tag==spotify_metadata_Track_album_tag) { result=album(value,out); }
            else if (!first_artist) { first_artist=true; result=artist(value,out); }
            if (result!=MEDIA_OK) { return result; }
        } else if (!metadata_skip(&r,wire)) { break; }
    }
    return r.result;
}
