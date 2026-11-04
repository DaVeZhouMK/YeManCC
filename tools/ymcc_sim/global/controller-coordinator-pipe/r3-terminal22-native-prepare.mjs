// Extend one actual terminal22 capture into a full native compile-only fixture.
// The original eight polarities and all public runtime gates are preserved.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {requireTerminalManagedContract,terminalManagedStageStatus,verifyManagedDecoderCompileEvidence} from './r3-managed-input-contract.mjs';
const roles=Object.freeze({positive:null,bypassAdmission:'bypass-admission',recoveryInheritance:'rollback-recovery-inheritance',cancellation:'rollback-cancellation-guards',psOwnership:'rollback-ps-endpoint-ownership',psBinding:'rollback-ps-binding-revision',switchReturn:'rollback-switch-return-rearm',switchOwner:'rollback-switch-fault-owner'});

// Pin only compiler-observed headers and the three explicit translation units.
// The verified declaration is an allowlist/preimage, never a claim every member ran.
export function collectObservedCompilerLedger({native,allowedTools=[]}){
 assert(native?.status==='COMPILED_NOT_EXECUTED'&&native.compileOnly===true&&native.compilerExitCode===0&&native.runBranchReached===false,'actual successful compile-only result required');
 const digest=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const key=file=>path.resolve(file).toLowerCase();
 const regular=file=>{assert(path.isAbsolute(file),'absolute compiler input required');const st=fs.lstatSync(file);assert(st.isFile()&&!st.isSymbolicLink(),'regular compiler input required');};
 const exact=row=>{assert(row&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256),'exact compiler input pin required');regular(row.path);assert.equal(digest(row.path),row.sha256,'compiler input changed: '+row.path);return {path:path.resolve(row.path),sha256:row.sha256};};
 exact(native.sourceManifest);exact(native.buildScript);exact(native.exe);
 const source=JSON.parse(fs.readFileSync(native.sourceManifest.path,'utf8').replace(/^\uFEFF/,''));
 assert(Array.isArray(source.records)&&source.records.length>0&&Array.isArray(source.compiledGenerated)&&Array.isArray(source.observedIncludePaths)&&source.observedIncludePaths.length>0,'actual compiler source manifest required');
 const allowed=new Map();
 const admit=row=>{assert(row&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256),'precompiled exact input pin required');const k=key(row.path),old=allowed.get(k);assert(!old||old.sha256===row.sha256,'conflicting precompiled input');allowed.set(k,{path:path.resolve(row.path),sha256:row.sha256});};
 for(const row of allowedTools)admit(row);
 for(const row of source.records)admit({path:row.snapshot,sha256:row.sha256});
 for(const row of source.compiledGenerated)admit(row);
 const buildRoot=path.dirname(native.buildScript.path),logFile=path.join(buildRoot,'build.log');regular(logFile);
 const buildLogSha256=digest(logFile),log=fs.readFileSync(logFile,'utf8');
 const observed=[...new Set(log.split(/\r?\n/).filter(line=>/(?:including file:|包含文件:)/i.test(line)).map(line=>line.match(/[A-Z]:\\.*$/i)?.[0]?.trim()).filter(Boolean).map(key))];
 assert(observed.length>0,'actual compiler include log required');
 const reported=[...new Set(source.observedIncludePaths.map(key))];
 assert.deepEqual([...observed].sort(),[...reported].sort(),'compiler include log and source manifest differ');
 const inputs=new Map(),used=[];
 const observedInput=file=>{const original=allowed.get(key(file));assert(original,'unbound observed compiler input: '+file);const row=exact(original);inputs.set(key(row.path),row);used.push(row.path);};
 for(const file of observed)observedInput(file);
 const fixture=source.compiledGenerated.filter(row=>key(row.path)===key(path.join(buildRoot,'fixture.cpp')));assert.equal(fixture.length,1,'unique actual fixture translation unit required');observedInput(fixture[0].path);
 for(const relative of ['native/dsu_server.cpp','native/steam_original_recovery_engine.cpp']){const rows=source.records.filter(row=>row.path===relative);assert.equal(rows.length,1,'actual separate translation unit required: '+relative);observedInput(rows[0].snapshot);}
 inputs.set(key(native.buildScript.path),exact(native.buildScript));
 assert.equal(digest(native.sourceManifest.path),native.sourceManifest.sha256,'compiler source manifest changed during ledger');
 assert.equal(digest(logFile),buildLogSha256,'compiler log changed during ledger');
 return {schemaVersion:1,status:'ACTUAL_COMPILER_INPUTS_PINNED_NOT_RUNTIME_QUALIFIED',method:'ORIGINAL_MSVC_SHOWINCLUDES_AND_EXPLICIT_THREE_TU_RECIPE',inputs:[...inputs.values()],observedIncludePaths:reported,explicitTranslationUnits:[fixture[0].path,...['native/dsu_server.cpp','native/steam_original_recovery_engine.cpp'].map(relative=>source.records.find(row=>row.path===relative).snapshot)],sourceManifest:exact(native.sourceManifest),buildScript:exact(native.buildScript),buildLog:{path:logFile,sha256:buildLogSha256,bytes:fs.statSync(logFile).size},declaredAllowlistMembers:allowed.size,unobservedAllowlistClaimedUsed:false,sourceApprovalGranted:false,publicRuntimeAccepted:false};
}

