#ifndef FWOG_FERRY_H
#define FWOG_FERRY_H
#include "esp_netif.h"
#include <stdbool.h>
#include <stddef.h>

/* 124 KB mailbox. Live pipe is a ring and is not limited by this. */
#define FERRY_POOL_MAX (124 * 1024)

void ferry_init(void (*puts)(const char *), esp_netif_t *ap_netif);
void ferry_ap_start(void);
void ferry_ap_stop(void);
void ferry_ap_wipe(void);
void ferry_stat(void);
void ferry_hello(void);
bool ferry_ap_is_on(void);
size_t ferry_pool_max(void);

#endif
