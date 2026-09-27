/* I2S speaker output on PIN_I2S_DIN (GPIO 4) / PIN_I2S_LRCLK (GPIO 5) /
 * PIN_I2S_BCLK (GPIO 6) -- the display CPU's only speaker driver, IC17
 * (MAX98357AETE+T). bsp/display_cpu/platform/board.h -- PIN_I2S_DIN,
 * PIN_I2S_LRCLK, PIN_I2S_BCLK.
 *
 * Ported from rmpLib/rpI2S.{h,cpp} (1027 lines -- the largest port in this
 * series). This is the THIRD and FOURTH DMA claimant in this BSP (after
 * lcd/st7789.c's 1 and pdm/pdm_mic.c's 1) and the FIRST to use a
 * HARDWARE-CHAINED pair of DMA channels rather than a single channel or a
 * software-re-armed one. The DMA and PIO budget this port was built against is
 * set out in pdm/pdm_mic.h; the hardware traps it depends on are documented
 * inline below.
 *
 * ---- SCOPE: what is ported and what is not ----
 * Ported: (a) the PIO program and bring-up (audio_i2s_program, initI2S,
 * update_pio_frequency); (b) the hardware DMA chain and the playback state
 * machine (setupDMAChain, process, finishAndReset); (c) buffer fill and
 * format conversion (fillBuffer's mono/stereo down-mix, 8->16-bit expansion,
 * and VOLUME_MULTIPLIERS volume scaling).
 *
 * Deliberately NOT ported:
 *   - TTS (speak()/LockAndSpeak()/finishSpeaking(), which call
 *     rpTTS_SAM::talk()). rpTTS_SAM::talk() uses the DISPLAY CANVAS PIXEL
 *     BUFFER as scratch RAM -- a real aliasing hazard against lcd/st7789.c's
 *     framebuffer-less drawing model, and not a peripheral-driver concern in
 *     the first place. Left out entirely; see the catalog row.
 *   - Sound-source / filesystem glue: startSoundFromFile(), playROMSound(),
 *     playNumberChar()/playNumbers(), startSoundFromFlashPointer(). These
 *     depend on rpFileSystem and the WavesArray/WavesArray2 asset tables --
 *     app-layer concerns. This driver exposes i2s_audio_start() instead,
 *     matching startSound()'s own signature (a raw sample pointer, a count,
 *     a mono flag, an 8-bit flag) -- the one entry point in the original that
 *     already takes its audio FROM THE CALLER rather than reaching into a
 *     filesystem or asset table itself. An app supplies the audio.
 *   - startSoundFromFileOLD() (rpI2S.cpp:637-711) -- dead, superseded by
 *     startSoundFromFile(); still compiled in the original but never called
 *     from anywhere that matters. Not ported.
 *   - The dead 743-byte `temp[]` test-audio array under `#ifndef TEST_H`
 *     (rpI2S.cpp:259-292) and the entirely commented-out PC-side
 *     buildWavFileHeader() tool (:296-346). Neither is reachable code.
 *   - ToneGen-based tone generation (playTestBeep(),
 *     playSoundFromFrequencyAndDuration(), the `playingGeneratedTone` branch
 *     of fillBuffer()). Not named in this port's scope, depends on a ToneGen
 *     class this BSP does not carry, and is a signal-GENERATING feature, not
 *     format conversion of caller-supplied audio. An app that wants a beep
 *     can synthesize samples itself and hand them to i2s_audio_start() like
 *     any other sound.
 *   - ASSETS_NEW_FLOAT_ATENUATION (rpI2S.h:19, 0.30) -- applied to only one
 *     of two legacy asset tables (WavesArray2), for no stated reason. It is
 *     an asset-table-selection detail belonging to the sound-source layer
 *     this port does not carry (see above); i2s_audio_set_asset_gain() below
 *     is the generic knob an app-layer asset table would drive with whatever
 *     per-asset attenuation it wants -- 0.30, 1.0, or anything else -- this
 *     driver does not bake in any particular value.
 *
 * ---- Trap 1: TWO panicking DMA claims, non-panicking here with rollback ---
 * `rpI2S::initI2S()` claims its two channels via
 * `obDMA_a.initClaimFreeDMAChannel()` / `obDMA_b.initClaimFreeDMAChannel()`
 * (rpI2S.cpp:189-190), which is `rpDMA::initClaimFreeDMAChannel()`
 * (`rpDMA.cpp:13`) -- `dma_claim_unused_channel(true)`, the PANICKING form,
 * called TWICE. Two claims means two independent chances to panic, against
 * this project's convention (the peripheral catalog's DMA note, st7789.c, pdm_mic.c):
 * `dma_claim_unused_channel(false)` with the failure handled explicitly,
 * because the display CPU cannot be re-flashed without physical BOOTSEL
 * access it does not have.
 *
 * i2s_audio_init() below claims BOTH channels with `(false)`, and handles the
 * THREE distinct partial-failure points a two-channel claim creates that a
 * one-channel claim (st7789, pdm) never has to:
 *   1. First claim fails -> return false immediately. Nothing to unwind.
 *   2. First claim succeeds, second fails -> UNCLAIM the first before
 *      returning false. This is exactly the leak class pdm_mic.c's own
 *      header calls out ("PDM was just sent back for exactly that class of
 *      leak") -- two claims is two chances to leak one.
 *   3. Both claims succeed, but the PIO program does not fit on the shared
 *      block -> unclaim BOTH before returning false, mirroring pdm_mic_init()
 *      claiming its DMA channel before pio_add_program() runs so a failed
 *      program load never has to unwind a program (there is nothing to
 *      unwind on the PIO side; unclaiming the DMA channels is the only
 *      cleanup needed).
 * A failed i2s_audio_init() call at any of these three points leaves the
 * driver not-ready and leaks NOTHING -- no DMA channel, no PIO program space.
 *
 * ---- Trap 2: the amplifier has no software shutdown ----
 * IC17 (MAX98357AETE+T) has SD_MODE (pin 4) hard-pulled to 3V3 through R46
 * (220 ohm), with NO GPIO wired to it (recon-schematic.md trap 5). The amp is
 * enabled whenever 3V3 is up -- there is no mute/shutdown line to design
 * around. Silence is whatever this driver clocks out, which has two concrete
 * consequences a caller must understand:
 *   - An IDLE I2S BUS IS AN AUDIBLE STATE, not an off state. Whatever the PIO
 *     shift register last held keeps looping out at the bit clock rate for as
 *     long as the state machine runs, even with no DMA feeding it.
 *   - THE IDLE BUS IS SILENT FROM BOOT AND NOISY AFTER A NOTE, and that
 *     asymmetry is why this bug survives bring-up. A starved state machine
 *     re-shifts whatever it last held: from boot that is zeros, so it is
 *     silent; after a note it is audio samples, so it is noise. Testing for
 *     silence before playing anything is a FALSE PASS. Measured on hardware
 *     2026-07-28 while porting an application, by a human listening after a
 *     button press -- nothing in the build or the diagnostics caught it.
 *   - i2s_audio_stop() (and the natural end-of-playback path,
 *     i2s_audio_process()'s call into the equivalent of finishAndReset())
 *     push ONE explicit zero sample onto the PIO TX FIFO after aborting both
 *     DMA channels -- transcribed from finishAndReset()'s own
 *     `sendI2SData(0)` (rpI2S.cpp:986) -- but on its own that is BEST EFFORT,
 *     not a real mute: one word, written once, into a FIFO/shift-register
 *     pipeline that may still be mid-way through shifting out whatever sample
 *     arrived immediately before the abort.
 *
 * THE SUPPORTED WAY TO BE SILENT IS i2s_audio_park(), and it is automatic.
 * Every path that ends playback parks the state machine, so BCLK and LRCLK
 * stop and the amplifier has nothing to amplify. i2s_audio_start() unparks,
 * so a caller never has to think about it. An app needs i2s_audio_park()
 * explicitly only to silence the bus mid-note.
 *
 * DO NOT try to hold the bus quiet by feeding zeros from application code.
 * It cannot work, and the arithmetic is not close: the state machine consumes
 * 8000 frames/s, the TX FIFO is 4 entries deep -- 0.5 ms of audio -- and a
 * 2 ms application loop can top it up once per iteration at best. The FIFO is
 * empty for most of every loop period no matter how tight the loop is, and
 * the SM re-shifts stale content the whole time. That approach was tried on
 * hardware first; it only makes the noise intermittent.
 *
 * DO NOT try to stop the state machine from application code either. A safe
 * resume needs THREE things, and pio_sm_restart() gives you only the first:
 *
 *   1. the shift counters and divider phase cleared -- pio_sm_restart();
 *   2. the bit-loop counter x re-set, because the program loads x itself only
 *      at instructions 3 and 7, i.e. AFTER the left channel, so the first
 *      channel out of any entry uses whatever x was left over. Get this wrong
 *      and the 32-bit autopull boundary drifts off the L/R frame and every
 *      sample is bit-shifted -- loud digital noise;
 *   3. the program counter sent back to the entry point, which is
 *      pio_add_program()'s return value inside i2s_audio.c.
 *
 * An application cannot do 3 at all -- that offset is not exposed -- and 2 is
 * easy to miss because the program does reload x every frame, just not soon
 * enough. Both failures are INTERMITTENT, which is how an app-side attempt
 * and a first version of the in-driver fix each passed their first test.
 *
 * ---- Trap 3: GAIN_SLOT termination is UNCONFIRMED -- do not assume a
 *      channel/gain mode ----
 * IC17 pin 2 (GAIN_SLOT) could not be confirmed floating or grounded from the
 * schematic text layer (recon-schematic.md trap 6) -- a floating GAIN_SLOT
 * means 9 dB gain and L+R MONO per the MAX98357A datasheet. This driver does
 * not assume either: i2s_audio_start()'s `force_mono` flag is caller-supplied
 * (matching startSound()'s own `bForceMono` parameter, which the legacy
 * callers set explicitly per sound, not derived from any amplifier gain
 * mode), and the PIO program emits whatever 16-bit words the buffer-fill
 * path produces onto a single DIN line regardless of what IC17 internally
 * does with them. If GAIN_SLOT does turn out to force L+R mono internally,
 * every code path here still behaves identically -- there is no assumption
 * anywhere in this file that the amplifier is stereo-capable.
 *
 * ---- Trap 4: finishAndReset() aborts BOTH channels unconditionally -- this
 *      is correct, not merely harmless ----
 * `rpI2S::finishAndReset()` (:981-1000) calls `obDMA_a.abort()` then
 * `obDMA_b.abort()` (:983-984) even though process() only ever observes ONE
 * channel completing at the moment finishAndReset() runs. Tracing why both
 * aborts are needed, not just harmless:
 *
 * setupDMAChain() (:890-929) configures channel A with
 * `channel_config_set_chain_to(&a_cfg, obDMA_b.getChannel())` and channel B
 * with `channel_config_set_chain_to(&b_cfg, obDMA_a.getChannel())` -- each
 * channel's completion re-triggers the OTHER, IN HARDWARE, with no software
 * involvement and no delay for a poll loop to notice. Channel A is started
 * immediately (`dma_channel_configure(..., true)` at :925); channel B is
 * configured but NOT started (`..., false` at :911) -- it only ever starts
 * because A's completion chains into it.
 *
 * So by the time process() (:932-979) OBSERVES that channel A has completed
 * (`obDMA_a.isComplete()`), channel B has ALREADY been auto-triggered by the
 * hardware chain and may already be actively shifting samples into the PIO
 * FIFO -- there is no software step between A's completion and B's start for
 * process() to intervene in. When the sample counters show playback is
 * finished (`m_iTotalSamples == m_iCurrentSample`) and finishAndReset() runs,
 * channel A is idle (it is the one process() just observed complete) but
 * channel B is very likely RUNNING (chain-triggered the instant A finished).
 * Aborting A is a no-op on an idle channel -- `dma_channel_abort()` reads the
 * ABORT trigger, and the SDK's own busy-wait inside it returns immediately
 * when the channel was not running to begin with. Aborting B is what
 * actually stops sound: it is the channel doing real work at that moment.
 * The unconditional double-abort is therefore CORRECT, not merely tolerated
 * -- exactly which channel is "the idle one" and which is "the running one"
 * flips every other call (A-then-B when state was playingA, B-then-A when
 * state was playingB), so a version that only aborted "the channel process()
 * just checked" would leave the OTHER, actively running channel armed and
 * clocking out real audio after the driver believes playback has stopped.
 * i2s_audio_process()'s internal stop path (the equivalent of
 * finishAndReset()) preserves this exact unconditional double-abort.
 *
 * CORRECTION (fix round 1): the paragraph above proves that aborting BOTH
 * channels is necessary ONCE THE DECISION TO STOP HAS BEEN MADE. It says
 * nothing about WHEN that decision is correct to make -- and the original's
 * own criterion for that (`m_iTotalSamples == m_iCurrentSample`,
 * :944/:964) is WRONG, in a way this port initially transcribed unchanged
 * and a review caught. `m_iCurrentSample`/`s_current_sample` is a FILL
 * cursor, advanced every time a buffer is FILLED, not a PLAY cursor. Because
 * both ping-pong buffers are pre-filled before the chain ever starts (see
 * the "buffer-fill deviations" note below), the fill cursor reaches
 * `total_samples` up to one WHOLE BUFFER ahead of what has actually been
 * heard. Concretely: a sound that is an exact multiple of the buffer size
 * has BOTH buffers fully filled, and the fill cursor already AT the total,
 * before channel A has played a single sample. The first time
 * `process()` observes A complete, the fill-cursor check says "finished" --
 * and `finishAndReset()`/its equivalent then aborts channel B, WHICH IS AT
 * THAT MOMENT ACTIVELY PLAYING THE SOUND'S ENTIRE SECOND HALF (already
 * auto-triggered by the hardware chain the instant A completed, per the
 * mechanism this trap's own paragraph above describes). Real audio is
 * silently truncated -- not a hypothetical edge case, but the norm for any
 * sound longer than one buffer, with the truncated fraction ranging from
 * negligible up to a full buffer depending on where the source ends
 * relative to a buffer boundary. This traces identically through
 * `rpI2S::process()`/`finishAndReset()` -- an inherited reference defect,
 * not a regression introduced by this port -- but it went unnoticed and
 * undocumented in the initial pass, which is what makes it a finding.
 *
 * THE FIX: the stop decision in `i2s_audio_process()` no longer consults
 * the fill cursor (`i2s_audio_samples_remain()`) at all. Instead it checks
 * `i2s_audio_chain_drained()` against the SIBLING buffer's own most recent
 * fill -- the buffer the hardware chain has already auto-triggered, or is
 * about to, the instant the currently-tracked buffer's transfer completes.
 * `s_buf_consumed[2]` (indexed by `i2s_audio_buffer_t`) records how many
 * REAL samples each buffer's last fill produced; a buffer whose last fill
 * produced zero real samples contains nothing but the zero-padding this
 * port's own earlier fix guarantees is there (see "buffer-fill deviations"
 * below) -- so aborting it, whether it is idle or has already started
 * playing a few samples of pure silence, costs nothing. A buffer whose last
 * fill produced ANY real samples must be allowed to finish playing before
 * the chain can be stopped.
 *
 * Why this is the correct fix, not merely a smaller version of the same
 * bug: the mutual, permanent `chain_to` link means the hardware ALWAYS
 * re-triggers the sibling the instant the active channel completes, with
 * no software step in between -- there is no way to inspect a buffer's
 * contents and decide not to let it start. So the earliest a stop decision
 * can possibly take effect is at the NEXT completion after the sibling
 * buffer's contents are known to be pure silence, and the discovery that a
 * buffer is now pure silence happens exactly one refill before that buffer
 * is chain-triggered to play (by construction: it is being refilled
 * because the OTHER buffer just started playing it). So the WORST-CASE
 * trailing latency this fix adds is bounded by roughly one buffer-period
 * (I2S_AUDIO_BUFF_SIZE / sample_rate -- 128 ms at this driver's 8000 Hz),
 * and typically much less: since the hardware auto-triggers the silent
 * sibling immediately, `i2s_audio_process()`'s very next poll usually
 * catches it and aborts within microseconds of it starting, not after a
 * full buffer-period of silence. This is not "cheaply avoidable" beyond
 * that bound with software alone: doing better would need the hardware to
 * either not auto-chain into a buffer known in advance to be silent (the
 * `CHAIN_TO` mechanism has no such conditional) or report actual playback
 * position rather than transfer completion, neither of which this
 * peripheral offers. Trading a bounded, harmless silence tail for the
 * guarantee that no real audio is ever truncated is the right, and
 * essentially the only available, trade at this layer.
 *
 * ---- Trap 5: two magic numbers with the ORIGINAL AUTHORS' OWN admissions
 *      that they do not understand them -- neither is transcribed ----
 * `fillBuffer()` (rpI2S.cpp:760-888) contains two adjustments the original
 * authors themselves flag as not understood:
 *
 *   - `:838` `pOutBuffer[iCount] = pOutBuffer[iCount] >> 2; //not sure why
 *     this is happening -- some kind of volume adjustment?  --skellenger`.
 *     This line lives INSIDE `if (m_bFillBuffFromFile)` (:830) -- the
 *     file-playback branch. This port does not carry file-based playback at
 *     all (see SCOPE above: startSoundFromFile()/OLD and the filesystem
 *     pointer they need are app-layer, out of scope). So this magic number
 *     is not merely dropped, it belongs to a branch that literally does not
 *     exist in this port -- there is no `m_bFillBuffFromFile` equivalent
 *     state for it to attach to. Nothing to decide here beyond "this path is
 *     not ported," which SCOPE already covers.
 *
 *   - `:843` `pOutBuffer[iCount] = (short)((float)mp_SoundPointer[m_iCurrentSample]
 *     * 0.8); //this is some kind of volume adjustment, should not be doing
 *     it here --wjs`. Unlike the `>>2` above, this line is NOT gated by
 *     m_bFillBuffFromFile -- it runs for every MONO, non-8-bit,
 *     non-file, non-tone sound, i.e. it is live on the exact path this port
 *     DOES carry (i2s_audio_start() with force_mono=true, is_8bit=false,
 *     which is what every legacy ROM/number-sound caller used). This one
 *     required an actual decision, not just a scope observation.
 *
 *     DECISION: DROPPED, not transcribed. i2s_audio_select_sample() below
 *     reads the mono sample unscaled (`src[index]`), with no 0.8 factor
 *     anywhere. Reasoning, weighed against pdm_mic.h's own precedent for
 *     preserving a similarly "looks like a bug" volume asymmetry unchanged:
 *       1. The PDM case had no admission from its own author that the
 *          arithmetic was wrong -- only external evidence (two settings
 *          producing identical output) suggesting it MIGHT be unintentional.
 *          Here the author who wrote the line says outright it "should not
 *          be doing it here" -- that is not ambiguous evidence to weigh, it
 *          is the author's own verdict on their own code.
 *       2. This project already has a real, INTENTIONAL, explicit volume
 *          path -- VOLUME_MULTIPLIERS[volume] * asset_gain (:771, ported as
 *          i2s_audio_volume_multiplier() below) -- that every sample already
 *          passes through regardless of mono/stereo/8-bit. A second,
 *          uncoordinated *0.8 stacked on top of that, for mono sounds only,
 *          is not a deliberate design (no comment anywhere explains why mono
 *          specifically needs 80% level and stereo does not) -- it reads as
 *          a leftover experiment the author never removed.
 *       3. Unlike WS2812's side-set polarity (the hardware record) or PDM's
 *          sampled clock edge (trap 3 there), there is no schematic or
 *          datasheet fact this constant could be encoding. It is pure
 *          control-surface arithmetic with an author's own comment
 *          disowning it -- the opposite of the "prove it against hardware
 *          before calling it a defect" bar this port's brief sets, because
 *          the author already did the proving and reached "no."
 *     A future maintainer who finds mono sounds louder than before this port
 *     should know: they used to be built at 80% of whatever
 *     VOLUME_MULTIPLIERS produced, silently, and that silent extra
 *     attenuation is gone.
 *
 * ---- Trap 6: `rpI2S.cpp:123`'s integer-only clock divider -- preserved,
 *      unexplained, empirical ----
 * `update_pio_frequency()` (:117-125) computes
 * `divider = system_clock_frequency * 4 / sample_freq` and then calls
 * `pio_sm_set_clkdiv_int_frac(pio, iSM, divider >> 8u, 0)` -- passing 0 for
 * the FRACTIONAL divisor argument, discarding the low 8 bits of `divider`
 * entirely, with the comment `//TEST removed fractional divisor for jitter
 * issues`. This is an empirical workaround with NO STATED SYMPTOM: no bug
 * report, no waveform description, nothing to verify against a datasheet or
 * schematic -- just "someone tried the fractional divisor, it caused
 * jitter, and removed it." i2s_audio_pio_divider() below transcribes the
 * exact formula (`sys_clk_hz * 4u / sample_rate_hz`, integer division
 * throughout, matching the original's `uint32_t` arithmetic), and
 * i2s_audio_init()/the sample-rate path always calls
 * `pio_sm_set_clkdiv_int_frac(pio, sm, (uint16_t)(divider >> 8u), 0)` --
 * fractional argument hardcoded to 0, exactly as the comment insists. This is
 * NOT "fixed" to restore the fractional divisor: doing so would need
 * hardware to re-litigate a jitter symptom this port cannot observe or
 * reproduce, not a code review. If the jitter issue is ever revisited, it
 * needs a board and a scope, not an edit here.
 *
 * ---- Trap 7 (recon-dma.md's own numbering; not one of the two "not sure
 *      why" numbers above) -- the volume path that actually matters is
 *      VOLUME_MULTIPLIERS, and the `#if (0)` block is dropped, not ported ---
 * `fillBuffer()` (:850-869) is wrapped in `#if (0)`, with the author's own
 * comment `//skellenger: removing this for now since m_iVolumeAdjustment ==
 * 5 and this code is ignored anyway`. It never executes in the original and
 * additionally contains a buggy shift formula (the same shape pdm_mic.h's
 * trap 5 note describes for the PDM case, but here it is doubly moot: dead
 * AND buggy). DROPPED entirely -- see pdm_mic.h's own "CORRECTION (fix round
 * 1)" paragraph, which verified this directly against rpI2S.cpp rather than
 * taking an earlier draft's claim on faith: the REAL, LIVE volume path is
 * `VOLUME_MULTIPLIERS[m_iVolumeAdjustment] * m_fSingleAssetVolumeAdjustment`
 * (:771), with `m_iVolumeAdjustment` clamped to 0-10 immediately above
 * (:767-770). i2s_audio_volume_multiplier() below ports exactly that live
 * path -- the clamp and the multiply -- and nothing from the dead `#if (0)`
 * block.
 *
 * ---- The PIO program: origin floated, not fixed, and the offset is now
 *      captured instead of caller-supplied ----
 * `audio_i2s_program` (rpI2S.cpp:92-96) declares `.origin = 4` -- a FIXED
 * load offset, unlike ws2812_program/pdm_mic_program's floating `-1`.
 * `initI2S()`'s caller passes a separate `iFirstInstruction` parameter that
 * must equal that same fixed offset (4) for `audio_i2s_program_init()` and
 * the final `pio_sm_exec(..., pio_encode_jmp(iFirstInstruction))` to target
 * the program the fixed origin actually placed -- `pio_add_program()`'s own
 * return value (the ACTUAL load offset) is silently discarded at :247. This
 * only works because the legacy monolith's single shared PIO layout put
 * WS2812 first and I2S was hand-placed to land at a known offset after it --
 * a static, whole-firmware layout decision, not a fact about this program.
 *
 * This BSP's PIO block is shared dynamically by THREE drivers (WS2812, PDM,
 * I2S -- recon-dma.md), each choosing its own PIO block/SM via
 * `ws2812_init(pio, sm)` / `pdm_mic_init(pio, sm)`'s own caller-chosen
 * pattern, with `pio_add_program()`'s return value captured and used, never
 * assumed. i2s_audio_init() below follows exactly that pattern:
 * `i2s_audio_program`'s `.origin` is `-1` (floating), and the offset
 * `pio_add_program()` actually returns is what every later step (the wrap
 * points, the entry-point jump, the final restart-and-jump) uses. This
 * removes the fragile fixed-origin coupling entirely rather than
 * reproducing it -- there is no `iFirstInstruction` parameter on
 * i2s_audio_init() for the same reason there is none on ws2812_init() or
 * pdm_mic_init(): the offset is a fact the SDK hands back, not a number a
 * caller needs to already know.
 *
 * The bring-up SEQUENCE and REGISTER writes are otherwise transcribed
 * unchanged and in the same order: park DIN/LRCLK/BCLK as GPIO outputs low
 * (:179-187), configure the SM (wrap, 2-bit side-set at LRCLK/BCLK, OUT on
 * DIN, autopull threshold 32, MSB-first -- audio_i2s_program_init(),
 * :105-115), an internal `pio_sm_exec(jmp offset+7)` that the helper performs
 * BEFORE the outer function's own `pio_sm_restart()` +
 * `pio_sm_exec(jmp offset)` + `pio_sm_set_enabled(true)` sequence
 * (:251-253) -- both jumps preserved, in the same order, because AGENTS.md's
 * porting rule is to keep register sequences and timing exactly as the
 * original had them even where the net PC movement looks redundant on paper.
 *
 * One redundant original step IS dropped: `rpDMA::setupPeripheralRequest()`
 * (called from initI2S() at :202-217/:229-244, setting the DMA channel's
 * TREQ bits directly against a hardcoded PIO-index/SM switch) has its entire
 * effect OVERWRITTEN before it ever matters -- `setupDMAChain()` calls
 * `channel_config_set_dreq(&a_cfg/&b_cfg, pio_get_dreq(pio, sm, true))`
 * (:904, :918) on the SAME channels every time a sound starts, and that is
 * the only DREQ assignment in effect by the time either channel is actually
 * triggered (dma_channel_configure() writes a fresh, complete
 * dma_channel_config every call). The init-time DREQ setup is dead by
 * construction, not merely early -- i2s_audio_init() does not reproduce it,
 * and DREQ is set exactly once, at the point it actually takes effect,
 * inside the DMA-chain setup that runs per `i2s_audio_start()` call.
 * `pio_gpio_init()` replaces the original's hand-branched
 * `gpio_set_function(pin, GPIO_FUNC_PIO0)` / `GPIO_FUNC_PIO1` switch for the
 * same reason ws2812_driver.c and pdm_mic.c already made that substitution:
 * it derives the function select from the PIO instance itself, so it is
 * correct for whichever of pio0/pio1 the caller passes without a manual
 * branch.
 *
 * ---- Buffer-fill deviations from setupDMAChain()'s exact shape (both
 *      documented, both deliberate, both about not leaving stale audio in a
 *      buffer that is about to be chain-triggered) ----
 *
 * 1. ALWAYS fill both ping-pong buffers before starting playback.
 *    `startSound()` (:713-753) only calls `fillBuffer(false)` (buffer B)
 *    CONDITIONALLY -- `if (fillBuffer(true)) fillBuffer(false);` (:745-748)
 *    -- i.e. only if filling buffer A indicated more samples remain. For a
 *    sound short enough to fit entirely in one buffer (<=
 *    I2S_AUDIO_BUFF_SIZE samples), buffer B is NEVER filled for this
 *    playback and keeps whatever a PREVIOUS, unrelated sound left in the
 *    same static array. But `setupDMAChain()` chains BOTH channels
 *    regardless of how much real audio exists (:905/:919) and starts
 *    channel A immediately, and channel A's completion auto-triggers channel
 *    B in hardware (trap 4, above) -- which for a short sound happens on the
 *    very first process() poll, calling finishAndReset() to abort both
 *    channels. Between B's hardware auto-trigger and software's abort
 *    catching up to it, channel B is actively shifting STALE, UNRELATED
 *    audio from a previous sound into the PIO FIFO -- a real, provable (by
 *    reading setupDMAChain()/process()/fillBuffer() together, not merely
 *    suspected) audible-garbage-burst hazard for every short sound, with no
 *    schematic or datasheet fact bearing on it at all. i2s_audio_start()
 *    below fills BOTH buffers unconditionally before calling the DMA-chain
 *    setup, at the cost of one extra, cheap fill call for the common case --
 *    for a short sound, buffer B's tail is genuinely silent (zero-padded,
 *    see the next point) rather than stale, so the same hardware
 *    auto-trigger-before-abort race now plays silence instead of garbage
 *    during its brief window.
 *
 * 2. The 8-bit fill path zero-pads its buffer tail; the original's does not.
 *    fillBuffer()'s general (mono/stereo, 16-bit source) branch loops
 *    `iCount` from 0 to `I2S_AUDIO_BUFF_SIZE` (the full buffer CAPACITY) and
 *    explicitly zeroes anything past the real sample count (:826,
 *    `pOutBuffer[iCount] = 0;` at :873). Its `playing8BitAudio` branch
 *    (:808-822) loops only to `iCurrentBuffSize` (the real sample count) and
 *    never touches the rest of the buffer at all -- a provable asymmetry
 *    between the two branches of the SAME function, not something this port
 *    invented. Combined with point 1 above, an 8-bit sound shorter than the
 *    buffer would otherwise leave buffer B's untouched tail as stale audio
 *    from whatever played before it, exactly the hazard point 1 exists to
 *    close -- so closing point 1 without also closing this one would still
 *    leave an 8-bit-specific gap. i2s_audio_fill_buffer_8bit() below zero-
 *    pads out[count..capacity) exactly like the 16-bit path, unifying the
 *    two branches' tail behaviour. This is a genuine, provable defect fixed
 *    outright (not merely documented and transcribed) because closing it is
 *    what makes point 1's fix actually complete for 8-bit sounds too --
 *    they reinforce each other and are only correct taken together.
 *
 * ---- Volume clamp and the stray `assert()`s ----
 * `fillBuffer()` clamps `m_iVolumeAdjustment` to 0-10 as a MUTATING side
 * effect on the class member itself (:767-770), every call. This port's
 * i2s_audio_volume_multiplier() clamps its own LOCAL copy of the parameter
 * instead -- a pure function with no persistent state to mutate is the
 * point of exposing it for host testing at all. `update_pio_frequency()`'s
 * two `assert()`s (:120,122) are debug-only range sanity checks (`system_
 * clock_frequency < 0x40000000`, `divider < 0x1000000`) with no behaviour
 * attached in a release build; both hold for every clock/rate this board
 * uses (200 MHz sys clock, 8000 Hz sample rate) with wide margin, so they
 * are not reproduced -- there is nothing for them to guard against on this
 * board's fixed clock tree that a host test cannot already confirm by
 * inspection of the numbers involved.
 */
