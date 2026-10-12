/* SUBMERGE UI test: ui_core.mjs driving Schwung's REAL page controller and
 * input decoder (from a copy of the device's shared/ or a Schwung checkout),
 * drawn through Schwung's framebuffer harness, against a fake param store
 * serving the built DSP's real contract (tests/dump_contract.py).
 *
 *   SM_SHARED     a shared/ directory to take the page engine from (e.g. one
 *                 mirrored from the device); default <schwung>/src/shared
 *   SCHWUNG_ROOT  a Schwung checkout (for tools/param-pages/harness.mjs);
 *                 default ../schwung beside this repository
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE). */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = process.env.SCHWUNG_ROOT || path.resolve(here, "../../schwung");
const shared = process.env.SM_SHARED || path.join(root, "src/shared");
const load = (p) => import(pathToFileURL(p).href);
const { createController } = await load(path.join(shared, "param_pages/page_controller.mjs"));
const { decodeInput, applyInput } = await load(path.join(shared, "param_pages/page_input.mjs"));
const { createFramebuffer, drawContext } = await load(path.join(root, "tools/param-pages/harness.mjs"));
const { createUi, MAIN_KEYS, SINK_PAD, LED, STATUS_EVERY_TICKS, formatValue, toneHz, decaySeconds } =
    await load(path.join(here, "../ui_core.mjs"));

let failures = 0;
function check(cond, msg) {
    if (!cond) { failures++; console.error("FAIL:", msg); }
    else console.log("ok  ", msg.split(":")[0]);
}

const contract = JSON.parse(fs.readFileSync(path.join(here, "../build/native/contract.json"), "utf8"));
const manifest = JSON.parse(fs.readFileSync(path.join(here, "../module.json"), "utf8"));
const values = { ...contract.values };
const writes = [];
const reads = [];
let shown = true, padBlocked = false;
const leds = {};
const store = {
    get(key) {
        reads.push(key);
        if (key === "ui_hierarchy") return contract.ui_hierarchy;
        if (key === "grid_hierarchy") return contract.grid_hierarchy;
        if (key === "chain_params") return contract.chain_params;
        if (key in values) return values[key];
        return "";
    },
    set(key, value) {
        writes.push([key, String(value)]);
        if (!(key in values)) return true;
        /* as submerge_plugin.c: SINK takes OFF/ON or a number, reports the name */
        if (key === "sink") {
            const s = String(value).toUpperCase();
            values.sink = s === "ON" || Number(s) >= 0.5 ? "ON" : "OFF";
        } else {
            values[key] = Math.max(0, Math.min(1, Number(value))).toFixed(4);
        }
        return true;
    },
};

let clock = 1000;
const fb = createFramebuffer();
const ctx = drawContext(fb);
const ui = createUi({
    createController, decodeInput, applyInput,
    getParam: (k) => store.get(k),
    setParam: (k, v) => store.set(k, v),
    draw: ctx,
    clear: () => fb.clearScreen(),
    layout: () => "movy",
    now: () => clock,
    displayOn: () => shown,
    padBlock: (on) => { padBlocked = on; },
    sendLed: (n, col) => { leds[n] = col; return true; },
    padSnapshot: () => ({ "68": 5, "99": 7 }),
});
function ticks(n) { for (let i = 0; i < n; i++) { clock += 17; ui.tick(); } }
const bare = (k) => k.slice(k.indexOf(":") + 1);
const shot = (name) => {
    const dir = path.join(here, "../build/screens");
    fs.mkdirSync(dir, { recursive: true });
    if (fb.toPng) fs.writeFileSync(path.join(dir, name + ".png"), fb.toPng(4));
    else if (fb.writePng) fb.writePng(path.join(dir, name + ".png"));
};

ui.init();
ticks(40);
const c = ui.state.controller;
check(JSON.stringify(c.page.keys.map(bare)) === JSON.stringify(MAIN_KEYS),
      "one page = MIX DEPTH TONE DECAY MOTION BLUR FEEDBACK DARK: " + c.page.keys);
