#ifndef RP_MODE_H
#define RP_MODE_H
#include <stdbool.h>

typedef enum {
    RP_MODE_HIZ = 0,
    RP_MODE_DIO,
    RP_MODE_I2C,
    RP_MODE_UART,
    RP_MODE_SPI,
    RP_MODE_COUNT
} rp_mode_t;

const char *rp_mode_name(rp_mode_t mode);
rp_mode_t    rp_mode_from_name(const char *name);
/* False if `name` is not HIZ/DIO/I2C/UART/SPI (trailing space ignored). */
bool         rp_mode_parse(const char *name, rp_mode_t *out);

#endif
