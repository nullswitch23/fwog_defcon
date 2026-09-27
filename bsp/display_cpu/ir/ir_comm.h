/* IR transmit and receive on PIN_IR_TX (GPIO 9) / PIN_IR_RX (GPIO 16) -- the
 * display CPU's infrared blaster/receiver, Q10 (TX, an N-channel MOSFET low-
 * side switch driving LED8) and IC15 (RX, a TSOP75238TR 38 kHz demodulator).
 * bsp/display_cpu/platform/board.h -- PIN_IR_TX, PIN_IR_RX.
 *
 * Ported from rmpLib/rpIRComm.{h,cpp} (994 lines) -- the largest and least
 * trustworthy reference in this series, ported LAST on purpose: it needs a
 * whole PIO block to itself (25 of 32 instruction words, 3 state machines),
 * whose budget is only settled once WS2812+PDM+I2S are placed in the other
 * block, and it carries a real memory-corruption bug (below) that is a
 * precondition to fix, not a detail to transcribe. See pdm/pdm_mic.h for the
 * PIO and DMA budget this port was built against; the polarity evidence for
 * the traps below is given inline.
 *
 * ---- THE BUG: a one-element array behind a class member, corrupting live
 *      PIO state -- FIXED BY CONSTRUCTION, not merely resized ----
 * `rpIRComm.h:48-49`:
 *
 *     #define IR_MEASUREMENTMENT_MAX 1
 *     int iIRBitTimes[IR_MEASUREMENTMENT_MAX];
 *
 * A ONE-ELEMENT array. `BuildIRDataFromCode()` (`:214-238`) writes up to
 * index ~66 (2 header entries + 64 bit-time entries for 32 NEC bits + 1
 * trailing mark = 67), and `ProcessIRData()`/`ReadIRData()` iterate expecting
 * up to 67 entries. Because `iIRBitTimes` is a CLASS MEMBER, not a stack
 * array, every write past index 0 overflows into whatever member declaration
 * order placed next -- `rpTimer obPlayTimer`, `int iTimerDelay`, and (via
 * `iIRBitTimeCount`/`iIRCodeTotalBits` and the members further down)
 * eventually into `rpPIO obPIORx`, an embedded object holding a PIO
 * instruction cache and DMA wrapper state. Writing 67 `int`s (268 bytes)
 * starting at a 4-byte array is corruption of LIVE PIO state, not a benign
 * stack smash -- it is the class of bug this port's brief singled out as the
 * reason IR had to go last, after three other PIO drivers had already proven
 * the claim/fallback idioms this port reuses.
 *
 * This port does not "resize the array and move on." There is no class
 * member array at all: `ir_nec_encode()`/`ir_nec_decode()` below are pure
 * functions that take a CALLER-SUPPLIED buffer and an explicit capacity --
 * the same shape every other pure function in this BSP already uses
 * (`i2s_audio_fill_buffer()`, `pdm_analyze_buffer()`). `IR_NEC_FRAME_ENTRIES`
 * (67) is the minimum capacity a caller must supply for a full 32-bit NEC
 * frame (2 header + 32*2 bit-times + 1 trailing mark); `ir_nec_encode()`
 * refuses to write anything and returns 0 if the caller's buffer is smaller.
 * There is no adjacent struct member for an overflow to reach, because there
 * is no fixed-size member at all -- the bug's entire precondition (a
 * too-small array embedded in a struct ahead of other live state) does not
 * exist in this port's shape, not merely "made big enough this time."
 *
 * ---- Consequence: RX was NEVER PROVEN on real hardware, in ANY form ----
 * `ReadIRData()`'s capture loop is bounded by
 * `iCount < IR_MEASUREMENTMENT_MAX`, i.e. it captures exactly ONE interval
 * before returning -- so the software bit-bang RX path could not have ever
 * produced a decodable frame as shipped; `ProcessIRData()`'s own guard
 * (`if (iIRBitTimeCount < 16) return;`) would reject every capture it ever
 * received. This port's `ir_nec_decode()` below is correctly sized and
 * host-tested against a full 67-entry frame -- proving the DECODE ARITHMETIC
 * is correct in a way the original never could -- but that is NOT the same
 * claim as "RX works on this board." The PIO receive program
 * (`ir_nec_receive_program_instructions[]` below) decodes bits in HARDWARE
 * and was never reachable by the buggy array at all (see SCOPE below for why
 * the PIO and bit-bang paths are entirely separate implementations) -- but
 * there is equally no positive evidence it was ever exercised on a board
 * either; the legacy monolith's own RX path, in every form it shipped,
 * cannot be cited as proof of anything working. Do not describe any RX
 * behaviour in this port as "faithful to working reference behaviour" -- the
 * reference's RX never worked, in the one form that was even reachable, and
 * was never independently proven in the other. Treat every RX byte this
 * driver ever hands back with that in mind until a board proves otherwise.
 *
 * ---- SCOPE: this file is five subsystems in a trench coat; only the PIO
 *      engine and the pure NEC protocol arithmetic are ported ----
 * `rpIRComm` contains TWO UNRECONCILED IMPLEMENTATIONS PER DIRECTION,
 * selected by a `bUsePIO` flag a caller sets from OUTSIDE these two files:
 *
 *   - TX: `sendIRTransmitPIO()` (PIO, hardware timing) vs.
 *     `SendIRDataStateMachine()`/`SendIRDataBlocking()` (a PWM slice bit-
 *     banged against `iIRBitTimes` and wall-clock polling via
 *     `get_absolute_time()`/`absolute_time_diff_us()`).
 *   - RX: `readIRRxFIFO()` (PIO, hardware decode straight to a 32-bit FIFO
 *     word) vs. `ReadIRData()`/`ProcessIRData()` (`gpio_get()` polling
 *     against wall-clock time, decoding through the buggy `iIRBitTimes`
 *     array above).
 *
 * PORTED: the PIO engine -- `initPIOTx`/`initPIORx` (as `ir_comm_init()`),
 * the three PIO programs, `sendIRTransmitPIO`/`readIRRxFIFO` (as
 * `ir_comm_send()`/`ir_comm_read()`) -- because it is the path with hardware
 * timing guarantees, not a busy-wait against wall-clock time that has no
 * place in a BSP driver.
 *
 * DROPPED, deliberately: the ENTIRE bit-bang path, for both directions.
 * `SendIRDataStateMachine`/`SendIRDataBlocking`/`ReadIRData` and the PWM
 * slice setup in `init()`'s `else` branch are not ported at all -- not
 * behind a flag, not as a fallback. Both busy-wait against wall-clock time
 * (`get_absolute_time()`/`absolute_time_diff_us()` polling loops), which this
 * BSP's other ports have consistently treated as unfit for driver code (no
 * other driver in this BSP polls wall-clock time in a hot loop). A future
 * caller that wants a software fallback when the PIO block is unavailable
 * would need to design one against this project's own conventions, not
 * resurrect this one.
 *
 * `BuildIRDataFromCode()`/`ProcessIRData()` themselves -- the PURE NEC
 * bit-timing arithmetic the bit-bang paths built their I/O around -- ARE
 * ported, as `ir_nec_encode()`/`ir_nec_decode()` below, correctly sized and
 * host-tested. Two things are worth being precise about here: (1) this is
 * the protocol arithmetic, not the bit-bang I/O loops that measured/replayed
 * it against wall-clock time -- those loops are exactly what was dropped
 * above; (2) NEITHER the PIO TX nor the PIO RX path calls these functions at
 * runtime. The PIO TX path (`nec_carrier_control`) takes a raw 32-bit code
 * from `ir_comm_send()` and generates NEC timing directly in PIO hardware,
 * bit by bit, via `out x,1` against the shifted-out code and a fixed IRQ-7
 * schedule -- no bit-times array involved. The PIO RX path
 * (`nec_receive`) decodes bits directly in hardware (`in PINS,1` samples the
 * demodulator output at a fixed delay after each burst) and autopushes a
 * complete 32-bit word to the RX FIFO -- again, no bit-times array. So
 * `ir_nec_encode()`/`ir_nec_decode()` are ported and host-tested as the
 * NEC-protocol reference this file's PIO timing constants were tuned
 * against (the mark/space windows below are exactly what the PIO programs'
 * own delays encode in hardware), and as the correctly-sized fix for the bug
 * above, not because either live PIO path calls them.
 *
 * ALSO DROPPED, deliberately, and NOT required by the PIO engine:
 *   - The replay/fuzz ring buffer (`AddIRCodeToFifo`/`sendNextCode`/
 *     `playIRLog`/`startStopFuzz`/`getNextFuzzCode`/`process()`, ~90 lines).
 *     Pure app-layer sequencing on top of `sendIRTransmitPIO()` -- a caller
 *     that wants to replay or fuzz IR codes can call `ir_comm_send()`
 *     directly on its own schedule. Not ported.
 *   - The IR-code database (`loadIRDatabase`/`parseIRDatabaseLine`/
 *     `createIRDatabase`/`addToIRDatabase`/`sendDataBaseCode`/
 *     `loadRokuDatabase`, ~220 lines). Filesystem (`rpFileSystem`) and GUI
 *     (`rpFwGUI`) coupling -- app layer, out of BSP scope entirely, the same
 *     rationale every other port in this BSP applies to console/filesystem
 *     glue.
 *   - `m_bIRSendFlashLED`/`m_bIRRxFlashLED` -- app-layer "flash a status LED
 *     on activity" bookkeeping, not part of the peripheral driver.
 *
 * ---- Trap 1 (recon-schematic.md trap 1): RX is ACTIVE-LOW. A decoder that
 *      treats "carrier present" as logic high has it backwards ----
 * IC15 is a TSOP75238TR -- an INTEGRATED 38 kHz demodulator, not a bare
 * photodiode. Per the Vishay datasheet its output IDLES HIGH and PULSES LOW
 * while a 38 kHz burst is detected. `rpIRComm::isRxActive()` (`:128-131`)
 * agrees: `return !((bool)gpio_get(m_iInputPin)); // IR is active low`, and
 * `initPIORx()`'s own `#define IR_RX_ACTIVE_STATE 0` / `IR_RX_INACTIVE_STATE
 * 1` (`:469-470`) encode the same fact into the PIO program: `wait 0 pin 0`
 * waits for the ACTIVE (burst-present) state, which is GPIO LOW. This port's
 * `ir_nec_receive_program_instructions[]` below preserves that polarity
 * exactly -- `wait 0 pin 0` (active-low wait), unchanged. The 38.222 kHz live
 * TX carrier (trap 3 below) sits inside the TSOP75238's passband by design,
 * which is why that odd-looking constant is correct, not a typo for
 * 38.000 kHz.
 *
 * ---- Trap 2 (recon-schematic.md trap 2): TX is NON-INVERTING. Do not
 *      invert it ----
 * GPIO 9 -> R22 (330 ohm) -> gate of Q10 (N-channel MOSFET); Q10 drain -> IR
 * LED8 cathode; LED8 anode -> R74 -> `5V_USB`. This is a LOW-SIDE SWITCH:
 * GPIO high turns Q10 on, which pulls the LED cathode toward ground and
 * lights it. GPIO high == LED on, exactly like a normal push-pull GPIO drive
 * -- there is no inverting stage in this path (contrast `PIN_LED_DATA`'s IC6
 * inverter, ws2812_driver.h's own long polarity note, and the hardware record
 * -- IC6 is the ONLY inverting gate on the whole 12-sheet board, and it does
 * not touch this net). `ir_nec_carrier_burst_program_instructions[]` below
 * (`set pins,1` then `set pins,0`) is transcribed unchanged and is already
 * correct for this polarity -- do not "correct" it to look like an
 * active-low drive; that would be the WS2812 near-miss (the hardware record --
 * a driver nearly shipped inverted by trusting an SDK example over the
 * schematic) repeating itself in the opposite direction. R74's value could
 * not be read reliably from the schematic PDF (recon-schematic.md's gaps
 * list) -- this port does not reason about maximum LED drive current or duty
 * cycle anywhere, and neither should a caller of `ir_comm_send()`.
 *
 * ---- Trap 3: three magic numbers, transcribed as TUNED, not "corrected"
 *      against the well-known reference the original embeds as a comment --
 *      LIVE VALUES ONLY ----
 * `rpIRComm.cpp` embeds the well-known mjcross `nec_receive`/
 * `nec_carrier_burst` PIO reference (BSD-3-Clause, preserved in the original
 * source as a ~160-line comment block, `:550-711` -- documentation only, not
 * code, and not reproduced here) and then DELIBERATELY DEVIATES from it in
 * three places:
 *
 *   | | live code (`rpIRComm.cpp`) | the embedded reference comment |
 *   |---|---|---|
 *   | TX carrier frequency | `38.222e3` (`:454`) | `38.000 kHz` |
 *   | RX tick period | `556.00e-6` (`:482`) | `562.5e-6` (`:634-637`) |
 *   | RX burst-loop counter | `31` (`:473`, `IR_RX_BOOST_COUNTER`) | `BURST_LOOP_COUNTER = 30` (`:573`) |
 *
 * These read as hand-tuning against THIS board's actual optics (the 38.222
 * kHz carrier sits inside the TSOP75238's passband by design -- trap 1) and
 * clock tree, not typos. `ir_nec_carrier_burst_clkdiv()`,
 * `ir_nec_rx_clkdiv()`, and `ir_nec_receive_program_instructions[]`'s own
 * `set x, 31` below transcribe the LIVE values -- 38.222 kHz, 556.00 us, and
 * 31 -- not the textbook ones in the adjacent comment. Do NOT "correct"
 * these back to the reference's 38.000 kHz / 562.5 us / 30 -- that would be
 * silently undoing this board's own tuning.
 *
 * ONE constant is NOT part of this deviation and is transcribed as the
 * reference has it, unchanged: the TX side's `nec_carrier_control` tick
 * rate (`2 * (1 / 562.5e-6f)`, `rpIRComm.cpp:460`) uses the REFERENCE
 * 562.5 us, not the RX-only tuned 556.00 us -- these are two independent
 * clock dividers on two independent state machines (`nec_carrier_control`
 * for TX, `nec_receive` for RX), and only the RX one was hand-tuned. Mixing
 * them up (e.g. using 556.00 us for TX timing) would be a fabricated
 * "consistency fix" this file's own author never made.
 * `ir_nec_carrier_control_clkdiv()` below transcribes `2 * (1/562.5e-6)`
 * exactly, kept as a separate function from `ir_nec_rx_clkdiv()` specifically
 * so this distinction cannot be accidentally collapsed into one shared
 * constant later.
 *
 * ---- No DMA, no DMA IRQ -- despite the catalog's old "wants DMA" note ----
 * `rpIRComm.{h,cpp}` contains NO `dma_` call at all (recon-dma.md). This
 * driver claims zero DMA channels and installs no DMA IRQ handler --
 * the peripheral catalog's IR row is corrected alongside this port landing, the same way
 * WS2812's was.
 *
 * ---- A whole PIO block, three state machines, no PIO-to-NVIC IRQ ----
 * This driver needs 25 of 32 instruction words across three programs --
 * `nec_receive` (9), `nec_carrier_burst` (5), `nec_carrier_control` (11) --
 * which is why recon-dma.md gives IR a WHOLE PIO BLOCK to itself rather than
 * folding it into the block WS2812+PDM+I2S already share (16 of 32 words
 * there; adding IR's 25 to either block would exceed 32 and
 * `pio_add_program()` would refuse). `ir_comm_init()` below takes the PIO
 * block and the RX state machine as CALLER-CHOSEN parameters, exactly like
 * `ws2812_init()`/`pdm_mic_init()`/`i2s_audio_init()` -- never hardcoded.
 * `irq nowait 7` / `wait 1 irq 7` inside the PIO programs is STATE-MACHINE-
 * TO-STATE-MACHINE signalling entirely inside the PIO hardware's own 8 IRQ
 * flags -- it is NOT wired to the NVIC, and this driver installs no
 * `irq_set_enabled()`/`irq_set_exclusive_handler()` call of any kind. Contrast
 * `pdm_mic.h`'s trap 2 (an EXCLUSIVE, NVIC-visible `DMA_IRQ_0` claim) -- this
 * driver has nothing analogous to that at all.
 *
 * `initPIORx()` unconditionally calls `initPIOTx()` (`rpIRComm.cpp:546`), so
 * all three state machines came up together in the original, and
 * `ir_comm_init()` below preserves that: there is one init entry point, not
 * a separate TX-only or RX-only bring-up. TX and RX CAN coexist -- separate
 * state machines, separate GPIOs (9 and 16), no electrical conflict -- and
 * always have, in this driver's own design.
 *
 * ---- The PIO programs: instruction words, and how they were obtained ----
 * `nec_carrier_burst_program_instructions[]` (5 words) and
 * `nec_carrier_control_program_instructions[]` (11 words) are transcribed
 * UNCHANGED from the ALREADY-COMPILED arrays the original source embeds
 * (`rpIRComm.cpp:32-46`/`:54-70`) -- delay values, jump targets, and wrap
 * points exactly as encoded there. Both used a FIXED `.origin` in the
 * original (19 and 14, respectively -- a whole-firmware layout fact, not a
 * property of the program); this port floats both (`.origin = -1`), exactly
 * like every other PIO program in this BSP, and lets `pio_add_program()`'s
 * own return value decide the load offset -- see i2s_audio.h's PIO note for
 * why a fixed origin is fragile and how this BSP replaces it uniformly. This
 * is safe because the RP2040 PIO loader (`pio_add_program_at_offset()`)
 * relocates every `JMP` target by adding the actual load offset at load
 * time; the arrays' `JMP` operands already encode addresses LOCAL to each
 * program (e.g. `nec_carrier_control`'s `jmp x--,2` targets its own local
 * address 2, not a global instruction-memory address), which is exactly what
 * a floating-origin program requires and exactly how the original's own
 * fixed-origin arrays already happened to be written.
 *
 * `ir_nec_receive_program_instructions[]` (9 words) is NOT copied from a
 * compiled array in the original -- `initPIORx()` builds this program at
 * RUNTIME via `rpPIO`'s own micro-assembler (`obPIORx.encode_set()`/
 * `encode_wait()`/`encode_jmp()`/`encode_mov()`/`encode_in()`/`encode_nop()`
 * calls, `rpIRComm.cpp:506-541`), not a static instruction array `rpPIO`
 * could be inspected against directly. This port instead HAND-ENCODES the
 * same nine PIO instructions the original's `encode_*` call sequence
 * describes -- set X,31 / wait 0 pin 0 / jmp pin,data_bit / jmp X--,burst_loop
 * / mov ISR,NULL / wait 1 pin 0 / jmp always,next_burst / nop[14] /
 * in PINS,1 -- as a fixed `static const uint16_t[9]` array, matching every
 * other PIO program in this BSP's own style (a literal instruction table,
 * not a runtime-built one). Each instruction's encoding was derived from the
 * RP2040 PIO instruction format and cross-checked against SEVERAL already-
 * verified encodings already present in this same file (`0x0042` = `jmp
 * x--,2` appears in BOTH this port's derivation and the original's own
 * compiled `nec_carrier_burst_program_instructions[4]`; `0xc007` = `irq
 * nowait 7` and `0xaf42` = `nop [15]` appear in the original's own compiled
 * `nec_carrier_control_program_instructions[]`; `0x4001` = `in pins,1`
 * matches `pdm_mic_program_instructions[1]` in this same BSP) -- so the
 * instruction-format derivation itself is checked against known-correct
 * silicon behaviour from three independent sources, not derived once and
 * trusted blind. `tests/test_ir.c` pins the exact nine words the same way
 * `tests/test_ws2812.c`/`test_pdm.c`/`test_i2s.c` pin theirs, specifically so
 * a future "fix" to this array is caught before it reaches hardware.
 *
 * ---- pio_sm_put(), not pio_sm_put_blocking() -- transcribed as-is ----
 * `sendIRTransmitPIO()` (`:115-126`) calls `pio_sm_put()` -- the
 * NON-BLOCKING form -- onto the `nec_carrier_control` state machine's TX
 * FIFO (joined to depth 8 via `PIO_FIFO_JOIN_TX`). `ir_comm_send()` below
 * does the same. This means a caller that sends a new code while the FIFO
 * already holds 8 unconsumed words overwrites hardware state rather than
 * blocking or being told the FIFO was full -- a real, if narrow, correctness
 * gap (a caller sending faster than ~one frame every few hundred
 * microseconds could lose a code silently). This is NOT one of the defects
 * this port's brief singled out for a fix (only the array-sizing bug and
 * the polarity questions were), so it is transcribed exactly as the
 * original has it, not "improved" on suspicion alone -- callers that send
 * IR codes back-to-back faster than the hardware can drain them should be
 * aware of this rather than assume `ir_comm_send()` blocks or reports
 * failure.
 */