check(bare(c.page.keys[0]) === "mix", "knob 1 is MIX");
check(!reads.includes("ui_hierarchy"), "the grid reads grid_hierarchy, never the empty ui_hierarchy");
check(fb.countLit() > 200, "the page draws: " + fb.countLit());
check(ui.title() === "SUBMERGE", "title: " + ui.title());
console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());
shot("main");

/* ---- every knob writes its own key ----------------------------------------- */
for (let k = 0; k < 8; k++) {
    const w0 = writes.length;
    for (let i = 0; i < 5; i++) { ui.onMidi([0xb0, 71 + k, 1]); ticks(1); }
    ticks(10);
    check(writes.slice(w0).some(([key]) => key === MAIN_KEYS[k]), `knob ${k + 1} = ${MAIN_KEYS[k]}: ` + writes.slice(w0));
}
ui.onMidi([0x90, 2, 127]); ticks(4);                                      /* touch knob 3 = TONE */
shot("touch_tone");
ui.onMidi([0x80, 2, 0]); ticks(4);
ui.onMidi([0x90, 3, 127]); ticks(4);                                      /* touch knob 4 = DECAY */
shot("touch_decay");
ui.onMidi([0x80, 3, 0]); ticks(4);

/* ---- the display units agree with the DSP ----------------------------------- */
let agree = true;
for (const [v, hz] of Object.entries(contract.tone_hz)) if (Math.abs(toneHz(Number(v)) - hz) > 0.01) agree = false;
for (const [v, s] of Object.entries(contract.decay_s)) if (Math.abs(decaySeconds(Number(v)) - s) > 0.01) agree = false;
check(agree, "TONE Hz / DECAY s mappings match the DSP's at 0, 25, 50, 75, 100 %");
check(formatValue("synth:tone", "0") === "30 Hz" && formatValue("synth:tone", "1") === "120 Hz", "TONE reads in Hz");
check(formatValue("synth:decay", "0") === "0.3 s" && formatValue("synth:decay", "1") === "25 s"
      && formatValue("synth:decay", "0.75") === "8.3 s", "DECAY reads in seconds");
check(formatValue("synth:feedback", "0.96") === "96%" && formatValue("synth:mix", "0") === "0%", "the rest read in %");

/* ---- SINK pad ---------------------------------------------------------------- */
check(padBlocked === true, "pads taken while the screen is up");
ticks(8);
check(leds[SINK_PAD] === LED.SINK_OFF && leds[68] === LED.OFF, "SINK pad dim violet, others dark");
ui.onMidi([0x90, SINK_PAD, 100]);
check(values.sink === "ON" && leds[SINK_PAD] === LED.SINK_ON, "SINK pad toggles on, bright violet");
ui.onMidi([0x80, SINK_PAD, 0]);
ticks(STATUS_EVERY_TICKS * 2);
check(ui.title() === "SINK", "header reads SINK: " + ui.title());
console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());
shot("sink");
const wf = writes.length;
ui.onMidi([0x90, 70, 100]); ui.onMidi([0x80, 70, 0]);
check(writes.length === wf, "other pads do nothing");
ui.onMidi([0x90, SINK_PAD, 100]); ui.onMidi([0x80, SINK_PAD, 0]);
ticks(STATUS_EVERY_TICKS * 2);
check(values.sink === "OFF" && ui.title() === "SUBMERGE" && leds[SINK_PAD] === LED.SINK_OFF, "SINK pad toggles back off");
values.sink = "ON";                                     /* restored from a saved state */
ticks(STATUS_EVERY_TICKS * 2 + 32);
check(ui.title() === "SINK" && leds[SINK_PAD] === LED.SINK_ON, "a DSP-side SINK (state restore) reaches pad and header");

/* ---- leaving gives the pads back ------------------------------------------ */
shown = false;
ticks(2);
check(padBlocked === false && leds[99] === 7 && leds[68] === 5, "pads and Move's colours handed back");

/* ---- contract agreement ----------------------------------------------------- */
const gh = JSON.parse(contract.grid_hierarchy);
check(gh.levels.root.knobs.every((k) => manifest.capabilities.chain_params.some((p) => p.key === k)),
      "every grid key is in chain_params");

if (failures) { console.error(`${failures} UI FAILURE(S)`); process.exit(1); }
console.log("ALL UI TESTS PASSED");
