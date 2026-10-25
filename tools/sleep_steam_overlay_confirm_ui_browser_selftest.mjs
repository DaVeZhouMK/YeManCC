// Former confirmation UI suite is retired along with the feature.
import assert from 'node:assert/strict';
import fs from 'node:fs';
const source=fs.readFileSync('src/views/SleepGuardView.vue','utf8');
assert(!source.includes('onSteamOverlayOffFix'));
assert(!source.includes('steamOverlayConfirmOpen'));
assert(!source.includes('steamOverlayTriggerEl'));
assert(!source.includes('Steam大屏唤醒手柄卡死修复'));
const policy=fs.readFileSync('native/sleep_steam_preferences.h','utf8');
assert(policy.includes('kOverlayOffFixDeprecated=true'));
assert(policy.includes('kOverlayOffFixDefaultEnabled=false'));
const bridge=fs.readFileSync('src/bridge/yeman.ts','utf8');
assert(bridge.includes("if ('steamOverlayOffFix' in cfg) throw new Error"));
const runtime=fs.readFileSync('native/steam_settings_runtime.h','utf8');
assert(runtime.includes('if(args.contains("overlayOffFix"))return ymcc::sleepprefs::retiredOverlayReply()'));
console.log('PASS obsolete sleep Overlay-off UI/dialog removed and stale bridge/native setters fail closed');
await import('./sleep_recovery_ui_browser_selftest.mjs');
