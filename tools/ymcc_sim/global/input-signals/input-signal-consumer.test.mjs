import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { INPUT_SIGNAL_CONTRACT,runInputSignalRegressions,replayInputSignals,evaluateInputCompatibility } from './input-signal-consumer.mjs';
const repoRoot=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const workDir=process.env.YMCC_INPUT_WORK_DIR??path.join(process.cwd(),'work','ymcc-input-signals');
test('genuine product regression cases',async t=>{
 const result=await runInputSignalRegressions({repoRoot,workDir});
 for(const entry of result.inputConsumerStateFeedback)await t.test(entry.id,()=>assert.equal(entry.status,'PASS',entry.code));
 assert.equal(result.status,'PASS');
 for(const file of ['src/bridge/ipc.ts','src/bridge/controllerShortcutRuntime.ts','src/bridge/controllerShortcutActions.ts','src/bridge/shortcutActionFeedback.ts','src/bridge/monitorNoticeHost.ts','native/shortcut_action_protocol.h'])assert(result.executedProductModules.some(m=>m.path===file),file);
 assert(result.sourceHashes.every(s=>/^[a-f0-9]{64}$/.test(s.sha256)));
});
test('OEM archive scalars remain diagnostic and cannot become requests',async()=>{
 const result=await replayInputSignals({repoRoot,events:[{domain:'input',event:'msi-oem-suppression-observation',memberId:'fixture-member',line:1,
  phase:'settled-1500ms',actionDispatchAccepted:true,defaultActionSuppressionVerified:false,hardwareWriteIssued:false,readOnly:true}]});
 assert.equal(result.summary.actionsExecuted,0);assert.equal(result.summary.oemDiagnosticObservations,1);assert.equal(result.executedProductModules.length,0);
 const evidence=result.inputConsumerStateFeedback[0];assert.equal(evidence.provenance,'archive-literal');assert.equal(evidence.feedback.physicalSuppressionConfirmed,false);assert.equal(evidence.state.defaultActionSuppressionVerified,false);
 assert.equal(result.status,'INCOMPLETE');
});
test('explicit source fixture traverses actual production chain separately',async()=>{
 const result=await replayInputSignals({repoRoot,workDir,events:[{event:'fixture.shortcutAction',provenance:'source-derived-fixture',actionId:'app.launchQuickApp',params:{appPath:'Z:\\YMCC-MEMORY-ONLY\\fixture.exe'}}]});
 assert.equal(result.summary.sourceDerivedFixtures,1);assert.equal(result.summary.actionsExecuted,1);assert.equal(result.inputConsumerStateFeedback[0].feedback.status,'CONSUMED');
 assert.equal(result.inputConsumerStateFeedback[0].feedback.native.state,'applied');assert.equal(result.summary.archiveLiteral,0);
});
test('missing native mailbox entry fails at real claim boundary',async()=>{
 const result=await replayInputSignals({repoRoot,workDir,events:[{event:'input.shortcutAction',provenance:'source-derived-fixture',data:{schemaVersion:1,id:'unissued-request',actionId:'os.switchProgram',params:{},issuedAtUnixMs:1792332000000,expiresAtUnixMs:1792332030000,powerGeneration:1,inputRevision:1}}]});
 assert.equal(result.summary.actionsExecuted,0);assert.equal(result.inputConsumerStateFeedback[0].feedback.status,'NATIVE_CLAIM_REJECTED_UNISSUED_REQUEST');assert(result.remainingGaps.some(g=>g.stage==='claim'));
});
test('missing raw fields fail at request reader and are not supplemented',async()=>{
 const result=await replayInputSignals({repoRoot,workDir,events:[{event:'input.shortcutAction',data:{schemaVersion:1,actionId:'os.switchProgram'}}]});
 assert.equal(result.summary.actionsExecuted,0);assert.equal(result.inputConsumerStateFeedback[0].feedback.status,'INVALID_REQUEST_REJECTED');
});
test('missing feedback consumer is an explicit stage failure',async()=>{
 const result=await replayInputSignals({repoRoot,workDir,behavior:{dropFeedback:true},events:[{event:'fixture.shortcutAction',provenance:'source-derived-fixture',actionId:'app.launchQuickApp',params:{appPath:'Z:\\YMCC-MEMORY-ONLY\\fixture.exe'}}]});
 assert.equal(result.inputConsumerStateFeedback[0].feedback.status,'MISSING_FEEDBACK_CONSUMER');assert.equal(result.summary.actionsExecuted,1);
});
test('fixture provenance must be explicit',async()=>{
 const result=await replayInputSignals({repoRoot,events:[{event:'fixture.shortcutAction',actionId:'os.switchProgram'}]});
 assert.equal(result.summary.actionsExecuted,0);assert(result.remainingGaps.some(g=>g.code==='FIXTURE_PROVENANCE_REQUIRED'));
});
test('schema mismatch blocks action admission',async()=>{
 const result=await replayInputSignals({repoRoot,compatibility:{expectedContract:{requestSchemaVersion:2}},events:[{event:'fixture.shortcutAction',provenance:'source-derived-fixture'}]});
 assert.equal(result.status,'BLOCKED');assert.equal(result.summary.actionsExecuted,0);assert.equal(result.executedProductModules.length,0);
});
test('HC candidate differences need external ABI gate, with no hardcoded version',()=>{
 const unchanged=evaluateInputCompatibility({baseline:{hcVersion:'future-example-version',runtimeRoot:'candidate-root'},candidate:{hcVersion:'future-example-version',runtimeRoot:'candidate-root'}});
 assert.equal(unchanged.status,'SCHEMA_COMPATIBLE');assert.equal(unchanged.hcValidation,'NOT_EXECUTED_BY_INPUT_CONSUMER');
 const changed=evaluateInputCompatibility({baseline:{hcVersion:'old-example-version',runtimeRoot:'a'},candidate:{hcVersion:'new-example-version',runtimeRoot:'b'}});
 assert.equal(changed.status,'BLOCKED_COMPATIBILITY_DIFFERENCE');assert.deepEqual(changed.differences.map(d=>d.field),['hcVersion','runtimeRoot']);
 assert.equal(evaluateInputCompatibility({baseline:{payloadIdentity:'old'}}).status,'BLOCKED_COMPATIBILITY_DIFFERENCE');assert.equal(INPUT_SIGNAL_CONTRACT.hcVersionPinned,false);
});
test('input replay budget is bounded',async()=>{
 await assert.rejects(()=>replayInputSignals({repoRoot,events:Array(20001).fill(null)}),/INPUT_EVENTS_LIMIT/);
});

test('malformed fixture parameters fail at the genuine product parameter reader',async()=>{
 const result=await replayInputSignals({repoRoot,workDir,events:[{event:'fixture.shortcutAction',provenance:'source-derived-fixture',actionId:'monitor.toggle',params:{operation:'illegal'}}]});
 assert.equal(result.summary.actionsExecuted,0);assert(result.remainingGaps.some(g=>g.stage==='input'&&g.code==='INVALID_FIXTURE_ACTION_PARAMETERS'));
});
