/*
 * SUBMERGE - the dark feedback network.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Four modulated delay lines (83..174 ms) mixed by an orthonormal 4x4
 * Hadamard matrix. Inside the loop, per line:
 *
 *     read (fractional, MOTION-modulated length)
 *       -> one-pole low-pass  (DARK: the loop gets darker every pass)
 *       -> one-pole high-pass at 2 Hz (no DC build-up; the audible subsonic
 *          guard is the output's 22 Hz high-pass - at 18 Hz IN the loop a
 *          40 Hz tone lost 0.8 dB per pass, i.e. ~8 dB/s, and no FEEDBACK
 *          setting could make the field last)
 *       -> resonance emphasis: c x + (1 - c) BP(x), BP a broad (Q 2)
 *          band-pass on one of the bank's modes. On the mode it passes
 *          everything; away from it only c. FEEDBACK lowers c, so a high
 *          FEEDBACK lets the RESONANCE FIELD sustain itself while the rest
 *          dies away - the "slowly changing timbre" (the lines' modes are
 *          redistributed among each other by the matrix every pass)
 *       -> gain g_i x governor (DECAY sets g, FEEDBACK pushes it toward 1)
 *       -> Hadamard -> soft clip -> written back, plus the new input
 *
 * Bounded by construction:
 *
 *   |u| <= g_top |read|                   (g is divided by the measured peak
 *                                          of |LP x HP x emphasis|, so the
 *                                          best-passed frequency gets g and
 *                                          nothing more; governor <= 1)
 *   |sat(H u)| <= |H u| = |u|             (H orthonormal, clip slope <= 1)
 *
 * so no setting can make it grow; FEEDBACK 100 % only makes it decay very
 * slowly. (An earlier version sent a share of every pass round through the
 * resonator bank instead: that path's rank-one projection threw away about
 * half of what it took, every pass, so FEEDBACK 100 % died in seconds.) The
 * governor is a second, independent net: it only acts when the loop's RMS
 * passes -9 dBFS, which normal material never reaches.
 */
#ifndef SUBMERGE_FEEDBACK_H
#define SUBMERGE_FEEDBACK_H

#include "submerge_dsp.h"

#define SM_NLINES 4
#define SM_LINE_SIZE 16384      /* power of two; 341 ms at 48 kHz */
#define SM_G_TOP 0.9995f        /* the loop gain FEEDBACK 100 % approaches */
#define SM_GOV_RMS 0.35f        /* governor threshold, ~ -9 dBFS */

typedef struct {
    float buf[SM_NLINES][SM_LINE_SIZE];
    int pos;
    float len[SM_NLINES];        /* current length, samples (fractional) */
    float len_step[SM_NLINES];   /* per-sample ramp toward this block's target */
    sm_lp1 lp[SM_NLINES];
    sm_hp1 hp[SM_NLINES];
    sm_biquad bp[SM_NLINES];     /* resonance emphasis (only b0/a1/a2 used) */
    float c;                     /* off-resonance pass-through, this block */
    float g[SM_NLINES];          /* loop gain incl. governor, this block */
    float drive;                 /* clip drive (>= 1; output divided back) */
    float gov;                   /* governor gain, 0..1 */
    float energy;                /* sum of o^2 this block */
    int energy_n;
} sm_fdn;

/* Base lengths, in ms. */
extern const float sm_fdn_base_ms[SM_NLINES];

void sm_fdn_init(sm_fdn *f, float fs);
void sm_fdn_clear(sm_fdn *f);

/* Per-block control. target_len in samples; g_line is DECAY/FEEDBACK's gain
 * per line before the governor; res_hz each line's emphasised mode and c
 * the off-resonance pass-through (1 = no emphasis); lp_hz the loop
 * low-pass. */
void sm_fdn_set(sm_fdn *f, const float *target_len, const float *g_line,
                const float *res_hz, float c, float drive, float lp_hz, float fs, int frames);

/* End of block: update the governor from this block's energy. */
void sm_fdn_govern(sm_fdn *f, float block_dt);

static inline float sm_fdn_tap(const float *buf, int pos, float len) {
    float rp = (float)pos - len;
    int i = (int)floorf(rp);
    float fr = rp - (float)i;
    float a = buf[i & (SM_LINE_SIZE - 1)];
    float b = buf[(i + 1) & (SM_LINE_SIZE - 1)];
    return a + fr * (b - a);
}

/* Read the four lines: o = filtered outputs, u = o x gain. */
static inline void sm_fdn_read(sm_fdn *f, float *o, float *u) {
    for (int i = 0; i < SM_NLINES; ++i) {
        f->len[i] += f->len_step[i];
        float x = sm_fdn_tap(f->buf[i], f->pos, f->len[i]);
        x = sm_hp1_run(&f->hp[i], sm_lp1_run(&f->lp[i], x));
        sm_biquad *b = &f->bp[i];   /* RBJ band-pass, peak 0 dB: b0 (x - x2) */
        float y = b->b0 * (x - b->x2) - b->a1 * b->y1 - b->a2 * b->y2;
        b->x2 = b->x1; b->x1 = x;
        b->y2 = b->y1; b->y1 = y;
        x = f->c * x + (1.0f - f->c) * y;
        o[i] = x;
        u[i] = x * f->g[i];
        f->energy += x * x;
    }
    f->energy_n += SM_NLINES;
}

/* Mix, clip and write back, adding the new input inj[i]. */
static inline void sm_fdn_write(sm_fdn *f, const float *u, const float *inj) {
    float a = u[0], b = u[1], c = u[2], d = u[3];
    float h[SM_NLINES] = {
        0.5f * (a + b + c + d), 0.5f * (a - b + c - d),
        0.5f * (a + b - c - d), 0.5f * (a - b - c + d)};
    float inv = 1.0f / f->drive;
    for (int i = 0; i < SM_NLINES; ++i) {
        float s = sm_softclip(h[i] * f->drive) * inv;
        f->buf[i][f->pos] = s + inj[i];
    }
    f->pos = (f->pos + 1) & (SM_LINE_SIZE - 1);
}

void sm_fdn_flush(sm_fdn *f);

#endif
