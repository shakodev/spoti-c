#include "internal.h"
#include <wchar.h>

bool hl_arenas(Headless *h)
{
    void *p[] = {h->core,h->protocol,h->transport,h->metadata,h->range_meta,h->cache_meta,h->blocks,
        h->codecs[0],h->codecs[1],h->work[0],h->work[1],h->token_memory};
    size_t n[] = {sizeof(h->core),sizeof(h->protocol),sizeof(h->transport),sizeof(h->metadata),
        sizeof(h->range_meta),sizeof(h->cache_meta),sizeof(h->blocks),sizeof(h->codecs[0]),
        sizeof(h->codecs[1]),sizeof(h->work[0]),sizeof(h->work[1]),sizeof(h->token_memory)};
    for (unsigned i = 0; i < HL_ARENAS; ++i) {
        if (!arena_init(&h->arenas[i], p[i], n[i]) || !metrics_attach_arena(&h->accounting, &h->arenas[i], &h->bindings[i])) { return false; }
    }
    return true;
}
static ApResult fixture_key(void *ctx, uint8_t cmd, const void *data, size_t size)
{
    (void)ctx; (void)cmd; (void)data; (void)size; return AP_INVALID;
}
static MediaResult refresh(void *ctx, RangeSource range, MediaSource *out)
{
    Headless *h = ctx;
    for (unsigned i = 0; i < 2; ++i) {
        HeadlessSource *s = &h->sources[i];
        if (s->used && s->range.slot == range.slot && s->range.generation == range.generation) {
            return media_source_get(h->media, s->media_id, out);
        }
    }
    return MEDIA_STALE;
}
bool hl_network_open(Headless *h)
{
    if (!hl_arenas(h)) { fprintf(stderr,"headless startup: arenas\n"); return false; }
    if (h->config.fixture_url) {
        if (strncmp(h->fixture_url, "http://127.0.0.1:", 17)) { return false; }
        const char *slash = strchr(h->fixture_url + 17, '/');
        if (!slash || (size_t)(slash - h->fixture_url) >= 120) { return false; }
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, h->fixture_url,
            (int)(slash - h->fixture_url), h->origin, 255)) { return false; }
        HttpConfig hc = {.metrics = &h->accounting, .allow_loopback_http = true};
        if (http_client_create(&h->arenas[2], &hc, &h->http) != HTTP_OK) { fprintf(stderr,"headless startup: HTTP\n"); return false; }
        AudioKeyConfig kc = {.send = fixture_key};
        if (audio_key_create(&h->arenas[1], &kc, &h->keys) != AP_OK) { return false; }
        MediaConfig mc = {.http = h->http, .keys = h->keys, .fixture_origin = h->origin,
            .policy = {{'N','L',0}, "premium", false, 0}};
        if (media_resolver_create(&h->arenas[3], &mc, &h->media) != MEDIA_OK) { fprintf(stderr,"headless startup: media\n"); return false; }
    } else if (!hl_live_open(h)) { return false; }
    if (worker_start(&h->arenas[0], 8, (CoreClock){0}, &h->disk) != CORE_OK) { fprintf(stderr,"headless startup: disk worker\n"); return false; }
    AudioCacheConfig cc = {.worker = h->disk, .directory = h->cache_path, .metrics = &h->accounting, .entry_limit = 64};
    CacheResult cr = audio_cache_create(&h->arenas[5], &cc, &h->cache);
    if (cr != CACHE_OK) { fprintf(stderr,"headless startup: cache %u\n",(unsigned)cr); return false; }
    RangeConfig rc = {.http = h->http, .cache = h->cache, .fixture_loopback_http = h->config.fixture_url != NULL,
        .refresh = refresh, .context = h, .max_attempts = 1};
    RangeResult rr = range_fetcher_create(&h->arenas[4], &h->arenas[6], &rc, &h->ranges);
    if (rr != RANGE_OK) { fprintf(stderr,"headless startup: range %u\n",(unsigned)rr); }
    return rr == RANGE_OK;
}
bool hl_network_poll(Headless *h)
{
    if (http_pump(h->http) != HTTP_OK || !hl_live_poll(h)) { return false; }
    (void)media_source_poll(h->media);
    (void)range_poll(h->ranges);
    RangeStats stats = range_stats(h->ranges);
    atomic_store(&h->network_attempts, stats.network_attempts); atomic_store(&h->cache_hits, stats.cache_hits);
    return hl_sources(h);
}
static DWORD minimum(DWORD a, DWORD b) { return a < b ? a : b; }
void hl_network_wait(Headless *h, DWORD maximum)
{
    HANDLE handles[12]; DWORD n = 0;
    handles[n++] = h->read_wake; handles[n++] = player_request_event(h->player);
    HANDLE ranges[3]; size_t count = range_wait_handles(h->ranges, ranges);
    for (size_t i = 0; i < count; ++i) { handles[n++] = ranges[i]; }
    DWORD wait = minimum(maximum, range_next_wait_ms(h->ranges));
    wait = minimum(wait, media_source_next_wait_ms(h->media));
    if (h->ap) {
        HANDLE ap[5]; count = ap_wait_handles(h->ap, ap, 5);
        for (size_t i = 0; i < count; ++i) {
            bool duplicate = false;
            for (DWORD j = 0; j < n; ++j) { duplicate |= ap[i] == handles[j]; }
            if (!duplicate) { handles[n++] = ap[i]; }
        }
        wait = minimum(wait, ap_next_wait_ms(h->ap)); wait = minimum(wait, audio_key_next_wait_ms(h->keys));
        wait = minimum(wait, token_next_wait_ms(h->tokens));
    }
    if (WaitForMultipleObjects(n, handles, FALSE, wait) == WAIT_FAILED) { atomic_store(&h->failed, true); }
    atomic_fetch_add(&h->wakes, 1);
}
bool hl_network_close(Headless *h)
{
    bool ok = true;
    if (h->ranges) {
        RangeStats stats = range_stats(h->ranges);
        h->final.network_attempts = stats.network_attempts; h->final.cache_hits = stats.cache_hits;
        uint64_t until = GetTickCount64() + 15000;
        while (range_fetcher_shutdown(h->ranges) == RANGE_PENDING) {
            if (GetTickCount64() >= until) { return false; }
            (void)http_wait(h->http, 10);
        }
        ok &= range_stats(h->ranges).leases == 0;
    }
    if (h->cache) {
        uint64_t until = GetTickCount64() + 15000;
        CacheResult r;
        while ((r = audio_cache_shutdown(h->cache)) == CACHE_PENDING) {
            if (GetTickCount64() >= until) { return false; }
            HANDLE event = audio_cache_wait_handle(h->cache);
            if (event) { (void)WaitForSingleObject(event, 10); }
        }
        ok &= r == CACHE_OK;
    }
    if (h->disk) {
        if (worker_stop(h->disk) != CORE_OK || worker_join(h->disk, 15000) != CORE_OK) { return false; }
    }
    if (h->media) {
        uint64_t until = GetTickCount64() + 15000;
        while (media_resolver_shutdown(h->media) == MEDIA_PENDING) {
            if (GetTickCount64() >= until) { return false; }
            (void)http_wait(h->http, 10);
        }
        ok &= media_resolver_balanced(h->media);
    }
    ok &= hl_live_close(h);
    if (h->http && http_client_shutdown(h->http, 15000) != HTTP_OK) { return false; }
    for (unsigned i = 0; i < HL_ARENAS; ++i) { arena_dispose(&h->arenas[i]); }
    ok &= metrics_balanced(&h->accounting);
    h->resources_released = true;
    if (!ok) { MetricsSnapshot s = metrics_snapshot(&h->accounting);
        fprintf(stderr,"headless close ledger handles=%llu arenas=%llu errors=%llu\n",(unsigned long long)s.handles,
            (unsigned long long)s.arena_count,(unsigned long long)s.errors); }
    return ok;
}
