#include "api/metadata.h"
#include "support/test_check.h"
#include "pb_encode.h"
#include <string.h>

typedef struct Wire { uint8_t data[16384]; size_t bytes; } Wire;
typedef struct Envelope {
    uint64_t kind, status, provider;
    const char *uri;
    const char *type;
    bool omit_header, duplicate_entity, duplicate_array, duplicate_any;
} Envelope;

static const char uri[] = "spotify:track:0000000000000000000000";
static const char other_uri[] = "spotify:track:0000000000000000000001";
static const char track_type[] = "type.googleapis.com/spotify.metadata.Track";
static MediaPolicy policy = {{'N', 'L', '\0'}, "premium", false, 60000};
static Wire root, response, temp, extra;

static void number(Wire *out, uint32_t tag, uint64_t value)
{
    pb_ostream_t stream = pb_ostream_from_buffer(out->data + out->bytes, sizeof(out->data) - out->bytes);
    CHECK(pb_encode_tag(&stream, PB_WT_VARINT, tag)); CHECK(pb_encode_varint(&stream, value));
    out->bytes += stream.bytes_written;
}

static void blob(Wire *out, uint32_t tag, const void *data, size_t bytes)
{
    pb_ostream_t stream = pb_ostream_from_buffer(out->data + out->bytes, sizeof(out->data) - out->bytes);
    CHECK(pb_encode_tag(&stream, PB_WT_STRING, tag)); CHECK(pb_encode_string(&stream, data, bytes));
    out->bytes += stream.bytes_written;
}

static void message(Wire *out, uint32_t tag, const Wire *nested)
{
    blob(out, tag, nested->data, nested->bytes);
}

static void text(Wire *out, uint32_t tag, const char *value)
{
    blob(out, tag, value, strlen(value));
}

static void file(Wire *track, uint64_t format, uint8_t marker)
{
    Wire value = {0}; uint8_t id[20]; memset(id, marker, sizeof(id));
    blob(&value, 1, id, sizeof(id)); number(&value, 2, format); message(track, 12, &value);
}

static void track(Wire *out, uint8_t marker, int format)
{
    memset(out, 0, sizeof(*out)); uint8_t gid[16] = {0}; gid[15] = marker;
    blob(out, 1, gid, sizeof(gid)); number(out, 7, 420000);
    if (format >= 0) file(out, (uint64_t)format, (uint8_t)(format + 10));
}

static Envelope defaults(void)
{
    Envelope e = {10, 200, 0, uri, track_type, false, false, false, false}; return e;
}

static void envelope(Wire *out, const Wire *value, Envelope e)
{
    Wire any = {0}, entity = {0}, array = {0}, header = {0};
    text(&any, 1, e.type); message(&any, 2, value);
    if (e.duplicate_any) message(&any, 2, value);
    number(&header, 1, e.status);
    if (!e.omit_header) message(&entity, 1, &header);
    text(&entity, 2, e.uri); message(&entity, 3, &any);
    header.bytes = 0; number(&header, 1, e.provider); message(&array, 1, &header);
    number(&array, 2, e.kind); message(&array, 3, &entity);
    if (e.duplicate_entity) message(&array, 3, &entity);
    out->bytes = 0; message(out, 2, &array);
    if (e.duplicate_array) message(out, 2, &array);
}

static MediaTrack decode(const Wire *value, Envelope e, MediaResult expected)
{
    envelope(&response, value, e);
    MediaTrack output, before; memset(&output, 0x5a, sizeof(output)); before = output;
    CHECK(metadata_decode(response.data, response.bytes, uri, &policy, &output) == expected);
    if (expected != MEDIA_OK) CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    return output;
}

static void restriction(Wire *out, uint32_t country_tag, const char *countries, const char *product)
{
    out->bytes = 0; text(out, 5, product); text(out, country_tag, countries); number(out, 4, 0);
}

