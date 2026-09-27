#ifndef FWOG_BN_FLASH_H
#define FWOG_BN_FLASH_H

#include <stdbool.h>
#include <stdint.h>
#include "bottlenose/esp_rom_flash.h"
#include "gpio/breakout.h"

/* Shared Bottlenose C6 ROM-flash state machine. Numbers match LanFerry's
 * LF_FLASH_* so existing display screens keep working. */
#define FWOG_BN_FLASH_IDLE  0u
#define FWOG_BN_FLASH_HOLD  1u
#define FWOG_BN_FLASH_SYNC  2u
#define FWOG_BN_FLASH_WRITE 3u
#define FWOG_BN_FLASH_OK    4u
#define FWOG_BN_FLASH_FAIL  5u

#define FWOG_BN_BAUD 115200u

typedef struct {
    uint8_t  flash;
    uint8_t  pct;
    bool     rom_pins;
    char     why[16];
    uint16_t e8;     /* GPIO8 edges while HOLD (ROM TX on C6 GPIO16) */
    uint16_t e9;     /* GPIO9 edges while HOLD (ROM TX on C6 GPIO17) */
    uint8_t  last8;
    uint8_t  last9;
} fwog_bn_flash_t;

typedef void (*fwog_bn_flash_cb_t)(const fwog_bn_flash_t *st, void *ctx);

void fwog_bn_uart_app_init(void);
/* FPGA+expander+pads: hear C6 app TX on GPIO8, drive C6 RX on GPIO9. */
fwog_io_result_t fwog_bn_io_swap(void);
/* FPGA+expander+pads for header UART1 as OG TX / C6 TX on GPIO9, no flow.
 * ROM APP orientation (C6 U0TXD on GPIO9). Not the running-app HELLO path. */
fwog_io_result_t fwog_bn_io_app(void);
/* Both UART data pins as inputs (HOLD / hello-hunt). */
fwog_io_result_t fwog_bn_io_listen(void);
/* PIO RX on GPIO8 at 115200; GPIO9 stays an input for edge counts. */
void fwog_bn_uart_listen(void);
/* Full SWAP after HELLO: FPGA GPIO9 out, PIO RX GPIO8, SIO bitbang TX. */
bool fwog_bn_uart_swap_init(void);
bool fwog_bn_last_rx_pio(void);
int fwog_bn_getc(void); /* -1 if empty */
void fwog_bn_write(const fwog_bn_flash_t *st, const char *s);

/* Running-app UART after the ROM-pin C6 image: OG TX GPIO8 / RX GPIO9,
 * FPGA dirs with no RTS. This is the path that delivered BN BLE START. */
fwog_io_result_t fwog_bn_link_app(void);

/* Arm: both UART lines inputs, HOLD. Caller should stop AP/BLE first.
 * Fails with NOIMG if fwog_c6_image_size < 256. */
void fwog_bn_flash_arm(fwog_bn_flash_t *st, fwog_bn_flash_cb_t cb, void *ctx);
/* Count GPIO8/GPIO9 edges while HOLD (IRQ); updates why as "8=%u 9=%u". */
void fwog_bn_flash_poll(fwog_bn_flash_t *st);
void fwog_bn_flash_go(fwog_bn_flash_t *st, fwog_bn_flash_cb_t cb, void *ctx);
void fwog_bn_flash_cancel(fwog_bn_flash_t *st, fwog_bn_flash_cb_t cb, void *ctx);

#endif
