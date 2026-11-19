// Pinned compile-only profile with an explicit reviewed default logger contract.
// Presence of this module grants no runtime or full source approval.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {readLifecycleLogData} from './fixtures/input_coordinator_lifecycle/lifecycle-log-data.mjs';
export {TERMINAL26_MSI_POLICY_SUCCESSOR,terminal26MsiPolicySuccessorFiles,pairTerminal26MsiPolicyProducts,admitTerminal26MsiPolicyTopology,pairTerminal26MsiPolicyFixture,pairTerminal26MsiPolicyIo} from './input_coordinator_msi_policy_source.mjs';
export {TERMINAL26_MSI_POLICY_FIXTURE_INPUT,terminal26MsiPolicyFixtureInputFiles} from './input_coordinator_msi_policy_source.mjs';
export {TERMINAL26_MSI_POLICY_SUCCESSOR_R7,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5} from './input_coordinator_msi_policy_source.mjs';

const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const repo=fileURLToPath(new URL('../',import.meta.url));
const profileRelative='tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-compile-source-profile-v2.json';
const profileFile=path.join(repo,profileRelative);
const PROFILE_SHA256='69d5eac31101aafe95126f1ba53f152560341d39ac0865a13e99f87ad96c4b2d';
const BASELINE23_SHA256='0fdd87e8578789f1235c7b13b1e63d50f38592af2c582474393a9ad40ac953f4';
const CONTRACT='current105-terminal26-sdk-lifetime-compact-v1';
export const TERMINAL26_CURRENT_NATIVE_SOURCE_PROFILE='current105-terminal26-current-capture-20261117a';
const CURRENT_PROFILE_RELATIVE='tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-compile-source-profile-current-20261117a.json';
export function terminal26NativeSourceProfileFiles(selection){
 if(selection===undefined)return [];
 assert.equal(typeof selection,'string','TERMINAL26_NATIVE_SOURCE_PROFILE_STRING_REQUIRED');
 assert.equal(selection,TERMINAL26_CURRENT_NATIVE_SOURCE_PROFILE,'TERMINAL26_UNKNOWN_NATIVE_SOURCE_PROFILE');
 return [path.join(repo,CURRENT_PROFILE_RELATIVE)];
}
export const TERMINAL26_PRODUCT_SUCCESSOR='terminal-cleanup-handoff-20261115';
const SUCCESSOR_RELATIVE='tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-product-source-successor-v1.json';
const SUCCESSOR_SHA256='ae011adde47132880c1d2d65d0a3f6fbcc64453c99073bfde0d7f1808f44d17b';
export const TERMINAL26_MSI_CLEANUP_SUCCESSOR='msi-quarantine-cleanup-r2-20261115';
const MSI_OWNER_ROOT='G:/YeManCC-Work/Testing/YMCC/Evidence/VC-ROG-CommonSelfHeal-20261108-194834/msi-common-cleanup-review-20261115';
export function terminal26MsiCleanupSuccessorFiles(selection){
 if(selection===undefined)return [];
 assert.equal(selection,TERMINAL26_MSI_CLEANUP_SUCCESSOR,'TERMINAL26_UNKNOWN_MSI_HEADER_SUCCESSOR');
 return ['CANDIDATE-r2.json','INDEPENDENT-REVIEW-r2.json','source-before/native/msi_native_foreground_isolation_runtime.h','candidate-r2/native/msi_native_foreground_isolation_runtime.h'].map(relative=>path.join(MSI_OWNER_ROOT,relative));
}
export function reviewedTerminal26MsiCleanupHeader(original,{sourceRoot,managedInputContract,productHeaderSuccessor}={}){
 if(productHeaderSuccessor===undefined)return {bytes:original,productHeaderSuccessor:null};
 const inputs=terminal26MsiCleanupSuccessorFiles(productHeaderSuccessor);
 assert.equal(managedInputContract,CONTRACT,'TERMINAL26_MSI_HEADER_CONTRACT_REQUIRED');
 const root='G:/YeManCC-Work/Testing/YMCC/Workspace/R105T26-20261114162102/Mainline/YeManCC-source/YeManCC';
 assert.equal(path.resolve(sourceRoot),path.resolve(root),'TERMINAL26_MSI_HEADER_BASE_REQUIRED');
 assert.equal(fs.realpathSync(sourceRoot),fs.realpathSync(root),'TERMINAL26_MSI_HEADER_REAL_BASE_REQUIRED');
 const expected=[
  {bytes:2008,sha256:'79178d9d3486c56bbefc223664c9f4f8cb4517845e84892ee311f1dbefad37d3'},
  {bytes:2053,sha256:'6d234048fb301631ca63c6c080c08a6fc6927db214b172ef1387bfec6c376e24'},
  {bytes:155320,sha256:'9321685e2e02c53ae23274ac726d50f0eeec53429d44131e3202150c43e1be03'},
  {bytes:155556,sha256:'e6a9ef846f9629c664d2cd7a628be417281a13285d5f67a63cad263035055944'}];
 const data=inputs.map((file,i)=>{const stat=fs.lstatSync(file);assert(stat.isFile()&&!stat.isSymbolicLink());const bytes=fs.readFileSync(file);assert.equal(bytes.length,expected[i].bytes,'TERMINAL26_MSI_HEADER_INPUT_SIZE');assert.equal(sha(bytes),expected[i].sha256,'TERMINAL26_MSI_HEADER_INPUT_CHANGED');return bytes;});
 assert(Buffer.isBuffer(original)&&original.equals(data[2]),'TERMINAL26_MSI_HEADER_ORIGINAL_CHANGED');
 const candidate=JSON.parse(data[0]),review=JSON.parse(data[1]);assert.equal(candidate.sourceReady,false);assert.equal(candidate.mainlineImported,false);
 assert.equal(candidate.files.length,1);assert.equal(candidate.files[0].candidate.sha256,expected[3].sha256);assert.equal(review.candidateSha256,expected[3].sha256);assert.equal(review.blockingFinding,false);
 const before=original.toString('utf8'),eol=before.includes('\r\n')?'\r\n':'\n';
 const anchor='        return inputHostNativeVisibilityColdReturnOrQueueLocked("before-virtual-work");';
 const insertion=['        // Exact retained return work owns this endpoint debt, not cold history.',
  '        if(reason=="quarantine-endpoint-absent" &&',
  '                (g_inputHostReleaseQuarantined || g_msiSteamDeckReleaseDebt.required()))return true;',anchor].join(eol);
 assert.equal(before.split(anchor).length,2,'TERMINAL26_MSI_HEADER_ANCHOR_CHANGED');const after=before.replace(anchor,()=>insertion);
 assert(Buffer.from(after).equals(data[3]),'TERMINAL26_MSI_HEADER_EXACT_CANDIDATE_REQUIRED');assert.equal(after.replace(insertion,()=>anchor),before);
 const relative='native/msi_native_foreground_isolation_runtime.h';readPinned(sourceRoot,{relative,...expected[2]});
 return {bytes:data[3],productHeaderSuccessor:{id:productHeaderSuccessor,relative,before:expected[2],after:expected[3],
  inputs:inputs.map((file,i)=>({path:file,...expected[i]})),inverseVerified:true,productSourceSemanticChange:true,sourceCaptureModified:false,
  candidateSourceReady:false,mainlineImported:false,publicRuntimeAccepted:false,physicalAccepted:false,
  scope:'Exact private r2 candidate admits only original quarantine cleanup; all original endpoint/physical-return/Start checks remain. No behavior validation is granted by selection.'}};
}

