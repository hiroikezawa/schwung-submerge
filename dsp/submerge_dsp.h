/*
 * SUBMERGE - small DSP building blocks shared by every stage.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Everything here is a textbook primitive written from the maths (one-pole
 * filters, an RBJ-style biquad, a rational soft clipper, a xorshift noise
 * source). Nothing allocates; every function is safe on the audio thread.
 */
#ifndef SUBMERGE_DSP_H
#define SUBMERGE_DSP_H

#include <math.h>
#include <stdint.h>

#ifndef SM_PI
#define SM_PI 3.14159265358979323846f
#endif

static inline float sm_clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

/* Coefficient of a one-pole smoother with time constant tau (seconds) when
 * it is stepped every `dt` seconds. */
static inline float sm_tau_coef(float tau, float dt) {
    if (tau <= 0.0f) return 0.0f;
    return expf(-dt / tau);
}

/* One-pole low-pass. y += (1-a)(x-y), a from the cut-off. */
typedef struct { float a, y; } sm_lp1;

static inline void sm_lp1_set(sm_lp1 *f, float hz, float fs) {
    f->a = expf(-2.0f * SM_PI * hz / fs);
}
static inline float sm_lp1_run(sm_lp1 *f, float x) {
    f->y = x + f->a * (f->y - x);
    return f->y;
}

/* One-pole high-pass (input minus its one-pole low-pass). */
typedef struct { sm_lp1 lp; } sm_hp1;

static inline void sm_hp1_set(sm_hp1 *f, float hz, float fs) { sm_lp1_set(&f->lp, hz, fs); }
static inline float sm_hp1_run(sm_hp1 *f, float x) { return x - sm_lp1_run(&f->lp, x); }

/* Direct-form-I biquad. */
typedef struct { float b0, b1, b2, a1, a2, x1, x2, y1, y2; } sm_biquad;

static inline void sm_biquad_lowpass(sm_biquad *f, float hz, float q, float fs) {
    float w = 2.0f * SM_PI * hz / fs, c = cosf(w), al = sinf(w) / (2.0f * q);
    float n = 1.0f / (1.0f + al);
    f->b0 = (1.0f - c) * 0.5f * n;
    f->b1 = (1.0f - c) * n;
    f->b2 = f->b0;
    f->a1 = -2.0f * c * n;
    f->a2 = (1.0f - al) * n;
}
static inline void sm_biquad_highpass(sm_biquad *f, float hz, float q, float fs) {
    float w = 2.0f * SM_PI * hz / fs, c = cosf(w), al = sinf(w) / (2.0f * q);
    float n = 1.0f / (1.0f + al);
    f->b0 = (1.0f + c) * 0.5f * n;
    f->b1 = -(1.0f + c) * n;
    f->b2 = f->b0;
    f->a1 = -2.0f * c * n;
    f->a2 = (1.0f - al) * n;
}
static inline float sm_biquad_run(sm_biquad *f, float x) {
    float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1; f->x1 = x;
    f->y2 = f->y1; f->y1 = y;
    return y;
}
static inline void sm_biquad_clear(sm_biquad *f) { f->x1 = f->x2 = f->y1 = f->y2 = 0.0f; }

/* Rational soft clipper: tanh-like, slope exactly 1 at zero, never steeper
 * than 1 anywhere (so it can only take energy OUT of a loop), saturates at
 * +-1. Inputs beyond +-3 are clamped first. */
static inline float sm_softclip(float x) {
    x = sm_clampf(x, -3.0f, 3.0f);
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* The same curve with a resting bias, so positive and negative half-waves
 * bend differently (a little even-harmonic glue). The bias is subtracted
 * back out, so silence maps to silence; the slope stays <= 1. */
static inline float sm_softclip_asym(float x, float bias) {
    return sm_softclip(x + bias) - sm_softclip(bias);
}

/* xorshift32 white noise in [-1, 1). */
static inline float sm_noise(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}
static inline float sm_rand01(uint32_t *s) { return 0.5f * (sm_noise(s) + 1.0f); }

/* A tiny value below which a filter state is flushed to zero, so a decaying
 * loop never lives in denormals (slow on some CPUs) and silence is silence. */
#define SM_FLUSH 1e-15f
static inline float sm_flush(float x) { return fabsf(x) < SM_FLUSH ? 0.0f : x; }

#endif
