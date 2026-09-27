#ifndef PH_PROTO_H
#define PH_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define PH_PACKED
#else
#define PH_PACKED __attribute__((packed))
#endif

#define PH_MSG_CMD 0x62u
#define PH_MSG_ST  0x63u
#define PH_MSG_LIB 0x64u
#define PH_MSG_LABEL 0x65u /* T9 name for one MAC; not the whole library */
#define PH_CMD_NOP    0u
#define PH_CMD_SCAN   1u
#define PH_CMD_FLASH  2u  /* on: 0 cancel, 1 hold-BOOT, 2 write */
#define PH_CMD_FILT   3u  /* on: 0 all, 1 named, 2 apple */
#define PH_CMD_CURSOR 4u  /* on: 0 up, 1 down */
#define PH_CMD_LOCK   5u  /* toggle lock on cursor row */
#define PH_CMD_FREEZE 6u  /* on: 0 live, 1 freeze visible list */
#define PH_CMD_LIBPIN 7u  /* on: library slot 0..PH_LIB_N-1, hunt that MAC */
#define PH_CMD_LIBSAVE 8u /* on: slot, store the current pin or cursor */
#define PH_CMD_LIBDEL  9u /* on: slot, clear that library entry */

#define PH_LIB_GET 0u
#define PH_LIB_SET 1u
#define PH_LIB_N   8u
#define PH_LIB_MAGIC 0x50484C31u /* PHL1 */
#define PH_LIB_NAME  "PHLIB.BIN"
#define PH_LIB_TMP   "PHLIB.TMP"

#define PH_LIBOP_IDLE 0u
#define PH_LIBOP_SAVE 1u /* FatFs write in progress */
#define PH_LIBOP_OK   2u
#define PH_LIBOP_FAIL 3u

#define PH_MAX     6u
#define PH_NAME_N  12u
#define PH_HIST    24u
#define PH_FILT_ALL   0u
#define PH_FILT_NAMED 1u
#define PH_FILT_APPLE 2u

#define PH_FLASH_IDLE  0u
#define PH_FLASH_HOLD  1u
#define PH_FLASH_SYNC  2u
#define PH_FLASH_WRITE 3u
#define PH_FLASH_OK    4u
#define PH_FLASH_FAIL  5u

#define PH_TREND_UNK   0u
#define PH_TREND_CLOSE 1u  /* burst envelope rising */
#define PH_TREND_HOLD  2u
#define PH_TREND_FAR   3u
#define PH_DEADBAND_DB 5   /* burst-peak vs prior peak; Find My is not CW */

typedef struct PH_PACKED {
    uint8_t type;
    uint8_t cmd;
    uint8_t on;
    uint8_t _pad;
} ph_cmd_t;
_Static_assert(sizeof(ph_cmd_t) == 4, "ph_cmd_t");

typedef struct PH_PACKED {
    int8_t  rssi;
    uint8_t apple;          /* 1 = Apple company ID 0x004C seen in ADV */
    uint8_t addr[6];
    char    name[PH_NAME_N];
} ph_dev_t;
_Static_assert(sizeof(ph_dev_t) == 20, "ph_dev_t");

typedef struct PH_PACKED {
    uint8_t  type;
    uint8_t  hello;
    uint8_t  scan_on;
    uint8_t  n;
    uint8_t  flash;
    uint8_t  pct;
    uint8_t  filter;
    uint8_t  cursor;
    uint8_t  locked;
    uint8_t  freeze;        /* 1 = MAC list snapshot; no live reshuffle */
    uint8_t  total;
    uint8_t  trend;         /* PH_TREND_*; burst envelope, not inter-packet */
    uint8_t  hist_n;
    uint8_t  lib_op;        /* PH_LIBOP_* — RAM pin book, not RSSI */
    int8_t   hist[PH_HIST]; /* pinned Find My / ADV RSSI bursts, oldest first */
    char     why[16];
    ph_dev_t dev[PH_MAX];
} ph_status_t;
_Static_assert(sizeof(ph_status_t) == 174, "ph_status_t");

typedef struct PH_PACKED {
    uint8_t have;
    uint8_t addr[6];
    uint8_t _pad;
    char    label[PH_NAME_N];
} ph_lib_ent_t;
_Static_assert(sizeof(ph_lib_ent_t) == 20, "ph_lib_ent_t");

typedef struct PH_PACKED {
    uint32_t magic;
    ph_lib_ent_t ent[PH_LIB_N];
} ph_lib_file_t;
_Static_assert(sizeof(ph_lib_file_t) == 164, "ph_lib_file_t");

typedef struct PH_PACKED {
    uint8_t  type; /* PH_MSG_LIB */
    uint8_t  cmd;  /* GET/SET */
    uint8_t  _pad[2];
    uint32_t magic;
    ph_lib_ent_t ent[PH_LIB_N];
} ph_lib_msg_t;
_Static_assert(sizeof(ph_lib_msg_t) == 168, "ph_lib_msg_t");

typedef struct PH_PACKED {
    uint8_t type; /* PH_MSG_LABEL */
    uint8_t _pad;
    uint8_t addr[6];
    char    label[PH_NAME_N];
} ph_label_msg_t;
_Static_assert(sizeof(ph_label_msg_t) == 20, "ph_label_msg_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
