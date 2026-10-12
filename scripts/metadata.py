#!/usr/bin/env python3
"""Embed module.json's contract in the DSP and check it against the C table.

The runtime callbacks never read files, so chain_params and the version are
compiled in. The build fails if module.json and dsp/submerge_params.h
disagree on any key, order, range or default.

Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
"""
import json
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
meta = json.loads((root / "module.json").read_text())
cp = meta["capabilities"]["chain_params"]
params_h = (root / "dsp" / "submerge_params.h").read_text()
rows = re.findall(r'\{"(\w+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*([01])\}', params_h)
assert rows, "could not parse SM_PARAMS in submerge_params.h"
assert [p["key"] for p in cp] == [r[0] for r in rows], "chain_params order must match submerge_params.h"
for p, (key, lo, hi, de, integer) in zip(cp, rows):
    lo, hi, de = float(lo), float(hi), float(de)
    if p["type"] == "enum":
        assert len(p["options"]) == hi - lo + 1 and lo == 0, key
        assert p["options"].index(p["default"]) == de, key
        assert integer == "1", key
    else:
        assert (p["min"], p["max"], p["default"]) == (lo, hi, de), key
        assert integer == "0", key
assert meta["dsp"] == meta["id"] + ".so", "the chain host loads audio_fx/<id>/<id>.so"
assert meta["capabilities"]["chain_params"][0]["key"] == "mix", "knob 1 is MIX"
version = meta["version"]


def literal(value):
    return json.dumps(json.dumps(value, separators=(",", ":")), ensure_ascii=True)


out = pathlib.Path(sys.argv[1]) / "submerge_metadata.h"
out.write_text("\n".join([
    "/* Generated at build time from module.json; edit that instead. */",
    "static const char kChainParams[] = " + literal(cp) + ";",
    f'static const char kVersion[] = "{version}";',
]) + "\n")