#ifndef FWOG_I2S_AUDIO_H
#define FWOG_I2S_AUDIO_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* One ping-pong buffer's capacity, in samples. Transcribed from
 * rpI2S.h:15's I2S_AUDIO_BUFF_SIZE (1024) -- the header comment there
 * ("this is a buffer of int16_t so 256 * 2 = 512 bytes") is stale relative
 * to the 1024 the macro actually expands to (2048 bytes); the NUMBER used
 * here is the live 1024, not the comment's arithmetic. */
#define I2S_AUDIO_BUFF_SIZE 1024u

/* Volume levels 0-10 inclusive -- rpI2S.cpp:767-770's clamp range. */
#define I2S_AUDIO_VOLUME_LEVELS 11u

/* ---- Pure: host-tested, no PIO/DMA ---- */

/* The per-level volume table, transcribed unchanged from rpI2S.cpp:51-63
 * (`VOLUME_MULTIPLIERS[11]`). Exposed (not `static`) so tests/test_i2s.c can
 * pin the exact values -- the same reason ws2812_program_instructions[] and
 * pdm_mic_program_instructions[] are exposed, applied to a data table instead
 * of a PIO program. */
extern const float i2s_audio_volume_multipliers[11];

/* PIO clock divider for the audio_i2s program at a given sample rate, given
 * the live system clock in Hz. Transcribed from rpI2S.cpp:117-125's
 * `update_pio_frequency()` -- `system_clock_frequency * 4 / sample_freq`,
 * INTEGER division throughout, matching the original's `uint32_t`
 * arithmetic exactly (see the header's trap 6 note for why the low 8 bits
 * of this value, not the fractional divisor, are what
 * pio_sm_set_clkdiv_int_frac() actually receives -- that shift happens at
 * the call site, not in this function, so this function's return value is
 * directly comparable to the original's own `divider` variable before its
 * `>> 8u`). Never hardcode this: this board's clk_sys is 200 MHz, not the
 * Pico SDK's 125 MHz default -- see the test for both, and for the 8000 Hz
 * and 22050 Hz rates that appear literally in the original (8000 Hz is this
 * driver's only live rate; 22050 Hz was rpI2S::speak()'s TTS rate, which is
 * out of scope here, but the arithmetic itself does not care which rate is
 * asked for). */
