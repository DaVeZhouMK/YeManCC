// Versioned build-only preparation. Never declares a public runtime PASS.
import fs from 'node:fs';
import path from 'node:path';
import {createHash,randomUUID} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createCapturedSourceSnapshot,validateSourceSnapshotRoots,SNAPSHOT_MANIFEST,SNAPSHOT_POLICY} from '../source-snapshot.mjs';
import {bindSnapshotNodeTools} from '../snapshot-node-tools.mjs';
import {readVirtualControllerSource} from '../../../virtual_controller_runtime_source.mjs';
import {requireTerminalManagedContract,terminalManagedStageStatus,verifyManagedDecoderCompileEvidence} from './r3-managed-input-contract.mjs';
const moduleRelative='tools/ymcc_sim/global/controller-coordinator-pipe';
const defaultRepo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const pathKey=value=>path.resolve(value).toLowerCase();
const strictChild=(root,file)=>{const relative=path.relative(root,file);return relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);};
function noPreparationReparse(file){
 for(let current=path.resolve(file);;current=path.dirname(current)){
  let stat;try{stat=fs.lstatSync(current);}catch(error){if(error.code!=='ENOENT')throw error;}
  if(stat&&(stat.isSymbolicLink()||pathKey(fs.realpathSync(current))!==pathKey(current)))throw Error('TERMINAL22_PREPARATION_REPARSE_PATH');
  if(path.dirname(current)===current)break;
 }
}
export function resolveTerminal22PreparationLayout({primaryRoot,workspaceRoot}={}){
 if(typeof primaryRoot!=='string'||!path.isAbsolute(primaryRoot)||/[\0\r\n]/.test(primaryRoot))throw Error('TERMINAL22_ABSOLUTE_PRIMARY_REQUIRED');
 if(typeof workspaceRoot!=='string'||!path.isAbsolute(workspaceRoot)||/[\0\r\n]/.test(workspaceRoot))throw Error('FRESH_ABSOLUTE_WORKSPACE_REQUIRED');
 primaryRoot=path.resolve(primaryRoot);workspaceRoot=path.resolve(workspaceRoot);
 noPreparationReparse(primaryRoot);noPreparationReparse(workspaceRoot);
 if(!fs.lstatSync(primaryRoot).isDirectory())throw Error('TERMINAL22_PRIMARY_DIRECTORY_REQUIRED');
 if(fs.existsSync(workspaceRoot))throw Error('FRESH_ABSOLUTE_WORKSPACE_REQUIRED');
 const sourceWorkspace=path.resolve(primaryRoot,'../../..');
 const coreBase=path.join(sourceWorkspace,'Testing','YMCC','Workspace'),evidenceBase=path.join(sourceWorkspace,'Testing','YMCC','Evidence');
 const coreOwned=strictChild(coreBase,workspaceRoot),legacyEvidence=strictChild(evidenceBase,workspaceRoot);
 if(!coreOwned&&!legacyEvidence)throw Error('OWNED_TESTING_EVIDENCE_WORKSPACE_REQUIRED');
 if(coreOwned&&pathKey(primaryRoot)!==pathKey(path.join(sourceWorkspace,'Mainline','YeManCC-source','YeManCC')))throw Error('TERMINAL22_CORE_PRIMARY_LAYOUT_REQUIRED');
 const sourceRoot=coreOwned?path.join(workspaceRoot,'Mainline','YeManCC-source','YeManCC'):path.join(path.dirname(primaryRoot),path.basename(primaryRoot)+'-terminal22-'+randomUUID());
 return {primaryRoot,workspaceRoot,sourceRoot,authorizedBuildRoot:path.join(workspaceRoot,'Build/Validation/CurrentCoordinatorTerminal22'),layoutMode:coreOwned?'G_CORE_WORKSPACE_OWNER':'LEGACY_EVIDENCE_SIBLING'};
}

export function parseTerminalManagedPreparationArguments(args){
 if(!Array.isArray(args))throw Error('STRICT_PREPARATION_ARGUMENTS');const options={};
 for(let i=0;i<args.length;i++){const key=args[i];if(!['--repo','--workspace','--managed-input-contract'].includes(key)||Object.hasOwn(options,key)||typeof args[i+1]!=='string'||!args[i+1]||args[i+1].startsWith('--'))throw Error('STRICT_PREPARATION_ARGUMENTS');options[key]=args[++i];}
 const contract=requireTerminalManagedContract(options['--managed-input-contract']);options['--managed-input-contract']=contract.id;return options;
}

