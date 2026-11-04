import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {guardCurrentSteam,observeCurrent105Fixture,currentCompileSourceDataProfile} from './input_coordinator_current105_source_adapters.mjs';
import {terminal22SteamCompileSpec,terminal23CompileSourceProfile} from './input_coordinator_terminal22_source_adapters.mjs';
import {currentLifecycleLogBoundary} from './input_coordinator_current_compile_only.mjs';
import {readVirtualControllerSource} from './virtual_controller_runtime_source.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const capture=process.env.YMCC_TERMINAL22_SOURCE_ROOT||root;
const original=readVirtualControllerSource(path.join(capture,'native/main.cpp')).source;
const options={sourceRoot:capture,managedInputContract:'current105-terminal22-v1'};
test('explicit terminal22 keeps both original executing entries blocked',()=>{
 const guarded=guardCurrentSteam(original,options);
 assert.equal(guarded.substitutions.length,2);
 assert(guarded.substitutions.every(row=>row.rejectOnly));
 assert(guarded.inert.includes('#define steamOriginalRecoveryStart fixtureNeverCallOriginalSteamRecoveryStart'));
 assert(guarded.inert.includes('static void steamOriginalRecoveryStart(){++forbiddenIo;throw std::logic_error("unexpected-offline-steam-recovery-start");}'));
 assert(guarded.inert.includes('throw std::logic_error("unexpected-offline-steam-recovery-engine")'));
});
test('default historical guard still refuses the newer runtime header',()=>assert.throws(()=>guardCurrentSteam(original)));
test('an explicit legacy label cannot admit the new header',()=>assert.throws(()=>guardCurrentSteam(original,{...options,managedInputContract:'current105-exact21-v1'})));
test('unknown versions cannot borrow the newer compile guard',()=>{
 for(const managedInputContract of ['terminal22','current105-terminal22-v2',null,22])assert.throws(()=>guardCurrentSteam(original,{...options,managedInputContract}),/UNKNOWN_COMPILE_SOURCE_CONTRACT/);
});
test('terminal22 requires the captured absolute source root',()=>{
 for(const sourceRoot of [undefined,'relative-source','',null])assert.throws(()=>terminal22SteamCompileSpec(sourceRoot),/ABSOLUTE_CAPTURE_SOURCE_REQUIRED/);
});
test('missing or repeated original Start include cannot pass the wrapper',()=>{
 const include='#include "steam_original_recovery_runtime.h"';
 assert.throws(()=>guardCurrentSteam(original.replace(include,''),options));
 assert.throws(()=>guardCurrentSteam(original+'\n'+include,options));
});
test('changing the engine bridge is rejected before its replacement',()=>{
 const marker='static json steamOriginalRecoveryEngineHandle(';
 const at=original.indexOf(marker);assert(at>=0);
 const brace=original.indexOf('{',at);assert(brace>at);
 assert.throws(()=>guardCurrentSteam(original.slice(0,brace+1)+' /* unreviewed bridge */ '+original.slice(brace+1),options));
});
test('changed header bytes are refused, even when the guarded entry names still exist',()=>{
 const evidence=path.resolve(root,'../../../Testing/YMCC/Evidence/VC-Current105Terminal22-23');
 const owned=fs.mkdtempSync(path.join(evidence,'guard-header-negative-'));fs.mkdirSync(path.join(owned,'native'));
 for(const name of ['steam_original_recovery_runtime.h','steam_original_recovery_dual_route.h'])fs.copyFileSync(path.join(capture,'native',name),path.join(owned,'native',name));
 fs.appendFileSync(path.join(owned,'native/steam_original_recovery_runtime.h'),'\n// unreviewed header change\n');
 assert.throws(()=>terminal22SteamCompileSpec(owned),/REVIEWED_NATIVE_HEADER_CHANGED/);
});