#ifndef FWOG_IR_COMM_H
#define FWOG_IR_COMM_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Minimum caller-supplied buffer capacity for a full 32-bit NEC frame: 2
 * header entries (leading mark, leading space) + 32 bits * 2 entries each
 * (mark, space) + 1 trailing mark = 67. Transcribed from
 * `BuildIRDataFromCode()`'s own `iIRBitTimeCount = 67;` (`rpIRComm.cpp:236`)
 * -- see the header's THE BUG note for why this number, correctly enforced
 * against a caller-supplied capacity rather than an oversized-on-paper class
 * member, is the actual fix. */
#define IR_NEC_FRAME_ENTRIES 67u

/* ---- Pure: host-tested, no PIO/SDK dependency ---- */

/* Encode a 32-bit NEC code into its bit-time sequence (microsecond mark/space
 * durations), transcribed from `BuildIRDataFromCode()` (`rpIRComm.cpp:214-
 * 238`): a 9000 us leading mark, a 4500 us leading space, then per bit
 * (LSB first, matching `iNewCode & (1 << iCount)`) either "563,1688" (logic
 * 1) or "563,563" (logic 0), followed by one final 563 us trailing mark.
 * Writes into `bit_times[0..IR_NEC_FRAME_ENTRIES)` and returns
 * IR_NEC_FRAME_ENTRIES (67) on success. Returns 0 and writes nothing if
 * `capacity < IR_NEC_FRAME_ENTRIES` or `bit_times` is NULL -- see the
 * header's THE BUG note: this capacity check, against a caller-supplied
 * buffer, is what replaces the original's one-element class member. Neither
 * the live PIO TX path (`ir_comm_send()`, which hands a raw 32-bit code
 * straight to the `nec_carrier_control` state machine) nor anything else in
 * this driver calls this function at runtime -- see the header's SCOPE note
 * for why it is ported and host-tested anyway. */
