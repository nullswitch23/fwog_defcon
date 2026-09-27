#ifndef TB_PLAY_H
#define TB_PLAY_H
#include "tb_seq.h"
#include <stdbool.h>
#include <stdint.h>

#define TB_PLAY_MSG_MAX 40u

typedef struct {
    bool (*audio_idle)(void);
    bool (*play_tone)(uint16_t f1, uint16_t f2, unsigned ms);
    int (*serial_hangup)(unsigned timeout_ms);
    bool (*button_continue)(void);
    bool (*listen_2600)(void);
} tb_play_io_t;

typedef enum {
    TB_PLAY_IDLE = 0,
    TB_PLAY_TONE,
    TB_PLAY_PAUSE,
    TB_PLAY_WAIT_KEY,
    TB_PLAY_WAIT_MS,
    TB_PLAY_WAIT_2600,
    TB_PLAY_HANGUP,
} tb_play_state_t;

typedef struct {
    const tb_step_t *steps;
    unsigned n;
    unsigned idx;
    tb_play_state_t state;
    uint32_t until_ms;
    uint32_t wait2600_since_ms;
    bool wait2600_first;
    bool done;
    char msg[TB_PLAY_MSG_MAX];
} tb_play_t;

void tb_play_init(tb_play_t *p);
void tb_play_stop(tb_play_t *p);
bool tb_play_busy(const tb_play_t *p);
bool tb_play_start(tb_play_t *p, const tb_step_t *steps, unsigned n,
                   bool wait2600_first);
void tb_play_poll(tb_play_t *p, uint32_t now_ms, const tb_play_io_t *io);

#endif
