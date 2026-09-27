/* On-disk capture convention for DiskGlass and the apps that write it.
 *
 * Pure: no SDK, no FatFs. Host-tested in tests/test_diskglass_file.c.
 * Volume is main's 8 MB FatFs. Per-app folders, 8.3-friendly names:
 *
 *   /talkclip/CLIP0001.RAW     8 kHz int16 LE PCM, DG header then samples
 *   /trail/WALK0001.CSV        InertialTrail pedometer/map log
 *   /trailrf/TRAIL001.CSV      text, first line "# diskglass trailrf"
 *   /ismburst/BURST001.BIN     ISMburst's IBST blob (also ismburst.bin)
 *   /fobreplay/FOB0001.BIN     DG header + OOK edge timings
 *   /fobreplay/HONDA.BIN       optional T9 8.3 name (FobReplay 006)
 *
 * DiskGlass browses; TalkClip playback is PCM over the link to the
 * display I2S speaker. RF files show metadata; DiskGlass can ASK-TX an
 * OOK blob. StickPeek (USB host) is not this. */
#ifndef DG_FILE_H
#define DG_FILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DG_MAGIC      0x31474644u  /* 'DGF1' */
#define DG_VER        1u
#define DG_IBST_MAGIC 0x54534249u  /* 'IBST' — apps/ismburst ib_store_file_t */

#define DG_KIND_UNKNOWN 0u
#define DG_KIND_PCM16   1u
#define DG_KIND_CSV     2u
#define DG_KIND_OOK     3u
#define DG_KIND_IBST    4u
#define DG_KIND_TEXT    5u
#define DG_KIND_WASM    6u

#define DG_APP_LEN 12u

#ifdef _MSC_VER
#pragma pack(push, 1)
#define DG_PACKED
#else
#define DG_PACKED __attribute__((packed))
#endif

typedef struct DG_PACKED {
    uint32_t magic;
    uint16_t ver;
    uint16_t kind;
    uint32_t payload_bytes;
    uint32_t sample_hz;
    uint32_t freq_hz;
    uint32_t duration_us;
    int16_t  peak_rssi;
    uint16_t edges;
    uint8_t  first_level;
    uint8_t  radio;
    uint16_t flags;
    char     app[DG_APP_LEN];
    uint8_t  _pad[4];
} dg_hdr_t;
_Static_assert(sizeof(dg_hdr_t) == 48, "dg_hdr_t");

/* Lead-in for DG_KIND_OOK payload (then `n` little-endian uint16 durations). */
typedef struct DG_PACKED {
    uint16_t n;
    uint8_t  first_level;
    uint8_t  radio;
} dg_ook_lead_t;
_Static_assert(sizeof(dg_ook_lead_t) == 4, "dg_ook_lead_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

void dg_hdr_init(dg_hdr_t *h, uint16_t kind, const char *app);
bool dg_hdr_valid(const dg_hdr_t *h);

/* Classify a file from its first bytes. n may be a prefix. */
uint16_t dg_sniff(const void *buf, size_t n);

const char *dg_kind_label(uint16_t kind);

/* CLIP0007.RAW + "CLIP" -> 7. 0 if the name does not match. */
unsigned dg_index_from_name(const char *name, const char *prefix);

/* "CLIP", 7, "RAW" -> "CLIP0007.RAW" (4-digit). */
void dg_format_name(char *out, size_t cap, const char *prefix,
                    unsigned idx, const char *ext);

bool dg_join_path(char *out, size_t cap, const char *dir, const char *name);

uint32_t dg_pcm_duration_ms(uint32_t payload_bytes, uint32_t sample_hz);

#endif