size_t ir_nec_encode(uint32_t code, int *bit_times, size_t capacity);

/* Decode a bit-time sequence back into a NEC code, transcribed from
 * `ProcessIRData()` (`rpIRComm.cpp:173-204`). Returns false (leaving
 * `*out_code`/`*out_bits` at 0) if `count < 16` or the header entries
 * (`bit_times[0]` not in (7000,9500), `bit_times[1]` not in (3500,5000)) are
 * out of range -- matching the original's own early-return guards exactly.
 * Otherwise decodes bit-time PAIRS starting at index 2: `bit_times[i]` (the
 * mark) must fall in (400,650) us or decoding stops immediately, keeping
 * whatever `*out_code`/`*out_bits` had already accumulated -- transcribed
 * faithfully, including that a single malformed mark anywhere aborts the
 * REST of the frame rather than just that one bit, exactly like the
 * original's own unconditional `return` on a bad mark. `bit_times[i+1]` (the
 * space) is checked ONLY for the logic-1 window (1000,2000) us EXCLUSIVE at
 * both ends -- any value outside that window, including implausible ones,
 * is silently treated as logic 0, exactly like the original's `if (...)
 * iIRCode += ...` with no `else` branch and no independent validation of the
 * logic-0 case. Decoding stops after 32 bits (matching
 * `if (iCurrentBit == 32) return;`). ONE deliberate addition not present in
 * the original: `bit_times[i+1]` is never read when `i+1 >= count` -- the
 * original's own loop (`iCount < iIRBitTimeCount; iCount += 2`) can read one
 * past the caller's last entry if `count` is odd, which was harmless ONLY
 * because the original's array was always logically imagined at a fixed 67
 * entries in the class-member era and the buggy `IR_MEASUREMENTMENT_MAX = 1`
 * meant this code path was never reachable with real data in the first
 * place (see the header's THE BUG note). Now that `count` is a real,
 * caller-supplied bound, reading past it would be actual undefined behaviour
 * in a hosted C function -- this bounds check is required for correctness of
 * THIS port's shape, not a reinterpretation of NEC protocol semantics.
 * Neither live PIO path calls this function at runtime -- see the header's
 * SCOPE note. */
