/*
 * SUBMERGE - the dark feedback network.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 */
#include "submerge_feedback.h"

#include <string.h>

const float sm_fdn_base_ms[SM_NLINES] = {83.1f, 109.7f, 137.3f, 173.9f};

void sm_fdn_clear(sm_fdn *f) {
    memset(f->buf, 0, sizeof(f->buf));
    for (int i = 0; i < SM_NLINES; ++i) {
        f->lp[i].y = 0.0f;
        f->hp[i].lp.y = 0.0f;
        sm_biquad_clear(&f->bp[i]);
    }
    f->pos = 0;
    f->gov = 1.0f;
    f->energy = 0.0f;
    f->energy_n = 0;
}

void sm_fdn_init(sm_fdn *f, float fs) {
    sm_fdn_clear(f);
    for (int i = 0; i < SM_NLINES; ++i) {
        f->len[i] = sm_fdn_base_ms[i] * 0.001f * fs;
        f->len_step[i] = 0.0f;
        f->g[i] = 0.0f;
        sm_hp1_set(&f->hp[i], 2.0f, fs);
        sm_lp1_set(&f->lp[i], 600.0f, fs);
    }
    f->c = 1.0f;
    f->drive = 1.0f;
}

/* |LP x HP x emphasis| of line i at angular frequency w. */
static float chain_gain(const sm_fdn *f, int i, float w) {
    float c1 = cosf(w), s1 = sinf(w), c2 = cosf(2.0f * w), s2 = sinf(2.0f * w);
    float a = f->lp[i].a, h = f->hp[i].lp.a;
    float lp2 = (1.0f - a) * (1.0f - a) / (1.0f - 2.0f * a * c1 + a * a);
    float hp2 = h * h * (2.0f - 2.0f * c1) / (1.0f - 2.0f * h * c1 + h * h);
    const sm_biquad *b = &f->bp[i];
    /* BP = b0 (1 - z^-2) / (1 + a1 z^-1 + a2 z^-2), z^-1 = e^{-jw} */
    float nr = b->b0 * (1.0f - c2), ni = b->b0 * s2;
    float dr = 1.0f + b->a1 * c1 + b->a2 * c2, di = -(b->a1 * s1 + b->a2 * s2);
    float den = dr * dr + di * di;
    float br = (nr * dr + ni * di) / den, bi = (ni * dr - nr * di) / den;
    float er = f->c + (1.0f - f->c) * br, ei = (1.0f - f->c) * bi;
    return sqrtf(lp2 * hp2 * (er * er + ei * ei));
}

void sm_fdn_set(sm_fdn *f, const float *target_len, const float *g_line,
                const float *res_hz, float c, float drive, float lp_hz, float fs, int frames) {
    f->c = sm_clampf(c, 0.0f, 1.0f);
    f->drive = drive < 1.0f ? 1.0f : drive;
    for (int i = 0; i < SM_NLINES; ++i) {
        float t = sm_clampf(target_len[i], 2.0f, (float)(SM_LINE_SIZE - 4));
        f->len_step[i] = frames > 0 ? (t - f->len[i]) / (float)frames : 0.0f;
        sm_lp1_set(&f->lp[i], lp_hz * (1.0f + 0.07f * (float)i), fs);
        /* RBJ band-pass, constant 0 dB peak, Q 2 */
        float w = 2.0f * SM_PI * sm_clampf(res_hz[i], 10.0f, 0.2f * fs) / fs;
        float al = sinf(w) / (2.0f * 2.0f), n = 1.0f / (1.0f + al);
        f->bp[i].b0 = al * n;
        f->bp[i].a1 = -2.0f * cosf(w) * n;
        f->bp[i].a2 = (1.0f - al) * n;
        /* The in-loop filters lose a little even at their best frequency
         * (at DARK 100 the low-pass alone cost ~0.15 dB a pass at 50 Hz,
         * so FEEDBACK 100 died within a minute). Normalise the line by the
         * chain's PEAK gain - found on a 40-point grid, 8 Hz..1 kHz, plus a
         * margin - so the best-passed frequency gets exactly g and nothing
         * gets more: the bound in the header still holds. */
        float peak = 0.0f;
        for (int k = 0; k < 40; ++k) {
            float hz = 8.0f * powf(125.0f, (float)k / 39.0f);
            float gk = chain_gain(f, i, 2.0f * SM_PI * hz / fs);
            if (gk > peak) peak = gk;
        }
        peak *= 1.002f;
        float norm = peak > 0.5f ? 1.0f / peak : 2.0f;
        f->g[i] = sm_clampf(g_line[i], 0.0f, SM_G_TOP) * norm * f->gov;
        if (f->g[i] * peak > SM_G_TOP) f->g[i] = SM_G_TOP / peak;
    }
}

void sm_fdn_govern(sm_fdn *f, float block_dt) {
    float ms = f->energy_n > 0 ? f->energy / (float)f->energy_n : 0.0f;
    f->energy = 0.0f;
    f->energy_n = 0;
    float rms = sqrtf(ms);
    float target = rms > SM_GOV_RMS ? SM_GOV_RMS / rms : 1.0f;
    /* Down in ~80 ms, back up over ~2 s. */
    float tau = target < f->gov ? 0.08f : 2.0f;
    float a = sm_tau_coef(tau, block_dt);
    f->gov = target + a * (f->gov - target);
    if (!(f->gov > 0.0f)) f->gov = 0.0f;   /* also catches NaN */
    if (f->gov > 1.0f) f->gov = 1.0f;
}

void sm_fdn_flush(sm_fdn *f) {
    for (int i = 0; i < SM_NLINES; ++i) {
        f->lp[i].y = sm_flush(f->lp[i].y);
        f->hp[i].lp.y = sm_flush(f->hp[i].lp.y);
        sm_biquad *b = &f->bp[i];
        b->x1 = sm_flush(b->x1); b->x2 = sm_flush(b->x2);
        b->y1 = sm_flush(b->y1); b->y2 = sm_flush(b->y2);
    }
}
