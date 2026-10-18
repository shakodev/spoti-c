#include "connect_test.h"
#include "protocol/dealer_internal.h"

static _Alignas(16) unsigned char core_memory[100000], protocol_memory[256 * 1024];
static unsigned char input[DEALER_POOL_BYTES], output[DEALER_POOL_BYTES];
static tinfl_decompressor inflater;
void dealer_winhttp_tests(void);
static void payloads(void)
{
    size_t n = connect_fixture("hello.gz", input, sizeof(input)), written = 99;
    CHECK(dealer_gzip(&inflater, input, n, output, DEALER_PAYLOAD_CAP, &written) == DEALER_OK);
    CHECK(written == 12 && !memcmp(output, "hello dealer", 12));
    for (size_t i = 0; i < n; ++i) { CHECK(dealer_gzip(&inflater, input, i, output, DEALER_PAYLOAD_CAP, &written) != DEALER_OK); }
    input[n - 8] ^= 1; CHECK(dealer_gzip(&inflater, input, n, output, DEALER_PAYLOAD_CAP, &written) == DEALER_MALFORMED); input[n - 8] ^= 1;
    input[n - 4] ^= 1; CHECK(dealer_gzip(&inflater, input, n, output, DEALER_PAYLOAD_CAP, &written) == DEALER_MALFORMED); input[n - 4] ^= 1;
    input[3] = 0xe0; CHECK(dealer_gzip(&inflater, input, n, output, DEALER_PAYLOAD_CAP, &written) == DEALER_MALFORMED); input[3] = 0;
    input[n] = 0; CHECK(dealer_gzip(&inflater, input, n + 1, output, DEALER_PAYLOAD_CAP, &written) == DEALER_MALFORMED);
    CHECK(dealer_gzip(&inflater, input, n, output, 3, &written) == DEALER_LIMIT);
    memcpy(input + n, input, n); CHECK(dealer_gzip(&inflater, input, n * 2, output, DEALER_PAYLOAD_CAP, &written) == DEALER_MALFORMED);
    n = connect_fixture("optional.gz", input, sizeof(input));
    CHECK(dealer_gzip(&inflater, input, n, output, DEALER_PAYLOAD_CAP, &written) == DEALER_OK && written == 12);
    for (size_t i = 0; i < n; ++i) { CHECK(dealer_gzip(&inflater, input, i, output, DEALER_PAYLOAD_CAP, &written) != DEALER_OK); }
    input[15] ^= 1; CHECK(dealer_gzip(&inflater, input, n, output, DEALER_PAYLOAD_CAP, &written) == DEALER_MALFORMED);
    n = connect_fixture("stored.gz", input, sizeof(input));
    CHECK(dealer_gzip(&inflater, input, n, output, DEALER_PAYLOAD_CAP, &written) == DEALER_OK && written == 12);
    n = connect_fixture("expansion.gz", input, sizeof(input));
    CHECK(dealer_gzip(&inflater, input, n, output, DEALER_PAYLOAD_CAP, &written) == DEALER_LIMIT);
    /* Forging ISIZE cannot bypass the actual inflate expansion bound. */
    memset(input + n - 4, 0, 4);
    CHECK(dealer_gzip(&inflater, input, n, output, DEALER_PAYLOAD_CAP, &written) == DEALER_LIMIT);
    n = connect_fixture("dynamic.gz", input, sizeof(input));
    CHECK(dealer_gzip(&inflater, input, n, output, DEALER_PAYLOAD_CAP, &written) == DEALER_OK && written == 61440);
    for (size_t i = 0; i < written; ++i) { CHECK(output[i] == (unsigned char)i); }
    const char *bad[] = {"=AAA", "A===", "AB==", "AAB=", "AAAA====", "AA A", "!AAA", "AAA"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) { CHECK(dealer_base64(bad[i], strlen(bad[i]), output, sizeof(output), &written) == DEALER_MALFORMED); }
    CHECK(dealer_base64("aGVsbG8=", 8, output, 3, &written) == DEALER_LIMIT);
    CHECK(dealer_base64("aGVsbG8=", 8, output, sizeof(output), &written) == DEALER_OK && written == 5 && !memcmp(output, "hello", 5));
}
/* The transport lifecycle tests exercise the same pump used by WinHTTP. */
typedef struct Fake {
    unsigned opens, closes, sends; bool ready, written, fail, close_pending, have_frame;
    uint64_t now, expiry; DealerFrame frame;
} Fake;
static Fake fake;
static DealerResult fake_open(void *ctx, const ApEndpoint *e, const wchar_t *path)
{
    Fake *f = ctx; CHECK(e->port == 443 && !wcscmp(path, L"/?access_token=fixture%2Btoken")); ++f->opens; f->ready = true; return DEALER_PENDING;
}
static DealerResult fake_poll(void *ctx, DealerFrame *frame)
{
    Fake *f = ctx; if (f->fail) { f->fail = false; return DEALER_IO; }
    if (f->ready) { f->ready = false; *frame = (DealerFrame){.type = DEALER_READY}; return DEALER_OK; }
    if (f->written) { f->written = false; *frame = (DealerFrame){.type = DEALER_WRITTEN}; return DEALER_OK; }
    if (f->have_frame) { f->have_frame = false; *frame = f->frame; return DEALER_OK; }
    return DEALER_PENDING;
}
static DealerResult fake_send(void *ctx, const void *data, size_t n)
{
    Fake *f = ctx; CHECK(data && n); ++f->sends; f->written = true; return DEALER_OK;
}
static DealerResult fake_close(void *ctx) { Fake *f = ctx; ++f->closes; return f->close_pending ? DEALER_PENDING : DEALER_OK; }
static HANDLE fake_event(void *ctx) { (void)ctx; return NULL; }
static uint64_t now(void *ctx) { return ((Fake *)ctx)->now; }
static DealerResult token(void *ctx, wchar_t *out, size_t cap, uint64_t *expiry)
{
    Fake *f = ctx; CHECK(!wcscpy_s(out, cap, L"fixture+token")); *expiry = f->expiry; return DEALER_OK;
}
static const DealerTransport transport = {fake_open, fake_poll, fake_send, fake_close, fake_event};
static void replay(void)
{
    Arena core, protocol; pt_open(); CHECK(arena_init(&core, core_memory, sizeof(core_memory)));
    CHECK(arena_init(&protocol, protocol_memory, sizeof(protocol_memory)));
    ConnectConfig cfg = {.player = pt.player, .device_id = "fixture-device", .name = "Fixture", .client_id = "fixture-client", .fixture_replay = true};
    ConnectState *state; CHECK(connect_create(&core, &cfg, &state) == DEALER_OK);
    fake = (Fake){.expiry = 500000};
    DealerConfig dc = {.connect = state, .clock = {now, &fake}, .endpoint = {.host = "dealer.spotify.com", .port = 443},
        .transport = &transport, .transport_context = &fake, .fixture_token = token, .fixture_context = &fake};
    DealerClient *d; CHECK(dealer_create(&core, &protocol, &dc, &d) == DEALER_OK);
    CHECK(dealer_pump(d) == DEALER_PENDING); CHECK(dealer_pump(d) == DEALER_OK);
    char reply[2048]; size_t written; size_t n = connect_fixture("connection.json", input, sizeof(input));
    CHECK(dealer_frame(d, input, n, true, reply, sizeof(reply), &written) == DEALER_OK && !written);
    n = connect_fixture("transfer.dealer.json", input, sizeof(input));
    for (size_t i = 0; i < n; ++i) {
        CHECK(dealer_frame(d, input + i, 1, i + 1 == n, reply, sizeof(reply), &written) == (i + 1 == n ? DEALER_OK : DEALER_PENDING));
    }
    CHECK(strstr(reply, "\"success\":true")); pt_drive(); CHECK(player_snapshot(pt.player).state == PLAYER_PAUSED);
    uint64_t generation = player_snapshot(pt.player).generation;
    pt_heap_begin();
    CHECK(dealer_frame(d, input, n, true, reply, sizeof(reply), &written) == DEALER_OK); pt_drive();
    pt_heap_end();
    CHECK(player_snapshot(pt.player).generation == generation);
    fake.have_frame = true; fake.frame = (DealerFrame){.type = DEALER_TEXT_END, .data = input, .bytes = n};
    CHECK(dealer_pump(d) == DEALER_OK && fake.sends == 1); CHECK(dealer_pump(d) == DEALER_OK); pt_drive();
    CHECK(player_snapshot(pt.player).generation == generation);
    n = connect_fixture("unknown.dealer.json", input, sizeof(input));
    CHECK(dealer_frame(d, input, n, true, reply, sizeof(reply), &written) == DEALER_UNSUPPORTED && strstr(reply, "\"success\":false"));
    CHECK(dealer_frame(d, "{", 1, true, reply, sizeof(reply), &written) == DEALER_MALFORMED);
    memset(input, ' ', DEALER_PAYLOAD_CAP);
    CHECK(dealer_frame(d, input, DEALER_PAYLOAD_CAP, false, reply, sizeof(reply), &written) == DEALER_PENDING);
    CHECK(dealer_frame(d, input, 1, true, reply, sizeof(reply), &written) == DEALER_LIMIT);
    fake.now = 90001; CHECK(dealer_pump(d) == DEALER_IO); CHECK(dealer_snapshot(d).phase == DEALER_RETIRING);
    fake.close_pending = true; CHECK(dealer_pump(d) == DEALER_PENDING);
    fake.now += 3001; CHECK(dealer_pump(d) == DEALER_PENDING); CHECK(dealer_next_wait_ms(d) == INFINITE);
    fake.close_pending = false; CHECK(dealer_pump(d) == DEALER_PENDING); fake.now += 10000;
    CHECK(dealer_pump(d) == DEALER_PENDING); CHECK(dealer_pump(d) == DEALER_PENDING); CHECK(dealer_pump(d) == DEALER_OK);
    CHECK(fake.opens == 2 && dealer_snapshot(d).reconnects == 1);
    fake.expiry = fake.now; fake.now = 500001; CHECK(dealer_pump(d) == DEALER_AUTH);
    CHECK(dealer_shutdown(d) == DEALER_CLOSED); CHECK(connect_shutdown(state) == DEALER_OK);
    CHECK(arena_reset(&core) && arena_reset(&protocol));
    CHECK(connect_create(&core, &cfg, &state) == DEALER_OK); dc.connect = state; fake = (Fake){.expiry = 500000};
    CHECK(dealer_create(&core, &protocol, &dc, &d) == DEALER_OK);
    CHECK(dealer_pump(d) == DEALER_PENDING && dealer_pump(d) == DEALER_OK);
    n = connect_fixture("logout.dealer.json", input, sizeof(input));
    fake.have_frame = true; fake.close_pending = true;
    fake.frame = (DealerFrame){.type = DEALER_TEXT_END, .data = input, .bytes = n};
    CHECK(dealer_pump(d) == DEALER_PENDING && connect_snapshot(state).logged_out);
    CHECK(dealer_shutdown(d) == DEALER_PENDING); fake.close_pending = false;
    CHECK(dealer_shutdown(d) == DEALER_CLOSED && dealer_snapshot(d).phase == DEALER_STOPPED && fake.opens == 1);
    pt_drive(); CHECK(player_snapshot(pt.player).state == PLAYER_STOPPED);
    arena_dispose(&protocol); arena_dispose(&core); pt_close();
}
static DWORD WINAPI run(void *context)
{
    (void)context; DWORD before, after; CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    pt_load_fixtures(); payloads(); replay();
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after) && before == after);
    dealer_winhttp_tests();
    puts("Dealer bounded payload and transport lifecycle tests passed"); return 0;
}
int main(void)
{
    HANDLE thread = CreateThread(NULL, 0, run, NULL, 0, NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread, 80000) == WAIT_OBJECT_0); CloseHandle(thread); return 0;
}
