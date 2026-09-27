#include "tb_seq.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool comment_or_empty(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return true;
    return *s == '#' || *s == ';';
}

static int parse_int_field(const char *tok) {
    if (!tok) return 0;
    while (*tok && isspace((unsigned char)*tok)) tok++;
    if (!*tok) return 0;
    return (int)strtol(tok, NULL, 10);
}

static const char *next_field(const char *line, char *out, size_t out_cap) {
    if (!line || !out || out_cap == 0u) return NULL;
    while (*line == '\t') line++;
    if (!*line) {
        out[0] = '\0';
        return line;
    }
    size_t i = 0;
    while (line[i] && line[i] != '\t' && i + 1u < out_cap) {
        out[i] = line[i];
        i++;
    }
    out[i] = '\0';
    if (line[i] == '\t') return line + i + 1;
    return line + i;
}

static bool parse_tone_or_h(char type, const char *tone, const char *dur,
                            const char *pause, tb_step_t *step) {
    step->type = type;
    step->tone[0] = '\0';
    if (tone) {
        strncpy(step->tone, tone, TB_SEQ_TONE_MAX - 1u);
        step->tone[TB_SEQ_TONE_MAX - 1u] = '\0';
    }
    step->duration_ms = parse_int_field(dur);
    step->pause_ms = parse_int_field(pause);
    return true;
}

static bool parse_wait(const char *dur, tb_step_t *step) {
    step->type = '~';
    step->tone[0] = '\0';
    step->pause_ms = 0;
    step->duration_ms = parse_int_field(dur);
    return true;
}

bool tb_seq_parse(const char *text, tb_step_t *out, unsigned cap, unsigned *n_out) {
    char line[256];
    char f0[32], f1[32], f2[32], f3[32];
    unsigned n = 0;
    if (!text || !out || !cap || !n_out) return false;
    *n_out = 0;

    while (*text) {
        unsigned li = 0;
        while (*text && *text != '\n' && *text != '\r' && li + 1u < sizeof line) {
            line[li++] = *text++;
        }
        line[li] = '\0';
        while (*text == '\n' || *text == '\r') text++;

        if (comment_or_empty(line)) continue;
        if (n >= cap) return false;

        const char *rest = next_field(line, f0, sizeof f0);
        if (!f0[0]) continue;

        if (strcmp(f0, "~") == 0) {
            rest = next_field(rest, f1, sizeof f1);
            if (!parse_wait(f1, &out[n])) return false;
            n++;
            continue;
        }

        if (strlen(f0) != 1u) continue;
        const char t = f0[0];
        if (t != 'D' && t != 'C' && t != 'H') continue;

        rest = next_field(rest, f1, sizeof f1);
        rest = next_field(rest, f2, sizeof f2);
        rest = next_field(rest, f3, sizeof f3);
        if (!parse_tone_or_h(t, f1, f2, f3, &out[n])) return false;
        n++;
    }

    *n_out = n;
    return true;
}
