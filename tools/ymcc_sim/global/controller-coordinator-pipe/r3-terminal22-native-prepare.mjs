// Extend one actual terminal22 capture into a full native compile-only fixture.
// The original eight polarities and all public runtime gates are preserved.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
const roles=Object.freeze({positive:null,bypassAdmission:'bypass-admission',recoveryInheritance:'rollback-recovery-inheritance',cancellation:'rollback-cancellation-guards',psOwnership:'rollback-ps-endpoint-ownership',psBinding:'rollback-ps-binding-revision',switchReturn:'rollback-switch-return-rearm',switchOwner:'rollback-switch-fault-owner'});
const options={};for(let i=2;i<process.argv.length;i++){
 const key=process.argv[i];assert(['--preparation','--toolchain-manifest','--out','--role','--progress','--final-stages'].includes(key)&&!Object.hasOwn(options,key),'unknown/duplicate option');
 assert(process.argv[i+1]&&!process.argv[i+1].startsWith('--'),'option requires value');options[key]=process.argv[++i];
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
assert.equal(preparation.status,'MANAGED22_COMPILED_NOT_RUNTIME_ACCEPTED');assert.equal(preparation.publicRuntimeAccepted,false);
assert.equal(hash(preparation.layout.path),preparation.layout.sha256);assert.equal(hash(preparation.managed.manifestPath),preparation.managed.manifestSha256);
const layout=read(preparation.layout.path),out=path.resolve(options['--out']);
assert(within(layout.authorizedBuildRoot,out)&&!fs.existsSync(out),'fresh owned native preparation required');
for(let parent=path.dirname(out);within(layout.authorizedBuildRoot,parent);parent=path.dirname(parent))if(fs.existsSync(parent))assert(!fs.lstatSync(parent).isSymbolicLink(),'redirected output parent');
fs.mkdirSync(out,{recursive:true});
const state={schemaVersion:1,status:'PREPARING',role,mutation:roles[role],sourceRoot:layout.sourceRoot,preparation:pin(preparationFile),
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
 assert.equal(state.managedAdmissions,22);
 const nativeOut=path.join(out,'native'),runtimeRoot=path.join(out,'qualification-runtime');
 const argv=['--out',nativeOut,'--managed-dll',managed.fixtureDll,...(roles[role]?['--mutation',roles[role]]:[]),'--compile-only'];
 state.status='COMPILING_NATIVE';say('Compile the actual full native coordinator; no fixture or product is executed');
 state.checkProgress=checkProgress;state.finalStages=finalStages;state.native=buildCurrentCompileOnly({argv,batchAuthority:token,runtimeRoot,checkProgress,finalStages});
 assert.equal(state.native.status,'COMPILED_NOT_EXECUTED');assert.equal(state.native.compileOnly,true);assert.equal(state.native.runBranchReached,false);
 state.runtimeRoot=runtimeRoot;state.status='NATIVE22_COMPILED_NOT_RUNTIME_ACCEPTED';
}catch(error){state.status='INCOMPLETE';state.errors.push({message:error.message,stack:error.stack});process.exitCode=1;}
finally{
 if(token){try{state.batchAfter=authority.finishCurrentCompileBatch(token);}catch(error){state.status='INCOMPLETE';state.errors.push({phase:'finish-authority',message:error.message,stack:error.stack});process.exitCode=1;}}
 save();console.log(JSON.stringify({status:state.status,role,resultFile,errors:state.errors.map(row=>row.message)}));
}