/*
 * SUBMERGE - Signal Chain UI.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * The shadow UI opens this (instead of its stock knob grid) because the DSP
 * answers `ui_hierarchy` with an empty string. It draws the same grid with
 * Schwung's own page engine and takes the pads for the SINK pad; all of
 * that lives in ui_core.mjs. This file only binds Schwung's globals, resolved
 * at CALL time because host_module_get/set_param are per-slot shims the
 * shadow UI installs around this module.
 */
import { createUi } from './ui_core.mjs';
import { createController, LAYOUT_MOVY, LAYOUT_LIST }
    from '/data/UserData/schwung/shared/param_pages/page_controller.mjs';
import { decodeInput, applyInput }
    from '/data/UserData/schwung/shared/param_pages/page_input.mjs';

const has = (name) => typeof globalThis[name] === "function";

/* Same rule as the stock grid (shadow_ui_param_pages.mjs paramPagesLayout):
 * the screen reader forces the list, otherwise Param View decides. */
const PARAM_VIEW_LIST = 0, PARAM_VIEW_KNOBS = 1;
function layout() {
    if (has("tts_get_enabled") && tts_get_enabled()) return LAYOUT_LIST;
    const mode = has("param_view_get_mode") ? param_view_get_mode() : PARAM_VIEW_LIST;
    return mode === PARAM_VIEW_KNOBS ? LAYOUT_MOVY : LAYOUT_LIST;
}

const core = createUi({
    createController, decodeInput, applyInput,
    getParam: (key) => has("host_module_get_param") ? host_module_get_param(key) : null,
    setParam: (key, value) => has("host_module_set_param")
        ? host_module_set_param(key, String(value)) !== false : false,
    /* As screen_reader.mjs announce(): only while the shadow UI is shown. */
    announce: (text) => {
        if (!text || !has("host_send_screenreader")) return;
        if (has("shadow_get_display_mode") && shadow_get_display_mode() !== 1) return;
        host_send_screenreader(text);
    },
    layout,
    draw: {
        fillRect: (...a) => fill_rect(...a),
        print: (...a) => print(...a),
        textWidth: (t) => has("text_width") ? text_width(t) : String(t).length * 5,
        line: has("draw_line") ? draw_line : undefined,
        fillCircle: has("fill_circle") ? fill_circle : undefined,
        drawCircle: has("draw_circle") ? draw_circle : undefined,
        drawArc: has("draw_arc") ? draw_arc : undefined,
    },
    clear: () => clear_screen(),
    trailingMenus: () => has("shadow_component_trailing_menus")
        ? shadow_component_trailing_menus() : [],
    runAction: (action) => has("shadow_component_run_action")
        ? shadow_component_run_action(action) : false,
    now: () => Date.now(),
    shiftHeld: () => has("shadow_get_shift_held") && shadow_get_shift_held() !== 0,
    displayOn: () => !has("shadow_get_display_mode") || shadow_get_display_mode() === 1,
    padBlock: (on) => { if (has("host_pad_block")) host_pad_block(on ? 1 : 0); },
    sendLed: (note, color) => has("move_midi_internal_send")
        ? move_midi_internal_send([0x09, 0x90, note, color]) !== false : false,
    padSnapshot: () => has("shadow_get_pad_led_snapshot") ? shadow_get_pad_led_snapshot() : null,
});

globalThis.chain_ui = {
    init: core.init,
    tick: core.tick,
    onMidiMessageInternal: core.onMidi,
    handleBack: () => {
        const consumed = core.handleBack();
        if (!consumed) core.releaseAll();
        return consumed;
    },
};
