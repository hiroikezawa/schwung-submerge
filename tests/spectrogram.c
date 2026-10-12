/*
 * SUBMERGE - draw a render as a PNG: top, the dry (grey) and processed
 * (black) level envelopes in dB over time; bottom, the processed signal's
 * spectrogram from 0 to 600 Hz. For checking by eye that an attack really
 * turns into a slow low field. No libraries: the PNG is written with
 * uncompressed ("stored") deflate blocks.
 *
 *   spectrogram <dry.wav> <wet.wav> <out.png>
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 700
#define H_ENV 160
#define H_SPEC 240
#define H (H_ENV + H_SPEC + 10)
#define FMAX 600.0

static float *read_wav(const char *path, int *frames, int *rate) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char hdr[44];
    if (fread(hdr, 1, 44, f) != 44) { fclose(f); return NULL; }
    *rate = hdr[24] | hdr[25] << 8 | hdr[26] << 16 | hdr[27] << 24;
    uint32_t bytes = hdr[40] | hdr[41] << 8 | hdr[42] << 16 | (uint32_t)hdr[43] << 24;
    *frames = (int)(bytes / 4);
    int16_t *raw = malloc(bytes);
    float *mono = malloc(sizeof(float) * (size_t)*frames);
    if (fread(raw, 1, bytes, f) != bytes) { fclose(f); free(raw); free(mono); return NULL; }
    fclose(f);
    for (int i = 0; i < *frames; ++i) mono[i] = 0.5f * (raw[2 * i] + raw[2 * i + 1]) / 32768.0f;
    free(raw);
    return mono;
}

static uint32_t crc_table[256];
static uint32_t crc(const unsigned char *b, size_t n, uint32_t c) {
    c = ~c;
    for (size_t i = 0; i < n; ++i) c = crc_table[(c ^ b[i]) & 255] ^ (c >> 8);
    return ~c;
}
static void be32(unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *type, const unsigned char *data, uint32_t n) {
    unsigned char b[4];
    be32(b, n); fwrite(b, 1, 4, f);
    uint32_t c = crc((const unsigned char *)type, 4, 0);
    c = crc(data, n, c);
    fwrite(type, 1, 4, f); fwrite(data, 1, n, f);
    be32(b, c); fwrite(b, 1, 4, f);
}
static void write_png(const char *path, const unsigned char *gray) {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
    size_t raw_n = (size_t)H * (W + 1);
    unsigned char *raw = malloc(raw_n);
    for (int y = 0; y < H; ++y) { raw[y * (W + 1)] = 0; memcpy(raw + y * (W + 1) + 1, gray + y * W, W); }
    size_t blocks = (raw_n + 65534) / 65535;
    size_t z_n = 2 + raw_n + blocks * 5 + 4;
    unsigned char *z = malloc(z_n), *p = z;
    *p++ = 0x78; *p++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_n; off += 65535) {
        size_t n = raw_n - off < 65535 ? raw_n - off : 65535;
        *p++ = off + n >= raw_n ? 1 : 0;
        *p++ = n & 255; *p++ = n >> 8; *p++ = ~n & 255; *p++ = (~n >> 8) & 255;
        memcpy(p, raw + off, n); p += n;
    }
    for (size_t i = 0; i < raw_n; ++i) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be32(p, b << 16 | a); p += 4;
    FILE *f = fopen(path, "wb");
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ihdr[13];
    be32(ihdr, W); be32(ihdr + 4, H);
    ihdr[8] = 8; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)(p - z));
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw); free(z);
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s dry.wav wet.wav out.png\n", argv[0]); return 2; }
    int nd, nw, rate;
    float *dry = read_wav(argv[1], &nd, &rate), *wet = read_wav(argv[2], &nw, &rate);
    if (!dry || !wet) { fprintf(stderr, "cannot read wavs\n"); return 1; }
    int n = nd < nw ? nd : nw;
    unsigned char *img = malloc(W * H);
    memset(img, 255, W * H);
    /* envelopes: -60..0 dB, 20 ms RMS windows mapped to columns */
    for (int x = 0; x < W; ++x) {
        int a = (int)((long)n * x / W), b = (int)((long)n * (x + 1) / W);
        for (int which = 0; which < 2; ++which) {
            float *s = which ? wet : dry;
            double e = 0;
            for (int i = a; i < b; ++i) e += s[i] * s[i];
            double db = 10 * log10(e / (b - a + 1e-9) + 1e-12);
            int y = (int)((-db) / 60.0 * (H_ENV - 1));
            if (y < 0) y = 0;
            if (y >= H_ENV) continue;
            for (int yy = y; yy < H_ENV; ++yy)
                if (which || img[yy * W + x] == 255) img[yy * W + x] = which ? (yy == y ? 0 : img[yy * W + x]) : 200;
            if (which) { img[y * W + x] = 0; if (y + 1 < H_ENV) img[(y + 1) * W + x] = 0; }
        }
    }
    for (int x = 0; x < W; ++x) img[(H_ENV + 4) * W + x] = 128;
    /* spectrogram of the wet: 8192-point Hann windows, 0..FMAX Hz, -70..0 dB */
    const int N = 8192;
    int bins = (int)(FMAX * N / rate);
    double *mag = malloc(sizeof(double) * bins);
    for (int x = 0; x < W; ++x) {
        int c = (int)((long)n * x / W) - N / 2;
        for (int k = 0; k < bins; ++k) {
            double re = 0, im = 0;
            for (int i = 0; i < N; i += 4) {   /* every 4th sample: fine below 600 Hz after the LP */
                int idx = c + i;
                float v = (idx >= 0 && idx < n) ? wet[idx] : 0.0f;
                double w = 0.5 - 0.5 * cos(2 * M_PI * i / N);
                double ph = 2 * M_PI * k * i / N;
                re += v * w * cos(ph); im -= v * w * sin(ph);
            }
            mag[k] = 20 * log10(sqrt(re * re + im * im) * 8.0 / N + 1e-9);
        }
        for (int y = 0; y < H_SPEC; ++y) {
            double f = FMAX * (H_SPEC - 1 - y) / (H_SPEC - 1);
            int k = (int)(f * N / rate);
            if (k >= bins) k = bins - 1;
            double v = (mag[k] + 70.0) / 70.0;
            if (v < 0) v = 0;
            if (v > 1) v = 1;
            img[(H_ENV + 10 + y) * W + x] = (unsigned char)(255 * (1.0 - v));
        }
    }
    /* 100 Hz grid lines */
    for (int hz = 100; hz < FMAX; hz += 100) {
        int y = H_ENV + 10 + (int)((H_SPEC - 1) * (1.0 - hz / FMAX));
        for (int x = 0; x < W; x += 3) img[y * W + x] = 160;
    }
    write_png(argv[3], img);
    return 0;
}
