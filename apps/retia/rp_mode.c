#include "rp_mode.h"
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

static const char *k_names[RP_MODE_COUNT] = {
    "HIZ", "DIO", "I2C", "UART", "SPI"
};

const char *rp_mode_name(rp_mode_t mode) {
    if ((unsigned)mode >= RP_MODE_COUNT) return "?";
    return k_names[mode];
}

static void upper_copy(char *dst, size_t n, const char *src) {
    size_t i = 0;
    while (src[i] && i + 1u < n) {
        dst[i] = (char)toupper((unsigned char)src[i]);
        i++;
    }
    dst[i] = '\0';
}

bool rp_mode_parse(const char *name, rp_mode_t *out) {
    char up[16];
    size_t n;
    if (!name || !out) return false;
    upper_copy(up, sizeof up, name);
    n = strlen(up);
    while (n > 0u && (up[n - 1u] == ' ' || up[n - 1u] == '\t')) {
        up[--n] = '\0';
    }
    if (n == 0u) return false;
    for (unsigned i = 0; i < RP_MODE_COUNT; i++) {
        if (strcmp(up, k_names[i]) == 0) {
            *out = (rp_mode_t)i;
            return true;
        }
    }
    return false;
}

rp_mode_t rp_mode_from_name(const char *name) {
    rp_mode_t m = RP_MODE_HIZ;
    if (!rp_mode_parse(name, &m)) return RP_MODE_HIZ;
    return m;
}