uint32_t i2s_audio_pio_divider(uint32_t sys_clk_hz, uint32_t sample_rate_hz);

/* The eight raw PIO instruction words, transcribed unchanged from
 * rpI2S.cpp:78-89's `audio_i2s_program_instructions[]` -- delay values (all
 * zero; timing here comes from the `set x,14`/`jmp x--` loops, not
 * per-instruction delay slots), wrap points, AND side-set polarity, all
 * exactly as encoded there. This is the LIVE variant: rpI2S.cpp:65-76 also
 * defines `audio_i2s_program_instructionsOppsite[]`, an alternate 8-word
 * program that is never referenced by `audio_i2s_program` or anything else
 * in the file -- dead code, not ported, not pinned here. Exposed here (not
 * behind HOST_TEST -- plain data, no SDK dependency) so tests/test_i2s.c can
 * pin the exact words, the same pattern ws2812_program_instructions[] and
 * pdm_mic_program_instructions[] use. Defined in i2s_audio.c. */
extern const uint16_t i2s_audio_program_instructions[8];

/* Which ping-pong buffer is the OTHER one, given the buffer that just
 * finished (or is about to be filled). Pure flip: transcribed from the
 * two symmetric branches of `rpI2S::process()` (:932-979), which always
 * refill the buffer that just completed while the OTHER one is (or is about
 * to be) actively chain-triggered by hardware -- see the header's trap 4
 * note for why both channels matter every time this flips. */
