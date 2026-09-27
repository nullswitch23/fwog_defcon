#include "ogemu.h"
#include "common/crc.h"
#include "lcd/st7789.h"
#include "lcd/lcd_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

#define OGEMU_FRAME_MS 50u
#define OGEMU_PATH_MAX 256

static char s_frame[OGEMU_PATH_MAX];
static unsigned s_last_frame_tick;

#define SCAN_LEN (ST7789_H * (1u + ST7789_W * 3u))

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t adler32(const uint8_t *p, size_t n) {
    uint32_t a = 1u, b = 0u;
    for (size_t i = 0; i < n; i++) {
        a += p[i];
        if (a >= 65521u) a -= 65521u;
        b += a;
        if (b >= 65521u) b -= 65521u;
    }
    return (b << 16) | a;
}

static int write_chunk(FILE *f, const char type[4], const uint8_t *data,
                       uint32_t len) {
    uint8_t hdr[8];
    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    if (fwrite(hdr, 1, 8, f) != 8) return -1;
    if (len && fwrite(data, 1, len, f) != len) return -1;
    uint32_t crc = fwog_crc32_update(FWOG_CRC32_INIT, type, 4);
    if (len) crc = fwog_crc32_update(crc, data, len);
    crc = fwog_crc32_final(crc);
    uint8_t c[4];
    put_be32(c, crc);
    if (fwrite(c, 1, 4, f) != 4) return -1;
    return 0;
}

static uint8_t expand5(uint8_t v) {
    return (uint8_t)((v << 3) | (v >> 2));
}
static uint8_t expand6(uint8_t v) {
    return (uint8_t)((v << 2) | (v >> 4));
}

int ogemu_write_png(const char *path) {
    if (!path || !path[0]) return -1;
    const uint16_t *fb = st7789_host_fb();
    uint8_t *scan = (uint8_t *)malloc(SCAN_LEN);
    if (!scan) return -1;
    size_t o = 0u;
    for (unsigned y = 0u; y < ST7789_H; y++) {
        scan[o++] = 0u;
        for (unsigned x = 0u; x < ST7789_W; x++) {
            const uint16_t c = fb[(uint32_t)y * ST7789_W + x];
            scan[o++] = expand5((uint8_t)((c >> 11) & 0x1Fu));
            scan[o++] = expand6((uint8_t)((c >> 5) & 0x3Fu));
            scan[o++] = expand5((uint8_t)(c & 0x1Fu));
        }
    }

    const uint32_t max_block = 65535u;
    const uint32_t nblocks = (SCAN_LEN + max_block - 1u) / max_block;
    const size_t idat_cap = 2u + (size_t)nblocks * 5u + SCAN_LEN + 4u;
    uint8_t *idat = (uint8_t *)malloc(idat_cap);
    if (!idat) {
        free(scan);
        return -1;
    }
    size_t w = 0u;
    idat[w++] = 0x78u;
    idat[w++] = 0x01u;
    size_t off = 0u;
    for (uint32_t b = 0u; b < nblocks; b++) {
        uint32_t n = (uint32_t)(SCAN_LEN - off);
        if (n > max_block) n = max_block;
        const int last = (b + 1u == nblocks);
        idat[w++] = last ? 0x01u : 0x00u;
        idat[w++] = (uint8_t)(n & 0xFFu);
        idat[w++] = (uint8_t)(n >> 8);
        const uint16_t nlen = (uint16_t)~(uint16_t)n;
        idat[w++] = (uint8_t)(nlen & 0xFFu);
        idat[w++] = (uint8_t)(nlen >> 8);
        memcpy(idat + w, scan + off, n);
        w += n;
        off += n;
    }
    const uint32_t ad = adler32(scan, SCAN_LEN);
    put_be32(idat + w, ad);
    w += 4u;
    free(scan);

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(idat);
        return -1;
    }
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    int rc = 0;
    if (fwrite(sig, 1, 8, f) != 8) rc = -1;
    uint8_t ihdr[13];
    put_be32(ihdr, ST7789_W);
    put_be32(ihdr + 4, ST7789_H);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    if (rc == 0) rc = write_chunk(f, "IHDR", ihdr, 13);
    if (rc == 0) rc = write_chunk(f, "IDAT", idat, (uint32_t)w);
    if (rc == 0) rc = write_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(idat);
    return rc;
}

