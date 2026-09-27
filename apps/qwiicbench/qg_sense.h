#ifndef QG_SENSE_H
#define QG_SENSE_H
#include "qg_proto.h"
#include <stddef.h>
#include <stdint.h>

/* Fill up to QG_MAX channels from a scanned address list. Returns n. */
unsigned qg_fill(qg_chan_t *out, unsigned max,
                 const uint8_t *addr, unsigned naddr);

#endif
