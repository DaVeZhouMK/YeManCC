// Versioned build-only preparation. Never declares a public runtime PASS.
import fs from 'node:fs';
import path from 'node:path';
import {createHash,randomUUID} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createCapturedSourceSnapshot,validateSourceSnapshotRoots,verifyCapturedSourceSnapshot,verifyCapturedSourceSnapshotParallel,SNAPSHOT_MANIFEST,SNAPSHOT_POLICY} from '../source-snapshot.mjs';
import {bindSnapshotNodeTools,NODE_TOOLS_BINDING_NAME} from '../snapshot-node-tools.mjs';
import {readVirtualControllerSource} from '../../../virtual_controller_runtime_source.mjs';
import {requireTerminalManagedContract,terminalManagedStageStatus,verifyManagedDecoderCompileEvidence,validateManagedSourceRequirements} from './r3-managed-input-contract.mjs';
const moduleRelative='tools/ymcc_sim/global/controller-coordinator-pipe';
const defaultRepo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const pathKey=value=>path.resolve(value).toLowerCase();
const same=(a,b)=>typeof a==='string'&&path.isAbsolute(a)&&pathKey(a)===pathKey(b);
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const hash=file=>sha(fs.readFileSync(file));
const isSha=value=>typeof value==='string'&&/^[a-f0-9]{64}$/.test(value);
const PREFLIGHT_COPY_CHECK_MS=4000;
const extraPinOption='--expected-preflight-source-pins';
const isResumeStage=stage=>stage==='resume-managed'||stage==='resume-frozen-managed';
const strictChild=(root,file)=>{const relative=path.relative(root,file);return relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);};
function noPreparationReparse(file){
 for(let current=path.resolve(file);;current=path.dirname(current)){
  let stat;try{stat=fs.lstatSync(current);}catch(error){if(error.code!=='ENOENT')throw error;}
  if(stat&&(stat.isSymbolicLink()||pathKey(fs.realpathSync(current))!==pathKey(current)))throw Error('TERMINAL22_PREPARATION_REPARSE_PATH');
  if(path.dirname(current)===current)break;
 }
}
export function resolveTerminal22PreparationLayout({primaryRoot,workspaceRoot,resume=false}={}){
 if(typeof primaryRoot!=='string'||!path.isAbsolute(primaryRoot)||/[\0\r\n]/.test(primaryRoot))throw Error('TERMINAL22_ABSOLUTE_PRIMARY_REQUIRED');
 if(typeof workspaceRoot!=='string'||!path.isAbsolute(workspaceRoot)||/[\0\r\n]/.test(workspaceRoot))throw Error('FRESH_ABSOLUTE_WORKSPACE_REQUIRED');
 primaryRoot=path.resolve(primaryRoot);workspaceRoot=path.resolve(workspaceRoot);
 noPreparationReparse(primaryRoot);noPreparationReparse(workspaceRoot);
 if(!fs.lstatSync(primaryRoot).isDirectory())throw Error('TERMINAL22_PRIMARY_DIRECTORY_REQUIRED');
 if(!resume&&fs.existsSync(workspaceRoot))throw Error('FRESH_ABSOLUTE_WORKSPACE_REQUIRED');
 const sourceWorkspace=path.resolve(primaryRoot,'../../..');
 const coreBase=path.join(sourceWorkspace,'Testing','YMCC','Workspace'),evidenceBase=path.join(sourceWorkspace,'Testing','YMCC','Evidence');
 const coreOwned=strictChild(coreBase,workspaceRoot),legacyEvidence=strictChild(evidenceBase,workspaceRoot);
 if(!coreOwned&&!legacyEvidence)throw Error('OWNED_TESTING_EVIDENCE_WORKSPACE_REQUIRED');
 if(resume&&(!coreOwned||!fs.existsSync(workspaceRoot)||!fs.lstatSync(workspaceRoot).isDirectory()))throw Error('TERMINAL22_RESUME_REQUIRES_EXISTING_CORE_CAPTURE_OWNER');
 if(coreOwned&&pathKey(primaryRoot)!==pathKey(path.join(sourceWorkspace,'Mainline','YeManCC-source','YeManCC')))throw Error('TERMINAL22_CORE_PRIMARY_LAYOUT_REQUIRED');
 const sourceRoot=coreOwned?path.join(workspaceRoot,'Mainline','YeManCC-source','YeManCC'):path.join(path.dirname(primaryRoot),path.basename(primaryRoot)+'-terminal22-'+randomUUID());
 return {primaryRoot,workspaceRoot,sourceRoot,authorizedBuildRoot:path.join(workspaceRoot,'Build/Validation/CurrentCoordinatorTerminal22'),layoutMode:coreOwned?'G_CORE_WORKSPACE_OWNER':'LEGACY_EVIDENCE_SIBLING'};
}

