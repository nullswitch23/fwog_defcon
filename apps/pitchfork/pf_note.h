#ifndef PF_NOTE_H
#define PF_NOTE_H

/* Map a frequency in Hz to a note name ("A4") and signed cents vs equal
 * temperament (A4=440). Returns 0 if hz is unusable.
 *
 * Usable band is guitar-ish through the PDM Nyquist: the MP34DT06J path is
 * 8 kHz PCM, so 4 kHz is the hard ceiling, and CIC droop plus the FFT min
 * bin (~94 Hz) eat the bottom. */
#define PF_HZ_MIN 80u
#define PF_HZ_MAX 3500u

int pf_from_hz(unsigned hz, char *name, unsigned name_n, int *cents);
int pf_from_hz_f(float hz, char *name, unsigned name_n, int *cents);
int pf_midi_from_hz(float hz); /* 0 if unusable */
void pf_name_midi(int midi, char *name, unsigned name_n);
int pf_cents_vs_midi(float hz, int midi);
/* Stick on a MIDI note until three consecutive frames agree. */
int pf_hold_midi(int *cand, unsigned *n, int midi, int shown);

/* Parabolic peak offset in bins from three consecutive magnitudes.
 * Clamped to ±0.5. Zero if the denominator is degenerate. */
float pf_parabolic_delta(float a, float b, float c);

/* Harmonic-product spectrum: pick the bin whose mag2, 2nd, and 3rd
 * harmonics multiply highest. `m2` is mag^2 per bin, length `n2` (n/2 of
 * the FFT). Ignores bins below `min_bin`. Returns 0 if the buffers are
 * unusable. Prefer this over the raw spectral peak so a guitar harmonic
 * does not name the wrong octave. */
unsigned pf_hps_bin(const float *m2, unsigned n2, unsigned min_bin);

#endif
