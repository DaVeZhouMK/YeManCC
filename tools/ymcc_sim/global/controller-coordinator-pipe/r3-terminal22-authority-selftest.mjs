// Exercise the real captured compile authority without modifying its snapshot.
// Build membership is not runtime, physical-return or complete lifecycle acceptance.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';

const options={};
for(let i=2;i<process.argv.length;i++){
 const key=process.argv[i];
 assert(['--preparation','--out'].includes(key)&&!Object.hasOwn(options,key),'unknown/duplicate option');
 assert(process.argv[i+1]&&!process.argv[i+1].startsWith('--'),'option requires value');
 options[key]=process.argv[++i];
}
assert(options['--preparation']&&options['--out'],'--preparation and --out required');
const preparationFile=path.resolve(options['--preparation']),out=path.resolve(options['--out']);
assert(!fs.existsSync(out),'fresh output required');
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const preparation=read(preparationFile);
assert.equal(preparation.status,'MANAGED22_COMPILED_NOT_RUNTIME_ACCEPTED');
assert.equal(preparation.publicRuntimeAccepted,false);
assert.equal(preparation.terminalCleanupCompiledExactlyOnce,true);
assert.equal(hash(preparation.layout.path),preparation.layout.sha256);
assert.equal(hash(preparation.managed.manifestPath),preparation.managed.manifestSha256);
const layout=read(preparation.layout.path),manifest=read(preparation.managed.manifestPath);
assert.equal(layout.managedInputContract,'current105-terminal22-v1');
const moduleRoot=path.join(layout.sourceRoot,'tools/ymcc_sim/global/controller-coordinator-pipe');
const authority=await import(pathToFileURL(path.join(moduleRoot,'r3-current-compile-batch.mjs')).href);
const contracts=await import(pathToFileURL(path.join(moduleRoot,'r3-managed-input-contract.mjs')).href);
const relative=path.relative(layout.authorizedBuildRoot,out);
assert(relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),'output outside original owned build root');
fs.mkdirSync(out);
const inputs=[preparationFile,preparation.layout.path,preparation.managed.manifestPath,
 fileURLToPath(import.meta.url),path.join(moduleRoot,'r3-current-compile-batch.mjs'),path.join(moduleRoot,'r3-managed-input-contract.mjs')]
 .map(file=>({path:file,sha256:hash(file)}));
const result={schemaVersion:1,status:'RUNNING',inputs,cases:[],sourceApprovalGranted:false,
 publicRuntimeAccepted:false,fullCoordinatorRuntimeAccepted:false,physicalReturnValidated:false,
 normalProductMainExecuted:false,realSdkLoaded:false,deviceIo:false};
