#include "vorbis_test.h"

static _Alignas(16) uint8_t next_codec[DECODER_CODEC_BYTES], next_work[DECODER_WORK_BYTES];
static DWORD WINAPI fake_device_callback(void *context)
{
    (void)context; float out[8]; size_t frames = 99, accepted = 99;
    CHECK(decoder_read_frames(vt.decoder, 1, out, 4, &frames) == AUDIO_BAD_STATE && frames == 0);
    CHECK(decoder_push(vt.decoder, 1, 0, vt.ogg, 1, false, &accepted) == AUDIO_BAD_STATE && accepted == 0);
    CHECK(decoder_seek(vt.decoder, 2, 0) == AUDIO_BAD_STATE);
    CHECK(decoder_close(vt.decoder) == AUDIO_BAD_STATE);
    return 0;
}
static AudioResult reentrant(void *context, uint64_t generation, uint64_t offset, void *out, size_t cap, size_t *got)
{
    (void)context; size_t frames = 99; float pcm[8];
    CHECK(decoder_read_frames(vt.decoder, generation, pcm, 4, &frames) == AUDIO_BAD_STATE && frames == 0);
    CHECK(decoder_close(vt.decoder) == AUDIO_BAD_STATE);
    CHECK(decoder_seek(vt.decoder, generation + 1, 0) == AUDIO_BAD_STATE);
    return vt_read_at(NULL, generation, offset, out, cap, got);
}
void vt_limit_tests(void)
{
    vt_load(1); vt_heap_start(); vt_open(vt.rate, vt.channels, true, DECODER_CODEC_BYTES);
    size_t initial_codec = vt.codec.used, initial_work = vt.work.used, got;
    HANDLE thread = CreateThread(NULL, 0, fake_device_callback, NULL, 0, NULL); CHECK(thread != NULL);
    CHECK(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0 && CloseHandle(thread));
    CHECK(vt.calls == 0); /* Wrong-thread device callback never enters the codec/source. */
    vt.decoder->config.read_at = reentrant;
    CHECK(decoder_read_frames(vt.decoder, 1, vt.actual, 1000, &got) == AUDIO_OK && got == 1000);
    vt_compare(vt.actual, vt.golden, got * vt.channels);
    Arena codec, work; VorbisDecoder *next = NULL;
    CHECK(arena_init(&codec, next_codec, sizeof(next_codec)) && arena_init(&work, next_work, sizeof(next_work)));
    DecoderConfig config = {.output = {vt.rate,vt.channels,AUDIO_FLOAT32}, .generation = 1,
        .source_bytes = vt.file_bytes, .read_at = vt_read_at};
    CHECK(decoder_open(&codec, &work, &config, &next) == AUDIO_OK);
    CHECK(codec.used + vt.codec.used == 2 * DECODER_CODEC_BYTES);
    CHECK(work.used + vt.work.used <= 2 * DECODER_WORK_BYTES);
    CHECK(decoder_read_frames(next, 1, vt.actual, 1000, &got) == AUDIO_OK && got == 1000);
    vt_compare(vt.actual, vt.golden, got * vt.channels);
    CHECK(vt.codec.used == initial_codec && vt.work.used == initial_work);
    CHECK(decoder_close(next) == AUDIO_OK && codec.used == 0 && work.used == 0);
    arena_dispose(&codec); arena_dispose(&work);
    for (size_t i = 0; i < sizeof(next_codec); ++i) { CHECK(next_codec[i] == 0); }
    for (size_t i = 0; i < sizeof(next_work); ++i) { CHECK(next_work[i] == 0); }
    vt_close(); vt_heap_stop();
    CHECK(arena_init(&codec, next_codec, sizeof(next_codec)) && arena_init(&work, next_work, 128));
    CHECK(decoder_open(&codec, &work, &config, &next) == AUDIO_NO_MEMORY && next == NULL);
    CHECK(codec.used == 0 && work.used == 0); arena_dispose(&codec); arena_dispose(&work);
    config.output.channels = 3;
    CHECK(arena_init(&codec, next_codec, sizeof(next_codec)) && arena_init(&work, next_work, sizeof(next_work)));
    CHECK(decoder_open(&codec, &work, &config, &next) == AUDIO_UNSUPPORTED && next == NULL);
    config.output.channels=vt.channels; config.trailing_zero_limit=16;
    CHECK(decoder_open(&codec,&work,&config,&next)==AUDIO_INVALID && next==NULL);
    CHECK(codec.used == 0 && work.used == 0); arena_dispose(&codec); arena_dispose(&work);
}
