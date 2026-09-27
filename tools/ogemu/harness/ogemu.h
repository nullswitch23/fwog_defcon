/* Display-CPU UI harness. Virtual time, five buttons, PNG dump, world bus. */
#ifndef OGEMU_H
#define OGEMU_H
#include <stdbool.h>
#include <stdint.h>
#include "input/buttons.h"

#define OGEMU_OOK_MAX 512u

extern int fwog_app_main(void);

void ogemu_time_reset(void);
uint32_t ogemu_now_ms(void);
void ogemu_advance_ms(uint32_t ms);

/* Called from sleep_ms: consume script, maybe dump and exit. */
void ogemu_on_sleep(uint32_t ms);

void ogemu_btn_set(fwog_btn_id_t id, bool down);
void ogemu_btn_clear(void);

int ogemu_write_png(const char *path);
int ogemu_write_ppm(const char *path);
int ogemu_write_text(const char *path);
void ogemu_print_text(void);
bool ogemu_text_contains(const char *needle);

/* Live LCD for the tk GUI: atomic P6 PPM, throttled unless force. */
void ogemu_set_frame_path(const char *path);
void ogemu_maybe_write_frame(int force);

/* Script / argv. Returns 0, or a positive code if the file could not load. */
int ogemu_script_load(const char *path);
int ogemu_script_push_line(const char *line);
/* Directory of the last --script file, or NULL. OOK paths try this next. */
const char *ogemu_script_dir(void);
void ogemu_set_dump_png(const char *path);
void ogemu_set_dump_txt(const char *path);
void ogemu_request_quit(int code);
bool ogemu_script_done(void);
void ogemu_finish(void);

/* Live inject: do not auto-quit when the queue is empty. stdin EOF still
 * pushes quit. --listen stays until a quit command. */
void ogemu_set_live(bool on);
bool ogemu_live(void);
int  ogemu_live_stdin(void);
int  ogemu_live_listen(unsigned port);

/* World bus. JSONL accel/mic/rssi/ook land here; HOST_TEST driver shells
 * read them. Radio is stored for DIAG and a later stub main — display apps
 * only see RSSI if a link frame is also queued. */
void ogemu_world_accel_mg(int32_t x, int32_t y, int32_t z);
void ogemu_world_mic_rms(uint32_t rms);
void ogemu_world_rssi(int16_t dbm, uint32_t hz);
int  ogemu_world_ook_file(const char *path);
/* Queue a decoded inter-CPU payload on the display UART0 RX stub. */
int  ogemu_link_rx_payload(const void *payload, size_t len);
int  ogemu_world_link_hex(const char *hex);
/* VoltPet VP_MSG_RF (0x4E) on the link — rising RSSI edge from a stub main. */
void ogemu_world_rf_burst(int16_t dbm, uint32_t hz, unsigned art);
/* Sine at `hz` (PDM 8 kHz) with amplitude matching `rms`. hz 0 keeps the
 * square-wave mic_rms stimulus. */
void ogemu_world_mic_tone(uint32_t hz, uint32_t rms);
/* Typed main→display status frames. Receive-side chrome only — no PHY. */
int ogemu_world_bs_status(uint8_t band, uint8_t mode, int16_t peak_dbm,
                          uint32_t peak_hz, int16_t hit_dbm);
int ogemu_world_ib_status(uint8_t state, int16_t rssi, int16_t peak_rssi,
                          uint8_t guess, const char *label, const char *hex);
int ogemu_world_tf_status(int16_t rssi0, int16_t rssi1, uint32_t freq_hz);
int ogemu_world_te_status(uint8_t preset, int16_t rssi, int16_t last_rssi,
                          uint16_t bursts, uint8_t in_burst, uint16_t last_ms);
int ogemu_world_fob_status(uint8_t state, int16_t rssi, uint8_t slots,
                           uint8_t unused, uint16_t edges, uint32_t freq_hz);
int ogemu_world_trf_status(int16_t rssi0, int16_t rssi1,
                           uint32_t freq0_hz, uint32_t freq1_hz);
int ogemu_world_dg_vol(unsigned count, uint32_t free_bytes);
int ogemu_world_dg_list(unsigned index, const char *name, uint16_t kind,
                        uint32_t size, int is_dir, int end);
int ogemu_world_dg_meta(const char *name, uint16_t kind, uint32_t size,
                        uint32_t duration_us, uint32_t sample_hz);
int ogemu_world_ph_status(uint8_t hello, uint8_t scan, int8_t rssi,
                          uint8_t apple, const char *name);
int ogemu_world_bd_status(uint8_t hello, uint8_t adv, uint8_t conn,
                          uint8_t lock, uint8_t flash, uint8_t pct);
int ogemu_world_lf_status(uint8_t hello, uint8_t ap_on, uint8_t clients,
                          uint8_t flash, uint8_t pct, uint32_t bytes,
                          uint8_t nfiles,
                          const char *ssid, const char *pass, const char *file);
int ogemu_world_bb_status(uint8_t hello, uint8_t game_on, uint8_t players,
                          uint8_t phase, uint8_t flash, uint8_t pct,
                          uint16_t tick, uint16_t heap_k, uint8_t step_100us,
                          const char *ssid, const char *pass, const char *why,
                          uint8_t bots, uint8_t clock_s, uint8_t sudden,
                          uint8_t teams);
int ogemu_world_hk_status(uint8_t io_ok, uint8_t other_buses_hiz,
                          const char *addresses);
int ogemu_world_qg_status(uint8_t io_ok, const char *name, const char *val,
                          int16_t plot);
int ogemu_world_rg_status(const char *packed);
int ogemu_world_am_status(uint8_t hello, uint8_t wp_on, uint8_t sweep,
                          uint8_t armed, uint8_t mode, uint8_t flash,
                          uint8_t pct, uint8_t channel, uint8_t aps,
                          uint32_t tx, const char *log, const char *ret,
                          const char *why);
void ogemu_world_get_accel_mg(int32_t *x, int32_t *y, int32_t *z);
uint32_t ogemu_world_get_mic_rms(void);
int16_t  ogemu_world_get_rssi(uint32_t *hz);
unsigned ogemu_world_ook_count(void);

#endif