typedef enum { I2S_AUDIO_BUFFER_A = 0, I2S_AUDIO_BUFFER_B = 1 } i2s_audio_buffer_t;
i2s_audio_buffer_t i2s_audio_other_buffer(i2s_audio_buffer_t current);

/* How many samples the buffer this call is about to fill, sized to `capacity`,
 * should now contain: transcribed from fillBuffer()'s
 * `iCurrentBuffSize = min(I2S_AUDIO_BUFF_SIZE, iNumSamplesRemaining)`
 * (rpI2S.cpp:764,773-776). Returns 0 (not negative) once `current_sample >=
 * total_samples`, matching `iNumSamplesRemaining <= 0` falling through to
 * `iCurrentBuffSize = iNumSamplesRemaining` in the original, which a caller
 * there never actually observes going negative because playback always
 * stops (finishAndReset()) at exactly `current_sample == total_samples`. */
unsigned i2s_audio_fill_count(int total_samples, int current_sample, unsigned capacity);

/* Whether the FILL cursor has more samples to deliver: transcribed from
 * `m_iTotalSamples == m_iCurrentSample` (rpI2S.cpp:944,964), inverted to a
 * "remain" sense for readability at call sites. NOT used by
 * i2s_audio_process() as its stop gate any more -- see the header's
 * fix-round-1 note on trap 4 for why the fill cursor runs up to one whole
 * buffer AHEAD of what has actually played, and i2s_audio_chain_drained()
 * below for the criterion that replaced it. Retained as a small, correct,
 * still-tested predicate in its own right (it answers "has every input
 * sample been handed to a fill call yet", which is a real and separately
 * useful question -- just not the one that decides when to stop the DMA
 * chain). */
