#ifndef QG_NICK_H
#define QG_NICK_H
#include "qg_proto.h"
#include <stdint.h>

#define QG_NICK_CAP 16u

/* RAM nicknames keyed by 7-bit I2C address. Lost on reboot (no FatFs). */
void qg_nick_set(uint8_t addr, const char *name);
const char *qg_nick_get(uint8_t addr); /* NULL when unset */

#endif
