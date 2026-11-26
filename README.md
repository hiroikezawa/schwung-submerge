# SUBMERGE

**English** | [日本語](README.ja.md)

A fully original Audio FX module for Schwung on Ableton Move.

Rather than putting a reverb or delay on the input, it erases the input's
outline, timbre and attack and turns it into **ground rumble, the reverberation
of an underground space, distant low frequencies, lumps of rolling low end and
irregularly heaving dark ambience**. It does not make the source reverberate; it
turns the source into "a low-frequency environmental phenomenon".

- The opposite direction from particles, air and high-frequency texture (the
  Clouds family): sub / rumble / earth / decay / dark low-frequency field
- 8 knobs on one page (knob 1 is always MIX). Usable in a track slot and in
  **Master FX**
- The DSP is a fully original implementation, MIT License (author Hiro Ikezawa)

| Knob | Parameter | Display |
|---|---|---|
| 1 | MIX | 0–100 % |
| 2 | DEPTH | 0–100 % |
| 3 | TONE | 30–120 Hz (the lowest mode of the resonator bank) |
| 4 | DECAY | 0.3–25 s |
| 5 | MOTION | 0–100 % |
| 6 | BLUR | 0–100 % |
| 7 | FEEDBACK | 0–100 % |
| 8 | DARK | 0–100 % |

## Using it

1. Put **SUBMERGE** in a track slot's Audio FX (Signal Chain) or in Master FX
   (Shift+Vol+Menu)
2. Start by playing a kick or snare with the defaults, MIX 50 % and DEPTH 70 %
3. The higher DEPTH goes, the less the original instrument can be recognised,
   until only the rumble is left
4. At FEEDBACK of 90 % or more the field sustains itself for a long time (it does
   not run away even at 100 %)

The screen is Schwung's standard knob grid (the same look as other modules). The
module has no screen of its own and declares its 8 knobs with `ui_hierarchy`, so
the same screen opens on a track and in Master FX (and in Sends / bus inserts).
Touching a knob shows its value large in the header, like `TONE 49 HZ` /
`DECAY 3.4 SEC` / `FDBK 96%`.

TONE holds Hz and DECAY seconds directly as their values (so the standard grid
can show them with units). The standard grid has no way to declare a logarithmic
curve, so the knobs move linearly in Hz / seconds.

## Files

```text
schwung-submerge/
├── LICENSE                     MIT (Hiro Ikezawa)
├── README.md                   this file
├── README.ja.md                Japanese README
├── THIRD_PARTY_LICENSES.md     notice for the Schwung API headers (MIT)
├── module.json                 id submerge, audio_fx, 8 chain_params
├── help.json                   on-device help (ASCII)
├── dsp/
│   ├── submerge_plugin.c       audio_fx_api_v2 adapter, ui_hierarchy, instance pool, saved state
│   ├── submerge_engine.c/.h    analysis, excitation, wiring of the 3 stages, output stage, smoothing
│   ├── submerge_resonator.c/.h low-frequency resonator bank (8 modes)
│   ├── submerge_diffusion.c/.h BLUR: allpass diffusion + multi-tap smear + high-frequency damping
│   ├── submerge_feedback.c/.h  dark feedback network (4-line FDN) + governor
│   ├── submerge_params.h       parameter table (checked against module.json at build time)
│   └── submerge_dsp.h          one-pole filters / biquads / soft clip / noise
├── third_party/schwung/        Schwung's plugin API headers (MIT, unmodified)
├── scripts/
│   ├── build.sh                native / asan / arm64 builds; arm64 puts a tarball in dist/
│   ├── test.sh                 all tests (below)
│   ├── render_demo.sh          WAV + PNG generation for offline listening
│   ├── install.py              install through Schwung Manager
│   ├── metadata.py             module.json → embedded in the DSP + consistency check
│   ├── check_abi.py            ABI check of the ARM64 .so
│   └── check_licenses.py       check for copyleft and copyright notices
└── tests/
    ├── test_engine.c           engine / plugin API tests
    ├── test_grid.mjs           standard grid test with the same page engine as the device
    ├── dump_contract.py        passes the built .so's answers to the grid test
    ├── render_demo.c           demo renderer and level/band probe
    ├── spectrogram.c           envelope + 0–600 Hz spectrogram to PNG
    ├── signals.h               synthetic test signals: kick / snare / chord / noise etc.
    └── bench_device.c          CPU bench for a real Move (static aarch64 binary)
```