bool i2s_audio_samples_remain(int total_samples, int current_sample);

/* Whether the hardware chain has fully drained real audio and it is now
 * safe to stop: true once the SIBLING buffer -- the one the chain has
 * already auto-triggered, or is about to, the instant the currently-tracked
 * buffer's transfer completes -- produced ZERO real samples on its own most
 * recent fill (`sibling_consumed == 0`). This is the CORRECTED stop
 * criterion for `i2s_audio_process()`, replacing a fill-cursor check
 * (`i2s_audio_samples_remain()`) that stopped the chain up to one whole
 * buffer before the sibling had actually been given a chance to play --
 * see the header's fix-round-1 note on trap 4 for the full derivation,
 * including why this is the tightest bound achievable given the hardware's
 * unconditional `chain_to` auto-retrigger. `sibling_consumed` is that
 * buffer's own `i2s_audio_fill_buffer()`/`i2s_audio_fill_buffer_8bit()`
 * return value from its most recent fill (one refill cycle ago, not the
 * one just performed for the buffer that completed THIS call). */
bool i2s_audio_chain_drained(unsigned sibling_consumed);

/* Expand one UNSIGNED 8-bit PCM sample to signed 16-bit, transcribed from
 * rpI2S.cpp:811 (`(((short)mp_btSoundBuffer[i]) - 128) * 50`). `mp_btSoundBuffer`
 * is `const unsigned char *` in the original, so the source byte's natural
 * range is 0-255 -- taking it as `uint8_t` here pins that down explicitly:
 * taking it as `int8_t` instead would silently reinterpret every byte >=
 * 0x80 as negative before the `-128` even runs, producing a completely
 * different (and wrong) result for the upper half of the input range. See
 * the test for this exact sign-handling case. */
