#ifndef LAND_HOME_H
#define LAND_HOME_H

#include "lcd/lcd_text.h"
#include "lcd/st7789.h"
#include <stdio.h>

/* PinDesk landing geometry: four 36 px rows, scale-2 names. */
#define LAND_ROWS  4
#define LAND_ROW_Y 56
#define LAND_ROW_H 36

static inline int land_page(int sel) {
    if (sel < 0) sel = 0;
    return sel / LAND_ROWS;
}

static inline int land_npages(int n) {
    if (n <= 0) return 1;
    return (n + LAND_ROWS - 1) / LAND_ROWS;
}

static inline void land_paint_arrows(int sel, int n, uint16_t acc, uint16_t bg) {
    const int page = land_page(sel);
    const int pages = land_npages(n);
    if (page > 0)
        lcd_text_draw_padded(304, 8, "^", 2, 1, acc, bg);
    if (page + 1 < pages)
        lcd_text_draw_padded(304, 220, "v", 2, 1, acc, bg);
}

static inline void land_paint_home(const char *title, const char *tag,
                                   const char *const *names,
                                   const char *const *blurbs,
                                   int n, int sel,
                                   uint16_t bg, uint16_t acc,
                                   uint16_t dim, uint16_t fg) {
    const int page = land_page(sel);
    const int base = page * LAND_ROWS;
    int i;
    st7789_clear(bg);
    lcd_text_draw_padded(8, 8, title, 18, 2, acc, bg);
    lcd_text_draw_padded(8, 40, tag, 40, 1, dim, bg);
    for (i = 0; i < LAND_ROWS; i++) {
        const int idx = base + i;
        const uint16_t y = (uint16_t)(LAND_ROW_Y + i * LAND_ROW_H);
        char line[44];
        uint16_t row_fg;
        if (idx >= n) break;
        row_fg = (idx == sel) ? acc : fg;
        snprintf(line, sizeof line, "%s %s",
                 (idx == sel) ? ">" : " ", names[idx]);
        lcd_text_draw_padded(8, y, line, 20, 2, row_fg, bg);
        lcd_text_draw_padded(24, (uint16_t)(y + 18), blurbs[idx], 36, 1, dim, bg);
    }
    land_paint_arrows(sel, n, acc, bg);
    lcd_text_draw_padded(8, 212, "GRY up  RED down  GRN open",
                         38, 1, dim, bg);
    lcd_text_draw_padded(8, 224, "In a tile: YEL/GRN/GRY hold home",
                         40, 1, dim, bg);
}

#endif
