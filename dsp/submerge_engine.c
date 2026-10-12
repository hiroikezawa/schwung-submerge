/*
 * SUBMERGE - the engine.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Signal path (per sample; control is updated once per <= 128-frame chunk):
 *
 *   in L/R --(20 Hz HPF)--+--------------------------------------- dry
 *                         |
 *                         +-- mono --> low band (160 Hz LP)  -----+
 *                         |           envelopes fast/slow/swell   |
 *                         |           transient = fast - slow     |  excitation
 *                         |           thump  = LP(transient)      |  (DEPTH decides
 *                         |           energy = low noise x swell  |   the blend)
 *                         |           tnoise = noise x transient  |
 *                         |           sub    = 1/2 flip-flop      |
 *                         |                                       v
 *                         |                         [ resonator bank x8 ]
 *                         |                                       |
 *                         +-- direct (dark LP) x (1-DEPTH)^1.5 -> +
 *                                                                 v
 *                                                       [ diffusion / smear ]
 *                                                                 |
 *                                        +------------------------+
 *                                        v                        v
 *                                [ dark FDN x4 ] ---------> wet = early + tail
 *                                                                 |
 *               out LP (DARK) -> 22 Hz HP -> M/S (side HP 85 Hz) -> knee -> mix
 */
#include "submerge_engine.h"

#include <string.h>

#define SM_CHUNK 128

/* Calibration (set from the renders: wet RMS close to the input's at the
 * defaults, see README "Levels"). */
#ifndef SM_EXC_GAIN
#define SM_EXC_GAIN    6.0f
#endif     /* excitation into the bank (outside the loop) */
#ifndef SM_NOISE_GAIN
#define SM_NOISE_GAIN  9.0f
#endif     /* the twice-low-passed noise is quiet */
#ifndef SM_EARLY_GAIN
#define SM_EARLY_GAIN  1.0f
#endif
#ifndef SM_TAIL_GAIN
#define SM_TAIL_GAIN   2.5f
#endif
#ifndef SM_WET_GAIN
#define SM_WET_GAIN    1.3f
#endif
#define SM_KNEE        0.9f     /* safety knee starts at -0.9 dBFS */

static const float k_tau[SM_P_COUNT] = {
    0.03f,  /* mix (and ramped per sample on top) */
    0.10f,  /* depth */
    0.30f,  /* tone: smooth, but quick enough to hear the knob */
    0.50f,  /* decay */
    0.25f,  /* motion */
    0.15f,  /* blur */
    0.20f,  /* feedback */
    0.30f,  /* dark */
    0.00f,  /* sink: its own ramps */
};

/* ---- modulation --------------------------------------------------------- */

static void wander_init(sm_wander *w, uint32_t *rng) {
    w->target = sm_noise(rng);
    w->s1 = w->s2 = w->target;
    w->timer = sm_rand01(rng);
}

static float wander_step(sm_wander *w, uint32_t *rng, float speed, float dt) {
    w->timer -= dt * speed;
    if (w->timer <= 0.0f) {
        w->target = sm_noise(rng);
        w->timer = 0.35f + 2.4f * sm_rand01(rng);   /* irregular hold times */
    }
    float a = sm_tau_coef(0.6f / speed, dt);
    w->s1 = w->target + a * (w->s1 - w->target);
    w->s2 = w->s1 + a * (w->s2 - w->s1);
    return w->s2;
}

/* ---- set-up ------------------------------------------------------------- */

