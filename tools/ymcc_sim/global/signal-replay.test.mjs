import test from 'node:test';
import assert from 'node:assert/strict';
import { sha256File } from './runner.mjs';
import { replaySignals, SIGNAL_CONSUMERS } from './signal-replay.mjs';
const sourceHashes=[{path:'tools/ymcc_sim/global/signal-replay.mjs',sha256:sha256File('tools/ymcc_sim/global/signal-replay.mjs')}];
const evidence=[{input:{},consumer:{},state:{},feedback:{}}];
const plan=(status='READY')=>({status,sourceArchive:{stable:true,sha256:'a'.repeat(64)},events:[{domain:'fan',event:'startup.handshake'},{domain:'input',event:'msi-oem-suppression-observation'}]});
const load=async()=>async()=>({status:'PASS',evidence,sourceHashes});
const options=(extra={})=>({repoRoot:process.cwd(),plan:plan(),loadConsumer:load,...extra});

test('fixed modules consume only their producer events and bind archive identity',async()=>{
  const seen=[];const r=await replaySignals(options({loadConsumer:async(domain,spec)=>async args=>{seen.push({domain,spec,events:args.events});return{status:'PASS',evidence,sourceHashes};}}));
  assert.equal(r.status,'PASS');assert.equal(seen.length,2);assert(seen.every(x=>x.events.length===1&&x.events[0].domain===x.domain));
  assert.equal(seen[0].spec.file,SIGNAL_CONSUMERS.fan.file);assert.equal(r.hardwareValidated,false);assert.equal(r.exactEventTimelineReplayed,false);
});
test('incomplete archive cannot pass despite valid consumer receipts',async()=>{
  assert.equal((await replaySignals(options({plan:plan('INCOMPLETE')}))).status,'INCOMPLETE');
});
test('missing source identity, empty selection, unknown domains and unusable plans do not execute',async()=>{
  let calls=0;const loadConsumer=async()=>{calls++;return async()=>({status:'PASS',evidence,sourceHashes});};
  const altered=plan();altered.sourceArchive.stable=false;
  assert.equal((await replaySignals(options({plan:altered,loadConsumer}))).status,'BLOCKED');
  assert.equal((await replaySignals(options({domains:[],loadConsumer}))).status,'FAIL');
  assert.equal((await replaySignals(options({domains:['arbitrary-native-command'],loadConsumer}))).status,'FAIL');
  assert.equal((await replaySignals(options({plan:plan('FAIL'),loadConsumer}))).status,'BLOCKED');assert.equal(calls,0);
});
test('no event, missing export, malformed receipt and no feedback cannot pass',async()=>{
  const empty=plan();empty.events=[];assert.equal((await replaySignals(options({plan:empty}))).status,'BLOCKED');
  for(const response of [{},null,{status:'PASS'}]) {
    const r=await replaySignals(options({loadConsumer:async()=>async()=>response}));assert.notEqual(r.status,'PASS');
  }
  assert.equal((await replaySignals(options({loadConsumer:async()=>undefined}))).status,'BLOCKED');
});
test('consumer failure and thrown errors are explicit failures; incomplete consumer remains incomplete',async()=>{
  assert.equal((await replaySignals(options({loadConsumer:async()=>async()=>({status:'FAIL',evidence,sourceHashes})}))).status,'FAIL');
  assert.equal((await replaySignals(options({loadConsumer:async()=>async()=>{throw Error('fixture');}}))).status,'FAIL');
  assert.equal((await replaySignals(options({loadConsumer:async()=>async()=>({status:'INCOMPLETE',evidence,sourceHashes})}))).status,'INCOMPLETE');
});

test('consumer-specific incomplete and compatibility statuses retain their meaning',async()=>{
 assert.equal((await replaySignals(options({loadConsumer:async()=>async()=>({status:'INCOMPLETE_COVERAGE',evidence,sourceHashes})}))).status,'INCOMPLETE');
 assert.equal((await replaySignals(options({loadConsumer:async()=>async()=>({status:'BLOCKED_COMPATIBILITY_DIFFERENCE',evidence,sourceHashes})}))).status,'BLOCKED');
});
test('source receipts bind actual product bytes; missing or stale hashes never pass',async()=>{
 assert.equal((await replaySignals(options({loadConsumer:async()=>async()=>({status:'PASS',evidence})}))).status,'BLOCKED');
 const r=await replaySignals(options({loadConsumer:async()=>async()=>({status:'PASS',evidence,sourceHashes:[{path:sourceHashes[0].path,sha256:'0'.repeat(64)}]})}));
 assert.equal(r.status,'FAIL');assert(r.diagnostics.some(x=>x.code==='PRODUCT_SOURCE_CHANGED_OR_MISSING'));
});

test('performance signals select their fixed GPU consumer and retain INCOMPLETE missing-binding receipts',async()=>{
 const p={status:'READY',sourceArchive:{stable:true,sha256:'a'.repeat(64)},events:[{domain:'performance',event:'machine-capability',capability:'gpu.dedicatedMemory'}]};let selected;const r=await replaySignals(options({plan:p,domains:['performance'],loadConsumer:async(_domain,spec)=>{selected=spec;return async()=>({status:'INCOMPLETE',evidence,sourceHashes});}}));assert.equal(selected.file,'tools/ymcc_sim/global/gpu-memory-signals/log-consumer.mjs');assert.equal(r.status,'INCOMPLETE');assert.equal(r.results.length,1);assert.equal(r.fullProductAcceptance,false);
});

test('ingestion source hashes are checked before any consumer runs',async()=>{
 const p=plan();p.sourceHashes=[{path:'tools/ymcc_sim/global/log-corpus.mjs',sha256:'0'.repeat(64)}];let calls=0;
 const r=await replaySignals(options({plan:p,loadConsumer:()=>{calls++;}}));
 assert.equal(r.status,'FAIL');assert.equal(calls,0);assert(r.diagnostics.some(row=>row.code==='PLAN_SOURCE_CHANGED_OR_MISSING'));
});
