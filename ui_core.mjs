/*
 * SUBMERGE - Signal Chain UI core.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * The knob screen is Schwung's OWN page engine (page_controller.mjs) - the
 * same grid, header, value cards and trailing pages (My Presets, Module)
 * every other module shows. It is driven from here rather than by the
 * shadow UI only because the SINK pad needs the pads, and the shadow UI's
 * stock grid runs no module code. So the DSP serves `ui_hierarchy` EMPTY (the
 * shadow UI then opens ui_chain.js) and the real hierarchy as
 * `grid_hierarchy`.
 *
 * One page, the eight knobs in order:
 *   MIX DEPTH TONE DECAY MOTION BLUR FEEDBACK DARK
 * TONE reads in Hz (the lowest resonator), DECAY in seconds, the rest in %.
 *
 * SINK: the top-right pad (99), a toggle. Bright violet while on, a dim
 * violet otherwise; the header reads SINK while it is on. The other pads are
 * held (host_pad_block) while this screen is up and do nothing.
 *
 * Every dependency is injected (ui_chain.js binds the device, the tests bind
 * fakes), so nothing here touches a global.
 */

export const MAIN_KEYS = ["mix", "depth", "tone", "decay", "motion", "blur", "feedback", "dark"];

export const SINK_PAD = 99;
export const PAD_FIRST = 68, PAD_LAST = 99;
export const LED = { SINK_ON: 19 /* Violet */, SINK_OFF: 102 /* VeryDarkViolet */, OFF: 0 };
export const STATUS_EVERY_TICKS = 6;     /* ~10 Hz: one IPC read costs ~2.8 ms */
const LEDS_PER_TICK = 8;                 /* 32 pads repainted every 4 frames */

/* The same two mappings as dsp/submerge_params.h (tests/test_ui.mjs pins
 * them against values computed by the built DSP). */
export const toneHz = (t) => 35 * Math.pow(65 / 35, t);
export const decaySeconds = (d) => 0.3 * Math.pow(25 / 0.3, d);

export function formatValue(fullKey, raw) {
    const i = fullKey.indexOf(":");
    const k = i < 0 ? fullKey : fullKey.slice(i + 1);
    const v = Number(raw);
    if (!Number.isFinite(v) || !MAIN_KEYS.includes(k)) return null;
    if (k === "tone") return `${Math.round(toneHz(v))} Hz`;
    if (k === "decay") {
        const s = decaySeconds(v);
        return s < 10 ? `${s.toFixed(1)} s` : `${Math.round(s)} s`;
    }
    return `${Math.round(v * 100)}%`;
}

export function createUi(deps) {
    const {
        createController, decodeInput, applyInput,
        getParam, setParam, announce = () => {},
        layout = () => undefined, draw, clear,
        trailingMenus = null, runAction = null, now = () => Date.now(),
        shiftHeld = () => false,
        displayOn = () => true, padBlock = () => {},
        sendLed = () => {}, padSnapshot = () => null,
    } = deps;

    const st = {
        controller: null,
        shift: false,
        shiftCc: false,
        tick: 0,
        sink: false,
        visible: false,
        ledCursor: 0,
    };

    const bare = (fullKey) => {
        const i = fullKey.indexOf(":");
        return i < 0 ? fullKey : fullKey.slice(i + 1);
    };

    /* The controller asks for `<prefix>:<key>`; the host's per-slot shims add
     * the real prefix themselves. The hierarchy lives under grid_hierarchy. */
    function ioGet(fullKey) {
        const k = bare(fullKey);
        if (k === "ui_hierarchy") return getParam("grid_hierarchy");
        if (k.endsWith(":effective") || k.endsWith(":modulated")) return null;
        return getParam(k);
    }

    function ioSet(fullKey, value) {
        return setParam(bare(fullKey), value);
    }

    function pollSink() {
        const raw = getParam("sink");
        if (raw === "ON" || raw === "OFF") st.sink = raw === "ON";
    }

    function shiftNow() { return st.shiftCc || !!shiftHeld(); }

    function syncShift() {
        const next = shiftNow();
        if (next === st.shift) return;
        st.shift = next;
        if (st.controller) applyInput(st.controller, { type: "shift", down: next }, { nowMs: now(), reveal: true });
    }

    function init() {
        st.controller = createController({
            getParam: ioGet,
            setParam: ioSet,
            formatValue,
            announce,
            trailingMenus: trailingMenus ? () => trailingMenus() || [] : undefined,
            now,
        });
        st.controller.load({ slot: 0, component: "synth", prefix: "synth" });
        pollSink();
    }

    function title() {
        return st.sink ? "SINK" : "SUBMERGE";
    }

    function padColor(note) {
        if (note === SINK_PAD) return st.sink ? LED.SINK_ON : LED.SINK_OFF;
        return LED.OFF;
    }

    function paintPads() {
        for (let i = 0; i < LEDS_PER_TICK; ++i) {
            const note = PAD_FIRST + (st.ledCursor++ % 32);
            sendLed(note, padColor(note));
        }
    }

    function restorePads() {
        const snap = padSnapshot();
        for (let n = PAD_FIRST; n <= PAD_LAST; ++n) sendLed(n, (snap && snap[String(n)]) || 0);
    }

    function tick() {
        const c = st.controller;
        if (!c) return;
        if (!displayOn()) { if (st.visible) releaseAll(); return; }
        st.visible = true;
        padBlock(true);                   /* restated every frame: the shim drops it on its own */
        syncShift();
        if (++st.tick % STATUS_EVERY_TICKS === 0) pollSink();
        const l = layout();
        if (l) c.setLayout(l);
        c.tick();
        clear();
        c.render(draw, { title: title() });
        c.renderOverlays(draw, { clearScreen: clear });
        paintPads();
    }

    function toggleSink() {
        st.sink = !st.sink;
        setParam("sink", st.sink ? "1" : "0");
        sendLed(SINK_PAD, padColor(SINK_PAD));
        announce(st.sink ? "Sink on" : "Sink off");
    }

    function run(todo) {
        if (todo && todo.action === "menu" && todo.entry && todo.entry.action && runAction)
            return !!runAction(todo.entry.action);
        return false;
    }

    function onMidi(data) {
        const c = st.controller;
        if (!c || !data || data.length < 3 || !displayOn()) return;
        const status = data[0] & 0xf0;
        if ((status === 0x90 || status === 0x80) && data[1] >= PAD_FIRST && data[1] <= PAD_LAST) {
            if (data[1] === SINK_PAD && status === 0x90 && data[2] > 0) toggleSink();
            return;
        }
        if (status === 0xb0 && data[1] === 49) { st.shiftCc = data[2] > 0; syncShift(); return; }
        syncShift();
        const intent = decodeInput(data, { shift: st.shift });
        if (!intent) return;
        run(applyInput(c, intent, { nowMs: now(), reveal: st.shift }));
    }

    /* Back peels the grid's layers first, then leaves. */
    function handleBack() {
        const c = st.controller;
        if (!c) return false;
        const todo = applyInput(c, { type: "back" }, { nowMs: now() });
        return !(todo && todo.action === "exit");
    }

    function releaseAll() {
        restorePads();
        padBlock(false);
        st.visible = false;
    }

    return { init, tick, onMidi, handleBack, releaseAll, title, padColor, state: st };
}
