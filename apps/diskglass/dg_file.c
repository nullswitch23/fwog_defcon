#include "dg_file.h"
#include <stdio.h>
#include <string.h>

void dg_hdr_init(dg_hdr_t *h, uint16_t kind, const char *app) {
    if (!h) return;
    memset(h, 0, sizeof *h);
    h->magic = DG_MAGIC;
    h->ver = DG_VER;
    h->kind = kind;
    if (app) {
        strncpy(h->app, app, DG_APP_LEN - 1u);
        h->app[DG_APP_LEN - 1u] = '\0';
    }
}

bool dg_hdr_valid(const dg_hdr_t *h) {
    if (!h) return false;
    if (h->magic != DG_MAGIC) return false;
    if (h->ver != DG_VER) return false;
    if (h->kind == DG_KIND_UNKNOWN || h->kind > DG_KIND_IBST) return false;
    return true;
}

static bool looks_csv(const uint8_t *p, size_t n) {
    if (n < 8u) return false;
    if (p[0] == '#') {
        /* "# diskglass …" or any comment-then-CSV. */
        unsigned i;
        for (i = 1; i < n && i < 80u; i++) {
            if (p[i] == ',') return true;
            if (p[i] == '\n' || p[i] == '\r') break;
        }
        for (; i < n && i < 160u; i++) {
            if (p[i] == ',') return true;
        }
        return false;
    }
    /* Bare "step,rssi_dbm,freq_hz" */
    if (n >= 4u && p[0] == 's' && p[1] == 't' && p[2] == 'e' && p[3] == 'p') {
        unsigned i;
        for (i = 4; i < n && i < 40u; i++) {
            if (p[i] == ',') return true;
        }
    }
    return false;
}

uint16_t dg_sniff(const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t magic;
    if (!p || n < 4u) return DG_KIND_UNKNOWN;
    memcpy(&magic, p, 4);
    if (magic == DG_MAGIC) {
        dg_hdr_t h;
        if (n < sizeof h) return DG_KIND_UNKNOWN;
        memcpy(&h, p, sizeof h);
        if (!dg_hdr_valid(&h)) return DG_KIND_UNKNOWN;
        return h.kind;
    }
    if (magic == DG_IBST_MAGIC) return DG_KIND_IBST;
    if (n >= 4u && p[0] == 0u && p[1] == 'a' && p[2] == 's' && p[3] == 'm')
        return DG_KIND_WASM;
    if (looks_csv(p, n)) return DG_KIND_CSV;
    {
        size_t i, bad = 0;
        if (n == 0u) return DG_KIND_UNKNOWN;
        for (i = 0; i < n; i++) {
            uint8_t c = p[i];
            if (c == 9u || c == 10u || c == 13u) continue;
            if (c >= 32u && c <= 126u) continue;
            bad++;
        }
        if (bad * 4u <= n) return DG_KIND_TEXT;
    }
    return DG_KIND_UNKNOWN;
}

const char *dg_kind_label(uint16_t kind) {
    switch (kind) {
    case DG_KIND_PCM16: return "PCM 8 kHz";
    case DG_KIND_CSV:   return "CSV";
    case DG_KIND_OOK:   return "OOK burst";
    case DG_KIND_IBST:  return "ISMburst";
    case DG_KIND_TEXT:  return "text";
    case DG_KIND_WASM:  return "WASM";
    default:            return "file";
    }
}

unsigned dg_index_from_name(const char *name, const char *prefix) {
    size_t plen;
    unsigned i, n;
    if (!name || !prefix || !prefix[0]) return 0;
    plen = strlen(prefix);
    for (i = 0; i < plen; i++) {
        char a = name[i], b = prefix[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return 0;
    }
    if (name[plen] < '0' || name[plen] > '9') return 0;
    n = 0;
    for (i = (unsigned)plen; name[i] >= '0' && name[i] <= '9'; i++) {
        n = n * 10u + (unsigned)(name[i] - '0');
        if (n > 9999u) return 0;
    }
    return n;
}

void dg_format_name(char *out, size_t cap, const char *prefix,
                    unsigned idx, const char *ext) {
    if (!out || cap == 0u) return;
    if (!prefix) prefix = "FILE";
    if (!ext) ext = "BIN";
    if (idx > 9999u) idx = 9999u;
    snprintf(out, cap, "%s%04u.%s", prefix, idx, ext);
}

bool dg_join_path(char *out, size_t cap, const char *dir, const char *name) {
    size_t dlen, nlen;
    if (!out || cap < 2u || !name) return false;
    if (!dir || !dir[0] || (dir[0] == '/' && dir[1] == '\0')) {
        if (cap < strlen(name) + 2u) return false;
        out[0] = '/';
        strncpy(out + 1, name, cap - 2u);
        out[cap - 1u] = '\0';
        return true;
    }
    dlen = strlen(dir);
    nlen = strlen(name);
    if (dlen + 1u + nlen + 1u > cap) return false;
    memcpy(out, dir, dlen);
    out[dlen] = '/';
    memcpy(out + dlen + 1u, name, nlen + 1u);
    return true;
}

uint32_t dg_pcm_duration_ms(uint32_t payload_bytes, uint32_t sample_hz) {
    if (sample_hz == 0u) sample_hz = 8000u;
    /* int16 mono: 2 bytes/sample */
    return (uint32_t)(((uint64_t)payload_bytes * 1000u) / (2u * sample_hz));
}
