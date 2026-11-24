// Exact private source preparation for the shared-input-power29 consumer.
// Membership and static adaptation never grant compile or runtime authority.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash}from'node:crypto';import{fileURLToPath}from'node:url';
import{readLifecycleLogData}from'./fixtures/input_coordinator_lifecycle/lifecycle-log-data.mjs';
import{SNAPSHOT_MANIFEST,validateSourceSnapshotRoots}from'./ymcc_sim/global/source-snapshot.mjs';
const repo=fileURLToPath(new URL('../',import.meta.url)),relative='tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal29-compile-source-profile.json',PROFILE='720f2c3ec61da702ab5cc37962ebda1ba8a96fcc936f9e7b371c9d24d3c3c1e3',CONTRACT='current105-terminal29-shared-input-power-v1',sha=b=>createHash('sha256').update(b).digest('hex');
export {relative as terminal29CompileProfileRelative};
function exact(root,row){assert(row&&typeof row.relative==='string'&&!path.isAbsolute(row.relative)&&!row.relative.includes('\\')&&row.relative.split('/').every(p=>p!=='.'&&p!=='..'),'TERMINAL29_RELATIVE_SOURCE_REQUIRED');const file=path.join(root,row.relative),st=fs.lstatSync(file);assert(st.isFile()&&!st.isSymbolicLink(),'TERMINAL29_REGULAR_SOURCE_REQUIRED');assert.equal(path.resolve(fs.realpathSync(file)).toLowerCase(),path.resolve(file).toLowerCase(),'TERMINAL29_REDIRECTED_SOURCE');const bytes=fs.readFileSync(file);assert.equal(bytes.length,row.bytes,'TERMINAL29_SOURCE_SIZE:'+row.relative);assert.equal(sha(bytes),row.sha256,'TERMINAL29_SOURCE_SHA:'+row.relative);return bytes;}
const key=file=>path.resolve(file).toLowerCase();
function verifyCaptureRoot(sourceRoot,profileBytes){
 const same=key(sourceRoot)===key(repo),st=fs.lstatSync(sourceRoot);
 assert(st.isDirectory()&&!st.isSymbolicLink()&&key(fs.realpathSync.native(sourceRoot))===key(sourceRoot),'TERMINAL29_PLAIN_SOURCE_ROOT_REQUIRED');
 if(same)return;
 const bounds=validateSourceSnapshotRoots(repo,sourceRoot);
 assert.notEqual(key(path.dirname(bounds.snapshotRoot)),key(path.dirname(bounds.sourceRoot)),'TERMINAL29_EXACT_CORE_CAPTURE_REQUIRED');
 const file=path.join(sourceRoot,SNAPSHOT_MANIFEST);
 assert(key(fs.realpathSync.native(file))===key(file)&&fs.lstatSync(file).isFile(),'TERMINAL29_PLAIN_MANIFEST_REQUIRED');
 const capture=JSON.parse(fs.readFileSync(file));
 assert(capture.status==='PASS'&&key(capture.sourceRoot)===key(repo)&&key(capture.snapshotRoot)===key(sourceRoot),'TERMINAL29_CAPTURE_MANIFEST_REQUIRED');
 const rows=capture.files.filter(row=>row.path===relative);
 assert.equal(rows.length,1,'TERMINAL29_UNIQUE_CAPTURE_PROFILE_REQUIRED');
 exact(sourceRoot,{relative,bytes:profileBytes.length,sha256:PROFILE});
 assert(rows[0].size===profileBytes.length&&rows[0].sha256===PROFILE,'TERMINAL29_CAPTURE_PROFILE_CHANGED');
}
export function terminal29CompileSourceProfile(sourceRoot){
 assert(path.isAbsolute(sourceRoot),'TERMINAL29_ABSOLUTE_SOURCE_ROOT');const bytes=fs.readFileSync(path.join(repo,relative));assert.equal(sha(bytes),PROFILE,'TERMINAL29_PROFILE_CHANGED');const profile=JSON.parse(bytes);assert.equal(profile.kind,'CURRENT105_TERMINAL29_SHARED_POWER_COMPILE_SOURCE_V1');assert.equal(profile.managedInputContract,CONTRACT);assert.deepEqual(profile.sourceCapture,{binding:'G_CORE_WORKSPACE_OWNER_V1',fullSourceExpectationApproved:false});verifyCaptureRoot(sourceRoot,bytes);
 exact(repo,profile.baselineCompileProfile);for(const row of profile.inputs)exact(sourceRoot,row);readLifecycleLogData(sourceRoot,profile.producerData);assert.equal(profile.sourceApproval,false);assert.equal(profile.publicRuntime,false);
 return {...profile,nativeGuardProvenance:{actualManagedInputContract:CONTRACT,nativeSpecManagedInputContract:CONTRACT,nativeSpecKind:profile.kind,nativeSpecPath:path.join(repo,relative),nativeSpecSha256:PROFILE,reusedNativeSpec:false,compileOnly:true,runtimeAuthorityGranted:false,sourceApprovalGranted:false},steamRecovery:{runtimeHeader:{...profile.runtimeHeader,path:path.join(sourceRoot,profile.runtimeHeader.relative)},startSha256:profile.startSha256,exportSnapshotSha256:profile.exportSnapshotSha256,bridgeSha256:profile.bridgeSha256}};
}
function transform(original,entry){assert.equal(sha(original),entry.before.sha256,'TERMINAL29_REPAIR_INPUT_CHANGED');assert.equal(Buffer.byteLength(original),entry.before.bytes);let text=original.toString('utf8');for(const change of entry.changes){assert.equal(text.split(change.before).length,2,'TERMINAL29_EXACT_REPAIR_ANCHOR');text=text.replace(change.before,()=>change.after);}assert.equal(sha(text),entry.after.sha256,'TERMINAL29_REPAIR_OUTPUT_CHANGED');let reverse=text;for(const change of [...entry.changes].reverse()){assert.equal(reverse.split(change.after).length,2);reverse=reverse.replace(change.after,()=>change.before);}assert.equal(reverse,original.toString('utf8'));return text;}
function bindRadialFixturePower(text,sourceRoot){
 const relative='native/virtual_controller_ui_output_runtime.h';
 const header=fs.readFileSync(path.join(sourceRoot,relative));
 const signature=[...header.toString('utf8').matchAll(/^static void radialInputIsolationOwnership[^\r\n]+/gm)].map(row=>row[0]);
 assert.equal(signature.length,1,'TERMINAL29_UNIQUE_RADIAL_OWNER_DECLARATION_REQUIRED');
 const legacy='static void radialInputIsolationOwnership(bool active) {';
 const shared='static void radialInputIsolationOwnership(bool active,const ymcc::sleeppower::SharedPowerV1& power) {';
 assert(signature[0]===legacy||signature[0]===shared,'TERMINAL29_RADIAL_OWNER_DECLARATION_CHANGED');
 const before='CachedRadialOwnerScope(){radialInputIsolationOwnership(true);}';
 assert.equal(text.split(before).length,2,'TERMINAL29_EXACT_RADIAL_FIXTURE_OWNER_REQUIRED');
 const after='CachedRadialOwnerScope(){ymcc::sleeppower::SharedPowerV1 power; if(!captureSharedPowerLifecycle(power))throw std::logic_error("foreground-neutral-public-power-capture-failed"); radialInputIsolationOwnership(true,power);}';
 const usesSharedPower=signature[0]===shared;
 const result=usesSharedPower?text.replace(before,()=>after):text;
 if(usesSharedPower)assert.equal(result.replace(after,()=>before),text);
 return {text:result,receipt:{relative,headerSha256:sha(header),usesSharedPower,inputSha256:sha(text),outputSha256:sha(result)}};
}
export function repairTerminal29Observation(observation,options){const p=terminal29CompileSourceProfile(options.sourceRoot);assert.equal(observation.originalSha256,p.fixture.sha256);const text=transform(observation.text,p.lifecycleInputRepair);const radial=bindRadialFixturePower(text,options.sourceRoot);return{...observation,text:radial.text,radialFixturePower:radial.receipt,lifecycleFixtureInputRepair:{kind:'EXACT_TERMINAL29_EXISTING_INPUT_REPAIR',inputSha256:p.lifecycleInputRepair.before.sha256,outputSha256:p.lifecycleInputRepair.after.sha256,changes:p.lifecycleInputRepair.changes.map(r=>({label:r.label})),sourceCaptureModified:false,assertionsRemoved:0,fullParentAccepted:false}};}
export function repairTerminal29Tuple(original,options){const p=terminal29CompileSourceProfile(options.sourceRoot);return Buffer.from(transform(original,p.tupleInputRepair));}
