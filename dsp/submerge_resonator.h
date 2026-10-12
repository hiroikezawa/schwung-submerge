/*
 * SUBMERGE - the low-frequency resonator bank.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Eight two-pole band-pass resonators ("modes"), each normalised to a peak
 * gain of 1, tuned to the TONE fundamental times slightly inharmonic ratios
 * (1.00 1.13 1.27 1.43 1.61 1.80 2.03 2.27 - no chord, just density). Every
 * mode has its own detune, decay scale, stereo position and modulation depth.
 *
 * The bank's mode weights are normalised to sum to <= 1, and each mode's
 * |H| <= 1, so the bank as a whole can never ADD gain to whatever passes
 * through it. That is what lets the feedback network route its output back
 * through the bank without the loop growing (submerge_feedback.h). Any
 * loudness the excitation needs is applied BEFORE the bank, outside the loop.
 */
#ifndef SUBMERGE_RESONATOR_H
#define SUBMERGE_RESONATOR_H

#include "submerge_dsp.h"

#define SM_NRES 8

typedef struct {
    /* coefficients (set per block) */
    float b0, a1, a2;
    /* state */
    float x1, x2, y1, y2;
} sm_mode;

typedef struct {
    sm_mode mode[SM_NRES];
    float wl[SM_NRES], wr[SM_NRES];   /* per-mode weight x pan, this block */
} sm_resbank;

/* Fixed per-mode character (const tables in submerge_resonator.c). */
extern const float sm_res_ratio[SM_NRES];
extern const float sm_res_detune[SM_NRES];
extern const float sm_res_decay_scale[SM_NRES];
extern const float sm_res_pan[SM_NRES];
extern const float sm_res_mod_depth[SM_NRES];

void sm_resbank_clear(sm_resbank *b);

/* Tune mode i: frequency in Hz and its T60 in seconds. */
void sm_resbank_tune(sm_resbank *b, int i, float hz, float t60, float fs);

/* Set the output weights. weight[i] >= 0 is the mode's level before the
 * bank normalises the set to sum <= 1; pan[i] in [-1, 1]. */
void sm_resbank_weights(sm_resbank *b, const float *weight, const float *pan);

/* One sample in (mono excitation), stereo out. */
static inline void sm_resbank_run(sm_resbank *b, float x, float *l, float *r) {
    float sl = 0.0f, sr = 0.0f;
    for (int i = 0; i < SM_NRES; ++i) {
        sm_mode *m = &b->mode[i];
        float y = m->b0 * (x - m->x2) - m->a1 * m->y1 - m->a2 * m->y2;
        m->x2 = m->x1; m->x1 = x;
        m->y2 = m->y1; m->y1 = y;
        sl += b->wl[i] * y;
        sr += b->wr[i] * y;
    }
    *l = sl;
    *r = sr;
}

/* Flush near-zero state so a dying bank reaches exact silence. */
void sm_resbank_flush(sm_resbank *b);

#endif
