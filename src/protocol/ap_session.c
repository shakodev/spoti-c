#include "ap_internal.h"
#include "api/json_base.h"
#include <string.h>

ApState ap_state(const ApSession *s) { return s == NULL ? AP_STATE_CLOSED : s->state; }
ApResult ap_result(const ApSession *s) { return s == NULL ? AP_INVALID : s->result; }
uint32_t ap_generation(const ApSession *s) { return s == NULL ? 0 : s->generation; }

ApResult ap_session_create(Arena *arena, const ApConfig *config, ApSession **output)
{
    if (output == NULL) { return AP_INVALID; }
    *output = NULL;
    if (arena == NULL || arena->capacity > MEMORY_BUDGET_PROTOCOL || config == NULL ||
        config->version == 0 || config->max_attempts > AP_ENDPOINT_COUNT) { return AP_INVALID; }
    bool native = config->transport.begin == NULL;
    if ((!native && (config->transport.poll == NULL || config->transport.read == NULL ||
         config->transport.write == NULL || config->transport.stop == NULL || config->transport.handles == NULL)) ||
        (native && (config->transport.poll != NULL || config->transport.read != NULL ||
         config->transport.write != NULL || config->transport.stop != NULL || config->transport.handles != NULL))) {
        return AP_INVALID;
    }
    ArenaMark mark = arena_mark(arena);
    ApSession *s = arena_alloc(arena, sizeof(*s), _Alignof(ApSession));
    if (s == NULL) { return AP_NO_MEMORY; }
    *s = (ApSession){0}; s->config = *config; s->native_io = native;
    if (s->config.max_attempts == 0) { s->config.max_attempts = 3; }
    if (s->config.connect_timeout_ms == 0) { s->config.connect_timeout_ms = 5000; }
    if (s->config.handshake_timeout_ms == 0) { s->config.handshake_timeout_ms = 10000; }
    if (s->config.auth_timeout_ms == 0) { s->config.auth_timeout_ms = 10000; }
    if (native) {
        ApResult result = ap_socket_create(&s->socket, config->metrics);
        if (result != AP_OK) { (void)arena_rewind(arena, mark); return result; }
        s->io = ap_socket_transport(&s->socket);
    } else { s->io = config->transport; }
    crypto_init(&s->crypto);
    s->state = AP_STATE_CLOSED;
    s->deadline = s->frame_deadline = s->keepalive_at = s->tx_deadline = CORE_CLOCK_NEVER;
    *output = s; return AP_OK;
}

ApResult ap_retire(ApSession *s)
{
    if (s->http_active) {
        HttpResult pumped = http_pump(s->config.http);
        if (pumped != HTTP_OK) { return AP_IO_ERROR; }
        HttpCompletion completion;
        HttpResult result = http_consume(s->config.http, s->resolve_id, &completion);
        if (result == HTTP_OK) { s->http_active = false; }
        else if (result != HTTP_PENDING) { return AP_IO_ERROR; }
    }
    if (s->io_stopping) {
        ApResult result = s->io.stop(s->io.context);
        if (result == AP_OK) { s->io_stopping = false; }
        else if (result != AP_PENDING) { return result; }
    }
    return s->http_active || s->io_stopping ? AP_PENDING : AP_OK;
}

ApResult ap_close(ApSession *s, ApResult result, bool retry)
{
    s->result = result;
    s->state = retry && s->attempts < s->config.max_attempts ? AP_STATE_BACKOFF : AP_STATE_CLOSED;
    s->deadline = s->state == AP_STATE_BACKOFF ? core_deadline_after(core_clock_now(&s->config.clock),
        core_backoff_ms(250, 2000, s->attempts == 0 ? 0 : s->attempts - 1)) : CORE_CLOCK_NEVER;
    s->frame_deadline = s->keepalive_at = s->tx_deadline = CORE_CLOCK_NEVER;
    s->io_ready = s->pong_pending = s->pong_queued = false;
    if (s->http_active) { (void)http_cancel(s->config.http, s->resolve_id); }
    ApResult stopped = s->io.stop(s->io.context);
    s->io_stopping = stopped != AP_OK;
    ap_packet_clear(&s->packet); ap_handshake_clear(&s->handshake);
    SecureZeroMemory(s->nonce, sizeof(s->nonce));
    if (crypto_dh_clear(&s->dh) != CRYPTO_OK) { s->state = AP_STATE_CLOSED; s->result = AP_IO_ERROR; }
    return result;
}