The chain host loads `audio_fx/<id>/<id>.so`, so the DSP is `submerge.so`.

## DSP signal path

```text
Stereo In ─┬──────────────────────────────────────────────────────── Dry ───┐
           │                                                                 │
           └─ 20 Hz HPF (2nd order, the minimum low cut on the input)         │
                 │                                                           │
                 ├─ mono ─ analysis ─────────────────────────┐               │
                 │    low band   = 160 Hz LPF                │               │
                 │    env fast   = 1 ms / 30 ms              │               │
                 │    env slow   = 12 ms / 280 ms            │ excitation    │
                 │    env swell  = (40-290 ms) / 600 ms      │ (DEPTH sets   │
                 │    transient  = fast - 1.3 x slow (>=0)   │  the blend)   │
                 │    thump      = LPF(transient)            │               │
                 │    energy     = low-band noise x swell    │               │
                 │    tnoise     = 500 Hz LPF noise x transient               │
                 │    sub        = 1/2 division at low-band zero crossings x env
                 │                                           v               │
                 │                    [ low-frequency resonator bank x8 ]     │
                 │                     -> asymmetric soft saturation          │
                 │                                           │               │
                 └─ direct (dark LPF) x 0.35(1-DEPTH)^1.5 -> +               │
                                                             v               │
                                    [ BLUR: 4 allpass stages + 4-tap smear    │
                                      + 2 one-pole LPFs (heavy HF damping) ]  │
                                                             │               │
                                  ┌──────────────────────────┤               │
                                  v                          v               │
                      [ dark feedback network, 4 lines ]  early (diffused field)
                        read (modulated) -> LPF(DARK)         │               │
                        -> 2 Hz HPF -> resonance emphasis     │               │
                        -> gain(DECAY/FEEDBACK, peak-normalised)              │
                        -> Hadamard -> soft clip -> write     │               │
                                  │ tail                     │               │
                                  └─────────> wet = early + tail              │
                                                  │                          │
           output LPF (DARK, 1.6 kHz-260 Hz) -> 22 Hz HPF (subsonic/DC)       │
           -> M/S (85 Hz HPF on Side only = mono lows) -> safety knee (-0.9 dBFS~)
                                                  │                          │
                                      equal-power MIX <──────────────────────┘
```

The important points:

- **The wet path is not `Input → Reverb`.** The only path by which the input
  waveform itself enters the wet is "direct": 0.35 at DEPTH 0 % and 0 at DEPTH
  100 %, and even that goes through the diffusion network, so it falls apart in
  time. At high DEPTH the resonators are excited only by envelopes, transients and
  energy (noise × envelope), and no waveform information of the original
  instrument remains in the wet.
- The excitation uses noise, so even the same input gives a slightly different
  "rumble" each time (the irregularity of thunder and ground rumble).
- The division of labour: the resonator bank sits outside the loop, the feedback
  network inside it.

### Low-frequency resonator bank (`submerge_resonator.c`)

