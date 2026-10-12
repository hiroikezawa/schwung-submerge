/*
 * SUBMERGE - engine and plugin tests (desktop).
 *
 * Links the DSP sources directly: the API tests go through
 * move_audio_fx_init_v2() exactly as the chain host does (int16, 128-frame
 * blocks), the internal ones (NaN guard) use the engine.
 *
 *   test_engine            everything (~1-2 min: two 10-minute stability runs)
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/audio_fx_api_v2.h"
#include "signals.h"
#include "submerge_dsp.h"
#include "submerge_engine.h"

audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

#define FS 44100
#define BLOCK 128

static int failures = 0;
static void check(int ok, const char *what, double v) {
    printf("%s  %-62s %.5g\n", ok ? "ok  " : "FAIL", what, v);
    if (!ok) failures++;
}

static audio_fx_api_v2_t *api;
static host_api_v1_t host;

static void *make(float mix) {
    void *in = api->create_instance("", NULL);
    char v[32];
    snprintf(v, sizeof(v), "%g", mix);
    api->set_param(in, "mix", v);
    return in;
}
static void setp(void *in, const char *k, double v) {
    char b[32];
    snprintf(b, sizeof(b), "%g", v);
    api->set_param(in, k, b);
}
static const char *getp(void *in, const char *k) {
    static char b[4096];
    if (api->get_param(in, k, b, sizeof(b)) < 0) return "<err>";
    return b;
}
static void extreme(void *in) {
    setp(in, "depth", 1); setp(in, "decay", 1); setp(in, "motion", 1);
    setp(in, "blur", 1); setp(in, "feedback", 1); setp(in, "dark", 1);
}

/* Run `frames` of float stereo `src` (NULL = silence) through the plugin,
 * writing the processed float result to `dst` (may be NULL). */