int16_t i2s_audio_expand_8bit(uint8_t sample);

/* Select one 16-bit sample for playback from a caller-supplied source at
 * logical sample index `index`, down-mixing exactly as rpI2S.cpp:842-845
 * does: `mono` reads `src[index]` directly; NOT-mono (stereo) reads
 * `src[index * 2]` -- the LEFT channel of an interleaved LR pair. This is
 * NOT an average of L+R -- the right-channel sample at `src[index*2+1]` is
 * silently discarded, exactly as the original does. The original's mono
 * branch also multiplied by 0.8 here (rpI2S.cpp:843) -- DROPPED in this
 * port; see the header's trap 5 note for the argument. */
int16_t i2s_audio_select_sample(const int16_t *src, int index, bool mono);

/* VOLUME_MULTIPLIERS[volume_adjustment] * asset_gain, with volume_adjustment
 * clamped to 0-10 (rpI2S.cpp:767-771). `asset_gain` is
 * m_fSingleAssetVolumeAdjustment's role -- a per-sound multiplier an app
 * layer may apply on top of the user's volume setting (defaults to 1.0; see
 * i2s_audio_set_asset_gain() below and the header's note on
 * ASSETS_NEW_FLOAT_ATENUATION being out of this driver's scope). */
float i2s_audio_volume_multiplier(int volume_adjustment, float asset_gain);