static void analysis_coefs(sm_engine *e) {
    float fs = e->fs;
    e->c_fast_a = sm_tau_coef(0.001f, 1.0f / fs);
    e->c_fast_r = sm_tau_coef(0.030f, 1.0f / fs);
    e->c_slow_a = sm_tau_coef(0.012f, 1.0f / fs);
    e->c_slow_r = sm_tau_coef(0.280f, 1.0f / fs);
    e->c_swell_r = sm_tau_coef(0.600f, 1.0f / fs);
    sm_biquad_highpass(&e->hp_in[0], 20.0f, 0.7071f, fs);
    sm_biquad_highpass(&e->hp_in[1], 20.0f, 0.7071f, fs);
    sm_biquad_lowpass(&e->lowband, 160.0f, 0.7071f, fs);
    sm_lp1_set(&e->noise_lp1, 140.0f, fs);
    sm_lp1_set(&e->noise_lp2, 140.0f, fs);
    sm_lp1_set(&e->tnoise_lp, 500.0f, fs);
    sm_lp1_set(&e->sub_lp, 90.0f, fs);
    sm_biquad_highpass(&e->out_sub[0], 22.0f, 0.6f, fs);
    sm_biquad_highpass(&e->out_sub[1], 22.0f, 0.6f, fs);
    sm_biquad_highpass(&e->side_hp, 85.0f, 0.7071f, fs);
}

void sm_engine_reset(sm_engine *e) {
    for (int c = 0; c < 2; ++c) {
        sm_biquad_clear(&e->hp_in[c]);
        sm_biquad_clear(&e->out_lp[c]);
        sm_biquad_clear(&e->out_sub[c]);
        e->direct_lp[c][0].y = e->direct_lp[c][1].y = 0.0f;
    }
    sm_biquad_clear(&e->lowband);
    sm_biquad_clear(&e->thump_lp);
    sm_biquad_clear(&e->side_hp);
    e->env_fast = e->env_slow = e->env_swell = 0.0f;
    e->noise_lp1.y = e->noise_lp2.y = e->tnoise_lp.y = e->sub_lp.y = 0.0f;
    e->sub_ff = 1.0f;
    e->sub_sign = 0.0f;
    sm_resbank_clear(&e->bank);
    sm_diffusion_clear(&e->diff);
    sm_fdn_clear(&e->fdn);
    e->last_wet_peak = 0.0f;
}

void sm_engine_init(sm_engine *e, float fs) {
    memset(e, 0, sizeof(*e));
    e->fs = fs;
    for (int i = 0; i < SM_P_COUNT; ++i) e->p[i] = e->s[i] = SM_PARAMS[i].def;
    e->rng = 0x5eed1234u;
    sm_diffusion_init(&e->diff, fs);
    sm_fdn_init(&e->fdn, fs);
    analysis_coefs(e);
    sm_engine_reset(e);
    for (int i = 0; i < SM_WANDERERS; ++i) wander_init(&e->wander[i], &e->rng);
    sm_engine_snap(e);
}

void sm_engine_set(sm_engine *e, int param, float value) {
    if (param < 0 || param >= SM_P_COUNT || !isfinite(value)) return;
    const sm_param_info *pi = &SM_PARAMS[param];
    value = sm_clampf(value, pi->min, pi->max);
    if (pi->integer) value = value >= 0.5f ? 1.0f : 0.0f;
    e->p[param] = value;
}

float sm_engine_get(const sm_engine *e, int param) {
    if (param < 0 || param >= SM_P_COUNT) return 0.0f;
    return e->p[param];
}

void sm_engine_snap(sm_engine *e) {
    for (int i = 0; i < SM_P_COUNT; ++i) e->s[i] = e->p[i];
    e->tone_log = logf(sm_tone_hz(e->p[SM_P_TONE]));
    e->sink_amt = e->sink_deep = e->sink_fb = e->p[SM_P_SINK];
    float m = e->p[SM_P_MIX];
    e->dry_g = m >= 1.0f ? 0.0f : cosf(m * SM_PI * 0.5f);
    e->wet_g = sinf(m * SM_PI * 0.5f);
}

/* ---- control (once per chunk) -------------------------------------------- */

