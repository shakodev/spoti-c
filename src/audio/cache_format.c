#include "cache_internal.h"

uint32_t cache_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint64_t cache_u64(const uint8_t *p) { return cache_u32(p) | ((uint64_t)cache_u32(p + 4) << 32); }
void cache_set32(uint8_t *p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) { p[i] = (uint8_t)(n >> (8 * i)); } }
void cache_set64(uint8_t *p, uint64_t n) { cache_set32(p, (uint32_t)n); cache_set32(p + 4, (uint32_t)(n >> 32)); }
uint32_t cache_checksum(const void *data, size_t bytes)
{
    const uint8_t *p = data; uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < bytes; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) { crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1))); }
    }
    return ~crc;
}
void cache_encode(uint8_t h[64], const CacheEntry *e)
{
    memset(h, 0, 64); cache_set32(h, UINT32_C(0x314b4c42)); cache_set32(h + 4, 1);
    memcpy(h + 8, e->file_id, 20); cache_set32(h + 28, e->bytes); cache_set64(h + 32, e->offset);
    cache_set64(h + 40, e->total); cache_set32(h + 48, e->checksum); cache_set64(h + 56, e->stamp);
    cache_set32(h + 52, cache_checksum(h, 64));
}
bool cache_decode(const uint8_t h[64], CacheEntry *e)
{
    uint8_t copy[64]; memcpy(copy, h, 64); cache_set32(copy + 52, 0);
    if (cache_u32(h) != UINT32_C(0x314b4c42) || cache_u32(h + 4) != 1 ||
        cache_u32(h + 52) != cache_checksum(copy, 64)) { return false; }
    CacheEntry value = {0}; memcpy(value.file_id, h + 8, 20); value.bytes = cache_u32(h + 28);
    value.offset = cache_u64(h + 32); value.total = cache_u64(h + 40); value.checksum = cache_u32(h + 48);
    value.stamp = cache_u64(h + 56);
    if (value.offset % AUDIO_CACHE_BLOCK != 0 || value.total <= value.offset || value.bytes == 0 || value.bytes > AUDIO_CACHE_BLOCK ||
        value.bytes != (value.total - value.offset < AUDIO_CACHE_BLOCK ? (size_t)(value.total - value.offset) : AUDIO_CACHE_BLOCK)) { return false; }
    value.valid = true; *e = value; return true;
}
