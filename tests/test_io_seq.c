#include "test_util.h"
#include "common/io_seq.h"
#include "common/io_cfg.h"
#include <string.h>

/* A recording mock: every call appends a letter, so a whole run is one
   comparable string. */
typedef struct {
    char    log[64];
    size_t  n;
    uint8_t fpga[2];      /* what the "FPGA" holds */
    bool    fail_write;
    int     corrupt_byte; /* -1 = none, 0 = flip byte 0, 1 = flip byte 1 */
    bool    fail_read;
    bool    fail_expander;
    bool    fail_enable;
    bool    read_liar;    /* fpga_read returns true but writes nothing */
} mock_t;

static void note(mock_t *m, char c) {
    if (m->n + 1u < sizeof m->log) m->log[m->n++] = c;
    m->log[m->n] = '\0';
}
static bool m_enable(void *ctx, bool on) {
    mock_t *m = ctx;
    /* The call itself is always logged -- the mock records that the open
       was ATTEMPTED. fail_enable makes the attempt fail, which the caller
       must treat as "never opened" and therefore never issue the matching
       close call: the log has no trailing 'e' in that case. */
    note(m, on ? 'E' : 'e');
    if (on && m->fail_enable) return false;
    return true;
}
static bool m_fpga_write(void *ctx, const uint8_t d[2]) {
    mock_t *m = ctx;
    note(m, 'W');
    if (m->fail_write) return false;
    m->fpga[0] = d[0]; m->fpga[1] = d[1];
    return true;
}
static bool m_fpga_read(void *ctx, uint8_t d[2]) {
    mock_t *m = ctx;
    note(m, 'R');
    if (m->fail_read) return false;
    /* The liar: returns success while leaving the caller's buffer exactly
       as it found it. This is the precise contract violation the
       complement seed in io_seq.c defends against -- a real fpga_read()
       that reports success without actually filling both bytes. */
    if (m->read_liar) return true;
    d[0] = m->fpga[0]; d[1] = m->fpga[1];
    if (m->corrupt_byte == 0) d[0] = (uint8_t)(d[0] ^ 0xFFu);
    if (m->corrupt_byte == 1) d[1] = (uint8_t)(d[1] ^ 0xFFu);
    return true;
}
static void m_pads(void *ctx, const fwog_io_cfg_t *cfg) {
    (void)cfg; note((mock_t *)ctx, 'P');
}
static bool m_expander(void *ctx, const fwog_io_cfg_t *cfg) {
    mock_t *m = ctx; (void)cfg;
    note(m, 'X');
    return !m->fail_expander;
}

static void ops_init(fwog_io_ops_t *o, mock_t *m) {
    o->config_enable  = m_enable;
    o->fpga_write     = m_fpga_write;
    o->fpga_read      = m_fpga_read;
    o->pads_apply     = m_pads;
    o->expander_apply = m_expander;
    o->ctx            = m;
}

/* memset(&m, 0, sizeof m) alone would leave corrupt_byte at 0, which means
   "flip byte 0" -- every case that wants no corruption must reset it to -1
   after zeroing. Centralising that here means a new field added to mock_t
   in future can't silently reintroduce the same trap for a case that forgot
   to set it. */
static void mock_reset(mock_t *m) {
    memset(m, 0, sizeof *m);
    m->corrupt_byte = -1;
}

