#include "track_filter.h"
#include <string.h>

bool track_utf8(const char *s, size_t n)
{
    if (!s && n) { return false; }
    for (size_t i=0;i<n;) {
        unsigned char c=(unsigned char)s[i++]; uint32_t cp, rest, minimum;
        if (c<128) { if (!c) { return false; } continue; }
        if (c>=0xc2 && c<=0xdf) { cp=c&31u; rest=1; minimum=128; }
        else if (c>=0xe0 && c<=0xef) { cp=c&15u; rest=2; minimum=2048; }
        else if (c>=0xf0 && c<=0xf4) { cp=c&7u; rest=3; minimum=65536; }
        else { return false; }
        if (rest>n-i) { return false; }
        while (rest--) { c=(unsigned char)s[i++]; if ((c&0xc0)!=0x80) { return false; } cp=(cp<<6)|(c&63); }
        if (cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) { return false; }
    }
    return true;
}
static bool string(const char *s, size_t cap)
{
    const char *end=memchr(s,0,cap); return end && track_utf8(s,(size_t)(end-s));
}
bool track_record_valid(const TrackRecord *r)
{
    if (!r || (r->flags & ~(uint32_t)(TRACK_ID|TRACK_METADATA|TRACK_PLAYABLE)) ||
        ((r->flags&TRACK_PLAYABLE) && !(r->flags&TRACK_ID))) { return false; }
    uint8_t id=0; for (unsigned i=0;i<16;++i) { id|=r->id[i]; }
    return (!(r->flags&TRACK_ID) || id) && string(r->title,sizeof(r->title)) &&
        string(r->artist,sizeof(r->artist)) && string(r->album,sizeof(r->album));
}
static unsigned char fold(unsigned char c) { return c>='A' && c<='Z' ? (unsigned char)(c+32) : c; }
static bool contains(const char *s, const char *query)
{
    for (;*s;++s) {
        size_t j=0; while (query[j] && s[j] && fold((unsigned char)s[j])==fold((unsigned char)query[j])) { ++j; }
        if (!query[j]) { return true; }
    }
    return false;
}
bool track_matches(const TrackRecord *r, const char *query)
{
    return !*query || ((r->flags&TRACK_METADATA) &&
        (contains(r->title,query) || contains(r->artist,query) || contains(r->album,query)));
}