bool ir_nec_decode(const int *bit_times, size_t count, uint32_t *out_code,
                   int *out_bits);

/* ---- Frame integrity and field accessors, for the 32-bit word
 * `ir_comm_read()` hands back. Layout, MSB first: [~cmd][cmd][addr_hi][addr_lo].
 *
 * WHY THESE EXIST. `ir_comm_read()` returns whatever the receive state machine
 * decoded, with no validation -- the PIO program counts pulse-distance timings
 * and cannot tell a corrupted bit from a real one. On 2026-07-28 this board
 * received 92 frames from a household remote and THREE of them carried a
 * command byte that did not match its own complement (the hardware record). They came
 * back from `ir_comm_read()` indistinguishable from the good ones. NEC carries
 * the complement precisely so a receiver can reject those, and until these
 * functions existed nothing in this BSP did.
 *
 * These are pure and host-tested against the real captured frames, including
 * the three corrupt ones -- see tests/test_ir.c. ---- */

/* True if the command byte and its complement agree. This is NEC's own
 * integrity check and it applies to BOTH protocol variants; a caller that acts
 * on a frame without checking this can act on a command that was never sent.
 * All-zeros and all-ones -- what a stuck receive pin produces -- both fail. */
bool ir_nec_command_valid(uint32_t code);

