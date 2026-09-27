/* 6x8 text rendering on the ST7789.
 *
 * Separate from st7789.c because the font is not panel-specific. Glyphs are
 * blitted one at a time into their own window: the peak buffer is
 * 48 * scale^2 pixels (3456 B at scale 6), against 15 KB for a full-width strip.
 *
 * Glyphs paint their own background, so drawing a shorter string over a
 * longer one leaves the tail behind -- use lcd_text_draw_padded() for any
 * region that is redrawn. */
#ifndef FWOG_LCD_TEXT_H
#define FWOG_LCD_TEXT_H
#include <stdbool.h>
#include <stdint.h>
#include "lcd/st7789.h"

#define LCD_GLYPH_W    6u
#define LCD_GLYPH_H    8u
#define LCD_FONT_FIRST 0x20u
#define LCD_FONT_LAST  0x7Eu
#define LCD_FONT_CHARS (LCD_FONT_LAST - LCD_FONT_FIRST + 1u)   /* 95 */

/* The six column bytes for `c`. Never NULL; anything outside
 * LCD_FONT_FIRST..LCD_FONT_LAST returns the blank glyph. */
const uint8_t *lcd_font_glyph(char c);

/* How many characters fit in `width_px` at `scale`. 0 when scale is 0. */
unsigned lcd_text_cols(unsigned width_px, unsigned scale);

/* Pixel width of `nchars` characters at `scale`. */
unsigned lcd_text_width_px(unsigned nchars, unsigned scale);

/* Expand one glyph into a row-major RGB565 block, `scale` times in each
 * axis. Returns the number of pixels written. `out` must have room for
 * LCD_GLYPH_W*LCD_GLYPH_H*scale*scale pixels. Pure, so the column-major
 * bit layout is actually tested rather than inferred from the panel. */
unsigned lcd_font_expand(char c, unsigned scale, uint16_t fg, uint16_t bg,
                         uint16_t *out);

/* Draw `str` at (x, y). Each glyph paints its own background, so this also
 * erases whatever occupied those cells. */
void lcd_text_draw(uint16_t x, uint16_t y, const char *str, unsigned scale,
                   uint16_t fg, uint16_t bg);

/* As above, but pad with spaces to exactly `cols` characters -- the rule
 * that keeps a shorter string from leaving the previous one's tail on
 * screen. Truncates at `cols`; never wraps. */
void lcd_text_draw_padded(uint16_t x, uint16_t y, const char *str,
                          unsigned cols, unsigned scale,
                          uint16_t fg, uint16_t bg);

/* Like lcd_text_draw_padded, but skip the blit when `str` already matches
 * `slot`. Live VU / RSSI lines that rewrite identical glyphs every 2 ms are
 * what the panel shows as flicker. `slot` must hold `cols+1` bytes. Returns
 * true if a blit ran. */
bool lcd_text_draw_padded_changed(uint16_t x, uint16_t y, const char *str,
                                  unsigned cols, unsigned scale,
                                  uint16_t fg, uint16_t bg,
                                  char *slot, unsigned slot_n);

#ifdef HOST_TEST
/* Last N lcd_text_draw / lcd_text_draw_padded strings, oldest first.
 * Agents grep this instead of OCR'ing the PNG. */
#ifndef LCD_TEXT_LOG_CAP
#define LCD_TEXT_LOG_CAP 48u
#endif
#ifndef LCD_TEXT_LOG_STR
#define LCD_TEXT_LOG_STR 48u
#endif
typedef struct {
    uint16_t x, y;
    unsigned cols;     /* 0 when the caller used lcd_text_draw() */
    unsigned scale;
    char     str[LCD_TEXT_LOG_STR];
} lcd_text_log_entry_t;

void lcd_text_log_clear(void);
unsigned lcd_text_log_count(void);
/* NULL when i is out of range. */
const lcd_text_log_entry_t *lcd_text_log_at(unsigned i);
#endif

#endif
