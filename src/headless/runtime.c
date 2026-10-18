#include "internal.h"
#include <wchar.h>

static DWORD WINAPI network(void *ctx)
{
    Headless *h = ctx; bool ok = hl_network_open(h);
    if (ok) {
        h->decode_thread = CreateThread(NULL, 0, hl_decode, h, 0, NULL);
        ok = h->decode_thread && WaitForSingleObject(h->decode_ready, INFINITE) == WAIT_OBJECT_0 && !atomic_load(&h->failed);
        if (ok) { ResetEvent(h->decode_ready); }
    }
    if (!ok) { atomic_store(&h->failed, true); }
    SetEvent(h->ready);
    if (ok) {
        while (!atomic_load_explicit(&h->decode_finished, memory_order_acquire)) {
            ResetEvent(h->read_wake);
            if (!hl_network_poll(h)) {
                atomic_store(&h->failed, true);
                (void)player_submit(h->player, &(PlayerCommand){.type = PLAYER_SHUTDOWN});
            }
            if (!atomic_load_explicit(&h->decode_finished, memory_order_acquire)) { hl_network_wait(h, INFINITE); }
        }
        SetEvent(h->decode_ready); /* Borrowed player wait handles are now quiescent. */
    }
    if (h->decode_thread) {
        if (WaitForSingleObject(h->decode_thread, 15000) != WAIT_OBJECT_0) { atomic_store(&h->failed,true); return 1; }
        CloseHandle(h->decode_thread); h->decode_thread = NULL;
    }
    ok &= hl_network_close(h); h->final.balanced = ok;
    if (!ok) { atomic_store(&h->failed, true); }
    return ok ? 0 : 1;
}
bool headless_open(const HeadlessConfig *config, Headless **out)
{
    if (!out) { return false; } *out = NULL;
    if (!config || !config->cache_directory || wcslen(config->cache_directory) >= 240 ||
        (config->fixture_url && strlen(config->fixture_url) >= MEDIA_URL_BYTES) ||
        (!config->fixture_url && (!config->credential_path || !config->country || !config->catalogue)) ||
        (config->credential_path && wcslen(config->credential_path) >= 240) ||
        (config->country && strlen(config->country) != 2) || (config->catalogue && strlen(config->catalogue) >= 33)) { return false; }
    /* COM can initialize USER32 on its first caller. For the opt-in device
     * path, resolve it here before creating the non-GUI decode owner. */
    if (config->wasapi) { (void)IsGUIThread(FALSE); }
    Headless *h = VirtualAlloc(NULL, sizeof(*h), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!h) { return false; }
    if (!metrics_init(&h->accounting)) { VirtualFree(h,0,MEM_RELEASE); return false; }
    atomic_init(&h->failed,false); atomic_init(&h->closing,false); atomic_init(&h->decode_finished,false);
    atomic_init(&h->frames,0); atomic_init(&h->wakes,0); atomic_init(&h->callback_ticks,0);
    atomic_init(&h->callbacks,0); atomic_init(&h->underruns,0);
    atomic_init(&h->network_attempts,0); atomic_init(&h->cache_hits,0);
    for (unsigned i = 0; i < 2; ++i) { atomic_init(&h->sources[i].phase,HL_IDLE); }
    h->config = *config; InitializeSRWLock(&h->lifecycle); QueryPerformanceFrequency(&h->frequency);
    DWORD path_length = GetFullPathNameW(config->cache_directory, 240, h->cache_path, NULL);
    if (!path_length || path_length >= 240) { VirtualFree(h, 0, MEM_RELEASE); return false; }
    if (config->credential_path) { wcscpy_s(h->credential_path, 240, config->credential_path); }
    if (config->fixture_url) { strcpy_s(h->fixture_url, sizeof(h->fixture_url), config->fixture_url); }
    if (config->country) { strcpy_s(h->country, sizeof(h->country), config->country); }
    if (config->catalogue) { strcpy_s(h->catalogue, sizeof(h->catalogue), config->catalogue); }
    h->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    h->decode_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    h->read_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (h->ready && h->decode_ready && h->read_wake) { h->network_thread = CreateThread(NULL, 0, network, h, 0, NULL); }
    if (h->network_thread && WaitForSingleObject(h->ready, INFINITE) == WAIT_OBJECT_0 && !atomic_load(&h->failed)) { *out = h; return true; }
    HeadlessMetrics ignored; (void)headless_close(h, &ignored); return false;
}
PlayerResult headless_submit(Headless *h, const PlayerCommand *c)
{
    if (!h || !c) { return PLAYER_INVALID; }
    AcquireSRWLockShared(&h->lifecycle);
    PlayerResult r = atomic_load(&h->closing) || atomic_load(&h->failed) || atomic_load(&h->decode_finished) ? PLAYER_CLOSED : player_submit(h->player, c);
    ReleaseSRWLockShared(&h->lifecycle); return r;
}
PlayerSnapshot headless_snapshot(Headless *h)
{
    if (!h || atomic_load(&h->failed)) { return (PlayerSnapshot){.state = PLAYER_ERROR}; }
    return player_snapshot(h->player);
}
bool headless_close(Headless *h, HeadlessMetrics *out)
{
    if (!out) { return false; }
    *out = (HeadlessMetrics){0};
    if (!h) { return false; }
    AcquireSRWLockExclusive(&h->lifecycle); atomic_store(&h->closing, true);
    if (h->player && !atomic_load(&h->failed) && !atomic_load(&h->decode_finished)) {
        PlayerResult r;
        do {
            r = player_submit(h->player, &(PlayerCommand){.type = PLAYER_SHUTDOWN});
            if (r == PLAYER_FULL) { Sleep(1); }
        } while (r == PLAYER_FULL);
    }
    ReleaseSRWLockExclusive(&h->lifecycle);
    if (h->network_thread && WaitForSingleObject(h->network_thread, 30000) != WAIT_OBJECT_0) {
        (void)headless_metrics(h, out); return false;
    }
    bool ok = !atomic_load(&h->failed) && h->final.balanced;
    ok &= headless_metrics(h, out); out->balanced = h->final.balanced;
    if (h->network_thread && !h->resources_released) { return false; } /* Retain live dependency storage on timeout. */
    HANDLE handles[] = {h->network_thread,h->read_wake,h->decode_ready,h->ready};
    for (unsigned i = 0; i < 4; ++i) { if (handles[i]) { ok &= CloseHandle(handles[i]) != 0; } }
    /* Arena owners already wiped every byte ever allocated before disposal.
     * PCM is the only fixed payload buffer outside those arenas; scratch and
     * all inline secrets live in the header. Do not fault unused VM capacity
     * into memory merely to overwrite pages which have always been zero. */
    if (h->player) { SecureZeroMemory(h->pcm, sizeof(h->pcm)); }
    SecureZeroMemory(h, offsetof(Headless, core));
    ok &= VirtualFree(h, 0, MEM_RELEASE) != FALSE;
    return headless_process_metrics(out) && ok;
}