8 two-pole band-pass resonators (RBJ's "0 dB peak" type; |H| ≤ 1 at every frequency).

- Frequency = TONE's fundamental × the ratios
  `1.00 1.13 1.27 1.43 1.61 1.80 2.03 2.27` (non-integer ratios: density, not a
  chord) with a fixed detune (±0.3–0.7 %)
- At the bottom of TONE: 30/34/38/43/48/54/61/68 Hz; at the top: 120…272 Hz
- Decay (T60) = (0.35 + 0.25 × DECAY knob) s × a per-mode factor (higher modes
  shorter). The resonators' own Q is kept down and the long tail is left to the
  feedback network (so it does not become a set of pure sine waves). v0.1.0 made
  it proportional to DECAY seconds up to 1.8 s, so raising DECAY narrowed the
  band and dropped the playing level by −11 dB — the sound "disappeared"
- Output weights per mode (DARK weakens the higher modes), normalised in total.
  Placement differs slightly per mode, with lower modes nearer the centre

### BLUR (`submerge_diffusion.c`)

4 Schroeder allpass stages of different lengths left and right (L: 11.3 / 17.9 /
27.1 / 41.3 ms, R: 12.7 / 19.3 / 29.9 / 37.7 ms, lengths that act on the low end)
→ a 4-tap smear (19–109 ms, tap sum × 1/√4; v0.1.0 used the mean × 1/4 and, through
cancellation, dropped −6.7 dB at maximum BLUR) → two one-pole LPFs (DARK takes them
from 1.1 kHz → 150 Hz). The allpasses have |H| = 1, the smear is a convex
combination of delayed copies and the LPFs are ≤ 1, so this network adds no gain.
No grains are used, and no high frequencies are created.

### Dark feedback network (`submerge_feedback.c`)

An FDN mixing 4 delay lines (83.1 / 109.7 / 137.3 / 173.9 ms, wobbling ±3.5 % with
MOTION) through an orthogonal Hadamard matrix. Each pass round the loop:

1. Fractional-delay read (linear interpolation, length ramped within the block)
2. A one-pole LPF (DARK takes it from 1.4 kHz → 220 Hz, each line offset by 7 %) —
   darker on every pass
3. A one-pole 2 Hz HPF (prevents DC build-up)
4. Resonance emphasis `c·x + (1−c)·BP(x)` (BP near the resonators' third mode,
   Q 2). FEEDBACK lowers c from 1 → 0.97, so at high FEEDBACK everything outside
   the resonant band dies first and "a resonant field" sustains itself (the timbre
   changes gradually)
5. Gain g (below) × the governor
6. Hadamard → soft clip → write back (+ new input from the diffusion network ×0.5)

How the design got here: at first the in-loop HPF was 18 Hz and part of the FDN
output went back through the resonator bank, but the former cost 0.8 dB per pass
at 40 Hz and the latter threw away about half of what it took in on each pass, so
even at FEEDBACK 100 % it died within seconds. It was measured and changed to the
structure above.

## Internal mapping of each knob

TONE is stored as Hz and DECAY as seconds directly; the others are stored as 0..1
and mapped inside the engine.

| Knob | What moves inside |
|---|---|
| MIX | Equal power: dry = cos(πm/2), wet = sin(πm/2). Exactly 1/0 at the ends (MIX 0 % is bit-identical to the input). 30 ms smoothing per block + a per-sample ramp |
| DEPTH | Moves the excitation blend from "waveform" to "envelope": low band 0.22·(1−d)^1.3+0.02, thump 1.2+5d, energy 0.5+3.5d, tnoise 1+4d, sub 0.25d, direct 0.35·(1−d)^1.5. Swell attack 40 → 290 ms (attack suppression), thump LPF 70 → 35 Hz (rounder), +0.25d to BLUR (more smear). Not a pitch shift or EQ; it changes "what does the exciting" |
| TONE | Resonator bank fundamental = TONE's value (30–120 Hz, two octaves). Follows in the log domain over 0.3 s. The FDN's resonance emphasis follows it too. Widened because v0.1.0's 35–65 Hz could not be told apart by ear |
| DECAY | DECAY's value (0.3–25 s). The FDN's g_dec = 10^(−3·L/(fs·T)) (L = line length), so at FEEDBACK 0 % "one excitation falls −60 dB in this time". Tail output gain 2.5 × (1 − 0.45·n), where n is DECAY's position on a log scale, 0..1 (giving back a little of what builds up with long DECAY). The resonators' T60 is also set from n, at 0.35–0.6 s. From minimum to maximum DECAY the playing level moves about ±1 dB, and the tail after stopping +18 dB. DECAY alone has g < 1, so it never self-oscillates |
| MOTION | 28 irregular modulators (sample-and-hold at random intervals of 0.35–2.75 s through two one-pole smoothers). Per resonator: frequency ±2 %, level ("go… goro… guu"), decay ±30 %, placement. FDN line lengths ±3.5 %, the centre of the resonance emphasis, the per-line gain distribution. Speed = 0.25 + 1.5·motion. Every modulator is independent, so they do not move in the same direction |
| BLUR | Allpass coefficient 0.12 → 0.70, smear amount 0.8·blur², the spread of the resonators' placement |
| FEEDBACK | g = g_dec + (0.9995 − g_dec)·(1 − (1−fb)^1.3), resonance emphasis c = 1 − 0.03·fb. Very long at 90 %, almost infinite at 98 %, slowly self-sustaining at 100 % (test: at 100 % still sounding after a minute, at −18 dB) |
| DARK | Output LPF 1.6 kHz → 260 Hz, diffusion damping 1.1 kHz → 150 Hz, loop LPF 1.4 kHz → 220 Hz, loop clip drive 1 → 1.5, saturation drive after the resonators 1.15 → 1.75, lower weight for the higher modes, tnoise (transient excitation) −60 %, a lower thump LPF. The floor stops at 260 Hz, so the low end keeps its presence even at DARK 100 % |

Smoothing time constants (per block): MIX 30 ms (+ a per-sample ramp), DEPTH
100 ms, TONE 300 ms (log), DECAY 500 ms, MOTION 250 ms, BLUR 150 ms, FEEDBACK
200 ms, DARK 300 ms. The FDN's delay lengths are ramped per sample within the block.

## Stereo

- Lower resonator modes sit nearer the centre, higher ones further out (wobbling
  with MOTION)
- The diffusion network has different lengths left and right, which creates the
  width in the low mids to mids
- The output stage splits into M/S with a **2nd-order 85 Hz HPF on Side only**:
  30–80 Hz is nearly mono, 80–200 Hz moderately wide, and above that the width
  comes from the diffusion. Mono compatibility comes first

## Keeping the CPU down

- 8 mono resonators (stereo is only in the weights), 4 FDN lines, diffusion of 4
  stages + 4 taps per channel
- Filter coefficients, modulators and gain normalisation (40-point frequency
  response evaluation × 4 lines) are computed at a control rate of **once every
  128 samples**
- Delay lines are power-of-two ring buffers (wrapped with a mask)
- `-O2`, no `-ffast-math` (to keep the NaN/Inf guards), `-ffp-contract=off`
- On a desktop (x86-64), 22 µs per block on average (0.8 % of the 2902 µs budget),
  115 µs at worst

On a real Move (2026-10-20, v0.2.0, run at normal priority pinned to cores 0–2):

| Setting | Mean per block | Share of the 2902 µs budget | Worst |
|---|---|---|---|
| Defaults | 171 µs | 5.9 % | 1.0 ms |
| Every knob at maximum | 151 µs | 5.2 % | 1.0 ms |

The worst values include time when the benchmark, a normal-priority process, was
interrupted by Move's own threads (the design keeps the work nearly constant per
block). To measure again:

```bash
scp build/bench-device-arm64 ableton@move.local:/data/UserData/
```

```bash
ssh ableton@move.local "taskset 0x7 /data/UserData/bench-device-arm64"
```

## Realtime safety

- The audio callback side (`process_block` / `set_param` / `get_param`, all
  running on the SPI callback) does no malloc/free, new/delete, mutex, file I/O,
  syscalls, blocking or logging at all
- Instances are taken from a static pool (8 of them, about 460 KB each with every
  buffer inside the struct) with an atomic CAS, so `create_instance` allocates
  nothing either
- `check_abi.py` checks that the ARM64 .so references no
  malloc/calloc/free/new/delete/pthread_mutex

## Safety measures

| Measure | Where |
|---|---|
| A 20 Hz input HPF (2nd order) — keeps extreme DC and infrasound out of the loop (not cut as high as 80 Hz) | Engine input |
| A 2 Hz in-loop HPF — prevents DC build-up | Each FDN line |
| A structural limit on loop gain: the peak gain of each line's filter chain (LPF × HPF × resonance emphasis) is evaluated numerically every block and normalised so that the best-passing frequency gets exactly g (≤ 0.9995). Hadamard is orthogonal and the soft clip has a slope ≤ 1 → the loop cannot amplify in any setting | FDN |
| A soft clip (rational function, slope ≤ 1, saturating at ±1) permanently in the loop | FDN, after the resonators |
| Governor: lowers the gain only when the loop RMS exceeds −9 dBFS (down over 80 ms, back over 2 s). It does not act on normal material | FDN |
| A 22 Hz output HPF (2nd order) — subsonic / DC | Output stage |
| NaN/Inf guard: the output sum and the main states are checked every block; if non-finite, the internal state is rebuilt from silence and that block outputs the dry (the count can be read as `nan_resets`) | Output stage |
| Catastrophic-peak protection: only what exceeds −0.9 dBFS is brought within ±1.0 by a soft knee (normal sound is untouched; not a brick wall) | Output stage |
| Denormal protection: filter states below 1e-15 are zeroed every block | Every stage |
| Parameter strings: non-finite values are ignored, out-of-range values clamped. State JSON is applied only when every key is present and valid | Plugin |

