// Observe an actual fixed-frame native105 process. Never publishes a runtime profile.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import{createHash}from'node:crypto';import{fileURLToPath,pathToFileURL}from'node:url';
import {readCompletedNativeReport} from './r3-terminal22-observation-terminal.mjs';
import {captureFinalStages} from './r3-terminal22-stage-receipts.mjs';
const options={};for(let i=2;i<process.argv.length;i++){const key=process.argv[i];assert(['--native-preparation','--out'].includes(key)&&!Object.hasOwn(options,key),'strict observer options required');assert(process.argv[i+1]&&!process.argv[i+1].startsWith('--'),'option value required');options[key]=process.argv[++i];}
assert(options['--native-preparation']&&options['--out'],'native preparation and fresh owned result directory required');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,'')),hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex'),pin=p=>({path:path.resolve(p),sha256:hash(p),bytes:fs.statSync(p).size});
const nativeFile=path.resolve(options['--native-preparation']),n=read(nativeFile);
assert.equal(n.status,'NATIVE22_COMPILED_NOT_RUNTIME_ACCEPTED');assert.equal(n.nativeExecuted,false);assert.equal(n.publicRuntimeAccepted,false);
const preparation=read(n.preparation.path);assert.equal(hash(n.preparation.path),n.preparation.sha256);
const layout=read(preparation.layout.path);assert.equal(hash(preparation.layout.path),preparation.layout.sha256);
const root=path.resolve(options['--out']);assert(!fs.existsSync(root),'fresh observer evidence required');
const relative=path.relative(layout.authorizedBuildRoot,root);assert(relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),'observer evidence outside original authority root');
assert(!fs.existsSync(n.runtimeRoot),'the exact compiled runtime directory must be unused');
const moduleRoot=path.join(n.sourceRoot,'tools/ymcc_sim/global/controller-coordinator-pipe');
const oracle=await import(pathToFileURL(path.join(moduleRoot,'r3-current105-oracle.mjs')).href);
assert(['positive','cancellation','psOwnership','psBinding'].includes(n.role),'full-pipe observation role required');
const expected=oracle.currentExpected(n.role),{verifyCurrent105OriginalGroups}=await import(pathToFileURL(path.join(moduleRoot,'r3-current105-original-groups.mjs')).href);
const {runProcess}=await import(pathToFileURL(path.join(n.sourceRoot,'tools/ymcc_sim/global/runner.mjs')).href);
const {verifyCapturedSourceSnapshot}=await import(pathToFileURL(path.join(n.sourceRoot,'tools/ymcc_sim/global/source-snapshot.mjs')).href);
const manifest=read(n.native.sourceManifest.path),managed=read(preparation.managed.manifestPath);
const inputs=new Map();function add(p,digest){p=path.resolve(p);assert(fs.lstatSync(p).isFile()&&!fs.lstatSync(p).isSymbolicLink(),'regular bounded input required');const k=p.toLowerCase(),old=inputs.get(k);assert(!old||old.sha256===digest,'input digest conflict');assert.equal(hash(p),digest,'changed input '+p);inputs.set(k,{path:p,sha256:digest});}
function addPin(row){add(row.path,row.sha256);}
fs.mkdirSync(root,{recursive:true});
const result={schemaVersion:1,status:'VERIFYING_INPUTS',role:n.role,nativePreparation:pin(nativeFile),observer:pin(fileURLToPath(import.meta.url)),strictNativeBudgetMs:60000,publicRuntimeAccepted:false,currentPrimaryAccepted:false,fullCoordinatorRuntimeAccepted:false,normalProductMainExecuted:false,realSdkLoaded:false,deviceIo:false,physicalAcceptance:false,errors:[]};
const resultFile=path.join(root,'OBSERVATION.json'),save=()=>fs.writeFileSync(resultFile,JSON.stringify(result,null,2)+'\n');
result.controlInputs=[result.observer,...['r3-terminal22-observation-terminal.mjs','r3-terminal22-stage-receipts.mjs'].map(name=>pin(fileURLToPath(new URL('./'+name,import.meta.url))))];
try{
 for(const row of result.controlInputs)addPin(row);
 addPin(n.native.exe);addPin(n.native.sourceManifest);addPin(n.native.buildScript);addPin(n.preparation);addPin(preparation.layout);addPin({path:preparation.managed.manifestPath,sha256:preparation.managed.manifestSha256});
 for(const row of manifest.records){add(row.original,row.sha256);add(row.snapshot,row.sha256);}
 for(const row of [...manifest.compiledGenerated,...n.batchAfter.actualTools,...managed.evaluatedCompilerInputs,...managed.runtimeArtifacts])addPin(row);
 for(const row of managed.inputBindings.sources){add(row.path,row.sha256);add(row.snapshot,row.sha256);}
 for(const row of managed.inputBindings.generatedInputs)addPin(row);
 const sourceCheck=()=>verifyCapturedSourceSnapshot({manifestFile:path.join(n.sourceRoot,'.ymcc-source-snapshot/manifest.json'),repoRoot:layout.primaryRoot,expectedManifestSha256:layout.manifestSha256});
 result.sourceBefore=sourceCheck();assert.equal(result.sourceBefore.status,'FROZEN_SOURCE_VERIFIED');assert.equal(result.sourceBefore.creationStatus,'PASS');
 result.inputCount=inputs.size;result.preflight={nativeExe:pin(n.native.exe.path),managedDll:pin(managed.fixtureDll),managedSources:managed.inputBindings.sources.length};assert.equal(result.preflight.managedSources,22);
 fs.writeFileSync(path.join(root,'INPUTS.json'),JSON.stringify([...inputs.values()],null,2));
 fs.mkdirSync(n.runtimeRoot,{recursive:true});result.status='RUNNING_ORIGINAL105';save();
 const processResult=await runProcess(n.native.exe.path,[n.runtimeRoot,'full-pipe',managed.fixtureDll],{cwd:n.runtimeRoot,env:{...process.env,YEMAN_POWER_CONTROL_DIR:n.runtimeRoot},timeoutMs:60000});
 const {stdout,stderr,...terminal}=processResult;result.nativeProcess={exe:n.native.exe.path,args:[n.runtimeRoot,'full-pipe',managed.fixtureDll],...terminal,strictNativeBudgetMs:60000,forcedTermination:terminal.timedOut,nativeWallDurationMs:terminal.durationMs};
 fs.writeFileSync(path.join(root,'stdout.log'),stdout);fs.writeFileSync(path.join(root,'stderr.log'),stderr);
 if(n.finalStages)result.finalStageObservation=captureFinalStages(n.runtimeRoot,terminal.pid);
 save();
 for(const row of inputs.values())assert.equal(hash(row.path),row.sha256,'post-run input changed: '+row.path);
 result.postInputsStable=true;result.sourceAfter=sourceCheck();assert.equal(result.sourceAfter.status,'FROZEN_SOURCE_VERIFIED');
 const rawFile=path.join(n.runtimeRoot,'fixture-result.json');
 result.rawReport=fs.existsSync(rawFile)?pin(rawFile):null;
 const raw=readCompletedNativeReport(terminal,expected.expectedExit,()=>{assert(fs.existsSync(rawFile),'complete native raw report missing');return read(rawFile);});
 result.rawSummary={status:raw.status,cases:raw.cases.length,checks:raw.checks,failures:raw.failures,forbiddenIo:raw.forbiddenIo};
 result.exactSemantics=oracle.verifyCurrentSemantics(n.role,raw);
 result.originalGroups=verifyCurrent105OriginalGroups(raw,{ownershipRollback:n.role==='psOwnership',bindingRollback:['psBinding','cancellation'].includes(n.role),cancellationRollback:n.role==='cancellation'});
 result.children=oracle.verifyCurrentChildren(n.role,raw,managed,terminal.pid);
 result.status='BOUNDED_ORIGINAL105_MATCHED_PUBLIC_RUNTIME_PENDING';
}catch(error){result.status='INCOMPLETE';result.errors.push({message:error.message,stack:error.stack});process.exitCode=1;}
result.finishedAt=new Date().toISOString();save();console.log(JSON.stringify({status:result.status,role:n.role,rawSummary:result.rawSummary?{...result.rawSummary,failures:result.rawSummary.failures.length}:null,error:result.errors.map(e=>e.message),resultFile}));