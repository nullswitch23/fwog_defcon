/* KeeLoq hop-code encrypt and best-effort key recovery for FobReplay.
 * Standard 528-round NLF+LFSR. Recovery needs several known plaintext/cipher
 * pairs; with only two successive frames it usually fails and the app falls
 * back to PREDICT-CTR. */
#ifndef FOB_KEELOQ_H
#define FOB_KEELOQ_H
#include <stdbool.h>
#include <stdint.h>

uint32_t keeloq_encrypt(uint32_t plain, uint64_t key);
uint32_t keeloq_decrypt(uint32_t cipher, uint64_t key);

/* Try to recover a 64-bit key from up to 8 (plain,cipher) pairs. Returns
 * true when encrypt(key, plain[i])==cipher[i] for every supplied pair. */
bool keeloq_recover_key(const uint32_t *plain, const uint32_t *cipher,
                        unsigned npairs, uint64_t *key_out);

#endif