static void control(sm_engine *e, int frames, float *dry_to, float *wet_to) {
    const float fs = e->fs, dt = (float)frames / fs;

    for (int i = 0; i < SM_P_COUNT; ++i) {
        if (i == SM_P_SINK) continue;
        float a = sm_tau_coef(k_tau[i], dt);
        e->s[i] = e->p[i] + a * (e->s[i] - e->p[i]);
        if (fabsf(e->s[i] - e->p[i]) < 1e-5f) e->s[i] = e->p[i];
    }
    /* SINK: in over ~3.5 s (and keeps sinking over ~9 s), out over ~2 s. */
    float sink = e->p[SM_P_SINK];
    float up_a = dt / 3.5f, up_d = dt / 9.0f, down = dt / 2.0f;
    e->sink_amt += sink > e->sink_amt ? fminf(up_a, sink - e->sink_amt) : -fminf(down, e->sink_amt - sink);
    e->sink_deep += sink > e->sink_deep ? fminf(up_d, sink - e->sink_deep) : -fminf(down, e->sink_deep - sink);
    /* The loop is held FIRST (~0.4 s), so whatever is sounding when the pad
     * is pressed keeps sounding while the input fades out behind it. */
    e->sink_fb += sink > e->sink_fb ? fminf(dt / 0.4f, sink - e->sink_fb) : -fminf(down, e->sink_fb - sink);
    const float sa = e->sink_amt, sd = e->sink_deep;

    const float depth = e->s[SM_P_DEPTH];
    const float motion = e->s[SM_P_MOTION];
    const float blur = e->s[SM_P_BLUR];
    const float dark = sm_clampf(e->s[SM_P_DARK] + (1.0f - e->s[SM_P_DARK]) * 0.75f * sd, 0.0f, 1.0f);
    const float fb = e->s[SM_P_FEEDBACK] + fmaxf(0.0f, 0.985f - e->s[SM_P_FEEDBACK]) * e->sink_fb;
    const float decay_s = sm_decay_s(e->s[SM_P_DECAY]);

    /* TONE glides in the log domain, slowly; SINK pulls it ~18 % down. */
    {
        float a = sm_tau_coef(k_tau[SM_P_TONE], dt);
        float target = logf(sm_tone_hz(e->p[SM_P_TONE]));
        e->tone_log = target + a * (e->tone_log - target);
    }
    const float f0 = expf(e->tone_log) * (1.0f - 0.18f * sd);

    /* Modulators. SINK slows them. */
    const float speed = (0.25f + 1.5f * motion) * (1.0f - 0.5f * sa);
    float wv[SM_WANDERERS];
    for (int i = 0; i < SM_WANDERERS; ++i) wv[i] = wander_step(&e->wander[i], &e->rng, speed, dt);
    const float *w_freq = wv, *w_amp = wv + SM_NRES, *w_pan = wv + 2 * SM_NRES;
    const float *w_line = wv + 3 * SM_NRES;

    /* Resonator bank: modes ring 0.15..1.8 s; the long tail is the FDN's. */
    const float res_t60 = 0.35f + 0.25f * e->s[SM_P_DECAY];
    float weight[SM_NRES], pan[SM_NRES];
    for (int i = 0; i < SM_NRES; ++i) {
        float md = sm_res_mod_depth[i] * motion;
        float hz = f0 * sm_res_ratio[i] * (1.0f + sm_res_detune[i]) * (1.0f + 0.02f * md * w_freq[i]);
        float t60 = res_t60 * sm_res_decay_scale[i] * (1.0f + 0.3f * md * w_amp[i]);
        sm_resbank_tune(&e->bank, i, hz, t60, fs);
        float tilt = 1.0f / (1.0f + (0.25f + 0.9f * dark) * (float)i);   /* DARK: upper modes weaker */
        float amp = fmaxf(0.08f, 1.0f + 0.85f * md * w_amp[i]);           /* "go...goro...guu" */
        weight[i] = tilt * amp;
        pan[i] = sm_res_pan[i] * (0.6f + 0.6f * blur) + 0.3f * md * w_pan[i];
    }
    sm_resbank_weights(&e->bank, weight, pan);
    e->bank_drive = 1.15f + 0.6f * dark;

    /* Diffusion: DEPTH smears a little too; damping falls with DARK. */
    const float blur_e = sm_clampf(blur + 0.25f * depth, 0.0f, 1.0f);
    sm_diffusion_set(&e->diff, blur_e, 1100.0f * powf(150.0f / 1100.0f, dark), fs);

    /* Feedback network. DECAY gives the loop gain a tail of decay_s; FEEDBACK
     * pushes the gain toward SM_G_TOP (practically infinite). */
    {
        float len[SM_NLINES], g[SM_NLINES];
        float shape = 1.0f - powf(1.0f - fb, 1.3f);
        for (int i = 0; i < SM_NLINES; ++i) {
            float base = sm_fdn_base_ms[i] * 0.001f * fs;
            len[i] = base * (1.0f + 0.035f * motion * w_line[i]);
            float g_dec = powf(10.0f, -3.0f * base / (fs * decay_s));
            float gi = g_dec + (SM_G_TOP - g_dec) * shape;
            gi *= 1.0f - 0.002f * motion * (0.5f + 0.5f * w_line[i]);   /* uneven distribution */
            g[i] = gi;
        }
        /* Resonance emphasis: every line favours the same broad region
         * around the bank's third mode (spread a few % apart). Off-region
         * content keeps c per pass, so as FEEDBACK rises the field left
         * circulating narrows toward the resonance. (Giving each line a
         * DIFFERENT mode lost ~2.5 dB/s at FEEDBACK 100: the matrix hands
         * every line's content to three lines that attenuate it.) */
        static const float spread[SM_NLINES] = {0.97f, 1.00f, 1.03f, 1.06f};
        float res_hz[SM_NLINES];
        for (int i = 0; i < SM_NLINES; ++i)
            res_hz[i] = f0 * sm_res_ratio[2] * spread[i] * (1.0f + 0.02f * motion * w_line[i]);
        float c = 1.0f - 0.03f * fb;
        sm_fdn_set(&e->fdn, len, g, res_hz, c, 1.0f + 0.5f * dark,
                   1400.0f * powf(220.0f / 1400.0f, dark), fs, frames);
    }

    /* Excitation: DEPTH moves it from the waveform to its envelope. */
    {
        float d = depth, keep = 1.0f - d;
        e->w_low = 0.22f * powf(keep, 1.3f) + 0.02f;
        e->w_thump = 1.2f + 5.0f * d;
        e->w_energy = 0.5f + 3.5f * d;
        e->w_tnoise = (1.0f + 4.0f * d) * (1.0f - 0.6f * dark) * (1.0f - sa);
        e->w_sub = 0.25f * d;   /* a seasoning: more and it reads as an octave-down */
        e->exc_gain = SM_EXC_GAIN * (1.0f - 0.85f * sa);
        e->direct_w = 0.35f * powf(keep, 1.5f) * (1.0f - 0.9f * sa);
        /* Transient suppression: the thump gets rounder with DEPTH and DARK. */
        float th_hz = (70.0f - 35.0f * d) * (1.0f - 0.4f * dark);
        sm_biquad_lowpass(&e->thump_lp, th_hz, 0.6f, fs);
        e->c_swell_a = sm_tau_coef(0.04f + 0.25f * d, 1.0f / fs);
        float dir_hz = 2200.0f * powf(400.0f / 2200.0f, dark);
        for (int c = 0; c < 2; ++c) {
            sm_lp1_set(&e->direct_lp[c][0], dir_hz, fs);
            sm_lp1_set(&e->direct_lp[c][1], dir_hz, fs);
        }
    }

    /* Output low-pass: 1.6 kHz .. 260 Hz. Never below that - DARK 100 must
     * still be heard, not felt. */
    {
        float hz = 1600.0f * powf(260.0f / 1600.0f, dark);
        sm_biquad_lowpass(&e->out_lp[0], hz, 0.6f, fs);
        sm_biquad_lowpass(&e->out_lp[1], hz, 0.6f, fs);
    }

    /* Mix: equal power, exact at both ends. */
    float m = e->s[SM_P_MIX];
    *dry_to = m >= 1.0f ? 0.0f : cosf(m * SM_PI * 0.5f);
    *wet_to = m <= 0.0f ? 0.0f : sinf(m * SM_PI * 0.5f);
}

