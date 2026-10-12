/*
 * SUBMERGE - the parameter table shared by the engine, the plugin and tests.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * module.json's chain_params must agree with this table; scripts/metadata.py
 * checks keys, order, ranges and defaults at build time. Every knob is stored
 * as 0..1; the engine maps it (sm_tone_hz, sm_decay_s below are the two
 * non-percentage ones and ui_core.mjs carries the same two formulas for the
 * display, pinned by tests/test_ui.mjs against the built DSP).
 */
#ifndef SUBMERGE_PARAMS_H
#define SUBMERGE_PARAMS_H

#include <math.h>

enum {
    SM_P_MIX,       /* equal-power dry/wet */
    SM_P_DEPTH,     /* how far the input is turned into a low field */
    SM_P_TONE,      /* resonator bank centre, 30..120 Hz fundamental (log) */
    SM_P_DECAY,     /* 0.3..25 s (log) */
    SM_P_MOTION,    /* irregular slow movement */
    SM_P_BLUR,      /* diffusion / smearing */
    SM_P_FEEDBACK,  /* dark self-circulation */
    SM_P_DARK,      /* sink-into-the-ground macro */
    SM_P_SINK,      /* 0/1 toggle, the SINK pad */
    SM_P_COUNT
};

typedef struct {
    const char *key;
    float min, max, def;
    int integer;
} sm_param_info;

static const sm_param_info SM_PARAMS[SM_P_COUNT] = {
    {"mix",      0.0f, 1.0f, 0.5f,  0},
    {"depth",    0.0f, 1.0f, 0.7f,  0},
    {"tone",     0.0f, 1.0f, 0.35f, 0},
    {"decay",    0.0f, 1.0f, 0.55f, 0},
    {"motion",   0.0f, 1.0f, 0.4f,  0},
    {"blur",     0.0f, 1.0f, 0.5f,  0},
    {"feedback", 0.0f, 1.0f, 0.35f, 0},
    {"dark",     0.0f, 1.0f, 0.4f,  0},
    {"sink",     0.0f, 1.0f, 0.0f,  1},
};

#define SM_TONE_LO_HZ 30.0f
#define SM_TONE_HI_HZ 120.0f
#define SM_DECAY_LO_S 0.3f
#define SM_DECAY_HI_S 25.0f

/* Fundamental of the resonator bank (the lowest mode) for a TONE setting. */
static inline float sm_tone_hz(float t) {
    return SM_TONE_LO_HZ * powf(SM_TONE_HI_HZ / SM_TONE_LO_HZ, t);
}
/* Decay time for a DECAY setting. */
static inline float sm_decay_s(float d) {
    return SM_DECAY_LO_S * powf(SM_DECAY_HI_S / SM_DECAY_LO_S, d);
}

#endif
