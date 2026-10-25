import {applyRuntimeHardening,runtimeHardeningProfile} from './input_coordinator_runtime_hardening.mjs';
import {applyPassingChain,passingChainProfile,PASSING_CHAIN_SOURCE_PROFILE,PASSING_CHAIN_TOOL_INPUTS} from './input_coordinator_passing_chain.mjs';
export const PASSING_CHAIN_PREPARE_TOOL_INPUTS=Object.freeze(['tools/input_coordinator_private_terminal_prepare.mjs',...PASSING_CHAIN_TOOL_INPUTS]);
// Source preparation / read-only launch planning. No target process or assembly loader exists here.
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {applyPrivateTerminalClosure,hashPrivateSource,PRIVATE_TERMINALS} from './input_coordinator_private_terminals.mjs';
const need=(x,m)=>{if(!x)throw Error('PRIVATE_PREPARE_'+m);},roots=new WeakMap();
export const FIXED_PRIVATE_DOTNET='C:/Program Files/dotnet/dotnet.exe';
const same=(a,b)=>path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase();
const within=(root,file)=>{const rel=path.relative(path.resolve(root),path.resolve(file));return rel!==''&&rel!=='..'&&!rel.startsWith('..'+path.sep)&&!path.isAbsolute(rel);};
function noReparse(file){need(path.isAbsolute(file??''),'ABSOLUTE_PATH');let p=path.resolve(file);for(;;){need(!fs.lstatSync(p).isSymbolicLink(),'REPARSE_PATH');const parent=path.dirname(p);if(parent===p)break;p=parent;}need(same(fs.realpathSync(file),file),'REDIRECTED_PATH');}
export function exactPrivateFile(pin){need(pin&&path.isAbsolute(pin.path??'')&&/^[a-f0-9]{64}$/.test(pin.sha256??''),'EXACT_PIN_REQUIRED');noReparse(pin.path);need(fs.statSync(pin.path).isFile()&&hashPrivateSource(fs.readFileSync(pin.path))===pin.sha256,'PIN_CHANGED:'+pin.path);return {path:path.resolve(pin.path),sha256:pin.sha256};}
const file=p=>({path:path.resolve(p),sha256:hashPrivateSource(fs.readFileSync(p))});
const data=p=>JSON.parse(fs.readFileSync(p,'utf8'));
export function preparePrivateSource({ownedParent,outputDirectory,inputPins,observerPin}={}){
 noReparse(ownedParent);need(path.isAbsolute(outputDirectory??'')&&within(ownedParent,outputDirectory)&&!fs.existsSync(outputDirectory)&&same(path.dirname(outputDirectory),ownedParent),'FRESH_DIRECT_CHILD_SOURCE_OUTPUT');
 const keys=['inert','msiRuntime','fixture','pipe'];const checked=Object.fromEntries(keys.map(k=>[k,exactPrivateFile(inputPins?.[k])]));const observer=exactPrivateFile(observerPin);
 const texts=Object.fromEntries(keys.map(k=>[k,fs.readFileSync(checked[k].path,'utf8')]));texts.observerHeader=fs.readFileSync(observer.path,'utf8');
 const expected=Object.fromEntries([...keys.map(k=>[k,checked[k].sha256]),['observerHeader',observer.sha256]]);const transformed=applyPrivateTerminalClosure(texts,{expected});
 fs.mkdirSync(outputDirectory);const native=path.join(outputDirectory,'source/native');fs.mkdirSync(native,{recursive:true});
 const generated={inert:path.join(native,'main.inert.cpp'),msiRuntime:path.join(native,'msi_crash_recovery_runtime.private_fixture.h'),fixture:path.join(outputDirectory,'fixture.cpp'),pipe:path.join(outputDirectory,'pipe_cases.inc'),observerHeader:path.join(native,'private_terminal_observer.h')};
 for(const k of Object.keys(generated))fs.writeFileSync(generated[k],transformed[k],{flag:'wx'});
 const receipt={...transformed.evidence,inputFiles:checked,observerInput:observer,generated:Object.fromEntries(Object.entries(generated).map(([k,p])=>[k,file(p)])),compilerExecuted:false,targetExecuted:false,targetExecutionAllowed:false};
 fs.writeFileSync(path.join(outputDirectory,'PRIVATE-TERMINAL-SOURCE.json'),JSON.stringify(receipt,null,2)+'\n',{flag:'wx'});return receipt;
}
function bindings(pins){
 const required=['exe','dotnet','managedDll','managedManifest','sourceManifest','compileReceipt','metadata'];const checked=Object.fromEntries(required.map(k=>[k,exactPrivateFile(pins?.[k])]));
 need(same(checked.dotnet.path,FIXED_PRIVATE_DOTNET),'FACTORY_FIXED_DOTNET_REQUIRED');need(/\.exe$/i.test(checked.exe.path)&&/\.dll$/i.test(checked.managedDll.path)&&same(checked.managedManifest.path,path.join(path.dirname(checked.managedDll.path),'fixture-manifest.json')),'FIXTURE_ENTRY_FILENAMES_REQUIRED');
 const build=data(checked.compileReceipt.path),sm=data(checked.sourceManifest.path),managed=data(checked.managedManifest.path),metadata=data(checked.metadata.path),closure=build.privateTerminalClosure;
 need(build.status==='COMPILED_NOT_EXECUTED'&&build.compileOnly===true&&build.fixtureExecuted===false&&build.nativeExecuted===false&&build.runBranchReached===false,'ACTUAL_COMPILE_ONLY_RECEIPT_REQUIRED');
 need(closure?.kind==='PRIVATE_THREE_TERMINAL_LEAF_CLOSURE'&&closure.symbols?.length===3&&new Set(closure.symbols).size===3&&PRIVATE_TERMINALS.every(s=>closure.symbols.includes(s)),'NEW_THREE_TERMINAL_COMPILE_PROVENANCE_REQUIRED');need(closure.original21Unchanged===true&&closure.originalAssertionsAndMutationsUnchanged===true&&closure.decisionHooks?.length===5&&closure.decisionHooks.every(r=>r.unchanged===true&&/^[a-f0-9]{64}$/.test(r.sha256??'')),'RETAINED_ORIGINAL_DECISIONS_REQUIRED');
 need(same(build.exe?.path??'',checked.exe.path)&&build.exe.sha256===checked.exe.sha256&&same(build.sourceManifest?.path??'',checked.sourceManifest.path)&&build.sourceManifest.sha256===checked.sourceManifest.sha256,'COMPILE_ARTIFACT_CHAIN');
 need(hashPrivateSource(JSON.stringify(sm.privateTerminalClosure))===hashPrivateSource(JSON.stringify(closure)),'COMPILE_SOURCE_CLOSURE_DIFFERS');
 need(Array.isArray(sm.compiledGenerated)&&sm.compiledGenerated.length>0&&sm.compiledGenerated.length<=3072,'ORIGINAL_INPUT_BUDGET');for(const row of sm.compiledGenerated)exactPrivateFile(row);
 const names={inert:'source/native/main.inert.cpp',msiRuntime:'source/native/msi_crash_recovery_runtime.private_fixture.h',fixture:'fixture.cpp',pipe:'pipe_cases.inc',observerHeader:'source/native/private_terminal_observer.h'};
 const generated={};for(const [key,suffix] of Object.entries(names)){const found=sm.compiledGenerated.filter(r=>r.path.replaceAll('\\','/').endsWith('/'+suffix));need(found.length===1&&found[0].sha256===closure.outputHashes?.[key],'GENERATED_CLOSURE_MEMBER:'+key);generated[key]=found[0];}
 const pipe=fs.readFileSync(generated.pipe.path,'utf8');need(pipe.includes('const std::wstring dotnet=L"C:\\\\Program Files\\\\dotnet\\\\dotnet.exe";')&&pipe.includes('std::to_wstring(GetCurrentProcessId())')&&pipe.includes('ymccPrivateTerminal::observeChildCreated(observation,pi.dwProcessId);'),'OWNED_PARENT_FACTORY_REQUIRED');
 need(managed.status==='BUILT'&&managed.fixtureExecuted===false&&managed.realSdkLoaded===false&&managed.hardwareIo===false&&same(managed.fixtureDll,checked.managedDll.path)&&same(managed.manifestPath??'',checked.managedManifest.path),'SDK_FREE_SAME_BATCH_MANAGED_REQUIRED');
 need(build.managedProof?.dll?.sha256===checked.managedDll.sha256&&same(build.managedProof.dll.path,checked.managedDll.path)&&build.managedProof.manifestSha256===checked.managedManifest.sha256&&same(build.managedProof.manifest,checked.managedManifest.path),'COMPILED_MANAGED_TOOL_CHAIN');
 const sources=managed.inputBindings?.sources;need(Array.isArray(sources)&&sources.length===20&&new Set(sources.map(r=>r.relative)).size===20&&Array.isArray(managed.evaluatedCompilerInputs)&&managed.evaluatedCompilerInputs.length>0,'ACTUAL_COMPILED20_AND_TOOL_METADATA_REQUIRED');
 for(const r of sources){exactPrivateFile(r);exactPrivateFile({path:r.snapshot,sha256:r.sha256});}
 for(const r of [...managed.inputBindings.generatedInputs,...managed.runtimeArtifacts,...managed.evaluatedCompilerInputs,...managed.inputBindings.toolchainAllowlist])exactPrivateFile(r);
 for(const suffix of ['.deps.json','.runtimeconfig.json']){const sibling=checked.managedDll.path.replace(/\.dll$/i,suffix);need(managed.runtimeArtifacts.some(r=>same(r.path,sibling)),'EXACT_RUNTIME_SIBLING_REQUIRED:'+suffix);}
 need(managed.runtimeArtifacts.some(r=>same(r.path,checked.managedDll.path)&&r.sha256===checked.managedDll.sha256),'MANAGED_DLL_RUNTIME_MEMBER_REQUIRED');
 need(metadata.status==='METADATA_READ_ONLY'&&metadata.targetAssembliesLoaded===0,'METADATA_ONLY_RECEIPT');
 for(const key of ['exe','managedDll'])need(metadata.targets?.some(r=>same(r.path,checked[key].path)&&r.sha256===checked[key].sha256&&r.targetAssemblyLoaded===false),'EXACT_METADATA_TARGET:'+key);
 const managedPe=metadata.targets.find(r=>same(r.path,checked.managedDll.path));need(managedPe.bclReferencesOnly===true&&managedPe.pinvokes?.length===2&&['OpenProcess','WaitForSingleObject'].every(entry=>managedPe.pinvokes.some(r=>r.entry===entry&&r.method==='ParentProcessLifetime::'+entry&&/^kernel32(?:\.dll)?$/i.test(r.module))),'MANAGED_PARENT_ONLY_PE_TERMINALS');
 return {checked,closure,generated,managedSources:20};
}
export function preparePrivateLaunch({ownedParent,runName,pins,mode='full-pipe'}={}){
 need(mode==='full-pipe','UNIQUE_FULL_PIPE_MODE');need(typeof runName==='string'&&/^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/.test(runName),'FRESH_RUN_NAME');noReparse(ownedParent);need(fs.statSync(ownedParent).isDirectory(),'OWNED_PARENT_DIRECTORY');
 const proof=bindings(pins),runRoot=path.join(ownedParent,runName);need(!fs.existsSync(runRoot),'EXCLUSIVE_FRESH_ROOT_REQUIRED');fs.mkdirSync(runRoot);noReparse(runRoot);need(fs.readdirSync(runRoot).length===0,'EMPTY_ROOT_REQUIRED');
 const token=Object.freeze({kind:'PRIVATE_EMPTY_ROOT_PLAN_CAPABILITY'});roots.set(token,{runRoot,ownedParent,pins:structuredClone(pins),proof,used:false});return token;
}
export function planPrivateLaunch(token){
 const state=roots.get(token);need(state&&!state.used,'LIVE_PRIVATE_ROOT_CAPABILITY');noReparse(state.ownedParent);noReparse(state.runRoot);need(fs.readdirSync(state.runRoot).length===0,'EMPTY_ROOT_REQUIRED');const proof=bindings(state.pins);state.used=true;
 return {status:'PLAN_ONLY_NOT_EXECUTION_AUTHORIZATION',mode:'full-pipe',exe:proof.checked.exe,args:[state.runRoot,'full-pipe',proof.checked.managedDll.path],cwd:state.runRoot,env:{SystemRoot:process.env.SystemRoot??'C:\\Windows',YEMAN_POWER_CONTROL_DIR:state.runRoot,DOTNET_ROOT:path.dirname(FIXED_PRIVATE_DOTNET),DOTNET_MULTILEVEL_LOOKUP:'0'},managedParent:'ONLY_ORIGINAL_NATIVE_FACTORY_ACTUAL_GetCurrentProcessId',pins:proof.checked,sourceClosure:proof.closure,sourceAuthorityAdmissionRequired:true,actualParentOwnershipAndLoadedRuntimeReviewRequired:true,nativeTimeoutMs:60000,producerPrepareTimeoutMs:90000,leafTimeoutMs:120000,group2x60EqualsSingle60:false,targetExecutionAllowed:false,compilerExecutionAllowed:false,normalHostMainAllowed:false};
}
if(process.argv[1]&&same(process.argv[1],fileURLToPath(import.meta.url))){
 need(process.argv.length===4&&['--prepare-source','--prepare-hardened-source','--prepare-passing-chain-source','--launch-plan'].includes(process.argv[2]),'USE_EXPLICIT_SOURCE_OR_LAUNCH_PLAN_CONFIG');const config=data(path.resolve(process.argv[3]));const result=process.argv[2]==='--prepare-source'?preparePrivateSource(config):process.argv[2]==='--prepare-hardened-source'?prepareRuntimeHardenedSource(config):process.argv[2]==='--prepare-passing-chain-source'?preparePassingChainSource(config):planPrivateLaunch(preparePrivateLaunch(config));process.stdout.write(JSON.stringify(result,null,2)+'\n');
}