/* ---- audio ---------------------------------------------------------------- */

static inline float follow(float env, float x, float att, float rel) {
    float c = x > env ? att : rel;
    return x + c * (env - x);
}

static inline float knee(float x) {
    float a = fabsf(x);
    if (a <= SM_KNEE) return x;
    float over = (a - SM_KNEE) / (1.0f - SM_KNEE);
    float y = SM_KNEE + (1.0f - SM_KNEE) * sm_softclip(over);
    return x < 0.0f ? -y : y;
}

/* Returns 1 when the chunk is exactly dry (out == in). */
static int process_chunk(sm_engine *e, const float *in, float *out, int n) {
    float dry_to, wet_to;
    control(e, n, &dry_to, &wet_to);
    const float dry0 = e->dry_g, wet0 = e->wet_g;
    const float d_dry = (dry_to - dry0) / (float)n, d_wet = (wet_to - wet0) / (float)n;
    const int pure_dry = dry0 == 1.0f && dry_to == 1.0f && wet0 == 0.0f && wet_to == 0.0f;

    const float bd = e->bank_drive, inv_bd = 1.0f / bd;
    /* The field is loud enough to carry on its own (SINK relies on it); a
     * long DECAY accumulates more, so it is trimmed back a little there. */
    const float tail_g = SM_TAIL_GAIN * (1.0f - 0.45f * e->s[SM_P_DECAY]);
    float peak = 0.0f, check = 0.0f;   /* check: NaN/Inf anywhere ends up here */

    for (int t = 0; t < n; ++t) {
        float xl = in[2 * t], xr = in[2 * t + 1];
        float hl = sm_biquad_run(&e->hp_in[0], xl);
        float hr = sm_biquad_run(&e->hp_in[1], xr);
        float m = 0.5f * (hl + hr);

        /* analysis */
        float lowb = sm_biquad_run(&e->lowband, m);
        float a = fabsf(m);
        e->env_fast = follow(e->env_fast, a, e->c_fast_a, e->c_fast_r);
        e->env_slow = follow(e->env_slow, a, e->c_slow_a, e->c_slow_r);
        e->env_swell = follow(e->env_swell, a, e->c_swell_a, e->c_swell_r);
        float trans = e->env_fast - 1.3f * e->env_slow;
        if (trans < 0.0f) trans = 0.0f;
        float thump = sm_biquad_run(&e->thump_lp, trans);

        float nz = sm_noise(&e->rng);
        float nlow = sm_lp1_run(&e->noise_lp2, sm_lp1_run(&e->noise_lp1, nz));
        float energy = nlow * SM_NOISE_GAIN * e->env_swell;
        float tnoise = sm_lp1_run(&e->tnoise_lp, nz) * trans * 3.0f;

        /* sub: a flip-flop toggled on every rising zero crossing of the low
         * band (hysteresis follows the level), i.e. half its frequency. */
        float hyst = 0.05f * e->env_slow + 1e-4f;
        if (e->sub_sign <= 0.0f && lowb > hyst) { e->sub_sign = 1.0f; e->sub_ff = -e->sub_ff; }
        else if (e->sub_sign >= 0.0f && lowb < -hyst) e->sub_sign = -1.0f;
        float sub = sm_lp1_run(&e->sub_lp, e->sub_ff) * e->env_slow;

        float exc = (e->w_low * lowb + e->w_thump * thump + e->w_energy * energy +
                     e->w_tnoise * tnoise + e->w_sub * sub) * e->exc_gain;

        /* feedback network: read */
        float o[SM_NLINES], u[SM_NLINES];
        sm_fdn_read(&e->fdn, o, u);

        /* resonator bank, light asymmetric saturation */
        float bl, br;
        sm_resbank_run(&e->bank, exc, &bl, &br);
        bl = sm_softclip_asym(bl * bd, 0.15f) * inv_bd;
        br = sm_softclip_asym(br * bd, -0.12f) * inv_bd;

        /* the still-recognisable share, dark */
        float dl = sm_lp1_run(&e->direct_lp[0][1], sm_lp1_run(&e->direct_lp[0][0], hl));
        float dr = sm_lp1_run(&e->direct_lp[1][1], sm_lp1_run(&e->direct_lp[1][0], hr));
        float vl = bl + e->direct_w * dl;
        float vr = br + e->direct_w * dr;

        sm_diffusion_run(&e->diff, &vl, &vr);

        /* feedback network: write */
        float inj[SM_NLINES] = {0.5f * vl, 0.5f * vr, 0.5f * vl, -0.5f * vr};
        sm_fdn_write(&e->fdn, u, inj);

        float tl = 0.5f * (o[0] + o[2]);
        float tr = 0.5f * (o[1] - o[3]);
        float wl = SM_EARLY_GAIN * vl + tail_g * tl;
        float wr = SM_EARLY_GAIN * vr + tail_g * tr;

        /* output: dark LP, DC, mono lows, safety */
        wl = sm_biquad_run(&e->out_sub[0], sm_biquad_run(&e->out_lp[0], wl));
        wr = sm_biquad_run(&e->out_sub[1], sm_biquad_run(&e->out_lp[1], wr));
        float mid = 0.5f * (wl + wr);
        float side = sm_biquad_run(&e->side_hp, 0.5f * (wl - wr));
        wl = knee(SM_WET_GAIN * (mid + side));
        wr = knee(SM_WET_GAIN * (mid - side));
        float pk = fabsf(wl) > fabsf(wr) ? fabsf(wl) : fabsf(wr);
        if (pk > peak) peak = pk;
        check += wl + wr;

        float dg = dry0 + d_dry * (float)(t + 1);
        float wg = wet0 + d_wet * (float)(t + 1);
        out[2 * t] = dg * xl + wg * wl;
        out[2 * t + 1] = dg * xr + wg * wr;
    }
    e->dry_g = dry_to;
    e->wet_g = wet_to;
    e->last_wet_peak = peak;
    sm_fdn_govern(&e->fdn, (float)n / e->fs);

    /* Denormal flush, and the NaN/Inf guard: one non-finite value anywhere
     * would poison every loop forever, so the internal state is rebuilt from
     * silence and the chunk goes out dry. */
    sm_resbank_flush(&e->bank);
    sm_diffusion_flush(&e->diff);
    sm_fdn_flush(&e->fdn);
    /* (A comparison with NaN is false, so `peak` alone could never show one:
     * the sum carries it.) */
    float state = e->fdn.lp[0].y + e->fdn.lp[1].y + e->fdn.lp[2].y + e->fdn.lp[3].y +
                  e->bank.mode[0].y1 + e->diff.ch[0].damp2.y + e->diff.ch[1].damp2.y;
    if (!isfinite(check) || !isfinite(state) || !isfinite(e->fdn.gov) || !isfinite(e->env_slow)) {
        e->nan_resets++;
        sm_engine_reset(e);
        for (int t = 0; t < 2 * n; ++t) out[t] = in[t] * e->dry_g;
        return 0;
    }
    if (pure_dry) {
        memmove(out, in, sizeof(float) * 2 * (size_t)n);
        return 1;
    }
    return 0;
}

