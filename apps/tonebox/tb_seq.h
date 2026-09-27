#ifndef TB_SEQ_H
#define TB_SEQ_H
#include <stdbool.h>
#include <stddef.h>

#define TB_SEQ_TONE_MAX 16u
#define TB_SEQ_STEP_MAX 1024u

typedef struct {
    char type; /* D, C, H, ~ */
    char tone[TB_SEQ_TONE_MAX];
    int duration_ms;
    int pause_ms;
} tb_step_t;

/* Parse rdoetjes tab-delimited sequence text (# and ; comments). */
bool tb_seq_parse(const char *text, tb_step_t *out, unsigned cap, unsigned *n_out);

#endif