export async function executeTerminal22NativePreparation(argv=process.argv.slice(2)){
const options={};for(let i=0;i<argv.length;i++){
 const key=argv[i];assert(['--preparation','--toolchain-manifest','--out','--role','--progress','--final-stages'].includes(key)&&!Object.hasOwn(options,key),'unknown/duplicate option');
 assert(argv[i+1]&&!argv[i+1].startsWith('--'),'option requires value');options[key]=argv[++i];
}
for(const key of ['--preparation','--toolchain-manifest','--out'])assert(options[key],key+' required');
const role=options['--role']??'positive';assert(Object.hasOwn(roles,role),'unknown original native polarity');
const checkProgress=options['--progress']==='completed-assertions';assert(options['--progress']===undefined||checkProgress,'unknown read-only progress mode');
const finalStages=options['--final-stages']==='observed';assert(options['--final-stages']===undefined||finalStages,'unknown final-stage observation mode');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const pin=file=>({path:path.resolve(file),sha256:hash(file),bytes:fs.statSync(file).size});
const within=(root,file)=>{const rel=path.relative(root,file);return rel&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);};
const preparationFile=path.resolve(options['--preparation']),toolchainFile=path.resolve(options['--toolchain-manifest']);
const preparation=read(preparationFile);
assert([terminalManagedStageStatus('managed'),terminalManagedStageStatus('managed','current105-terminal23-sd-decoder-v1')].includes(preparation.status),'explicit managed terminal preparation required');assert.equal(preparation.publicRuntimeAccepted,false);
assert.equal(hash(preparation.layout.path),preparation.layout.sha256);assert.equal(hash(preparation.managed.manifestPath),preparation.managed.manifestSha256);
const layout=read(preparation.layout.path),contract=requireTerminalManagedContract(layout.managedInputContract),out=path.resolve(options['--out']);
assert.equal(preparation.status,terminalManagedStageStatus('managed',contract.id),'preparation status differs from selected managed contract');
if(contract.steamDeckPadDecoder)assert.equal(preparation.managedInputContract,contract.id,'explicit decoder contract preparation required');
assert(within(layout.authorizedBuildRoot,out)&&!fs.existsSync(out),'fresh owned native preparation required');
for(let parent=path.dirname(out);within(layout.authorizedBuildRoot,parent);parent=path.dirname(parent))if(fs.existsSync(parent))assert(!fs.lstatSync(parent).isSymbolicLink(),'redirected output parent');
fs.mkdirSync(out,{recursive:true});
const state={schemaVersion:1,status:'PREPARING',managedInputContract:contract.id,role,mutation:roles[role],sourceRoot:layout.sourceRoot,preparation:pin(preparationFile),
 toolchainManifest:pin(toolchainFile),runner:pin(fileURLToPath(import.meta.url)),sourceApprovalGranted:false,
 publicRuntimeAccepted:false,nativeExecuted:false,normalProductMainExecuted:false,realSdkLoaded:false,deviceIo:false,errors:[]};
