#ifndef CM_STORE_H
#define CM_STORE_H
#include "cm_proto.h"
#include <stdbool.h>

void cm_store_init(void);
void cm_store_poll_cdc(void);
void cm_store_on_cmd(const cm_cmd_t *c);
void cm_store_push(void);

#endif
