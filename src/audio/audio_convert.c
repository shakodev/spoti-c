#include "audio_format.h"
#include <math.h>
#include <string.h>

bool audio_format_valid(AudioFormat f)
{
    return f.sample_format == AUDIO_FLOAT32 && f.sample_rate >= 8000 && f.sample_rate <= 192000 &&
        (f.channels == 1 || f.channels == 2);
}
AudioResult audio_convert_length(uint64_t frames, uint32_t in, uint32_t out, uint64_t *length)
{
    if (length == NULL || in == 0 || out == 0) { return AUDIO_INVALID; }
    if (frames > (UINT64_MAX - (in - 1)) / out) { return AUDIO_LIMIT; }
    *length = (frames * out + in - 1) / in; return AUDIO_OK;
}
AudioResult audio_convert_init(AudioConvert *c, AudioFormat in, AudioFormat out, uint64_t position)
{
    if (c == NULL || !audio_format_valid(in) || !audio_format_valid(out)) { return AUDIO_UNSUPPORTED; }
    if (position > UINT64_MAX / in.sample_rate) { return AUDIO_LIMIT; }
    *c = (AudioConvert){.input = in, .output = out, .position = position}; return AUDIO_OK;
}
AudioResult audio_convert_block(AudioConvert *c, const float *input, size_t frames, uint64_t start,
    bool final, float *output, size_t capacity, size_t *written, bool *consumed)
{
    if (c == NULL || input == NULL || frames == 0 || written == NULL || consumed == NULL ||
        (capacity != 0 && output == NULL) || frames > UINT64_MAX - start) { return AUDIO_INVALID; }
    *written = 0; *consumed = false;
    uint64_t end = start + frames, limit = UINT64_MAX;
    if (final && audio_convert_length(end, c->input.sample_rate, c->output.sample_rate, &limit) != AUDIO_OK) { return AUDIO_LIMIT; }
    while (*written < capacity && c->position < limit) {
        if (c->position > UINT64_MAX / c->input.sample_rate) { return AUDIO_LIMIT; }
        uint64_t numerator = c->position * c->input.sample_rate;
        uint64_t at = numerator / c->output.sample_rate;
        uint32_t remainder = (uint32_t)(numerator % c->output.sample_rate);
        if (at >= end || (remainder != 0 && at == end - 1 && !final)) { break; }
        const float *a;
        if (at < start) {
            if (!c->tail_valid || at != c->tail_position || at + 1 != start) { return AUDIO_CORRUPT; }
            a = c->tail;
        } else { a = input + (size_t)(at - start) * c->input.channels; }
        const float *b = at + 1 >= end ? a : input + (size_t)(at + 1 - start) * c->input.channels;
        float mixed[2]; float fraction = (float)remainder / (float)c->output.sample_rate;
        for (uint32_t channel = 0; channel < c->input.channels; ++channel) {
            if (!isfinite(a[channel]) || !isfinite(b[channel])) { return AUDIO_CORRUPT; }
            mixed[channel] = a[channel] + (b[channel] - a[channel]) * fraction;
        }
        float *dest = output + *written * c->output.channels;
        if (c->input.channels == c->output.channels) { memcpy(dest, mixed, c->output.channels * sizeof(float)); }
        else if (c->output.channels == 1) { dest[0] = (mixed[0] + mixed[1]) * 0.5f; }
        else { dest[0] = mixed[0]; dest[1] = mixed[0]; }
        ++*written; ++c->position;
    }
    uint64_t at = c->position * c->input.sample_rate / c->output.sample_rate;
    if (at >= end || (at == end - 1 && !final)) {
        memcpy(c->tail, input + (frames - 1) * c->input.channels, c->input.channels * sizeof(float));
        c->tail_position = end - 1; c->tail_valid = true; *consumed = true;
    }
    if (final && c->position == limit) { *consumed = true; }
    return AUDIO_OK;
}
