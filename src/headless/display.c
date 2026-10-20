#include "internal.h"
void hl_publish_display(Headless *h, const uint8_t id[16], const MediaDisplay *display)
{
    AcquireSRWLockExclusive(&h->display_lock);
    unsigned slot=0;
    for (unsigned i=0;i<4;++i) {
        if (!memcmp(h->displays[i].id,id,16)) { slot=i; break; }
        if (h->displays[i].used<h->displays[slot].used) { slot=i; }
    }
    memcpy(h->displays[slot].id,id,16); h->displays[slot].display=*display;
    h->displays[slot].used=++h->display_serial;
    ReleaseSRWLockExclusive(&h->display_lock);
    if (h->config.state_changed) { h->config.state_changed(h->config.state_context); }
}
bool headless_track_info(Headless *h, const uint8_t id[16], MediaDisplay *out)
{
    if (!h || !id || !out || atomic_load(&h->closing)) { return false; }
    bool found=false; AcquireSRWLockShared(&h->display_lock);
    for (unsigned i=0;i<4;++i) {
        if (h->displays[i].used && !memcmp(h->displays[i].id,id,16)) { *out=h->displays[i].display; found=true; break; }
    }
    ReleaseSRWLockShared(&h->display_lock); return found;
}
bool headless_queue_snapshot(Headless *h, PlayerQueueSnapshot *out)
{
    if (!h || !out) { return false; }
    AcquireSRWLockShared(&h->lifecycle);
    bool valid=h->player && !atomic_load(&h->closing) && !atomic_load(&h->failed);
    if (valid) { player_queue_snapshot(h->player,out); }
    ReleaseSRWLockShared(&h->lifecycle); return valid;
}