// Explicit preparation stage after the three-terminal closure has generated its inputs.
// Returns new sources only; original directories, binaries and failure receipts stay intact.
export function prepareRuntimeHardenedSource({ownedParent,outputDirectory,inputPins}={}){
 noReparse(ownedParent);need(path.isAbsolute(outputDirectory??'')&&within(ownedParent,outputDirectory)&&same(path.dirname(outputDirectory),ownedParent)&&!fs.existsSync(outputDirectory),'FRESH_HARDENED_SOURCE_OUTPUT');
 const profile=runtimeHardeningProfile(),required=profile.files.filter(r=>r.beforeSha256!==null),keys=required.map(r=>r.path).sort();
 need(inputPins&&JSON.stringify(Object.keys(inputPins).sort())===JSON.stringify(keys),'EXACT_HARDENED_INPUT_SET');
 const sourceTexts={},checked={};for(const row of required){const pin=exactPrivateFile(inputPins[row.path]);need(pin.sha256===row.beforeSha256,'HARDENED_BASELINE_CHANGED:'+row.path);checked[row.path]=pin;sourceTexts[row.path]=fs.readFileSync(pin.path,'utf8');}
 const changed=applyRuntimeHardening(sourceTexts);fs.mkdirSync(outputDirectory);const generated={};
 for(const [relative,text] of Object.entries(changed.files)){const destination=path.resolve(outputDirectory,relative);need(within(outputDirectory,destination),'HARDENED_OUTPUT_ESCAPE');fs.mkdirSync(path.dirname(destination),{recursive:true});fs.writeFileSync(destination,text,{flag:'wx'});generated[relative]=file(destination);}
 const receipt={...changed.evidence,inputFiles:checked,generated,compilerExecuted:false,targetExecutionAllowed:false};
 fs.writeFileSync(path.join(outputDirectory,'RUNTIME-HARDENING-SOURCE.json'),JSON.stringify(receipt,null,2)+'\n',{flag:'wx'});return receipt;
}