ApResult ap_connect(ApSession *s)
{
    if (s->attempts >= s->config.max_attempts || s->endpoints.accesspoint_count == 0) {
        return ap_close(s, AP_IO_ERROR, false);
    }
    size_t index = s->attempts % s->endpoints.accesspoint_count;
    ++s->attempts; s->login_sent = false;
    s->state = AP_STATE_CONNECT; s->result = AP_PENDING;
    s->deadline = core_deadline_after(core_clock_now(&s->config.clock), s->config.connect_timeout_ms);
    if (crypto_dh_init(&s->dh, &s->crypto) != CRYPTO_OK ||
        crypto_random(&s->crypto, s->nonce, sizeof(s->nonce)) != CRYPTO_OK) { return ap_close(s, AP_IO_ERROR, false); }
    ApResult result = s->io.begin(s->io.context, &s->endpoints.accesspoint[index]);
    s->io_ready = result == AP_OK;
    if (result != AP_OK && result != AP_PENDING) { return ap_close(s, result, true); }
    return AP_OK;
}

static bool endpoints_valid(const ApEndpoints *endpoints)
{
    if (endpoints->accesspoint_count == 0 || endpoints->accesspoint_count > AP_ENDPOINT_COUNT ||
        endpoints->dealer_count > AP_ENDPOINT_COUNT || endpoints->spclient_count > AP_ENDPOINT_COUNT) { return false; }
    for (size_t i = 0; i < endpoints->accesspoint_count; ++i) {
        if (!ap_endpoint_valid(&endpoints->accesspoint[i])) { return false; }
    }
    for (size_t i = 0; i < endpoints->dealer_count; ++i) { if (!ap_endpoint_valid(&endpoints->dealer[i])) { return false; } }
    for (size_t i = 0; i < endpoints->spclient_count; ++i) { if (!ap_endpoint_valid(&endpoints->spclient[i])) { return false; } }
    return true;
}

ApResult ap_start(ApSession *s, const ApEndpoints *resolved)
{
    if (s == NULL) { return AP_INVALID; }
    if (s->destroyed || s->shutdown_started) { return AP_CLOSED; }
    if (s->state != AP_STATE_CLOSED || s->http_active || s->io_stopping || s->in_event) { return AP_BAD_STATE; }
    if (s->generation == UINT32_MAX) { return AP_EXHAUSTED; }
    if (resolved != NULL && !endpoints_valid(resolved)) { return AP_INVALID; }
    ++s->generation; s->attempts = 0; s->result = AP_PENDING;
    s->frame_deadline = s->keepalive_at = s->tx_deadline = CORE_CLOCK_NEVER;
    if (resolved != NULL) { s->endpoints = *resolved; return ap_connect(s); }
    if (s->config.http == NULL) { s->result = AP_INVALID; return AP_INVALID; }
    s->state = AP_STATE_RESOLVE;
    s->deadline = core_deadline_after(core_clock_now(&s->config.clock), 10000);
    ApResult result = s->config.resolver_url != NULL ?
        ap_resolve_start_at(s->config.http, s->config.resolver_url, s->handshake.transcript, JSON_BASE_INPUT_LIMIT, &s->resolve_id) :
        ap_resolve_start(s->config.http, s->handshake.transcript, JSON_BASE_INPUT_LIMIT, &s->resolve_id);
    if (result != AP_OK) { return ap_close(s, result, false); }
    s->http_active = true; return AP_OK;
}

ApResult ap_resolve_event(ApSession *s)
{
    if (http_pump(s->config.http) != HTTP_OK) { return ap_close(s, AP_IO_ERROR, false); }
    HttpCompletion completion;
    HttpResult result = http_consume(s->config.http, s->resolve_id, &completion);
    if (result == HTTP_PENDING) { return AP_PENDING; }
    s->http_active = false;
    if (result != HTTP_OK) { return ap_close(s, AP_IO_ERROR, false); }
    if (completion.result != HTTP_OK || !completion.final) {
        return ap_close(s, completion.result == HTTP_TIMEOUT ? AP_TIMEOUT : AP_IO_ERROR, false);
    }
    ApResult parsed = ap_resolve_parse((char *)s->handshake.transcript, completion.bytes,
        JSON_BASE_INPUT_LIMIT + YYJSON_PADDING_SIZE, s->packet.rx, sizeof(s->packet.rx), &s->endpoints);
    ap_packet_clear(&s->packet); ap_handshake_clear(&s->handshake);
    if (parsed != AP_OK) { return ap_close(s, parsed, false); }
    return ap_connect(s);
}

