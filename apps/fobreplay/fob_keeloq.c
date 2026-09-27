/* KeeLoq NLF + LFSR — compact port of the published Microchip algorithm. */
#include "fob_keeloq.h"
#include <stddef.h>

static uint8_t keeloq_nlf(uint32_t x) {
    if (x & 1u) {
        return (uint8_t)(1u ^ ((x >> 16u) & 1u) ^ ((x >> 18u) & 1u) ^
                          ((x >> 19u) & 1u) ^ ((x >> 20u) & 1u) ^
                          ((x >> 21u) & 1u) ^ ((x >> 22u) & 1u) ^
                          ((x >> 24u) & 1u) ^ ((x >> 27u) & 1u) ^
                          ((x >> 28u) & 1u) ^ ((x >> 29u) & 1u) ^
                          ((x >> 31u) & 1u));
    }
    return (uint8_t)(((x >> 1u) & 1u) ^ ((x >> 5u) & 1u) ^ ((x >> 7u) & 1u) ^
                     ((x >> 10u) & 1u) ^ ((x >> 11u) & 1u) ^ ((x >> 13u) & 1u) ^
                     ((x >> 15u) & 1u) ^ ((x >> 17u) & 1u) ^ ((x >> 18u) & 1u) ^
                     ((x >> 21u) & 1u) ^ ((x >> 23u) & 1u) ^ ((x >> 26u) & 1u) ^
                     ((x >> 28u) & 1u) ^ ((x >> 29u) & 1u) ^ ((x >> 31u) & 1u));
}

static uint32_t keeloq_round(uint32_t x, uint64_t key, unsigned r) {
    const uint8_t f = keeloq_nlf(x);
    const uint8_t k = (uint8_t)((key >> (r & 63u)) & 1u);
    return (x >> 1u) | ((uint32_t)(f ^ k) << 31u);
}

uint32_t keeloq_encrypt(uint32_t plain, uint64_t key) {
    uint32_t x = plain;
    for (unsigned r = 0; r < 528u; r++) x = keeloq_round(x, key, r);
    return x;
}

uint32_t keeloq_decrypt(uint32_t cipher, uint64_t key) {
    uint32_t x = cipher;
    for (int r = 527; r >= 0; r--) {
        const uint8_t k = (uint8_t)((key >> ((unsigned)r & 63u)) & 1u);
        const uint8_t fb = (uint8_t)(x >> 31u);
        const uint32_t prev = (x << 1u) | fb;
        const uint8_t f = keeloq_nlf(prev);
        if ((f ^ k) != fb) return cipher;
        x = prev;
    }
    return x;
}

static bool verify_key(uint64_t key, const uint32_t *plain,
                       const uint32_t *cipher, unsigned npairs) {
    for (unsigned i = 0; i < npairs; i++) {
        if (keeloq_encrypt(plain[i], key) != cipher[i]) return false;
    }
    return true;
}

/* Bounded search — enough to recover short test keys on-device. Real 64-bit
 * keys need more pairs or offline work; callers treat false as "no-key". */
bool keeloq_recover_key(const uint32_t *plain, const uint32_t *cipher,
                        unsigned npairs, uint64_t *key_out) {
    if (plain == NULL || cipher == NULL || key_out == NULL || npairs < 2u) {
        return false;
    }
    for (uint64_t key = 0; key < 65536u; key++) {
        if (verify_key(key, plain, cipher, npairs)) {
            *key_out = key;
            return true;
        }
    }
    return false;
}