/* Apply a volume multiplier to one sample: `(int16_t)((float)sample *
 * multiplier)`, matching rpI2S.cpp:849's own multiply-then-narrow -- TRUNCATING
 * (toward zero), NOT rounding, and NOT saturated/clamped. The original never
 * saturates this multiply either (VOLUME_MULTIPLIERS tops out at 1.00 and is
 * clamped 0-10, so in practice the result never exceeds the input's own
 * magnitude) -- that absence of saturation is inherited behaviour, not
 * introduced here. */
int16_t i2s_audio_apply_volume(int16_t sample, float multiplier);

/* One buffer's worth of fillBuffer()'s core loop (rpI2S.cpp:826-875, the
 * "every other case" branch), for a 16-BIT source buffer, MINUS the
 * m_bFillBuffFromFile branch (filesystem, out of scope -- and see the
 * header's trap 5 note: it also carries the `>>2` magic number that is
 * therefore moot here) and the dead `#if (0)` volume block (trap 7).
 * Writes `out[0..count)` from `src` starting at logical index `start_index`
 * (down-mixed per `mono`, volume-scaled per `volume_adjustment`/
 * `asset_gain`), and zero-pads `out[count..capacity)` exactly like the
 * original's `else { pOutBuffer[iCount] = 0; }` tail (:873). Returns
 * `count` (the number of INPUT samples consumed), so a caller can advance
 * its own sample cursor by the return value, matching
 * `m_iCurrentSample`'s per-call increment. */
unsigned i2s_audio_fill_buffer(int16_t *out, unsigned capacity,
                               const int16_t *src, int start_index,
                               unsigned count, bool mono,
                               int volume_adjustment, float asset_gain);

/* The 8-bit-source variant (rpI2S.cpp:808-822's `playing8BitAudio` branch):
 * `src` is UNSIGNED 8-bit PCM, expanded per-sample with
 * i2s_audio_expand_8bit() before the same volume scaling. UNLIKE
 * i2s_audio_fill_buffer() above, the original's 8-bit branch does NOT
 * zero-pad `out[count..capacity)` -- see the header's "buffer-fill
 * deviations" note for why this port DOES zero-pad it here anyway, as a
 * deliberate fix (not a silent transcription) that closes a real stale-
 * buffer hazard the original's asymmetry leaves open. */
unsigned i2s_audio_fill_buffer_8bit(int16_t *out, unsigned capacity,
                                    const uint8_t *src, int start_index,
                                    unsigned count, int volume_adjustment,
                                    float asset_gain);

#ifndef HOST_TEST
#include "hardware/pio.h"

/* Claim TWO DMA channels (non-panicking, with rollback -- trap 1) and a
 * state machine on the given PIO block, load the audio_i2s program (offset
 * captured, not caller-supplied -- see the header's PIO note), configure the
 * PIO pins/SM exactly as initI2S() does, and start the state machine
 * clocking at 8000 Hz. Does NOT start any DMA transfer or claim the PIO
 * block's instruction memory a second time on a repeat call -- like
 * ws2812_init()/pdm_mic_init(), a second call while already ready is a
 * no-op that returns true and touches no hardware state, for the same
 * leaked-claim-plus-orphaned-hardware reason pdm_mic.h's trap 8 documents.
 *
 * `pio`/`sm` are caller-chosen, never hardcoded -- see the header's PIO
 * note. Uses PIN_I2S_DIN/PIN_I2S_LRCLK/PIN_I2S_BCLK from board.h; this board
 * has exactly one I2S speaker on one triplet of pins, so unlike the PIO
 * block/SM that is a board fact, not a caller choice.
 *
 * Returns false, with the driver left not-ready, if either DMA channel claim
 * fails or the PIO program does not fit -- see the header's trap 1 note for
 * exactly what is unwound at each of the three failure points. */
bool i2s_audio_init(PIO pio, uint sm);

