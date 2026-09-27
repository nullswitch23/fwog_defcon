/* 8-char SoftAP alphabet, plus C6 NVS load/save (skipped in host tests). */
#include "pass_nvs.h"
#include <string.h>

#ifndef HOST_TEST
#include "esp_random.h"
#include "nvs.h"
#endif

static const char k_abc[] =
    "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";

bool fwog_pass_ok(const char *pass) {
    if (!pass || strlen(pass) != (size_t)FWOG_PASS_LEN) return false;
    for (int i = 0; i < FWOG_PASS_LEN; i++) {
        if (!strchr(k_abc, pass[i])) return false;
    }
    return true;
}

void fwog_pass_gen(char dst[FWOG_PASS_LEN + 1], uint32_t (*rnd)(void)) {
    for (int i = 0; i < FWOG_PASS_LEN; i++) {
        dst[i] = k_abc[rnd() % (sizeof k_abc - 1u)];
    }
    dst[FWOG_PASS_LEN] = '\0';
}

#ifndef HOST_TEST
static uint32_t idf_rnd(void) {
    return esp_random();
}

static bool nvs_load(const char *key, char dst[FWOG_PASS_LEN + 1]) {
    nvs_handle_t h;
    if (nvs_open(FWOG_PASS_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    char buf[FWOG_PASS_LEN + 1];
    size_t n = sizeof buf;
    memset(buf, 0, sizeof buf);
    esp_err_t e = nvs_get_str(h, key, buf, &n);
    nvs_close(h);
    if (e != ESP_OK || !fwog_pass_ok(buf)) return false;
    memcpy(dst, buf, FWOG_PASS_LEN + 1);
    return true;
}

static void nvs_save(const char *key, const char *pass) {
    nvs_handle_t h;
    if (nvs_open(FWOG_PASS_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    (void)nvs_set_str(h, key, pass);
    (void)nvs_commit(h);
    nvs_close(h);
}

void fwog_pass_nvs_ensure(const char *key, char dst[FWOG_PASS_LEN + 1]) {
    if (nvs_load(key, dst)) return;
    fwog_pass_gen(dst, idf_rnd);
    nvs_save(key, dst);
}

void fwog_pass_nvs_rotate(const char *key, char dst[FWOG_PASS_LEN + 1]) {
    fwog_pass_gen(dst, idf_rnd);
    nvs_save(key, dst);
}
#endif
