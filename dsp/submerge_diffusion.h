/*
 * SUBMERGE - BLUR: the diffusion / short-delay network.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Per channel: four Schroeder all-passes in series (lengths differ between
 * left and right - that is the stereo decorrelation), then a multi-tap
 * short-delay smear (the transient smearing), then two one-pole low-passes
 * (the strong high damping that keeps it from turning into "shhh").
 *
 * Gain: an all-pass has |H| = 1, the smear is a convex blend of delayed
 * copies, and the low-passes are <= 1, so the network never adds gain -
 * a requirement of the feedback loop it sits in.
 */
#ifndef SUBMERGE_DIFFUSION_H
#define SUBMERGE_DIFFUSION_H

#include "submerge_dsp.h"

#define SM_AP_N 4
#define SM_AP_SIZE 4096         /* power of two; > 45 ms at 48 kHz */
#define SM_SMEAR_SIZE 8192      /* power of two; > 120 ms at 48 kHz */
#define SM_SMEAR_TAPS 4

typedef struct {
    float buf[SM_AP_SIZE];
    int len;
} sm_allpass;

typedef struct {
    sm_allpass ap[SM_AP_N];
    float smear[SM_SMEAR_SIZE];
    int tap[SM_SMEAR_TAPS];
    sm_lp1 damp1, damp2;
} sm_diff_chan;

typedef struct {
    sm_diff_chan ch[2];
    int pos;                 /* shared write index (all buffers advance together) */
    float coef;              /* all-pass coefficient, this block */
    float smear;             /* smear blend 0..0.8, this block */
} sm_diffusion;

void sm_diffusion_init(sm_diffusion *d, float fs);
void sm_diffusion_clear(sm_diffusion *d);

/* blur 0..1, damping cut-off in Hz. */
void sm_diffusion_set(sm_diffusion *d, float blur, float damp_hz, float fs);

static inline float sm_diff_chan_run(sm_diff_chan *c, int pos, float coef, float smear, float x) {
    for (int k = 0; k < SM_AP_N; ++k) {
        sm_allpass *a = &c->ap[k];
        float dly = a->buf[(pos - a->len) & (SM_AP_SIZE - 1)];
        float y = dly - coef * x;
        a->buf[pos & (SM_AP_SIZE - 1)] = x + coef * y;
        x = y;
    }
    c->smear[pos & (SM_SMEAR_SIZE - 1)] = x;
    float taps = 0.0f;
    for (int k = 0; k < SM_SMEAR_TAPS; ++k)
        taps += c->smear[(pos - c->tap[k]) & (SM_SMEAR_SIZE - 1)];
    /* 1/sqrt(taps), not 1/taps: the copies are largely uncorrelated, so an
     * average lost ~6 dB as BLUR rose. */
    x += smear * (taps * 0.5f - x);
    return sm_lp1_run(&c->damp2, sm_lp1_run(&c->damp1, x));
}

static inline void sm_diffusion_run(sm_diffusion *d, float *l, float *r) {
    int pos = d->pos;
    *l = sm_diff_chan_run(&d->ch[0], pos, d->coef, d->smear, *l);
    *r = sm_diff_chan_run(&d->ch[1], pos, d->coef, d->smear, *r);
    d->pos = (pos + 1) & (SM_SMEAR_SIZE - 1);
}

void sm_diffusion_flush(sm_diffusion *d);

#endif
