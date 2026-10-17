#include "shannon_test.h"

static void known_adapter(const ShannonVector *v)
{
    uint8_t buffer[SHANNON_AP_MAX_PLAINTEXT];
    uint8_t mac[4];
    ShannonAdapter tx = {0}, rx = {0};
    CHECK(shannon_adapter_init(&tx, SHANNON_TX, v->key, v->key_size) == SHANNON_OK);
    CHECK(shannon_adapter_init(&rx, SHANNON_RX, v->key, v->key_size) == SHANNON_OK);
    /* White-box seed to exercise the full u32 nonce space without 2^32 packets. */
    tx.next_nonce = rx.next_nonce = vector_nonce(v);
    CHECK(shannon_adapter_begin(&tx) == SHANNON_OK);
    CHECK(shannon_adapter_begin(&rx) == SHANNON_OK);
    ShannonAdapter saved_rx = rx;
    memcpy(buffer, v->plaintext, v->size);
    for (size_t i = 0; i < v->size; ++i)
        CHECK(shannon_adapter_update(&tx, buffer + i, 1) == SHANNON_OK);
    CHECK(shannon_adapter_finish_tx(&tx, mac) == SHANNON_OK);
    CHECK(memcmp(buffer, v->ciphertext, v->size) == 0 && memcmp(mac, v->mac, 4) == 0);
    CHECK(memcmp(&rx, &saved_rx, sizeof(rx)) == 0);
    ShannonAdapter saved_tx = tx;
    size_t header = v->size < 3 ? v->size : 3;
    CHECK(shannon_adapter_update(&rx, buffer, header) == SHANNON_OK);
    CHECK(shannon_adapter_update(&rx, buffer + header, v->size - header) == SHANNON_OK);
    CHECK(shannon_adapter_finish_rx(&rx, mac) == SHANNON_OK);
    CHECK(memcmp(buffer, v->plaintext, v->size) == 0);
    CHECK(memcmp(&tx, &saved_tx, sizeof(tx)) == 0);
    shannon_adapter_clear(&tx); shannon_adapter_clear(&rx);
    CHECK(all_zero(&tx, sizeof(tx)) && all_zero(&rx, sizeof(rx)));
}

static void tamper(const ShannonVector *v)
{
    CHECK(v->size == 8 && v->mac_size == 4);
    for (size_t position = 0; position < v->size + 4; ++position) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            uint8_t bytes[8], mac[4];
            ShannonAdapter rx = {0};
            memcpy(bytes, v->ciphertext, sizeof(bytes));
            memcpy(mac, v->mac, sizeof(mac));
            if (position < v->size) bytes[position] ^= (uint8_t)(1u << bit);
            else mac[position - v->size] ^= (uint8_t)(1u << bit);
            CHECK(shannon_adapter_init(&rx, SHANNON_RX, v->key, v->key_size) == SHANNON_OK);
            rx.next_nonce = vector_nonce(v);
            CHECK(shannon_adapter_begin(&rx) == SHANNON_OK);
            CHECK(shannon_adapter_update(&rx, bytes, 3) == SHANNON_OK);
            CHECK(shannon_adapter_update(&rx, bytes + 3, 5) == SHANNON_OK);
            CHECK(shannon_adapter_finish_rx(&rx, mac) == SHANNON_AUTH_FAILED);
            CHECK(rx.phase == SHANNON_FAILED && all_zero(&rx.cipher, sizeof(rx.cipher)));
            CHECK(shannon_adapter_begin(&rx) == SHANNON_BAD_STATE);
            CHECK(shannon_adapter_update(&rx, bytes, 1) == SHANNON_BAD_STATE);
            CHECK(shannon_adapter_finish_rx(&rx, mac) == SHANNON_BAD_STATE);
            shannon_adapter_clear(&rx);
            CHECK(all_zero(&rx, sizeof(rx)));
        }
    }
}