export async function prepareTerminal22(args=process.argv.slice(2)){
const options=parseTerminalManagedPreparationArguments(args);
const selectedContract=requireTerminalManagedContract(options['--managed-input-contract']);
const {primaryRoot,workspaceRoot,sourceRoot,authorizedBuildRoot,layoutMode}=resolveTerminal22PreparationLayout({primaryRoot:path.resolve(options['--repo']??defaultRepo),workspaceRoot:options['--workspace']});
fs.mkdirSync(path.dirname(sourceRoot),{recursive:true});fs.mkdirSync(authorizedBuildRoot,{recursive:true});
// Use the exact shared core/sibling roots gate before any capture is created.
validateSourceSnapshotRoots(primaryRoot,sourceRoot);
const sha=bytes=>createHash('sha256').update(bytes).digest('hex'),hash=p=>sha(fs.readFileSync(p));
const resultPath=path.join(authorizedBuildRoot,'PREPARATION.json');
const state={schemaVersion:1,status:'CAPTURING_SOURCE',managedInputContract:selectedContract.id,sourceRoot,primaryRoot,workspaceRoot,authorizedBuildRoot,layoutMode,sourceApprovalGranted:false,publicRuntimeAccepted:false,normalHostMainExecuted:false,realSdkLoaded:false,deviceIo:false,errors:[]};
const save=()=>fs.writeFileSync(resultPath,JSON.stringify(state,null,2)+'\n');
const say=message=>{console.log(message);save();};
try{
 say('Capturing a new complete v'+SNAPSHOT_POLICY.version+' source snapshot for terminal22 compile-only input');
 const capture=createCapturedSourceSnapshot({repoRoot:primaryRoot,outDir:sourceRoot,onProgress:event=>{if(['source-before','copy-complete','before-source-after'].includes(event.phase))console.log('capture '+event.phase);}});
 state.capture={status:capture.status,manifestFile:capture.manifestFile,copyVerifiedFiles:capture.copyVerifiedFiles};
 if(capture.status!=='PASS')throw Error('TERMINAL22_SOURCE_CAPTURE_FAILED:'+capture.failure?.message);
 const manifestFile=path.join(sourceRoot,SNAPSHOT_MANIFEST),manifestSha256=hash(manifestFile);
 say('Binding existing Node tools to the immutable capture; no packages are installed');
 const binding=bindSnapshotNodeTools({manifestFile,expectedSnapshotManifestSha256:manifestSha256,primaryRepo:primaryRoot,allowHistoricalSnapshot:true});
 state.nodeBinding={status:binding.status,path:binding.bindingManifestFile};if(binding.status!=='PASS')throw Error('TERMINAL22_NODE_BINDING_FAILED');
 const imported=relative=>import(pathToFileURL(path.join(sourceRoot,relative)).href);
 const contracts=await imported(moduleRelative+'/r3-managed-input-contract.mjs');
 const authority=await imported(moduleRelative+'/r3-current-compile-batch.mjs');
 const {buildManagedFixture}=await imported(moduleRelative+'/r3-current-managed-builder.mjs');
 const {toolchainFiles}=await imported('tools/ymcc_sim/global/inputhost-protocol/adapter.mjs');
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
 const actualToolFiles=[...new Set([...toolchainFiles(sourceRoot),process.execPath,...['r3-current-managed-builder.mjs','r3-current-compile-batch.mjs','r3-managed-input-contract.mjs'].map(name=>path.join(sourceRoot,moduleRelative,name))])];
 say('Opening one exact tool/source compile batch; terminal22 cannot use legacy21 authority');
 const token=authority.beginCurrentCompileBatch({layout,actualToolFiles,receiptDirectory:path.join(authorizedBuildRoot,'authority')});
 try{
  state.status='COMPILING_MANAGED'+contract.count;say('Compiling the actual current LoopResult, HostSession and unique terminal policy once');
  state.managed=buildManagedFixture(path.join(authorizedBuildRoot,'managed'+contract.count),{sourceRoot,batchAuthority:token});
  const manifest=JSON.parse(fs.readFileSync(state.managed.manifestPath,'utf8'));
  const mapped=authority.verifyCurrentManagedSourceRows(manifest,sourceRoot,{batchAuthority:token});
  if(mapped.length!==contract.count||manifest.managedInputContract!==contract.id||manifest.terminalCleanupCompiled!==true)throw Error('ACTUAL_SELECTED_TERMINAL_CONSUMER_REQUIRED');
   verifyManagedDecoderCompileEvidence(manifest,contract.id,authorizedBuildRoot);
  state.managedMemberCount=mapped.length;state.terminalCleanupCompiledExactlyOnce=true;
 }finally{state.batchAfter=authority.finishCurrentCompileBatch(token);}
 state.status=terminalManagedStageStatus('managed',selectedContract.id);say('Managed terminal22 build completed; full coordinator runtime remains unaccepted');
}catch(error){state.status='INCOMPLETE';state.errors.push({message:error.message,stack:error.stack});save();throw error;}

return state;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))await prepareTerminal22();
