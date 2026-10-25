import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {test} from 'node:test';
import {createHash} from 'node:crypto';
import {REPO,sourceInputs,historicalInputs,signalArtifactRows,getSignalEvidenceAdapters,toolInputs} from './adapter.mjs';
import {CHILDREN} from './receipts.mjs';
import {primaryContract,primaryGeneratedUnits,primaryFunction,validatePrimaryFunctionIdentity,validatePrimaryMatrix,validatePrimaryArtifactSet,validatePrimaryThread,primaryLinkLibraries} from './primary-thread-receipts.mjs';
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const schema=read(new URL('./consumer-schema.json',import.meta.url)),c=primaryContract(REPO),basis=read(c.basisResult.path),clone=()=>structuredClone(basis);
const generated=primaryGeneratedUnits(REPO,c),digest=s=>createHash('sha256').update(s).digest('hex');
test('twenty old groups stay ordered; real primary ownership is twenty-first',()=>{
 assert.equal(CHILDREN.length,22);assert.equal(CHILDREN[18],schema.sensorEnsure.child);assert.equal(CHILDREN[19],schema.ownedCleanupComposition.child);assert.equal(CHILDREN[20],c.child);assert.equal(new Set(CHILDREN).size,22);
 assert.equal(c.sourceInputs.length,13);for(const n of c.sourceInputs)assert(sourceInputs.includes(n),n);assert.equal(new Set(sourceInputs).size,sourceInputs.length);assert.equal(toolInputs(REPO).files.length,731);assert.equal(Object.keys(historicalInputs(REPO)).length,12);
 const a=getSignalEvidenceAdapters(REPO)[0];assert(!a.blockedReason,a.blockedReason);assert(signalArtifactRows(REPO).length<=3072);assert.equal(schema.ownedCleanupComposition.primaryThreadScope.newPrimaryAcceptance,false);
});
test('run-final2 supplies exact original 31 checks and five private thread witnesses for both profiles',()=>validatePrimaryMatrix(basis,c));
for(const profile of c.profiles)for(const variant of Object.keys(c.negativeLabels))test('missing actual negative build rejected: '+profile+'/'+variant,()=>{const r=clone();r.runs=r.runs.filter(row=>row.profile!==profile||row.variant!==variant);assert.throws(()=>validatePrimaryMatrix(r,c),/PRIMARY_FOURTEEN_BUILD_MATRIX/);});
for(const [name,change,error] of [
 ['wrong negative label',r=>r.runs[1].expectedFailure='unrelated assertion',/PRIMARY_SPECIFIED_NEGATIVE_OR_EXIT/],
 ['historical negative exits zero',r=>r.runs[1].exitCode=0,/PRIMARY_SPECIFIED_NEGATIVE_OR_EXIT/],
 ['label missing from runtime',r=>r.runs[1].runtime.failures=[],/PRIMARY_CURRENT_OR_SPECIFIED_LABEL/],
 ['missing private join',r=>r.runs[0].runtime.privateThreadsJoinedBeforeExit=4,/PRIMARY_PRIVATE_RESOURCES_BEFORE_EXIT/],
 ['retained primary owner',r=>r.runs[0].runtime.primaryOwnerRemaining=true,/PRIMARY_PRIVATE_RESOURCES_BEFORE_EXIT/],
 ['leaked pipe oracle',r=>r.runs[0].runtime.pipeOracleLiveBeforeExit=1,/PRIMARY_PRIVATE_RESOURCES_BEFORE_EXIT/],
 ['handle close missing',r=>r.runs[0].runtime.handlesAfter=51,/PRIMARY_PRIVATE_RESOURCES_BEFORE_EXIT/],
 ['positive oracle salvage',r=>r.runs[0].runtime.negativeOnlyPrimarySalvage=1,/PRIMARY_CURRENT_OR_SPECIFIED_LABEL/],
 ['dropped log-whitelist checks',r=>r.runs[0].runtime.checks=29,/PRIMARY_CURRENT_OR_SPECIFIED_LABEL/],
 ['dropped case',r=>r.runs[0].runtime.cases.pop(),/PRIMARY_PRESERVED_CHECKS_CASES_AND_FAILURES/],
 ['duplicate current instead of mutation',r=>r.runs[1]=structuredClone(r.runs[0]),/PRIMARY_PROFILE_VARIANT/],
])test('primary matrix rejects '+name,()=>{const r=clone();change(r);assert.throws(()=>validatePrimaryMatrix(r,c),error);});
const identityFixture=()=>({normalizationPolicy:c.normalizationPolicy,functions:structuredClone(generated.parts),rawFunctionHashes:structuredClone(generated.rawFunctionHashes),rawActualMainFunctionHashes:Object.fromEntries(Object.entries(c.functions).filter(([,p])=>p==='native/main.cpp').map(([n])=>[n,generated.rawFunctionHashes[n]]))});
test('all complete raw functions remain independently pinned while CRLF alone normalizes',()=>{
 const f=identityFixture();validatePrimaryFunctionIdentity(f,generated,c);for(const name of Object.keys(c.functions)){const raw=fs.readFileSync(path.join(REPO,c.functions[name]),'utf8').replace(/^\uFEFF/,'');const lf=raw.replace(/\r\n/g,'\n'),crlf=lf.replace(/\n/g,'\r\n');assert.equal(primaryFunction(lf,name).sha256,primaryFunction(crlf.replace(/\r\n/g,'\n'),name).sha256);assert.equal(primaryFunction(raw,name).sha256,generated.rawFunctionHashes[name]);}
});
for(const [name,change] of [
 ['raw main function',f=>f.rawActualMainFunctionHashes.inputHostClearLocked='0'.repeat(64)],
 ['raw Start full function',f=>f.rawFunctionHashes.inputHostStart='0'.repeat(64)],
 ['normalized complete helper',f=>f.functions.inputHostPrimaryThreadRetirementProvenLocked.sha256='0'.repeat(64)],
 ['unapproved normalization policy',f=>f.normalizationPolicy='ignore whitespace and comments'],
])test('primary identity rejects '+name,()=>{const f=identityFixture();change(f);assert.throws(()=>validatePrimaryFunctionIdentity(f,generated,c),/PRIMARY_RAW_/);});
test('LF normalization retains an inserted statement, comment and spaces in complete function identity',()=>{const name='inputHostPrimaryThreadRetirementProvenLocked',raw=fs.readFileSync(path.join(REPO,'native/main.cpp'),'utf8').replace(/^\uFEFF/,'').replace(/\r\n/g,'\n'),fn=primaryFunction(raw,name);for(const suffix of ['throw 1;','// extra comment',' ']){const changed=fn.text.slice(0,-1)+suffix+'\n}';assert.notEqual(digest(changed),fn.sha256);}});
test('all seven generated primary variants are distinct complete source units',()=>{assert.equal(new Set(Object.values(generated.units).map(digest)).size,7);assert(Object.values(generated.units).every(s=>!s.includes('@@')&&s.includes('primaryThreadsJoined')===false));assert.notEqual(generated.created,generated.historical);});
test('localized or replacement linker prefixes retain exact absolute frozen-library paths',()=>{const p='G:\\Frozen Toolchain\\lib\\kernel32.lib';assert.deepEqual(primaryLinkLibraries('    Searching '+p+':\n    '+String.fromCharCode(0xfffd).repeat(8)+' '+p+':\n'),[p,p]);assert.deepEqual(primaryLinkLibraries('    Searching relative.lib:\n'),[]);});
const fresh=process.env.YMCC_PRIMARY_THREAD_EXECUTION;
if(fresh){
 const e=read(fresh),receiptFile=e.jobs[0].publishedReceipts[0].actualPath,r=read(receiptFile),context=read(e.context.path);
 test('fresh independent group21 receipt is byte-pinned to a real consumer execution',()=>{if(process.env.YMCC_PRIMARY_THREAD_REVALIDATION){const v=read(process.env.YMCC_PRIMARY_THREAD_REVALIDATION);assert.equal(v.status,'PASS');assert.equal(v.pass,true);assert.equal(v.originalExecution.sha256,digest(fs.readFileSync(fresh)));assert.equal(v.originalExecution.status,e.status);assert.equal(e.error,'PRIMARY_ACTUAL_LINK_LIBRARIES');assert.equal(v.validation.builds,14);}else{assert.equal(e.status,'PASS');assert.equal(e.pass,true);}assert.equal(e.scope,'independent-group21-only');assert.equal(digest(fs.readFileSync(receiptFile)),e.jobs[0].publishedReceipts[0].sha256);assert.equal(digest(fs.readFileSync(e.context.path)),e.context.sha256);validatePrimaryArtifactSet(context,r.out);});
 for(const role of ['fixture','object','command','dependencies','executable','runtime','buildLog','runLog','imports'])test('fresh primary rejects missing '+role+' evidence',()=>{const changed=structuredClone(context);delete changed.artifacts[0].files[role];assert.throws(()=>validatePrimaryArtifactSet(changed,r.out),/PRIMARY_ARTIFACT_ROLES/);});
 test('fresh primary rejects mutated generated source pin',()=>{const changed=structuredClone(context);changed.artifacts[0].files.fixture.sha256='0'.repeat(64);assert.throws(()=>validatePrimaryArtifactSet(changed,r.out),/PRIMARY_ARTIFACT_HASH:fixture/);});
 test('fresh primary rejects only thirteen artifact route records',()=>{const changed=structuredClone(context);changed.artifacts.pop();assert.throws(()=>validatePrimaryArtifactSet(changed,r.out),/PRIMARY_FOURTEEN_ARTIFACT_SETS/);});
 test('fresh primary rejects historical run-final2 reuse as current approved receipt',()=>{assert.throws(()=>validatePrimaryThread(basis,{repo:e.sourceRepository,toolchainAuthorityRepo:e.toolchainAuthorityRepository,sourceHashes:e.sourceHashes,tools:toolInputs(REPO),identity:e.identity}),/PRIMARY_CURRENT_STABLE_STATUS/);});
 test('fresh primary rejects a self-hashed but unapproved identity argument',()=>{assert.throws(()=>validatePrimaryThread(r,{repo:e.sourceRepository,toolchainAuthorityRepo:e.toolchainAuthorityRepository,sourceHashes:e.sourceHashes,tools:toolInputs(REPO),identity:{...e.identity,sha256:'0'.repeat(64)}}),/PRIMARY_APPROVED_IDENTITY/);});
 test('fresh primary rejects incomplete parent runtime closure',()=>{const hashes={...e.sourceHashes};delete hashes['native/virtual_controller_start_runtime.h'];assert.throws(()=>validatePrimaryThread(r,{repo:e.sourceRepository,toolchainAuthorityRepo:e.toolchainAuthorityRepository,sourceHashes:hashes,tools:toolInputs(REPO),identity:e.identity}),/PRIMARY_CONTEXT_SOURCE_CLOSURE/);});
}