static unsigned wall_ms(void) {
#ifdef _WIN32
    return (unsigned)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned)((unsigned long)ts.tv_sec * 1000ul
                      + (unsigned long)ts.tv_nsec / 1000000ul);
#endif
}

static int replace_file(const char *tmp, const char *path) {
#ifdef _WIN32
    if (MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) return 0;
    return -1;
#else
    if (rename(tmp, path) != 0) return -1;
    return 0;
#endif
}

int ogemu_write_ppm(const char *path) {
    if (!path || !path[0]) return -1;
    const uint16_t *fb = st7789_host_fb();
    char tmp[OGEMU_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    if (fprintf(f, "P6\n%u %u\n255\n", ST7789_W, ST7789_H) < 0) {
        fclose(f);
        return -1;
    }
    uint8_t row[960];
    _Static_assert(ST7789_W * 3u == 960u, "ppm row size");
    for (unsigned y = 0u; y < ST7789_H; y++) {
        uint8_t *p = row;
        const uint16_t *src = fb + (uint32_t)y * ST7789_W;
        for (unsigned x = 0u; x < ST7789_W; x++) {
            const uint16_t c = src[x];
            *p++ = expand5((uint8_t)((c >> 11) & 0x1Fu));
            *p++ = expand6((uint8_t)((c >> 5) & 0x3Fu));
            *p++ = expand5((uint8_t)(c & 0x1Fu));
        }
        if (fwrite(row, 1, sizeof row, f) != sizeof row) {
            fclose(f);
            return -1;
        }
    }
    if (fclose(f) != 0) return -1;
    return replace_file(tmp, path);
}

void ogemu_set_frame_path(const char *path) {
    if (!path) {
        s_frame[0] = '\0';
        return;
    }
    strncpy(s_frame, path, sizeof s_frame - 1u);
    s_frame[sizeof s_frame - 1u] = '\0';
}

void ogemu_maybe_write_frame(int force) {
    if (!s_frame[0]) return;
    unsigned now = wall_ms();
    if (!force && s_last_frame_tick != 0u &&
        (now - s_last_frame_tick) < OGEMU_FRAME_MS) {
        return;
    }
    if (ogemu_write_ppm(s_frame) == 0) {
        s_last_frame_tick = now;
    } else if (force) {
        fprintf(stderr, "ogemu: failed to write %s\n", s_frame);
    }
}

void ogemu_print_text(void) {
    const unsigned n = lcd_text_log_count();
    printf("TEXT %u overlay line(s)\n", n);
    for (unsigned i = 0u; i < n; i++) {
        const lcd_text_log_entry_t *e = lcd_text_log_at(i);
        if (!e) continue;
        printf("TEXT x=%u y=%u cols=%u scale=%u \"%s\"\n",
               (unsigned)e->x, (unsigned)e->y, e->cols, e->scale, e->str);
    }
}

int ogemu_write_text(const char *path) {
    if (!path || !path[0]) return -1;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    const unsigned n = lcd_text_log_count();
    fprintf(f, "TEXT %u overlay line(s)\n", n);
    for (unsigned i = 0u; i < n; i++) {
        const lcd_text_log_entry_t *e = lcd_text_log_at(i);
        if (!e) continue;
        fprintf(f, "TEXT x=%u y=%u cols=%u scale=%u \"%s\"\n",
                (unsigned)e->x, (unsigned)e->y, e->cols, e->scale, e->str);
    }
    fclose(f);
    return 0;
}

bool ogemu_text_contains(const char *needle) {
    if (!needle || !needle[0]) return true;
    const unsigned n = lcd_text_log_count();
    for (unsigned i = 0u; i < n; i++) {
        const lcd_text_log_entry_t *e = lcd_text_log_at(i);
        if (e && strstr(e->str, needle)) return true;
    }
    return false;
}
