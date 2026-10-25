// Default global run uses a private, never-switched desktop and no OS input.
// Real mouse/touch injections require the separate explicit --desktop-input flag.
export function getMonitorInputAdapter(){return {
 id:'touch.monitor-drag-pass',domain:'touch',title:'Production monitor bounded native mouse/touch without desktop capture',
 kind:'node',path:'tools/ymcc_sim/global/monitor-input/monitor-input-consumer.mjs',args:['--evidence-dir','{outDir}/native-monitor-input'],timeoutMs:120000,
 profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['native-monitor-input/verification.json','native-monitor-input/native-result.json'],
 evidenceInputs:['tools/ymcc_sim/global/monitor-input/monitor-bounded-input-consumer.cpp','native/monitor_hud_panel_input.h','tools/ymcc_sim/global/monitor-input/monitor-input-consumer.cpp','tools/ymcc_sim/global/monitor-input/adapter.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','native/monitor_desktop_hud.h','native/monitor_hud_drag.h','native/screen_touchpad_input_surface.h','native/monitor_hud_input_policy.h','native/monitor_hud_placement.h','native/monitor_hud_serialization.h','native/monitor_hud_visibility.h','native/monitor_hud_recovery.h','native/touch_haptic_dispatch.h','deps/json/json.hpp']
};}

export function getMonitorFeedbackAdapter(){return {
 id:"touch.monitor-feedback-cleanup",domain:"touch",title:"Actual HUD entry clears drag and hold feedback, compact template notice",kind:"node",path:"tools/ymcc_sim/global/monitor-input/feedback-consumer.mjs",args:["--evidence-dir","{outDir}/monitor-feedback"],timeoutMs:90000,profiles:["desktop","full"],scope:"behavioral-mock",resultFiles:["monitor-feedback/verification.json"],evidenceInputs:["src/bridge/ipc.ts","tools/ymcc_sim/global/monitor-input/feedback-consumer.mjs","tools/ymcc_sim/global/monitor-input/adapter.mjs","tools/ymcc_sim/global/monitor-input/notice-lifecycle-consumer.mjs","src/monitorNoticeOutputEntry.ts","src/bridge/monitorHudLayoutLane.ts","src/monitorHudOutputEntry.ts","src/bridge/monitorNoticeHost.ts","src/components/MonitorShortcutNotice.vue","src/styles/monitorHudOutput.css"]
};}