export function parseTerminalManagedPreparationArguments(args){
 if(!Array.isArray(args))throw Error('STRICT_PREPARATION_ARGUMENTS');const options={};
 for(let i=0;i<args.length;i++){const key=args[i];if(!['--repo','--workspace','--managed-input-contract','--stage','--capture-sha256','--expected-preflight-main-sha256',extraPinOption].includes(key)||Object.hasOwn(options,key)||typeof args[i+1]!=='string'||!args[i+1]||args[i+1].startsWith('--'))throw Error('STRICT_PREPARATION_ARGUMENTS');options[key]=args[++i];}
 options['--stage']??='managed';if(!['capture','managed'].includes(options['--stage'])&&!isResumeStage(options['--stage']))throw Error('STRICT_PREPARATION_STAGE');
 if(isResumeStage(options['--stage'])?!isSha(options['--capture-sha256']):Object.hasOwn(options,'--capture-sha256'))throw Error('TERMINAL22_RESUME_CAPTURE_SHA_REQUIRED_ONLY_FOR_RESUME');
 const contract=requireTerminalManagedContract(options['--managed-input-contract']);options['--managed-input-contract']=contract.id;
 if(Object.hasOwn(options,'--expected-preflight-main-sha256')&&(!contract.sharedInputPower||isResumeStage(options['--stage'])||!isSha(options['--expected-preflight-main-sha256'])))throw Error('TERMINAL29_PREFLIGHT_MAIN_SHA_REQUIRES_FRESH_CAPTURE');
 if(Object.hasOwn(options,extraPinOption)){
  if(!options['--expected-preflight-main-sha256'])throw Error('TERMINAL29_EXTRA_PREFLIGHT_PINS_REQUIRE_MAIN_PIN');
  const invalid=()=>{throw Error('TERMINAL29_EXTRA_PREFLIGHT_PINS_INVALID');};
  if(options[extraPinOption].length>2048)invalid();
  let pins;try{pins=JSON.parse(options[extraPinOption]);}catch{invalid();}
  if(!Array.isArray(pins)||pins.length<1||pins.length>8)invalid();
  const seen=new Set(['native/main.cpp','inputhost/program.cs']);
  for(const row of pins){
   if(!Array.isArray(row)||row.length!==2||typeof row[0]!=='string'||!isSha(row[1]))invalid();
   const parts=row[0].split('/'),key=row[0].toLowerCase();
   if(seen.has(key)||!parts.every(part=>/^[A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]+)*$/.test(part)))invalid();
   seen.add(key);
  }
  options[extraPinOption]=pins;
 }
 return options;
}

// Preflight pins refuse early; only the original full-tree checks can approve a capture.
function requirePreflightSource(root,expected,phase,relative='native/main.cpp',label='MAIN'){
 if(expected===undefined)return;
 const file=path.join(root,relative);noPreparationReparse(file);
 const stat=fs.lstatSync(file);
 if(!stat.isFile()||label==='SOURCE'&&stat.size>16*1024*1024)throw Error(`TERMINAL29_PREFLIGHT_${label}_REGULAR_FILE_REQUIRED`);
 const actual=hash(file);
 if(actual!==expected){
  const code=`TERMINAL29_PREFLIGHT_${label}_CHANGED`;
  throw Object.assign(new Error(`${code}:${phase}:${expected}:${actual}`),{code,details:{phase,path:relative,expected,actual}});
 }
}

