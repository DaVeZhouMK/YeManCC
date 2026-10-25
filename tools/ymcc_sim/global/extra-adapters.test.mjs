import assert from 'node:assert/strict';
import test from 'node:test';
import {getExtraAdapters} from './extra-adapters.mjs';
import {assertReviewedRegistryRow} from './registry-maintenance/assert-lists.mjs';

test('actual extra registry exposes the reviewed original TDP consumer exactly once',()=>{
 const rows=getExtraAdapters();
 const matches=rows.filter(row=>row.id==='performance.tdp-frontend-consumer');
 assert.equal(matches.length,1,'an import without the shared registry invocation must not count as integration');
 const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/tdp-consumer-signals/run.mjs');
 assert(row.evidenceInputs.includes('tools/tdp_frontend_static_selftest.ts'));
 assert(row.evidenceInputs.includes('src/bridge/yeman.ts'));
 assert.deepEqual(row.resultFiles,['tdp-consumer/result.json']);
 assert.equal(new Set(rows.map(row=>row.id)).size,rows.length);
});


test('actual extra registry binds both reviewed sleep consumers and the executed global completion inputs',()=>{
 const rows=getExtraAdapters();
 for(const [id,entry,original,required] of [
  ['sleep.recovery-ui-consumer','sleep-recovery-ui-consumer','tools/sleep_recovery_ui_browser_selftest.mjs','sleep-ui/result.json'],
  ['sleep.non-user-wake-consumer','sleep-non-user-wake-consumer','tools/sleep_non_user_wake_selftest.py','non-user-wake/result.json'],
 ]){
  const matches=rows.filter(row=>row.id===id);
  assert.equal(matches.length,1,'the public registry must invoke each actual consumer once');
  const row=matches[0];
  assert.equal(row.path,`tools/ymcc_sim/global/${entry}/run.mjs`);
  for(const source of [original,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source));
  assert.deepEqual(row.resultFiles,[required]);
  assert.equal(row.scope,'behavioral-mock');
  assert(row.profiles.includes('desktop')&&row.profiles.includes('full'));
  assert(row.artifactInputs.length>0&&row.artifactInputs.length<=640);
 }
 assert.equal(new Set(rows.map(row=>row.id)).size,rows.length);
});


