#ifndef FWOG_PASS_NVS_H
#define FWOG_PASS_NVS_H

#include <stdbool.h>
#include <stdint.h>

#define FWOG_PASS_LEN        8
#define FWOG_PASS_NVS_NS     "fwog"
#define FWOG_PASS_KEY_FERRY  "ferry"
#define FWOG_PASS_KEY_ARENA  "arena"

bool fwog_pass_ok(const char *pass);
void fwog_pass_gen(char dst[FWOG_PASS_LEN + 1], uint32_t (*rnd)(void));

#ifndef HOST_TEST
void fwog_pass_nvs_ensure(const char *key, char dst[FWOG_PASS_LEN + 1]);
void fwog_pass_nvs_rotate(const char *key, char dst[FWOG_PASS_LEN + 1]);
#endif

#endif