function readPinned(file,expected){
 noPreparationReparse(file);
 if(!fs.lstatSync(file).isFile())throw Error('TERMINAL22_RESUME_REGULAR_FILE_REQUIRED:'+file);
 const bytes=fs.readFileSync(file);
 if(!isSha(expected)||sha(bytes)!==expected)throw Error('TERMINAL22_RESUME_PIN_CHANGED:'+file);
 return JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''));
}
function openTerminalManagedCapture(roots,contract,captureSha,requireCurrent){
 const {sourceRoot,primaryRoot,authorizedBuildRoot}=roots;
 const capturePath=path.join(authorizedBuildRoot,'PREPARATION.json'),receipt=readPinned(capturePath,captureSha);
 if(receipt.schemaVersion!==1||receipt.preparationStage!=='capture'||receipt.status!=='SOURCE_CAPTURED_NOT_COMPILED'||receipt.sourceCaptureOnly!==true||receipt.managedBuiltThisRun!==false||receipt.managed!==undefined||receipt.batchAfter!==undefined||receipt.errors?.length!==0||receipt.managedInputContract!==contract.id||receipt.layoutMode!==roots.layoutMode||['sourceRoot','primaryRoot','workspaceRoot','authorizedBuildRoot'].some(key=>!same(receipt[key],roots[key]))||['sourceApprovalGranted','publicRuntimeAccepted','normalHostMainExecuted','realSdkLoaded','deviceIo'].some(key=>receipt[key]!==false))throw Error('TERMINAL22_RESUME_EXACT_CAPTURE_ONLY_RECEIPT_REQUIRED');
 const manifestFile=path.join(sourceRoot,SNAPSHOT_MANIFEST), bindingFile=path.join(path.dirname(manifestFile),NODE_TOOLS_BINDING_NAME);
 const layoutFile=path.join(authorizedBuildRoot,'LAYOUT-terminal'+contract.count+'.json');
 if(receipt.capture?.status!=='PASS'||!same(receipt.capture.manifestFile,manifestFile)||receipt.nodeBinding?.status!=='PASS'||!same(receipt.nodeBinding.path,bindingFile)||!same(receipt.layout?.path,layoutFile))throw Error('TERMINAL22_RESUME_CAPTURE_PATH_MISMATCH');
 const layout=readPinned(layoutFile,receipt.layout.sha256);
 if(layout.schemaVersion!==1||layout.managedInputContract!==contract.id||layout.sourceAuthority?.mode!=='EXPLICIT_CURRENT_CAPTURE_COMPILE_ONLY'||['sourceRoot','primaryRoot','workspaceRoot','authorizedBuildRoot'].some(key=>!same(layout[key],roots[key])))throw Error('TERMINAL22_RESUME_LAYOUT_MISMATCH');
 const manifest=readPinned(manifestFile,layout.manifestSha256);
 if(manifest.status!=='PASS'||!same(manifest.snapshotRoot,sourceRoot)||!same(manifest.sourceRoot,primaryRoot)||manifest.copyVerifiedFiles!==receipt.capture.copyVerifiedFiles)throw Error('TERMINAL22_RESUME_CAPTURE_STATE_CHANGED');
 if(!isSha(layout.productMainSha256)||manifest.main?.snapshot?.sha256!==layout.productMainSha256)throw Error('TERMINAL22_RESUME_CAPTURE_MAIN_MISMATCH');
 if(requireCurrent&&hash(path.join(primaryRoot,'native/main.cpp'))!==layout.productMainSha256)throw Error('TERMINAL22_RESUME_CURRENT_MAIN_CHANGED');
 const binding=readPinned(bindingFile,layout.nodeBindingSha256);
 if(binding.status!=='PASS'||!same(binding.snapshotRoot,sourceRoot)||!same(binding.sourceRoot,primaryRoot)||binding.expectedSnapshotManifestSha256!==layout.manifestSha256)throw Error('TERMINAL22_RESUME_NODE_BINDING_MISMATCH');
 for(const name of ['managed-resume','managed'+contract.count,'authority']){
  const file=path.join(authorizedBuildRoot,name);noPreparationReparse(file);
  if(fs.existsSync(file))throw Error('TERMINAL22_RESUME_EXISTING_MANAGED_ATTEMPT:'+file);
 }
 return {capturePath,receipt,layout,manifestFile};
}