// This is an explicitly selected product semantic successor, not an I/O port.
// Its fixed base capture and prior evidence remain byte-for-byte immutable.
export function reviewedTerminal26ProductSourceSuccessor(source,{sourceRoot,managedInputContract,productSourceSuccessor}={}){
 if(productSourceSuccessor===undefined)return {source,productSourceSuccessor:null};
 assert.equal(productSourceSuccessor,TERMINAL26_PRODUCT_SUCCESSOR,'TERMINAL26_UNKNOWN_PRODUCT_SUCCESSOR');
 assert.equal(managedInputContract,CONTRACT,'TERMINAL26_PRODUCT_SUCCESSOR_CONTRACT_REQUIRED');
 const bytes=readPinned(repo,{relative:SUCCESSOR_RELATIVE,bytes:5892,sha256:SUCCESSOR_SHA256});
 const profile=JSON.parse(bytes.toString('utf8'));
 assert.equal(profile.schemaVersion,1);assert.equal(profile.kind,'TERMINAL26_EXACT_REVIEWED_PRODUCT_SUCCESSOR_V1');
 assert.equal(profile.id,productSourceSuccessor);assert.equal(profile.managedInputContract,CONTRACT);
 assert.equal(path.resolve(sourceRoot),path.resolve(profile.baseSourceRoot),'TERMINAL26_SUCCESSOR_BASE_ROOT_REQUIRED');
 assert.equal(fs.realpathSync(sourceRoot),fs.realpathSync(profile.baseSourceRoot),'TERMINAL26_SUCCESSOR_REAL_ROOT_REQUIRED');
 const pinnedAbsolute=row=>{
  assert(row&&path.isAbsolute(row.path),'TERMINAL26_SUCCESSOR_ABSOLUTE_PIN_REQUIRED');
  const stat=fs.lstatSync(row.path);assert(stat.isFile()&&!stat.isSymbolicLink(),'TERMINAL26_SUCCESSOR_REGULAR_PIN_REQUIRED');
  const actual=fs.readFileSync(row.path);
  assert.equal(actual.length,row.bytes,'TERMINAL26_SUCCESSOR_PIN_SIZE_CHANGED');
  assert.equal(sha(actual),row.sha256,'TERMINAL26_SUCCESSOR_PIN_CHANGED');
  return actual;
 };
 assert.equal(profile.baseCompileProfile.sha256,PROFILE_SHA256);
 assert.equal(path.resolve(profile.baseCompileProfile.path),path.resolve(profileFile));
 pinnedAbsolute(profile.baseCompileProfile);
 const owner=JSON.parse(pinnedAbsolute(profile.ownerSourceReady).toString('utf8'));
 const patch=JSON.parse(pinnedAbsolute(profile.ownerPatch).toString('utf8'));
 assert.equal(owner.scopedSourceReady,true);assert.equal(owner.fullMainBuild,false);
 assert.equal(owner.patch.sha256,profile.ownerPatch.sha256);
 assert.equal(profile.products.length,2);assert.equal(patch.hunks.length,2);
 assert.deepEqual(profile.products.map(row=>row.relative),['native/virtual_recovery_fallback_runtime.h','native/virtual_runtime_recovery_runtime.h']);
 assert.equal(sha(source),profile.expandedBeforeSha256,'TERMINAL26_SUCCESSOR_EXPANDED_BASE_CHANGED');
 let repaired=source;const hunks=[];
 for(const row of profile.products){
  const original=readPinned(sourceRoot,{relative:row.relative,...row.before});
  const old=Buffer.from(row.beforeBase64,'base64').toString('utf8'),next=Buffer.from(row.afterBase64,'base64').toString('utf8');
  const declared=patch.hunks.find(hunk=>hunk.path===row.relative);
  assert(declared&&declared.oldBase64===row.beforeBase64&&declared.newBase64===row.afterBase64,'TERMINAL26_SUCCESSOR_OWNER_HUNK_CHANGED');
  assert.equal(original.toString('utf8').split(old).length,2,'TERMINAL26_SUCCESSOR_HEADER_ANCHOR_NOT_UNIQUE');
  const product=original.toString('utf8').replace(old,()=>next);
  assert.equal(Buffer.byteLength(product),row.after.bytes);assert.equal(sha(product),row.after.sha256);
  assert.equal(product.split(next).length,2);assert.equal(product.replace(next,()=>old),original.toString('utf8'));
  assert.equal(repaired.split(old).length,2,'TERMINAL26_SUCCESSOR_EXPANDED_ANCHOR_NOT_UNIQUE');
  repaired=repaired.replace(old,()=>next);hunks.push({relative:row.relative,before:row.before,after:row.after,beforeHunkSha256:sha(old),afterHunkSha256:sha(next)});
 }
 assert.equal(sha(repaired),profile.expandedAfterSha256,'TERMINAL26_SUCCESSOR_EXPANDED_OUTPUT_CHANGED');
 let inverse=repaired;
 for(const row of [...profile.products].reverse()){
  const old=Buffer.from(row.beforeBase64,'base64').toString('utf8'),next=Buffer.from(row.afterBase64,'base64').toString('utf8');
  assert.equal(inverse.split(next).length,2,'TERMINAL26_SUCCESSOR_INVERSE_ANCHOR_NOT_UNIQUE');inverse=inverse.replace(next,()=>old);
 }
 assert.equal(inverse,source,'TERMINAL26_SUCCESSOR_INVERSE_MISMATCH');
 return {source:repaired,productSourceSuccessor:{id:profile.id,kind:profile.kind,profile:{path:path.join(repo,SUCCESSOR_RELATIVE),bytes:bytes.length,sha256:SUCCESSOR_SHA256},
  baseSourceRoot:profile.baseSourceRoot,baseCompileProfile:profile.baseCompileProfile,ownerSourceReady:profile.ownerSourceReady,ownerPatch:profile.ownerPatch,
  expandedBeforeSha256:profile.expandedBeforeSha256,expandedAfterSha256:profile.expandedAfterSha256,hunks,inverseVerified:true,
  productSourceSemanticChange:true,sourceCaptureModified:false,managedRebuilt:false,sourceApprovalGranted:false,publicRuntimeAccepted:false,physicalAccepted:false}};
}