/* The command byte. Meaningful only when ir_nec_command_valid() is true. */
uint8_t ir_nec_command(uint32_t code);

/* The raw 16-bit address field, which is what an EXTENDED NEC remote means by
 * "address". For classic NEC this is [~addr][addr] and the address proper is
 * the low byte. */
uint16_t ir_nec_address16(uint32_t code);

/* True if the two address halves are complements, i.e. this is a CLASSIC NEC
 * frame (8-bit address) rather than an EXTENDED one (16-bit address).
 *
 * This is NOT an error check, and must never be used as one: extended NEC is
 * entirely legal, and the household remote captured in the hardware record is
 * extended. Treating a failed address complement as a bad frame would discard
 * every extended remote on the market. This board's own ir_comm_send() emits
 * classic frames, so a self-loopback round trip does satisfy it. */
bool ir_nec_address_is_classic(uint32_t code);

/* Popular consumer IR PHYs OpticClick can arm. Only one is loaded into
 * pio1 at a time (the NEC trio already fills 25 of 32 words). Switching
 * retunes the 38 kHz carrier SM (36 kHz RC5 / 40 kHz SIRC) and either
 * keeps the NEC bit-control + receive SMs or CPU-gates the carrier for
 * SIRC/RC5 mark/space. The TSOP is still a 38 kHz demodulator: TX at 36/40
 * kHz is in-band enough to try; RX of SIRC/RC5 is a measurement. */
