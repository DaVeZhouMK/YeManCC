import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {REPO,SELFTEST,resolveToolchain} from './adapter.mjs';

const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const key=file=>path.resolve(file).toLowerCase();
export const VARIANTS=[
 ['Release',null],['Checked',null],
 ['negative-enum-error-absent','enumeration/error-before-first'],
 ['negative-property-error-absent','property/read-error'],
 ['negative-observed-id-authority-alphabet','id/opaque-colon-before-live-target'],
 ['negative-forget-family','family/new-epoch-0'],
 ['negative-ps-unclassified-absent','ps/unclassified-generic-root'],
];
function verifyArtifact(row,expected) {
 assert(row&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/i.test(row.sha256),'EVIDENCE_INCOMPLETE: artifact identity');
 assert.equal(key(row.path),key(expected),'EVIDENCE_INCOMPLETE: unexpected artifact path');
 assert.equal(key(fs.realpathSync(row.path)),key(path.resolve(expected)),'EVIDENCE_INCOMPLETE: redirected artifact');
 const bytes=fs.readFileSync(row.path);
 assert.equal(bytes.length,row.bytes,'EVIDENCE_INCOMPLETE: artifact size');
 assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'),row.sha256,'EVIDENCE_INCOMPLETE: artifact SHA256');
 return bytes;
}
export function verifyResult(result,toolchain,{outDir}={}) {
 assert.equal(result.pass,true,'Existing selftest must pass');
 assert.equal(result.sourceCurrent,true,'Selftest source fence must remain current');
 assert.equal(result.selectedHostRulesCurrent,true,'Selected Host rules must remain current');
 assert.equal(key(result.capturedToolchain),key(toolchain.root),'Result must use the selected captured toolchain');
 assert.deepEqual(result.variants.map(row=>row.variant),VARIANTS.map(row=>row[0]),'All seven actual compiled variants are required');
 assert.deepEqual(result.variants.map(row=>row.positive),[true,true,false,false,false,false,false]);
 const root=path.resolve(outDir??path.dirname(path.dirname(result.variants[0].result.path)));
 let positiveCases;
 for(const [index,row] of result.variants.entries()) {
  const requiredCase=VARIANTS[index][1],positive=index<2;
  assert.equal(row.pass,true,'Compiled variant failed: '+row.variant);
  assert.equal(row.requiredCase,requiredCase,'Original semantic case selector required');
  assert(Number.isSafeInteger(row.assertions)&&row.assertions>0,'Actual fixture assertions required');
  verifyArtifact(row.binary,path.join(root,row.variant,'PnpAbsenceFixture.exe'));
  const raw=JSON.parse(verifyArtifact(row.result,path.join(root,row.variant,'RESULT.json')).toString('utf8').replace(/^\uFEFF/,''));
  assert.equal(raw.caseCount,row.cases,'Raw case count differs from summary');
  assert.equal(raw.assertions,row.assertions,'Raw assertions differ from summary');
  assert.equal(raw.failures.length,row.failureCount,'Raw failures differ from summary');
  assert.equal(raw.cases.length,raw.caseCount);
  const names=raw.cases.map(item=>item.case);
  assert(names.every(name=>typeof name==='string'&&name.length>0));
  assert.equal(new Set(names).size,names.length,'Raw case names must be unique');
  assert(raw.cases.every(item=>typeof item.pass==='boolean'&&Number.isSafeInteger(item.checks)&&item.checks>0));
  assert(raw.cases.reduce((sum,item)=>sum+item.checks,0)<=raw.assertions);
  for(const flag of ['realDeviceApiExecuted','hardwareAccepted','physicalOrOwnershipAuthority','deviceWritesThreadsTimersRetries']) assert.equal(raw[flag],false,'Software scope only: '+flag);
  for(const flag of ['productionReaderCompiledAndExportedNotCalled','productionPsProfileReaderCompiledAndExportedNotCalled']) assert.equal(raw[flag],true,'Actual compile-only API required: '+flag);
  if(positive) {
   assert.equal(row.cases,124,'Complete original and PS positive set required');
   assert.equal(row.assertions,1198,'Complete positive assertions required');
   assert.equal(raw.pass,true);assert.equal(raw.caseFilter,null);assert.equal(row.failureCount,0);
   assert(raw.cases.every(item=>item.pass));
   for(const [,name] of VARIANTS.slice(2)) assert(names.includes(name),'Original negative case absent from positive coverage: '+name);
   if(positiveCases) assert.deepEqual([...names].sort(),positiveCases,'Release/Checked actual case sets differ');
   else positiveCases=[...names].sort();
  } else {
   assert.equal(row.cases,1,'Semantic fault must execute exactly one case');
   assert.equal(raw.pass,false);assert.equal(raw.caseFilter,requiredCase);
   assert.deepEqual(names,[requiredCase],'Semantic fault actual selection differs');
   assert.equal(raw.cases[0].pass,false);
   assert(row.failureCount>0&&raw.failures.every(item=>item.case===requiredCase),'Every semantic failure must belong to the original required case');
  }
 }
 const registered=new Map(toolchain.frozen.map(row=>[key(row.path),row.sha256]));
 assert(result.boundToolchainInputs.length>0,'Actual compiler/linker closure required');
 const seen=new Set();
 for(const row of result.boundToolchainInputs) {
  assert(!seen.has(key(row.path)),'Duplicate actual toolchain input');seen.add(key(row.path));
  assert.equal(registered.get(key(row.path)),row.sha256,'Actual compiler/linker dependency must be registered: '+row.path);
  verifyArtifact(row,row.path);
 }
 assert.equal(result.scope.deviceApisExecuted,false);
 assert.equal(result.scope.productOrHostExecuted,false);
 assert.equal(result.scope.deviceServiceDriverOrSteamOperations,false);
 // RESULT.status is historical; production integration and hardware acceptance remain outside this run.
}

export function run(args,{env=process.env,spawn=spawnSync,repo=REPO}={}) {
 assert(args.length===2&&args[0]==='--out'&&path.isAbsolute(args[1]),'Use --out ABS_FRESH_DIRECTORY');
 const out=path.resolve(args[1]);
 assert(!fs.existsSync(out),'Output must be fresh');
 const toolchain=resolveToolchain({repo,env});
 for(const row of toolchain.bindings) assert.equal(hash(row.path),row.sha256,'Captured tool/dependency identity changed: '+row.path);
 const before=toolchain.artifacts.map(file=>({path:file,sha256:hash(file)}));
 const child=spawn(process.execPath,[path.join(repo,SELFTEST),'--out',out,'--toolchain',toolchain.root],
  {cwd:repo,env,encoding:'utf8',windowsHide:true,timeout:165000,maxBuffer:4*1024*1024});
 if(child.stdout) process.stdout.write(child.stdout);
 if(child.stderr) process.stderr.write(child.stderr);
 if(child.error) throw child.error;
 assert(!child.signal,'Selftest child terminated by signal: '+child.signal);
 if(child.status!==0) return child.status??1;
 verifyResult(JSON.parse(fs.readFileSync(path.join(out,'RESULT.json'),'utf8')),toolchain,{outDir:out});
 for(const row of before) assert.equal(hash(row.path),row.sha256,'Captured artifact changed during selftest: '+row.path);
 return 0;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
 try { process.exitCode=run(process.argv.slice(2)); }
 catch(error) { console.error(error.stack??String(error));process.exitCode=1; }
}
