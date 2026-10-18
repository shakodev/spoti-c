#include "player_test.h"
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static size_t allocations;
static int heap_hook(int kind, void *data, size_t bytes, int use, long request, const unsigned char *file, int line)
{
    (void)data; (void)bytes; (void)use; (void)request; (void)file; (void)line;
    if (kind == _HOOK_ALLOC || kind == _HOOK_REALLOC) { ++allocations; } return 1;
}
#endif
PlayerTest pt;
static _Alignas(64) unsigned char core_bytes[MEMORY_BUDGET_CORE], pcm[MEMORY_BUDGET_PCM];
static _Alignas(16) unsigned char codecs[2][DECODER_CODEC_BYTES], works[2][DECODER_WORK_BYTES];
static size_t load(const char *name, const char *ext, void *out, size_t capacity)
{
    char path[1024]; CHECK(sprintf_s(path, sizeof(path), "%s/%s.%s", VORBIS_FIXTURE_DIR, name, ext) > 0);
    FILE *file = NULL; CHECK(fopen_s(&file, path, "rb") == 0 && file);
    size_t bytes = fread(out, 1, capacity, file); CHECK(fgetc(file) == EOF && fclose(file) == 0); return bytes;
}
void pt_load_fixtures(void)
{
    const char *ogg[] = {"mono22", "stereo44", "stereo48"};
    const char *golden[] = {"mono22-to-48000-2", "stereo44", "stereo48"};
    for (unsigned i = 0; i < 3; ++i) {
        pt.files[i].bytes = load(ogg[i], "ogg", pt.files[i].ogg, sizeof(pt.files[i].ogg));
        pt.files[i].frames = load(golden[i], "f32", pt.files[i].golden, sizeof(pt.files[i].golden)) / (2 * sizeof(float));
    }
}
static AudioResult fake_open(void *ctx, AudioSink *s, AudioFormat *f, size_t *os_bytes)
{
    (void)ctx; (void)s; *f = pt.format; *os_bytes = 4096; return AUDIO_OK;
}
static AudioResult fake_start(void *ctx) { (void)ctx; ++pt.starts; pt.output_frames = 0; return AUDIO_OK; }
static AudioResult fake_stop(void *ctx) { (void)ctx; ++pt.stops; return AUDIO_OK; }
static void fake_close(void *ctx) { (void)ctx; ++pt.closes; }
static SinkClock fake_clock(void *ctx)
{
    (void)ctx;
    SinkClock c = {.frequency = pt.format.sample_rate,
        .position = pt.output_frames > pt.latency ? pt.output_frames - pt.latency : 0,
        .qpc_100ns = pt.output_frames * 10000000 / pt.format.sample_rate,
        .latency_100ns = (uint64_t)pt.latency * 10000000 / pt.format.sample_rate,
        .valid = pt.clock_valid, .latency_valid = true};
    return c;
}
static const SinkBackend backend = {fake_open,fake_start,fake_stop,fake_close,fake_clock};
static AudioResult read_source(void *ctx, uint64_t ticket, uint64_t offset, void *out, size_t capacity, size_t *got)
{
    PtReader *r = ctx; CHECK(r->used && r->ticket == ticket); ++r->reads; *got = 0;
    if (r->blocked) { return AUDIO_NEED_MORE; }
    PtFixture *f = &pt.files[r->fixture]; CHECK(offset <= f->bytes);
    size_t n = f->bytes - (size_t)offset; if (n > capacity) { n = capacity; }
    memcpy(out, f->ogg + (size_t)offset, n); *got = n;
    return offset + n == f->bytes ? AUDIO_EOF : AUDIO_OK;
}
void pt_open(void)
{
    pt.format = (AudioFormat){48000,2,AUDIO_FLOAT32}; pt.clock_valid = true;
    pt.starts = pt.stops = pt.closes = pt.resolved = pt.retired = pt.peak_readers = 0;
    pt.latency = 0; pt.held_count = 0; pt.hold_network = pt.hold_retire = pt.fail_prefetch = false;
    memset(pt.readers, 0, sizeof(pt.readers));
    CHECK(arena_init(&pt.core, core_bytes, sizeof(core_bytes)));
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(arena_init(&pt.codec[i], codecs[i], sizeof(codecs[i])));
        CHECK(arena_init(&pt.work[i], works[i], sizeof(works[i])));
    }
    CHECK(sink_open(&pt.sink, pcm, &backend, NULL) == AUDIO_OK);
    PlayerConfig config = {.sink = &pt.sink, .codec = {&pt.codec[0], &pt.codec[1]}, .work = {&pt.work[0], &pt.work[1]}};
    CHECK(player_create(&pt.core, &config, &pt.player) == PLAYER_OK);
}
void pt_command(PlayerCommand c) { CHECK(player_submit(pt.player, &c) == PLAYER_OK); }
void pt_track(PlayerCommandType type, unsigned track, bool play)
{
    PlayerCommand c = {.type = type, .play = play}; c.track_id[15] = (uint8_t)(track + 1); pt_command(c);
}
void pt_reply(const PlayerRequest *r)
{
    if (r->type == PLAYER_RETIRE) {
        for (unsigned i = 0; i < 2; ++i) {
            if (pt.readers[i].used && pt.readers[i].ticket == r->ticket) { pt.readers[i].used = false; }
        }
        pt_command((PlayerCommand){.type = PLAYER_SOURCE_RETIRED, .ticket = r->ticket}); ++pt.retired; return;
    }
    ++pt.resolved;
    if (r->prefetch && pt.fail_prefetch) {
        pt_command((PlayerCommand){.type = PLAYER_SOURCE_FAILED, .ticket = r->ticket, .error = AUDIO_CORRUPT}); return;
    }
    unsigned i = pt.readers[0].used ? 1 : 0; CHECK(!pt.readers[i].used);
    unsigned fixture = r->track_id[15] - 1; CHECK(fixture < 3);
    pt.readers[i] = (PtReader){.used = true, .ticket = r->ticket, .fixture = fixture};
    unsigned active = pt.readers[0].used + pt.readers[1].used;
    if (active > pt.peak_readers) { pt.peak_readers = active; }
    pt_command((PlayerCommand){.type = PLAYER_SOURCE_READY, .ticket = r->ticket,
        .source = {pt.files[fixture].bytes, read_source, &pt.readers[i]}});
}
void pt_network(void)
{
    PlayerRequest r;
    while (player_take_request(pt.player, &r) == PLAYER_OK) {
        if (pt.hold_network || (pt.hold_retire && r.type == PLAYER_RETIRE)) {
            CHECK(pt.held_count < 16); pt.held[pt.held_count++] = r;
        } else { pt_reply(&r); }
    }
}
void pt_drive(void)
{
    for (unsigned n = 0; n < 100; ++n) {
        PlayerResult result = player_pump(pt.player);
        CHECK(result == PLAYER_OK || result == PLAYER_MORE || result == PLAYER_CLOSED);
        pt_network();
        HANDLE handles[3]; size_t count = player_wait_handles(pt.player, handles);
        /* Ignore PCM low-water while emulating hardware; commands/new sources
         * drive immediate work, otherwise virtual test controls device progress. */
        if (result != PLAYER_MORE && count && WaitForSingleObject(handles[0], 0) == WAIT_TIMEOUT) { return; }
    }
    CHECK(false);
}
void pt_render(uint32_t frames)
{
    CHECK(frames * pt.format.channels <= 8192);
    sink_render(&pt.sink, pt.output, frames); pt.output_frames += frames;
}
void pt_compare(const float *a, const float *b, size_t samples)
{
    for (size_t i = 0; i < samples; ++i) { CHECK(isfinite(a[i]) && fabsf(a[i] - b[i]) < 0.00003f); }
}
void pt_close(void)
{
    pt.hold_network = pt.hold_retire = false;
    pt_command((PlayerCommand){.type = PLAYER_SHUTDOWN}); pt_drive();
    CHECK(player_pump(pt.player) == PLAYER_CLOSED);
    CHECK(player_destroy(pt.player) == PLAYER_OK); pt.player = NULL;
    if (pt.sink.opened) { CHECK(sink_close(&pt.sink) == AUDIO_OK); }
    CHECK(atomic_load(&pt.sink.device_bytes) == 0);
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(!pt.readers[i].used && pt.codec[i].used == 0 && pt.work[i].used == 0);
        arena_dispose(&pt.codec[i]); arena_dispose(&pt.work[i]);
    }
    arena_dispose(&pt.core);
}
void pt_heap_begin(void)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    allocations = 0; _CrtSetAllocHook(heap_hook);
#endif
}
void pt_heap_end(void)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(NULL); CHECK(allocations == 0);
#endif
}
