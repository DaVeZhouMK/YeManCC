// Actual repository/API/IPC consumer. Native commit is an explicit memory port;
// native locking/atomic IO are exercised by settings_baseline_native_selftest.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import {compileEntryBundle,createEntryFixture,repoRoot,fileHash} from './fixtures/controller_input_entry_lifecycle_fixture.mjs';
const args=process.argv.slice(2);assert(args.length===2&&args[0]==='--out'&&path.isAbsolute(args[1]));const out=args[1];assert(!fs.existsSync(out));fs.mkdirSync(out,{recursive:true});
const compiled=await compileEntryBundle();fs.writeFileSync(path.join(out,'compiled.cjs'),compiled.code);
const results=[];const deferred=()=>{let resolve;return{promise:new Promise(r=>{resolve=r;}),resolve:v=>resolve(v)};};
async function test(name,run){const start=performance.now();const f=await createEntryFixture(compiled);const facts={};try{await run(f,facts);results.push({name,pass:true,elapsedMs:performance.now()-start,facts});}catch(e){results.push({name,pass:false,elapsedMs:performance.now()-start,failure:e.stack??String(e),facts});}finally{f.stop();}}
await test('400-concurrent-section-writes-are-serialized-and-preserve-other-sections',async(f,facts)=>{
 const calls=[];let active=0,maxActive=0;
 f.beforeWrite=async()=>{active++;maxActive=Math.max(active,maxActive);await Promise.resolve();active--;};
 for(let i=0;i<400;++i)calls.push(f.repo.saveSettingsSection(i%2?'ui':'music',{stormCounter:i}));
 await Promise.all(calls);const d=f.document();assert.equal(f.commits.length,400);assert.equal(d.ui.stormCounter,399);assert.equal(d.music.stormCounter,398);assert.equal(d.futureDocument.retained,true);assert.equal(maxActive,1);
 facts.commits=f.commits.length;facts.maxConcurrentNativeRequests=maxActive;
});
await test('submission-snapshot-does-not-follow-later-object-mutation',async f=>{
 const entered=deferred(),release=deferred();let once=true;f.beforeWrite=async()=>{if(once){once=false;entered.resolve();await release.promise;}};
 const first=f.repo.saveSettingsSection('ui',{stormCounter:1});await entered.promise;
 const value={stormNested:{value:7}};const second=f.repo.saveSettingsSection('ui',value);value.stormNested.value=99;release.resolve();await Promise.all([first,second]);assert.equal(f.document().ui.stormNested.value,7);
});
await test('write-failures-do-not-poison-the-queue-or-report-success',async(f,facts)=>{
 let attempt=0;f.beforeWrite=async()=>{f.writeFailure=++attempt%7===0;};
 const calls=Array.from({length:120},(_,i)=>f.repo.saveSettingsSection('ui',{stormCounter:i}));const settled=await Promise.allSettled(calls);
 assert.equal(settled.filter(v=>v.status==='rejected').length,17);assert.equal(f.commits.length,103);assert.equal(f.document().ui.stormCounter,119);facts.attempts=attempt;facts.rejections=17;facts.commits=103;
});
await test('generation-reset-cancels-queued-old-writers-and-allows-fresh-work',async(f,facts)=>{
 const entered=deferred(),release=deferred();let once=true;f.beforeWrite=async()=>{if(once){once=false;entered.resolve();await release.promise;}};
 const old=Array.from({length:80},(_,i)=>f.repo.saveSettingsSection('ui',{stormCounter:i}));const settled=Promise.allSettled(old);
 await entered.promise;f.repo.clearSettingsCache();release.resolve();const replies=await settled;
 assert(replies.every(v=>v.status==='rejected'));assert.equal(f.writeAttempts.length,1);assert.equal(f.commits.length,1); // Already submitted commit cannot be undone.
 await f.repo.saveSettingsSection('ui',{stormCounter:1000});assert.equal(f.document().ui.stormCounter,1000);facts.oldQueuedRejected=80;facts.alreadySubmittedCommits=1;facts.freshCommitSucceeded=true;
});
await test('timed-out-committed-reply-cannot-overwrite-successor-cache',async(f,facts)=>{
 const held=deferred();let once=true;f.afterWrite=async receipt=>{if(once){once=false;await held.promise;}return receipt;};
 const start=performance.now();await assert.rejects(f.repo.saveSettingsSection('ui',{stormCounter:1}));assert(performance.now()-start>=2900);
 assert.equal(f.document().ui.stormCounter,1);await f.repo.saveSettingsSection('ui',{stormCounter:2});held.resolve();await f.flush();
 assert.equal(f.document().ui.stormCounter,2);assert.equal((await f.repo.loadSettings()).ui.stormCounter,2);facts.timeoutMs=performance.now()-start;facts.lateReplyDidNotRevert=true;
});
await test('unverified-native-receipt-is-rejected-and-next-write-recovers',async f=>{
 f.afterWrite=async()=>({ok:true,content:JSON.stringify(f.initial)});
 await assert.rejects(f.repo.saveSettingsSection('ui',{stormCounter:42}));f.afterWrite=null;
 await f.repo.saveSettingsSection('ui',{stormCounter:43});assert.equal(f.document().ui.stormCounter,43);assert.equal((await f.repo.loadSettings()).ui.stormCounter,43);
});
const sourceDrift=compiled.sources.filter(s=>fileHash(s.path)!==s.sha256).map(s=>s.path);
const report={status:results.every(r=>r.pass)&&!sourceDrift.length?'PASS':'FAIL',scope:'actual frontend settings repository/API/IPC; native explicit memory port; no installed file or hardware writes',compiledSha256:compiled.sha256,inputs:compiled.sources,sourceDrift,results};
fs.writeFileSync(path.join(out,'REPORT.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({status:report.status,results,sourceDrift,out}));process.exitCode=report.status==='PASS'?0:1;
