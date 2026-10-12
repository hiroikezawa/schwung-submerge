/*
 * SUBMERGE - the low-frequency resonator bank.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 */
#include "submerge_resonator.h"

#include <string.h>

/* Slightly inharmonic ratios: dense, not a chord. */
const float sm_res_ratio[SM_NRES] = {1.00f, 1.13f, 1.27f, 1.43f, 1.61f, 1.80f, 2.03f, 2.27f};
/* Fixed mistuning so no two instances of "the same" mode beat in lockstep. */
const float sm_res_detune[SM_NRES] = {0.000f, 0.004f, -0.003f, 0.006f, -0.005f, 0.003f, -0.007f, 0.005f};
/* Upper modes die sooner, as they would in a large body. */
const float sm_res_decay_scale[SM_NRES] = {1.00f, 0.88f, 0.94f, 0.76f, 0.82f, 0.66f, 0.71f, 0.58f};
/* The lowest modes sit in the middle; the stereo image comes from above. */
const float sm_res_pan[SM_NRES] = {0.00f, -0.12f, 0.12f, -0.28f, 0.28f, -0.42f, 0.42f, -0.05f};
/* How strongly MOTION moves each mode (frequency, level, decay, pan). */
const float sm_res_mod_depth[SM_NRES] = {0.55f, 0.80f, 0.70f, 1.00f, 0.90f, 1.00f, 0.85f, 1.00f};

void sm_resbank_clear(sm_resbank *b) { memset(b, 0, sizeof(*b)); }

/* The RBJ constant-0-dB-peak band-pass, with its bandwidth taken from the
 * pole radius a T60 implies. This form's magnitude never exceeds 1. */
void sm_resbank_tune(sm_resbank *b, int i, float hz, float t60, float fs) {
    sm_mode *m = &b->mode[i];
    hz = sm_clampf(hz, 10.0f, 0.2f * fs);
    t60 = sm_clampf(t60, 0.02f, 30.0f);
    float r = expf(-6.907755f / (t60 * fs));
    float a2 = r * r;
    float alpha = (1.0f - a2) / (1.0f + a2);
    float w = 2.0f * SM_PI * hz / fs;
    m->a1 = -2.0f * cosf(w) / (1.0f + alpha);
    m->a2 = a2;
    m->b0 = alpha / (1.0f + alpha);
}

void sm_resbank_weights(sm_resbank *b, const float *weight, const float *pan) {
    float sum = 0.0f;
    for (int i = 0; i < SM_NRES; ++i) sum += weight[i] > 0.0f ? weight[i] : 0.0f;
    float norm = sum > 1.0f ? 1.0f / sum : 1.0f;
    for (int i = 0; i < SM_NRES; ++i) {
        float w = (weight[i] > 0.0f ? weight[i] : 0.0f) * norm;
        float p = sm_clampf(pan[i], -1.0f, 1.0f);
        float th = (p + 1.0f) * (SM_PI * 0.25f);   /* constant-power pan */
        b->wl[i] = w * cosf(th);
        b->wr[i] = w * sinf(th);
    }
}

void sm_resbank_flush(sm_resbank *b) {
    for (int i = 0; i < SM_NRES; ++i) {
        sm_mode *m = &b->mode[i];
        m->x1 = sm_flush(m->x1); m->x2 = sm_flush(m->x2);
        m->y1 = sm_flush(m->y1); m->y2 = sm_flush(m->y2);
    }
}