const resultFile=path.join(out,'NATIVE-PREPARATION.json'),save=()=>fs.writeFileSync(resultFile,JSON.stringify(state,null,2)+'\n');
const say=message=>{console.log(message);save();};
let authority,token;
try{
 const declared=read(toolchainFile);assert.equal(declared.schemaVersion,1);assert(Array.isArray(declared.files)&&declared.files.length>0);assert(Array.isArray(declared.tools)&&declared.tools.length>0);
 const nativeTools=new Map();
 say('Verify every original native toolchain member; archived frozen paths are not substituted');
 for(const row of [...declared.files,...declared.tools]){
  assert(path.isAbsolute(row.original)&&/^[a-f0-9]{64}$/.test(row.sha256),'native tool pin required');
  assert(fs.lstatSync(row.original).isFile()&&!fs.lstatSync(row.original).isSymbolicLink(),'regular original native tool required');
  assert.equal(hash(row.original),row.sha256,'native original member changed: '+row.original);
  const key=path.resolve(row.original).toLowerCase(),old=nativeTools.get(key);assert(!old||old.sha256===row.sha256,'native tool pin conflict');nativeTools.set(key,{path:row.original,sha256:row.sha256});
 }
 state.nativeToolchain={files:declared.files.length,tools:declared.tools.length,allOriginalMembersVerified:true};
 const moduleRoot=path.join(layout.sourceRoot,'tools/ymcc_sim/global/controller-coordinator-pipe');
 authority=await import(pathToFileURL(path.join(moduleRoot,'r3-current-compile-batch.mjs')).href);
 const {buildCurrentCompileOnly}=await import(pathToFileURL(path.join(layout.sourceRoot,'tools/input_coordinator_current_compile_only.mjs')).href);
 const actual=new Map(preparation.batchAfter.actualTools.map(row=>[path.resolve(row.path).toLowerCase(),row.path]));
 for(const row of nativeTools.values())actual.set(path.resolve(row.path).toLowerCase(),row.path);
 actual.set(path.resolve(fileURLToPath(import.meta.url)).toLowerCase(),fileURLToPath(import.meta.url));
 state.status='VERIFYING_NATIVE_BATCH';say('Open full captured source and original native toolchain authority');
 token=authority.beginCurrentCompileBatch({layout,actualToolFiles:[...actual.values()],receiptDirectory:path.join(out,'authority')});
 const managed=read(preparation.managed.manifestPath);
 state.managedAdmissions=authority.verifyCurrentManagedSourceRows(managed,layout.sourceRoot,{batchAuthority:token}).length;
 assert.equal(state.managedAdmissions,contract.count);verifyManagedDecoderCompileEvidence(managed,contract.id,layout.authorizedBuildRoot);
 const nativeOut=path.join(out,'native'),runtimeRoot=path.join(out,'qualification-runtime');
 const argv=['--out',nativeOut,'--managed-dll',managed.fixtureDll,...(roles[role]?['--mutation',roles[role]]:[]),'--compile-only'];
 state.status='COMPILING_NATIVE';say('Compile the actual full native coordinator; no fixture or product is executed');
 state.checkProgress=checkProgress;state.finalStages=finalStages;state.native=buildCurrentCompileOnly({argv,batchAuthority:token,runtimeRoot,checkProgress,finalStages});
 assert.equal(state.native.status,'COMPILED_NOT_EXECUTED');assert.equal(state.native.compileOnly,true);assert.equal(state.native.runBranchReached,false);
 const compilerLedger=collectObservedCompilerLedger({native:state.native,allowedTools:[...nativeTools.values()]});
 const compilerLedgerFile=path.join(out,'COMPILER-INPUTS.json');fs.writeFileSync(compilerLedgerFile,JSON.stringify(compilerLedger,null,2)+'\n',{flag:'wx'});
 state.compilerLedger=pin(compilerLedgerFile);state.actualObservedCompilerInputs=compilerLedger.inputs.length;
 state.runtimeRoot=runtimeRoot;state.status=terminalManagedStageStatus('native',contract.id);
}catch(error){state.status='INCOMPLETE';state.errors.push({message:error.message,stack:error.stack});process.exitCode=1;}
finally{
 if(token){try{state.batchAfter=authority.finishCurrentCompileBatch(token);}catch(error){state.status='INCOMPLETE';state.errors.push({phase:'finish-authority',message:error.message,stack:error.stack});process.exitCode=1;}}
 save();console.log(JSON.stringify({status:state.status,role,resultFile,errors:state.errors.map(row=>row.message)}));
}
 return state;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))await executeTerminal22NativePreparation();
