#include "bootloader/bl_jump.h"
#include "common/diag.h"
#include "common/link/link_uart.h"
#include "platform/board.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/regs/m0plus.h"
#include "hardware/resets.h"
#include <stdint.h>

/* The app's vector table sits one boot2 (256 bytes) past the slot base. */
#define APP_VECTORS  (FWOG_APP_XIP_ADDR + 0x100u)

#define PPB(off)     (*(volatile uint32_t *)(PPB_BASE + (off)))

#define RAM_BEGIN    0x20000000u
#define RAM_END      0x20042000u   /* 264 KB: SRAM0-5 plus both scratch banks */

bool bl_app_image_ok(const fwog_app_meta_t *m) {
    /* The vector table must be inside the image the metadata describes. */
    if (m->size < 0x100u + 8u) return false;

    const uint32_t *vt = (const uint32_t *)APP_VECTORS;
    uint32_t msp   = vt[0];
    uint32_t entry = vt[1];

    /* Initial stack pointer must be word-aligned RAM. */
    if (msp < RAM_BEGIN || msp > RAM_END || (msp & 3u)) return false;
    /* Reset vector must be a Thumb address inside the app slot. */
    if ((entry & 1u) == 0u) return false;
    uint32_t pc = entry & ~1u;
    if (pc < APP_VECTORS || pc >= FWOG_APP_XIP_ADDR + m->size) return false;
    return true;
}

void bl_jump_to_app(void) {
    const uint32_t *vt = (const uint32_t *)APP_VECTORS;
    uint32_t msp   = vt[0];
    uint32_t entry = vt[1];

    /* Give any in-flight DIAG a chance to drain before USB goes away. */
    DIAG("[bl] jumping to app at %08x\n", (unsigned)APP_VECTORS);

    /* --- quiesce, in the order that keeps the outside world sane ---
     *
     * CHECKLIST: if anything here ever brings up hardware the bootloader
     * did not touch before -- a timer, a PIO state machine, another UART --
     * add its teardown here. Nothing in the build catches a peripheral this
     * function forgot to quiesce; this comment is the only mechanism that
     * does. Accounted for below: DMA (all twelve channels, plus their sticky
     * interrupt flags), SPI1 and the LCD's CS line, USBCTRL, the link UART
     * and its four pins, and I2C1. */

    /* DMA first, and before the NVIC mask below: a channel keeps writing
       memory the app now owns regardless of interrupt state, because DMA
       does not go through the NVIC at all. The LCD driver claims one channel
       for its fill path, so this is mandatory, not defensive -- but abort
       all twelve rather than only that one, so a second claimant does not
       have to remember to come back here. dma_channel_abort() blocks until
       each channel reaches a safe state, which is the other reason this runs
       before interrupts are masked: a running channel must be allowed to
       retire. */
    for (uint ch = 0; ch < NUM_DMA_CHANNELS; ch++) {
        dma_channel_abort(ch);
    }

    /* DMA_INTR is write-to-clear and nothing else clears it, so a sticky bit
       set here is consumed by the *application* -- a different binary -- for
       a transfer that retired before this jump. The LCD fill channel is
       configured irq_quiet, but dma_channel_abort() can raise the flag by
       itself (the SDK's own doc block warns about exactly that), and a
       second claimant need not be quiet at all. Clear all twelve. */
    dma_hw->intr = (1u << NUM_DMA_CHANNELS) - 1u;

    /* Park the LCD chip select. A fill aborted above may have left CS
       asserted: the fill path holds it low for the whole transfer, and this
       function is reached from inside the link-drain loop precisely so that
       loop keeps running while one is in flight. The app's board_init()
       calls fwog_clocks_init() before board_init_pins(), so clk_peri is
       retuned while whatever is left in SPI1's TX FIFO is still draining
       into a selected panel. It self-corrects, but the hardware record says do not
       inherit state -- so hand over with the panel deselected. */
    gpio_put(PIN_LCD_CS, 1);

    /* USB next, and by reset rather than a polite disconnect: holding
       USBCTRL in reset drops the D+ pull-up, so the host sees a clean
       unplug instead of a device that stops answering. The app's own
       stdio_usb init unresets it. */
    reset_block_mask(RESETS_RESET_USBCTRL_BITS);

    /* The link. Latch the idle-high level on PIN_LINK_TX *before* switching
       its direction to output: gpio_init() clears the output latch as well
       as setting the pin to input, so calling gpio_set_dir(OUT) first would
       assert the driver onto that cleared (low) latch and produce exactly
       the break condition this sequence exists to avoid, for however long
       it takes the next instruction to run. Latching first means the pin is
       never driven low at any point in this sequence. */
    fwog_link_uart_deinit();
    gpio_init(PIN_LINK_TX);
    gpio_put(PIN_LINK_TX, 1);
    gpio_set_dir(PIN_LINK_TX, GPIO_OUT);
    gpio_init(PIN_LINK_RX);
    gpio_init(PIN_LINK_CTS);
    gpio_init(PIN_LINK_RTS);

    /* I2C is only ever brought up by ship mode, which never returns, but
       reset it anyway: this function must not depend on which paths ran. */
    reset_block_mask(RESETS_RESET_I2C1_BITS);

    /* Clocks, PLLs, XIP and the QSPI pads are deliberately NOT reset. The
       app's crt0 reconfigures clocks itself, and the XIP setup must survive
       because the app's boot2 is skipped. */

    /* --- interrupts --- */
    __asm volatile ("cpsid i" ::: "memory");
    PPB(M0PLUS_SYST_CSR_OFFSET) = 0u;          /* SysTick off  */
    PPB(M0PLUS_NVIC_ICER_OFFSET) = 0xFFFFFFFFu; /* all disabled */
    PPB(M0PLUS_NVIC_ICPR_OFFSET) = 0xFFFFFFFFu; /* all cleared  */
    /* ICER/ICPR above cover only the 32 external IRQs. SysTick and PendSV
       are system exceptions with their own pending bits in ICSR, and writing
       SYST_CSR = 0 disables the counter without clearing an already-pending
       SysTick -- that lives in ICSR.PENDSTCLR, which nothing ICPR touches.
       Clear both: if either is pending when interrupts re-enable below, it
       fires into the app with VTOR already pointing at the app's vector
       table but before the app's own init has run. */
    PPB(M0PLUS_ICSR_OFFSET) =
        M0PLUS_ICSR_PENDSTCLR_BITS | M0PLUS_ICSR_PENDSVCLR_BITS;
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");

    PPB(M0PLUS_VTOR_OFFSET) = APP_VECTORS;
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");

    /* Interrupts are re-enabled inside the asm, after MSP is switched: the
       app's crt0 does not clear PRIMASK, so leaving it set here would hand
       over a CPU that never takes an interrupt again. Every NVIC source is
       already masked and SysTick is off, so nothing can fire in the window
       between cpsie and bx. */
    __asm volatile (
        "msr msp, %0 \n"
        "cpsie i     \n"
        "bx   %1     \n"
        :: "r" (msp), "r" (entry) : "memory");

    __builtin_unreachable();
}
