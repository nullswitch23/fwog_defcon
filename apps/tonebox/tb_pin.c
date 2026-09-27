#include "tb_pin.h"
#include <stdio.h>
#include <string.h>

static int append(char *out, size_t cap, size_t *pos, const char *s) {
    size_t need;
    if (!out || !pos || !s) return 0;
    need = strlen(s);
    if (*pos + need >= cap) return 0;
    memcpy(out + *pos, s, need);
    *pos += need;
    out[*pos] = '\0';
    return 1;
}

bool tb_pin_generate(const char *dial_body, unsigned digits, unsigned redial_every,
                     char *out, size_t cap, size_t *out_len) {
    unsigned max_i, attempts;
    size_t pos = 0;
    char line[64];

    if (!out || !out_len || !dial_body) return false;
    if (digits != 2u && digits != 3u) return false;
    attempts = redial_every ? redial_every : 3u;
    max_i = (digits == 2u) ? 99u : 999u;

    for (unsigned i = 0; i <= max_i; i++) {
        if (i % attempts == 0u) {
            if (!append(out, cap, &pos, dial_body)) return false;
        }
        {
            char pin[4];
            if (digits == 2u) snprintf(pin, sizeof pin, "%02u", i);
            else snprintf(pin, sizeof pin, "%03u", i);
            for (unsigned d = 0; pin[d]; d++) {
                snprintf(line, sizeof line, "D\t%c\t100\t100\n", pin[d]);
                if (!append(out, cap, &pos, line)) return false;
            }
        }
        if (!append(out, cap, &pos, "D\t#\t100\t100\n")) return false;
        if (!append(out, cap, &pos, "~\t1000\n")) return false;
    }
    *out_len = pos;
    return true;
}