## Saved state

`state` saves MIX DEPTH TONE DECAY MOTION BLUR FEEDBACK DARK as JSON (for example
`{"version":2,"mix":0.31,"depth":0.12,"tone":97,"decay":12.5,...,"dark":0.2}`). The
states of the delay lines and resonators are not saved. States from 0.1.x
(`"version":1`, TONE/DECAY as 0..1 knob positions, with `sink`) can also be read:
TONE/DECAY are converted to Hz / seconds and `sink` is ignored. After a load the
inside is rebuilt from silence and the knobs go to the saved values without
smoothing. A state with missing or broken keys is rejected as a whole.

## ARM64 build result / ABI dependencies

```bash
./scripts/build.sh arm64
```

This produces `dist/submerge/` and `dist/submerge-module.tar.gz`.

```text
PASS: AArch64, only move_audio_fx_init_v2 exported,
      NEEDED ['libm.so.6', 'libc.so.6', 'ld-linux-aarch64.so.1'] max GLIBC 2.27
      - no malloc/new/mutex imported
submerge.so: ELF 64-bit LSB shared object, ARM aarch64, 73 KB (text 26 KB, bss 3.6 MB = pool of 8)
```

- ABI: Schwung **Audio FX API v2** (`move_audio_fx_init_v2` → `audio_fx_api_v2_t`).
  Only `sample_rate` is read from `host_api_v1_t` (44100 / 48000 are accepted;
  anything else returns NULL and refuses the load)
- Dependencies: libc / libm only. GLIBC 2.27 or older
- `ui_hierarchy` declares one page of 8 knobs, and there is no module-specific
  screen (`ui_chain.js`). Schwung 1.7.3 does not open the screen of a module
  without `ui_hierarchy` at Master FX / Send / bus positions, so this is the
  condition for Master FX support
- Read-only diagnostic keys: `version` `nan_resets` `governor`

## Installing

```bash
python3 scripts/install.py http://move.local:7700
```

This uploads through Schwung Manager's `install-custom`. Restart Move after
installing.

## Tests

```bash
./scripts/test.sh
```

