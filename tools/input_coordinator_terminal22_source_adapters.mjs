// Explicit newer compile guard only; this specification grants no runtime authority.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const profileFile=fileURLToPath(new URL('./ymcc_sim/global/controller-coordinator-pipe/r3-terminal22-native-source-profile.json',import.meta.url));
const PROFILE_SHA256='f6916a908291a9246d3638278fa6579e173330a4a1a4b1278ecff1a491ac8107';
export function terminal22SteamCompileSpec(sourceRoot,managedInputContract='current105-terminal22-v1'){
 assert(managedInputContract==='current105-terminal22-v1'||managedInputContract==='current105-terminal23-sd-decoder-v1','UNKNOWN_NATIVE_GUARD_MANAGED_CONTRACT');
 assert(typeof sourceRoot==='string'&&path.isAbsolute(sourceRoot),'TERMINAL22_ABSOLUTE_CAPTURE_SOURCE_REQUIRED');
 assert.equal(hash(fs.readFileSync(profileFile)),PROFILE_SHA256,'TERMINAL22_NATIVE_PROFILE_CHANGED');
 const profile=JSON.parse(fs.readFileSync(profileFile,'utf8'));
 assert.equal(profile.kind,'CURRENT105_TERMINAL22_STEAM_COMPILE_GUARD_V1');
 assert.equal(profile.managedInputContract,'current105-terminal22-v1');
 const baseline=fileURLToPath(new URL('./ymcc_sim/global/controller-coordinator-pipe/r3-current105-source-adapter-profile.json',import.meta.url));
 assert.equal(hash(fs.readFileSync(baseline)),profile.baselineProfileSha256,'TERMINAL22_BASELINE_PROFILE_CHANGED');
 for(const row of [profile.runtimeHeader,...profile.extraCompileHeaders]){
  assert(typeof row.relative==='string'&&!path.isAbsolute(row.relative)&&!row.relative.includes('..')&&!row.relative.includes('\\'),'TERMINAL22_NATIVE_HEADER_PATH_REQUIRED');
  assert.equal(hash(fs.readFileSync(path.join(sourceRoot,row.relative))),row.sha256,'TERMINAL22_REVIEWED_NATIVE_HEADER_CHANGED:'+row.relative);
 }
 // The profile itself remains terminal22. Only the caller's managed decoder membership differs.
 return {managedInputContract,nativeGuardProvenance:{actualManagedInputContract:managedInputContract,
  nativeSpecManagedInputContract:profile.managedInputContract,nativeSpecKind:profile.kind,
  nativeSpecPath:profileFile,nativeSpecSha256:PROFILE_SHA256,
  reusedNativeSpec:managedInputContract!==profile.managedInputContract,
  scope:managedInputContract==='current105-terminal23-sd-decoder-v1'
   ?'Managed decoder membership only; reuses the unchanged pinned terminal22 native compile-only rejection specification'
   :'Original explicit terminal22 native compile-only rejection specification',
  compileOnly:true,runtimeAuthorityGranted:false},
  steamRecovery:{runtimeHeader:{...profile.runtimeHeader,path:path.join(sourceRoot,profile.runtimeHeader.relative)},startSha256:profile.startSha256,bridgeSha256:profile.bridgeSha256}};
}

// A separate current23 compile-only specification. Old22 identity is never rewritten.
const PROFILE23_SHA256='0fdd87e8578789f1235c7b13b1e63d50f38592af2c582474393a9ad40ac953f4';
const profile23File=fileURLToPath(new URL('./ymcc_sim/global/controller-coordinator-pipe/r3-terminal23-compile-source-profile.json',import.meta.url));
export function terminal23CompileSourceProfile(sourceRoot){
 assert(typeof sourceRoot==='string'&&path.isAbsolute(sourceRoot),'TERMINAL23_ABSOLUTE_CAPTURE_SOURCE_REQUIRED');
 assert.equal(hash(fs.readFileSync(profile23File)),PROFILE23_SHA256,'TERMINAL23_COMPILE_PROFILE_CHANGED');
 const profile=JSON.parse(fs.readFileSync(profile23File,'utf8'));
 assert.equal(profile.kind,'CURRENT105_TERMINAL23_COMPILE_SOURCE_GUARD_V1');assert.equal(profile.managedInputContract,'current105-terminal23-sd-decoder-v1');
 assert.equal(profile.baselineNativeSpecSha256,PROFILE_SHA256);assert.equal(hash(fs.readFileSync(profileFile)),PROFILE_SHA256,'TERMINAL22_BASELINE_NATIVE_PROFILE_CHANGED');
 const baseline=fileURLToPath(new URL('./ymcc_sim/global/controller-coordinator-pipe/r3-current105-source-adapter-profile.json',import.meta.url));
 assert.equal(hash(fs.readFileSync(baseline)),profile.baselineSourceProfileSha256,'TERMINAL23_BASELINE_SOURCE_PROFILE_CHANGED');
 for(const row of [profile.runtimeHeader,...profile.extraCompileHeaders,...profile.extraCompileSources]){
  assert(typeof row.relative==='string'&&!path.isAbsolute(row.relative)&&!row.relative.includes('..')&&!row.relative.includes('\\'),'TERMINAL23_COMPILE_INPUT_PATH_REQUIRED');
  assert.equal(hash(fs.readFileSync(path.join(sourceRoot,row.relative))),row.sha256,'TERMINAL23_REVIEWED_COMPILE_INPUT_CHANGED:'+row.relative);
 }
 assert.equal(profile.review.compileOnly,true);assert.equal(profile.review.publicRuntimeAccepted,false);assert.equal(profile.review.actualCompilerLedgerStillRequired,true);
 return {...profile,nativeGuardProvenance:{actualManagedInputContract:profile.managedInputContract,nativeSpecManagedInputContract:profile.managedInputContract,
  nativeSpecKind:profile.kind,nativeSpecPath:profile23File,nativeSpecSha256:PROFILE23_SHA256,baselineNativeSpecSha256:profile.baselineNativeSpecSha256,reusedNativeSpec:false,
  scope:'Current23 compile-only reviewed entry denial and fixture observation data; neither runtime nor full source semantic approval',compileOnly:true,runtimeAuthorityGranted:false},
  steamRecovery:{runtimeHeader:{...profile.runtimeHeader,path:path.join(sourceRoot,profile.runtimeHeader.relative)},startSha256:profile.startSha256,bridgeSha256:profile.bridgeSha256,exportSnapshotSha256:profile.exportSnapshotSha256}};
}
