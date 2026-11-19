import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import test from 'node:test';
import crypto from 'node:crypto';
import {getExtraAdapters} from '../extra-adapters.mjs';
import {MAX_ARTIFACT_INPUTS,validateAdapters} from '../runner.mjs';
import {ID,MODULE,REPO,SELFTEST,DEFAULT_TOOLCHAIN,TOOLCHAIN_ENV,resolveToolchain,getPnpAbsenceAdapters} from './adapter.mjs';
import {run,verifyResult} from './run.mjs';

const missing=path.join(os.tmpdir(),'ymcc-pnp-missing-'+process.pid);
const configuredToolchain=process.env[TOOLCHAIN_ENV]??DEFAULT_TOOLCHAIN;
test('exact front-door registration has source and frozen compiler/linker inputs in both profiles',()=>{
 const matches=getExtraAdapters(REPO,{selectedIds:[ID]}).filter(row=>row.id===ID);
 assert.equal(matches.length,1);
 const [adapter]=validateAdapters(matches,REPO),toolchain=resolveToolchain();
 assert.equal(adapter.blockedReason,undefined);
 assert.equal(adapter.kind,'node');assert.equal(adapter.scope,'behavioral-mock');
 assert.deepEqual(adapter.profiles,['desktop','full']);
 assert.equal(adapter.path,MODULE+'/run.mjs');
 assert.deepEqual(adapter.args,['--out','{outDir}/pnp-absence']);
 assert.deepEqual(adapter.resultFiles,['pnp-absence/RESULT.json']);
 for(const file of ['native/virtual_target_return_pnp_absence.h',SELFTEST,'InputHost/Program.cs',MODULE+'/field-record.json']) assert(adapter.evidenceInputs.includes(file));
 assert(adapter.artifactInputs.length<=MAX_ARTIFACT_INPUTS);
 for(const row of toolchain.frozen) assert(adapter.artifactInputs.includes(row.path));
 assert.equal(toolchain.frozen.length,280);
});
test('explicit absolute toolchain override wins and missing/relative captures refuse without fallback',()=>{
 assert.equal(resolveToolchain({env:{[TOOLCHAIN_ENV]:configuredToolchain}}).root,fs.realpathSync(configuredToolchain));
 assert.throws(()=>resolveToolchain({env:{[TOOLCHAIN_ENV]:'relative'}}),/ABSOLUTE/);
 assert.throws(()=>resolveToolchain({env:{[TOOLCHAIN_ENV]:missing}}),/ENOENT/);
 const [adapter]=getPnpAbsenceAdapters(REPO,{env:{[TOOLCHAIN_ENV]:missing}});
 assert.match(adapter.blockedReason,/PNP_ABSENCE_TOOLCHAIN_UNAVAILABLE/);
 let called=false;
 assert.throws(()=>run(['--out',missing+'-out'],{env:{[TOOLCHAIN_ENV]:missing},spawn(){called=true;}}),/ENOENT/);
 assert.equal(called,false);
});
test('wrapper launches the existing selftest child and propagates its real failure code',()=>{
 let called=false;
 const out=missing+'-child';
 const exit=run(['--out',out],{spawn(executable,args,options){
  called=true;assert.equal(executable,process.execPath);
  assert.deepEqual(args,[path.join(REPO,SELFTEST),'--out',path.resolve(out),'--toolchain',fs.realpathSync(configuredToolchain)]);
  assert.equal(options.cwd,REPO);assert.equal(options.windowsHide,true);
  return {status:23,stdout:'',stderr:'',signal:null};
 }});
 assert.equal(called,true);assert.equal(exit,23);assert.equal(fs.existsSync(out),false);
 assert.throws(()=>run(['--out','relative']),/ABS_FRESH/);
 assert.throws(()=>run(['--out',REPO]),/fresh/);
});
test('acceptance uses pass, variants and fences, without interpreting the historical status label',()=>{
 const toolchain=resolveToolchain();
 const names=['Release','Checked','negative-enum-error-absent','negative-property-error-absent','negative-observed-id-authority-alphabet','negative-forget-family','negative-ps-unclassified-absent'];
 const required=[null,null,'enumeration/error-before-first','property/read-error','id/opaque-colon-before-live-target','family/new-epoch-0','ps/unclassified-generic-root'];
 const privateRoot=fs.mkdtempSync(path.join(os.tmpdir(),'ymcc-pnp-verifier-'));
 const artifact=file=>{const bytes=fs.readFileSync(file);return {path:file,bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex')};};
 const positiveNames=[...required.slice(2),...Array.from({length:119},(_,index)=>'positive-contract-'+index)];
 const variants=names.map((variant,index)=>{
  const positive=index<2,dir=path.join(privateRoot,variant);fs.mkdirSync(dir);
  const binary=path.join(dir,'PnpAbsenceFixture.exe');fs.writeFileSync(binary,'not-an-executable; verifier identity control only');
  const cases=(positive?positiveNames:[required[index]]).map(name=>({case:name,checks:1,pass:positive}));
  const failures=positive?[]:[{case:required[index],message:'intentional original semantic rejection'}];
  const raw={pass:positive,caseCount:cases.length,assertions:positive?1198:3,cases,failures,caseFilter:positive?null:required[index],
   realDeviceApiExecuted:false,hardwareAccepted:false,physicalOrOwnershipAuthority:false,deviceWritesThreadsTimersRetries:false,
   productionReaderCompiledAndExportedNotCalled:true,productionPsProfileReaderCompiledAndExportedNotCalled:true};
  const file=path.join(dir,'RESULT.json');fs.writeFileSync(file,JSON.stringify(raw));
  return {variant,positive,pass:true,cases:cases.length,assertions:raw.assertions,failureCount:failures.length,requiredCase:required[index],binary:artifact(binary),result:artifact(file)};
 });
 const receipt={pass:true,status:'historical-label',sourceCurrent:true,selectedHostRulesCurrent:true,capturedToolchain:toolchain.root,
  variants,boundToolchainInputs:[artifact(toolchain.frozen[0].path)],scope:{deviceApisExecuted:false,productOrHostExecuted:false,deviceServiceDriverOrSteamOperations:false}};
 try {
 verifyResult(receipt,toolchain);
 verifyResult({...receipt,status:'a different historical label'},toolchain);
 assert.throws(()=>verifyResult({...receipt,pass:false},toolchain));
 assert.throws(()=>verifyResult({...receipt,sourceCurrent:false},toolchain));
 assert.throws(()=>verifyResult({...receipt,variants:receipt.variants.slice(0,5)},toolchain));
 assert.throws(()=>verifyResult({...receipt,boundToolchainInputs:[{path:missing,sha256:'0'.repeat(64)}]},toolchain));
 const field=JSON.parse(fs.readFileSync(path.join(REPO,MODULE,'field-record.json'),'utf8'));
 assert.equal(field.fieldObservation.count,29);assert.equal(field.fieldObservation.exactRejectedG3EDeviceId,null);
 assert.match(field.regressionInputs.meaning,/not captured G3E node identity/);
 assert.throws(()=>verifyResult({...receipt,variants:receipt.variants.map((row,index)=>index===6?{...row,cases:2}:row)},toolchain),/count|one case/i);
 assert.throws(()=>verifyResult({...receipt,variants:receipt.variants.map((row,index)=>index===6?{...row,requiredCase:'wrong-case'}:row)},toolchain),/selector/i);
 } finally { fs.rmSync(privateRoot,{recursive:true,force:true}); }
});
