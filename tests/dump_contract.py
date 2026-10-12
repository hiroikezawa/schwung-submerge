#!/usr/bin/env python3
"""Write what the built submerge.so serves to the UI (grid_hierarchy, the
values, and the TONE/DECAY display mappings at several knob positions) to
build/native/contract.json, so tests/test_ui.mjs runs against the DSP's real
answers rather than a copy.

Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
"""
import ctypes as c
import json
import pathlib
import sys

lib = c.CDLL(sys.argv[1])


class Host(c.Structure):
    _fields_ = [("api_version", c.c_uint32), ("sample_rate", c.c_int),
                ("frames_per_block", c.c_int), ("rest", c.c_void_p * 32)]


class API(c.Structure):
    _fields_ = [("version", c.c_uint32)] + [(k, c.c_void_p) for k in
               ("create", "destroy", "process", "set", "get", "midi")]


lib.move_audio_fx_init_v2.restype = c.POINTER(API)
api = lib.move_audio_fx_init_v2(c.byref(Host(1, 44100, 128))).contents
create = c.CFUNCTYPE(c.c_void_p, c.c_char_p, c.c_char_p)(api.create)
get = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_char_p, c.c_char_p, c.c_int)(api.get)
put = c.CFUNCTYPE(None, c.c_void_p, c.c_char_p, c.c_char_p)(api.set)
inst = create(b"", None)


def read(key):
    buf = c.create_string_buffer(8192)
    n = get(inst, key.encode(), buf, 8192)
    return None if n < 0 else buf.value.decode()


keys = ["mix", "depth", "tone", "decay", "motion", "blur", "feedback", "dark", "sink"]
out = {"grid_hierarchy": read("grid_hierarchy"), "ui_hierarchy": read("ui_hierarchy"),
       "chain_params": read("chain_params"), "values": {k: read(k) for k in keys},
       "tone_hz": {}, "decay_s": {}}
for v in ("0", "0.25", "0.5", "0.75", "1"):
    put(inst, b"tone", v.encode())
    put(inst, b"decay", v.encode())
    out["tone_hz"][v] = float(read("tone_hz"))
    out["decay_s"][v] = float(read("decay_s"))
json.loads(out["grid_hierarchy"])
assert out["ui_hierarchy"] == "", "ui_hierarchy must be served empty"
pathlib.Path(sys.argv[2]).write_text(json.dumps(out, indent=1))
print("contract dumped:", sys.argv[2])
