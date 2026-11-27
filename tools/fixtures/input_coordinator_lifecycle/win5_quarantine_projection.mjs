import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
export {bindLifecycleBusDiscovery as bindWin5BusDiscovery} from './compiler_inputs.mjs';

export const selector='gpd-win5-elite-quarantine-then-on';
const sha=text=>createHash('sha256').update(text).digest('hex');

export function win5QuarantineScope(caseName,scenario){
  assert.equal(caseName,selector,'explicit Win5 selector required');
  const names=[...scenario.matchAll(/^static constexpr char win5EliteQuarantineCase\[\] = "(GPD\/Win5\/elite-quarantine-then-explicit-on\/exit)";$/gm)];
  assert.equal(names.length,1,'canonical scenario name required');
  return {selectedCase:caseName,suite:'lifecycle-directed',requestedModes:['full-pipe-core'],expectedCases:[names[0][1]],defaultSelectionChanged:false};
}

// Insert only into the fixture's generated C++ source, never a Terminal29 input.
export function projectWin5QuarantineFixture(fixture,scenario,caseName,replace){
  const scope=win5QuarantineScope(caseName,scenario);
  const include='#include "persona_lifecycle_cases.inc"';
  const admission='||selectedCase==L"case:gpd-settled-first-on-full-cycle"||';
  const dispatch='else if(mode==L"full-pipe-core"){if(selectedCase==L"case:gpd-settled-first-on-full-cycle")';
  const explicit=`selectedCase==L"case:${scope.selectedCase}"`;
  const edits=[
    [include,`${include}\n${scenario.trimEnd()}\n`],
    [admission,`${admission}${explicit}||`],
    [dispatch,`else if(mode==L"full-pipe-core"){if(${explicit})fullPipeWin5EliteQuarantineCase();else if(selectedCase==L"case:gpd-settled-first-on-full-cycle")`],
  ];
  const text=edits.reduce((src,[before,after])=>replace(src,before,after,'Win5 explicit fixture'),fixture);
  const undo=compiled=>[...edits].reverse().reduce((src,[before,after])=>replace(src,after,before,'Win5 fixture inverse'),compiled);
  assert.equal(undo(text),fixture,'fixture byte inverse');
  return {text,undo,scope,receipt:{fixtureBeforeSha256:sha(fixture),fixtureAfterSha256:sha(text),scenarioSha256:sha(scenario)}};
}

export function acceptsWin5QuarantineReceipt(results,scope){
  if(!Array.isArray(results)||results.length!==1||scope?.selectedCase!==selector||!Array.isArray(scope.expectedCases)||scope.expectedCases.length!==1)return false;
  const [run]=results,raw=run?.result;
  if(run?.mode!=='full-pipe-core'||run.exitCode!==0||run.signal||run.error||run.timedOut||raw?.status!=='PASS'||raw.forbiddenIo!==0||!Array.isArray(raw.cases)||raw.cases.length!==1)return false;
  const [row]=raw.cases;
  const {oldHost:old,blocked,failedFinal:failed,oldReturn:returned,replacement:next}=row??{};
  const pos=n=>Number.isSafeInteger(n)&&n>0;
  const candidate=blocked?.protection,final=failed?.protection;
  const finish=next?.receipt,tuple=finish?.stateAfterFinalCleanup?.tuple;
  return row?.name===scope.expectedCases[0]&&row.passed===true&&row.realManagedPipe===true&&
    pos(old?.requestId)&&pos(old?.pid)&&pos(old?.epoch)&&typeof old.birth==='string'&&/^[1-9]\d*$/.test(old.birth)&&typeof old.runId==='string'&&old.runId.length>0&&
    old.receipt?.processId===old.pid&&old.receipt.nativeObservedProcessExit===true&&old.receipt.faultConsumed===true&&
    pos(blocked?.requestId)&&pos(blocked.power)&&pos(blocked.deadline)&&blocked.requestId!==old.requestId&&
    blocked.consumes===1&&blocked.releaseQuarantine===1&&blocked.newHosts===0&&blocked.preparedCommands===0&&blocked.ready===0&&
    candidate?.requestId===blocked.requestId&&candidate.phase==='verifying'&&candidate.locked===true&&candidate.candidateOutcome==='failure'&&
    candidate.verificationRemainingMs===10000&&candidate.fallbackAttempts===0&&
    final?.requestId===blocked.requestId&&final.phase==='failure'&&final.locked===false&&final.fallbackAttempts===0&&
    failed.failures===1&&failed.successes===0&&returned?.settled===true&&returned.newHosts===0&&returned.epoch===old.epoch&&returned.runId===old.runId&&
    pos(next?.requestId)&&next.requestId!==blocked.requestId&&next.requestId!==old.requestId&&pos(next.pid)&&pos(next.epoch)&&
    next.successes===1&&next.failures===0&&row.lastSourceReceipt?.hostSubmission==='frame-accepted'&&row.lastSourceReceipt.focusIsolationSemanticNeutral===false&&
    next.epoch!==old.epoch&&typeof next.runId==='string'&&next.runId.length>0&&next.runId!==old.runId&&
    finish?.processId===next.pid&&finish.nativeObservedProcessExit===true&&finish.nativeObservedExitCode===0&&
    finish.nativeObserverCloseSucceeded===true&&finish.finalCleanupOk===true&&finish.hasController===false&&finish.hasContext===false&&
    tuple?.RunId===next.runId&&tuple?.Epoch===next.epoch;
}

export function win5QuarantineSelectionReceipt(results,scope){
  const producer=results?.[0],rows=producer?.result?.cases;
  const exact=acceptsWin5QuarantineReceipt(results,scope);
  return {...scope,actualCases:Array.isArray(rows)?rows.map(row=>row.name):[],exact,
    status:exact?'PASS':'FAIL',producerExitCode:producer?.exitCode,producerError:producer?.error};
}
