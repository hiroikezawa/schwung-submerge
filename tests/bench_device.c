/*
 * SUBMERGE - CPU benchmark meant to run ON the Move (static aarch64 binary).
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Runs the engine exactly as the chain host does - 128-frame int16 blocks at
 * 44.1 kHz - and reports the mean and worst block time for a few settings.
 * It is a normal-priority process: it measures the code, it does not compete
 * with Move's realtime threads for core 3 (pin it with taskset 0x7).
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "submerge_engine.h"

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static sm_engine e;

int main(void) {
    const struct { const char *name; float v[8]; } cfg[] = {
        {"defaults", {0.5f, 0.7f, 49.0f, 3.4f, 0.4f, 0.5f, 0.35f, 0.4f}},
        {"all max", {1, 1, 120, 25, 1, 1, 1, 1}},
    };
    int16_t buf[256];
    uint32_t rng = 1;
    for (unsigned c = 0; c < sizeof(cfg) / sizeof(cfg[0]); ++c) {
        sm_engine_init(&e, 44100.0f);
        for (int p = 0; p < 8; ++p) sm_engine_set(&e, p, cfg[c].v[p]);
        sm_engine_snap(&e);
        double worst = 0, total = 0;
        const int warm = 400, n = 3445;   /* ~10 s measured */
        for (int i = 0; i < warm + n; ++i) {
            for (int k = 0; k < 256; ++k) {
                rng = rng * 1664525u + 1013904223u;
                buf[k] = (int16_t)((int32_t)rng >> 19);
            }
            double t0 = now_us();
            sm_engine_process(&e, buf, 128);
            double dt = now_us() - t0;
            if (i < warm) continue;
            total += dt;
            if (dt > worst) worst = dt;
        }
        printf("%-18s mean %6.1f us (%4.1f %% of 2902 us)  worst %6.1f us\n", cfg[c].name,
               total / n, 100.0 * total / n / 2902.0, worst);
    }
    return 0;
}
