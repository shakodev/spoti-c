#ifndef SPOTIC_DECRYPT_TEST_H
#define SPOTIC_DECRYPT_TEST_H
#include "audio/decrypt_stream.h"
#include "audio/range_internal.h"
#include "protocol/media_internal.h"
#include "protocol/crypto_internal.h"
#include "support/test_check.h"

enum { DT_FILE_BYTES = 2 * RANGE_BLOCK_BYTES + 333 };
typedef struct DecryptTest {
    RangeFetcher ranges; MediaResolver media;
    DecryptStream stream; MediaSource source;
    uint8_t pool[RANGE_BLOCKS * RANGE_BLOCK_BYTES];
    uint8_t scratch[DECRYPT_SCRATCH_MAX], output[DECRYPT_SCRATCH_MAX + 2];
} DecryptTest;
extern DecryptTest dt;
extern uint8_t dt_cipher[DT_FILE_BYTES], dt_carry16[96], dt_carry32[96];
void dt_reset(void);
uint8_t dt_plain(uint64_t offset);
void dt_check_zero(const void *, size_t);
void dt_read(uint64_t logical, size_t bytes);
void dt_lifecycle_tests(void);
void dt_error_tests(void);
#endif