test('actual extra registry invokes the archived v4 delivery consumer and preserves live sources as references',()=>{
 const rows=getExtraAdapters().filter(row=>row.id==='lifecycle.lighting-build-delivery');
 assert.equal(rows.length,1);const row=rows[0];
 assert.equal(row.path,'tools/ymcc_sim/global/lighting-component-continuity/v4/Run-BuildDelivery.ps1');
 assert.equal(row.kind,'powershell');assert.deepEqual(row.resultFiles,['p/result.json']);
 assert.equal(row.artifactInputs.length,1);assert(row.artifactInputs[0].endsWith('fixture.zip'));
 for(const input of ['tools/hc-device-runtime/New-LightingBuildDelivery.ps1','tools/hc-device-runtime/Test-LightingBuildDelivery.ps1','tools/hc-device-runtime/connect-installed-lighting.ps1','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(input));
 assert(row.referenceInputs.includes('native/main.cpp'));assert(!row.evidenceInputs.includes('native/main.cpp'));
});




test('single PID sleep consumer binds the maintained original script and identity boundaries once',()=>{
 const matches=getExtraAdapters().filter(row=>row.id==='sleep.game-pause-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/sleep-game-pause-consumer/run.mjs');assert.deepEqual(row.resultFiles,['game-pause/result.json']);
 for(const source of ['tools/sleep_game_pause_selftest.py','native/main.cpp','tools/ymcc_sim/global/sleep-game-pause-consumer/portable-boundary.h','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source));
 assert.equal(row.scope,'behavioral-mock');assert(row.artifactInputs.length>0&&row.artifactInputs.length<=640);
});


test('user-excluded generic UI remains outside the active capability registry',async()=>{
 const fs=await import('node:fs');
 const scope=JSON.parse(fs.readFileSync(new URL('../../../capabilities/USER-SCOPE-DECISIONS.json',import.meta.url),'utf8'));
 const entry=scope.entries.find(row=>row.id==='settings.wake-password-ui');
 assert.equal(entry?.status,'USER_EXCLUDED');
 assert.equal(entry.countsAsIntegrationGap,false);
 assert.equal(entry.countsTowardsCompletionDenominator,false);
 assert.equal(entry.automaticallyReimportOnUpgrade,false);
 assert.equal(entry.excludedSimulationCase,'sleep.wake-password-ui-consumer');
 assert(!getExtraAdapters().some(row=>row.id===entry.excludedSimulationCase));
});


test('raw Host output observation consumer is registered once with its actual production helper',()=>{
 const matches=getExtraAdapters().filter(row=>row.id==='controller.host-game-output-observation');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/controller-host-game-output-observation/run.mjs');assert.deepEqual(row.resultFiles,['output-observation/result.json']);
 for(const source of ['InputHost/Program.cs','InputHost/HostGameOutputObservation.cs','tools/inputhost_game_output_observation_selftest.mjs','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source));
 assert.equal(row.scope,'behavioral-mock');assert(row.artifactInputs.length>0&&row.artifactInputs.length<=640);
 assert(!getExtraAdapters().some(item=>item.id==='sleep.wake-password-ui-consumer'));
});


test('single PID freeze helper is registered once with actual fixture and portable boundary sources',()=>{
 const matches=getExtraAdapters().filter(row=>row.id==='sleep.single-pid-freeze-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/sleep-single-pid-freeze-consumer/run.mjs');assert.deepEqual(row.resultFiles,['single-pid-freeze/result.json']);
 for(const source of ['native/main.cpp','tools/ymcc_sim/global/sleep-single-pid-freeze-consumer/fixture.cpp.in','tools/ymcc_sim/global/sleep-game-pause-consumer/portable-boundary.h','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source));
 assert.equal(row.scope,'behavioral-mock');assert.equal(row.artifactInputs.length,572);
});


test('failed fan readback identity consumer invokes the unchanged original and error format contract',()=>{
 const matches=getExtraAdapters().filter(row=>row.id==='fan.failed-read-identity-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/fan-failed-read-identity-consumer/run.mjs');assert.deepEqual(row.resultFiles,['failed-read/result.json']);
 for(const source of ['tools/fan_failed_state_read_identity_selftest.ts','src/bridge/fanHost.ts','tools/ymcc_sim/global/fan-failed-read-identity-consumer/assert-format.json','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source));
 assert.equal(row.scope,'behavioral-mock');assert.equal(row.artifactInputs.length,58);
});


test('adopted fan start consumer binds original startup cancellation and shared actual runtime observation',()=>{
 const matches=getExtraAdapters().filter(row=>row.id==='fan.adopted-start-refusal-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/fan-adopted-start-refusal-consumer/run.mjs');assert.deepEqual(row.resultFiles,['adopted-start/result.json']);
 for(const source of ['tools/fan_adopted_start_refusal_selftest.ts','src/bridge/fanHost.ts','tools/ymcc_sim/global/fan-adopted-start-refusal-consumer/assert-format.json','tools/ymcc_sim/global/fan-failed-read-identity-consumer/observe-runtime.mjs','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source));
 assert.equal(row.scope,'behavioral-mock');assert.equal(row.artifactInputs.length,58);
});


test('marker ownership consumer binds the repaired real functions and immutable fault fixture once',()=>{
 const matches=getExtraAdapters().filter(row=>row.id==='sleep.marker-ownership-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/marker-ownership-consumer/run.mjs');assert.deepEqual(row.resultFiles,['marker-ownership/result.json']);
 for(const source of ['native/main.cpp','tools/ymcc_sim/global/marker-ownership-consumer/fixture.cpp.in','tools/ymcc_sim/global/marker-ownership-consumer/spec.json','tools/ymcc_sim/global/sleep-game-pause-consumer/portable-boundary.h','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source));
 assert.equal(row.evidenceInputs.length,28);assert.equal(row.scope,'behavioral-mock');assert.equal(row.artifactInputs.length,574);
});


test('fan Close refusal consumer binds actual public lifecycle and unchanged original case source once',()=>{
 const matches=getExtraAdapters().filter(row=>row.id==='fan.close-observation-refusal-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/fan-close-observation-refusal-consumer/run.mjs');assert.deepEqual(row.resultFiles,['close-observation/result.json']);
 for(const source of ['tools/fan_close_observation_refusal_selftest.ts','src/bridge/fanHost.ts','tools/ymcc_sim/global/fan-close-observation-refusal-consumer/spec.json','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source),source);
 assert.equal(row.evidenceInputs.length,78);assert.equal(row.scope,'behavioral-mock');assert.equal(row.artifactInputs.length,58);
});


test('lighting ordering consumer binds actual component and maintained shared helper once',()=>{
 const matches=getExtraAdapters().filter(row=>row.id==='lighting.ui-feedback-ordering-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/lighting-ui-feedback-ordering-consumer/run.mjs');assert.deepEqual(row.resultFiles,['feedback-ordering/result.json']);
 for(const source of ['src/components/ControllerFeedbackCapabilitiesCard.vue','tools/ymcc_sim/global/lighting-signals/ui-feedback.mjs','tools/controller_lighting_operations_browser_selftest.mjs','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source),source);
 assert.equal(row.evidenceInputs.length,26);assert.equal(row.artifactInputs.length,332);assert.equal(row.scope,'behavioral-mock');
});


test('nonzero motor private-pipe consumer binds independent actual24/25 source fixtures and complete1011 tool closure',()=>{
 const matches=getExtraAdapters().filter(row=>row.id==='controller.game-motor-private-pipe-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/controller-game-motor-pipe-consumer/run.mjs');assert.deepEqual(row.resultFiles,['game-motor-pipe/result.json']);
 for(const source of ['native/main.cpp','InputHost/Program.cs','InputHost/HostGameOutputObservation.cs','capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs','tools/fixtures/controller_game_motor_pipe/ManagedGameMotorFixture.cs','tools/fixtures/controller_game_motor_pipe/NativeMotorPipeTests.cpp','tools/fixtures/controller_game_motor_pipe/driver.mjs','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1'])assert(row.evidenceInputs.includes(source),source);
 assert(!row.evidenceInputs.includes('tools/input_lifecycle_managed_fixture.mjs'));assert(!row.evidenceInputs.includes('tools/ymcc_sim/global/controller-lifecycle-pipe/managed-fixture.json'));
 assert(row.evidenceInputs.includes('tools/ymcc_sim/global/controller-game-motor-pipe-consumer/owned-build-bridge.mjs'));assertReviewedRegistryRow(row);assert.equal(row.evidenceInputs.length,68);assert.equal(row.artifactInputs.length,1011);assert.equal(new Set(row.artifactInputs.map(p=>p.toLowerCase())).size,1011);for(const header of ['native/game_feedback_source_ticket.h','native/game_feedback_dispatch.h','native/touch_haptic_dispatch.h'])assert(row.evidenceInputs.includes(header));assert.equal(row.scope,'behavioral-mock');
});


test('fan explicit OFF chain binds unchanged original TS and actual HTTP Host engine consumers once',()=>{
 const matches=getExtraAdapters().filter(row=>row.id==='fan.explicit-off-host-chain-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/fan-explicit-off-host-chain-consumer/run.mjs');assert.deepEqual(row.resultFiles,['explicit-off/result.json','explicit-off/native-power-probe/result.json']);
 for(const source of ['tools/fan_explicit_off_host_chain_selftest.ts','tools/FanRepeatedSleepIntentSelfTest/Program.cs','src/bridge/fanHost.ts','src/bridge/fanApi.ts','FanLab/real-host/Program.cs','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source),source);
 for(const source of ['tools/fan_sta_dispatch_binding_selftest.cs','tools/fan_hc_sta_thread_identity_selftest.cs','tools/fan_cold_start_manual_continuation_selftest.ts','tools/ymcc_sim/global/fan-explicit-off-host-chain-consumer/PayloadBindingProbe.cs','tools/ymcc_sim/global/fan-explicit-off-host-chain-consumer/host-supplemental-probe.mjs'])assert(row.evidenceInputs.includes(source),source);assert.equal(row.evidenceInputs.length,143);assert.equal(row.artifactInputs.length,548);assert.equal(row.scope,'behavioral-mock');
});


test('directory resume consumer preserves original sixty cases and binds actual native70 plus live26 consumers once',()=>{
 const matches=getExtraAdapters().filter(row=>row.id==='sleep.directory-resume-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/sleep-directory-resume-consumer/run.mjs');assert.deepEqual(row.resultFiles,['directory-resume/result.json']);
 for(const source of ['native/main.cpp','deps/json/json.hpp','tools/ymcc_sim/global/sleep-directory-resume-consumer/fixture.cpp.in','tools/ymcc_sim/global/sleep-directory-resume-consumer/spec.json','tools/ymcc_sim/global/sleep-game-pause-consumer/portable-boundary.h','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source),source);
 for(const source of ['tools/ymcc_sim/global/sleep-directory-resume-consumer/live-rpc-main.cpp.in','tools/ymcc_sim/global/sleep-directory-resume-consumer/live-bridge-driver.mjs','src/bridge/gameproc.ts','src/components/GameQuickActions.vue'])assert(row.evidenceInputs.includes(source),source);assert.equal(row.evidenceInputs.length,34);assert.equal(row.artifactInputs.length,575);assert.equal(row.scope,'behavioral-mock');
});


test('source-bound game feedback writer is registered once with the complete current source and tool closure',()=>{
 const matches=getExtraAdapters().filter(row=>row.id==='controller.game-feedback-writer-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/controller-game-feedback-writer-consumer/run.mjs');assert.deepEqual(row.resultFiles,['writer-consumer/result.json']);
 for(const source of ['native/main.cpp','native/game_feedback_source_ticket.h','native/game_feedback_dispatch.h','native/game_feedback_diagnostics.h','capabilities/hc-controller-feedback-policy.json','capabilities/hc-device-catalog.json','tools/fixtures/controller_game_feedback_writer/CombinedMotorTests.cpp','tools/fixtures/controller_game_feedback_writer/driver.mjs','tools/fixtures/controller_game_feedback_writer/ManagedGameMotorFixture.cs','tools/ymcc_sim/global/runner.mjs'])assert(row.evidenceInputs.includes(source),source);
 for(const source of ['InputHost/PsEndpointOwnership.cs','InputHost/HostRuntimeBinaryIdentity.cs','tools/fixtures/controller_game_feedback_writer/ManagedEntry.cs','tools/fixtures/controller_game_feedback_writer/XboxManagedProtocolTests.cs','tools/fixtures/controller_game_feedback_writer/XboxProtocolControls.cpp','tools/fixtures/controller_game_feedback_writer/xbox-faults.mjs','capabilities/CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json'])assert(row.evidenceInputs.includes(source),source);
 assert(row.evidenceInputs.includes('tools/fixtures/controller_game_feedback_writer/FrameAdmissionControls.cpp'));assert(row.evidenceInputs.includes('tools/ymcc_sim/global/controller-game-motor-pipe-consumer/owned-build-bridge.mjs'));assert(row.evidenceInputs.includes('tools/fixtures/xinput_identity_feedback_context.mjs'));assertReviewedRegistryRow(row);assert.equal(row.evidenceInputs.length,107);assert.equal(row.artifactInputs.length,1020);assert.equal(new Set(row.artifactInputs.map(p=>p.toLowerCase())).size,1020);for(const name of ['vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','msvcp140_atomic_wait.dll','tbbmalloc.dll'])assert(row.artifactInputs.some(p=>p.replaceAll('\\','/').toLowerCase().endsWith('/'+name)),name);assert.equal(row.scope,'behavioral-mock');assert.equal(new Set(getExtraAdapters().map(row=>row.id)).size,getExtraAdapters().length);
});


test('battery actual Host roundtrip is registered once with unchanged original transport assertions and full compiler runtime inputs',()=>{
 const rows=getExtraAdapters(),matches=rows.filter(row=>row.id==='battery.native-host-roundtrip-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/battery-native-host-roundtrip-consumer/run.mjs');assert.deepEqual(row.resultFiles,['native-host-roundtrip/result.json']);
 for(const source of ['native/battery_charge_client.h','tools/battery_charge_transport_selftest.cpp','native/BatteryHost/Program.cs','native/BatteryHost/Controller.cs','tools/fixtures/battery_native_host_roundtrip/cases.json','tools/fixtures/battery_native_host_roundtrip/compile-managed.mjs','tools/fixtures/battery_native_host_roundtrip/toolchain-inputs.json','tools/ymcc_sim/global/host-process.ps1'])assert(row.evidenceInputs.includes(source),source);
 assertReviewedRegistryRow(row);assert.equal(row.evidenceInputs.length,57);assert.equal(row.artifactInputs.length,938);assert.equal(new Set(row.artifactInputs.map(p=>p.toLowerCase())).size,938);assert.equal(row.scope,'behavioral-mock');assert.equal(new Set(rows.map(r=>r.id)).size,rows.length);
 for(const name of ['vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','msvcp140_atomic_wait.dll','tbbmalloc.dll'])assert(row.artifactInputs.some(p=>p.replaceAll('\\','/').toLowerCase().endsWith('/'+name)),name);
});


test('startup and exit cleanup is registered once with actual production definitions and complete tools',()=>{
 const rows=getExtraAdapters(),matches=rows.filter(row=>row.id==='sleep.startup-exit-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/sleep-startup-exit-consumer/run.mjs');assert.deepEqual(row.resultFiles,['startup-exit/result.json']);
 for(const source of ['native/main.cpp','tools/ymcc_sim/global/sleep-startup-exit-consumer/spec.json','tools/ymcc_sim/global/sleep-startup-exit-consumer/fixture.cpp.in','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source),source);
 assert.equal(row.evidenceInputs.length,30);assert.equal(row.artifactInputs.length,574);assert.equal(row.scope,'behavioral-mock');assert.equal(new Set(rows.map(x=>x.id)).size,rows.length);
});

test('battery actual mounted UI native recovery is registered once without pruning the full tool closure',()=>{
 const rows=getExtraAdapters(),matches=rows.filter(row=>row.id==='battery.ui-native-recovery-consumer');assert.equal(matches.length,1);const row=matches[0];
 assert.equal(row.path,'tools/ymcc_sim/global/battery-ui-native-recovery-consumer/run.mjs');assert.deepEqual(row.resultFiles,['ui-native-recovery/result.json']);
 for(const source of ['src/components/BatteryChargeSettings.vue','src/bridge/batteryCharge.ts','native/battery_charge_client.h','native/BatteryHost/Controller.cs','tools/fixtures/battery_ui_native_recovery/ui-driver.mjs','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source),source);
 assertReviewedRegistryRow(row);assert.equal(row.scope,'behavioral-mock');assert.equal(new Set(rows.map(x=>x.id)).size,rows.length);
});


test('session marker file consumer registers actual stream lifecycle and real exit caller once without private source repair',()=>{
 const rows=getExtraAdapters(),matches=rows.filter(x=>x.id==='sleep.session-marker-file-consumer');assert.equal(matches.length,1);const row=matches[0];assert.equal(row.path,'tools/ymcc_sim/global/sleep-session-marker-file-consumer/run.mjs');assert.deepEqual(row.resultFiles,['session-marker-files/result.json']);
 for(const source of ['native/main.cpp','tools/ymcc_sim/global/sleep-session-marker-file-consumer/spec.json','tools/ymcc_sim/global/sleep-session-marker-file-consumer/scenario-plan.json','tools/ymcc_sim/global/sleep-session-marker-file-consumer/fixture.cpp.in','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source),source);
 assert.equal(row.evidenceInputs.length,29);assert.equal(row.artifactInputs.length,574);assert.equal(new Set(row.artifactInputs.map(x=>x.toLowerCase())).size,574);assert.equal(row.scope,'behavioral-mock');assert.equal(new Set(rows.map(x=>x.id)).size,rows.length);
});


test('nonblocking exit inflight consumer is registered once with actual global and thread fixture source',()=>{
 const rows=getExtraAdapters(),matches=rows.filter(x=>x.id==='sleep.exit-inflight-consumer');assert.equal(matches.length,1);const row=matches[0];assert.equal(row.path,'tools/ymcc_sim/global/sleep-exit-inflight-consumer/run.mjs');assert.deepEqual(row.resultFiles,['exit-inflight/result.json']);
 for(const source of ['native/main.cpp','tools/ymcc_sim/global/sleep-exit-inflight-consumer/spec.json','tools/ymcc_sim/global/sleep-exit-inflight-consumer/scenario-plan.json','tools/ymcc_sim/global/sleep-exit-inflight-consumer/fixture.cpp.in','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source),source);
 assert.equal(row.evidenceInputs.length,29);assert.equal(row.artifactInputs.length,574);assert.equal(new Set(row.artifactInputs.map(x=>x.toLowerCase())).size,574);assert.equal(row.scope,'behavioral-mock');assert.equal(new Set(rows.map(x=>x.id)).size,rows.length);
});


test('native radial mandatory two stages bind actual4 sources and full1191 tools once',()=>{
 const rows=getExtraAdapters(),matches=rows.filter(x=>x.id==='controller.radial-native-consumer');assert.equal(matches.length,1);const row=matches[0];assert.equal(row.kind,'powershell');assert.equal(row.path,'tools/ymcc_sim/global/radial-native-consumer/Run-Radial.ps1');assert.deepEqual(row.resultFiles,['radial-native/result.json','radial-native/post-create-result.json','radial-native/main-return/result.json','radial-native/wrapper-terminal.json']);
 for(const source of ['native/main.cpp','tools/ymcc_sim/global/radial-native-consumer/main-return/run.mjs','tools/ymcc_sim/global/radial-native-consumer/main-return/contract.json','native/radial_overlay.h','native/radial_input_region.h','deps/json/json.hpp','tools/ymcc_sim/global/radial-native-consumer/run.mjs','tools/ymcc_sim/global/radial-native-consumer/prepare.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source),source);
 assert.equal(row.evidenceInputs.length,24);assert.equal(row.artifactInputs.length,1191);assert.equal(new Set(row.artifactInputs.map(p=>p.toLowerCase())).size,1191);assert.equal(row.completionContract.rules[0].line,'RADIAL_NATIVE_CONSUMER_V2_COMPLETED postCreate=12+2 mainReturn=12+2');assert(row.evidenceInputs.includes('native/main.cpp'));assert.equal(new Set(rows.map(x=>x.id)).size,rows.length);
});


test('enabled MSI SD rear public consumer binds original scripts, all five faults and approved582 tools once',()=>{
 const rows=getExtraAdapters(),matches=rows.filter(row=>row.id==='controller.msi-sd-rear-consumer');assert.equal(matches.length,1);const row=matches[0];assert.equal(row.kind,'powershell');assert.equal(row.path,'tools/ymcc_sim/global/msi-sd-rear-consumer/Run-MsiSdRear.ps1');assert.equal(row.timeoutMs,480000);assert.equal(row.evidenceInputs.length,49);assert.equal(row.artifactInputs.length,582);assert.equal(new Set(row.artifactInputs.map(file=>file.toLowerCase())).size,582);assert.equal(row.resultFiles.length,9);
 for(const name of ['native/main.cpp','native/msi_native_rear_keyboard_runtime.h','tools/hc_msi_rear_consumers_selftest.mjs','tools/hc_msi_rear_consumers_source_fault_selftest.mjs','tools/fixtures/hc_msi_rear_consumers/probe.cpp','capabilities/HcControllerConfigurationCapabilityCatalog.h','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(name),name);
 assert.equal(row.completionContract.rules[0].line,'MSI_SD_REAR_CONSUMER_V1_COMPLETED positive=78/67093 faults=5/5 DefaultRelease');assert(!row.resultFiles.some(file=>file.includes('native-missing-mode-fence')));assert.equal(new Set(rows.map(row=>row.id)).size,rows.length);
});


test('unified registry invokes original Decky and game-library consumers with exact execution evidence',()=>{
 const rows=getExtraAdapters();
 for(const [id,module,producer] of [
  ['plugins.decky-mirror-host-consumer','decky-mirror-host-consumer','tools/decky_sidebar_host_selftest.mjs'],
  ['game-library.account-discovery-consumer','game-library-ui-consumers','tools/custom_steam_library_account_status_selftest.mjs'],
  ['game-library.snapshot-consumer','game-library-ui-consumers','tools/custom_steam_library_ui_datachain_selftest.mjs'],
 ]){
  const matches=rows.filter(row=>row.id===id);assert.equal(matches.length,1,'uninvoked imports or duplicate adapters do not register a consumer');const row=matches[0];
  assert.equal(row.path,'tools/ymcc_sim/global/'+module+'/run.mjs');
  for(const source of [producer,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])assert(row.evidenceInputs.includes(source),source);
  assert(row.profiles.includes('desktop')&&row.profiles.includes('full'));assert.equal(row.scope,'behavioral-mock');assert(row.resultFiles.length>0);assert(row.artifactInputs.length>0&&row.artifactInputs.length<=640);
 }
 assert.equal(new Set(rows.map(row=>row.id)).size,rows.length);
});
