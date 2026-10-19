/*
 * SUBMERGE - Schwung audio FX v2 adapter.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Every entry point here runs on Move's SPI callback (or, for a bus insert,
 * the bus worker): no allocation, no file I/O, no locks. Instances come from
 * a static pool claimed with an atomic compare-and-swap, so even
 * create_instance allocates nothing.
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "host/audio_fx_api_v2.h"
#include "submerge_engine.h"
#include "submerge_metadata.h"

#define SM_POOL 8

typedef struct {
    int used;
    sm_engine engine;
} sm_slot;

static sm_slot g_pool[SM_POOL];
static int g_host_rate = 44100;

/* Schwung's standard knob grid, one page, the eight knobs in order. Served
 * as ui_hierarchy, so the shadow UI opens it in a track slot AND in Master
 * FX (which opens nothing for a module without one). */
static const char k_hierarchy[] =
    "{\"modes\":null,\"levels\":{"
      "\"root\":{\"label\":\"SUBMERGE\","
        "\"knobs\":[\"mix\",\"depth\",\"tone\",\"decay\",\"motion\",\"blur\",\"feedback\",\"dark\"],"
        "\"params\":[\"mix\",\"depth\",\"tone\",\"decay\",\"motion\",\"blur\",\"feedback\",\"dark\"]}"
    "}}";

static int param_index(const char *key) {
    for (int i = 0; i < SM_P_COUNT; ++i)
        if (!strcmp(key, SM_PARAMS[i].key)) return i;
    return -1;
}

static const char *skip_space(const char *p) {
    while (*p && isspace((unsigned char)*p)) ++p;
    return p;
}

static int parse_value(const char *text, float *out) {
    const char *p = skip_space(text);
    char *end;
    float v = strtof(p, &end);
    if (end == p || *skip_space(end) || !isfinite(v)) return 0;
    *out = v;
    return 1;
}

/* Flat JSON object of numbers. Every parameter must be present; unknown keys
 * (from a later version, or 0.1.x's "sink") are skipped. Nothing is applied
 * unless all is valid. Version 1 (0.1.x) stored TONE and DECAY as 0..1 knob
 * positions; they are converted to Hz and seconds. */
static int restore_state(sm_engine *e, const char *text) {
    if (strlen(text) > 2048) return 0;
    float next[SM_P_COUNT];
    unsigned seen = 0;
    float version = 1.0f;
    const char *p = skip_space(text);
    if (*p++ != '{') return 0;
    for (int field = 0; field < 64; ++field) {
        p = skip_space(p);
        if (*p++ != '"') return 0;
        char key[32];
        int n = 0;
        while (*p && *p != '"' && n < (int)sizeof(key) - 1) key[n++] = *p++;
        key[n] = 0;
        if (*p++ != '"') return 0;
        p = skip_space(p);
        if (*p++ != ':') return 0;
        p = skip_space(p);
        char *end;
        float v = strtof(p, &end);
        if (end == p || !isfinite(v)) return 0;
        p = end;
        int idx = param_index(key);
        if (idx >= 0) {
            if (seen & (1u << idx)) return 0;
            seen |= 1u << idx;
            next[idx] = v;
        } else if (!strcmp(key, "version")) {
            if (v < 1.0f) return 0;
            version = v;
        }
        p = skip_space(p);
        if (*p == '}') {
            if (*skip_space(p + 1)) return 0;
            if (seen != (1u << SM_P_COUNT) - 1) return 0;
            if (version < 2.0f) {
                next[SM_P_TONE] = 30.0f * powf(4.0f, sm_clampf(next[SM_P_TONE], 0.0f, 1.0f));
                next[SM_P_DECAY] = 0.3f * powf(25.0f / 0.3f, sm_clampf(next[SM_P_DECAY], 0.0f, 1.0f));
            }
            for (int i = 0; i < SM_P_COUNT; ++i) sm_engine_set(e, i, next[i]);
            /* The knobs jump to the saved values; the SOUND still starts from
             * silence (no buffers are saved). */
            sm_engine_snap(e);
            return 1;
        }
        if (*p++ != ',') return 0;
    }
    return 0;
}

