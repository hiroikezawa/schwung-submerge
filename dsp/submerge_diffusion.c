/*
 * SUBMERGE - BLUR: the diffusion / short-delay network.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 */
#include "submerge_diffusion.h"

#include <string.h>

/* Mutually prime-ish lengths in ms, different per side. Long for a
 * diffuser - at 40..200 Hz a 3 ms all-pass does nothing audible. */
static const float k_ap_ms[2][SM_AP_N] = {
    {11.3f, 17.9f, 27.1f, 41.3f},
    {12.7f, 19.3f, 29.9f, 37.7f},
};
static const float k_tap_ms[2][SM_SMEAR_TAPS] = {
    {19.0f, 41.0f, 67.0f, 101.0f},
    {23.0f, 47.0f, 73.0f, 109.0f},
};

void sm_diffusion_clear(sm_diffusion *d) {
    for (int c = 0; c < 2; ++c) {
        for (int k = 0; k < SM_AP_N; ++k) memset(d->ch[c].ap[k].buf, 0, sizeof(d->ch[c].ap[k].buf));
        memset(d->ch[c].smear, 0, sizeof(d->ch[c].smear));
        d->ch[c].damp1.y = d->ch[c].damp2.y = 0.0f;
    }
    d->pos = 0;
}

void sm_diffusion_init(sm_diffusion *d, float fs) {
    sm_diffusion_clear(d);
    for (int c = 0; c < 2; ++c) {
        for (int k = 0; k < SM_AP_N; ++k) d->ch[c].ap[k].len = (int)(k_ap_ms[c][k] * 0.001f * fs);
        for (int k = 0; k < SM_SMEAR_TAPS; ++k) d->ch[c].tap[k] = (int)(k_tap_ms[c][k] * 0.001f * fs);
    }
    d->coef = 0.0f;
    d->smear = 0.0f;
}

void sm_diffusion_set(sm_diffusion *d, float blur, float damp_hz, float fs) {
    blur = sm_clampf(blur, 0.0f, 1.0f);
    d->coef = 0.12f + 0.58f * blur;      /* 0.12 .. 0.70: never ringing */
    d->smear = 0.8f * blur * blur;       /* the taps take over late in the knob */
    for (int c = 0; c < 2; ++c) {
        sm_lp1_set(&d->ch[c].damp1, damp_hz, fs);
        sm_lp1_set(&d->ch[c].damp2, damp_hz * 1.3f, fs);
    }
}

void sm_diffusion_flush(sm_diffusion *d) {
    for (int c = 0; c < 2; ++c) {
        d->ch[c].damp1.y = sm_flush(d->ch[c].damp1.y);
        d->ch[c].damp2.y = sm_flush(d->ch[c].damp2.y);
    }
}
