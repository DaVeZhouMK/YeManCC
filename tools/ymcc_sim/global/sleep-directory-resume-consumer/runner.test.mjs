import test from'node:test';import assert from'node:assert/strict';import fs from'node:fs';import path from'node:path';import{getSleepDirectoryResumeConsumerAdapters,REPO,MODULE,SOURCE_SUCCESSOR,moduleSources,inputFiles,toolchainSpec}from'./adapter.mjs';
import{compileFunction}from'node:vm';
import{createHash}from'node:crypto';
import{verifyDirectorySourceSuccessor}from'./source-successor.mjs';
test('directory worker extracts the complete definition with braced defaults and rejects ambiguous input',()=>{
 const source=fs.readFileSync(path.join(REPO,MODULE,'worker.mjs'),'utf8'),start=source.indexOf('function definition('),end=source.indexOf('\nfunction extract(',start);
 assert(start>=0&&end>start);
 const definition=compileFunction('return ('+source.slice(start,end)+');',['need'])(assert);
 const signature='static int probe(int n, const std::function<bool()>& current = {})';
 const exact=signature+'\n{ /* } */ return current ? n : 0; }';
 assert.equal(definition(signature+';\n'+exact,'probe'),exact);
 assert.throws(()=>definition(signature+';','probe'),/RESUME_DEFINITION_NOT_UNIQUE/);
 assert.throws(()=>definition(exact+'\n'+exact,'probe'),/RESUME_DEFINITION_NOT_UNIQUE/);
 assert.throws(()=>definition(exact.slice(0,-1),'probe'),/RESUME_DEFINITION_UNBALANCED/);
 assert.throws(()=>definition('static int probe(int n','probe'),/RESUME_PARAMETERS_UNBALANCED/);
});
test('resume consumer declares complete exact inputs and immutable native/live fault contracts',()=>{const[a]=getSleepDirectoryResumeConsumerAdapters(),s=JSON.parse(fs.readFileSync(path.join(REPO,MODULE,'spec.json'),'utf8'));assert.equal(a.id,'sleep.directory-resume-consumer');assert.equal(s.nativeReviewedFunctions.length,14);assert.equal(s.reviewedConsumerHelpers.length,4);assert.equal(s.nativeTargets,8);assert.equal(s.markerParsers+s.jsonResultParser+s.localHelpers,6);assert.equal(s.bridgeReviewedFunctions.length,2);assert.equal(s.fixture.nativeGroups,70);assert.equal(s.fixture.liveGroups,26);assert.equal(s.nativeFixedAssertions.length,70);assert.equal(s.nativeFaults.length,13);assert.equal(s.liveFaults.length,5);const prior=[...new Set([...moduleSources().filter(p=>!p.includes('/source-successor.')),...s.productSources,...s.sharedLeaves])];const support=['sleep_legacy_resume_journal.h','sleep_recovery_record_name.h','startup_resume_delivery.h','startup_resume_file_identity.h','startup_resume_scope.h','sleep_power_observation.h','sleep_power_state.h'].map(p=>'native/'+p);assert.equal(prior.filter(p=>!support.includes(p)).length,42);for(const p of support)assert(s.productSources.includes(p),p);for(const p of prior)assert(a.evidenceInputs.includes(p),p);for(const p of a.evidenceInputs)assert(fs.statSync(path.join(REPO,p)).isFile(),p);for(const f of s.nativeFaults)for(const id of f.expectedFailedCases)assert(s.nativeFixedAssertions.some(x=>x.case===id),id);});
test('exact successor replays original review over captured bytes and rejects unknown inputs',()=>{
 const captured=new Map(inputFiles().map(name=>[name,fs.readFileSync(path.join(REPO,name))]));
 const spec=MODULE+'/spec.json',verify=rows=>verifyDirectorySourceSuccessor(REPO,spec,SOURCE_SUCCESSOR,rows),receipt=verify(captured);
 assert.equal(receipt.status,'PASS');assert.equal(receipt.sourceApproval,false);
 assert.equal(receipt.directorySuccessor.sharedInputs,18);assert.equal(receipt.directorySuccessor.originalInputs,31);
 assert.equal(receipt.directorySuccessor.restoredInputPredecessors,9);
 for(const [name,error] of [[SOURCE_SUCCESSOR,/manifest changed/],[spec,/registered spec changed/],['tools/ymcc_sim/global/sleep-native-recovery-consumer/run.mjs',/run\.mjs:bytes changed/],['tools/sleep_non_user_wake_selftest.py',/sleep_non_user_wake_selftest\.py:bytes changed/]]){
  const changed=new Map(captured);changed.set(name,Buffer.concat([changed.get(name),Buffer.from('\n')]));
  assert.throws(()=>verify(changed),error);
 }
 const missing=new Map(captured);missing.delete('tools/fixtures/sleep_non_user_wake/resume_sender_join.inc');
 assert.throws(()=>verify(missing),/resume_sender_join\.inc:not captured/);
});
test('tool declaration retains the original 561 entries and adds only the reviewed Windows event header',()=>{
 const rows=toolchainSpec().files,header='c:\\program files (x86)\\windows kits\\10\\include\\10.0.26100.0\\um\\winevt.h';
 const isHeader=row=>path.resolve(row.path).toLowerCase()===header;
 assert.deepEqual(rows.filter(isHeader),[{path:header,bytes:24121,sha256:'98a76f9cdc7149fba4407a8d5b4b90e4bcd5bace35fb066da6591aacd77c97c1',role:'windows-sdk-actual-resume-classification-compiler-input'}]);
 const prior=rows.filter(row=>!isHeader(row));
 assert.equal(prior.length,561);
 // The predecessor comes from reversing the shared source-successor declaration, delta 29.
 assert.equal(createHash('sha256').update(JSON.stringify(prior)).digest('hex'),'ccecaccda06f78d303da844aa98578d41f065ff6440b40b8a8fb85596c8eda01');
});
