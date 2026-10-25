import test from'node:test';import assert from'node:assert/strict';
import{parseFinalStages,FINAL_STAGE_MAX_BYTES}from'./r3-terminal22-stage-receipts.mjs';
const names=['body-start','body-return','report-built','report-write-start','serialize-start','serialize-end','report-write-end','main-return'];
const rows=()=>names.map((stage,i)=>({schemaVersion:1,stage,pid:42,tid:7,tickMs:100+i*20,qpc:String(100000+i*20000),qpcFrequency:'1000000',bytes:stage==='serialize-end'?34000:0,lost:0}));
const text=r=>r.map(r=>JSON.stringify(r)).join('\n')+'\n';
test('full measured phases do not prove natural exit or cleanup',()=>{const r=parseFinalStages(text(rows()),42);assert.equal(r.status,'OBSERVED_THROUGH_MAIN_RETURN');assert.equal(r.phases.bodyMs,20);assert.equal(r.phases.serializationMs,20);assert.equal(r.serializationBytes,34000);assert.equal(r.naturalExitProven,false);assert.equal(r.resourceReleaseProven,false);assert.equal(r.acceptanceGranted,false);});
test('empty report and serialize prefix remain measured-prefix only',()=>{const r=parseFinalStages(text(rows().slice(0,5)),42);assert.equal(r.status,'OBSERVED_PREFIX');assert.equal(r.lastStage,'serialize-start');assert.equal(r.phases.serializationMs,null);assert.equal(r.phases.bodyMs,20);});
test('body prefix never invents a body duration',()=>{const r=parseFinalStages(text(rows().slice(0,1)),42);assert.equal(r.phases.bodyMs,null);assert.equal(r.status,'OBSERVED_PREFIX');});
test('actual recorded serialize throw is distinct and not success',()=>{const r=rows().slice(0,6);r[5].stage='serialize-throw';const out=parseFinalStages(text(r),42);assert.equal(out.status,'OBSERVED_SERIALIZATION_THROW');assert.equal(out.phases.serializationMs,null);assert.equal(out.acceptanceGranted,false);});
for(const[name,mutate]of[
 ['wrong native pid',r=>r[0].pid=99],['missing pid',r=>delete r[0].pid],
 ['recycled thread',r=>r[1].tid=8],['changed clock frequency',r=>r[1].qpcFrequency='1'],
 ['backwards QPC',r=>r[1].qpc='0'],['backwards tick',r=>r[1].tickMs=0],
 ['lost observation',r=>r[0].lost=1],['negative bytes',r=>r[0].bytes=-1],
 ['clock rounding input',r=>r[0].qpc=100000],['zero frequency',r=>r[0].qpcFrequency='0'],
 ['reordered marker',r=>[r[1],r[2]]=[r[2],r[1]]],['duplicate marker',r=>r[1].stage=r[0].stage],
 ['missing intermediate marker',r=>r.splice(2,1)],['unknown stage',r=>r[0].stage='cleanup-complete']
])test(name+' remains UNKNOWN',()=>{const r=rows();mutate(r);const result=parseFinalStages(text(r),42);assert.equal(result.status,'UNKNOWN');assert.equal(result.acceptanceGranted,false);});
test('partial JSON and forged full-count summary stay UNKNOWN',()=>{assert.equal(parseFinalStages('{',42).status,'UNKNOWN');assert.equal(parseFinalStages(JSON.stringify({status:'PASS',checks:6457}),42).status,'UNKNOWN');});
test('bounded input rejects oversize evidence',()=>assert.equal(parseFinalStages(' '.repeat(FINAL_STAGE_MAX_BYTES+1),42).status,'UNKNOWN'));
test('CRLF is parsed without changing measured phases',()=>assert.equal(parseFinalStages(text(rows()).replaceAll('\n','\r\n'),42).phases.serializationMs,20));
test('no markers remains UNKNOWN',()=>assert.equal(parseFinalStages('',42).status,'UNKNOWN'));
