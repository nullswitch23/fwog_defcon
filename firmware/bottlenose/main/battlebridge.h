#ifndef FWOG_BATTLEBRIDGE_H
#define FWOG_BATTLEBRIDGE_H

#include <stdbool.h>

void battlebridge_init(void (*puts)(const char *));
void battlebridge_start(void);
void battlebridge_stop(void);
void battlebridge_go(void);
void battlebridge_set_bots(unsigned skill);
void battlebridge_set_teams(bool on);
void battlebridge_wipe(void);
void battlebridge_stat(void);
bool battlebridge_is_on(void);

#endif
