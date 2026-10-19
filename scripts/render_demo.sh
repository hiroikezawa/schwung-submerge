#!/usr/bin/env bash
# Offline listening: render kick / snare / chord / noise-burst through
# SUBMERGE on the desktop and write WAVs (dry + processed) and PNGs (level
# envelopes + 0-600 Hz spectrogram) to build/renders/<preset>/.
#
#   scripts/render_demo.sh            three presets: default, deep, shallow
#   scripts/render_demo.sh name k=v   one custom preset, e.g. depth=1 dark=0.8
#
# Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$MODULE/build/renders"
mkdir -p "$OUT" "$MODULE/build/generated"
python3 "$MODULE/scripts/metadata.py" "$MODULE/build/generated"
"${CC:-gcc}" -std=gnu11 -O2 -ffp-contract=off -I"$MODULE/dsp" -I"$MODULE/build/generated" \
  "$MODULE/tests/render_demo.c" "$MODULE"/dsp/submerge_{engine,resonator,diffusion,feedback}.c \
  -lm -o "$MODULE/build/render_demo"
"${CC:-gcc}" -O2 "$MODULE/tests/spectrogram.c" -lm -o "$MODULE/build/spectrogram"

render() {  # render <name> [k=v ...]
  local name=$1; shift
  mkdir -p "$OUT/$name"
  echo "== $name $*"
  "$MODULE/build/render_demo" "$OUT/$name" "$@"
  for n in 1_kick 2_snare 3_chord 4_noise; do
    "$MODULE/build/spectrogram" "$OUT/$name/${n}_dry.wav" "$OUT/$name/${n}_submerge.wav" "$OUT/$name/$n.png"
  done
}
if [[ $# -gt 0 ]]; then
  render "$@"
else
  render default
  render deep depth=1 decay=8.3 feedback=0.8 dark=0.7 motion=0.6 blur=0.8
  render shallow depth=0 dark=0.2 decay=1.7
fi
echo "WAVs and PNGs in $OUT"
