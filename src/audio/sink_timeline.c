#include "audio_sink.h"

void sink_timeline_record(AudioSink *s, uint64_t output, uint64_t media, uint32_t frames, uint32_t got)
{
    /* Silent gaps are represented by the next media span's output_start.
     * A stalled network must not exhaust history with identical silence. */
    if (!got) { return; }
    uint64_t write = atomic_load_explicit(&s->span_write, memory_order_relaxed);
    uint64_t read = atomic_load_explicit(&s->span_read, memory_order_acquire);
    if (write - read == SINK_TIMELINE_SPANS) {
        atomic_store(&s->span_overflow, 1); return;
    }
    s->spans[write % SINK_TIMELINE_SPANS] = (SinkSpan){output, media, frames, got};
    atomic_store_explicit(&s->span_write, write + 1, memory_order_release);
}
static bool scale(uint64_t ticks, uint64_t frequency, uint32_t rate, uint64_t *frames)
{
    if (!frequency || ticks / frequency > UINT64_MAX / rate ||
        ticks % frequency > UINT64_MAX / rate) { return false; }
    uint64_t whole = ticks / frequency * rate, part = ticks % frequency * rate / frequency;
    if (part > UINT64_MAX - whole) { return false; }
    *frames = whole + part; return true;
}
void sink_timeline_query(AudioSink *s, SinkInfo *i)
{
    i->timeline_overflow = atomic_load(&s->span_overflow) != 0;
    uint64_t output = 0, latency = 0;
    bool valid = false;
    if (i->state == SINK_PAUSED) {
        i->timeline_valid = s->played_valid && !i->timeline_overflow;
        i->played_media_frames = s->played_media; return;
    }
    if (i->clock.valid && scale(i->clock.position, i->clock.frequency, i->format.sample_rate, &output) &&
        output <= UINT64_MAX - s->output_base) {
        output += s->output_base; valid = true;
    } else if (i->clock.latency_valid && scale(i->clock.latency_100ns, 10000000, i->format.sample_rate, &latency)) {
        if ((i->clock.latency_100ns % 10000000) * i->format.sample_rate % 10000000) { ++latency; }
        uint64_t queued = latency > UINT64_MAX - i->clock.padding_frames ? UINT64_MAX : latency + i->clock.padding_frames;
        output = i->submitted_frames > queued ? i->submitted_frames - queued : 0; valid = true;
    }
    if (output > i->submitted_frames) { output = i->submitted_frames; }
    uint64_t read = atomic_load_explicit(&s->span_read, memory_order_relaxed);
    uint64_t write = atomic_load_explicit(&s->span_write, memory_order_acquire);
    if (valid && !i->timeline_overflow) {
        while (read != write) {
            SinkSpan span = s->spans[read % SINK_TIMELINE_SPANS];
            if (output < span.output_start) { break; }
            uint64_t elapsed = output - span.output_start;
            uint64_t played = span.media_start + (elapsed < span.media ? elapsed : span.media);
            if (played > s->played_media) { s->played_media = played; }
            if (elapsed < span.frames) { break; }
            ++read;
        }
        atomic_store_explicit(&s->span_read, read, memory_order_release);
        s->played_valid = true;
    }
    i->played_media_frames = s->played_media;
    i->timeline_valid = valid && !i->timeline_overflow;
}
