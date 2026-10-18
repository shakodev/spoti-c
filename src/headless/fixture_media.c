/* Explicit offline injection only. Production uses the public resolver. This
 * seam installs PUBLIC synthetic key material, never an account credential. */
#include "internal.h"
#include "protocol/media_internal.h"
bool hl_fixture_media(Headless *h, HeadlessSource *s)
{
    for (unsigned i = 0; i < MEDIA_SOURCE_SLOTS; ++i) {
        MediaSlot *slot = &h->media->slots[i];
        if (slot->stage != MS_FREE) { continue; }
        if (h->media->generation == UINT64_MAX) { return false; }
        slot->id = (MediaId){++h->media->generation, i, 0};
        slot->source.key = slot->id;
        slot->source.track.format = 2;
        memcpy(slot->source.track.gid, s->request.track_id, 16);
        memset(slot->source.track.file_id, 0x19, 20);
        for (unsigned j = 0; j < 16; ++j) { slot->secret[j] = (uint8_t)j; }
        slot->source.url_count = 1;
        strcpy_s(slot->source.urls[0].url, MEDIA_URL_BYTES, h->fixture_url);
        slot->source.urls[0].refresh_at = GetTickCount64() + 86400000;
        slot->stage = MS_READY; slot->result = MEDIA_OK; slot->notified = true;
        s->media_id = slot->id; s->media = slot->source; return true;
    }
    return false;
}