const resultFile=path.join(out,'RESULT.json');
const save=()=>fs.writeFileSync(resultFile,JSON.stringify(result,null,2)+'\n');
function rejects(name,fn,pattern){
 let observed;
 try{fn();}catch(error){observed=error;}
 assert(observed,name+': rejection missing');
 assert.match(observed.message,pattern,name+': wrong rejection');
 result.cases.push({name,status:'PASS',observed:observed.message});save();
}
const clone=value=>structuredClone(value);
let token;
try{
 const invalidLayouts=[
  ['legacy-label-cannot-admit22',l=>{l.managedInputContract=contracts.LEGACY_MANAGED_CONTRACT;},/ORIGINAL_EXACT21_SOURCE_PROFILE_REQUIRED/],
  ['unknown-contract',l=>{l.managedInputContract='current105-exact22-v1';},/CURRENT_MANAGED_CONTRACT_UNKNOWN/],
  ['missing-terminal',l=>{l.expectedManagedSources.splice(3,1);},/EXACT22_MEMBERS_REQUIRED/],
  ['duplicate-terminal',l=>{l.expectedManagedSources[4]=clone(l.expectedManagedSources[3]);},/EXACT22_MEMBERS_REQUIRED/],
  ['extra-member',l=>{l.expectedManagedSources.push(clone(l.expectedManagedSources[0]));},/EXACT22_MEMBERS_REQUIRED/],
  ['foreign-terminal',l=>{l.expectedManagedSources[3].relative='InputHost/FakeTerminal.cs';},/MEMBER_NOT_IN_current105-terminal22-v1/],
  ['wrong-terminal-pin',l=>{l.expectedManagedSources[3].sha256='0'.repeat(64);},/TERMINAL22_SOURCE_PIN_CHANGED:InputHost\/HostTerminalCleanup.cs/],
  ['wrong-contract-code-pin',l=>{l.managedContractCodeSha256='0'.repeat(64);},/EXPLICIT_TERMINAL22_CONTRACT_PIN_REQUIRED/],
 ];
 for(const [name,mutate,pattern] of invalidLayouts){
  const bad=clone(layout);mutate(bad);
  const receiptDirectory=path.join(out,'rejected-'+name);
  rejects(name,()=>authority.beginCurrentCompileBatch({layout:bad,
   actualToolFiles:preparation.batchAfter.actualTools.map(row=>row.path),receiptDirectory}),pattern);
  assert(!fs.existsSync(receiptDirectory),'rejected layout created authority receipts');
 }
 const fake={kind:'CURRENT_CAPTURE_COMPILE_ONLY_IN_PROCESS_CAPABILITY'};
 rejects('counterfeit-token',()=>authority.verifyCurrentManagedSourceRows(manifest,layout.sourceRoot,{batchAuthority:fake}),/LIVE_BRANDED_BATCH_REQUIRED/);
 rejects('contract-is-not-token',()=>authority.requireCurrentCompileBatch(contracts.managedCompileContract(contracts.TERMINAL_MANAGED_CONTRACT)),/LIVE_BRANDED_BATCH_REQUIRED/);
 token=authority.beginCurrentCompileBatch({layout,actualToolFiles:preparation.batchAfter.actualTools.map(row=>row.path),receiptDirectory:path.join(out,'authority')});
 const admitted=authority.verifyCurrentManagedSourceRows(manifest,layout.sourceRoot,{batchAuthority:token});
 assert.equal(admitted.length,22);
 assert.equal(admitted.filter(row=>row.relative==='InputHost/HostTerminalCleanup.cs').length,1);
 result.cases.push({name:'actual-compiled22-admitted-once',status:'PASS',members:admitted.length});save();
 rejects('serialized-token-loses-authority',()=>authority.requireCurrentCompileBatch(JSON.parse(JSON.stringify(token))),/LIVE_BRANDED_BATCH_REQUIRED/);
 rejects('wrong-source-root',()=>authority.verifyCurrentManagedSourceRows(manifest,layout.primaryRoot,{batchAuthority:token}),/SOURCE_ROOT_DIFFERS/);
 const mutations=[
  ['rows-missing-terminal',m=>{m.inputBindings.sources.splice(3,1);},/EXACT22_MEMBERS_REQUIRED/],
  ['rows-duplicate',m=>{m.inputBindings.sources[4]=clone(m.inputBindings.sources[3]);},/EXACT22_MEMBERS_REQUIRED/],
  ['rows-extra',m=>{m.inputBindings.sources.push(clone(m.inputBindings.sources[0]));},/EXACT22_MEMBERS_REQUIRED/],
  ['rows-foreign-member',m=>{m.inputBindings.sources[3].relative='InputHost/FakeTerminal.cs';},/MEMBER_NOT_IN_current105-terminal22-v1/],
  ['rows-wrong-terminal-hash',m=>{m.inputBindings.sources[3].sha256='0'.repeat(64);},/EXACT_CURRENT_MANAGED_MEMBER_REQUIRED/],
  ['rows-wrong-original-path',m=>{m.inputBindings.sources[3].path=path.join(layout.primaryRoot,'InputHost/HostTerminalCleanup.cs');},/EXACT_CURRENT_MANAGED_MEMBER_REQUIRED/],
  ['rows-unowned-snapshot',m=>{m.inputBindings.sources[3].snapshot=path.join(layout.sourceRoot,'InputHost/HostTerminalCleanup.cs');},/EXACT_CURRENT_MANAGED_MEMBER_REQUIRED/],
 ];
 for(const [name,mutate,pattern] of mutations){const bad=clone(manifest);mutate(bad);rejects(name,()=>authority.verifyCurrentManagedSourceRows(bad,layout.sourceRoot,{batchAuthority:token}),pattern);}
 result.batchAfter=authority.finishCurrentCompileBatch(token);
 rejects('closed-token',()=>authority.verifyCurrentManagedSourceRows(manifest,layout.sourceRoot,{batchAuthority:token}),/LIVE_BRANDED_BATCH_REQUIRED/);
 rejects('double-finish',()=>authority.finishCurrentCompileBatch(token),/LIVE_BRANDED_BATCH_REQUIRED/);
 token=null;
 assert(inputs.every(row=>hash(row.path)===row.sha256),'selftest inputs changed');
 result.status='PASS';save();console.log(JSON.stringify({status:result.status,cases:result.cases.length,resultFile}));
}catch(error){
 result.status='FAIL';result.error={message:error.message,stack:error.stack};
 if(token){try{result.batchAfter=authority.finishCurrentCompileBatch(token);}catch(closeError){result.closeError=closeError.message;}}
 save();throw error;
}