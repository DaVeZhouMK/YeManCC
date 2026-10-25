// Seal only the bounded managed evidence; intentionally never grants lifecycle/driver acceptance.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const root=path.resolve(process.argv[2]??'G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostManaged23');
const get=relative=>JSON.parse(fs.readFileSync(path.join(root,relative),'utf8').replace(/^\uFEFF/,''));
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const current=get('current-005/result.json');assert.equal(current.status,'PASS');assert(current.sourceStable);
const commands=get('current-005/commands.json');assert(commands.every(r=>r.status===0));
const known=[];
for(const p of current.profiles){assert.equal(p.exit,0);assert.equal(p.result.cases.length,18);assert(p.result.cases.every(r=>r.pass));const gaps=p.result.cases.filter(r=>r.knownFailureReproduced);assert.equal(gaps.length,2);assert(gaps.every(r=>r.resourceContractPass===false&&r.loopExit===0&&r.mainFinallyReleaseOk===false));known.push({config:p.config,cases:gaps});}
const originals=current.inputs.map(p=>({path:p.path,sha256:p.sha256,currentSha256:hash(p.path)}));assert(originals.every(r=>r.currentSha256===r.sha256),'Current source/tool identity drift');
const negativeSpecs=[
 {dir:'negative-late-output-001',fault:'accept-late-output',name:'QUIESCE revokes captured callback; late frames rejected; ordered release',error:'revoked delegate revived pending output'},
 {dir:'negative-output-stop-001',fault:'skip-output-stop',name:'output barrier refuses release and same-owner retry succeeds',unsafeUnmap:true},
 {dir:'negative-idle-stop-001',fault:'skip-idle-stop',name:'production metadata refuses fake SDK without cancellation',error:'neutral raced idle clock'},
 {dir:'negative-parent-cancel-001',fault:'ignore-parent-cancellation',name:'pipe-read-loss',error:'TimeoutException'}
];
const negatives=negativeSpecs.map(spec=>{
 const r=get(spec.dir+'/result.json');assert.equal(r.status,'FAIL');assert.equal(r.fault,spec.fault);assert(r.sourceStable);assert(get(spec.dir+'/commands.json').filter(c=>c.label.endsWith('-build')).every(c=>c.status===0));
 const profiles=r.profiles.map(p=>{assert.equal(p.exit,1);const row=p.result.cases.find(c=>c.name===spec.name);assert(row&&!row.pass,'Named semantic rejection missing');if(spec.error)assert(row.error.includes(spec.error));if(spec.unsafeUnmap)assert(row.beforeFixtureCleanup.UnsafeFakeUnmaps>0,'Blocked output did not reach unsafe fake teardown');return {config:p.config,exit:p.exit,rejectedCase:row.name,error:row.error,resources:row.beforeFixtureCleanup};});
 return {fault:spec.fault,path:path.join(root,spec.dir,'result.json'),profiles};
});
const legacy=get('legacy-adapted-001/result.json');assert.equal(legacy.status,'PASS');assert(legacy.currentSourceStable);
for(const p of legacy.profiles){assert.equal(p.exit,0);assert.equal(p.result.cases.length,19);assert.equal(p.result.legacyFailures.length,3);}
const originalBuild=path.join(root,'existing-original-001/Release-build.log');assert(fs.readFileSync(originalBuild,'utf8').includes('error CS0103'));assert(fs.readFileSync(originalBuild,'utf8').includes('UsbipMaintenanceGate'));
const tools=['tools/input_host_managed_lifecycle_selftest.mjs','tools/input_host_managed_lifecycle_legacy_replay.mjs','tools/input_host_managed_lifecycle_evidence.mjs','tools/fixtures/input_host_managed_lifecycle_20261023/ManagedCases.cs','tools/fixtures/input_host_managed_lifecycle_20261023/Workers.cs.in'].map(relative=>({path:path.join(repo,relative),sha256:hash(path.join(repo,relative))}));
const receipts=['current-005/result.json','current-005/inputs-before.json','current-005/consumer-extractions.json','current-005/toolchain.json','current-005/commands.json','legacy-adapted-001/result.json','legacy-adapted-001/commands.json','existing-original-001/Release-build.log',...negativeSpecs.flatMap(s=>[s.dir+'/result.json',s.dir+'/commands.json'])].map(relative=>({path:path.join(root,relative),sha256:hash(path.join(root,relative))}));
const result={schemaVersion:1,recordedAt:new Date().toISOString(),verificationStatus:'PASS_BOUNDED_ASSERTIONS_AND_EXPECTED_NEGATIVE_REJECTIONS',lifecycleAcceptance:'INCOMPLETE',fullParentGoalComplete:false,currentSourceIdentityVerified:true,productionFilesEdited:[],positive:{path:path.join(root,'current-005/result.json'),profiles:['Release','Debug'],passedHealthyCasesPerProfile:16,reproducedOpenGapsPerProfile:2},knownManagedResourceContractFailures:known,semanticNegatives:negatives,legacy:{unmodifiedOriginalEntry:'BUILD_FAILED_RETAINED',buildLog:originalBuild,boundaryAdapted:path.join(root,'legacy-adapted-001/result.json'),currentCasesPerProfile:19,originalAssertionFailuresPerProfile:3,originalWholeLegacyExitPerProfile:1},tools,receipts,sourceIdentity:originals,boundaries:{realBclOwnedThreadsEventsCts:true,realBclPipesAndOriginalParentProcessHandle:true,sdkMetadataViaExplicitIRuntimeSeam:true,realSdkAssemblyLoaded:false,productMainExecuted:false,installedApplicationOrDriverOperated:false,pnpEndpointAndBackendRemoval:'FAKE_ONLY',usbipMaintenanceMutexAndServices:'INERT_TERMINAL',neutralAndFrameBackend:'INERT_TERMINAL',reportChainMappingAndDiagnosticWorkers:'CALL_ORDER_ONLY'},parentGaps:['Choose and prove terminal fallback when parent loss leaves blocked worker after both managed release attempts; do not count fixture process exit as cleanup.','Native primary-thread HANDLE owner fix and its process/job fallback remain in parent task.','Actual SDK constructor/report loop, mapping/backend/PnP removal, USB/IP and physical-driver behavior, real sleep/recovery remain unproved.','Real report-chain mappings, diagnostic worker lifetime and HIDHIDE_CYCLE background task are outside this bounded fixture.','Global lifecycle registration/aggregate acceptance and installed/hardware verification were not changed or claimed.']};
const target=path.join(root,'DELIVERY.json');fs.writeFileSync(target,JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({verificationStatus:result.verificationStatus,lifecycleAcceptance:result.lifecycleAcceptance,healthyCasesPerConfig:16,knownGapsPerConfig:2,negatives:negatives.length,legacyCasesPerConfig:19,currentSourceIdentityVerified:true,delivery:target}));
