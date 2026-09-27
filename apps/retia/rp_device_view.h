#ifndef RP_DEVICE_VIEW_H
#define RP_DEVICE_VIEW_H
#include "rp_proto.h"
#include <stdbool.h>

void rp_device_view_init(void);
/* Combo landings already own splash / LCD. */
void rp_device_view_attach(void);
void rp_device_view_paint(const rp_state_t *st);

#endif
