/* ESP32-C6 ROM download (esptool SLIP) over the Bottlenose header UART.
 *
 * Application UART1 matches the ROM (C6 TX=GPIO16 / RX=GPIO17). The older
 * TX17/RX16 app image could HELLO on OG GPIO8 but SWAP TX on GPIO9 never
 * reached C6 RX on this Orca. RP2040 UART1 cannot swap TX/RX on GPIO 8/9,
 * so download still auto-selects:
 *
 *   SWAP — PIO rom_tx on GPIO9, rom_rx on GPIO8
 *          (C6 GPIO17 app TX / GPIO16 ROM TX sit on opposite OG pins)
 *   APP  — hardware uart1 115200 8N1, no flow control
 *          (GPIO8 TX / GPIO9 RX)
 *
 * HOLD: this Orca ROM TX is on OG GPIO9 (C6 U0TXD=GPIO16). The running app
 * TXes on C6 GPIO17, which is OG GPIO8 — uart1 RX on GPIO9 cannot hear it.
 * App HELLO uses SWAP PIO RX on GPIO8. e8==0 && e9>0 still forces ROM APP
 * first. A SYNC fail tries SWAP.
 * After SYNC: drain leftover SLIP, optional GET_SECURITY_INFO (0x14), then
 * SPI_ATTACH with 8 zero bytes. Attach NAK is not fatal until FLASH_BEGIN
 * also fails. FLASH_BEGIN/DATA/END write a merged image at offset 0.
 */
#ifndef FWOG_ESP_ROM_FLASH_H
#define FWOG_ESP_ROM_FLASH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ESP_ROM_OK = 0,
    ESP_ROM_ERR_NOIMG,
    ESP_ROM_ERR_SYNC,
    ESP_ROM_ERR_ATTACH,
    ESP_ROM_ERR_BEGIN,
    ESP_ROM_ERR_WRITE,
    ESP_ROM_ERR_END
} esp_rom_err_t;

/* Both GPIO8 and GPIO9 inputs + pull-ups. No drive. */
void esp_rom_pins_listen(void);
void esp_rom_pins_begin(void); /* same as listen (HOLD) */
void esp_rom_pins_end(void);

/* True: try SWAP first. False: APP first.
 * e8==0 && e9>0 → APP (this Orca: ROM TX on OG GPIO9). Both 0 → SWAP. */
bool esp_rom_orient_swap_first(uint16_t e8, uint16_t e9);

/* `progress` is 0..100. Called often; kick the watchdog there.
 * e8/e9 are HOLD edge counts on GPIO8 / GPIO9 (ROM TX hear). */
esp_rom_err_t esp_rom_flash(const uint8_t *img, size_t len,
                            void (*progress)(unsigned pct),
                            uint16_t e8, uint16_t e9);

/* App UART: PIO RX on GPIO8; TX is SIO bitbang on GPIO9 (FPGA SWAP dirs).
 * ROM flashing still uses PIO TX. This Orca's ROM path was uart1 APP;
 * SWAP PIO TX on GPIO9 was never proven. io_dir_apply already leaves
 * GPIO9 as SIO OUT idle-high — bitbang drives that pad. */
bool esp_rom_swap_rx_begin(void);
bool esp_rom_swap_begin(void);
bool esp_rom_swap_tx_ready(void);
int  esp_rom_swap_try_rx(void); /* -1 if empty / not started */
void esp_rom_swap_tx(uint8_t b);

#endif
