/* UART0 transport for the inter-CPU link, with RTS/CTS.
 *
 * Compiled per-CPU, like i2c_bus.c, because it resolves FWOG_LINK_UART and
 * PIN_LINK_* from that CPU's platform/board.h. Both CPUs use the identical
 * role mapping -- TX 0, RX 1, CTS 2, RTS 3 -- because the RP2040 pad mux
 * fixes it; the crossing is in the copper (the hardware record).
 *
 * 12.5 Mbaud is clk_peri 200 MHz / 16, the PL011 maximum, at a divisor of
 * exactly 1.0. There is no margin. The rate has never been measured on the
 * inter-CPU trace; if it proves marginal the ladder is 10 Mbaud, then
 * 8.33 Mbaud (both via the fractional divisor), then 6.25 Mbaud at integer
 * divisor 2. `fw build --baud N` selects one without editing anything.
 *
 * The divisor model is pure and host-tested, because the SDK's clamping
 * behavior at the ceiling is silent: ask for 20 Mbaud and uart_init()
 * cheerfully configures 12.5 and returns. */
#ifndef FWOG_LINK_UART_H
#define FWOG_LINK_UART_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/clocks.h"

#ifndef FWOG_LINK_BAUD
#define FWOG_LINK_BAUD 12500000u
#endif

/* The rate a PL011 clocked at clk_hz actually produces when asked for
 * `baud`, replicating the SDK's uart_set_baudrate() arithmetic exactly
 * (including its IBRD==0 clamp). Returns 0 for degenerate inputs. */
uint32_t fwog_uart_actual_baud(uint32_t clk_hz, uint32_t baud);

/* True when the achievable rate is within 1% of the requested one. 1% is
 * the usual UART framing budget: the receiver samples mid-bit and drifts
 * over ten bit times, so beyond about 2% the stop bit lands in the wrong
 * place. */
bool fwog_uart_baud_ok(uint32_t clk_hz, uint32_t baud);

#ifndef HOST_TEST
/* Bring up UART0 with RTS/CTS at `baud`. Returns false and leaves the UART
 * down if the rate is not reachable from the current clk_peri -- callers
 * must check, because silently running at the wrong rate is worse than not
 * running.
 *
 * Must run after fwog_clocks_init(): the divisor comes from
 * clock_get_hz(clk_peri). That tracks clk_sys only because the board header
 * sets PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK -- if that is ever lost,
 * clk_peri falls to 48 MHz and this function correctly REFUSES 12.5 Mbaud
 * rather than silently running at a quarter rate. See the hardware record. */
bool fwog_link_uart_init(uint32_t baud);

/* Quiesce before handing the CPU to an application. */
void fwog_link_uart_deinit(void);

/* Blocking write. At 12.5 Mbaud a full 4 KB DATA frame is ~3.3 ms. */
void fwog_link_uart_write(const void *data, size_t len);

/* Non-blocking single byte. Returns false when the FIFO is empty.
 *
 * Call this in a tight loop during a transfer. At 12.5 Mbaud a byte arrives
 * every 800 ns and the PL011 FIFO holds 32, so the whole RX budget is about
 * 25 us. Hardware RTS covers the gaps, but only if the FIFO is drained
 * promptly enough for RTS to reassert before the peer stalls. */
bool fwog_link_uart_read(uint8_t *b);

/* Frame `payload` with fwog_link_encode() and write it. Returns false if
 * the payload exceeds FWOG_LINK_MAX_PAYLOAD. */
bool fwog_link_uart_send_frame(const void *payload, size_t len);
#else
/* ogemu: display-side UART0 is a byte queue the world bus can fill. */
bool fwog_link_uart_init(uint32_t baud);
void fwog_link_uart_deinit(void);
void fwog_link_uart_write(const void *data, size_t len);
bool fwog_link_uart_read(uint8_t *b);
bool fwog_link_uart_send_frame(const void *payload, size_t len);
#endif

#endif
