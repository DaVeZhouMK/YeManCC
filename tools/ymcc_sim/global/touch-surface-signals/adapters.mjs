/** Pure static registration. No IO, compilation, process or consumer import. */
export const TOUCH_SOURCE_INPUTS=Object.freeze([
 'src/bridge/screenTouchpads.ts','src/bridge/ipc.ts','tools/screen_touchpads_selftest.ts',
 'tools/screen_touchpads_event_time_selftest.cpp','tools/screen_touchpads_feedback_lease_selftest.cpp',
 'tools/screen_touchpad_partial_send_selftest.cpp','tools/screen_touchpad_partial_send_selftest.mjs',
 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs','deps/json/json.hpp',
 'native/controller_persona_settings.h','native/controller_persona.h','native/msi_dinput_acquire_recovery.h',
 'native/screen_button_overlay.h','native/screen_control_glyphs.h','native/screen_special_actions.h','native/screen_standalone_special.h',
 'native/screen_touchpad_auto_hide.h','native/screen_touchpad_contact_visual.h','native/screen_touchpad_cursor_visibility.h',
 'native/screen_touchpad_cursor.h','native/screen_touchpad_display_policy.h','native/screen_touchpad_input_surface.h',
 'native/screen_touchpad_owner.h','native/screen_touchpad_press_visual.h','native/screen_touchpad_visibility.h','native/screen_touchpads.h',
 'native/touch_dinput_fixture.h','native/touch_feedback_lease.h','native/touch_haptic_dispatch.h',
 'tools/ymcc_sim/global/input-signals/source-extract.mjs',
 'tools/ymcc_sim/global/touch-surface-signals/adapters.mjs','tools/ymcc_sim/global/touch-surface-signals/native-toolchain.mjs',
 'tools/ymcc_sim/global/touch-surface-signals/source-excerpts.mjs','tools/ymcc_sim/global/touch-surface-signals/touch-fixture.cpp',
 'tools/ymcc_sim/global/touch-surface-signals/legacy-event-time.cpp','tools/ymcc_sim/global/touch-surface-signals/touch-surface.mjs',
 'tools/ymcc_sim/global/touch-surface-signals/touch-surface.test.mjs',
]);
export function getTouchSurfaceAdapters(){return [{
 id:'controller.touch-surface',domain:'controller',title:'Formal touch surface ownership, feedback lease, ordering and lifecycle without OS input',
 kind:'node',path:'tools/ymcc_sim/global/touch-surface-signals/touch-surface.mjs',
 args:['--work-dir','{outDir}/touch-work','--output','{outDir}/touch-surface-result.json'],
 timeoutMs:180000,profiles:['desktop','full'],scope:'behavioral-mock',
 resultFiles:['touch-surface-result.json'],evidenceInputs:[...TOUCH_SOURCE_INPUTS],
}];}