static int copy_text(char *buf, int size, const char *text) {
    size_t n = strlen(text);
    if (n >= (size_t)size) {
        buf[0] = 0;
        return -1;
    }
    memcpy(buf, text, n + 1);
    return (int)n;
}

static void *create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir;
    (void)config_json;
    for (int i = 0; i < SM_POOL; ++i) {
        int expected = 0;
        if (__atomic_compare_exchange_n(&g_pool[i].used, &expected, 1, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            sm_engine_init(&g_pool[i].engine, (float)g_host_rate);
            return &g_pool[i].engine;
        }
    }
    return NULL;
}

static void destroy_instance(void *instance) {
    for (int i = 0; i < SM_POOL; ++i) {
        if (instance == &g_pool[i].engine) {
            __atomic_store_n(&g_pool[i].used, 0, __ATOMIC_RELEASE);
            return;
        }
    }
}

static void process_block(void *instance, int16_t *audio, int frames) {
    if (!instance || !audio || frames <= 0) return;
    sm_engine_process((sm_engine *)instance, audio, frames);
}

static void set_param(void *instance, const char *key, const char *val) {
    if (!instance || !key || !val) return;
    sm_engine *e = (sm_engine *)instance;
    if (!strcmp(key, "state")) {
        (void)restore_state(e, val);
        return;
    }
    int idx = param_index(key);
    float v;
    if (idx < 0 || !parse_value(val, &v)) return;
    sm_engine_set(e, idx, v);
}

static int get_param(void *instance, const char *key, char *buf, int size) {
    if (!instance || !key || !buf || size <= 0) return -1;
    buf[0] = 0;
    const sm_engine *e = (const sm_engine *)instance;
    int n = -1;
    int idx = param_index(key);
    if (idx >= 0) {
        n = snprintf(buf, size, "%.4f", e->p[idx]);
    } else if (!strcmp(key, "name")) {
        return copy_text(buf, size, "SUBMERGE");
    } else if (!strcmp(key, "chain_params")) {
        return copy_text(buf, size, kChainParams);
    } else if (!strcmp(key, "ui_hierarchy")) {
        return copy_text(buf, size, k_hierarchy);
    } else if (!strcmp(key, "state")) {
        n = snprintf(buf, size,
            "{\"version\":2,\"mix\":%.6g,\"depth\":%.6g,\"tone\":%.6g,\"decay\":%.6g,"
            "\"motion\":%.6g,\"blur\":%.6g,\"feedback\":%.6g,\"dark\":%.6g}",
            e->p[SM_P_MIX], e->p[SM_P_DEPTH], e->p[SM_P_TONE], e->p[SM_P_DECAY],
            e->p[SM_P_MOTION], e->p[SM_P_BLUR], e->p[SM_P_FEEDBACK], e->p[SM_P_DARK]);
    } else if (!strcmp(key, "version")) {
        return copy_text(buf, size, kVersion);
    } else if (!strcmp(key, "nan_resets")) {
        n = snprintf(buf, size, "%d", e->nan_resets);
    } else if (!strcmp(key, "governor")) {
        n = snprintf(buf, size, "%.3f", e->fdn.gov);
    }
    if (n < 0 || n >= size) {
        buf[0] = 0;
        return -1;
    }
    return n;
}

static audio_fx_api_v2_t g_api = {
    AUDIO_FX_API_VERSION_2,
    create_instance,
    destroy_instance,
    process_block,
    set_param,
    get_param,
    NULL,   /* on_midi: not used */
};

__attribute__((visibility("default")))
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host) {
    if (host && host->sample_rate > 0) {
        if (host->sample_rate != 44100 && host->sample_rate != 48000) return NULL;
        g_host_rate = host->sample_rate;
    }
    return &g_api;
}