int main(void) {
    fwog_io_cfg_t cfg;
    fwog_io_cfg_default(&cfg);
    fwog_io_ops_t ops;
    mock_t m;

    /* --- Happy path: the proven order from FreeWilliMain.cpp:592-680.
           Enable, FPGA write, FPGA readback, pads, expander, disable. --- */
    mock_reset(&m); ops_init(&ops, &m);
    ASSERT_EQ(fwog_io_sequence(&ops, &cfg), FWOG_IO_OK);
    ASSERT_TRUE(strcmp(m.log, "EWRPXe") == 0);

    /* --- Readback mismatch on byte 0: the safety property.
           The expander must NOT be written, the pads must NOT be set, and
           the window must still be closed. --- */
    mock_reset(&m); m.corrupt_byte = 0; ops_init(&ops, &m);
    ASSERT_EQ(fwog_io_sequence(&ops, &cfg), FWOG_IO_ERR_FPGA_VERIFY);
    ASSERT_TRUE(strcmp(m.log, "EWRe") == 0);
    ASSERT_TRUE(strchr(m.log, 'X') == NULL);   /* expander untouched */
    ASSERT_TRUE(strchr(m.log, 'P') == NULL);   /* pads untouched */

    /* --- Readback mismatch on byte 1 only: a comparison that checks only
           byte 0 must still catch this. Byte 1 carries the gpio26/27
           direction bits, so missing this case would let the sequencer set
           pads and shifters against an unverified FPGA routing for those
           two lines. --- */
    mock_reset(&m); m.corrupt_byte = 1; ops_init(&ops, &m);
    ASSERT_EQ(fwog_io_sequence(&ops, &cfg), FWOG_IO_ERR_FPGA_VERIFY);
    ASSERT_TRUE(strcmp(m.log, "EWRe") == 0);
    ASSERT_TRUE(strchr(m.log, 'X') == NULL);   /* expander untouched */
    ASSERT_TRUE(strchr(m.log, 'P') == NULL);   /* pads untouched */

    /* --- FPGA write failure: same guarantee, and no readback attempted. */
    mock_reset(&m); m.fail_write = true; ops_init(&ops, &m);
    ASSERT_EQ(fwog_io_sequence(&ops, &cfg), FWOG_IO_ERR_FPGA_WRITE);
    ASSERT_TRUE(strcmp(m.log, "EWe") == 0);

    /* --- FPGA read failure (the transport itself, not a mismatch): same
           guarantee, and the distinct FWOG_IO_ERR_FPGA_READ code is
           actually reachable. --- */
    mock_reset(&m); m.fail_read = true; ops_init(&ops, &m);
    ASSERT_EQ(fwog_io_sequence(&ops, &cfg), FWOG_IO_ERR_FPGA_READ);
    ASSERT_TRUE(strcmp(m.log, "EWRe") == 0);
    ASSERT_TRUE(strchr(m.log, 'X') == NULL);   /* expander untouched */
    ASSERT_TRUE(strchr(m.log, 'P') == NULL);   /* pads untouched */

    /* --- The liar: fpga_read() returns true but writes nothing, on an
           all-input config that packs to {0x00, 0x00}. This is Finding 1
           from mutation-testing round 1: a zero-initialised readback buffer
           would read back as "matches" here, because zero is what an
           unfilled buffer AND a legitimate all-input readback both look
           like. io_seq.c defends against this by seeding the readback
           buffer with the complement of what it expects, never zero -- this
           case is the only thing standing between that line and being
           deleted as dead code by a future refactor. Do not delete it: it
           is the one case that actually fails if the complement seed
           regresses to {0, 0}. */
    fwog_io_cfg_t all_input_cfg;
    memset(&all_input_cfg, 0, sizeof all_input_cfg);
    uint8_t all_input_want[2];
    fwog_io_pack_fpga(&all_input_cfg, all_input_want);
    ASSERT_EQ(all_input_want[0], 0x00u);
    ASSERT_EQ(all_input_want[1], 0x00u);
    mock_reset(&m); m.read_liar = true; ops_init(&ops, &m);
    ASSERT_EQ(fwog_io_sequence(&ops, &all_input_cfg), FWOG_IO_ERR_FPGA_VERIFY);
    ASSERT_TRUE(strcmp(m.log, "EWRe") == 0);
    ASSERT_TRUE(strchr(m.log, 'X') == NULL);   /* expander untouched */
    ASSERT_TRUE(strchr(m.log, 'P') == NULL);   /* pads untouched */

    /* --- Enable (open) failure: the window never opened, so it must never
           be closed either -- no trailing 'e'. This is the one path where
           config_enable(ctx, false) must NOT be called; a later refactor
           that "simplified" this into always closing would invert exactly
           the asymmetry this pins down. --- */
    mock_reset(&m); m.fail_enable = true; ops_init(&ops, &m);
    ASSERT_EQ(fwog_io_sequence(&ops, &cfg), FWOG_IO_ERR_ENABLE);
    ASSERT_TRUE(strcmp(m.log, "E") == 0);

    /* --- Expander failure is reported, and the window is still closed. */
    mock_reset(&m); m.fail_expander = true; ops_init(&ops, &m);
    ASSERT_EQ(fwog_io_sequence(&ops, &cfg), FWOG_IO_ERR_EXPANDER);
    ASSERT_TRUE(strcmp(m.log, "EWRPXe") == 0);

    /* --- The window is opened FIRST, always. rpProcessHostCommand.cpp:65
           only latches direction bits while it is set, so an implementation
           that wrote the expander before enabling would silently no-op. --- */
    mock_reset(&m); ops_init(&ops, &m);
    (void)fwog_io_sequence(&ops, &cfg);
    ASSERT_EQ(m.log[0], 'E');
    ASSERT_EQ(m.log[m.n - 1u], 'e');

    /* --- The bytes handed to the FPGA are fwog_io_pack_fpga()'s. --- */
    uint8_t want[2];
    fwog_io_pack_fpga(&cfg, want);
    mock_reset(&m); ops_init(&ops, &m);
    (void)fwog_io_sequence(&ops, &cfg);
    ASSERT_EQ(m.fpga[0], want[0]);
    ASSERT_EQ(m.fpga[1], want[1]);

    TEST_RETURN();
}
