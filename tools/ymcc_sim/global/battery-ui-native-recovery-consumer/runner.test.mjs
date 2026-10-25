import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {REPO,FIXTURE,MODULE,sourceFiles,toolchain,verifyTools,read,checkPlan,stopFallbackFault,verifyOwnedJob} from '../../../fixtures/battery_ui_native_recovery/contract.mjs';
import {getBatteryUiNativeRecoveryConsumerAdapters} from './adapter.mjs';
test('fixed three actual recovery/control scenarios and exact original assertion source fault are mandatory',()=>{
 const plan=read(path.join(REPO,FIXTURE,'cases.json'));checkPlan(plan);
 for(const changed of [{...plan,positiveCases:2},{...plan,cases:plan.cases.slice(0,-1)},{...plan,fault:{...plan.fault,expectedExit:0}},{...plan,fault:{...plan.fault,expectedFailed:[]}},{...plan,automaticSetReplay:true}])assert.throws(()=>checkPlan(changed));
});
test('current-source fault removes only the real software stop guard and requires its complete current anchor',()=>{
 const source=fs.readFileSync(path.join(REPO,'src/components/BatteryChargeSettings.vue'),'utf8'),fault=stopFallbackFault(source);
 assert.equal(source.replace(fault.before,fault.after),fault.source);
 assert.notEqual(fault.beforeSha256,fault.afterSha256);
 assert.throws(()=>stopFallbackFault(source.replace(fault.before,'return unrelatedCapability')));
 assert.ok(fault.source.includes('const release = mayRunSoftware.value && target === 100;'));
});
test('descriptor preserves all2362 external members, full original938 native closure and actual current source artifacts without truncation',()=>{
 const tool=toolchain(),[adapter]=getBatteryUiNativeRecoveryConsumerAdapters();
 assert.equal(adapter.id,'battery.ui-native-recovery-consumer');
 assert.equal(tool.externalCount,2362);assert.equal(tool.currentSourceArtifactCount,sourceFiles().length);
 assert.equal(adapter.artifactInputs.length,tool.files.length);assert.ok(tool.files.length>2375&&tool.files.length<=3072);
 for(const relative of ['native/battery_charge_client.h','native/BatteryHost/Controller.cs','src/components/BatteryChargeSettings.vue','src/bridge/batteryCharge.ts',FIXTURE+'/NativeUiClient.cpp',FIXTURE+'/ui-driver.mjs'])assert.ok(adapter.evidenceInputs.includes(relative));
 assert.equal(new Set(adapter.evidenceInputs).size,adapter.evidenceInputs.length);
 assert.ok(tool.files.every(x=>!/^D:[/\\]/i.test(x.path)));
 verifyTools(tool);
});
test('actual final declared tool/source member tampering is rejected without shortening the closure',()=>{
 const tool=toolchain(),last=tool.files.at(-1);assert.ok(fs.existsSync(last.path));
 const changed={...tool,files:tool.files.map((x,i)=>i===tool.files.length-1?{...x,sha256:'0'.repeat(64)}:x)};
 assert.throws(()=>verifyTools(changed),/BATTERY_UI_TOOL_INPUT_CHANGED/);
});
test('owned Job acceptance distinguishes an expected source-fault23 from failed/forced/unowned positive execution',()=>{
 const scope={nonce:'current-case',root:path.resolve('owned-ui-case'),expectedExit:23},receipt={status:'FAIL',nonce:scope.nonce,root:scope.root,workerPid:99,workerExitCode:23,natural:true,forcedTermination:false,jobAssignedBeforeGate:true};
 assert.equal(verifyOwnedJob(receipt,scope),receipt);
 for(const changed of [{...receipt,status:'PASS'},{...receipt,workerExitCode:0},{...receipt,nonce:'other'},{...receipt,forcedTermination:true},{...receipt,jobAssignedBeforeGate:false}])assert.throws(()=>verifyOwnedJob(changed,scope));
 assert.throws(()=>verifyOwnedJob(receipt,{...scope,expectedExit:0}));
});
test('new portable producer sources have no historical private workspace path',()=>{
 for(const relative of sourceFiles().filter(x=>x.startsWith(FIXTURE+'/')||x.startsWith(MODULE+'/'))){
  const text=fs.readFileSync(path.join(REPO,relative),'utf8');
  assert.ok(!/D:[/\\].*(?:Codex|private-roundtrip)/i.test(text),relative);
 }
});