ApResult ap_on_event(ApSession *s, ApEvent event)
{
    if (s == NULL || event.type > AP_EVENT_CANCEL || (int)event.type < 0) { return AP_INVALID; }
    if (s->destroyed) { return AP_CLOSED; }
    if (event.generation != s->generation) { return AP_STALE; }
    if (s->in_event) { return AP_BAD_STATE; }
    s->in_event = true;
    ApResult result = AP_OK;
    if (event.type == AP_EVENT_CANCEL) { result = ap_close(s, AP_CANCELLED, false); }
    else { result = ap_timers(s); }
    if (s->state == AP_STATE_BACKOFF || s->state == AP_STATE_CLOSED) {
        ApResult retired = ap_retire(s);
        if (retired != AP_OK && retired != AP_PENDING) { result = retired; }
        else if (retired == AP_PENDING && result == AP_OK) { result = AP_PENDING; }
        if (s->state == AP_STATE_BACKOFF && retired == AP_OK &&
            core_deadline_expired(core_clock_now(&s->config.clock), s->deadline)) { result = ap_connect(s); }
    } else if (s->state == AP_STATE_RESOLVE && event.type == AP_EVENT_HTTP) { result = ap_resolve_event(s); }
    else if (event.type == AP_EVENT_SOCKET || s->io_ready) {
        if (s->state == AP_STATE_CONNECT) {
            ApResult connected = s->io_ready ? AP_OK : s->io.poll(s->io.context);
            s->io_ready = false;
            if (connected == AP_OK) {
                result = ap_handshake_begin(&s->handshake, &s->dh, s->nonce, s->config.version);
                if (result == AP_OK) {
                    s->state = AP_STATE_HELLO; s->tx_offset = 0;
                    s->deadline = core_deadline_after(core_clock_now(&s->config.clock), s->config.handshake_timeout_ms);
                    result = ap_drive_io(s);
                } else { result = ap_close(s, result, false); }
            } else if (connected != AP_PENDING) { result = ap_close(s, connected, true); }
        } else if (s->state != AP_STATE_RESOLVE) {
            ApResult polled = s->io.poll(s->io.context);
            result = polled == AP_OK ? ap_drive_io(s) : polled == AP_PENDING ? AP_PENDING : ap_close(s, polled, true);
        }
    } else if (s->state == AP_STATE_AUTHENTICATED || s->state == AP_STATE_FRAMED) { result = ap_flush_tx(s); }
    s->in_event = false; return result;
}

uint32_t ap_next_wait_ms(const ApSession *s)
{
    if (s == NULL || s->destroyed) { return INFINITE; }
    if (s->io_ready || (s->pong_pending && s->packet.tx_bytes == 0)) { return 0; }
    uint64_t deadline = s->deadline;
    if (s->state == AP_STATE_BACKOFF && (s->http_active || s->io_stopping)) { deadline = CORE_CLOCK_NEVER; }
    if (s->frame_deadline < deadline) { deadline = s->frame_deadline; }
    if (s->keepalive_at < deadline) { deadline = s->keepalive_at; }
    if (s->tx_deadline < deadline) { deadline = s->tx_deadline; }
    return core_deadline_wait(core_clock_now(&s->config.clock), deadline);
}

size_t ap_wait_handles(ApSession *s, HANDLE *handles, size_t capacity)
{
    if (s == NULL || s->destroyed || handles == NULL || capacity == 0) { return 0; }
    size_t count = 0;
    if (s->http_active) { handles[count++] = http_wait_handle(s->config.http); }
    if (count < capacity) { count += s->io.handles(s->io.context, handles + count, capacity - count); }
    return count;
}

ApResult ap_session_shutdown(ApSession *s)
{
    if (s == NULL) { return AP_INVALID; }
    if (s->destroyed) { return AP_CLOSED; }
    if (s->in_event) { return AP_BAD_STATE; }
    s->shutdown_started = true;
    if (s->state != AP_STATE_CLOSED) { (void)ap_close(s, AP_CANCELLED, false); }
    ApResult result = ap_retire(s);
    if (result != AP_OK) { return result; }
    if (crypto_dh_clear(&s->dh) != CRYPTO_OK) { return AP_IO_ERROR; }
    if (s->native_io && s->socket.initialized) {
        result = ap_socket_destroy(&s->socket); if (result != AP_OK) { return result; }
    }
    if (!crypto_balanced(&s->crypto)) { return AP_IO_ERROR; }
    s->destroyed = true; return AP_OK;
}