// Explicit second source stage; consumes exact existing hardened outputs, never an executable.
export function preparePassingChainSource({ownedParent,outputDirectory,inputPins,sourceProfile}={}){
 need(sourceProfile===PASSING_CHAIN_SOURCE_PROFILE,'EXPLICIT_PASSING_SOURCE_PROFILE_REQUIRED');noReparse(ownedParent);need(path.isAbsolute(outputDirectory??'')&&within(ownedParent,outputDirectory)&&same(path.dirname(outputDirectory),ownedParent)&&!fs.existsSync(outputDirectory),'FRESH_PASSING_SOURCE_OUTPUT');
 const profile=passingChainProfile(),required=profile.inputFiles,keys=required.map(r=>r.path).sort();need(inputPins&&JSON.stringify(Object.keys(inputPins).sort())===JSON.stringify(keys),'EXACT_PASSING_INPUT_SET');const sourceTexts={},checked={};
 for(const row of required){const pin=exactPrivateFile(inputPins[row.path]);need(pin.sha256===row.sha256,'PASSING_BASELINE_CHANGED:'+row.path);checked[row.path]=pin;sourceTexts[row.path]=fs.readFileSync(pin.path,'utf8');}
 const changed=applyPassingChain(sourceTexts,{sourceProfile});fs.mkdirSync(outputDirectory);const generated={};for(const [relative,text] of Object.entries(changed.files)){const destination=path.resolve(outputDirectory,relative);need(within(outputDirectory,destination),'PASSING_OUTPUT_ESCAPE');fs.mkdirSync(path.dirname(destination),{recursive:true});fs.writeFileSync(destination,text,{flag:'wx'});generated[relative]=file(destination);}
 const receipt={...changed.evidence,inputFiles:checked,generated,toolInputs:PASSING_CHAIN_PREPARE_TOOL_INPUTS,compilerExecutionAllowed:false,targetExecutionAllowed:false,currentPrimaryAccepted:false};fs.writeFileSync(path.join(outputDirectory,'PASSING-CHAIN-SOURCE.json'),JSON.stringify(receipt,null,2)+'\n',{flag:'wx'});return receipt;
}
