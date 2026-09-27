/* Host stub: enough for ws2812_init(pio0, 0) and friends. */
#ifndef OGEMU_HARDWARE_PIO_H
#define OGEMU_HARDWARE_PIO_H

typedef unsigned uint;
struct pio_hw { int unused; };
typedef struct pio_hw *PIO;

extern struct pio_hw pio0_hw;
extern struct pio_hw pio1_hw;
#define pio0 (&pio0_hw)
#define pio1 (&pio1_hw)

#endif
