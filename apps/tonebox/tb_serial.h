#ifndef TB_SERIAL_H
#define TB_SERIAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Display CPU: link to main for hang-up relay. */
bool tb_serial_display_init(void);
void tb_serial_display_poll(void);
void tb_serial_display_attach(void);
void tb_serial_display_set_pump(void (*fn)(void));
void tb_serial_display_on_frame(const uint8_t *buf, size_t n);
int tb_serial_hangup(unsigned timeout_ms);

#ifndef HOST_TEST
/* Main CPU: breakout UART1 @ 9600 — never UART0 (inter-CPU link). */
void tb_serial_main_init(void);
void tb_serial_main_uart_init(void);
void tb_serial_main_poll(void);
bool tb_serial_main_handle(const uint8_t *buf, size_t n);
#endif

#endif
