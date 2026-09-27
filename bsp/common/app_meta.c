#include "common/app_meta.h"
#include <string.h>

uint32_t fwog_app_meta_crc(const fwog_app_meta_t *m) {
    return fwog_crc32(m, offsetof(fwog_app_meta_t, meta_crc32));
}

bool fwog_app_meta_valid(const fwog_app_meta_t *m) {
    if (m->magic != FWOG_APP_META_MAGIC) return false;
    if (m->size == 0u || m->size > FWOG_APP_MAX_SIZE) return false;
    if (!fwog_str_bounded(m->version, FWOG_APP_VERSION_LEN)) return false;
    return m->meta_crc32 == fwog_app_meta_crc(m);
}

void fwog_app_meta_fill(fwog_app_meta_t *m, uint32_t size, uint32_t crc32,
                        uint32_t build_ts, const char *version) {
    memset(m, 0, sizeof *m);
    m->magic    = FWOG_APP_META_MAGIC;
    m->size     = size;
    m->crc32    = crc32;
    m->build_ts = build_ts;
    /* strncpy would leave version[] unterminated at exactly 32 chars. The
       memset above already zeroed it, so copy at most LEN-1. */
    size_t n = strlen(version);
    if (n > FWOG_APP_VERSION_LEN - 1u) n = FWOG_APP_VERSION_LEN - 1u;
    memcpy(m->version, version, n);
    m->meta_crc32 = fwog_app_meta_crc(m);
}