function readPinned(root,row){
 assert(row&&typeof row.relative==='string'&&row.relative.length>0&&!path.isAbsolute(row.relative)&&
  !row.relative.includes('\\')&&row.relative.split('/').every(part=>/^[A-Za-z0-9_.-]+$/.test(part)&&part!=='.'&&part!=='..'),
  'TERMINAL26_INPUT_PATH_REQUIRED');
 assert(/^[a-f0-9]{64}$/.test(row.sha256??'')&&Number.isSafeInteger(row.bytes)&&row.bytes>=0,'TERMINAL26_INPUT_PIN_REQUIRED');
 const file=path.join(root,row.relative),stat=fs.lstatSync(file);
 assert(stat.isFile()&&!stat.isSymbolicLink(),'TERMINAL26_INPUT_NOT_REGULAR:'+row.relative);
 const relative=path.relative(fs.realpathSync(root),fs.realpathSync(file));
 assert(!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),'TERMINAL26_INPUT_ESCAPED_ROOT');
 const bytes=fs.readFileSync(file);
 assert.equal(bytes.length,row.bytes,'TERMINAL26_INPUT_SIZE_CHANGED:'+row.relative);
 assert.equal(sha(bytes),row.sha256,'TERMINAL26_REVIEWED_INPUT_CHANGED:'+row.relative);
 return bytes;
}

