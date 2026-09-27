# Pin map — FreeWili OG

Cross-checked against `bsp/display_cpu/platform/board.h` and
`bsp/main_cpu/platform/board.h`, which are authoritative. Every value here is
asserted by `tests/test_pinmap_display.c` and `tests/test_pinmap_main.c`, so
this document and the code cannot drift silently.

### Display CPU (RP2040)

| Function | GPIO | Notes |
|---|---|---|
| LCD SCK / MOSI | 10 / 11 | SPI1; 62.5 MHz panel ceiling, 50 MHz achieved at 200 MHz sys clock |
| LCD CS | 13 | legacy name `DISPLAY_LCD_CS_ACTIVE_LOW` |
| LCD DC | 12 | legacy name `DISPLAY_CS` — the legacy name lies; this is DC, not CS |
| LCD backlight | 25 | PWM |
| LCD tearing (GPTE) | 21 | present, unused by current firmware |
| Button gray | 14 | active-low, internal pull-up (`BUTTON1`) |
| Button yellow | 15 | active-low, internal pull-up (`BUTTON2`) |
| Button green | 22 | active-low, internal pull-up (`BUTTON3`) |
| Button blue | 23 | active-low, internal pull-up (`BUTTON4`) |
| Button red | 24 | active-low, internal pull-up (`BUTTON5`) |
| WS2812 data | 7 | legacy name `LED_SERIAL`; **7 pixels**; through IC6 (inverter) to LED1.DIN, so the PIO side-set polarity is deliberately inverted |
| IR TX / RX | 9 / 16 | |
| PDM mic CLK / DATA | 17 / 29 | |
| I2S speaker DIN / LRCLK / BCLK | 4 / 5 / 6 | |
| I2C1 SDA / SCL | 26 / 27 | LIS3DH accel @ 0x19, MCP7940 RTC @ 0x6F, BQ25896 charger @ 0x6B — the whole bus. There is no fuel gauge: the BQ27441 footprint is DNP on this board |
| UART0 TX / RX / CTS / RTS | 0 / 1 / 2 / 3 | link to main CPU; identical roles to main, traces cross |
| `MAIN_BOOT_OE` | 8 | OE of a tri-state buffer (U3) gating the red button onto main's `QSPI_SS`/BOOTSEL strap; the LOW state `board_init_pins()` establishes locks main OUT of BOOTSEL |

### Main CPU (RP2040)

| Function | GPIO | Notes |
|---|---|---|
| CC1101 SCLK / MISO / MOSI | 6 / 4 / 7 | two radios share the bus |
| CC1101 CS0 / CS1 | 5 / 18 | both parked HIGH before any bus traffic |
| CC1101 GDO0 ×2 | 20, 21 | |
| CC1101 GDO2 ×2 | 22, 19 | |
| iCE40 CLK / DONE / RESET | 23 / 24 / 29 | |
| LED | 25 | plain GPIO |
| `GUI_NRESET` | 28 | resets the display CPU |
| UART0 TX / RX / CTS / RTS | 0 / 1 / 2 / 3 | link to display CPU; identical roles to display, traces cross |
| Breakout SPI1 CS / SCLK / MISO / MOSI | 13 / 14 / 12 / 15 | user I/O |
| Breakout UART1 TX / RX / CTS / RTS | 8 / 9 / 10 / 11 | user I/O |
| Breakout I2C0 SDA / SCL | 16 / 17 | user I/O |
| Breakout GPIO in / out | 26 / 27 | user I/O |

Both CPUs: 16 MB QSPI flash, 264 KB SRAM, 125 MHz stock. This BSP runs them at
200 MHz — see the clock invariant in [AGENTS.md](../../AGENTS.md), which also
covers why `clk_peri` must be pinned to `clk_sys`.