void sm_engine_process_f(sm_engine *e, const float *in, float *out, int frames) {
    float tmp[2 * SM_CHUNK];
    while (frames > 0) {
        int n = frames < SM_CHUNK ? frames : SM_CHUNK;
        memcpy(tmp, in, sizeof(float) * 2 * (size_t)n);   /* in and out may alias */
        process_chunk(e, tmp, out, n);
        in += 2 * n;
        out += 2 * n;
        frames -= n;
    }
}

static inline int16_t to_i16(float x) {
    float y = x * 32768.0f;
    y = y >= 0.0f ? y + 0.5f : y - 0.5f;
    if (y > 32767.0f) return 32767;
    if (y < -32768.0f) return -32768;
    return (int16_t)y;
}

void sm_engine_process(sm_engine *e, int16_t *audio, int frames) {
    float in[2 * SM_CHUNK], out[2 * SM_CHUNK];
    while (frames > 0) {
        int n = frames < SM_CHUNK ? frames : SM_CHUNK;
        for (int i = 0; i < 2 * n; ++i) in[i] = (float)audio[i] * (1.0f / 32768.0f);
        if (!process_chunk(e, in, out, n)) {
            for (int i = 0; i < 2 * n; ++i) audio[i] = to_i16(out[i]);
        }
        audio += 2 * n;
        frames -= n;
    }
}
