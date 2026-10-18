#include "dealer_internal.h"
#include <string.h>

DealerResult dealer_credentials(DealerClient *d)
{
    wchar_t token[TOKEN_TEXT_CAP]; const wchar_t *value = token; DealerResult result;
    if (d->config.fixture_token) {
        result = d->config.fixture_token(d->config.fixture_context, token, TOKEN_TEXT_CAP, &d->expires);
        if (result != DEALER_OK) { return result; }
    } else {
        (void)token_poll(d->config.tokens);
        TokenResult r = token_authorize(d->config.tokens, d->origin, &d->authorization);
        if (r != TOKEN_OK) { return r == TOKEN_PENDING || r == TOKEN_FULL ? DEALER_PENDING : DEALER_AUTH; }
        value = d->authorization.bearer + 7;
        d->expires = token_info(d->config.tokens, TOKEN_BEARER).refresh_at;
    }
    size_t n = wcsnlen_s(value, TOKEN_TEXT_CAP), at = 15;
    if (!n || n == TOKEN_TEXT_CAP || d->expires <= core_clock_now(&d->config.clock)) { result = DEALER_AUTH; goto done; }
    wcscpy_s(d->path, 4096, L"/?access_token="); at = wcslen(d->path);
    static const wchar_t hex[] = L"0123456789ABCDEF";
    for (size_t i = 0; i < n; ++i) {
        wchar_t c = value[i]; if (c <= 32 || c >= 127 || at + 3 >= 4096) { result = DEALER_AUTH; goto done; }
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_' || c == L'.' || c == L'~') {
            d->path[at++] = c;
        } else { d->path[at++] = L'%'; d->path[at++] = hex[(unsigned)c >> 4]; d->path[at++] = hex[c & 15]; }
    }
    d->path[at] = 0; result = DEALER_OK;
done:
    SecureZeroMemory(token, sizeof(token)); return result;
}
DealerResult dealer_pump(DealerClient *d)
{
    if (!d) { return DEALER_INVALID; }
    uint64_t now = core_clock_now(&d->config.clock); const DealerTransport *t = d->config.transport; void *ctx = d->config.transport_context;
    if (d->view.phase == DEALER_STOPPED) { return DEALER_CLOSED; }
    if (d->view.phase == DEALER_RETIRING) {
        DealerResult state = DEALER_OK;
        if (d->stopping) {
            state = connect_snapshot(d->config.connect).logged_out ? connect_poll(d->config.connect) : connect_shutdown(d->config.connect);
        }
        DealerResult r = d->opened ? t->close(ctx) : DEALER_OK;
        if (r == DEALER_OK) { d->opened = false; }
        if (r != DEALER_OK || state == DEALER_PENDING) { return DEALER_PENDING; }
        if (d->stopping) { d->view.phase = DEALER_STOPPED; return DEALER_CLOSED; }
        d->view.phase = DEALER_BACKOFF; d->deadline = core_deadline_after(now, 10000); return DEALER_PENDING;
    }
    if (d->view.phase == DEALER_BACKOFF) {
        if (!core_deadline_expired(now, d->deadline)) { return DEALER_PENDING; }
        d->view.phase = DEALER_AUTHORIZING; ++d->view.reconnects; return DEALER_PENDING;
    }
    if (d->view.phase == DEALER_AUTHORIZING) {
        DealerResult r = dealer_credentials(d);
        if (r == DEALER_PENDING) { return r; }
        if (r != DEALER_OK) { dealer_disconnect(d, r); return r; }
        d->opened = true; r = t->open(ctx, &d->config.endpoint, d->path); SecureZeroMemory(d->path, sizeof(d->path));
        if (r != DEALER_OK && r != DEALER_PENDING) { dealer_disconnect(d, r); return r; }
        d->view.phase = DEALER_CONNECTING; d->deadline = core_deadline_after(now, 15000); return DEALER_PENDING;
    }
    if (now >= d->expires) { dealer_disconnect(d, DEALER_AUTH); return DEALER_AUTH; }
    if (core_deadline_expired(now, d->deadline)) { dealer_disconnect(d, DEALER_IO); return DEALER_IO; }
    DealerFrame frame = {0}; DealerResult r = t->poll(ctx, &frame);
    if (r != DEALER_OK && r != DEALER_PENDING) {
        if (r == DEALER_AUTH && (frame.status == 403 || ++d->unauthorized > 1)) { d->stopping = true; }
        if (r == DEALER_AUTH && d->config.tokens && d->authorization.issued) {
            (void)token_report_status(d->config.tokens, &d->authorization, frame.status);
        }
        dealer_disconnect(d, r); return r;
    }
    if (r == DEALER_OK) {
        if (frame.type == DEALER_READY) { d->view.phase = DEALER_ONLINE; d->unauthorized = 0; d->deadline = core_deadline_after(now, 90000); }
        else if (frame.type == DEALER_WRITTEN) { d->writing = false; d->deadline = core_deadline_after(now, 90000); }
        else if (frame.type == DEALER_BINARY || frame.type == DEALER_PEER_CLOSE || d->writing) { dealer_disconnect(d, DEALER_IO); return DEALER_IO; }
        else {
            char reply[2048]; size_t bytes;
            r = dealer_frame(d, frame.data, frame.bytes, frame.type == DEALER_TEXT_END, reply, sizeof(reply), &bytes);
            if (r == DEALER_CLOSED) {
                d->stopping = true; dealer_disconnect(d, r); return DEALER_PENDING;
            }
            if (bytes) {
                DealerResult sent = t->send(ctx, reply, bytes);
                if (sent != DEALER_OK) { dealer_disconnect(d, sent); return sent; }
                d->writing = true; d->deadline = core_deadline_after(now, 10000);
            } else if (r == DEALER_OK || r == DEALER_UNSUPPORTED || r == DEALER_STALE) {
                d->deadline = core_deadline_after(now, 90000);
            } else if (r != DEALER_PENDING) { dealer_disconnect(d, r); return r; }
        }
    }
    DealerResult state = connect_poll(d->config.connect);
    if (state == DEALER_CLOSED) { d->stopping = true; dealer_disconnect(d, state); return DEALER_PENDING; }
    if (state != DEALER_OK && state != DEALER_PENDING && state != DEALER_FULL) { dealer_disconnect(d, state); return state; }
    return r;
}