static void encoding_tests(void)
{
    uint8_t actual[128], before[128]; size_t written = 999;
    memset(actual, 0x55, sizeof(actual)); memcpy(before, actual, sizeof(actual));
    CHECK(metadata_request_body(uri, actual, 1, &written) == MEDIA_FULL && written == 0);
    CHECK(memcmp(actual, before, sizeof(actual)) == 0);
    CHECK(metadata_request_body(other_uri, actual, sizeof(actual), &written) == MEDIA_OK);
    const uint8_t prefix[] = {0x12, 42, 0x0a, 36}; const uint8_t suffix[] = {0x12, 2, 8, 10};
    CHECK(written == 44 && memcmp(actual, prefix, sizeof(prefix)) == 0);
    CHECK(memcmp(actual + 4, other_uri, 36) == 0 && memcmp(actual + 40, suffix, 4) == 0);
    CHECK(metadata_request_body("spotify:episode:0000000000000000000000", actual, sizeof(actual), &written) == MEDIA_INVALID);
    CHECK(written == 0);
    CHECK(metadata_request_body(NULL, actual, sizeof(actual), &written) == MEDIA_INVALID);
    CHECK(metadata_request_body(uri, NULL, sizeof(actual), &written) == MEDIA_INVALID);
    CHECK(metadata_request_body(uri, actual, sizeof(actual), NULL) == MEDIA_INVALID);
}

static void envelope_tests(void)
{
    track(&root, 0, 0); Envelope e = defaults();
    MediaTrack selected = decode(&root, e, MEDIA_OK);
    CHECK(selected.format == 0 && selected.duration_ms == 210000 && selected.file_id[0] == 10);
    e.kind = 12; (void)decode(&root, e, MEDIA_MALFORMED); e = defaults();
    e.status = 404; (void)decode(&root, e, MEDIA_HTTP_ERROR); e = defaults();
    e.provider = 500; (void)decode(&root, e, MEDIA_HTTP_ERROR); e = defaults();
    e.provider = 200; (void)decode(&root,e,MEDIA_OK); e=defaults();
    e.uri = other_uri; (void)decode(&root, e, MEDIA_MALFORMED); e = defaults();
    e.type = "type.googleapis.com/spotify.metadata.Episode"; (void)decode(&root, e, MEDIA_MALFORMED); e = defaults();
    e.omit_header = true; (void)decode(&root, e, MEDIA_MALFORMED); e = defaults();
    e.duplicate_entity = true; (void)decode(&root, e, MEDIA_MALFORMED); e = defaults();
    e.duplicate_array = true; (void)decode(&root, e, MEDIA_MALFORMED); e = defaults();
    e.duplicate_any = true; (void)decode(&root, e, MEDIA_MALFORMED);
    track(&root, 1, 0); (void)decode(&root, defaults(), MEDIA_MALFORMED);
    track(&root, 0, 0); envelope(&response, &root, defaults());
    for (size_t i = 0; i < response.bytes; ++i) {
        MediaTrack output, before; memset(&output, 0xa5, sizeof(output)); before = output;
        CHECK(metadata_decode(response.data, i, uri, &policy, &output) != MEDIA_OK);
        CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    }
    MediaTrack output;
    CHECK(metadata_decode(response.data, METADATA_MAX_RESPONSE + 1U, uri, &policy, &output) == MEDIA_LIMIT);
    CHECK(metadata_decode(NULL, 0, uri, &policy, &output) == MEDIA_INVALID);
    CHECK(metadata_decode(response.data, response.bytes, uri, NULL, &output) == MEDIA_INVALID);
    CHECK(metadata_decode(response.data, response.bytes, other_uri, &policy, &output) == MEDIA_MALFORMED);
}