| Test | What | Result |
|---|---|---|
| Dry | At MIX 0 % output = input (bit-identical in int16). Also right after restoring a saved state, and 1 s after turning MIX to 0 | Identical (0 samples different) |
| Wet | At MIX 100 %, the component of the input itself in the output (least-squares gain above 250 Hz, 0–5 ms delay) | DEPTH 0: noise 0.0002 / drums 0.017, DEPTH 100: 0.00005 / 0.002 (criterion < 0.03) |
| Stability | 10 minutes with every knob at maximum (drums, sine, pink, full-scale white noise and silence in turn) | 0 NaN resets, governor finite, decays in the silence afterwards |
| Silence | 60 s of silence into a new instance (everything 100 %) → output exactly 0. In the silence after a burst, no 5 s window is larger than the one before | Output 0, at most −0.5 dB change per window (only falling), still sounding after a minute at −18 dB |
| Impulse | An impulse with defaults → there is a tail, it decays, 98 % below 200 Hz | OK |
| Sine | 40 / 60 / 100 / 440 Hz for 40 s with everything at 100 % → steady (difference between 10 s windows < 2 dB), decays when stopped | Differences −0.7 to +0.2 dB, −20 dB after stopping |
| Noise | 60 s of pink / white noise with everything at 100 % → no runaway | Change over the last 15 s −0.2 / +0.06 dB |
| Level kept | Playing level difference < 4 dB over DECAY 0.3–25 s / BLUR 0–100 % (guards against v0.1.0's bug returning) | 0.9 dB / 1.5 dB |
| Zipper | Moving each of the 8 knobs instantly between minimum and maximum keeps the largest sample-to-sample difference below 3 times the steady state | 1.0 to 1.4 times |
| State | Round trip of every knob, reading the 0.1.x format (converting TONE/DECAY), rejecting incomplete states, handling of NaN and out-of-range, the instance pool | OK |
| NaN guard | NaN injected into the loop → one reset, output stays finite | OK |
| ASan / UBSan | All of the above with sanitizers | OK |
| Grid | With the same Schwung page engine as the device: one page with the 8 knobs in order, each knob writes its own key within range, display (`49 Hz` / `3.4 sec` / `70%`), no custom screen and no SINK | OK |
| ABI / licence | Check of the ARM64 .so, no copyleft mixed in, a copyright notice on every original file | OK |

The grid test uses the page engine from a copy of the device's `shared/`
(`build/device-shared/`, otherwise `../schwung`).

## Offline listening

```bash
./scripts/render_demo.sh
```

This writes the three presets `default` / `deep` / `shallow`. Any setting can be
given, like `./scripts/render_demo.sh mine depth=1 dark=0.9 feedback=0.95`.

In `build/renders/<preset>/` it writes dry / processed WAVs (44.1 kHz 16-bit,
14 s, MIX 100 %) for a kick, a snare, a guitar-like chord and a noise burst, plus
PNGs of the level envelope and a 0–600 Hz spectrogram. Everything is synthetic
(no sample material is used).

Measured with the defaults (MIX 100 %):

| Material | Input RMS | Wet RMS | Wet below 200 Hz | Wet above 1 kHz |
|---|---|---|---|---|
| Kick | 0.148 | 0.111 | 99 % | 0.0 % |
| Snare (54 % of the input above 1 kHz) | 0.042 | 0.028 | 99 % | 0.0 % |
| Chord (49 % of the input above 1 kHz) | 0.057 | 0.038 | 99 % | 0.0 % |
| Noise burst (95 % of the input above 1 kHz) | 0.074 | 0.034 | 99 % | 0.0 % |

In the spectrograms the kick's individual attacks vanish into a continuous
low-frequency field, and the envelope wobbles irregularly. In deep (DEPTH 100 /
FEEDBACK 80 / DARK 70) the chord's strums vanish and a dark 40–60 Hz field
remains, heaving, for over 8 s.

## Known issues and open questions

- The sound in Master FX has not yet been heard on the hardware (applied deeply
  to a whole mix the low end gets very large, so start with a low MIX)
- The TONE / DECAY knobs are linear in Hz / seconds (the standard grid has no way
  to declare a log curve). DECAY feels cramped at the short end
- CPU on Move is 5–6 % on average (bench). Check the real load inside the SPI
  thread on Schwung's CPU page
- Left near FEEDBACK 100 % for a long time, the field converges on the resonant
  band (around the third mode), and with low MOTION becomes a sustained tone
  close to a low hum. Raising MOTION makes it waver
- Feeding a kick at DEPTH 0 % can make the wet peak larger than the input (peak
  0.92, just short of the safety knee)
- With every knob at 100 % and full-scale white noise or a sine fed for a long
  time, the output hits the safety knee and sits near 0 dBFS (no clipping, no NaN)
- `ld-linux-aarch64.so.1` appears in NEEDED (from the toolchain; the same in
  existing modules)
- The excitation noise always starts from the same random seed, so the first few
  hits right after loading always have the same wobble

## Changes

- **0.2.0** Removed SINK, dropped the module's own screen (`ui_chain.js` /
  `ui_core.mjs`) for the standard grid. It returns `ui_hierarchy`, so the screen
  opens in Master FX / Send / buses too. TONE holds Hz and DECAY seconds as their
  values (0.1.x states are converted on load)
- **0.1.1** (after feedback on the hardware) Fixed sound disappearing when DECAY /
  BLUR were raised (resonator T60 decoupled from DECAY, tail level raised, smear at
  1/√N). TONE widened to 30–120 Hz. SINK now holds the loop first and then turns
  the input down (pressing it used to go silent)
- **0.1.0** First version

## Licence

MIT License — Copyright (c) 2026 Hiro Ikezawa. See `LICENSE`. The only bundled
external code is Schwung's plugin API headers (MIT, Charles Vestal); their notice
is in `THIRD_PARTY_LICENSES.md` and `third_party/schwung/LICENSE`. No GPL / LGPL /
AGPL code is linked or copied.
