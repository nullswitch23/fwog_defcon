#include "rp_terminal.h"
#include <stdio.h>
#include <stdarg.h>

void rp_terminal_init(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
}

void rp_terminal_welcome(void) {
    rp_terminal_println("");
    rp_terminal_println("  ___ _ _  ___   ____  _          _ _");
    rp_terminal_println(" | _ ) (_) |/ \\ | __ )(_)_ __ ___(_) |_ _   _");
    rp_terminal_println(" | _ \\| | |/ _ \\|  _ \\| | '__/ _ \\| | __| | | |");
    rp_terminal_println(" |___/|_|_/_/ \\_\\_| \\_\\_|_|  \\___/|_|\\__|\\__, |");
    rp_terminal_println("                                         |___/");
    rp_terminal_println(" FreeWili OG BitPirate port (Retia) v002");
    rp_terminal_println(" Terminal over USB CDC. Type 'help' or 'mode'.");
    rp_terminal_println("");
}

void rp_terminal_print(const char *s) {
    if (s) fputs(s, stdout);
}

void rp_terminal_println(const char *s) {
    if (s) puts(s);
}

void rp_terminal_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

void rp_terminal_prompt(const char *mode) {
    rp_terminal_printf("%s> ", mode ? mode : "?");
}
