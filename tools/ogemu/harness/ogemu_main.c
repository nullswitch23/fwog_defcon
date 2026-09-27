#include "ogemu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0) {
    fprintf(stderr,
        "wiliOG display-CPU UI emulator (v001, host, no RP2040)\n"
        "\n"
        "Usage: %s [options] [-- press green --tick 16 --release green ...]\n"
        "\n"
        "  --script FILE     JSONL or line-oriented button/time script\n"
        "  --dump FILE.png   write the 320x240 panel (also writes FILE.txt)\n"
        "  --text FILE.txt   overlay dump path (default: dump with .txt)\n"
        "  --ms N            if no script, run N ms then dump and quit\n"
        "  --press NAME      green/yellow/blue/gray/red (also g/y/b/h/r)\n"
        "  --release NAME\n"
        "  --hold NAME MS    press, wait debounce+MS (red 6000 ships)\n"
        "  --tick MS\n"
        "  --expect NEEDLE   fail if lcd_text overlay lacks NEEDLE\n"
        "  --quit\n"
        "  --stdin           live JSONL from stdin (EOF sends quit)\n"
        "  --listen PORT     live JSONL on 127.0.0.1:PORT (until quit)\n"
        "  --frame FILE.ppm  live P6 PPM of the 320x240 panel (tk GUI)\n"
        "\n"
        "World bus (JSONL or stdin/socket; same as tools/ogemu/bench.py):\n"
        "  accel/shake/mic_rms/mic_tone/rssi/ook\n"
        "  bs_status/ib_status/tf_status/te_status/fob_status/trf_status/rf\n"
        "  ph_status/bd_status/lf_status/am_status -- typed link frames; see README\n"
        "\n"
        "Keyboard map (tools/ogemu/gui.py; also documented here):\n"
        "  G green  Y yellow  B blue  H gray  R red\n"
        "  Enter=green  Left=yellow  Right=blue  Up=gray  Down=red\n",
        argv0);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    ogemu_time_reset();

    uint32_t default_ms = 250u;
    int have_cmds = 0;
    int live = 0;
    const char *script = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *n = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (!strcmp(a, "--script") && n) {
            script = n; i++; continue;
        }
        if (!strcmp(a, "--dump") && n) {
            ogemu_set_dump_png(n); i++; continue;
        }
        if (!strcmp(a, "--text") && n) {
            ogemu_set_dump_txt(n); i++; continue;
        }
        if (!strcmp(a, "--ms") && n) {
            default_ms = (uint32_t)strtoul(n, NULL, 10);
            i++; continue;
        }
        if (!strcmp(a, "--press") && n) {
            char line[64];
            snprintf(line, sizeof line, "press %s", n);
            if (ogemu_script_push_line(line) != 0) return 2;
            have_cmds = 1; i++; continue;
        }
        if (!strcmp(a, "--release") && n) {
            char line[64];
            snprintf(line, sizeof line, "release %s", n);
            if (ogemu_script_push_line(line) != 0) return 2;
            have_cmds = 1; i++; continue;
        }
        if (!strcmp(a, "--hold") && n) {
            const char *ms = "6000";
            int skip = 1;
            if (i + 2 < argc && argv[i + 2][0] >= '0' && argv[i + 2][0] <= '9') {
                ms = argv[i + 2];
                skip = 2;
            }
            char line[80];
            snprintf(line, sizeof line, "hold %s %s", n, ms);
            if (ogemu_script_push_line(line) != 0) return 2;
            have_cmds = 1;
            i += skip;
            continue;
        }
        if (!strcmp(a, "--tick") && n) {
            char line[40];
            snprintf(line, sizeof line, "tick %s", n);
            if (ogemu_script_push_line(line) != 0) return 2;
            have_cmds = 1; i++; continue;
        }
        if (!strcmp(a, "--expect") && n) {
            char line[300];
            snprintf(line, sizeof line, "expect_text %s", n);
            if (ogemu_script_push_line(line) != 0) return 2;
            have_cmds = 1; i++; continue;
        }
        if (!strcmp(a, "--quit")) {
            if (ogemu_script_push_line("quit") != 0) return 2;
            have_cmds = 1; continue;
        }
        if (!strcmp(a, "--stdin")) {
            if (ogemu_live_stdin() != 0) {
                fprintf(stderr, "ogemu: --stdin thread failed\n");
                return 2;
            }
            live = 1;
            have_cmds = 1;
            continue;
        }
        if (!strcmp(a, "--listen") && n) {
            unsigned port = (unsigned)strtoul(n, NULL, 10);
            if (ogemu_live_listen(port) != 0) {
                fprintf(stderr, "ogemu: --listen %s failed\n", n);
                return 2;
            }
            live = 1;
            have_cmds = 1;
            i++;
            continue;
        }
        if (!strcmp(a, "--frame") && n) {
            ogemu_set_frame_path(n);
            i++;
            continue;
        }
        fprintf(stderr, "ogemu: unknown arg %s\n", a);
        usage(argv[0]);
        return 2;
    }

    if (script) {
        if (ogemu_script_load(script) != 0) return 2;
        have_cmds = 1;
    }
    if (!have_cmds) {
        char line[40];
        snprintf(line, sizeof line, "tick %u", (unsigned)default_ms);
        (void)ogemu_script_push_line(line);
        (void)ogemu_script_push_line("quit");
    } else if (!live) {
        if (ogemu_script_push_line("quit") != 0) return 2;
    }

    return fwog_app_main();
}
