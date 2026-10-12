/*
 * SUBMERGE - offline demo renderer and level/spectrum probe.
 *
 *   render_demo <outdir> [key=value ...]
 *
 * Renders kick / snare / chord / noise-burst material through the engine at
 * the given settings (defaults otherwise) and writes, for each, the dry input
 * and the processed output as WAV, plus one line of measurements:
 *   in/wet RMS, wet peak, and the share of the wet energy below 200 Hz and
 *   above 1 kHz (the "rumble, not shhh" check).
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "signals.h"
#include "submerge_dsp.h"
#include "submerge_engine.h"

#define FS 44100
#define SECS 14

static sm_engine eng;
static float in_buf[2 * FS * SECS], out_buf[2 * FS * SECS];

static void band_shares(const float *buf, int frames, double *lo, double *hi) {
    sm_biquad l, h;
    memset(&l, 0, sizeof(l));
    memset(&h, 0, sizeof(h));
    sm_biquad_lowpass(&l, 200.0f, 0.7071f, FS);
    sm_biquad_highpass(&h, 1000.0f, 0.7071f, FS);
    double t = 0, el = 0, eh = 0;
    for (int i = 0; i < frames; ++i) {
        float m = 0.5f * (buf[2 * i] + buf[2 * i + 1]);
        float a = sm_biquad_run(&l, m), b = sm_biquad_run(&h, m);
        t += m * m; el += a * a; eh += b * b;
    }
    *lo = t > 0 ? el / t : 0;
    *hi = t > 0 ? eh / t : 0;
}

static double rms(const float *buf, int from, int to) {
    double s = 0;
    for (int i = 2 * from; i < 2 * to; ++i) s += buf[i] * buf[i];
    return sqrt(s / (2.0 * (to - from) + 1e-9));
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    const char *names[4] = {"kick", "snare", "chord", "noise"};
    for (int s = 0; s < 4; ++s) {
        sm_engine_init(&eng, FS);
        sm_engine_set(&eng, SM_P_MIX, 1.0f);
        for (int a = 2; a < argc; ++a) {
            char key[32];
            float v;
            if (sscanf(argv[a], "%31[^=]=%f", key, &v) != 2) continue;
            for (int p = 0; p < SM_P_COUNT; ++p)
                if (!strcmp(key, SM_PARAMS[p].key)) sm_engine_set(&eng, p, v);
        }
        sm_engine_snap(&eng);
        int frames = FS * SECS;
        memset(in_buf, 0, sizeof(in_buf));
        sig_rng = 777u + (uint32_t)s;
        /* four bars of material in the first 6 s, then 8 s of tail */
        for (int b = 0; b < 4; ++b) {
            int at = (int)(b * 1.5f * FS);
            if (s == 0) { sig_kick(in_buf, frames, at, FS, 0.7f); sig_kick(in_buf, frames, at + FS * 3 / 4, FS, 0.6f); }
            if (s == 1) { sig_snare(in_buf, frames, at + FS * 3 / 8, FS, 0.6f); sig_snare(in_buf, frames, at + FS * 9 / 8, FS, 0.6f); }
            if (s == 2 && (b % 2 == 0)) sig_chord(in_buf, frames, at, FS, 0.9f);
            if (s == 3) sig_noise_burst(in_buf, frames, at, FS, 0.4f, 0.4f);
        }
        sm_engine_process_f(&eng, in_buf, out_buf, frames);
        double lo, hi, ilo, ihi;
        band_shares(out_buf, frames, &lo, &hi);
        band_shares(in_buf, frames, &ilo, &ihi);
        float peak = 0;
        for (int i = 0; i < 2 * frames; ++i) if (fabsf(out_buf[i]) > peak) peak = fabsf(out_buf[i]);
        printf("%-6s in %.3f (lo %2.0f%% hi %2.0f%%)  wet %.3f peak %.2f  lo<200 %3.0f%%  hi>1k %4.1f%%  tail(10-14s) %.4f\n",
               names[s], rms(in_buf, 0, FS * 6), 100 * ilo, 100 * ihi, rms(out_buf, 0, FS * 6), peak,
               100 * lo, 100 * hi, rms(out_buf, FS * 10, FS * 14));
        char path[512];
        snprintf(path, sizeof(path), "%s/%d_%s_dry.wav", dir, s + 1, names[s]);
        wav_write(path, in_buf, frames, FS);
        snprintf(path, sizeof(path), "%s/%d_%s_submerge.wav", dir, s + 1, names[s]);
        wav_write(path, out_buf, frames, FS);
    }
    return 0;
}
