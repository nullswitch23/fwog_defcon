#ifndef TG_PROTO_H
#define TG_PROTO_H
#include <stdint.h>

#define TG_MSG_CMD  0x4Au
#define TG_MSG_DATA 0x4Bu
#define TG_CHUNK    32u
#define TG_NPIN     4u

typedef struct {
    const char *tag;
    uint8_t     rx_gpio;
    uint8_t     hdr;
    uint8_t     hw;     /* 1 = UART1, 0 = PIO */
} tg_pin_t;

/* Header RX allowlist. Not SPI (FPGA) and not I2C (open-drain). */
static const tg_pin_t k_tg_pin[TG_NPIN] = {
    { "UART1",  9u,  5u, 1u },
    { "GP26",  26u, 14u, 0u },
    { "GP27",  27u,  3u, 0u },
    { "CTS",   10u,  7u, 0u },
};

#ifdef _MSC_VER
#pragma pack(push, 1)
#define TG_PACKED
#else
#define TG_PACKED __attribute__((packed))
#endif

typedef struct TG_PACKED {
    uint8_t  type;
    uint8_t  baud_i;
    uint8_t  hex;
    uint8_t  pin_i;
} tg_cmd_t;
_Static_assert(sizeof(tg_cmd_t) == 4, "tg_cmd_t");

typedef struct TG_PACKED {
    uint8_t type;
    uint8_t n;
    uint8_t _pad[2];
    uint8_t data[TG_CHUNK];
} tg_data_t;
_Static_assert(sizeof(tg_data_t) == 36, "tg_data_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
