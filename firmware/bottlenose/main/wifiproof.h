#pragma once

#include <stdbool.h>

typedef struct {
    void (*puts)(const char *s);
    void (*ap_stop)(void);
    bool (*ap_is_on)(void);
} wifiproof_io_t;

void wifiproof_init(const wifiproof_io_t *io);
bool wifiproof_handle_line(const char *line);
