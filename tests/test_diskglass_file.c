#include "test_util.h"
#include "dg_file.h"
#include "dg_proto.h"
#include <string.h>

int main(void) {
    ASSERT_EQ(sizeof(dg_hdr_t), 48);
    ASSERT_EQ(sizeof(dg_ook_lead_t), 4);
    ASSERT_EQ(sizeof(dg_cmd_t), 52);
    ASSERT_EQ(sizeof(dg_list_t), 36);
    ASSERT_EQ(sizeof(dg_meta_t), 60);
    ASSERT_EQ(sizeof(dg_data_t), 1032);

    dg_hdr_t h;
    dg_hdr_init(&h, DG_KIND_PCM16, "talkclip");
    ASSERT_TRUE(dg_hdr_valid(&h));
    ASSERT_EQ(h.magic, DG_MAGIC);
    ASSERT_EQ(h.kind, DG_KIND_PCM16);
    ASSERT_TRUE(strcmp(h.app, "talkclip") == 0);
    ASSERT_EQ(dg_sniff(&h, sizeof h), DG_KIND_PCM16);

    h.magic = 0xDEADBEEFu;
    ASSERT_TRUE(!dg_hdr_valid(&h));
    ASSERT_EQ(dg_sniff(&h, sizeof h), DG_KIND_UNKNOWN);

    uint32_t ibst = DG_IBST_MAGIC;
    uint8_t ibst_buf[20];
    memset(ibst_buf, 0, sizeof ibst_buf);
    memcpy(ibst_buf, &ibst, 4);
    ASSERT_EQ(dg_sniff(ibst_buf, sizeof ibst_buf), DG_KIND_IBST);

    const char *csv = "# diskglass trailrf v1\nstep,rssi_dbm,freq_hz\n";
    ASSERT_EQ(dg_sniff(csv, strlen(csv)), DG_KIND_CSV);
    const char *bare = "step,rssi_dbm,freq_hz\n1,-70,433920000\n";
    ASSERT_EQ(dg_sniff(bare, strlen(bare)), DG_KIND_CSV);
    ASSERT_EQ(sizeof(dg_put_t), 268);
    ASSERT_EQ(sizeof(dg_host_t), 8);
    ASSERT_EQ(sizeof(dg_host_fb_t), 774);
    ASSERT_EQ(dg_sniff("hello", 5), DG_KIND_TEXT);
    ASSERT_TRUE(strcmp(dg_kind_label(DG_KIND_TEXT), "text") == 0);
    {
        const uint8_t wasm[4] = { 0, 'a', 's', 'm' };
        ASSERT_EQ(dg_sniff(wasm, sizeof wasm), DG_KIND_WASM);
        ASSERT_TRUE(strcmp(dg_kind_label(DG_KIND_WASM), "WASM") == 0);
    }

    {
        const char *note = "settings leftover\n";
        uint8_t buf[32];
        dg_ir_rx_t rx;
        unsigned i;
        uint16_t n = (uint16_t)strlen(note);
        dg_ir_rx_reset(&rx);
        ASSERT_TRUE(!dg_ir_rx_feed(&rx, dg_ir_start(n)));
        for (i = 0; i < n; i += 2u) {
            uint8_t a = (uint8_t)note[i];
            uint8_t b = (i + 1u < n) ? (uint8_t)note[i + 1u] : 0u;
            ASSERT_TRUE(!dg_ir_rx_feed(&rx, dg_ir_data((uint8_t)(i / 2u), a, b)));
        }
        ASSERT_TRUE(dg_ir_rx_feed(&rx, dg_ir_end(dg_ir_crc((const uint8_t *)note, n))));
        ASSERT_EQ(rx.nbytes, n);
        memcpy(buf, rx.buf, n);
        buf[n] = 0;
        ASSERT_TRUE(strcmp((const char *)buf, note) == 0);
    }

    ASSERT_EQ(dg_index_from_name("CLIP0007.RAW", "CLIP"), 7);
    ASSERT_EQ(dg_index_from_name("clip0007.raw", "CLIP"), 7);
    ASSERT_EQ(dg_index_from_name("BURST012.BIN", "BURST"), 12);
    ASSERT_EQ(dg_index_from_name("FOB0001.BIN", "CLIP"), 0);
    ASSERT_EQ(dg_index_from_name("voltpet.bin", "CLIP"), 0);

    char name[20];
    dg_format_name(name, sizeof name, "CLIP", 7, "RAW");
    ASSERT_TRUE(strcmp(name, "CLIP0007.RAW") == 0);
    dg_format_name(name, sizeof name, "TRAIL", 1, "CSV");
    ASSERT_TRUE(strcmp(name, "TRAIL0001.CSV") == 0);

    char path[40];
    ASSERT_TRUE(dg_join_path(path, sizeof path, "/", "CLIP0001.RAW"));
    ASSERT_TRUE(strcmp(path, "/CLIP0001.RAW") == 0);
    ASSERT_TRUE(dg_join_path(path, sizeof path, "/talkclip", "CLIP0001.RAW"));
    ASSERT_TRUE(strcmp(path, "/talkclip/CLIP0001.RAW") == 0);

    ASSERT_EQ(dg_pcm_duration_ms(16000u, 8000u), 1000u);
    ASSERT_TRUE(strcmp(dg_kind_label(DG_KIND_PCM16), "PCM 8 kHz") == 0);
    ASSERT_TRUE(strcmp(dg_kind_label(DG_KIND_IBST), "ISMburst") == 0);

    TEST_RETURN();
}