async function compilePreparedManaged({layout,out,state,say,requireCurrent=false}){
 const {sourceRoot}=layout,imported=relative=>import(pathToFileURL(path.join(sourceRoot,relative)).href);
 const contracts=await imported(moduleRelative+'/r3-managed-input-contract.mjs');
 const authority=await imported(moduleRelative+'/r3-current-compile-batch.mjs');
 const {toolchainFiles}=await imported('tools/ymcc_sim/global/inputhost-protocol/adapter.mjs');
 const contract=contracts.requireTerminalManagedContract(layout.managedInputContract);
 const actualToolFiles=[...new Set([...toolchainFiles(sourceRoot),process.execPath,...['r3-current-managed-builder.mjs','r3-current-compile-batch.mjs','r3-managed-input-contract.mjs'].map(name=>path.join(sourceRoot,moduleRelative,name))])];
 say('Opening one exact tool/source compile batch');
 const strictCurrent29=contract.sharedInputPower===true&&requireCurrent;
 const token=await authority.beginCurrentCompileBatch({layout,actualToolFiles,receiptDirectory:path.join(out,'authority'),requireCurrentSource:strictCurrent29,parallelSource:strictCurrent29});
 try{
  state.status='COMPILING_MANAGED'+contract.count;
  say('Compiling the original SDK-free managed fixture');
  const {buildManagedFixture}=await imported(moduleRelative+'/r3-current-managed-builder.mjs');
  state.managed=buildManagedFixture(path.join(out,'managed'+contract.count),{sourceRoot,batchAuthority:token});
  const manifest=JSON.parse(fs.readFileSync(state.managed.manifestPath,'utf8'));
  const mapped=authority.verifyCurrentManagedSourceRows(manifest,sourceRoot,{batchAuthority:token});
  if(mapped.length!==contract.count||manifest.managedInputContract!==contract.id||manifest.terminalCleanupCompiled!==true)throw Error('ACTUAL_SELECTED_TERMINAL_CONSUMER_REQUIRED');
  verifyManagedDecoderCompileEvidence(manifest,contract.id,layout.authorizedBuildRoot);
  state.managedMemberCount=mapped.length;state.terminalCleanupCompiledExactlyOnce=true;
 }finally{state.batchAfter=await authority.finishCurrentCompileBatch(token);}
 if(requireCurrent&&state.batchAfter.source?.isCurrent!==true)throw Error('TERMINAL22_RESUME_CURRENT_SOURCE_CHANGED_DURING_COMPILE');
}

async function resumeTerminalManaged(roots,contract,captureSha,stage){
 const requireCurrent=stage==='resume-managed';
 const {authorizedBuildRoot,primaryRoot}=roots;
 const {capturePath,receipt,layout,manifestFile}=openTerminalManagedCapture(roots,contract,captureSha,requireCurrent);
 const out=path.join(authorizedBuildRoot,'managed-resume');
 fs.mkdirSync(out);
 const resultPath=path.join(out,'PREPARATION.json');
 const state={...receipt,status:'VERIFYING_CAPTURE',preparationStage:stage,sourceCaptureOnly:false,managedBuiltThisRun:false,continuedFromCapture:{path:capturePath,sha256:captureSha},sourceCopiesThisRun:0,errors:[],...(!requireCurrent?{frozenSourceOnly:true,currentPrimaryAccepted:false}:{})};
 const save=()=>fs.writeFileSync(resultPath,JSON.stringify(state,null,2)+'\n');
 const say=message=>{console.log(message);save();};
 try{
  say('Rechecking the original capture before importing captured tools');
  const parallelCurrent=contract.sharedInputPower===true&&requireCurrent;
  const verify=parallelCurrent?verifyCapturedSourceSnapshotParallel:verifyCapturedSourceSnapshot;
  const started=performance.now(),proof=await verify({manifestFile,repoRoot:primaryRoot,expectedManifestSha256:layout.manifestSha256});
  state.captureReadback={wallMs:performance.now()-started,status:proof.status,created:proof.creationStatus,isCurrent:proof.isCurrent,fileCount:proof.fileCount,...(parallelCurrent?{sourceVerificationMode:'parallel-primary-and-copy'}:{})};
  if(requireCurrent&&proof.isCurrent!==true){
   state.captureReadback.sourceDiff=proof.sourceDiff;
   state.captureReadback.comparisonSourceDrift=proof.comparisonSourceDrift;
  }
  if(!proof.snapshotIntegrityVerified||proof.creationStatus!=='PASS'||proof.fileCount!==receipt.capture.copyVerifiedFiles)throw Error(requireCurrent?'TERMINAL22_RESUME_CURRENT_CAPTURE_REQUIRED':'TERMINAL22_RESUME_INTACT_FROZEN_CAPTURE_REQUIRED');
  if(requireCurrent&&proof.isCurrent!==true)throw Error('TERMINAL22_RESUME_CURRENT_CAPTURE_REQUIRED');
  await compilePreparedManaged({layout,out,state,say,requireCurrent});
  if(hash(capturePath)!==captureSha||hash(receipt.layout.path)!==receipt.layout.sha256)throw Error('TERMINAL22_RESUME_INPUT_CHANGED_DURING_COMPILE');
  state.status=terminalManagedStageStatus('managed',contract.id);state.managedBuiltThisRun=true;
  say('Managed continuation completed; Native and runtime qualification remain unexecuted');
 }catch(error){state.status='INCOMPLETE';state.errors.push({message:error.message,stack:error.stack});save();throw error;}
 return state;
}

