// Exact private source preparation for the shared-input-power29 consumer.
// Membership and static adaptation never grant compile or runtime authority.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash}from'node:crypto';import{fileURLToPath}from'node:url';
import{readLifecycleLogData}from'./fixtures/input_coordinator_lifecycle/lifecycle-log-data.mjs';
import{SNAPSHOT_MANIFEST,validateSourceSnapshotRoots}from'./ymcc_sim/global/source-snapshot.mjs';
const repo=fileURLToPath(new URL('../',import.meta.url)),relative='tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal29-compile-source-profile.json',PROFILE='720f2c3ec61da702ab5cc37962ebda1ba8a96fcc936f9e7b371c9d24d3c3c1e3',CONTRACT='current105-terminal29-shared-input-power-v1',sha=b=>createHash('sha256').update(b).digest('hex');
const settledRelative='tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal29-gpd-settled-fixture-inputs.json';
const SETTLED_PROFILE='75d0250cec717a101aa286f0a3147ede567f4e0260d4328154b6af19224b0642';
const settledTerminalProducers=[
 {bytes:248151,sha256:'453e2ea6bc60aad3dbb4fa1260ac08b12f17a9c611467a65dc9a55650eae8d87'},
 {bytes:248319,sha256:'3187dcd64d94b9acd7768f56ef7fdfca3bd02a290b9174f5d8a58346a549e6c0'},
 {bytes:248835,sha256:'ac421b065c90f9360e40e2b94db7785000e375e7fa195c3476d804fc53f1bdff'},
];
// The PS suspend case now tests cancellation and revision-only pause separately.
const psSuspendFixture={
 fixture:{relative:'tools/fixtures/input_coordinator_lifecycle/fixture.cpp',
  original:{bytes:366363,sha256:'0b659d6fc1fc37cbf2d3361ba006a7b4c26d4497c6b4bc319f2d57c174b86a08'},
  projected:{bytes:368367,sha256:'6442162a1cbb09fd291fdbe377f935fd0634b461cc6b69095d4c0ac59bf00734'}},
 pipeCases:{relative:'tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc',
  original:{bytes:272266,sha256:'2c5bbf2f3e93ee3a91a4a828f7a2c34f911b9f8d5b9cb8273cadb37167ec8c44'}},
 psWakeCases:{relative:'tools/fixtures/input_coordinator_lifecycle/ps_wake_cases.inc',
  bytes:42143,sha256:'f2a4ff71f7aff350c44889ac0a3872f9c2364aea3fbf6389626e6ae93d9bbee9'},
 producerData:{bytes:254181,sha256:'3076a761f49136045ff14302e92add5ac5411ef49f1d77f985898ed973bae6bb',
  locations:[{sourceLine:701,sourceUtf8Byte:102546},{sourceLine:810,sourceUtf8Byte:118771}]},
};
// The exit-receipt-only update does not enter the original full-pipe case list.
const psExitReceiptFixture={...psSuspendFixture,
 pipeCases:{...psSuspendFixture.pipeCases,
  original:{bytes:272648,sha256:'66725a98d64f4fdb8228f6c3488b612100ec332acea402367be025ee78d5c2e0'}},
 producerData:{bytes:254601,sha256:'02f4a46bc0eb646812d4ac5cba90836821c76b75a18c0b8a9058c4380c434fb7',
  locations:[{sourceLine:702,sourceUtf8Byte:102727},{sourceLine:811,sourceUtf8Byte:118952}]},
};
// The directed ROG case exercises delivery through the real public projection.
const publicReadyFixture={...psExitReceiptFixture,
 pipeCases:{...psExitReceiptFixture.pipeCases,
  original:{bytes:278494,sha256:'ce78a7536c9a1423a9911173125656ef66d0da702a179f9e338607c0e7d04a8c'}},
 producerData:{bytes:257520,sha256:'46f48da7348abf61774fab5f70a34f05b38130b82f2aaf4f1d0205b6788c0530',
  locations:[{sourceLine:743,sourceUtf8Byte:105646},{sourceLine:852,sourceUtf8Byte:121871}]},
};
// This successor only replaces the fixture's HidHide registry reads.
const publicReadyHidHideFixture={...publicReadyFixture,
 fixture:{...publicReadyFixture.fixture,
  original:{bytes:367574,sha256:'c9b68aa06a945bdda7067c89b804cfdc4ed69f1be1128bdf66872be703043697'},
  projected:{bytes:369578,sha256:'57f370b9d75005369196ed9f3d4ed69ca0fd26e77bd28bf4a039de142d3ffc8f'}},
 header:{relative:'native/hidhide_installed_version.h',bytes:3400,sha256:'073dd38a3e7a1143bc37e9d7d57614ac56ebfc2100142dcfe709d78bba259bb6'},
};
// The already-OFF wake variant is directed-only; it does not enter the Terminal29 modes.
const publicReadyAlreadyOffFixture={...publicReadyHidHideFixture,
 fixture:{...publicReadyHidHideFixture.fixture,
  original:{bytes:372277,sha256:'4d3384626a96db0f1eda2551e6f622e5b6a68aee229d127e0ad8a47a6eb9337e'},
  projected:{bytes:374281,sha256:'d4dd620e49bb9e5d978a00d2ac2194de021a263622fb942b219337bcaa8e8625'}}};
