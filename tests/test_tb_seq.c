#include "tb_seq.h"
#include "test_util.h"
#include <string.h>

int main(void) {
    tb_step_t steps[8];
    unsigned n = 0;
    const char *txt =
        "# comment\n"
        "D\t1\t100\t50\n"
        "C\tKP1\t80\t40\n"
        "H\t\t0\t200\n"
        "~\t500\n"
        "~\n";

    expect(tb_seq_parse(txt, steps, 8, &n));
    expect(n == 5u);
    expect(steps[0].type == 'D' && steps[0].tone[0] == '1');
    expect(steps[0].duration_ms == 100 && steps[0].pause_ms == 50);
    expect(steps[1].type == 'C' && strcmp(steps[1].tone, "KP1") == 0);
    expect(steps[2].type == 'H');
    expect(steps[3].type == '~' && steps[3].duration_ms == 500);
    expect(steps[4].type == '~' && steps[4].duration_ms == 0);
    return 0;
}
