#include "tb_c5.h"
#include "tb_seq.h"
#include <ctype.h>
#include <string.h>

bool tb_dtmf_freqs(char digit, uint16_t *f1, uint16_t *f2) {
    if (!f1 || !f2) return false;
    switch (toupper((unsigned char)digit)) {
    case '1': *f1 = 697; *f2 = 1209; return true;
    case '2': *f1 = 697; *f2 = 1336; return true;
    case '3': *f1 = 697; *f2 = 1477; return true;
    case '4': *f1 = 770; *f2 = 1209; return true;
    case '5': *f1 = 770; *f2 = 1336; return true;
    case '6': *f1 = 770; *f2 = 1477; return true;
    case '7': *f1 = 852; *f2 = 1209; return true;
    case '8': *f1 = 852; *f2 = 1336; return true;
    case '9': *f1 = 852; *f2 = 1477; return true;
    case '0': *f1 = 941; *f2 = 1336; return true;
    case '*': *f1 = 941; *f2 = 1209; return true;
    case '#': *f1 = 941; *f2 = 1477; return true;
    case 'A': *f1 = 697; *f2 = 1633; return true;
    case 'B': *f1 = 770; *f2 = 1633; return true;
    case 'C': *f1 = 852; *f2 = 1633; return true;
    case 'D': *f1 = 941; *f2 = 1633; return true;
    default: return false;
    }
}

static void upper_inplace(char *s) {
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

bool tb_c5_freqs(const char *code_in, uint16_t *f1, uint16_t *f2) {
    char code[TB_SEQ_TONE_MAX];
    if (!code_in || !f1 || !f2 || !code_in[0]) return false;
    strncpy(code, code_in, sizeof code - 1u);
    code[sizeof code - 1u] = '\0';
    upper_inplace(code);

    if (strlen(code) == 1u && isdigit((unsigned char)code[0])) {
        return tb_dtmf_freqs(code[0], f1, f2);
    }
    if (strcmp(code, "KP1") == 0) { *f1 = 1100; *f2 = 1700; return true; }
    if (strcmp(code, "KP2") == 0) { *f1 = 1300; *f2 = 1700; return true; }
    if (strcmp(code, "ST") == 0)  { *f1 = 1500; *f2 = 1700; return true; }
    if (strcmp(code, "CODE11") == 0) { *f1 = 700; *f2 = 1700; return true; }
    if (strcmp(code, "CODE12") == 0) { *f1 = 900; *f2 = 1700; return true; }
    if (strcmp(code, "SEIZE") == 0) { *f1 = 2400; *f2 = 0; return true; }
    if (strcmp(code, "PROCEED") == 0 || strcmp(code, "PK") == 0) {
        *f1 = 2500; *f2 = 0; return true; /* 2600 capped at 8 kHz Nyquist */
    }
    if (strcmp(code, "ANSWER") == 0) { *f1 = 2400; *f2 = 0; return true; }
    if (strcmp(code, "BUSY") == 0 || strcmp(code, "BUSYFLASH") == 0) {
        *f1 = 2500; *f2 = 0; return true;
    }
    if (strcmp(code, "CLEARBACK") == 0) { *f1 = 2500; *f2 = 0; return true; }
    if (strcmp(code, "CLEARFWD") == 0 || strcmp(code, "RELGUARD") == 0) {
        *f1 = 2400; *f2 = 2500; return true;
    }
    return false;
}