static void file_tests(void)
{
    uint8_t long_name[600]; memset(long_name, 'a', sizeof(long_name));
    track(&root, 0, 0); blob(&root, 2, long_name, sizeof(long_name));
    (void)decode(&root, defaults(), MEDIA_OK); /* Any.value exceeds generated static 257-byte option. */
    track(&root, 0, 0); file(&root, 9, 90); file(&root, 2, 32); file(&root, 1, 31);
    MediaTrack selected = decode(&root, defaults(), MEDIA_OK);
    CHECK(selected.format == 2 && selected.file_id[0] == 32);
    track(&root, 0, 8); (void)decode(&root, defaults(), MEDIA_UNSUPPORTED);
    track(&root, 0, -1); (void)decode(&root, defaults(), MEDIA_UNSUPPORTED);
    temp.bytes = 0; uint8_t id[20] = {0}; blob(&temp, 1, id, 19); number(&temp, 2, 0);
    message(&root, 12, &temp); (void)decode(&root, defaults(), MEDIA_MALFORMED);
    track(&root, 0, -1); temp.bytes = 0; blob(&temp, 1, id, 20); blob(&temp, 1, id, 20);
    number(&temp, 2, 0); message(&root, 12, &temp); (void)decode(&root, defaults(), MEDIA_MALFORMED);
    root.bytes = 0; blob(&root, 1, id, 16); number(&root, 7, 0); file(&root, 0, 5);
    (void)decode(&root, defaults(), MEDIA_UNSUPPORTED);
    track(&root, 0, 0); number(&root, 7, 2); (void)decode(&root, defaults(), MEDIA_MALFORMED);
}

static void policy_tests(void)
{
    restriction(&temp, 2, "NLUS", "premium"); track(&root, 0, 0); message(&root, 11, &temp);
    (void)decode(&root, defaults(), MEDIA_OK);
    restriction(&temp, 2, "US", "premium"); track(&root, 0, 0); message(&root, 11, &temp);
    (void)decode(&root, defaults(), MEDIA_RESTRICTED);
    restriction(&temp, 3, "NL", "premium"); track(&root, 0, 0); message(&root, 11, &temp);
    (void)decode(&root, defaults(), MEDIA_RESTRICTED);
    restriction(&temp, 2, "US", "free"); track(&root, 0, 0); message(&root, 11, &temp);
    (void)decode(&root, defaults(), MEDIA_OK);
    restriction(&temp, 2, "USA", "premium"); track(&root, 0, 0); message(&root, 11, &temp);
    (void)decode(&root, defaults(), MEDIA_MALFORMED);
    restriction(&temp, 2, "NL", "premium"); text(&temp, 3, "US");
    track(&root, 0, 0); message(&root, 11, &temp); (void)decode(&root, defaults(), MEDIA_MALFORMED);
    temp.bytes = 0; text(&temp, 2, "NL"); number(&temp, 4, 1);
    track(&root, 0, 0); message(&root, 11, &temp); (void)decode(&root, defaults(), MEDIA_RESTRICTED);
    temp.bytes = 0; text(&temp, 2, "US"); number(&temp, 1, 1);
    track(&root, 0, 0); message(&root, 11, &temp); (void)decode(&root, defaults(), MEDIA_RESTRICTED);
    temp.bytes = 0; text(&temp, 2, "US"); number(&temp, 1, 99);
    track(&root, 0, 0); message(&root, 11, &temp); (void)decode(&root, defaults(), MEDIA_RESTRICTED);
    track(&root, 0, 0); number(&root, 9, 1); policy.filter_explicit = true;
    (void)decode(&root, defaults(), MEDIA_RESTRICTED); policy.filter_explicit = false;
    (void)decode(&root, defaults(), MEDIA_OK);
    policy.filter_explicit = true; track(&temp, 1, 2); message(&root, 13, &temp);
    (void)decode(&root, defaults(), MEDIA_RESTRICTED); policy.filter_explicit = false;
    track(&root, 0, 0); number(&root, 17, policy.utc_ms + 1); (void)decode(&root, defaults(), MEDIA_RESTRICTED);
    track(&root, 0, 0); number(&root, 17, policy.utc_ms); (void)decode(&root, defaults(), MEDIA_OK);
}

