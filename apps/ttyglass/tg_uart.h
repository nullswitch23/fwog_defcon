#ifndef TG_UART_H
#define TG_UART_H
#include <stdint.h>
#include <stdbool.h>

void tg_uart_apply(int pin_i, uint32_t baud);
void tg_uart_stop(void);
bool tg_uart_readable(void);
uint8_t tg_uart_getc(void);

#endif