static void lifecycle(const ShannonVector *first, const ShannonVector *next)
{
    ShannonAdapter tx = {0}, before = tx;
    uint8_t bytes[SHANNON_AP_MAX_PLAINTEXT + 1] = {0}, mac[4];
    CHECK(shannon_adapter_init(NULL, SHANNON_TX, first->key, 32) == SHANNON_INVALID_ARGUMENT);
    CHECK(shannon_adapter_init(&tx, (ShannonDirection)0, first->key, 32) == SHANNON_INVALID_ARGUMENT);
    CHECK(shannon_adapter_init(&tx, SHANNON_TX, NULL, 32) == SHANNON_INVALID_ARGUMENT);
    CHECK(shannon_adapter_init(&tx, SHANNON_TX, first->key, 31) == SHANNON_INVALID_ARGUMENT);
    CHECK(memcmp(&tx, &before, sizeof(tx)) == 0);
    CHECK(shannon_adapter_begin(NULL) == SHANNON_INVALID_ARGUMENT);
    CHECK(shannon_adapter_begin(&tx) == SHANNON_BAD_STATE);
    CHECK(shannon_adapter_init(&tx, SHANNON_TX, first->key, 32) == SHANNON_OK);
    CHECK(shannon_adapter_update(&tx, bytes, 1) == SHANNON_BAD_STATE);
    CHECK(shannon_adapter_finish_tx(&tx, mac) == SHANNON_BAD_STATE);
    const ShannonVector *sequence[] = {first, next};
    for (size_t i = 0; i < 2; ++i) {
        const ShannonVector *v = sequence[i];
        CHECK(shannon_adapter_begin(&tx) == SHANNON_OK);
        before = tx;
        CHECK(shannon_adapter_begin(&tx) == SHANNON_BAD_STATE);
        CHECK(shannon_adapter_update(&tx, NULL, 1) == SHANNON_INVALID_ARGUMENT);
        CHECK(shannon_adapter_update(&tx, bytes, SIZE_MAX) == SHANNON_LIMIT);
        CHECK(shannon_adapter_finish_rx(&tx, mac) == SHANNON_BAD_STATE);
        CHECK(shannon_adapter_finish_tx(&tx, NULL) == SHANNON_INVALID_ARGUMENT);
        CHECK(memcmp(&tx, &before, sizeof(tx)) == 0);
        CHECK(shannon_adapter_update(&tx, NULL, 0) == SHANNON_OK);
        memcpy(bytes, v->plaintext, v->size);
        CHECK(shannon_adapter_update(&tx, bytes, v->size) == SHANNON_OK);
        CHECK(shannon_adapter_finish_tx(&tx, mac) == SHANNON_OK);
        CHECK(memcmp(bytes, v->ciphertext, v->size) == 0 && memcmp(mac, v->mac, 4) == 0);
        CHECK(tx.next_nonce == i + 1);
    }
    CHECK(shannon_adapter_begin(&tx) == SHANNON_OK);
    CHECK(shannon_adapter_update(&tx, bytes, SHANNON_AP_MAX_PLAINTEXT - 1) == SHANNON_OK);
    before = tx;
    CHECK(shannon_adapter_update(&tx, bytes, 2) == SHANNON_LIMIT);
    CHECK(memcmp(&tx, &before, sizeof(tx)) == 0);
    CHECK(shannon_adapter_update(&tx, bytes, 1) == SHANNON_OK);
    CHECK(shannon_adapter_finish_tx(&tx, mac) == SHANNON_OK);
    tx.next_nonce = UINT32_MAX;
    CHECK(shannon_adapter_begin(&tx) == SHANNON_OK);
    CHECK(tx.next_nonce == UINT64_C(0x100000000));
    CHECK(shannon_adapter_finish_tx(&tx, mac) == SHANNON_OK);
    CHECK(shannon_adapter_begin(&tx) == SHANNON_NONCE_EXHAUSTED);
    CHECK(tx.phase == SHANNON_FAILED && all_zero(&tx.cipher, sizeof(tx.cipher)));
    CHECK(shannon_adapter_begin(&tx) == SHANNON_BAD_STATE);
    shannon_adapter_clear(&tx); shannon_adapter_clear(NULL);
    CHECK(all_zero(&tx, sizeof(tx)));
}

void test_shannon_adapter(const ShannonVector *vectors, size_t count)
{
    const ShannonVector *first = NULL, *next = NULL, *odd = NULL;
    for (size_t i = 0; i < count; ++i) {
        const ShannonVector *v = &vectors[i];
        if (v->key_size == 32 && v->nonce_size == 4 && v->mac_size == 4) known_adapter(v);
        if (strcmp(v->name, "frame-empty") == 0) first = v;
        if (strcmp(v->name, "frame-next") == 0) next = v;
        if (strcmp(v->name, "frame-odd") == 0) odd = v;
    }
    CHECK(first && next && odd);
    tamper(odd); lifecycle(first, next);
}
