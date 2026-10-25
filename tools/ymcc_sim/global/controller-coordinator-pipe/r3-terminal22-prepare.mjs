// Versioned build-only preparation. Never declares a public runtime PASS.
import fs from 'node:fs';
import path from 'node:path';
import {createHash,randomUUID} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createCapturedSourceSnapshot,SNAPSHOT_MANIFEST} from '../source-snapshot.mjs';
import {bindSnapshotNodeTools} from '../snapshot-node-tools.mjs';
import {readVirtualControllerSource} from '../../../virtual_controller_runtime_source.mjs';
const moduleRelative='tools/ymcc_sim/global/controller-coordinator-pipe';
const defaultRepo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const args=process.argv.slice(2),options={};
for(let i=0;i<args.length;i++){const key=args[i];if(!['--repo','--workspace'].includes(key)||Object.hasOwn(options,key)||!args[i+1])throw Error('STRICT_PREPARATION_ARGUMENTS');options[key]=args[++i];}
const primaryRoot=path.resolve(options['--repo']??defaultRepo),workspaceRoot=options['--workspace'];
if(!workspaceRoot||!path.isAbsolute(workspaceRoot)||fs.existsSync(workspaceRoot))throw Error('FRESH_ABSOLUTE_WORKSPACE_REQUIRED');
const evidenceBase=path.resolve(primaryRoot,'../../../Testing/YMCC/Evidence');
const evidenceRelative=path.relative(evidenceBase,workspaceRoot);
if(!evidenceRelative||path.isAbsolute(evidenceRelative)||evidenceRelative==='..'||evidenceRelative.startsWith('..'+path.sep))throw Error('OWNED_TESTING_EVIDENCE_WORKSPACE_REQUIRED');
// The immutable capture API deliberately requires a sibling of the product.
// Build artifacts still stay in the owned Testing layer; no old gate changes.
const sourceRoot=path.join(path.dirname(primaryRoot),path.basename(primaryRoot)+'-terminal22-'+randomUUID());
const authorizedBuildRoot=path.join(workspaceRoot,'Build/Validation/CurrentCoordinatorTerminal22');
fs.mkdirSync(path.dirname(sourceRoot),{recursive:true});fs.mkdirSync(authorizedBuildRoot,{recursive:true});
const sha=bytes=>createHash('sha256').update(bytes).digest('hex'),hash=p=>sha(fs.readFileSync(p));
const resultPath=path.join(authorizedBuildRoot,'PREPARATION.json');
const state={schemaVersion:1,status:'CAPTURING_SOURCE',sourceRoot,primaryRoot,workspaceRoot,authorizedBuildRoot,sourceApprovalGranted:false,publicRuntimeAccepted:false,normalHostMainExecuted:false,realSdkLoaded:false,deviceIo:false,errors:[]};
const save=()=>fs.writeFileSync(resultPath,JSON.stringify(state,null,2)+'\n');
const say=message=>{console.log(message);save();};
try{
 say('Capturing a new complete v7 source snapshot for terminal22 compile-only input');
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
 const contract=contracts.managedCompileContract(contracts.TERMINAL_MANAGED_CONTRACT);
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
 const layoutPath=path.join(authorizedBuildRoot,'LAYOUT-terminal22.json');fs.writeFileSync(layoutPath,JSON.stringify(layout,null,2)+'\n');state.layout={path:layoutPath,sha256:hash(layoutPath)};
 const actualToolFiles=[...new Set([...toolchainFiles(sourceRoot),process.execPath,...['r3-current-managed-builder.mjs','r3-current-compile-batch.mjs','r3-managed-input-contract.mjs'].map(name=>path.join(sourceRoot,moduleRelative,name))])];
 say('Opening one exact tool/source compile batch; terminal22 cannot use legacy21 authority');
 const token=authority.beginCurrentCompileBatch({layout,actualToolFiles,receiptDirectory:path.join(authorizedBuildRoot,'authority')});
 try{
  state.status='COMPILING_MANAGED22';say('Compiling the actual current LoopResult, HostSession and unique terminal policy once');
  state.managed=buildManagedFixture(path.join(authorizedBuildRoot,'managed22'),{sourceRoot,batchAuthority:token});
  const manifest=JSON.parse(fs.readFileSync(state.managed.manifestPath,'utf8'));
  const mapped=authority.verifyCurrentManagedSourceRows(manifest,sourceRoot,{batchAuthority:token});
  if(mapped.length!==22||manifest.terminalCleanupCompiled!==true)throw Error('ACTUAL_TERMINAL22_CONSUMER_REQUIRED');
  state.managedMemberCount=mapped.length;state.terminalCleanupCompiledExactlyOnce=true;
 }finally{state.batchAfter=authority.finishCurrentCompileBatch(token);}
 state.status='MANAGED22_COMPILED_NOT_RUNTIME_ACCEPTED';say('Managed terminal22 build completed; full coordinator runtime remains unaccepted');
}catch(error){state.status='INCOMPLETE';state.errors.push({message:error.message,stack:error.stack});save();throw error;}