static void availability_tests(void)
{
    Wire date = {0}; number(&date, 1, 3940); number(&date, 5, 2);
    temp.bytes = 0; text(&temp, 1, "premium"); message(&temp, 2, &date);
    track(&root, 0, 0); message(&root, 19, &temp); (void)decode(&root, defaults(), MEDIA_OK);
    date.bytes = 0; number(&date, 1, 3940); number(&date, 5, 4);
    temp.bytes = 0; message(&temp, 2, &date);
    track(&root, 0, 0); message(&root, 19, &temp); (void)decode(&root, defaults(), MEDIA_RESTRICTED);
    date.bytes = 0; number(&date, 1, 3940);
    temp.bytes = 0; text(&temp, 1, "free"); message(&temp, 2, &date);
    track(&root, 0, 0); message(&root, 19, &temp); (void)decode(&root, defaults(), MEDIA_RESTRICTED);
    temp.bytes = 0; text(&temp, 1, "premium");
    track(&root, 0, 0); message(&root, 19, &temp); (void)decode(&root, defaults(), MEDIA_MALFORMED);
    date.bytes = 0; number(&date, 1, 4050); number(&date, 2, 4); number(&date, 3, 58);
    temp.bytes = 0; message(&temp, 2, &date);
    track(&root, 0, 0); message(&root, 19, &temp); (void)decode(&root, defaults(), MEDIA_MALFORMED);
    date.bytes = 0; number(&date, 1, 3800); temp.bytes = 0; message(&temp, 2, &date);
    track(&root, 0, 0); message(&root, 19, &temp); (void)decode(&root, defaults(), MEDIA_OK);
}

static void alternative_tests(void)
{
    track(&root, 0, 0); track(&temp, 1, 2); message(&root, 13, &temp);
    MediaTrack selected = decode(&root, defaults(), MEDIA_OK);
    CHECK(selected.gid[15] == 0 && selected.format == 0);
    track(&root, 0, 8); track(&temp, 1, 2); message(&root, 13, &temp);
    selected = decode(&root, defaults(), MEDIA_OK);
    CHECK(selected.gid[15] == 1 && selected.format == 2);
    track(&root, 0, 8); track(&temp, 0, 2); message(&root, 13, &temp);
    (void)decode(&root, defaults(), MEDIA_MALFORMED);
    track(&root, 0, 8);
    for (uint8_t i = 1; i <= 8; ++i) { track(&temp, i, 1); message(&root, 13, &temp); }
    (void)decode(&root, defaults(), MEDIA_OK);
    track(&temp, 9, 1); message(&root, 13, &temp); (void)decode(&root, defaults(), MEDIA_LIMIT);
    track(&temp, 4, 1);
    for (uint8_t i = 3; i > 0; --i) { track(&extra, i, -1); message(&extra, 13, &temp); temp = extra; }
    track(&root, 0, -1); message(&root, 13, &temp); (void)decode(&root, defaults(), MEDIA_LIMIT);
    track(&temp, 3, 1);
    for (uint8_t i = 2; i > 0; --i) { track(&extra, i, -1); message(&extra, 13, &temp); temp = extra; }
    track(&root, 0, -1); message(&root, 13, &temp); (void)decode(&root, defaults(), MEDIA_OK);
    track(&root, 0, -1);
    for (uint8_t i = 1; i <= 8; ++i) {
        track(&temp, i, -1); track(&extra, (uint8_t)(i + 8), 1); message(&temp, 13, &extra); message(&root, 13, &temp);
    }
    (void)decode(&root, defaults(), MEDIA_LIMIT);
}