export function terminal26CompileSourceProfile(sourceRoot,{nativeSourceProfile}={}){
 if(nativeSourceProfile!==undefined){
  terminal26NativeSourceProfileFiles(nativeSourceProfile);
  return currentTerminal26CompileSourceProfile(sourceRoot);
 }
 assert(typeof sourceRoot==='string'&&path.isAbsolute(sourceRoot),'TERMINAL26_ABSOLUTE_CAPTURE_SOURCE_REQUIRED');
 const bytes=fs.readFileSync(profileFile);
 assert.equal(sha(bytes),PROFILE_SHA256,'TERMINAL26_COMPILE_PROFILE_CHANGED');
 const profile=JSON.parse(bytes.toString('utf8'));
 assert.equal(profile.schemaVersion,1);
 assert.equal(profile.kind,'CURRENT105_TERMINAL26_COMPILE_SOURCE_GUARD_V2');
 assert.equal(profile.managedInputContract,CONTRACT);
 assert.equal(profile.baselineCompileProfile.sha256,BASELINE23_SHA256);
 readPinned(repo,profile.baselineCompileProfile);readPinned(repo,profile.predecessorProfile);
 for(const row of profile.baselineProfiles)readPinned(repo,row);
 assert.equal(path.resolve(sourceRoot),path.resolve(profile.sourceCapture.root),'TERMINAL26_FIXED_CAPTURE_ROOT_REQUIRED');
 assert.equal(fs.realpathSync(sourceRoot),fs.realpathSync(profile.sourceCapture.root),'TERMINAL26_FIXED_CAPTURE_REAL_ROOT_REQUIRED');
 const inputs=[profile.runtimeHeader,...profile.extraCompileHeaders,...profile.extraCompileSources,profile.fixture,profile.producerData,profile.bridgeSource,...profile.fixtureSourceOverrides.map(row=>row.before)];
 assert.equal(inputs.length,14);assert.equal(new Set(inputs.map(row=>row.relative)).size,inputs.length,'TERMINAL26_DUPLICATE_INPUT');
 for(const row of inputs)readPinned(sourceRoot,row);
 for(const row of profile.adapterDependencies)readPinned(repo,row);
 assert.equal(profile.sourceApproval,false);assert.equal(profile.publicRuntime,false);
 assert.equal(profile.review.compileOnly,true);assert.equal(profile.review.sourceApprovalGranted,false);
 assert.equal(profile.review.publicRuntimeAccepted,false);assert.equal(profile.review.actualCompilerLedgerStillRequired,true);
 // Version1 keeps its original one-row/905-byte gate. This exact version2
 // contract verifies the whole producer and both default/directed literals.
 try{readLifecycleLogData(sourceRoot,profile.producerData);}
 catch(cause){throw new Error('TERMINAL26_SOURCE_REVIEW_OPEN:PRODUCER_DATA:'+cause.message,{cause});}
 assert.equal(profile.review.profileUsable,true,'TERMINAL26_SOURCE_REVIEW_OPEN');
 assert.equal(profile.review.status,'CLOSED','TERMINAL26_SOURCE_REVIEW_OPEN');
 return {...profile,sourceApproval:false,publicRuntime:false,
  nativeGuardProvenance:{actualManagedInputContract:CONTRACT,nativeSpecManagedInputContract:CONTRACT,
   nativeSpecKind:profile.kind,nativeSpecPath:profileFile,nativeSpecSha256:PROFILE_SHA256,
   baselineCompileProfileSha256:BASELINE23_SHA256,baselineNativeSpecSha256:profile.baselineProfiles[0].sha256,
   reusedNativeSpec:false,scope:'Only the listed fixed-capture inputs and three throw-only entry substitutions; full source semantics and public runtime remain unapproved',
   compileOnly:true,runtimeAuthorityGranted:false,sourceApprovalGranted:false,publicRuntimeAccepted:false},
  steamRecovery:{runtimeHeader:{...profile.runtimeHeader,path:path.join(sourceRoot,profile.runtimeHeader.relative)},
   startSha256:profile.startSha256,exportSnapshotSha256:profile.exportSnapshotSha256,bridgeSha256:profile.bridgeSha256}};
}


