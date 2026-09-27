#ifndef TB_DIALS_H
#define TB_DIALS_H

typedef struct {
    const char *title;
    const char *text;
    unsigned len;
} tb_dial_t;

extern const char tb_dial_national_kp1[];
extern const unsigned tb_dial_national_kp1_len;
extern const char tb_dial_intl_kp2[];
extern const unsigned tb_dial_intl_kp2_len;
extern const char tb_dial_bf_pin2[];
extern const unsigned tb_dial_bf_pin2_len;
extern const char tb_dial_phone_tpl[];
extern const unsigned tb_dial_phone_tpl_len;

extern const tb_dial_t tb_builtin_dials[];
extern const unsigned tb_builtin_dials_n;

#endif
