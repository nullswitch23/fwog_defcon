/* The iCE40 configuration bitstream this BSP loads at boot.
 *
 * Source: freewilimain/fpga_bit_default_v5.h in the **FreeWili 1-only**
 * firmware repository at tag `spartahackFw1final` -- the last FW1 release.
 * That file is what its FreeWilliMain.cpp:25 includes and :1957 programs at
 * boot, so this is the gateware a shipped OG actually runs. Converted to a raw
 * binary by tools/fpga_header_to_bin.py.
 *
 *   size    104090 bytes
 *   sha256  8a5c1ecad97bca9862dd7ccc0130e72454dc6b1bb0231ca3b16e25ca1e239a91
 *
 * ---- THIS REPLACED A DIFFERENT BITSTREAM. Do not "restore" the old one. ----
 *
 * Until 2026-07-28 this file carried a bitstream taken from the OTHER
 * reference tree -- the MULTI-TARGET FreeWili firmware repository -- at
 * freewilimain/fpgabitstreams/fpga_bit_default_v5.h:
 *
 *   sha256  113da0ba5e8aa4b212f1de43e8900c771dc5340473f5eb6a87dd3b8318cc5589
 *
 * Same 104090 bytes, same symbol name, same size constant, identical iCE40
 * sync word and header -- and **41.4% of all bytes different**, spread evenly
 * through the entire configuration payload. Not a timestamp or a metadata
 * field: two genuinely different designs.
 *
 * The likely cause, and the reason the old header's provenance argument looked
 * airtight while being wrong: that tree is MULTI-TARGET. It builds
 * `targets/fw2main` and `targets/fwclassic` from ONE shared
 * `fpgabitstreams/` directory. A bitstream updated there for FreeWili 2 is
 * silently inherited by the OG target, and the include-and-program trace
 * (`FreeWilliMain.cpp` includes it, programs it) still reads as correct at
 * every step. The FW1-only repository cannot have that failure mode, which is
 * why it is the authority for anything OG.
 *
 * The old bitstream DID configure the part on hardware -- it reported
 * `cdone=1`. That is the trap worth remembering: CDONE is structural
 * acceptance of a well-formed bitstream, so it comes up for the wrong
 * gateware exactly as readily as for the right gateware. It can never
 * distinguish them. Only provenance can.
 *
 * There is deliberately NO public loader for a different bitstream. Unknown
 * gateware could route a breakout pin in a direction the PCAL6416's level
 * shifters disagree with, which is an output driving an output. Keeping the
 * loader internal makes that unreachable rather than merely discouraged.
 * Future OG revisions drop the FPGA, so this costs nothing long term. */
#ifndef FWOG_FPGA_BITSTREAM_H
#define FWOG_FPGA_BITSTREAM_H
#include <stddef.h>
#include <stdint.h>

#define FWOG_FPGA_BITSTREAM_SIZE 104090u

extern const uint8_t fwog_fpga_bitstream[];
extern const uint8_t fwog_fpga_bitstream_end[];

#endif
