/* Boot and ship-mode splash for display apps.
 *
 * Full-panel 320x240 NES-box layout: black border, framed illustration,
 * title band, corner seal. Layout homage to NES box art as popularized by
 * James Rolfe / Angry Video Game Nerd; the illustration is original (hacker /
 * DEF CON / Phrack zine theming). No Nintendo marks, no licensed characters.
 * It is a still
 * frame — bind a name once, blit once, overlay the title, dwell three
 * seconds, then clear so the app is not drawn on top of the art.
 *
 * Call fwog_splash_boot() after the panel is up. It dwells three seconds,
 * then clears the panel so the app UI is not painted over the box art.
 * The corner badge is the DEF CON smiley-and-crossbones mark. Boot
 * overlays "DEF CON" under the version; ship overlays "sign off" under
 * GOODBYE.
 *
 * Boot audio is optional and off by default. i2s_audio can play 8 kHz PCM
 * from flash. tools/gen_splash_art.py --wav emits splash_boot_pcm[];
 * fwog_splash_boot() plays it only when SPLASH_BOOT_PCM_SAMPLES > 0.
 *
 * --ship-wav emits splash_ship_pcm[]. fwog_splash_ship() (from
 * fwog_power_poll, 6 s red hold) plays it before the pack FET opens.
 * Do not start I2S from either path when the matching sample count is 0.
 *
 * No-ops if the panel is not ready or nothing was bound. ogvegas keeps its
 * own startup image and only binds so shutoff still says goodbye.
 */
#ifndef FWOG_SPLASH_H
#define FWOG_SPLASH_H

void fwog_splash_bind(const char *name, const char *version);

void fwog_splash_boot(void);
void fwog_splash_ship(void);

#endif
