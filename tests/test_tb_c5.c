#include "tb_c5.h"
#include "test_util.h"

int main(void) {
    uint16_t f1, f2;
    expect(tb_dtmf_freqs('5', &f1, &f2));
    expect(f1 == 770 && f2 == 1336);
    expect(tb_c5_freqs("KP2", &f1, &f2));
    expect(f1 == 1300 && f2 == 1700);
    expect(tb_c5_freqs("CLEARFWD", &f1, &f2));
    expect(f1 == 2400 && f2 == 2500);
    expect(!tb_c5_freqs("NOPE", &f1, &f2));
    return 0;
}
