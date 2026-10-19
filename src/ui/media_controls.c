#include "media_controls.h"
#include <string.h>

static bool owner(const MediaControls *m) { return m && m->alive && m->owner==GetCurrentThreadId(); }
static bool text(const char *s, size_t cap)
{
    const char *end=memchr(s,0,cap);
    return end && (!*s || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,(int)(end-s),NULL,0)>0);
}
static bool timespan(uint64_t frames, uint32_t rate, uint64_t *value)
{
    if (!rate) { return false; }
    uint64_t whole=frames/rate, part=frames%rate*10000000/rate;
    if (whole>(uint64_t)INT64_MAX/10000000 || part>(uint64_t)INT64_MAX-whole*10000000) { return false; }
    *value=whole*10000000+part; return true;
}
MediaResult media_controls_init(MediaControls *m, MediaConfig config, MediaAdapter adapter)
{
    if (!m || !config.submit || !config.wake || !adapter.open || !adapter.publish || !adapter.close) { return MEDIA_INVALID; }
    memset(m,0,sizeof(*m)); InitializeSRWLock(&m->lock);
    atomic_init(&m->stats.queued,0); atomic_init(&m->stats.submitted,0); atomic_init(&m->stats.duplicates,0);
    atomic_init(&m->stats.stale,0); atomic_init(&m->stats.full,0); atomic_init(&m->stats.publications,0);
    m->config=config; m->adapter=adapter; m->owner=GetCurrentThreadId(); m->alive=true;
    MediaResult r=adapter.open(adapter.context,m);
    if (r!=MEDIA_OK && r!=MEDIA_UNSUPPORTED) {
        m->closing=true; MediaResult cleanup=adapter.close(adapter.context);
        if (cleanup==MEDIA_OK) { m->alive=false; } else { r=cleanup; }
    }
    return r;
}
MediaResult media_controls_publish(MediaControls *m, const MediaMetadata *metadata, PlayerSnapshot p, uint32_t caps, uint64_t now)
{
    if (!owner(m) || m->closing) { return MEDIA_CLOSED; }
    if (!metadata || (unsigned)p.state>PLAYER_ERROR || caps>63 || !text(metadata->title,sizeof(metadata->title)) ||
        !text(metadata->artist,sizeof(metadata->artist)) || !text(metadata->artwork,sizeof(metadata->artwork)) ||
        (metadata->artwork[0] && strncmp(metadata->artwork,"https://",8)) ||
        memcmp(metadata->track_id,p.track_id,16)) { return MEDIA_INVALID; }
    MediaPublication next={.metadata=*metadata,.player=p,.capabilities=caps};
    bool has_track=false; for (unsigned i=0;i<16;++i) { has_track|=p.track_id[i]!=0; }
    next.status=!has_track ? MEDIA_STATUS_CLOSED : p.state==PLAYER_PLAYING ? MEDIA_STATUS_PLAYING :
        p.state==PLAYER_PAUSED ? MEDIA_STATUS_PAUSED : p.state==PLAYER_LOADING || p.state==PLAYER_SEEKING || p.state==PLAYER_BUFFERING ?
        MEDIA_STATUS_CHANGING : MEDIA_STATUS_STOPPED;
    bool valid=(p.timeline_valid || p.state==PLAYER_PAUSED || p.state==PLAYER_ENDED) && p.rate;
    if (p.rate && (!timespan(p.duration_frames,p.rate,&next.duration) ||
        (valid && !timespan(p.position_frames,p.rate,&next.position)))) { return MEDIA_LIMIT; }
    if (next.duration && next.position>next.duration) { next.position=next.duration; }
    if (!valid || !next.duration) { next.capabilities&=~(uint32_t)MEDIA_CAN_SEEK; }
    if (!has_track) { next.capabilities=0; }
    AcquireSRWLockExclusive(&m->lock); m->latest=p; m->capabilities=next.capabilities; ReleaseSRWLockExclusive(&m->lock);
    uint32_t changes=0;
    if (!m->has_publication || memcmp(&next.metadata,&m->published.metadata,sizeof(next.metadata))) { changes|=MEDIA_METADATA; }
    if (!m->has_publication || next.status!=m->published.status || next.capabilities!=m->published.capabilities) { changes|=MEDIA_STATE; }
    bool force=!m->has_publication || p.generation!=m->published.player.generation ||
        memcmp(p.track_id,m->published.player.track_id,16) || (changes&MEDIA_STATE)!=0;
    if (force || ((now>=m->published_at && now-m->published_at>=1000) &&
        (next.position!=m->published.position || next.duration!=m->published.duration))) { changes|=MEDIA_TIMELINE; }
    if (!changes) { return MEDIA_OK; }
    MediaResult r=m->adapter.publish(m->adapter.context,&next,changes);
    if (r==MEDIA_OK) {
        if (!(changes&MEDIA_TIMELINE)) { next.position=m->published.position; next.duration=m->published.duration; }
        else { m->published_at=now; }
        m->published=next; m->has_publication=true; ++m->stats.publications;
    }
    return r;
}
MediaResult media_controls_post(MediaControls *m, MediaAction action, MediaSource source, int64_t position, uint64_t now)
{
    if (!m || (unsigned)action>MEDIA_SEEK || (unsigned)source>MEDIA_KEY || (action==MEDIA_SEEK && position<0)) { return MEDIA_INVALID; }
    AcquireSRWLockExclusive(&m->lock);
    MediaResult r=m->closing || !m->alive ? MEDIA_CLOSED : m->count==MEDIA_INPUT_CAP ? MEDIA_FULL : MEDIA_OK;
    if (r==MEDIA_OK) {
        MediaInput *e=&m->inputs[(m->head+m->count++)%MEDIA_INPUT_CAP];
        *e=(MediaInput){.action=action,.source=source,.position=position,.now=now,.generation=m->latest.generation};
        memcpy(e->track_id,m->latest.track_id,16); ++m->stats.queued;
    } else if (r==MEDIA_FULL) { ++m->stats.full; }
    void (*wake)(void *)=m->config.wake; void *context=m->config.context;
    ReleaseSRWLockExclusive(&m->lock);
    if (r==MEDIA_OK || r==MEDIA_FULL) { wake(context); } return r;
}
static bool family(MediaAction a, MediaAction b)
{
    return a==b || ((a==MEDIA_TOGGLE || b==MEDIA_TOGGLE) && a<=MEDIA_TOGGLE && b<=MEDIA_TOGGLE);
}
static MediaResult dispatch(MediaControls *m, MediaInput e)
{
    PlayerSnapshot p=m->latest;
    if (e.generation!=p.generation || memcmp(e.track_id,p.track_id,16)) { ++m->stats.stale; return MEDIA_STALE; }
    if (m->has_last && e.source!=m->last.source && e.now>=m->last.now && e.now-m->last.now<=MEDIA_DUPLICATE_MS &&
        (!memcmp(e.track_id,m->last.track_id,16) || e.action==MEDIA_NEXT || e.action==MEDIA_PREVIOUS) && family(e.action,m->last.action) &&
        (e.action!=MEDIA_SEEK || e.position==m->last.position)) { ++m->stats.duplicates; return MEDIA_OK; }
    PlayerCommand c={0}; uint32_t needed;
    switch (e.action) {
    case MEDIA_PLAY: c.type=PLAYER_PLAY; needed=MEDIA_CAN_PLAY; if (p.state==PLAYER_PLAYING) { return MEDIA_OK; } break;
    case MEDIA_PAUSE: c.type=PLAYER_PAUSE; needed=MEDIA_CAN_PAUSE; if (p.state==PLAYER_PAUSED) { return MEDIA_OK; } break;
    case MEDIA_TOGGLE: c.type=p.state==PLAYER_PLAYING ? PLAYER_PAUSE : PLAYER_PLAY; needed=p.state==PLAYER_PLAYING ? MEDIA_CAN_PAUSE : MEDIA_CAN_PLAY; break;
    case MEDIA_STOP: c.type=PLAYER_STOP; needed=MEDIA_CAN_STOP; break;
    case MEDIA_NEXT: c.type=PLAYER_NEXT; needed=MEDIA_CAN_NEXT; break;
    case MEDIA_PREVIOUS: c.type=PLAYER_PREVIOUS; needed=MEDIA_CAN_PREVIOUS; break;
    case MEDIA_SEEK: {
        c.type=PLAYER_SEEK; needed=MEDIA_CAN_SEEK;
        uint64_t value=(uint64_t)e.position, whole=value/10000000, part=value%10000000*p.rate/10000000;
        if (!p.rate || whole>UINT64_MAX/p.rate || part>UINT64_MAX-whole*p.rate) { return MEDIA_LIMIT; }
        c.position_frames=whole*p.rate+part; if (c.position_frames>p.duration_frames) { c.position_frames=p.duration_frames; } break;
    }
    default: return MEDIA_INVALID;
    }
    if (!(m->capabilities&needed)) { return MEDIA_UNSUPPORTED; }
    PlayerResult r=m->config.submit(m->config.context,&c);
    if (r==PLAYER_OK) { m->last=e; m->has_last=true; ++m->stats.submitted; }
    else if (r==PLAYER_FULL) { ++m->stats.full; }
    return r==PLAYER_OK ? MEDIA_OK : r==PLAYER_FULL ? MEDIA_FULL : MEDIA_CLOSED;
}
MediaResult media_controls_pump(MediaControls *m)
{
    if (!owner(m) || m->closing) { return MEDIA_CLOSED; }
    MediaResult result=MEDIA_OK;
    for (unsigned i=0;i<MEDIA_INPUT_CAP;++i) {
        AcquireSRWLockExclusive(&m->lock);
        if (!m->count || m->closing) { ReleaseSRWLockExclusive(&m->lock); break; }
        MediaInput e=m->inputs[m->head]; memset(&m->inputs[m->head],0,sizeof(e));
        m->head=(m->head+1)%MEDIA_INPUT_CAP; --m->count; ReleaseSRWLockExclusive(&m->lock);
        MediaResult r=dispatch(m,e); if (r!=MEDIA_OK) { result=r; }
    }
    return result;
}
MediaResult media_controls_close(MediaControls *m)
{
    if (!m) { return MEDIA_INVALID; }
    if (!m->alive) { return MEDIA_OK; }
    if (m->owner!=GetCurrentThreadId()) { return MEDIA_INVALID; }
    AcquireSRWLockExclusive(&m->lock); m->closing=true; m->count=0;
    memset(m->inputs,0,sizeof(m->inputs)); ReleaseSRWLockExclusive(&m->lock);
    MediaResult r=m->adapter.close(m->adapter.context); if (r==MEDIA_OK) { m->alive=false; }
    memset(&m->published,0,sizeof(m->published)); memset(&m->latest,0,sizeof(m->latest)); return r;
}
MediaResult media_controls_appcommand(MediaControls *m, unsigned command, uint64_t now)
{
    MediaAction a;
    switch (command) {
    case APPCOMMAND_MEDIA_PLAY: a=MEDIA_PLAY; break; case APPCOMMAND_MEDIA_PAUSE: a=MEDIA_PAUSE; break;
    case APPCOMMAND_MEDIA_PLAY_PAUSE: a=MEDIA_TOGGLE; break; case APPCOMMAND_MEDIA_STOP: a=MEDIA_STOP; break;
    case APPCOMMAND_MEDIA_NEXTTRACK: a=MEDIA_NEXT; break; case APPCOMMAND_MEDIA_PREVIOUSTRACK: a=MEDIA_PREVIOUS; break;
    default: return MEDIA_UNSUPPORTED;
    }
    return media_controls_post(m,a,MEDIA_WINDOW,0,now);
}
MediaResult media_controls_key(MediaControls *m, unsigned key, bool repeat, uint64_t now)
{
    unsigned command;
    switch (key) {
    case VK_MEDIA_PLAY_PAUSE: command=APPCOMMAND_MEDIA_PLAY_PAUSE; break;
    case VK_MEDIA_STOP: command=APPCOMMAND_MEDIA_STOP; break;
    case VK_MEDIA_NEXT_TRACK: command=APPCOMMAND_MEDIA_NEXTTRACK; break;
    case VK_MEDIA_PREV_TRACK: command=APPCOMMAND_MEDIA_PREVIOUSTRACK; break;
    default: return MEDIA_UNSUPPORTED;
    }
    if (repeat) { return MEDIA_OK; }
    MediaAction action=command==APPCOMMAND_MEDIA_PLAY_PAUSE ? MEDIA_TOGGLE : command==APPCOMMAND_MEDIA_STOP ? MEDIA_STOP :
        command==APPCOMMAND_MEDIA_NEXTTRACK ? MEDIA_NEXT : MEDIA_PREVIOUS;
    return media_controls_post(m,action,MEDIA_KEY,0,now);
}
