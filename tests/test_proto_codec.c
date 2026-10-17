#include "proto_test.h"
#include "metadata.pb.h"
#include "mercury.pb.h"
#include "connect.pb.h"
#include "authentication.pb.h"
#include "keyexchange.pb.h"

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static size_t heap_calls;
static int __cdecl heap_hook(int type, void *data, size_t size, int block,
                             long request, const unsigned char *file, int line)
{
    (void)type; (void)data; (void)size; (void)block; (void)request; (void)file; (void)line;
    ++heap_calls;
    return 1;
}
#endif

static const uint8_t track_wire[] = {
    0x0a,16,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    0x12,1,'T', 0x1a,3,0x12,1,'A', 0x22,3,0x12,1,'R',
    0x38,0xf6,1, 0x48,1,
    0x62,24,0x0a,20,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,0x10,2,
    0xa2,2,15,'s','p','o','t','i','f','y',':','t','r','a','c','k',':','x'
};

static void selected_fields(void)
{
    uint8_t memory[8192];
    Arena arena;
    CHECK(arena_init(&arena, memory, sizeof(memory)));
    ProtoTrack track = {0};
    CHECK(proto_decode_track(track_wire, sizeof(track_wire), &arena, &track) == PROTO_OK);
    CHECK(track.has_gid && track.gid[15] == 15);
    CHECK(strcmp(track.name, "T") == 0 && strcmp(track.album_name, "A") == 0);
    CHECK(strcmp(track.uri, "spotify:track:x") == 0);
    CHECK(track.duration_ms == 123 && track.explicit_content);
    CHECK(track.artist_count == 1 && strcmp(track.artists->name, "R") == 0);
    CHECK(track.file_count == 1 && track.files->has_id && track.files->id[19] == 19);
    CHECK(track.files->has_format && track.files->format == 2);
    CHECK(arena.used > 0 && arena.used < 512);
    arena_dispose(&arena);
}

static void unknown_and_malformed(void)
{
    static const uint8_t unknown[] = {
        0x12,1,'T',0xa0,6,1, 0xa1,6,0,0,0,0,0,0,0,0,
        0xa2,6,2,0xff,0xff, 0xa5,6,0,0,0,0
    };
    uint8_t memory[1024];
    Arena arena;
    CHECK(arena_init(&arena, memory, sizeof(memory)));
    ProtoTrack track = {0};
    CHECK(proto_decode_track(unknown, sizeof(unknown), &arena, &track) == PROTO_OK);
    CHECK(strcmp(track.name, "T") == 0);
    static const uint8_t bad[][14] = {
        {0}, {0x80}, {0x12,0x80}, {0x12,2,'x'}, {0x38,0x80},
        {0x13}, {0x14}, {0x0f}, {0x12,1,0xff}, {0x12,1,0},
        {0x6a,1,0x80}, /* Malformed discarded alternative Track. */
        {0x38,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,2},
        {0xa0,6,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,2},
        {0x6a,3,0x12,1,0xff}, /* Invalid UTF-8 in a discarded alternative. */
        {0x38,0x80,0x80,0x80,0x80,0x10} /* sint32 outside range */
    };
    static const size_t lengths[] = {1,1,2,3,2,1,1,1,3,3,3,11,12,5,6};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        size_t before = arena.used;
        ProtoTrack original = track;
        CHECK(proto_decode_track(bad[i], lengths[i], &arena, &track) != PROTO_OK);
        CHECK(arena.used == before && memcmp(&track, &original, sizeof(track)) == 0);
    }
    CHECK(proto_validate(NULL, NULL, 0) == PROTO_INVALID_ARGUMENT);
    CHECK(proto_validate(spotify_metadata_Track_fields, NULL, 1) == PROTO_INVALID_ARGUMENT);
    CHECK(proto_validate(spotify_metadata_Track_fields, unknown, PROTO_MAX_WIRE + 1) == PROTO_LIMIT);
    CHECK(proto_decode_track(NULL, 0, NULL, &track) == PROTO_INVALID_ARGUMENT);
    Header header = Header_init_zero;
    header.has_method = true; strcpy_s(header.method, sizeof(header.method), "GET");
    uint8_t tiny[1];
    size_t written = 99;
    CHECK(proto_encode(Header_fields, &header, tiny, sizeof(tiny), &written) == PROTO_LIMIT);
    CHECK(written == 0);
    /* A valid prefix may end at a field boundary; only non-boundary truncations fail. */
    for (size_t i = 1; i < sizeof(track_wire); ++i) {
        ProtoResult result = proto_decode_track(track_wire, i, &arena, &track);
        CHECK(result == PROTO_OK || result == PROTO_MALFORMED || result == PROTO_NO_MEMORY);
        CHECK(arena_reset(&arena));
    }
    arena_dispose(&arena);
}