/* Queue a buffer of caller-supplied samples and start playback immediately.
 * Matches `rpI2S::startSound()`'s own parameter shape (`pSound`,
 * `iSizeInWords`, `bForceMono`, `bSoundIs8Bit`) -- the one entry point in the
 * original that already takes its audio from the caller rather than a
 * filesystem or asset table (see SCOPE above).
 *
 * `samples` is `count` samples: `int16_t*` if `is_8bit` is false (mono:
 * `count` values; stereo: `count` INTERLEAVED L/R pairs, i.e. `2*count`
 * `int16_t`s, matching mp_SoundPointer's own indexing at rpI2S.cpp:845), or
 * `uint8_t*` (unsigned 8-bit PCM, `count` bytes) if `is_8bit` is true.
 * `force_mono` selects i2s_audio_select_sample()'s down-mix behaviour (moot
 * when `is_8bit` is true -- the 8-bit path never consults it, matching the
 * original: rpI2S.cpp:808-822's `playing8BitAudio` branch runs unconditionally
 * of `m_bFileIsMono`).
 *
 * THE SAMPLE RATE IS 8000 Hz, SO NYQUIST IS 4 kHz. Anything approaching that
 * aliases audibly. A tone or note table carried over from a higher-rate part
 * needs an OG-specific ceiling -- FreeWili 2 tables reaching 3000 Hz alias
 * here, and the port that found this clamps to 2.5 kHz. Put the clamp in
 * OG-specific code, not in a shared table.
 *
 * Returns false, touching no hardware state, if the driver is not ready
 * (i2s_audio_init() has not succeeded), if playback is already in progress
 * (matching `if (m_iState != idle) return false;`, rpI2S.cpp:716-717,741-742),
 * or if `samples` is NULL or `count` is 0.
 *
 * Fills BOTH ping-pong buffers before starting the DMA chain -- see the
 * header's "buffer-fill deviations" note for why this differs from
 * `setupDMAChain()`'s own conditional pre-fill, and what hazard doing so
 * closes.
 *
 * `samples` MUST OUTLIVE THE NOTE. This call does not copy it; the refill
 * path reads from it for as long as playback runs, so a stack array is a
 * use-after-scope that will sometimes sound fine. Use a static or otherwise
 * long-lived buffer sized to the longest sound the app plays.
 *
 * Unparks the state machine (trap 2) before arming the chain, so a caller
 * never has to pair this with i2s_audio_park() itself. */
bool i2s_audio_start(const void *samples, unsigned count, bool force_mono,
                     bool is_8bit);

/* Poll/advance: call every iteration of the app's main loop. Matches
 * `rpI2S::process()` (:932-979) -- a no-op if idle or if the currently
 * active channel has not yet completed. On completion, checks
 * i2s_audio_chain_drained() against the SIBLING buffer (fix round 1 --
 * see the header's trap 4 correction for why this, not a fill-cursor
 * check, is the right test): if the chain has drained, aborts both
 * channels and returns to idle (the equivalent of `finishAndReset()` --
 * see the header's trap 4 note for why aborting BOTH is correct here, not
 * merely harmless, and trap 2 for why a best-effort zero sample is pushed
 * onto the FIFO afterward, not a real mute). Otherwise, refills the buffer
 * that just finished (resetting that DMA channel's read address back to
 * the buffer's start first -- the original's
 * `dma_channel_set_read_addr(..., false)` at :951/:971, without which the
 * channel's read pointer would still point past the end of the buffer from
 * the transfer that just completed, corrupting its NEXT hardware-chained
 * re-trigger) and flips the active-buffer bookkeeping to the sibling. */
void i2s_audio_process(void);

/* Stop playback immediately: abort both DMA channels, push a best-effort
 * zero sample, PARK the state machine (trap 2), and return to idle. The
 * parking is what actually makes this quiet. The original has no standalone
 * public equivalent -- `finishAndReset()` is `private`, only ever reached
 * from inside `process()` at natural end-of-playback -- but the clean-API
 * shape this port targets ("configure, queue, start, poll/advance, stop")
 * calls for an explicit stop an app can invoke early (e.g. a mute/volume
 * control silencing an in-flight sound). Safe to call when already idle
 * (a no-op) or mid-playback (aborts whichever channel is active AND the one
 * the hardware chain may have already auto-triggered -- same reasoning as
 * the natural-completion path). */
void i2s_audio_stop(void);

/* Park the output in TRUE silence: stop the state machine, so BCLK and LRCLK
 * stop and the amplifier -- which has no shutdown line, trap 2 -- has nothing
 * to amplify. Idempotent, and safe to call when already idle.
 *
 * You rarely need this. Every path that ends playback parks already, and
 * i2s_audio_start() unparks, so the driver is silent between notes without a
 * caller doing anything. Call it explicitly only to cut an in-flight note
 * (it stops playback first).
 *
 * The resume is frame-aligned -- see trap 2 for why an application cannot
 * implement this itself, and why an attempt appears to work at first. */
void i2s_audio_park(void);

/* Whether the state machine is currently parked, i.e. whether the bus is
 * genuinely silent. Exposed for diagnostics and tests; a normal caller does
 * not need it, because parking is automatic. */
bool i2s_audio_is_parked(void);

/* Whether playback is idle -- `rpI2S::isIdle()` (rpI2S.h:102).
 *
 * THIS DESCRIBES THE DRIVER, NOT THE BUS. It is exactly `!s_playing`: it says
 * no note is in progress, and says nothing about whether the hardware is
 * making a sound. Before parking existed, "idle" and "audibly noisy" were
 * routinely true at the same time, and that wording trap is what made a first
 * fix attempt look reasonable. Use i2s_audio_is_parked() to ask about the
 * bus. */
bool i2s_audio_is_idle(void);

/* Set the user-facing volume level, clamped to 0-10 -- the
 * `m_iVolumeAdjustment` role (default 10, matching rpI2S.h:68's own
 * default). Takes effect on the NEXT buffer fill (the currently in-flight
 * DMA transfer is not retroactively rescaled), matching the original: the
 * clamp-and-multiply happens once per fillBuffer() call, not continuously. */
void i2s_audio_set_volume(int volume_adjustment);

/* Set the per-sound asset gain multiplier -- the
 * `m_fSingleAssetVolumeAdjustment` role (default 1.0, matching rpI2S.h:69).
 * An app-layer asset table is the intended caller (see the header's note on
 * ASSETS_NEW_FLOAT_ATENUATION being out of this driver's scope); this driver
 * does not set this to anything but 1.0 on its own except when playback ends
 * (matching finishAndReset()'s own `m_fSingleAssetVolumeAdjustment = 1.0;`,
 * rpI2S.cpp:985 -- i2s_audio_process()'s internal stop path does the same). */
void i2s_audio_set_asset_gain(float gain);

#endif
#endif