static void relink_tests(void)
{
    uint8_t gid[16] = {0}; gid[15] = 1;
    temp.bytes = 0; blob(&temp, 1, gid, sizeof(gid));
    track(&root, 0, 8); message(&root, 13, &temp);
    envelope(&response, &root, defaults());
    MetadataAlternatives alternatives; memset(&alternatives, 0x5a, sizeof(alternatives));
    MediaTrack output, before; memset(&output, 0xa5, sizeof(output)); before = output;
    CHECK(metadata_decode_relink(response.data, response.bytes, uri, &policy, &output, &alternatives) == MEDIA_UNSUPPORTED);
    CHECK(alternatives.count == 1 && alternatives.gids[0][15] == 1);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    number(&temp, 17, policy.utc_ms + 1); track(&root, 0, 8); message(&root, 13, &temp);
    envelope(&response, &root, defaults());
    CHECK(metadata_decode_relink(response.data, response.bytes, uri, &policy, &output, &alternatives) == MEDIA_RESTRICTED);
    CHECK(alternatives.count == 0 && memcmp(&output, &before, sizeof(output)) == 0);
    track(&root, 0, 0); message(&root, 13, &temp); envelope(&response, &root, defaults());
    CHECK(metadata_decode_relink(response.data, response.bytes, uri, &policy, &output, &alternatives) == MEDIA_OK);
    CHECK(alternatives.count == 0 && output.gid[15] == 0);
    temp.bytes = 0; blob(&temp, 1, gid, sizeof(gid)); track(&root, 0, 8);
    message(&root, 13, &temp); message(&root, 13, &temp); envelope(&response, &root, defaults());
    MetadataAlternatives unchanged = alternatives;
    CHECK(metadata_decode_relink(response.data, response.bytes, uri, &policy, &output, &alternatives) == MEDIA_MALFORMED);
    CHECK(memcmp(&alternatives, &unchanged, sizeof(alternatives)) == 0);
}

static void display_tests(void)
{
    Wire artist={0}, album={0}, image={0}, group={0}; uint8_t cover[20]; memset(cover,0x31,20);
    track(&root,0,0); text(&root,2,"Track title"); text(&artist,2,"First artist"); message(&root,4,&artist);
    artist.bytes=0; text(&artist,2,"Second artist"); message(&root,4,&artist);
    blob(&image,1,cover,20); number(&image,2,0); message(&album,9,&image);
    memset(cover,0x42,20); image.bytes=0; blob(&image,1,cover,20); number(&image,2,1);
    message(&group,1,&image); message(&album,17,&group); message(&root,3,&album);
    MediaTrack output=decode(&root,defaults(),MEDIA_OK);
    CHECK(!strcmp(output.display.title,"Track title") && !strcmp(output.display.artist,"First artist"));
    CHECK(output.display.has_cover && output.display.cover_id[0]==0x42);
    /* Display truncates only at scalar boundaries; validation covers the tail. */
    uint8_t long_name[300]; memset(long_name,'a',190); long_name[190]=0xd7; long_name[191]=0x90;
    memset(long_name+192,'b',108); track(&root,0,0); blob(&root,2,long_name,sizeof(long_name));
    output=decode(&root,defaults(),MEDIA_OK); CHECK(strlen(output.display.title)==190);
    long_name[299]=0xff; track(&root,0,0); blob(&root,2,long_name,sizeof(long_name));
    (void)decode(&root,defaults(),MEDIA_MALFORMED);
    track(&root,0,0); text(&root,2,"A"); text(&root,2,"B"); (void)decode(&root,defaults(),MEDIA_MALFORMED);
    track(&root,0,0); const char embedded[]={'a',0,'b'}; blob(&root,2,embedded,sizeof(embedded));
    (void)decode(&root,defaults(),MEDIA_MALFORMED);
    track(&root,0,0); image.bytes=album.bytes=0; blob(&image,1,cover,19); message(&album,9,&image); message(&root,3,&album);
    (void)decode(&root,defaults(),MEDIA_MALFORMED);
}
void metadata_tests(void)
{
    encoding_tests(); envelope_tests(); file_tests(); policy_tests(); availability_tests(); alternative_tests(); relink_tests(); display_tests();
    /* Country lists may contain every two-letter market, exceeding the generic
     * 256-byte text bound. Their separate static cap is 512 bytes. */
    char markets[301];
    for (size_t i = 0; i < 300; i += 2) { markets[i] = 'N'; markets[i + 1] = 'L'; }
    markets[300] = '\0'; track(&root, 0, 2);
    restriction(&temp, 2, markets, "premium"); message(&root, 11, &temp);
    (void)decode(&root, defaults(), MEDIA_OK);
}