typedef enum {
    IR_PHY_NEC = 0,
    IR_PHY_SIRC12,
    IR_PHY_SIRC15,
    IR_PHY_SIRC20,
    IR_PHY_RC5,
    IR_PHY_COUNT
} ir_phy_t;

const char *ir_phy_name(ir_phy_t phy);
float ir_phy_carrier_hz(ir_phy_t phy);
unsigned ir_sirc_nbits(ir_phy_t phy); /* 0 if not SIRC */

/* Mark/space microseconds, mark first. Host-tested. */
size_t ir_sirc_encode(uint32_t code, unsigned nbits, int *us, size_t cap);
size_t ir_rc5_encode(uint32_t code14, int *us, size_t cap);

/* PIO clock divider for the `nec_carrier_burst` state machine's 38.222 kHz
 * TX carrier (the LIVE, tuned value -- see the header's trap 3 note; do NOT
 * substitute the reference comment's 38.000 kHz), at
 * `nec_carrier_burst`'s own 4 ticks per carrier cycle. Transcribed from
 * `nec_carrier_burst_program_init()`'s
 * `clock_get_hz(clk_sys) / (freq * nec_carrier_burst_TICKS_PER_LOOP)`
 * (`rpIRComm.cpp:380`), expressed against a parameter instead of calling
 * `clock_get_hz()` directly so it is host-testable. Never hardcode this:
 * this board's clk_sys is 200 MHz, not the Pico SDK's 125 MHz default -- see
 * the test for both. */
