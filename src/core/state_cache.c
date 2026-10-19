#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "state_cache.h"
#include <string.h>
#include <stdio.h>

#define CACHE_MAGIC 0x53435453u
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i=0;i<4;++i) { p[i]=(uint8_t)(v>>(8*i)); } }
static uint32_t checksum(const uint8_t *p,size_t n)
{
    uint32_t crc=UINT32_MAX;
    for (size_t i=0;i<n;++i) {
        crc^=(i>=16 && i<20) ? 0 : p[i];
        for (unsigned j=0;j<8;++j) { crc=(crc>>1)^(0xedb88320u & (0u-(crc&1))); }
    }
    return ~crc;
}
void state_cache_defaults(StateCache *s,uint64_t epoch)
{
    if (s) { memset(s,0,sizeof(*s)); s->epoch=epoch; s->width=1100; s->height=720; s->volume=1000; }
}
static bool label_ok(const char *p)
{
    size_t n=0; while (n<STATE_CACHE_LABEL && p[n]) { if ((uint8_t)p[n]<32) { return false; } ++n; }
    return n<STATE_CACHE_LABEL && (!n || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p,(int)n,NULL,0)>0);
}
static bool valid(const StateCache *s)
{
    if (s->width<420 || s->width>16384 || s->height<360 || s->height>16384 ||
        s->x < -65536 || s->x>65536 || s->y < -65536 || s->y>65536 || s->route>8 ||
        s->detail>1 || s->volume>1000 || s->repeat>2 || s->shuffle>1 || s->scroll>100000000 ||
        s->route_kind>8 || s->count>STATE_CACHE_ENTITIES) { return false; }
    for (uint32_t i=0;i<s->count;++i) {
        if (s->entities[i].kind>8 || !label_ok(s->entities[i].label)) { return false; }
    }
    return true;
}
StateCacheResult state_cache_encode(const StateCache *s,void *data,size_t cap,size_t *written)
{
    if (written) { *written=0; }
    if (!s || !data || !written || !valid(s)) { return STATE_CACHE_INVALID; }
    size_t size=STATE_CACHE_HEADER+STATE_CACHE_SETTINGS+(size_t)s->count*STATE_CACHE_ENTRY;
    if (cap<size) { return STATE_CACHE_FULL; }
    uint8_t *p=data; memset(p,0,size);
    put32(p,CACHE_MAGIC); put32(p+4,2); put32(p+8,STATE_CACHE_HEADER); put32(p+12,(uint32_t)size);
    put32(p+24,(uint32_t)s->epoch); put32(p+28,(uint32_t)(s->epoch>>32));
    put32(p+32,STATE_CACHE_HEADER); put32(p+36,STATE_CACHE_SETTINGS);
    put32(p+40,STATE_CACHE_HEADER+STATE_CACHE_SETTINGS); put32(p+44,s->count);
    uint8_t *v=p+STATE_CACHE_HEADER;
    put32(v,(uint32_t)s->x); put32(v+4,(uint32_t)s->y); put32(v+8,s->width); put32(v+12,s->height);
    put32(v+16,s->route); put32(v+20,s->detail); put32(v+24,s->volume); put32(v+28,s->repeat);
    put32(v+32,s->shuffle); put32(v+36,s->scroll); put32(v+40,s->route_kind);
    memcpy(v+44,s->route_id,16); memcpy(v+60,s->track_id,16);
    for (uint32_t i=0;i<s->count;++i) {
        v=p+STATE_CACHE_HEADER+STATE_CACHE_SETTINGS+(size_t)i*STATE_CACHE_ENTRY;
        put32(v,s->entities[i].kind); memcpy(v+4,s->entities[i].id,16);
        memcpy(v+20,s->entities[i].label,strlen(s->entities[i].label));
    }
    put32(p+16,checksum(p,size)); *written=size; return STATE_CACHE_OK;
}
StateCacheResult state_cache_decode(const void *data,size_t n,uint64_t epoch,StateCache *out)
{
    if (!out) { return STATE_CACHE_INVALID; }
    state_cache_defaults(out,epoch);
    if (!data || n<STATE_CACHE_HEADER || n>STATE_CACHE_MAX_BYTES) { return STATE_CACHE_CORRUPT; }
    const uint8_t *p=data;
    if (get32(p)!=CACHE_MAGIC || get32(p+8)!=STATE_CACHE_HEADER || get32(p+12)!=n || get32(p+20)) { return STATE_CACHE_CORRUPT; }
    uint32_t version=get32(p+4);
    if (version!=1 && version!=2) { return STATE_CACHE_VERSION; }
    uint32_t offset=get32(p+32), bytes=get32(p+36), entities=get32(p+40), count=get32(p+44);
    if (offset!=STATE_CACHE_HEADER || bytes!=STATE_CACHE_SETTINGS || entities!=offset+bytes ||
        count>STATE_CACHE_ENTITIES || entities>n || (n-entities)/STATE_CACHE_ENTRY!=count ||
        (n-entities)%STATE_CACHE_ENTRY || checksum(p,n)!=get32(p+16)) { return STATE_CACHE_CORRUPT; }
    StateCache s; state_cache_defaults(&s,epoch); const uint8_t *v=p+offset;
    s.x=(int32_t)get32(v); s.y=(int32_t)get32(v+4); s.width=get32(v+8); s.height=get32(v+12);
    s.route=get32(v+16); s.detail=get32(v+20); s.volume=get32(v+24);
    s.repeat=version==2 ? get32(v+28) : 0; s.shuffle=version==2 ? get32(v+32) : 0;
    s.scroll=get32(v+36); s.route_kind=get32(v+40); memcpy(s.route_id,v+44,16); memcpy(s.track_id,v+60,16); s.count=count;
    for (uint32_t i=0;i<count;++i) {
        v=p+entities+(size_t)i*STATE_CACHE_ENTRY; s.entities[i].kind=get32(v);
        memcpy(s.entities[i].id,v+4,16); memcpy(s.entities[i].label,v+20,STATE_CACHE_LABEL);
    }
    if (!valid(&s)) { return STATE_CACHE_CORRUPT; }
    uint64_t stored=(uint64_t)get32(p+24)|(uint64_t)get32(p+28)<<32;
    if (stored!=epoch) {
        s.count=0; s.route=0; s.scroll=0; s.route_kind=0;
        memset(s.entities,0,sizeof(s.entities)); memset(s.route_id,0,16); memset(s.track_id,0,16);
    }
    *out=s; return stored==epoch ? STATE_CACHE_OK : STATE_CACHE_STALE;
}
StateCacheResult state_cache_load(const wchar_t *path,uint64_t epoch,StateCache *out)
{
    if (!out) { return STATE_CACHE_INVALID; } state_cache_defaults(out,epoch);
    if (!path || !path[0]) { return STATE_CACHE_INVALID; }
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (f==INVALID_HANDLE_VALUE) { return GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND ? STATE_CACHE_MISSING : STATE_CACHE_IO; }
    LARGE_INTEGER size; uint8_t wire[STATE_CACHE_WIRE_MAX]; DWORD read=0; StateCacheResult result=STATE_CACHE_CORRUPT;
    if (GetFileSizeEx(f,&size) && size.QuadPart>=STATE_CACHE_HEADER && size.QuadPart<=sizeof(wire)) {
        if (ReadFile(f,wire,(DWORD)size.QuadPart,&read,NULL) && read==size.QuadPart) { result=state_cache_decode(wire,read,epoch,out); }
        else { result=STATE_CACHE_IO; }
    }
    if (!CloseHandle(f)) { state_cache_defaults(out,epoch); return STATE_CACHE_IO; } return result;
}
StateCacheResult state_cache_save(const wchar_t *path,const StateCache *s,StateCacheCommit commit,void *context)
{
    static volatile LONG serial;
    if (!path || !path[0] || wcslen(path)>220) { return STATE_CACHE_INVALID; }
    uint8_t wire[STATE_CACHE_WIRE_MAX]; size_t n;
    StateCacheResult result=state_cache_encode(s,wire,sizeof(wire),&n);
    if (result!=STATE_CACHE_OK) { return result; }
    wchar_t temp[260];
    int length=swprintf(temp,260,L"%ls.%lu.%lu.tmp",path,GetCurrentProcessId(),(unsigned long)InterlockedIncrement(&serial));
    if (length<0 || length>=260) { return STATE_CACHE_FULL; }
    HANDLE f=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if (f==INVALID_HANDLE_VALUE) { return STATE_CACHE_IO; }
    DWORD wrote=0; bool ok=WriteFile(f,wire,(DWORD)n,&wrote,NULL)!=0 && wrote==n && FlushFileBuffers(f)!=0;
    ok=CloseHandle(f)!=0 && ok;
    result=ok ? STATE_CACHE_OK : STATE_CACHE_IO;
    if (ok && commit && !commit(context)) { ok=false; result=STATE_CACHE_CANCELLED; }
    if (ok && !MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) { result=STATE_CACHE_IO; }
    if (result!=STATE_CACHE_OK) { (void)DeleteFileW(temp); } return result;
}
