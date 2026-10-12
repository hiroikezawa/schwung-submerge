/*
 * SUBMERGE - the engine: analysis, excitation, the three stages, output.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * The engine is one plain struct with every buffer inline (about 0.5 MB):
 * sm_engine_init() is the only thing that touches it wholesale, and
 * sm_engine_process() allocates nothing, locks nothing and calls nothing
 * but libm.
 */
#ifndef SUBMERGE_ENGINE_H
#define SUBMERGE_ENGINE_H

#include <stdint.h>

#include "submerge_diffusion.h"
#include "submerge_dsp.h"
#include "submerge_feedback.h"
#include "submerge_params.h"
#include "submerge_resonator.h"

/* A slow irregular modulator: sample-and-hold at random intervals, smoothed
 * by two cascaded one-poles. No period to hear. */
typedef struct {
    float target, s1, s2, timer;
} sm_wander;

#define SM_WANDERERS (3 * SM_NRES + SM_NLINES)

typedef struct {
    float fs;

    /* knobs: target (set_param) and smoothed (per block) */
    float p[SM_P_COUNT];
    float s[SM_P_COUNT];
    float tone_log;                 /* smoothed log(fundamental) */
    float sink_amt, sink_deep;      /* the SINK transition, 0..1 each */

    /* per-sample ramped mix gains */
    float dry_g, wet_g;

    /* input conditioning */
    sm_biquad hp_in[2];
    /* analysis */
    sm_biquad lowband;
    float env_fast, env_slow, env_swell;
    float c_fast_a, c_fast_r, c_slow_a, c_slow_r, c_swell_a, c_swell_r;
    sm_biquad thump_lp;
    sm_lp1 noise_lp1, noise_lp2, tnoise_lp, sub_lp;
    uint32_t rng;
    float sub_ff, sub_sign;
    /* excitation weights, this block */
    float w_low, w_thump, w_energy, w_tnoise, w_sub, exc_gain;
    /* the direct (still recognisable) share, DEPTH 0 */
    sm_lp1 direct_lp[2][2];
    float direct_w;

    /* stages */
    sm_resbank bank;
    float bank_drive;
    sm_diffusion diff;
    sm_fdn fdn;

    /* output */
    sm_biquad out_lp[2];
    sm_biquad out_sub[2];           /* subsonic + DC guard, 22 Hz */
    sm_biquad side_hp;

    /* modulation */
    sm_wander wander[SM_WANDERERS];

    /* diagnostics */
    int nan_resets;
    float last_wet_peak;
} sm_engine;

void sm_engine_init(sm_engine *e, float fs);
/* Back to silence without touching the knobs (used by the NaN guard). */
void sm_engine_reset(sm_engine *e);
void sm_engine_set(sm_engine *e, int param, float value);
float sm_engine_get(const sm_engine *e, int param);
/* Jump every smoother to its target (after a state restore). */
void sm_engine_snap(sm_engine *e);
/* In-place stereo interleaved int16, any frame count. */
void sm_engine_process(sm_engine *e, int16_t *audio, int frames);
/* Float version for the tests and the demo renderer (in/out may alias). */
void sm_engine_process_f(sm_engine *e, const float *in, float *out, int frames);

#endif
