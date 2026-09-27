/* OpticClick inter-CPU + FatFs slot store (above VoltPet 0x4E). */
#ifndef OC_PROTO_H
#define OC_PROTO_H

#include <stdint.h>

#define OC_MSG_CMD   0x55u
#define OC_MSG_SLOTS 0x56u

#define OC_CMD_GET 0u

#define OC_MAGIC      0x4F434331u /* OCC1 */
#define OC_SLOTS      24u
#define OC_BRAND_LEN  12u
#define OC_FUNC_LEN   12u
#define OC_STORE_NAME "OPTIC.BIN"

#ifdef _MSC_VER
#pragma pack(push, 1)
#define OC_PACKED
#else
#define OC_PACKED __attribute__((packed))
#endif

typedef struct OC_PACKED {
    uint8_t type;
    uint8_t cmd;
} oc_cmd_t;
_Static_assert(sizeof(oc_cmd_t) == 2, "oc_cmd_t");

typedef struct OC_PACKED {
    uint32_t code;
    uint8_t  have;
    uint8_t  phy;   /* ir_phy_t; 0 = NEC. Replay calls ir_comm_set_phy. */
    uint8_t  _pad[2];
    char     brand[OC_BRAND_LEN];
    char     func[OC_FUNC_LEN];
} oc_slot_rec_t;
_Static_assert(sizeof(oc_slot_rec_t) == 32, "oc_slot_rec_t");

typedef struct OC_PACKED {
    uint32_t magic;
    oc_slot_rec_t slot[OC_SLOTS];
} oc_file_t;
_Static_assert(sizeof(oc_file_t) == 772, "oc_file_t");

typedef struct OC_PACKED {
    uint8_t  type; /* OC_MSG_SLOTS */
    uint8_t  _pad[3];
    uint32_t magic;
    oc_slot_rec_t slot[OC_SLOTS];
} oc_slots_msg_t;
_Static_assert(sizeof(oc_slots_msg_t) == 776, "oc_slots_msg_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