test('terminal23 current compile guard: real entries fixture and original log decoder remain exact',()=>{
 const id='current105-terminal23-sd-decoder-v1',opts={sourceRoot:capture,managedInputContract:id},spec=terminal23CompileSourceProfile(capture),proof=spec.nativeGuardProvenance;
 assert.equal(spec.kind,'CURRENT105_TERMINAL23_COMPILE_SOURCE_GUARD_V1');assert.equal(spec.managedInputContract,id);
 assert.equal(proof.actualManagedInputContract,id);assert.equal(proof.nativeSpecManagedInputContract,id);assert.equal(proof.reusedNativeSpec,false);
 assert.equal(proof.compileOnly,true);assert.equal(proof.runtimeAuthorityGranted,false);assert.equal(spec.review.publicRuntimeAccepted,false);assert.equal(spec.review.actualCompilerLedgerStillRequired,true);
 for(const [name,sha] of [['r3-terminal22-native-source-profile.json','f6916a908291a9246d3638278fa6579e173330a4a1a4b1278ecff1a491ac8107'],['r3-current105-source-adapter-profile.json','8c8a5803990da252418f91be0dc5df13b89efd589b14a197f962e12ba9943674']])assert.equal(createHash('sha256').update(fs.readFileSync(new URL('./ymcc_sim/global/controller-coordinator-pipe/'+name,import.meta.url))).digest('hex'),sha);
 const guarded=guardCurrentSteam(original,opts);assert.deepEqual(guarded.nativeGuardProvenance,proof);
 assert.deepEqual(guarded.substitutions.map(row=>row.symbol),['steamOriginalRecoveryStart','steamOriginalRecoveryEngineHandle','steamOriginalRecoveryExportSnapshot']);assert(guarded.substitutions.every(row=>row.rejectOnly));
 for(const [name,suffix,alias] of [['steamOriginalRecoveryStart','start','fixtureNeverCallOriginalSteamRecoveryStart'],['steamOriginalRecoveryExportSnapshot','export','fixtureNeverCallOriginalSteamRecoveryExportSnapshot']]){
  assert(guarded.inert.includes('#define '+name+' '+alias));
  assert(guarded.inert.includes('++forbiddenIo;throw std::logic_error("unexpected-offline-steam-recovery-'+suffix+'");}'));
 }
 assert(guarded.inert.includes('throw std::logic_error("unexpected-offline-steam-recovery-engine")'));
 const fixture=fs.readFileSync(path.join(capture,spec.fixture.relative),'utf8'),observed=observeCurrent105Fixture(fixture,opts);
 assert.equal(observed.originalBusinessChecksChanged,false);assert.equal(observed.originalSha256,spec.fixture.sha256);assert(observed.text.includes('fixtureOffCheckpoint("assertion",name,good?1:0);'));
 let inverse=observed.text;for(const row of [...spec.fixtureObservationChanges].reverse()){
  const before=row.before.replaceAll('\r\n','\n'),after=row.after.replaceAll('\r\n','\n');const lf=inverse.includes(after),actual=lf?after:after.replaceAll('\n','\r\n');
  for(let i=0;i<row.count;i++){assert(inverse.includes(actual));inverse=inverse.replace(actual,()=>lf?before:before.replaceAll('\n','\r\n'));}
 }assert.equal(inverse,fixture);
 const log=currentLifecycleLogBoundary(capture,{managedInputContract:id});assert.equal(Buffer.byteLength(log),905);assert.equal(createHash('sha256').update(log).digest('hex'),spec.producerData.decodedUtf8Sha256);
 assert(log.startsWith('const auto& detail=observed;'));assert.equal(Buffer.byteLength(log.slice(28)),877);assert.equal(createHash('sha256').update(log.slice(28)).digest('hex'),'fee1e275715d037b953baa1b8471e42fd4e0ea2f3be78557e80b25b6d43ef7fb');
});
test('terminal23 current compile guard: unknown IDs and stale legacy data stay refused',()=>{
 const id='current105-terminal23-sd-decoder-v1';for(const managedInputContract of ['terminal23','current105-terminal23-v1','current105-terminal23-sd-decoder-v2',null,23,{id},new String(id)]){
  assert.throws(()=>currentCompileSourceDataProfile({sourceRoot:capture,managedInputContract}),/UNKNOWN_COMPILE_SOURCE_CONTRACT/);assert.throws(()=>guardCurrentSteam(original,{sourceRoot:capture,managedInputContract}),/UNKNOWN_COMPILE_SOURCE_CONTRACT/);
 }
 for(const sourceRoot of [undefined,'relative-source','',null])assert.throws(()=>terminal23CompileSourceProfile(sourceRoot),/TERMINAL23_ABSOLUTE_CAPTURE_SOURCE_REQUIRED/);
 const legacy=currentCompileSourceDataProfile();assert.equal(legacy.fixture.sha256,'7078261d04d0378b0b81b7132fd000244d3791fbbff614f53a119761a5062537');const fixture=fs.readFileSync(path.join(capture,legacy.fixture.relative),'utf8');
 assert.throws(()=>observeCurrent105Fixture(fixture,{sourceRoot:capture,managedInputContract:'current105-terminal22-v1'}),/Exact captured current fixture/);
 assert.throws(()=>currentLifecycleLogBoundary(capture,{managedInputContract:'current105-terminal22-v1'}),/CURRENT_LOG_DATA_VALUE_CHANGED/);
});
test('terminal23 current compile guard: drifted inputs anchors fixture and log literal reject',()=>{
 const id='current105-terminal23-sd-decoder-v1',spec=terminal23CompileSourceProfile(capture),temp=process.env.YMCC_TERMINAL22_TEST_TEMP;assert(temp&&path.isAbsolute(temp),'OWNED_TEST_TEMP_REQUIRED');
 const owned=fs.mkdtempSync(path.join(temp,'guard23-'));try{
  const inputs=[spec.runtimeHeader,...spec.extraCompileHeaders,...spec.extraCompileSources];for(const row of [...inputs,spec.fixture,spec.producerData]){const file=path.join(owned,row.relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.copyFileSync(path.join(capture,row.relative),file);}
  assert.equal(terminal23CompileSourceProfile(owned).nativeGuardProvenance.nativeSpecSha256,spec.nativeGuardProvenance.nativeSpecSha256);
  for(const row of inputs){const file=path.join(owned,row.relative);fs.appendFileSync(file,'\n// unreviewed compiler input\n');assert.throws(()=>terminal23CompileSourceProfile(owned),/TERMINAL23_REVIEWED_COMPILE_INPUT_CHANGED/);fs.copyFileSync(path.join(capture,row.relative),file);}
  const opts={sourceRoot:owned,managedInputContract:id},include='#include "steam_original_recovery_runtime.h"';assert.throws(()=>guardCurrentSteam(original.replace(include,''),opts));assert.throws(()=>guardCurrentSteam(original+'\n'+include,opts));
  const at=original.indexOf('static json steamOriginalRecoveryEngineHandle(');assert(at>=0);const brace=original.indexOf('{',at);assert(brace>at);assert.throws(()=>guardCurrentSteam(original.slice(0,brace+1)+' /* drift */ '+original.slice(brace+1),opts));
  const fixture=fs.readFileSync(path.join(owned,spec.fixture.relative),'utf8');assert.throws(()=>observeCurrent105Fixture(fixture+'\n// drift',opts),/Exact captured current fixture/);
  const file=path.join(owned,spec.producerData.relative),producer=fs.readFileSync(file,'utf8');assert(producer.includes('const auto& detail=observed;'));fs.writeFileSync(file,producer.replace('const auto& detail=observed;','const auto& detail=unexpected;'));assert.throws(()=>currentLifecycleLogBoundary(owned,{managedInputContract:id}),/CURRENT_LOG_DATA_VALUE_CHANGED/);
 }finally{assert.equal(path.dirname(path.resolve(owned)),path.resolve(temp));assert(path.basename(owned).startsWith('guard23-'));fs.rmSync(owned,{recursive:true,force:true});}assert.equal(fs.existsSync(owned),false);
});