float ir_nec_carrier_burst_clkdiv(uint32_t sys_clk_hz);

/* PIO clock divider for the `nec_carrier_control` state machine, 2 ticks per
 * 562.5 us carrier-burst period -- the REFERENCE value, NOT the RX-only
 * tuned 556.00 us (see the header's trap 3 note for why these two are
 * deliberately different functions). Transcribed from
 * `initPIOTx()`'s own `2 * (1 / 562.5e-6f)` tick-rate argument
 * (`rpIRComm.cpp:460`) fed through `nec_carrier_control_program_init()`'s
 * `clock_get_hz(clk_sys) / tick_rate` (`:418`). Never hardcode this: see the
 * test for both 125 MHz and this board's 200 MHz. */
float ir_nec_carrier_control_clkdiv(uint32_t sys_clk_hz);

/* PIO clock divider for the `nec_receive` state machine, 10 ticks per the
 * LIVE, tuned 556.00 us burst period (see the header's trap 3 note; do NOT
 * substitute the reference comment's 562.5 us). Transcribed from
 * `initPIORx()`'s own `(double)clock_get_hz(clk_sys) / (10.0 / 556.00e-6)`
 * (`rpIRComm.cpp:482`) -- kept as `double` to match the original's own
 * variable type exactly; the call site narrows to `float` for
 * `sm_config_set_clkdiv()`, matching what the original's `rpPIO` wrapper
 * does internally. Never hardcode this: see the test for both 125 MHz and
 * this board's 200 MHz. */
double ir_nec_rx_clkdiv(uint32_t sys_clk_hz);

/* The three raw PIO instruction-word tables. See the header's "PIO
 * programs" note above for exactly how each was obtained (two transcribed
 * unchanged from the original's own compiled arrays; `nec_receive` hand-
 * encoded from the original's runtime `encode_*` call sequence and
 * cross-checked against known-correct encodings elsewhere in this BSP).
 * Exposed here (not behind HOST_TEST -- plain data, no SDK dependency) so
 * tests/test_ir.c can pin the exact words, the same pattern
 * ws2812_program_instructions[]/pdm_mic_program_instructions[]/
 * i2s_audio_program_instructions[] use. Defined in ir_comm.c. */
