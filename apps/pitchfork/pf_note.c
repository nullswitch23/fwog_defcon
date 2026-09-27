#include "pf_note.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const k_name[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

float pf_parabolic_delta(float a, float b, float c) {
    const float den = a - 2.0f * b + c;
    float p;
    if (den > -1.0e-12f && den < 1.0e-12f) return 0.0f;
    p = 0.5f * (a - c) / den;
    if (p > 0.5f) p = 0.5f;
    if (p < -0.5f) p = -0.5f;
    return p;
}

int pf_from_hz_f(float hz, char *name, unsigned name_n, int *cents) {
    if (!name || name_n < 4u || !cents ||
        !(hz >= (float)PF_HZ_MIN && hz <= (float)PF_HZ_MAX)) {
        if (name && name_n) name[0] = '\0';
        if (cents) *cents = 0;
        return 0;
    }
    const double n = 12.0 * log((double)hz / 440.0) / log(2.0) + 69.0;
    int midi = (int)(n + (n < 0 ? -0.5 : 0.5));
    if (midi < 21) midi = 21;
    if (midi > 108) midi = 108;
    const double target = 440.0 * pow(2.0, (midi - 69) / 12.0);
    *cents = (int)(1200.0 * log((double)hz / target) / log(2.0) +
                   ((double)hz >= target ? 0.5 : -0.5));
    const int pc = midi % 12;
    const int oct = midi / 12 - 1;
    snprintf(name, name_n, "%s%d", k_name[pc < 0 ? pc + 12 : pc], oct);
    return 1;
}

int pf_from_hz(unsigned hz, char *name, unsigned name_n, int *cents) {
    return pf_from_hz_f((float)hz, name, name_n, cents);
}

int pf_midi_from_hz(float hz) {
    double n;
    int midi;
    if (!(hz >= (float)PF_HZ_MIN && hz <= (float)PF_HZ_MAX)) return 0;
    n = 12.0 * log((double)hz / 440.0) / log(2.0) + 69.0;
    midi = (int)(n + (n < 0 ? -0.5 : 0.5));
    if (midi < 21) midi = 21;
    if (midi > 108) midi = 108;
    return midi;
}

void pf_name_midi(int midi, char *name, unsigned name_n) {
    int pc, oct;
    if (!name || name_n < 4u) return;
    if (midi < 21) midi = 21;
    if (midi > 108) midi = 108;
    pc = midi % 12;
    if (pc < 0) pc += 12;
    oct = midi / 12 - 1;
    snprintf(name, name_n, "%s%d", k_name[pc], oct);
}

int pf_cents_vs_midi(float hz, int midi) {
    double target;
    if (midi < 21 || midi > 108 || hz <= 0.0f) return 0;
    target = 440.0 * pow(2.0, (midi - 69) / 12.0);
    return (int)(1200.0 * log((double)hz / target) / log(2.0) +
                 ((double)hz >= target ? 0.5 : -0.5));
}

int pf_hold_midi(int *cand, unsigned *n, int midi, int shown) {
    if (!cand || !n || midi == 0) return shown;
    if (midi == *cand) {
        if (*n < 8u) (*n)++;
    } else {
        *cand = midi;
        *n = 1u;
    }
    if (*n >= 3u) return midi;
    return shown;
}

unsigned pf_hps_bin(const float *m2, unsigned n2, unsigned min_bin) {
    unsigned i, best;
    float best_s;
    unsigned hi;
    if (!m2 || n2 <= min_bin + 1u) return 0u;
    hi = n2 / 3u;
    if (hi <= min_bin) hi = n2;
    best = min_bin;
    best_s = -1.0f;
    for (i = min_bin; i < hi; i++) {
        float s = m2[i];
        if (i * 2u < n2) s *= m2[i * 2u];
        if (i * 3u < n2) s *= m2[i * 3u];
        if (s > best_s) {
            best_s = s;
            best = i;
        }
    }
    return best;
}