// Directed cases added outside both accepted log literals. Their selection stays unchanged.
const publicReadyDirectedProducer={bytes:259158,sha256:'158bc756e0b163bd9dc0967619d95dbb3ab8d6e98539f8415bcd0f1ce0da4024',
 locations:[{sourceLine:753,sourceUtf8Byte:106541},{sourceLine:862,sourceUtf8Byte:122766}]};
// The public Ready-only IPC projection changed; both default log row pins are intact.
const publicReadyIpcProducer={bytes:259306,sha256:'6e1e54e8e8fc7d5b40cf693d393b82ddd7c5d9bad4325cea15f914109fa5ddc4',
 locations:[{sourceLine:755,sourceUtf8Byte:106689},{sourceLine:864,sourceUtf8Byte:122914}]};
// BUS fixture binding and ADLX copy removal leave both log literals unchanged.
const publicReadyBusProducer={bytes:259552,sha256:'28ed6ce62a203c8023dcba586b16d5931dac0d8537010e0db000d17ce8b22a30',
 locations:[{sourceLine:756,sourceUtf8Byte:106774},{sourceLine:865,sourceUtf8Byte:122999}]};
const steamRouteStatusHeaders=[
 {relative:'native/steam_original_recovery_runtime.h',bytes:120398,
  sha256:'1aea9f3a528ded1da119aaf13729fda4ea28f361955b8541dfbf5f3a3cfbafe2'},
 {relative:'native/steam_original_recovery_runtime.h',bytes:120876,
  sha256:'8b855beec8b4f6fbafc331dc2c3d0124a57269d20508265f81d5d801cab812a5'},
];
export {relative as terminal29CompileProfileRelative,settledRelative as terminal29SettledProfileRelative};
function exact(root,row){assert(row&&typeof row.relative==='string'&&!path.isAbsolute(row.relative)&&!row.relative.includes('\\')&&row.relative.split('/').every(p=>p!=='.'&&p!=='..'),'TERMINAL29_RELATIVE_SOURCE_REQUIRED');const file=path.join(root,row.relative),st=fs.lstatSync(file);assert(st.isFile()&&!st.isSymbolicLink(),'TERMINAL29_REGULAR_SOURCE_REQUIRED');assert.equal(path.resolve(fs.realpathSync(file)).toLowerCase(),path.resolve(file).toLowerCase(),'TERMINAL29_REDIRECTED_SOURCE');const bytes=fs.readFileSync(file);assert.equal(bytes.length,row.bytes,'TERMINAL29_SOURCE_SIZE:'+row.relative);assert.equal(sha(bytes),row.sha256,'TERMINAL29_SOURCE_SHA:'+row.relative);return bytes;}
const key=file=>path.resolve(file).toLowerCase();
function verifyCaptureRoot(sourceRoot,pins){
 const same=key(sourceRoot)===key(repo),st=fs.lstatSync(sourceRoot);
 assert(st.isDirectory()&&!st.isSymbolicLink()&&key(fs.realpathSync.native(sourceRoot))===key(sourceRoot),'TERMINAL29_PLAIN_SOURCE_ROOT_REQUIRED');
 if(same)return;
 const bounds=validateSourceSnapshotRoots(repo,sourceRoot);
 assert.notEqual(key(path.dirname(bounds.snapshotRoot)),key(path.dirname(bounds.sourceRoot)),'TERMINAL29_EXACT_CORE_CAPTURE_REQUIRED');
 const file=path.join(sourceRoot,SNAPSHOT_MANIFEST);
 assert(key(fs.realpathSync.native(file))===key(file)&&fs.lstatSync(file).isFile(),'TERMINAL29_PLAIN_MANIFEST_REQUIRED');
 const capture=JSON.parse(fs.readFileSync(file));
 assert(capture.status==='PASS'&&key(capture.sourceRoot)===key(repo)&&key(capture.snapshotRoot)===key(sourceRoot),'TERMINAL29_CAPTURE_MANIFEST_REQUIRED');
 for(const pin of pins){
  const rows=capture.files.filter(row=>row.path===pin.relative);
  assert.equal(rows.length,1,'TERMINAL29_UNIQUE_CAPTURE_PROFILE_REQUIRED');
  exact(sourceRoot,pin);
  assert(rows[0].size===pin.bytes&&rows[0].sha256===pin.sha256,'TERMINAL29_CAPTURE_PROFILE_CHANGED');
 }
}
function bindSettledFixture(profile,sourceRoot,pins){
 const fixtureBytes=fs.statSync(path.join(sourceRoot,profile.fixture.relative)).size;
 if(fixtureBytes===profile.fixture.bytes)return null;
 const settledBytes=fs.readFileSync(path.join(repo,settledRelative));
 assert.equal(sha(settledBytes),SETTLED_PROFILE,'TERMINAL29_SETTLED_PROFILE_CHANGED');
 const settled=JSON.parse(settledBytes);
 assert.equal(settled.schemaVersion,1);assert.equal(settled.kind,'TERMINAL29_GPD_SETTLED_FIXTURE_INPUT_SUCCESSOR');
 assert.equal(settled.priorProfileSha256,PROFILE);
 const hidHideRegistry=[publicReadyHidHideFixture,publicReadyAlreadyOffFixture].find(row=>fixtureBytes===row.fixture.original.bytes);
 const psSuspend=Boolean(hidHideRegistry)||fixtureBytes===psSuspendFixture.fixture.original.bytes;
 const pipeBytes=psSuspend?fs.statSync(path.join(sourceRoot,psSuspendFixture.pipeCases.relative)).size:0;
 const psExitReceipt=pipeBytes===psExitReceiptFixture.pipeCases.original.bytes;
 const publicReady=pipeBytes===publicReadyFixture.pipeCases.original.bytes;
 if(hidHideRegistry)assert(publicReady,'TERMINAL29_HIDHIDE_REGISTRY_PUBLIC_READY_FIXTURE_PAIR_REQUIRED');
 const {fixture,pipeCases,producerData:previousProducer}=hidHideRegistry??(publicReady?publicReadyFixture:psExitReceipt?psExitReceiptFixture:psSuspend?psSuspendFixture:settled);
 const producerBytes=fs.statSync(path.join(sourceRoot,profile.producerData.relative)).size;
 const producerData=(publicReady?[publicReadyDirectedProducer,publicReadyIpcProducer,publicReadyBusProducer].find(row=>row.bytes===producerBytes):undefined)??previousProducer;
 assert.equal(fixture.relative,profile.fixture.relative);
 assert.equal(profile.fixtureSourceOverrides.filter(row=>row.relative===pipeCases.relative).length,1);
 assert.deepEqual(profile.lifecycleInputRepair.changes,[]);
 const withIdentity=(relative,value)=>({relative,...value});
 const replacements=new Map([[fixture.relative,fixture.original],[pipeCases.relative,pipeCases.original]]);
 if(psSuspend){const {relative,...identity}=psSuspendFixture.psWakeCases;replacements.set(relative,identity);}
 assert([...replacements].every(([relative])=>profile.inputs.filter(row=>row.relative===relative).length===1));
 profile.inputs=profile.inputs.map(row=>replacements.has(row.relative)?withIdentity(row.relative,replacements.get(row.relative)):row);
 profile.fixture=withIdentity(fixture.relative,fixture.original);
 profile.lifecycleInputRepair={before:fixture.projected,after:fixture.projected,changes:[]};
 profile.fixtureSourceOverrides=profile.fixtureSourceOverrides.map(row=>row.relative!==pipeCases.relative?row:
  psSuspend?{...row,before:pipeCases.original,after:pipeCases.original,changes:[]}:
   {...row,before:pipeCases.original,after:pipeCases.projected});
 assert.equal(producerData.locations.length,profile.producerData.literals.length);
 profile.producerData={...profile.producerData,bytes:producerData.bytes,sha256:producerData.sha256,
  literals:profile.producerData.literals.map((row,index)=>({...row,...producerData.locations[index]}))};
 const pin={relative:settledRelative,bytes:settledBytes.length,sha256:SETTLED_PROFILE};
 pins.push(pin);
 if(!psSuspend)return pin;
 const capturedInputs=[...Array.from(replacements,([relative,identity])=>withIdentity(relative,identity)),
  {relative:profile.producerData.relative,bytes:producerData.bytes,sha256:producerData.sha256}];
 if(hidHideRegistry){
  const header=hidHideRegistry.header;
  exact(sourceRoot,header);
  capturedInputs.push(header);
 }
 pins.push(...capturedInputs);
 return {...pin,revision:publicReady?'PUBLIC_READY_DELIVERY':psExitReceipt?'PS_EXIT_RECEIPT':'PS_SUSPEND_SCOPE',capturedInputs,
   ...(hidHideRegistry?{hidHideRegistryHeader:hidHideRegistry.header}:{})};
}
export function terminal29CompileSourceProfile(sourceRoot){
 assert(path.isAbsolute(sourceRoot),'TERMINAL29_ABSOLUTE_SOURCE_ROOT');const bytes=fs.readFileSync(path.join(repo,relative));assert.equal(sha(bytes),PROFILE,'TERMINAL29_PROFILE_CHANGED');const profile=JSON.parse(bytes);assert.equal(profile.kind,'CURRENT105_TERMINAL29_SHARED_POWER_COMPILE_SOURCE_V1');assert.equal(profile.managedInputContract,CONTRACT);assert.deepEqual(profile.sourceCapture,{binding:'G_CORE_WORKSPACE_OWNER_V1',fullSourceExpectationApproved:false});
 const pins=[{relative,bytes:bytes.length,sha256:PROFILE}];
 const fixtureSuccessor=bindSettledFixture(profile,sourceRoot,pins);
 // The optional GPD projections live after both immutable log literals.
 // Length selects a pin; the parser still checks the whole file and both rows.
 if(fixtureSuccessor&&!fixtureSuccessor.revision){
  const bytes=fs.statSync(path.join(sourceRoot,profile.producerData.relative)).size;
  const producer=settledTerminalProducers.find(row=>row.bytes===bytes);
  if(producer)profile.producerData={...profile.producerData,...producer};
 }
 // Steam entry body pins remain unchanged for the reviewed status variants.
 let runtimeHeaderSuccessor;
 const steamRouteStatusHeader=fixtureSuccessor?.revision==='PUBLIC_READY_DELIVERY'&&steamRouteStatusHeaders.find(
  row=>row.bytes===fs.statSync(path.join(sourceRoot,profile.runtimeHeader.relative)).size);
 if(steamRouteStatusHeader){
  exact(sourceRoot,steamRouteStatusHeader);
  assert.equal(profile.inputs.filter(row=>row.relative===steamRouteStatusHeader.relative).length,1);
  runtimeHeaderSuccessor={before:profile.runtimeHeader,after:steamRouteStatusHeader};
  profile.runtimeHeader=steamRouteStatusHeader;
  profile.inputs=profile.inputs.map(row=>row.relative===steamRouteStatusHeader.relative?steamRouteStatusHeader:row);
  pins.push(steamRouteStatusHeader);
 }
 verifyCaptureRoot(sourceRoot,pins);
 exact(repo,profile.baselineCompileProfile);for(const row of profile.inputs)exact(sourceRoot,row);readLifecycleLogData(sourceRoot,profile.producerData);assert.equal(profile.sourceApproval,false);assert.equal(profile.publicRuntime,false);
 return {...profile,nativeGuardProvenance:{actualManagedInputContract:CONTRACT,nativeSpecManagedInputContract:CONTRACT,nativeSpecKind:profile.kind,nativeSpecPath:path.join(repo,relative),nativeSpecSha256:PROFILE,...(fixtureSuccessor?{fixtureSuccessor}:{}),...(runtimeHeaderSuccessor?{runtimeHeaderSuccessor}:{}),reusedNativeSpec:false,compileOnly:true,runtimeAuthorityGranted:false,sourceApprovalGranted:false},steamRecovery:{runtimeHeader:{...profile.runtimeHeader,path:path.join(sourceRoot,profile.runtimeHeader.relative)},startSha256:profile.startSha256,exportSnapshotSha256:profile.exportSnapshotSha256,bridgeSha256:profile.bridgeSha256}};
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
