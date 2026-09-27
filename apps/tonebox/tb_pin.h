#ifndef TB_PIN_H
#define TB_PIN_H
#include <stddef.h>
#include <stdbool.h>

/* Generate bruteforce PIN sequence (rdoetjes bruteforce_generate_pins_and_dial.sh).
 * dial_body: tab-delimited D/C/H/~ lines for the number to redial.
 * digits: 2 (00-99) or 3 (000-999). redial_every: redial cadence (default 3). */
bool tb_pin_generate(const char *dial_body, unsigned digits, unsigned redial_every,
                     char *out, size_t cap, size_t *out_len);

#endif
