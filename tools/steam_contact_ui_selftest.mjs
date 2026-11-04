import assert from 'node:assert/strict';
import fs from 'node:fs';
const read=file=>fs.readFileSync(file,'utf8');
const panel=read('src/components/SteamRecoveryDiagnostics.vue');
const settings=read('src/views/SettingsView.vue');
const steam=read('src/views/SteamView.vue');
const sleep=read('src/views/SleepGuardView.vue');
// The 2026-10-22 user contract retired this entire card from Settings.
// Keep the retained component's read-only assertions below; a dormant component
// is not evidence that it should be mounted again.
assert.ok(/#diagnostics="\{ active \}"/.test(settings), 'Settings keeps its diagnostics slot');
function assertRetiredMountsAbsent(source, label) {
  assert.equal(/\b(?:SteamRecoveryDiagnostics|SleepRecoveryPanel)\b/.test(source), false,
    `${label}: retired Steam repair UI must remain unmounted`);
}
for (const [label, source] of [['Settings', settings], ['Steam', steam], ['Sleep', sleep]]) {
  assertRetiredMountsAbsent(source, label);
}
// Prove that stale reintroductions are still caught without rerunning a parent.
assert.throws(() => assertRetiredMountsAbsent(settings + '\n<SteamRecoveryDiagnostics v-if="active" />', 'Settings'), /must remain unmounted/);
assert.throws(() => assertRetiredMountsAbsent(settings + '\n<SleepRecoveryPanel />', 'Settings'), /must remain unmounted/);
assert.match(panel, /非 Steam 游戏修复/);
assert.match(panel, /<SleepRecoveryPanel/);
assert.match(panel, /getSteamGameLink/);
assert.match(panel, /Steam 两端联系（只读）/);
assert.match(panel, /联系状态不代表游戏内手柄/);
assert.match(panel, /旧联系不会继续沿用/);
assert.match(panel, /role="status" aria-live="polite"/);
assert.doesNotMatch(steam, /steam-contact-card|refreshSteamGameLink|SleepRecoveryPanel|SteamRecoveryDiagnostics/);
assert.doesNotMatch(sleep, /SleepRecoveryPanel|SteamRecoveryDiagnostics/);
const area=panel.slice(panel.indexOf('<div class="recovery-link-states"'),panel.indexOf('<p class="recovery-note"'));
assert.doesNotMatch(area, /<button|<input|<select|@click=|gameInputVerified|continueButtonVerified/);
assert.doesNotMatch(panel, /inspectSleepRecovery|repairSleepRecovery|launchSteam|switchMode|steamStop/);
assert.match(read('src/bridge/steamGameLink.ts'), /transportConnected: boolean/);
console.log('PASS: retired repair UI remains unmounted; retained contact facts stay read-only and independent of actual-input/Continue evidence; two stale-mount controls rejected');