static void run(void *in, const float *src, float *dst, long frames) {
    int16_t blk[2 * BLOCK];
    for (long f = 0; f < frames; f += BLOCK) {
        int n = frames - f < BLOCK ? (int)(frames - f) : BLOCK;
        for (int i = 0; i < 2 * n; ++i) {
            float x = src ? src[2 * f + i] : 0.0f;
            long v = lrintf(x * 32767.0f);
            blk[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        api->process_block(in, blk, n);
        if (dst) for (int i = 0; i < 2 * n; ++i) dst[2 * f + i] = blk[i] / 32768.0f;
    }
}

static double rms_of(const float *b, long from, long to) {
    double s = 0;
    for (long i = 2 * from; i < 2 * to; ++i) s += (double)b[i] * b[i];
    return sqrt(s / (2.0 * (double)(to - from)));
}
static double peak_of(const float *b, long from, long to) {
    double p = 0;
    for (long i = 2 * from; i < 2 * to; ++i) if (fabs(b[i]) > p) p = fabs(b[i]);
    return p;
}
static double db(double r) { return 20 * log10(r + 1e-12); }
/* Share of energy above 300 Hz (mono) - "has it got darker?" */
static double high_share(const float *b, long from, long to) {
    sm_biquad h;
    memset(&h, 0, sizeof(h));
    sm_biquad_highpass(&h, 300.0f, 0.7071f, FS);
    double t = 0, e = 0;
    for (long i = from; i < to; ++i) {
        float m = 0.5f * (b[2 * i] + b[2 * i + 1]);
        float y = sm_biquad_run(&h, m);
        t += m * m; e += y * y;
    }
    return t > 0 ? e / t : 0;
}
static double low_share(const float *b, long from, long to) {
    sm_biquad l;
    memset(&l, 0, sizeof(l));
    sm_biquad_lowpass(&l, 200.0f, 0.7071f, FS);
    double t = 0, e = 0;
    for (long i = from; i < to; ++i) {
        float m = 0.5f * (b[2 * i] + b[2 * i + 1]);
        float y = sm_biquad_run(&l, m);
        t += m * m; e += y * y;
    }
    return t > 0 ? e / t : 0;
}
/* Largest sample-to-sample step (a click is a big one). */
static double max_step(const float *b, long from, long to) {
    double m = 0;
    for (long i = from + 1; i < to; ++i)
        for (int c = 0; c < 2; ++c) {
            double d = fabs(b[2 * i + c] - b[2 * (i - 1) + c]);
            if (d > m) m = d;
        }
    return m;
}

#define SEC(s) ((long)((s) * FS))
static float *A, *B;   /* scratch, 70 s stereo each */
#define SCRATCH SEC(70)

static void fill_drums(float *buf, long frames, float amp) {
    memset(buf, 0, sizeof(float) * 2 * (size_t)frames);
    for (long at = 0; at + SEC(0.6) < frames; at += SEC(0.5)) {
        if ((at / SEC(0.5)) % 2 == 0) sig_kick(buf, (int)frames, (int)at, FS, amp);
        else sig_snare(buf, (int)frames, (int)at, FS, amp * 0.9f);
    }
}

/* ---- tests ---------------------------------------------------------------- */

static void test_dry(void) {
    /* A saved MIX 0 restores exactly (no ramp from the default). */
    void *in = api->create_instance("", NULL);
    api->set_param(in, "state", "{\"version\":1,\"mix\":0,\"depth\":1,\"tone\":0.5,\"decay\":1,"
                                "\"motion\":1,\"blur\":1,\"feedback\":1,\"dark\":0.5,\"sink\":0}");
    fill_drums(A, SEC(4), 0.8f);
    for (long i = 0; i < SEC(4); ++i) { A[2 * i] += 0.1f * sig_noise(); A[2 * i + 1] += 0.1f * sig_noise(); }
    run(in, A, B, SEC(4));
    long diff = 0;
    for (long i = 0; i < 2 * SEC(4); ++i) {
        long a = lrintf(A[i] * 32767.0f); a = a > 32767 ? 32767 : a < -32768 ? -32768 : a;
        if (lrintf(B[i] * 32768.0f) != a) diff++;
    }
    check(diff == 0, "MIX 0 (restored): output == input, bit for bit (samples differing)", (double)diff);
    api->destroy_instance(in);

    /* MIX moved to 0 from 0.5: ramps, then exact */
    in = make(0.5f);
    run(in, A, NULL, SEC(1));
    setp(in, "mix", 0);
    run(in, A, B, SEC(3));
    diff = 0;
    for (long i = 2 * SEC(1); i < 2 * SEC(3); ++i) {
        long a = lrintf(A[i] * 32767.0f); a = a > 32767 ? 32767 : a < -32768 ? -32768 : a;
        if (lrintf(B[i] * 32768.0f) != a) diff++;
    }
    check(diff == 0, "MIX turned to 0: exact dry within 1 s (samples differing)", (double)diff);
    api->destroy_instance(in);
}

/* At MIX 100 the output must not contain the input itself: least-squares
 * gain of the input in the output, at lags 0..5 ms, must be tiny. */
static void test_wet(void) {
    for (int depth = 0; depth <= 1; ++depth) {
        for (int src = 0; src < 2; ++src) {
            void *in = make(1.0f);
            setp(in, "depth", depth);
            long n = SEC(8);
            if (src == 0) {
                memset(A, 0, sizeof(float) * 2 * (size_t)n);
                sig_rng = 99;
                for (long i = 0; i < n; ++i) { float v = 0.3f * sig_noise(); A[2 * i] = v; A[2 * i + 1] = v; }
            } else {
                fill_drums(A, n, 0.7f);
            }
            run(in, A, B, n);
            /* Compared above 250 Hz: a kick's body and the low field it
             * excites share a frequency, so they correlate without either
             * being a copy; a direct copy of the input would show up here
             * just as much as anywhere. */
            sm_biquad ha[2], hb[2];
            memset(ha, 0, sizeof(ha));
            memset(hb, 0, sizeof(hb));
            for (int c = 0; c < 2; ++c) {
                sm_biquad_highpass(&ha[c], 250.0f, 0.7071f, FS);
                sm_biquad_highpass(&hb[c], 250.0f, 0.7071f, FS);
            }
            for (long i = 0; i < n; ++i)
                for (int c = 0; c < 2; ++c) {
                    A[2 * i + c] = sm_biquad_run(&ha[c], A[2 * i + c]);
                    B[2 * i + c] = sm_biquad_run(&hb[c], B[2 * i + c]);
                }
            double worst = 0;
            for (int lag = 0; lag <= SEC(0.005); ++lag) {
                double xy = 0, xx = 0;
                for (long i = SEC(1); i < n; ++i)
                    for (int c = 0; c < 2; ++c) {
                        xy += (double)A[2 * (i - lag) + c] * B[2 * i + c];
                        xx += (double)A[2 * (i - lag) + c] * A[2 * (i - lag) + c];
                    }
                double g = fabs(xy / xx);
                if (g > worst) worst = g;
            }
            char what[96];
            snprintf(what, sizeof(what), "MIX 100, DEPTH %d, %s: input's own gain in output >250 Hz (<0.03)",
                     depth * 100, src ? "drums" : "white noise");
            check(worst < 0.03, what, worst);
            api->destroy_instance(in);
        }
    }
}

/* Ten minutes at every knob on 100 %, fed drums, sines, noise and silence. */
static void test_stability(int sink) {
    void *in = make(1.0f);
    extreme(in);
    if (sink) setp(in, "sink", 1);
    double worst_peak = 0;
    int nonfinite = 0;
    long chunk = SEC(30);
    for (int k = 0; k < 20; ++k) {          /* 20 x 30 s = 10 min */
        switch (k % 5) {
            case 0: fill_drums(A, chunk, 1.0f); break;
            case 1: sig_sine(A, (int)chunk, FS, 40.0f + 20.0f * (float)k, 0.9f); break;
            case 2: sig_pink(A, (int)chunk, 1.5f); break;
            case 3: for (long i = 0; i < 2 * chunk; ++i) A[i] = sig_noise(); break;   /* full-scale white */
            default: memset(A, 0, sizeof(float) * 2 * (size_t)chunk); break;
        }
        run(in, A, B, chunk);
        for (long i = 0; i < 2 * chunk; ++i) if (!isfinite(B[i])) nonfinite++;
        double p = peak_of(B, 0, chunk);
        if (p > worst_peak) worst_peak = p;
    }
    char what[96];
    snprintf(what, sizeof(what), "10 min all-100%%%s: no NaN reset (count)", sink ? " + SINK" : "");
    check(atoi(getp(in, "nan_resets")) == 0 && nonfinite == 0, what, atof(getp(in, "nan_resets")));
    double gov = atof(getp(in, "governor"));
    snprintf(what, sizeof(what), "10 min all-100%%%s: governor finite, in 0..1", sink ? " + SINK" : "");
    check(isfinite(gov) && gov >= 0 && gov <= 1, what, gov);
    snprintf(what, sizeof(what), "10 min all-100%%%s: worst output peak (<= 1)", sink ? " + SINK" : "");
    check(worst_peak <= 1.0, what, worst_peak);
    /* afterwards, silence: it must fade, not hold or grow */
    run(in, NULL, B, SEC(60));
    double r1 = rms_of(B, SEC(0), SEC(10)), r2 = rms_of(B, SEC(50), SEC(60));
    snprintf(what, sizeof(what), "  then 60 s silence: last 10 s vs first 10 s (dB, < 0)");
    check(r2 <= r1 || r2 < 1e-6, what, db(r2) - db(r1));
    api->destroy_instance(in);
}

static void test_silence(void) {
    void *in = make(1.0f);
    extreme(in);
    run(in, NULL, B, SEC(60));
    check(peak_of(B, 0, SEC(60)) == 0.0, "silence in from a fresh instance, all 100 %: silence out", peak_of(B, 0, SEC(60)));
    api->destroy_instance(in);

    /* a burst, then silence: 5 s windows must never get louder */
    in = make(1.0f);
    extreme(in);
    memset(A, 0, sizeof(float) * 2 * (size_t)SEC(65));
    sig_noise_burst(A, (int)SEC(65), 0, FS, 0.8f, 1.0f);
    sig_kick(A, (int)SEC(65), (int)SEC(0.2), FS, 0.9f);
    run(in, A, B, SEC(65));
    double prev = rms_of(B, SEC(5), SEC(10)), worst = -99;
    for (int w = 2; w < 13; ++w) {
        double r = rms_of(B, SEC(5 * w), SEC(5 * w + 5));
        double d = db(r) - db(prev);
        if (d > worst) worst = d;
        prev = r;
    }
    check(worst < 0.5, "burst then silence at FEEDBACK 100: no 5 s window louder than the last (dB)", worst);
    double r10 = rms_of(B, SEC(5), SEC(10)), r60 = rms_of(B, SEC(60), SEC(65));
    check(db(r60) - db(r10) < -1.0 && r60 > 1e-6,
          "  ... and it is still there but fading after a minute (dB over 55 s)", db(r60) - db(r10));
    api->destroy_instance(in);
}

static void test_impulse(void) {
    void *in = make(1.0f);
    memset(A, 0, sizeof(float) * 2 * (size_t)SEC(12));
    A[2 * SEC(0.1)] = A[2 * SEC(0.1) + 1] = 0.9f;
    run(in, A, B, SEC(12));
    double early = rms_of(B, SEC(0.2), SEC(1.0)), late = rms_of(B, SEC(3), SEC(4)),
           end = rms_of(B, SEC(10), SEC(12));
    check(early > 1e-3, "impulse (defaults): a tail is there, 0.1-0.9 s RMS", early);
    check(late < early && end < late, "  ... and it decays (3-4 s RMS)", late);
    check(low_share(B, SEC(0.1), SEC(4)) > 0.9, "  ... and it is low: share below 200 Hz", low_share(B, SEC(0.1), SEC(4)));
    check(peak_of(B, 0, SEC(12)) < 0.9, "  ... with no ringing explosion (peak)", peak_of(B, 0, SEC(12)));
    api->destroy_instance(in);
}

static void test_sines(void) {
    const float hz[4] = {40, 60, 100, 440};
    for (int k = 0; k < 4; ++k) {
        void *in = make(1.0f);
        extreme(in);
        sig_sine(A, (int)SEC(40), FS, hz[k], 0.9f);
        run(in, A, B, SEC(40));
        double r1 = rms_of(B, SEC(20), SEC(30)), r2 = rms_of(B, SEC(30), SEC(40));
        run(in, NULL, A, SEC(20));
        double r3 = rms_of(A, SEC(10), SEC(20));
        char what[96];
        snprintf(what, sizeof(what), "%3.0f Hz sine, all 100 %%: steady (dB 30-40 s vs 20-30 s)", hz[k]);
        check(fabs(db(r2) - db(r1)) < 2.0 && peak_of(B, 0, SEC(40)) <= 1.0, what, db(r2) - db(r1));
        snprintf(what, sizeof(what), "  ... and lets go when the sine stops (dB)");
        check(r3 < r2, what, db(r3) - db(r2));
        api->destroy_instance(in);
    }
}

static void test_noise(void) {
    for (int pink = 0; pink <= 1; ++pink) {
        void *in = make(1.0f);
        extreme(in);
        if (pink) sig_pink(A, (int)SEC(60), 1.2f);
        else for (long i = 0; i < 2 * SEC(60); ++i) A[i] = 0.7f * sig_noise();
        run(in, A, B, SEC(60));
        double r1 = rms_of(B, SEC(30), SEC(45)), r2 = rms_of(B, SEC(45), SEC(60));
        char what[96];
        snprintf(what, sizeof(what), "%s noise 60 s, all 100 %%: no runaway (dB last 15 s vs before)", pink ? "pink" : "white");
        check(db(r2) - db(r1) < 1.5 && peak_of(B, 0, SEC(60)) <= 1.0 && atoi(getp(in, "nan_resets")) == 0, what, db(r2) - db(r1));
        api->destroy_instance(in);
    }
}

/* SINK: new hits count for less, the tail gets darker and longer; off
 * returns within ~3 s, without a click. */
static void test_sink(void) {
    long n = SEC(40);
    memset(A, 0, sizeof(float) * 2 * (size_t)n);
    sig_rng = 5;
    for (long at = 0; at < SEC(30); at += SEC(1)) {   /* a chord-ish hit every second */
        sig_chord(A, (int)n, (int)at, FS, 0.7f);
        sig_kick(A, (int)n, (int)at, FS, 0.5f);
    }
    /* reference run, SINK off */
    void *ref = make(1.0f);
    run(ref, A, B, n);
    double ref_hits = rms_of(B, SEC(14), SEC(20));
    double ref_hi = high_share(B, SEC(14), SEC(20));
    api->destroy_instance(ref);
    static float C[2 * 44100 * 40];
    memcpy(C, B, sizeof(float) * 2 * (size_t)n);

    void *in = make(1.0f);
    run(in, A, B, SEC(8));
    setp(in, "sink", 1);
    run(in, A + 2 * SEC(8), B + 2 * SEC(8), SEC(22));
    double s_hits = rms_of(B, SEC(14), SEC(20));
    double s_hi = high_share(B, SEC(14), SEC(20));
    check(atof(getp(in, "sink_level")) > 0.99, "SINK on: transition complete after a few s (level)", atof(getp(in, "sink_level")));
    check(s_hi < 0.6 * ref_hi, "SINK on: darker (share above 300 Hz vs SINK off, ratio)", s_hi / (ref_hi + 1e-12));
    /* input contribution: how much each new hit lifts the field. Measure the
     * RMS rise in the 300 ms after hits vs the 300 ms before, relative. */
    double lift_ref = 0, lift_sink = 0;
    for (int h = 14; h < 20; ++h) {
        lift_ref += rms_of(C, SEC(h), SEC(h + 0.3)) / (rms_of(C, SEC(h - 0.3), SEC(h)) + 1e-9);
        lift_sink += rms_of(B, SEC(h), SEC(h + 0.3)) / (rms_of(B, SEC(h - 0.3), SEC(h)) + 1e-9);
    }
    check(lift_sink / 6 < lift_ref / 6 - 0.05 || lift_sink / 6 < 1.05,
          "SINK on: a new hit lifts the field less (lift ratio, SINK)", lift_sink / 6);
    (void)s_hits; (void)ref_hits;
    /* tail: stop the input at 30 s, compare the decay over 30..40 s */
    run(in, NULL, B + 2 * SEC(30), SEC(10));
    double sink_tail = db(rms_of(B, SEC(37), SEC(40))) - db(rms_of(B, SEC(30.5), SEC(33.5)));
    double ref_tail = db(rms_of(C, SEC(37), SEC(40))) - db(rms_of(C, SEC(30.5), SEC(33.5)));
    check(sink_tail > ref_tail + 6.0, "SINK on: the tail lasts longer (dB lost over ~6 s, SINK)", sink_tail);
    printf("      (same, SINK off: %.1f dB)\n", ref_tail);
    /* off: smooth way back. A click is judged against the same input
     * through an instance that never sank (the normal state is brighter, so
     * its steps are naturally bigger than the sunk state's). */
    sig_rng = 11;
    sig_pink(A, (int)SEC(12), 0.4f);
    void *norm = make(1.0f);
    run(norm, A, C, SEC(12));
    double normal = max_step(C, SEC(6), SEC(12));
    api->destroy_instance(norm);
    run(in, A, B, SEC(6));
    setp(in, "sink", 0);
    run(in, A + 2 * SEC(6), B + 2 * SEC(6), SEC(6));
    check(atof(getp(in, "sink_level")) == 0.0, "SINK off: back to normal within ~2-3 s (level after 6 s)", atof(getp(in, "sink_level")));
    double during = max_step(B, SEC(6), SEC(12));
    check(during < 1.5 * normal, "SINK off: no click (largest step vs never-sunk, ratio)", during / (normal + 1e-9));
    api->destroy_instance(in);
}

/* v0.1.0 on hardware: raising DECAY or BLUR made the sound DISAPPEAR
 * (narrowing resonators, cancelling smear taps), and SINK went silent. The
 * playing level must hold across both knobs, and SINK must keep sounding. */
static void test_levels_hold(void) {
    const char *keys[2] = {"decay", "blur"};
    fill_drums(A, SEC(8), 0.7f);
    for (int k = 0; k < 2; ++k) {
        double lo = 1e9, hi = 0;
        for (int s = 0; s <= 4; ++s) {
            void *in = make(1.0f);
            api->set_param(in, "state", "{\"version\":1,\"mix\":1,\"depth\":0.7,\"tone\":0.35,\"decay\":0.55,"
                                        "\"motion\":0.4,\"blur\":0.5,\"feedback\":0.35,\"dark\":0.4,\"sink\":0}");
            setp(in, keys[k], s * 0.25);
            run(in, A, B, SEC(8));
            double r = rms_of(B, SEC(2), SEC(8));
            if (r < lo) lo = r;
            if (r > hi) hi = r;
            api->destroy_instance(in);
        }
        char what[96];
        snprintf(what, sizeof(what), "%s 0..100 %%: playing level spread (dB, < 4)", keys[k]);
        check(db(hi) - db(lo) < 4.0, what, db(hi) - db(lo));
    }
    /* SINK pressed on the last hit: still clearly sounding 5-6 s later */
    void *in = make(1.0f);
    run(in, A, B, SEC(6));
    double before = rms_of(B, SEC(4), SEC(6));
    setp(in, "sink", 1);
    run(in, NULL, B, SEC(6));
    double after = rms_of(B, SEC(5), SEC(6));
    check(db(after) - db(before) > -20.0, "SINK on the last hit: 5-6 s later still within 20 dB (dB)", db(after) - db(before));
    api->destroy_instance(in);
}

/* Every knob slammed 0 -> 1 -> 0 under steady material: no clicks. */
static void test_zipper(void) {
    const char *keys[8] = {"mix", "depth", "tone", "decay", "motion", "blur", "feedback", "dark"};
    for (int k = 0; k < 8; ++k) {
        void *in = make(0.5f);
        sig_rng = 3;
        sig_pink(A, (int)SEC(6), 0.4f);
        run(in, A, B, SEC(2));
        double base = max_step(B, SEC(1), SEC(2));
        setp(in, keys[k], 1);
        run(in, A + 2 * SEC(2), B + 2 * SEC(2), SEC(2));
        setp(in, keys[k], 0);
        run(in, A + 2 * SEC(4), B + 2 * SEC(4), SEC(2));
        double jump = max_step(B, SEC(2), SEC(6));
        char what[96];
        snprintf(what, sizeof(what), "%-8s 0<->1 jumps: largest step vs steady state (ratio < 3)", keys[k]);
        check(jump < 3.0 * base + 0.002, what, jump / (base + 1e-9));
        api->destroy_instance(in);
    }
}

static void test_state(void) {
    void *a = make(0.31f);
    setp(a, "depth", 0.12); setp(a, "tone", 0.9); setp(a, "decay", 0.05);
    setp(a, "motion", 0.66); setp(a, "blur", 0.77); setp(a, "feedback", 0.98);
    setp(a, "dark", 0.2); api->set_param(a, "sink", "ON");
    char st[1024];
    snprintf(st, sizeof(st), "%s", getp(a, "state"));
    void *b = make(0.5f);
    api->set_param(b, "state", st);
    char st2[1024];
    snprintf(st2, sizeof(st2), "%s", getp(b, "state"));
    check(!strcmp(st, st2), "state round trip (all 8 knobs + SINK)", 0);
    printf("      %s\n", st);
    check(!strcmp(getp(b, "sink"), "ON"), "  SINK restored ON", 0);
    api->set_param(b, "state", "{\"version\":1,\"mix\":0.2}");
    check(!strcmp(getp(b, "state"), st2), "  incomplete state rejected whole", 0);
    api->set_param(b, "state", "garbage");
    api->set_param(b, "mix", "nan");
    api->set_param(b, "mix", "7");
    check(!strcmp(getp(b, "mix"), "1.0000"), "  out-of-range clamps, NaN ignored", atof(getp(b, "mix")));
    check(!strcmp(getp(b, "ui_hierarchy"), "") && strstr(getp(b, "grid_hierarchy"), "\"feedback\",\"dark\"]") != NULL,
          "ui_hierarchy empty (opens ui_chain.js), grid_hierarchy has the 8 knobs", 0);
    check(strstr(getp(b, "chain_params"), "\"key\":\"mix\"") != NULL, "chain_params served", 0);
    api->destroy_instance(a);
    api->destroy_instance(b);
    /* the pool: eight instances, a ninth refused, freed slots reused */
    void *p[9];
    int got = 0;
    for (int i = 0; i < 9; ++i) if ((p[i] = api->create_instance("", NULL)) != NULL) got++;
    check(got == 8, "instance pool: 8 instances, the 9th refused", got);
    for (int i = 0; i < 9; ++i) if (p[i]) api->destroy_instance(p[i]);
    void *q = api->create_instance("", NULL);
    check(q != NULL, "  freed instances are reused", 0);
    api->destroy_instance(q);
}

static void test_nan_guard(void) {
    static sm_engine e;
    sm_engine_init(&e, FS);
    sm_engine_set(&e, SM_P_MIX, 1.0f);
    fill_drums(A, SEC(2), 0.8f);
    sm_engine_process_f(&e, A, B, (int)SEC(1));
    e.fdn.lp[2].y = NAN;   /* poison the loop */
    sm_engine_process_f(&e, A + 2 * SEC(1), B + 2 * SEC(1), (int)SEC(1));
    int finite = 1;
    for (long i = 2 * SEC(1); i < 2 * SEC(2); ++i) if (!isfinite(B[i])) finite = 0;
    check(e.nan_resets == 1 && finite, "NaN in the loop: one reset, output stays finite", e.nan_resets);
}

static void test_48k(void) {
    host_api_v1_t h48 = host;
    h48.sample_rate = 48000;
    audio_fx_api_v2_t *a = move_audio_fx_init_v2(&h48);
    check(a != NULL, "48 kHz host accepted", 0);
    host_api_v1_t h22 = host;
    h22.sample_rate = 22050;
    check(move_audio_fx_init_v2(&h22) == NULL, "22.05 kHz host refused", 0);
    move_audio_fx_init_v2(&host);   /* back to 44.1 for the rest */
}

int main(void) {
    memset(&host, 0, sizeof(host));
    host.api_version = 1;
    host.sample_rate = FS;
    host.frames_per_block = BLOCK;
    api = move_audio_fx_init_v2(&host);
    if (!api || api->api_version != AUDIO_FX_API_VERSION_2) { puts("FAIL: init"); return 1; }
    A = calloc(2 * (size_t)SCRATCH, sizeof(float));
    B = calloc(2 * (size_t)SCRATCH, sizeof(float));
    test_48k();
    test_dry();
    test_wet();
    test_silence();
    test_impulse();
    test_sines();
    test_noise();
    test_sink();
    test_zipper();
    test_levels_hold();
    test_state();
    test_nan_guard();
    test_stability(0);
    test_stability(1);
    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    puts("ALL ENGINE TESTS PASSED");
    return 0;
}
