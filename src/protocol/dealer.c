#include "dealer_internal.h"
#include "connect_internal.h"
#include <string.h>

_Static_assert(sizeof(ConnectState) + sizeof(DealerClient) + 32 * MEMORY_KIB <= MEMORY_BUDGET_CORE,
    "Connect, Dealer and bounded player metadata fit the core ledger");

static bool host_allowed(const ApEndpoint *e)
{
    size_t n = strnlen_s(e->host, sizeof(e->host));
    return n > 12 && n < sizeof(e->host) && e->port == 443 &&
        ((!strcmp(e->host + n - 12, ".spotify.com")) || (!strcmp(e->host + n - 12, ".spotify.net")));
}
DealerResult dealer_create(Arena *core, Arena *protocol, const DealerConfig *cfg, DealerClient **out)
{
    if (!out) { return DEALER_INVALID; } *out = NULL;
    if (!core || !protocol || !cfg || !cfg->connect || !host_allowed(&cfg->endpoint) ||
        core == protocol || core->capacity > MEMORY_BUDGET_CORE || protocol->capacity > MEMORY_BUDGET_PROTOCOL ||
        (!cfg->tokens && !cfg->fixture_token) || (cfg->fixture_token && !cfg->transport)) { return DEALER_INVALID; }
    const DealerTransport *t = cfg->transport ? cfg->transport : &dealer_native_transport;
    uintptr_t a_begin = (uintptr_t)core->base, b_begin = (uintptr_t)protocol->base;
    if (a_begin < b_begin + protocol->capacity && b_begin < a_begin + core->capacity) { return DEALER_INVALID; }
    if (!t->open || !t->poll || !t->send || !t->close || !t->event) { return DEALER_INVALID; }
    ArenaMark a = arena_mark(core), b = arena_mark(protocol);
    DealerClient *d = arena_alloc(core, sizeof(*d), _Alignof(DealerClient));
    unsigned char *wire = arena_alloc(protocol, DEALER_POOL_BYTES, 16), *scratch = arena_alloc(protocol, DEALER_POOL_BYTES, 16);
    if (!d || !wire || !scratch) { (void)arena_rewind(core, a); (void)arena_rewind(protocol, b); return DEALER_FULL; }
    memset(d, 0, sizeof(*d)); d->config = *cfg; d->config.transport = t;
    if (!cfg->transport) { d->config.transport_context = &d->native; }
    d->wire = wire; d->scratch = scratch; d->view.phase = DEALER_AUTHORIZING;
    swprintf_s(d->origin, 300, L"https://%S/", cfg->endpoint.host);
    *out = d; return DEALER_OK;
}
static bool escaped(char *out, size_t cap, size_t *at, const char *key)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; key[i]; ++i) {
        unsigned char c = (unsigned char)key[i];
        if (c < 32) {
            if (*at + 6 >= cap) { return false; }
            memcpy(out + *at, "\\u00", 4); *at += 4; out[(*at)++] = hex[c >> 4]; out[(*at)++] = hex[c & 15];
        } else {
            if (*at + 2 >= cap) { return false; }
            if (c == '"' || c == '\\') { out[(*at)++] = '\\'; } out[(*at)++] = (char)c;
        }
    }
    return true;
}
DealerResult dealer_frame(DealerClient *d, const void *data, size_t bytes, bool final,
    char *reply, size_t capacity, size_t *written)
{
    if (!d || !written || !reply || capacity < 2048 || (!data && bytes)) { return DEALER_INVALID; } *written = 0;
    if (d->stopping) { return DEALER_CLOSED; }
    if (bytes > DEALER_PAYLOAD_CAP - d->view.assembled) { d->view.assembled = 0; return DEALER_LIMIT; }
    if (bytes) { memcpy(d->wire + d->view.assembled, data, bytes); } d->view.assembled += bytes;
    if (!final) { return DEALER_PENDING; }
    size_t n = d->view.assembled; d->view.assembled = 0; DealerEnvelope e;
    DealerResult r = dealer_envelope(d->wire, n, d->scratch, &d->inflater, &e);
    if (r != DEALER_OK) { return r; }
    if (e.request) {
        r = !strcmp(e.uri, "hm://connect-state/v1/player/command") ?
            connect_command(d->config.connect, (char *)e.payload, e.bytes, d->scratch) : DEALER_UNSUPPORTED;
        const char *prefix = "{\"type\":\"reply\",\"key\":\""; size_t at = strlen(prefix); memcpy(reply, prefix, at);
        if (!escaped(reply, capacity, &at, e.key)) { return DEALER_LIMIT; }
        const char *end = r == DEALER_OK ? "\",\"payload\":{\"success\":true}}" : "\",\"payload\":{\"success\":false}}";
        size_t len = strlen(end); if (at + len >= capacity) { return DEALER_LIMIT; }
        memcpy(reply + at, end, len + 1); *written = at + len; return r;
    }
    if (!strncmp(e.uri, "hm://pusher/v1/connections/", 27)) {
        if (!*e.connection_id) { return DEALER_MALFORMED; }
        return connect_connection(d->config.connect, e.connection_id);
    }
    return connect_message(d->config.connect, e.uri, e.payload, e.bytes);
}
void dealer_disconnect(DealerClient *d, DealerResult reason)
{
    d->view.last_error = reason; d->view.phase = DEALER_RETIRING; d->view.assembled = 0; d->writing = false;
    d->deadline = core_deadline_after(core_clock_now(&d->config.clock), 3000);
    (void)connect_connection(d->config.connect, NULL); SecureZeroMemory(d->path, sizeof(d->path));
    token_authorization_clear(&d->authorization);
}
DealerSnapshot dealer_snapshot(DealerClient *d) { return d ? d->view : (DealerSnapshot){.phase = DEALER_STOPPED}; }
HANDLE dealer_wait_handle(DealerClient *d)
{
    if (!d) { return NULL; }
    if (d->view.phase == DEALER_AUTHORIZING && d->config.tokens) { return token_wait_handle(d->config.tokens); }
    return d->config.transport->event(d->config.transport_context);
}
size_t dealer_wait_handles(DealerClient *d, HANDLE out[3])
{
    if (!d || !out) { return 0; }
    HANDLE handles[] = {dealer_wait_handle(d), http_wait_handle(d->config.connect->config.http),
        d->view.phase == DEALER_ONLINE ? player_state_event(d->config.connect->config.player) : NULL};
    size_t n = 0;
    for (unsigned i = 0; i < 3; ++i) {
        bool duplicate = !handles[i]; for (size_t j = 0; j < n; ++j) { duplicate |= out[j] == handles[i]; }
        if (!duplicate) { out[n++] = handles[i]; }
    }
    return n;
}
uint32_t dealer_next_wait_ms(DealerClient *d)
{
    if (!d || d->view.phase == DEALER_STOPPED) { return INFINITE; }
    if (d->view.phase == DEALER_AUTHORIZING) { return d->config.tokens ? token_next_wait_ms(d->config.tokens) : 0; }
    uint64_t deadline = d->deadline;
    if (d->view.phase == DEALER_ONLINE && d->expires < deadline) { deadline = d->expires; }
    if (d->view.phase == DEALER_RETIRING && core_deadline_expired(core_clock_now(&d->config.clock), deadline)) { return INFINITE; }
    uint32_t wait = core_deadline_wait(core_clock_now(&d->config.clock), deadline);
    if (d->config.connect->config.http) {
        uint32_t http = http_next_wait_ms(d->config.connect->config.http); if (wait > http) { wait = http; }
    }
    return wait;
}
DealerResult dealer_shutdown(DealerClient *d)
{
    if (!d) { return DEALER_INVALID; }
    d->stopping = true;
    if (d->view.phase != DEALER_STOPPED && d->view.phase != DEALER_RETIRING) { dealer_disconnect(d, DEALER_CLOSED); }
    return dealer_pump(d);
}
