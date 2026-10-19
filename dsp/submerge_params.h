/*
 * SUBMERGE - the parameter table shared by the engine, the plugin and tests.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * module.json's chain_params must agree with this table; scripts/metadata.py
 * checks keys, order, ranges and defaults at build time. TONE is stored in
 * Hz and DECAY in seconds, so Schwung's standard knob grid can print them
 * itself ("49 Hz", "3.4 sec"): the module has no screen of its own any more
 * (that is what lets it open in Master FX). The rest are 0..1.
 */
#ifndef SUBMERGE_PARAMS_H
#define SUBMERGE_PARAMS_H

#include <math.h>

enum {
    SM_P_MIX,       /* equal-power dry/wet */
    SM_P_DEPTH,     /* how far the input is turned into a low field */
    SM_P_TONE,      /* resonator bank fundamental, 30..120 Hz */
    SM_P_DECAY,     /* 0.3..25 s */
    SM_P_MOTION,    /* irregular slow movement */
    SM_P_BLUR,      /* diffusion / smearing */
    SM_P_FEEDBACK,  /* dark self-circulation */
    SM_P_DARK,      /* sink-into-the-ground macro */
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
    {"tone",    30.0f, 120.0f, 49.0f, 0},
    {"decay",    0.3f, 25.0f, 3.4f,  0},
    {"motion",   0.0f, 1.0f, 0.4f,  0},
    {"blur",     0.0f, 1.0f, 0.5f,  0},
    {"feedback", 0.0f, 1.0f, 0.35f, 0},
    {"dark",     0.0f, 1.0f, 0.4f,  0},
};

/* DECAY's position on a log scale, 0..1 (what the engine's internal
 * curves are shaped against). */
static inline float sm_decay_norm(float seconds) {
    if (seconds <= 0.3f) return 0.0f;
    return logf(seconds / 0.3f) / logf(25.0f / 0.3f);
}

#endif