extern const uint16_t ir_nec_receive_program_instructions[9];
extern const uint16_t ir_nec_carrier_burst_program_instructions[5];
extern const uint16_t ir_nec_carrier_control_program_instructions[11];

/* The idempotency guard for ir_comm_init() -- pure and host-tested for the
 * same reason pdm_mic_needs_init() is (pdm_mic.h trap 8): ir_comm_init()
 * itself is hardware-only, so this predicate is what makes its "a second
 * call while already running is a safe no-op" guarantee testable without a
 * board. Unlike PDM, a repeat ir_comm_init() call has no DMA channel or IRQ
 * handler to leak -- there is neither here -- but it WOULD re-add all three
 * PIO programs a second time, consuming more of this driver's (otherwise
 * comfortably sized: 25 of 32 words) dedicated PIO block for no reason. */
bool ir_comm_needs_init(bool already_ready);

#ifndef HOST_TEST
#include "hardware/pio.h"

/* Claim three consecutive state machines on the given PIO block --
 * `sm_rx`, `sm_rx + 1` (TX carrier burst), `sm_rx + 2` (TX carrier control)
 * -- load all three PIO programs, configure the RX and TX pins, and start
 * all three state machines. Matches `initPIORx()`'s own behaviour exactly:
 * it unconditionally calls `initPIOTx()` (`rpIRComm.cpp:546`), so calling
 * this ONE function brings up TX and RX together, matching the original's
 * "you cannot have one without the other" shape (see the header's PIO-block
 * note for why that is fine: separate state machines, separate GPIOs, no
 * electrical conflict).
 *
 * `pio` and `sm_rx` are caller-chosen, never hardcoded -- see the header's
 * PIO-block note. `sm_rx` must leave `sm_rx + 2` a valid state machine index
 * (0-3) on `pio`, i.e. `sm_rx` must be 0 or 1 -- this driver's own dedicated
 * PIO block (recon-dma.md) is expected to start it at 0, leaving state
 * machine 3 free. Uses PIN_IR_TX/PIN_IR_RX from board.h; this board has
 * exactly one IR transmitter and one IR receiver, so unlike the PIO
 * block/state machine that is a board fact, not a caller choice.
 *
 * Returns false, with the driver left not-ready, if any of the three PIO
 * programs does not fit in `pio`'s instruction memory -- checked with
 * `pio_can_add_program()` before each `pio_add_program()` call, with any
 * already-added program(s) removed via `pio_remove_program()` before
 * returning, mirroring `i2s_audio_init()`'s own multi-point rollback (there,
 * across two DMA channel claims; here, across three PIO program loads) so a
 * failed call leaks NOTHING back onto a shared block. In practice this
 * driver's own dedicated block starts empty and needs only 25 of 32 words,
 * so this path is not expected to be reachable in normal operation -- it
 * exists because this BSP's convention is "checked, not asserted"
 * (pdm_mic.h's own trap 2 note makes the same point), not because a failure
 * is anticipated here.
 *
 * IDEMPOTENT: a second call while already running is a safe no-op that
 * returns true and touches no hardware state -- see ir_comm_needs_init()
 * above. */
bool ir_comm_init(PIO pio, uint sm_rx);

/* Send one code. NEC uses the `nec_carrier_control` TX FIFO (`pio_sm_put`,
 * non-blocking). SIRC/RC5 CPU-gate the retuned carrier burst. No-op if
 * `ir_comm_init()` has not succeeded. */
void ir_comm_send(uint32_t code);

/* Select which PHY's timing is loaded. No-op if already that PHY.
 * NEC restores the three PIO SMs; SIRC/RC5 park the NEC control+RX SMs
 * and retune the carrier burst. False if init has not succeeded. */
bool ir_comm_set_phy(ir_phy_t phy);
ir_phy_t ir_comm_phy(void);

/* Read one code. NEC pops the `nec_receive` FIFO. SIRC/RC5 GPIO-time the
 * demodulator pin (measurement). False if init failed, `out_code` is NULL,
 * or nothing is pending. See the header's "RX was never proven" note. */
bool ir_comm_read(uint32_t *out_code);

/* Stop all three state machines. Leaves the PIO programs loaded (this BSP's
 * drivers never release a claim once taken -- st7789.c's/pdm_mic.c's own
 * note) so a later ir_comm_init() call is not the intended use; exists for
 * symmetry with ws2812/pdm/i2s's own stop functions, not because anything in
 * this BSP calls it today. */
void ir_comm_stop(void);

#endif
#endif