// A separate finite current-capture guard. The historical V2 branch above is unchanged.
const CURRENT_PROFILE_SHA256='66c18e7ebed3f4ae0bae8725daa4ee97e9ff04bde3162dc7b9bd56d57fca66fe';
const CURRENT_CAPTURE_ROOT='G:/YeManCC-Work/Testing/YMCC/Workspace/R105CP-20261117A/Mainline/YeManCC-source/YeManCC';
const CURRENT_INPUT_RELATIVES=Object.freeze(["native/steam_original_recovery_runtime.h","native/steam_original_recovery_policy.h","native/steam_original_recovery_dual_route.h","native/steam_original_recovery_config.h","native/steam_original_recovery_wake_watch.h","native/steam_original_recovery_storage.h","native/steam_original_recovery_worker.h","native/steam_original_recovery_engine.cpp","native/steam_original_recovery_engine.h","tools/fixtures/input_coordinator_lifecycle/fixture.cpp","tools/input_coordinator_lifecycle_selftest.mjs","native/main.cpp","tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc","tools/fixtures/input_coordinator_lifecycle/prepare_ack_cases.inc","tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc"]);
function currentTerminal26CompileSourceProfile(sourceRoot){
 assert(typeof sourceRoot==='string'&&path.isAbsolute(sourceRoot),'TERMINAL26_CURRENT_ABSOLUTE_CAPTURE_REQUIRED');
 const file=path.join(repo,CURRENT_PROFILE_RELATIVE),bytes=readPinned(repo,{relative:CURRENT_PROFILE_RELATIVE,bytes:16442,sha256:CURRENT_PROFILE_SHA256});
 const profile=JSON.parse(bytes.toString('utf8'));
 assert.equal(profile.schemaVersion,1);assert.equal(profile.kind,'CURRENT105_TERMINAL26_CURRENT_CAPTURE_COMPILE_SOURCE_GUARD_V1');
 assert.equal(profile.id,TERMINAL26_CURRENT_NATIVE_SOURCE_PROFILE);assert.equal(profile.managedInputContract,CONTRACT);
 assert.equal(profile.sourceCapture.root,CURRENT_CAPTURE_ROOT);
 assert.equal(path.resolve(sourceRoot),path.resolve(CURRENT_CAPTURE_ROOT),'TERMINAL26_CURRENT_CAPTURE_ROOT_REQUIRED');
 assert.equal(fs.realpathSync(sourceRoot),fs.realpathSync(CURRENT_CAPTURE_ROOT),'TERMINAL26_CURRENT_CAPTURE_REAL_ROOT_REQUIRED');
 assert.equal(profile.predecessorProfile.sha256,PROFILE_SHA256);assert.equal(profile.predecessorProfile.relative,profileRelative);
 readPinned(repo,profile.predecessorProfile);assert.equal(profile.baselineCompileProfile.sha256,BASELINE23_SHA256);
 readPinned(repo,profile.baselineCompileProfile);for(const row of profile.baselineProfiles)readPinned(repo,row);
 const capture=profile.sourceCapture,manifest=JSON.parse(readPinned(sourceRoot,capture.manifest).toString('utf8'));
 assert.equal(manifest.schemaVersion,1);assert.equal(manifest.kind,capture.manifestKind);assert.equal(manifest.status,'PASS');
 assert.equal(manifest.policy.version,capture.policyVersion);assert.equal(manifest.files.length,capture.fileCount);
 assert.equal(manifest.copyVerifiedFiles,capture.fileCount);assert.equal(manifest.executionPerformed,false);
 assert.equal(manifest.snapshotPassApprovesChangedSource,false);
 assert.equal(path.resolve(manifest.snapshotRoot),path.resolve(sourceRoot),'TERMINAL26_CURRENT_MANIFEST_ROOT_REQUIRED');
 assert.equal(path.resolve(manifest.sourceRoot),path.resolve(capture.primaryRoot),'TERMINAL26_CURRENT_MANIFEST_PRIMARY_REQUIRED');
 const node=JSON.parse(readPinned(sourceRoot,capture.nodeToolsBinding).toString('utf8'));
 assert.equal(node.status,'PASS');assert.equal(node.expectedSnapshotManifestSha256,capture.manifest.sha256);
 assert.equal(path.resolve(node.snapshotRoot),path.resolve(sourceRoot),'TERMINAL26_CURRENT_NODE_CAPTURE_REQUIRED');
 const overrides=profile.fixtureSourceOverrides;
 assert.deepEqual(overrides.map(row=>row.relative),CURRENT_INPUT_RELATIVES.slice(12,14));
 for(const row of overrides){assert.equal(row.mode,'exact-reviewed-after-identity-v1');assert.equal(row.before.relative,row.relative);assert.deepEqual(row.before,row.after);assert.deepEqual(row.changes,[]);}
 const tuple=profile.tupleFixture;assert.equal(tuple.relative,CURRENT_INPUT_RELATIVES[14]);assert.equal(tuple.before.relative,tuple.relative);
 assert.equal(tuple.mode,'exact-original13-after-identity-v1');assert.deepEqual(tuple.before,tuple.after);assert.deepEqual(tuple.changes,[]);assert.equal(tuple.originalRepairCount,13);
 const inputs=[profile.runtimeHeader,...profile.extraCompileHeaders,...profile.extraCompileSources,profile.fixture,profile.producerData,profile.bridgeSource,...overrides.map(row=>row.before),tuple.before];
 assert.deepEqual(inputs.map(row=>row.relative),CURRENT_INPUT_RELATIVES,'TERMINAL26_CURRENT_EXACT15_INPUTS_REQUIRED');
 assert.equal(new Set(inputs.map(row=>row.relative)).size,15);
 for(const row of inputs){
  readPinned(sourceRoot,row);
  const found=manifest.files.filter(entry=>entry.path===row.relative);assert.equal(found.length,1,'TERMINAL26_CURRENT_MANIFEST_MEMBER_REQUIRED:'+row.relative);
  assert.equal(found[0].size,row.bytes);assert.equal(found[0].sha256,row.sha256,'TERMINAL26_CURRENT_MANIFEST_MEMBER_PIN_REQUIRED:'+row.relative);
 }
 for(const row of profile.adapterDependencies)readPinned(repo,row);
 assert.deepEqual(profile.fixtureObservationChanges.map(row=>row.count),[1,1,1,2]);
 const identity=profile.lifecycleFixtureInputRepair;assert.equal(identity.kind,'EXACT_CURRENT_CAPTURE_ALREADY_REPAIRED_IDENTITY_V1');
 assert.deepEqual(identity.input,profile.fixtureObservedOutput);assert.deepEqual(identity.output,profile.fixtureObservedOutput);
 assert.deepEqual(identity.changes,[]);assert.equal(identity.originalRepairCount,17);assert.equal(identity.alreadyAppliedInCapture,true);
 assert.equal(profile.sourceApproval,false);assert.equal(profile.sourceReady,false);assert.equal(profile.publicRuntime,false);assert.equal(profile.fullSourceExpectationApproved,false);
 assert.equal(profile.review.status,'SCOPED_COMPILE_SOURCE_GUARD_ONLY');assert.equal(profile.review.compileOnly,true);assert.equal(profile.review.sourceReady,false);
 assert.equal(profile.review.sourceApprovalGranted,false);assert.equal(profile.review.publicRuntimeAccepted,false);assert.equal(profile.review.actualCompilerLedgerStillRequired,true);
 assert.equal(profile.review.fullParentAccepted,false);assert.equal(profile.review.currentPrimaryAccepted,false);
 readLifecycleLogData(sourceRoot,profile.producerData);
 const selection={id:profile.id,kind:profile.kind,profile:{path:file,bytes:bytes.length,sha256:CURRENT_PROFILE_SHA256},
  sourceCapture:{root:sourceRoot,manifest:{...capture.manifest,path:path.join(sourceRoot,capture.manifest.relative)},nodeToolsBinding:{...capture.nodeToolsBinding,path:path.join(sourceRoot,capture.nodeToolsBinding.relative)}},
  inputCount:inputs.length,compileOnly:true,sourceReady:false,sourceApprovalGranted:false,publicRuntimeAccepted:false,currentPrimaryAccepted:false,fullParentAccepted:false};
 return {...profile,nativeSourceProfile:selection,
  nativeGuardProvenance:{actualManagedInputContract:CONTRACT,nativeSpecManagedInputContract:CONTRACT,nativeSpecKind:profile.kind,nativeSpecPath:file,nativeSpecSha256:CURRENT_PROFILE_SHA256,
   baselineCompileProfileSha256:profile.baselineCompileProfile.sha256,predecessorCompileProfileSha256:profile.predecessorProfile.sha256,baselineNativeSpecSha256:profile.baselineProfiles[0].sha256,reusedNativeSpec:false,nativeSourceProfile:selection,
   scope:'Only the exact current capture and fifteen compile-source roles; three throw-only entries and pinned fixture mappings. No full source or runtime qualification.',
   compileOnly:true,runtimeAuthorityGranted:false,sourceReady:false,sourceApprovalGranted:false,publicRuntimeAccepted:false},
  steamRecovery:{runtimeHeader:{...profile.runtimeHeader,path:path.join(sourceRoot,profile.runtimeHeader.relative)},startSha256:profile.startSha256,exportSnapshotSha256:profile.exportSnapshotSha256,bridgeSha256:profile.bridgeSha256}};
}
