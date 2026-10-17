#include "protocol/shannon_adapter.h"

void shannon_adapter_clear(ShannonAdapter *adapter)
{
    if (adapter) {
        volatile unsigned char *bytes = (volatile unsigned char *)adapter;
        for (size_t i = 0; i < sizeof(*adapter); ++i) bytes[i] = 0;
    }
}

ShannonResult shannon_adapter_init(ShannonAdapter *adapter, ShannonDirection direction,
                                   const void *key, size_t size)
{
    if (!adapter || !key || size != SHANNON_AP_KEY_SIZE ||
        (direction != SHANNON_TX && direction != SHANNON_RX)) return SHANNON_INVALID_ARGUMENT;
    shannon_adapter_clear(adapter);
    (void)shannon_key(&adapter->cipher, key, size);
    adapter->direction = direction;
    adapter->phase = SHANNON_IDLE;
    return SHANNON_OK;
}

ShannonResult shannon_adapter_begin(ShannonAdapter *adapter)
{
    if (!adapter) return SHANNON_INVALID_ARGUMENT;
    if (adapter->phase != SHANNON_IDLE) return SHANNON_BAD_STATE;
    if (adapter->next_nonce > UINT32_MAX) {
        shannon_adapter_clear(adapter);
        adapter->phase = SHANNON_FAILED;
        return SHANNON_NONCE_EXHAUSTED;
    }
    uint32_t value = (uint32_t)adapter->next_nonce;
    uint8_t nonce[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 8), (uint8_t)value};
    (void)shannon_nonce(&adapter->cipher, nonce, sizeof(nonce));
    ++adapter->next_nonce;
    adapter->bytes = 0;
    adapter->phase = SHANNON_ACTIVE;
    return SHANNON_OK;
}

ShannonResult shannon_adapter_update(ShannonAdapter *adapter, void *data, size_t size)
{
    if (!adapter || (!data && size)) return SHANNON_INVALID_ARGUMENT;
    if (adapter->phase != SHANNON_ACTIVE) return SHANNON_BAD_STATE;
    if (size > SHANNON_AP_MAX_PLAINTEXT - adapter->bytes) return SHANNON_LIMIT;
    if (adapter->direction == SHANNON_TX) (void)shannon_encrypt(&adapter->cipher, data, size);
    else (void)shannon_decrypt(&adapter->cipher, data, size);
    adapter->bytes += size;
    return SHANNON_OK;
}

ShannonResult shannon_adapter_finish_tx(ShannonAdapter *adapter, uint8_t mac[4])
{
    if (!adapter || !mac) return SHANNON_INVALID_ARGUMENT;
    if (adapter->phase != SHANNON_ACTIVE || adapter->direction != SHANNON_TX) return SHANNON_BAD_STATE;
    (void)shannon_finish(&adapter->cipher, mac, SHANNON_AP_MAC_SIZE);
    adapter->phase = SHANNON_IDLE;
    return SHANNON_OK;
}

ShannonResult shannon_adapter_finish_rx(ShannonAdapter *adapter, const uint8_t mac[4])
{
    if (!adapter || !mac) return SHANNON_INVALID_ARGUMENT;
    if (adapter->phase != SHANNON_ACTIVE || adapter->direction != SHANNON_RX) return SHANNON_BAD_STATE;
    uint8_t actual[SHANNON_AP_MAC_SIZE];
    (void)shannon_finish(&adapter->cipher, actual, sizeof(actual));
    volatile unsigned difference = 0;
    for (size_t i = 0; i < sizeof(actual); ++i) difference |= (unsigned)(actual[i] ^ mac[i]);
    volatile uint8_t *wipe = actual;
    for (size_t i = 0; i < sizeof(actual); ++i) wipe[i] = 0;
    if (difference) {
        shannon_adapter_clear(adapter);
        adapter->phase = SHANNON_FAILED;
        return SHANNON_AUTH_FAILED;
    }
    adapter->phase = SHANNON_IDLE;
    return SHANNON_OK;
}
