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


import ledgerFs from 'node:fs';
import ledgerPath from 'node:path';
import {createHash as ledgerCreateHash} from 'node:crypto';
import {simulationCorePath as ledgerSimulationCorePath} from '../release-host-environment.mjs';
import {collectObservedCompilerLedger} from './r3-terminal22-native-prepare.mjs';
const ledgerSha=bytes=>ledgerCreateHash('sha256').update(bytes).digest('hex');
function ledgerFixture(t){
 const requested=process.env.YMCC_TERMINAL22_LEDGER_TEST_TEMP;
 const root=requested?ledgerPath.resolve(requested):ledgerSimulationCorePath('Workspace/BacklogAssist-20261102/Runtime105NativeLedger/tmp');
 assert(ledgerPath.isAbsolute(root));ledgerFs.mkdirSync(root,{recursive:true});
 assert.equal(ledgerFs.realpathSync(root).toLowerCase(),ledgerPath.resolve(root).toLowerCase());
 const base=ledgerFs.mkdtempSync(ledgerPath.join(root,'observed-'));
 t.after(()=>{const rel=ledgerPath.relative(root,base);assert(rel&&!rel.startsWith('..')&&!ledgerPath.isAbsolute(rel));assert.equal(ledgerFs.realpathSync(base).toLowerCase(),ledgerPath.resolve(base).toLowerCase());ledgerFs.rmSync(base,{recursive:true,force:false});});
 const make=(relative,text)=>{const file=ledgerPath.join(base,relative);ledgerFs.mkdirSync(ledgerPath.dirname(file),{recursive:true});ledgerFs.writeFileSync(file,text,{flag:'wx'});return {path:file,sha256:ledgerSha(ledgerFs.readFileSync(file))};};
 const generated=make('native/generated.h','// compiled generated header\n'),system=make('toolchain/observed.h','// observed tool header\n'),unused=make('toolchain/unused.h','// declared but unused\n');
 const fixture=make('native/fixture.cpp','// actual first translation unit\n'),dsu=make('native/source/native/dsu_server.cpp','// actual second translation unit\n'),engine=make('native/source/native/steam_original_recovery_engine.cpp','// actual third translation unit\n');
 const buildScript=make('native/build.cmd','// fixture compile-only recipe\n'),exe=make('native/CoordinatorFixture.exe','controlled unit-test bytes; not executable');
 const source={records:[{path:'native/dsu_server.cpp',snapshot:dsu.path,sha256:dsu.sha256},{path:'native/steam_original_recovery_engine.cpp',snapshot:engine.path,sha256:engine.sha256}],compiledGenerated:[generated,fixture],observedIncludePaths:[generated.path,system.path]};
 const sourceManifest=make('native/source-manifest.json',JSON.stringify(source));
 const logFile=make('native/build.log',source.observedIncludePaths.map(file=>'Note: including file: '+file).join('\n')+'\n').path;
 const native={status:'COMPILED_NOT_EXECUTED',compileOnly:true,compilerExitCode:0,runBranchReached:false,sourceManifest,buildScript,exe};
 const f={base,source,sourceManifest,generated,system,unused,fixture,dsu,engine,native,logFile,allowedTools:[system,unused]};
 f.rewriteSource=()=>{ledgerFs.writeFileSync(sourceManifest.path,JSON.stringify(source));sourceManifest.sha256=ledgerSha(ledgerFs.readFileSync(sourceManifest.path));};return f;
}
test('terminal22 observed compiler ledger: keeps actual observations and explicit units separate from unobserved declarations',t=>{
 const f=ledgerFixture(t),r=collectObservedCompilerLedger(f),paths=new Set(r.inputs.map(row=>row.path.toLowerCase()));
 assert.equal(r.status,'ACTUAL_COMPILER_INPUTS_PINNED_NOT_RUNTIME_QUALIFIED');assert.equal(r.unobservedAllowlistClaimedUsed,false);assert.equal(r.sourceApprovalGranted,false);assert.equal(r.publicRuntimeAccepted,false);assert.equal(r.explicitTranslationUnits.length,3);assert.equal(r.observedIncludePaths.length,2);
 for(const row of [f.generated,f.system,f.fixture,f.dsu,f.engine,f.native.buildScript])assert(paths.has(row.path.toLowerCase()));assert.equal(paths.has(f.unused.path.toLowerCase()),false);assert.equal(paths.size,6);assert.equal(r.buildLog.sha256,ledgerSha(ledgerFs.readFileSync(f.logFile)));
});
test('terminal22 observed compiler ledger: rejects an unbound observed include instead of blessing post-read bytes',t=>{
 const f=ledgerFixture(t),unknown=ledgerPath.join(f.base,'unknown.h');ledgerFs.writeFileSync(unknown,'unbound');f.source.observedIncludePaths.push(unknown);f.rewriteSource();ledgerFs.appendFileSync(f.logFile,'Note: including file: '+unknown+'\n');assert.throws(()=>collectObservedCompilerLedger(f),/unbound observed compiler input/);
});
test('terminal22 observed compiler ledger: rejects drift from a real precompiled input pin',t=>{
 const f=ledgerFixture(t);ledgerFs.writeFileSync(f.system.path,'changed after precompile pin');assert.throws(()=>collectObservedCompilerLedger(f),/compiler input changed/);
});
test('terminal22 observed compiler ledger: rejects disagreement between original log and source manifest',t=>{
 const f=ledgerFixture(t);ledgerFs.writeFileSync(f.logFile,'Note: including file: '+f.generated.path+'\n');assert.throws(()=>collectObservedCompilerLedger(f),/compiler include log and source manifest differ/);
});
test('terminal22 observed compiler ledger: failed native compile cannot be promoted to an observed ledger',t=>{
 const f=ledgerFixture(t);f.native.compilerExitCode=1;assert.throws(()=>collectObservedCompilerLedger(f),/actual successful compile-only result required/);
});
