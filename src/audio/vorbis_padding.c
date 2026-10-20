#include "vorbis_internal.h"

AudioResult vorbis_end_trim(VorbisDecoder *d,uint64_t end,const uint8_t *tail,size_t bytes)
{
    if (end>d->config.source_bytes) { return AUDIO_CORRUPT; }
    uint64_t padding=d->config.source_bytes-end;
    if (!padding) { return AUDIO_OK; }
    if (padding>d->config.trailing_zero_limit) { return AUDIO_UNSUPPORTED; }
    if (padding>bytes || !tail) { return AUDIO_NEED_MORE; }
    for (size_t i=0;i<(size_t)padding;++i) { if (tail[i]) { return AUDIO_CORRUPT; } }
    d->config.source_bytes=end; return AUDIO_OK;
}
