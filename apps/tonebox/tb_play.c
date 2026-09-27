#include "tb_play.h"
#include "tb_c5.h"
#include <stdio.h>
#include <string.h>

#define TB_MIN_SILENCE_MS 40u

static void set_msg(tb_play_t *p, const char *m) {
    if (!p) return;
    if (!m) {
        p->msg[0] = '\0';
        return;
    }
    strncpy(p->msg, m, TB_PLAY_MSG_MAX - 1u);
    p->msg[TB_PLAY_MSG_MAX - 1u] = '\0';
}

void tb_play_init(tb_play_t *p) {
    if (!p) return;
    memset(p, 0, sizeof *p);
    p->state = TB_PLAY_IDLE;
}

void tb_play_stop(tb_play_t *p) {
    if (!p) return;
    p->steps = NULL;
    p->n = 0;
    p->idx = 0;
    p->state = TB_PLAY_IDLE;
    p->done = true;
    set_msg(p, "stopped");
}

bool tb_play_busy(const tb_play_t *p) {
    return p && p->state != TB_PLAY_IDLE;
}

bool tb_play_start(tb_play_t *p, const tb_step_t *steps, unsigned n,
                   bool wait2600_first) {
    if (!p || !steps || !n) return false;
    p->steps = steps;
    p->n = n;
    p->idx = 0;
    p->done = false;
    p->wait2600_first = wait2600_first;
    p->until_ms = 0;
    if (wait2600_first) {
        p->state = TB_PLAY_WAIT_2600;
        p->wait2600_since_ms = 0;
        set_msg(p, "wait 2600 wink");
    } else {
        p->state = TB_PLAY_PAUSE;
        set_msg(p, "run");
    }
    return true;
}

static bool step_freqs(const tb_step_t *s, uint16_t *f1, uint16_t *f2) {
    if (s->type == 'D' && s->tone[0]) return tb_dtmf_freqs(s->tone[0], f1, f2);
    if (s->type == 'C' && s->tone[0]) return tb_c5_freqs(s->tone, f1, f2);
    return false;
}

static void next_step(tb_play_t *p, uint32_t now_ms, const tb_play_io_t *io);

static unsigned pause_ms_for(const tb_step_t *s) {
    unsigned ms = (unsigned)(s->pause_ms > 0 ? s->pause_ms : 0);
    if (ms < TB_MIN_SILENCE_MS) ms = TB_MIN_SILENCE_MS;
    return ms;
}

static void begin_step(tb_play_t *p, uint32_t now_ms, const tb_play_io_t *io) {
    const tb_step_t *s = &p->steps[p->idx];
    char line[TB_PLAY_MSG_MAX];

    if (s->type == '~') {
        if (s->duration_ms <= 0) {
            p->state = TB_PLAY_WAIT_KEY;
            set_msg(p, "wait key");
        } else {
            p->state = TB_PLAY_WAIT_MS;
            p->until_ms = 0;
            set_msg(p, "wait");
        }
        return;
    }
    if (s->type == 'H') {
        p->state = TB_PLAY_HANGUP;
        set_msg(p, "H relay");
        (void)io;
        return;
    }

    uint16_t f1 = 0, f2 = 0;
    if (!step_freqs(s, &f1, &f2)) {
        snprintf(line, sizeof line, "skip %u", p->idx + 1u);
        set_msg(p, line);
        next_step(p, now_ms, io);
        return;
    }
    snprintf(line, sizeof line, "%c %s", s->type, s->tone);
    set_msg(p, line);
    if (s->duration_ms > 0 && io && io->play_tone) {
        (void)io->play_tone(f1, f2, (unsigned)s->duration_ms);
        p->state = TB_PLAY_TONE;
    } else {
        p->state = TB_PLAY_PAUSE;
        p->until_ms = 0;
    }
}

static void next_step(tb_play_t *p, uint32_t now_ms, const tb_play_io_t *io) {
    const tb_step_t *prev = &p->steps[p->idx];
    if (p->idx + 1u >= p->n) {
        tb_play_stop(p);
        set_msg(p, "done");
        return;
    }
    p->until_ms = now_ms + pause_ms_for(prev);
    p->state = TB_PLAY_PAUSE;
    p->idx++;
    (void)io;
}

void tb_play_poll(tb_play_t *p, uint32_t now_ms, const tb_play_io_t *io) {
    if (!p || p->state == TB_PLAY_IDLE || !io) return;

    if (p->state == TB_PLAY_WAIT_2600) {
        if (p->wait2600_since_ms == 0u) p->wait2600_since_ms = now_ms;
        const bool heard = io->listen_2600 && io->listen_2600();
        const bool timed_out = (now_ms - p->wait2600_since_ms) >= 30000u;
        if (heard || timed_out) {
            p->wait2600_first = false;
            p->state = TB_PLAY_PAUSE;
            p->until_ms = 0;
            set_msg(p, heard ? "2600 seen" : "2600 timeout");
            begin_step(p, now_ms, io);
        }
        return;
    }

    if (p->state == TB_PLAY_PAUSE) {
        if (p->until_ms != 0u && (int32_t)(now_ms - p->until_ms) < 0) return;
        p->until_ms = 0;
        begin_step(p, now_ms, io);
        return;
    }

    if (p->state == TB_PLAY_TONE) {
        if (io->audio_idle && io->audio_idle()) {
            next_step(p, now_ms, io);
        }
        return;
    }

    if (p->state == TB_PLAY_WAIT_MS) {
        const tb_step_t *s = &p->steps[p->idx];
        if (p->until_ms == 0u) {
            p->until_ms = now_ms + (uint32_t)(s->duration_ms > 0 ? s->duration_ms : 0);
        }
        if ((int32_t)(now_ms - p->until_ms) >= 0) {
            p->until_ms = 0;
            next_step(p, now_ms, io);
        }
        return;
    }

    if (p->state == TB_PLAY_WAIT_KEY) {
        if (io->button_continue && io->button_continue()) {
            next_step(p, now_ms, io);
        }
        return;
    }

    if (p->state == TB_PLAY_HANGUP) {
        int r = io->serial_hangup ? io->serial_hangup(1000u) : -1;
        if (r == 1) set_msg(p, "H ok");
        else if (r == 0) set_msg(p, "H fail");
        else set_msg(p, "H timeout");
        next_step(p, now_ms, io);
    }
}
