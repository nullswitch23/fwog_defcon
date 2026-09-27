#ifndef VP_PROTO_H
#define VP_PROTO_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define VP_PACKED
#else
#define VP_PACKED __attribute__((packed))
#endif

#define VP_MSG_CMD  0x4Cu
#define VP_MSG_ST   0x4Du
#define VP_MSG_RF   0x4Eu
#define VP_CMD_PULL 1u
#define VP_CMD_PUSH 2u
#define VP_MAGIC    0x56503247u  /* VP2G */
#define VP_NART     100u
#define VP_ART_BYTES 13u         /* 100 bits */
#define VP_NAME_N   12u
#define VP_SAVE_V2  36u          /* v002 voltpet.bin, no name */
#define VP_SAVE_CORE 35u         /* through art_bits; v002 pad was byte 35 */

typedef struct VP_PACKED {
    uint8_t  type;
    uint8_t  cmd;
    uint8_t  hunger;
    uint8_t  happy;
    uint8_t  energy;
    uint8_t  flags;      /* bit0 alive, bit1 sleeping */
    uint16_t xp;
    uint8_t  level;
    uint8_t  age;
    uint16_t fights_won;
    uint32_t steps;
    uint32_t magic;
    uint8_t  last_art;   /* 0..99, 0xFF none */
    uint8_t  art_count;
    uint8_t  art_bits[VP_ART_BYTES];
    char     name[VP_NAME_N];
    uint8_t  _pad;       /* species 0..4; 0xFF = unhatched, display rolls */
} vp_save_t;
_Static_assert(sizeof(vp_save_t) == 48, "vp_save_t wire size");
_Static_assert(VP_SAVE_CORE + 1u == VP_SAVE_V2, "v002 pad after core");

typedef struct VP_PACKED {
    uint8_t  type;
    uint8_t  ok;
    uint8_t  burst;      /* 1 on a rising RSSI edge */
    uint8_t  art_id;     /* 0..99, valid when burst */
    int16_t  rssi;
    uint16_t _pad;
    uint32_t hz;
} vp_rf_t;
_Static_assert(sizeof(vp_rf_t) == 12, "vp_rf_t wire size");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

static inline void vp_name_sanitize(char *name) {
    unsigned i, o = 0;
    if (name == 0) return;
    for (i = 0; i < VP_NAME_N - 1u && name[i]; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
        if (c >= 'A' && c <= 'Z') name[o++] = c;
    }
    memset(name + o, 0, VP_NAME_N - o);
}

/* Load a FatFs / link blob. v002 36-byte files keep stats; name is empty. */
static inline bool vp_save_from_bytes(vp_save_t *out, const void *p, size_t n) {
    if (out == 0 || p == 0) return false;
    memset(out, 0, sizeof *out);
    if (n == sizeof(vp_save_t)) {
        memcpy(out, p, n);
    } else if (n == VP_SAVE_V2) {
        memcpy(out, p, VP_SAVE_CORE);
    } else {
        return false;
    }
    if (out->magic != VP_MAGIC) return false;
    vp_name_sanitize(out->name);
    out->type = VP_MSG_ST;
    return true;
}

static inline bool vp_art_owned(const uint8_t *bits, unsigned id) {
    if (id >= VP_NART || bits == 0) return false;
    return (bits[id / 8u] & (uint8_t)(1u << (id % 8u))) != 0;
}

static inline void vp_art_give(uint8_t *bits, unsigned id) {
    if (id >= VP_NART || bits == 0) return;
    bits[id / 8u] |= (uint8_t)(1u << (id % 8u));
}

static inline uint8_t vp_level_for_xp(uint16_t xp) {
    unsigned lv = 1u + (unsigned)xp / 40u;
    if (lv > 20u) lv = 20u;
    return (uint8_t)lv;
}

#endif
