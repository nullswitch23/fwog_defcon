#ifndef GB_PROTO_H
#define GB_PROTO_H

#include <stdint.h>

#define GB_MSG_CMD  0x71u
#define GB_CMD_DUMP 1u

typedef struct {
    uint8_t type;
    uint8_t cmd;
} gb_cmd_t;

#endif