export async function prepareTerminal22(args=process.argv.slice(2)){
const options=parseTerminalManagedPreparationArguments(args);
const selectedContract=requireTerminalManagedContract(options['--managed-input-contract']);
const resume=isResumeStage(options['--stage']);
const roots=resolveTerminal22PreparationLayout({primaryRoot:path.resolve(options['--repo']??defaultRepo),workspaceRoot:options['--workspace'],resume});
const {primaryRoot,workspaceRoot,sourceRoot,authorizedBuildRoot,layoutMode}=roots;
if(resume)return resumeTerminalManaged(roots,selectedContract,options['--capture-sha256'],options['--stage']);
// This stage pins managed inputs only. Native preparation has its own source profile.
if(selectedContract.sharedInputPower&&layoutMode!=='G_CORE_WORKSPACE_OWNER')throw Error('TERMINAL29_EXACT_CORE_CAPTURE_REQUIRED');
const expectedPreflightMain=options['--expected-preflight-main-sha256'];
requirePreflightSource(primaryRoot,expectedPreflightMain,'before-capture');
const extraPins=new Map(options[extraPinOption]);
const requireExtra=(phase)=>{for(const [relative,expected] of extraPins)requirePreflightSource(primaryRoot,expected,phase,relative,'SOURCE');};
requireExtra('before-capture');
const programRelative='InputHost/Program.cs';
const program=fs.readFileSync(path.join(primaryRoot,programRelative));
validateManagedSourceRequirements(program.toString('utf8'),selectedContract.id);
const expectedPreflightProgram=expectedPreflightMain===undefined?undefined:sha(program);
const requireProgram=(root,phase)=>requirePreflightSource(root,expectedPreflightProgram,phase,programRelative,'PROGRAM');
fs.mkdirSync(path.dirname(sourceRoot),{recursive:true});fs.mkdirSync(authorizedBuildRoot,{recursive:true});
// Use the exact shared core/sibling roots gate before any capture is created.
validateSourceSnapshotRoots(primaryRoot,sourceRoot);
const resultPath=path.join(authorizedBuildRoot,'PREPARATION.json');
const state={schemaVersion:1,status:'CAPTURING_SOURCE',preparationStage:options['--stage'],managedInputContract:selectedContract.id,sourceRoot,primaryRoot,workspaceRoot,authorizedBuildRoot,layoutMode,managedBuiltThisRun:false,sourceApprovalGranted:false,publicRuntimeAccepted:false,normalHostMainExecuted:false,realSdkLoaded:false,deviceIo:false,errors:[]};
const save=()=>fs.writeFileSync(resultPath,JSON.stringify(state,null,2)+'\n');
const say=message=>{console.log(message);save();};
try{
 say('Capturing a new complete v'+SNAPSHOT_POLICY.version+' source snapshot for terminal22 compile-only input');
 const guardedReadback=selectedContract.sharedInputPower===true;
 let nextPinCheck=expectedPreflightMain===undefined?undefined:performance.now()+PREFLIGHT_COPY_CHECK_MS;
 const capture=createCapturedSourceSnapshot({repoRoot:primaryRoot,outDir:sourceRoot,guardedReadback,onProgress:event=>{
  if(event.phase==='after-capture-file'){
   if(event.path==='native/main.cpp')requirePreflightSource(sourceRoot,expectedPreflightMain,'captured-main');
   if(event.path===programRelative)requireProgram(sourceRoot,'captured-program');
   if(extraPins.has(event.path))requirePreflightSource(sourceRoot,extraPins.get(event.path),'captured-source',event.path,'SOURCE');
   if(nextPinCheck!==undefined&&performance.now()>=nextPinCheck){
    requirePreflightSource(primaryRoot,expectedPreflightMain,'copy-checkpoint');
    requireProgram(primaryRoot,'copy-checkpoint');
    requireExtra('copy-checkpoint');
    nextPinCheck=performance.now()+PREFLIGHT_COPY_CHECK_MS;
   }
  }
  if(event.phase==='before-source-after'){
   requirePreflightSource(primaryRoot,expectedPreflightMain,event.phase);
   requireProgram(primaryRoot,event.phase);
   requireExtra(event.phase);
  }
  if(['source-before','copy-complete','before-source-after'].includes(event.phase))console.log('capture '+event.phase);
 }});
 state.capture={status:capture.status,manifestFile:capture.manifestFile,copyVerifiedFiles:capture.copyVerifiedFiles};
 if(guardedReadback)state.capture.readback='GUARDED_DESCRIPTOR_READBACK';
 if(capture.status!=='PASS')throw Error('TERMINAL22_SOURCE_CAPTURE_FAILED:'+capture.failure?.message);
 if(expectedPreflightMain!==undefined&&capture.main.snapshot?.sha256!==expectedPreflightMain)throw Error('TERMINAL29_PREFLIGHT_MAIN_SNAPSHOT_IDENTITY_CHANGED');
 // These pins only reject early; the original snapshot and compile checks still decide admission.
 for(const [relative,expected] of extraPins){
  if(!capture.files.some(row=>row.path===relative&&row.sha256===expected))throw Error('TERMINAL29_PREFLIGHT_SOURCE_NOT_CAPTURED:'+relative);
 }
 requireExtra('before-node-binding');
 const manifestFile=path.join(sourceRoot,SNAPSHOT_MANIFEST),manifestSha256=hash(manifestFile);
 say('Binding existing Node tools to the immutable capture; no packages are installed');
 const binding=bindSnapshotNodeTools({manifestFile,expectedSnapshotManifestSha256:manifestSha256,primaryRepo:primaryRoot,allowHistoricalSnapshot:true});
 state.nodeBinding={status:binding.status,path:binding.bindingManifestFile};if(binding.status!=='PASS')throw Error('TERMINAL22_NODE_BINDING_FAILED');
 const imported=relative=>import(pathToFileURL(path.join(sourceRoot,relative)).href);
 const contracts=await imported(moduleRelative+'/r3-managed-input-contract.mjs');
 const contract=contracts.requireTerminalManagedContract(selectedContract.id);
 const spec=JSON.parse(fs.readFileSync(path.join(sourceRoot,'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json'),'utf8'));
 const dotnetPath=path.join(spec.dotnetRoot,'dotnet.exe');
 const layout={schemaVersion:1,sourceRoot,primaryRoot,workspaceRoot,authorizedBuildRoot,
  productMainSha256:hash(path.join(sourceRoot,'native/main.cpp')),manifestSha256,
  nodeBindingSha256:hash(binding.bindingManifestFile),expandedSourceSha256:sha(readVirtualControllerSource(path.join(sourceRoot,'native/main.cpp')).source),
  verifierCodeSha256:hash(path.join(sourceRoot,'tools/ymcc_sim/global/source-snapshot.mjs')),
  expectationsSha256:hash(path.join(sourceRoot,moduleRelative,'r3-current105-expectations.json')),
  sourceAuthority:{mode:'EXPLICIT_CURRENT_CAPTURE_COMPILE_ONLY'},managedInputContract:contract.id,
  managedContractCodeSha256:hash(path.join(sourceRoot,moduleRelative,'r3-managed-input-contract.mjs')),
  expectedManagedSources:contract.names.map(relative=>({relative,sha256:hash(path.join(sourceRoot,relative))})),
  dotnet:{path:dotnetPath,sha256:hash(dotnetPath)}};
 contracts.validateManagedCompileRows(layout.expectedManagedSources,contract.id);
 const layoutPath=path.join(authorizedBuildRoot,'LAYOUT-terminal'+contract.count+'.json');fs.writeFileSync(layoutPath,JSON.stringify(layout,null,2)+'\n');state.layout={path:layoutPath,sha256:hash(layoutPath)};
 if(options['--stage']==='capture'){
  state.status='SOURCE_CAPTURED_NOT_COMPILED';state.sourceCaptureOnly=true;state.managedBuiltThisRun=false;
  say('Source capture and exact managed membership recorded; no compile batch or compiler was started');return state;
 }
 requireExtra('before-managed-authority');
 await compilePreparedManaged({layout,out:authorizedBuildRoot,state,say,requireCurrent:selectedContract.sharedInputPower===true});
 state.status=terminalManagedStageStatus('managed',selectedContract.id);state.managedBuiltThisRun=true;say('Managed terminal22 build completed; full coordinator runtime remains unaccepted');
}catch(error){state.status='INCOMPLETE';state.errors.push({message:error.message,stack:error.stack,...(error.details?{details:error.details}:{})});save();throw error;}

return state;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))await prepareTerminal22();