static void limits(void)
{
    uint8_t memory[16384], wire[4096];
    Arena arena;
    CHECK(arena_init(&arena, memory, sizeof(memory)));
    ProtoTrack track = {0};
    /* Each wrapper adds a known recursive Track.alternative, even if unselected. */
    size_t size = 0;
    for (unsigned depth = 1; depth <= PROTO_MAX_DEPTH + 1; ++depth) {
        CHECK(proto_decode_track(wire, size, &arena, &track) ==
              (depth <= PROTO_MAX_DEPTH ? PROTO_OK : PROTO_LIMIT));
        memmove(wire + 2, wire, size);
        wire[0] = 0x6a; wire[1] = (uint8_t)size;
        size += 2;
    }
    for (size_t i = 0; i < PROTO_MAX_ITEMS + 1; ++i) {
        wire[2*i] = 0x22; wire[2*i+1] = 0; /* Track.artist */
    }
    CHECK(proto_decode_track(wire, 200, &arena, &track) == PROTO_OK);
    CHECK(track.artist_count == PROTO_MAX_ITEMS);
    CHECK(arena_reset(&arena));
    CHECK(proto_decode_track(wire, 202, &arena, &track) == PROTO_LIMIT);
    for (size_t i = 0; i < PROTO_MAX_ITEMS + 1; ++i) wire[2*i] = 0x6a;
    CHECK(proto_validate(spotify_metadata_Track_fields, wire, 202) == PROTO_LIMIT);
    /* Modern Connect repeated strings must remain bounded when skipped. */
    for (size_t i = 0; i < PROTO_MAX_ITEMS + 1; ++i) wire[2*i] = 0x22;
    CHECK(proto_validate(spotify_connectstate_ClusterUpdate_fields, wire, 200) == PROTO_OK);
    CHECK(proto_validate(spotify_connectstate_ClusterUpdate_fields, wire, 202) == PROTO_LIMIT);
    /* Packed scalar limits apply to elements, not just field occurrences. */
    wire[0] = 0x0a; wire[1] = 101; memset(wire + 2, 0, 101);
    CHECK(proto_validate(spotify_metadata_Restriction_fields, wire, 103) == PROTO_LIMIT);
    wire[1] = 100;
    CHECK(proto_validate(spotify_metadata_Restriction_fields, wire, 102) == PROTO_OK);
    wire[0] = 0x12; wire[1] = 0x80; wire[2] = 2; memset(wire + 3, 'x', 257);
    CHECK(proto_decode_track(wire, 259, &arena, &track) == PROTO_OK);
    CHECK(strlen(track.name) == PROTO_MAX_TEXT);
    CHECK(arena_reset(&arena));
    wire[1] = 0x81;
    CHECK(proto_decode_track(wire, 260, &arena, &track) == PROTO_LIMIT);
    wire[0] = 0xa2; wire[1] = 2; wire[2] = 0x81; wire[3] = 2;
    memset(wire + 4, 'x', 257);
    CHECK(proto_decode_track(wire, 261, &arena, &track) == PROTO_LIMIT);
    /* Static auth and known nested required fields are checked before decode. */
    memset(wire, 0, sizeof(wire)); wire[0] = 0xf2; wire[1] = 1; wire[2] = 0x81; wire[3] = 8;
    CHECK(proto_validate(LoginCredentials_fields, wire, 1029) == PROTO_LIMIT);
    CHECK(proto_validate(APResponseMessage_fields, "\x52\x00", 2) == PROTO_MALFORMED);
    arena_dispose(&arena);
}

static void exhaustion_and_mutations(void)
{
    uint8_t memory[256], wire[256];
    Arena arena;
    CHECK(arena_init(&arena, memory, 1));
    ProtoTrack original = {0}, out = original;
    CHECK(proto_decode_track(track_wire, sizeof(track_wire), &arena, &out) == PROTO_NO_MEMORY);
    CHECK(arena.used == 0 && memcmp(&original, &out, sizeof(out)) == 0);
    arena_dispose(&arena);
    CHECK(arena_init(&arena, memory, sizeof(memory)));
    uint32_t random = 42;
    for (unsigned i = 0; i < 5000; ++i) {
        memcpy(wire, track_wire, sizeof(track_wire));
        random = random * 1664525u + 1013904223u;
        wire[random % sizeof(track_wire)] = (uint8_t)(random >> 24);
        out = original;
        ProtoResult result = proto_decode_track(wire, sizeof(track_wire), &arena, &out);
        if (result != PROTO_OK)
            CHECK(arena.used == 0 && memcmp(&out, &original, sizeof(out)) == 0);
        CHECK(arena_reset(&arena));
    }
    arena_dispose(&arena);
}

int main(void)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(heap_hook);
    size_t before = heap_calls;
#endif
    test_proto_messages(); selected_fields(); unknown_and_malformed(); limits();
    exhaustion_and_mutations();
#if defined(_MSC_VER) && defined(_DEBUG)
    CHECK(heap_calls == before);
    _CrtSetAllocHook(NULL);
#endif
    puts("protocol_proto passed (no heap allocations)");
    return 0;
}
