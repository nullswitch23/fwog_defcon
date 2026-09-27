#ifndef RP_TERMINAL_H
#define RP_TERMINAL_H

void rp_terminal_init(void);
void rp_terminal_welcome(void);
void rp_terminal_print(const char *s);
void rp_terminal_println(const char *s);
void rp_terminal_printf(const char *fmt, ...);
void rp_terminal_prompt(const char *mode);

#endif
