#ifndef FWOG_BN_HID_H
#define FWOG_BN_HID_H

#include <stdbool.h>
#include <stdint.h>

void fwog_hid_register(void);
void fwog_hid_set_note(void (*cb)(const char *line));
void fwog_hid_start(void);
void fwog_hid_stop(void);
void fwog_hid_forget(void);
bool fwog_hid_on(void);
bool fwog_hid_connected(void);
bool fwog_hid_locked(void);
void fwog_hid_key(uint8_t keycode, int down);
void fwog_hid_cc(uint16_t usage, int down);
void fwog_hid_set_battery(uint8_t pct);

#endif
