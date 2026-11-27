// Fixed software capture, selected C++ and used-fixture inputs only. No launch or cache authority.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {isDeepStrictEqual} from 'node:util';
import {execFileSync} from 'node:child_process';

export const FIXED_ARTIFACT_PROFILE_KIND='CURRENT26_FIXED_CPP_ARTIFACT_SOFTWARE_V1';
const MODULE='tools/ymcc_sim/global/controller-coordinator-pipe';
const SELF=fileURLToPath(import.meta.url);
const R='G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC';
const W='G:/YeManCC-Work/Testing/YMCC/Workspace/R105CP-20261117A';
const B=W+'/Build/Validation/CurrentCoordinatorTerminal22';
const S=W+'/Mainline/YeManCC-source/YeManCC';
const E='G:/YeManCC-Work/Testing/YMCC/Evidence/BacklogAssist-20261102/Runtime105CurrentMembership-20261114/native26-routing/lifecycle-runtime-layout/real-store-original-consumer-20261116/r4-intake-20261117/current-primary/shared-role-batch';
const D=E+'/formal-owned-runtime-r9/generated-r13';
const ID='current105-terminal26-current-capture-20261117a';
const CONTRACT='current105-terminal26-sdk-lifetime-compact-v1';
const CAPTURE='03bc36a4c6546e3a1d0ba505aa12aa8cd2c22da0249291ef4b7b66f58897eee9';
const POLICY='owned-direct-child-v1';
const ROLES={positive:null,cancellation:'rollback-cancellation-guards',psOwnership:'rollback-ps-endpoint-ownership',psBinding:'rollback-ps-binding-revision'};
const ALL_ROLES={...ROLES,switchReturn:'rollback-switch-return-rearm',switchOwner:'rollback-switch-fault-owner',recoveryInheritance:'rollback-recovery-inheritance',bypassAdmission:'bypass-admission'};
const ROLE_DIR={positive:'np2/p',cancellation:'nb3/cancel',psOwnership:'nb3/psown',psBinding:'nb3/psbind',switchReturn:'nb3/swret',switchOwner:'nb3/swown',recoveryInheritance:'nb3/recover',bypassAdmission:'nb3/admit'};
const FIXED_DESCRIPTOR={path:D+'/FIXED-SOURCE-DESCRIPTOR-owned-r13.json',bytes:17330112,sha256:'f8e5b407c9e1e93fb21af6e7b4011779d7a69224e1ffb931c960a3da6f1bc793'};
const FIXED_EXPECTATIONS={path:D+'/SOURCE-EXPECTATIONS-current26-owned-r13.json',bytes:4509852,sha256:'a1f2b703005d321230672a86d6039ae0eccb8771bd33ebec0ce96179ed230b1f'};
const ARCHIVE_ROOT='G:/YeManCC-Work/Testing/YMCC/Workspace/vfixpipe26-archive';
const RECEIPTS={positive:{path:E+'/OWNED-CURRENT-FOUR-PIPE-RESULT-r10.json',bytes:2413213,sha256:'17864dd733e87e6d553222b3668a4a5fcae581332722553ab6e040921849586e'},negative:{path:E+'/OWNED-CURRENT-THREE-PIPE-CONTINUATION-RESULT-r11.json',bytes:7177251,sha256:'8818944b4f7f817662e3ec6a86855d54ae5e689380f23d7c03b3e00dba53f99f'}};
const PROFILE_KEYS='schemaVersion kind status scope workspaceRoot authorizedBuildRoot sourceRoot descriptor expectations qualification currentPipeOracle managedInputContract sourceCaptureManifestSha256 toolClosureComplete cacheAcceptance currentPrimaryAccepted nativeBudgetMs leafBudgetMs cleanupReserveMs powershell node'.split(' ');
const live=new WeakMap();
// Node's lstat identifies symbolic links/junctions, but does not expose every Windows reparse tag.
// This fixed read-only attribute query rejects FILE_ATTRIBUTE_REPARSE_POINT on all declared files/parents.
const ATTRIBUTE_QUERY="$ErrorActionPreference='Stop';$paths=ConvertFrom-Json ([Console]::In.ReadToEnd());foreach($p in $paths){$a=[System.IO.File]::GetAttributes([string]$p);if(($a-band[System.IO.FileAttributes]::ReparsePoint)-ne 0){throw ('REPARSE_POINT:'+ $p)}};[Console]::Out.Write($paths.Count)";
const need=(ok,message)=>{if(!ok)throw Error('FIXED_ARTIFACT_'+message);};
const hash=b=>createHash('sha256').update(b).digest('hex');
const key=p=>path.resolve(p).replaceAll('\\','/').toLowerCase();
const same=(a,b)=>typeof a==='string'&&typeof b==='string'&&key(a)===key(b);
const inside=(root,p)=>same(root,p)||key(p).startsWith(key(root)+'/');
const eq=(a,b,code)=>need(isDeepStrictEqual(a,b),code);
const plain=x=>x!==null&&typeof x==='object'&&!Array.isArray(x)&&Object.getPrototypeOf(x)===Object.prototype;
function keys(x,names,code){need(plain(x)&&Object.keys(x).sort().join('|')===[...names].sort().join('|'),code);}
function absolute(p){need(typeof p==='string'&&/^[A-Za-z]:[\\/]/.test(p)&&!/[\x00-\x1f]/.test(p)&&!p.slice(2).includes(':')&&!p.startsWith('\\\\'),'ABSOLUTE_FILE_PATH_REQUIRED');need(p.split(/[\\/]/).slice(1).every(x=>x!=='.'&&x!=='..'&&!/[. ]$/.test(x)),'PATH_ALIAS_REJECTED');return path.resolve(p);}
function relative(p){need(typeof p==='string'&&p.length>0&&!p.includes('\\')&&!p.startsWith('/')&&!p.includes(':')&&p.split('/').every(x=>x&&x!=='.'&&x!=='..'),'RELATIVE_SOURCE_PATH_REQUIRED');return p;}
function pin(p,complete=false){need(plain(p)&&typeof p.path==='string'&&/^[0-9a-f]{64}$/.test(p.sha256??'')&&(!complete||Number.isSafeInteger(p.bytes))&&(p.bytes===undefined||Number.isSafeInteger(p.bytes)&&p.bytes>=0),'PIN_SHAPE_REQUIRED');absolute(p.path);return p;}
function pinEq(a,b,code){pin(a);pin(b);need(same(a.path,b.path)&&a.sha256===b.sha256&&(a.bytes===undefined||b.bytes===undefined||a.bytes===b.bytes),code);}
function projection(p){return {path:path.resolve(p.path),bytes:p.bytes,sha256:p.sha256};}
function unique(rows,code,namespace=false){need(Array.isArray(rows),code);const set=new Set();for(const p of rows){pin(p);const k=(namespace?p.role+'|':'')+key(p.path);need(!set.has(k),code);set.add(k);}return set;}
function profileFile(repo){return path.join(repo,MODULE,'r3-terminal26-artifact-prerequisites.json');}

// Every parent is checked, including junctions. No cached identity survives a pass.
function noRedirect(file,dirs=new Set()){
 absolute(file);let parent=path.dirname(file),chain=[];
 while(!dirs.has(key(parent))){chain.push(parent);const next=path.dirname(parent);if(next===parent)break;parent=next;}
 for(const dir of chain.reverse()){const st=fs.lstatSync(dir);need(st.isDirectory()&&!st.isSymbolicLink()&&same(fs.realpathSync.native(dir),dir),'REPARSE_OR_REDIRECT:'+dir);dirs.add(key(dir));}
 const st=fs.lstatSync(file);need(st.isFile()&&!st.isSymbolicLink()&&same(fs.realpathSync.native(file),file),'REGULAR_UNREDIRECTED_FILE_REQUIRED:'+file);return st;
}
function pass(){
 const checked=new Map(),dirs=new Set(),relations=[];
 function read(expected,label='input',body=false){
  pin(expected);const k=key(expected.path);relations.push({relation:label,path:path.resolve(expected.path),sha256:expected.sha256});
  if(checked.has(k)){const item=checked.get(k);pinEq(item.pin,expected,'CONFLICTING_INPUT_PIN:'+expected.path);if(!body||item.body)return body?item.body:item.pin;checked.delete(k);}
  const before=noRedirect(expected.path,dirs),fd=fs.openSync(expected.path,'r');let contents;
  // Windows Node 22 reports lstat.dev=0 but a volume ID for fstat.dev.
  // Compare the actual file ID, birth, size and times across path/handle; dev within the held handle.
  try{const first=fs.fstatSync(fd);need(first.isFile()&&first.ino===before.ino&&first.birthtimeMs===before.birthtimeMs&&first.size===before.size,'FILE_REPLACED_BEFORE_READ');const h=createHash('sha256');let bytes=0;const blocks=[];const buffer=Buffer.allocUnsafe(1024*1024);for(;;){const count=fs.readSync(fd,buffer,0,buffer.length,null);if(!count)break;h.update(buffer.subarray(0,count));bytes+=count;if(body)blocks.push(Buffer.from(buffer.subarray(0,count)));}const after=fs.fstatSync(fd);need(first.dev===after.dev&&first.ino===after.ino&&first.size===after.size&&first.mtimeMs===after.mtimeMs&&first.ctimeMs===after.ctimeMs,'INPUT_CHANGED_DURING_READ');const digest=h.digest('hex');need(digest===expected.sha256&&(expected.bytes===undefined||bytes===expected.bytes),'INPUT_DRIFT:'+expected.path);const actual={path:path.resolve(expected.path),bytes,sha256:digest};const last=noRedirect(expected.path,dirs);need(last.ino===after.ino&&last.birthtimeMs===after.birthtimeMs&&last.size===after.size&&last.mtimeMs===after.mtimeMs&&last.ctimeMs===after.ctimeMs,'FILE_REPLACED_AFTER_READ');contents=body?Buffer.concat(blocks):undefined;checked.set(k,{pin:actual,body:contents});return body?contents:actual;}finally{fs.closeSync(fd);}
 }
 function current(file,label,body=false){noRedirect(file,dirs);const bytes=fs.readFileSync(file);return read({path:file,bytes:bytes.length,sha256:hash(bytes)},label,body);}
 function json(p,label){return JSON.parse(read(p,label,true).toString('utf8').replace(/^\uFEFF/,''));}
 function finish(powershell){
  for(const dir of dirs){const st=fs.lstatSync(dir);need(st.isDirectory()&&!st.isSymbolicLink()&&same(fs.realpathSync.native(dir),dir),'PARENT_REDIRECTED_DURING_PASS');}
  if(powershell){const paths=[...new Set([...dirs,...[...checked.values()].map(x=>x.pin.path)])];const response=execFileSync(powershell.path,['-NoLogo','-NoProfile','-NonInteractive','-Command',ATTRIBUTE_QUERY],{input:JSON.stringify(paths),encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:1024*1024});need(response.trim()===String(paths.length),'WINDOWS_ATTRIBUTE_COLLECTION_INCOMPLETE');}
 }
 return {read,current,json,relations,checked,finish};
}
function validateProfile(p,repo){
 need(plain(p)&&p.schemaVersion===1&&p.kind===FIXED_ARTIFACT_PROFILE_KIND,'PROFILE_KIND_OR_VERSION');
 if(p.status==='UNBOUND'){keys(p,['schemaVersion','kind','status'],'UNBOUND_EXACT_SCHEMA');return false;}
 keys(p,Object.hasOwn(p,'historicalArchive')?[...PROFILE_KEYS,'historicalArchive']:PROFILE_KEYS,'BOUND_EXACT_SCHEMA');need(p.status==='BOUND'&&p.scope==='FIXED_CPP_ARTIFACT_SOFTWARE_ONLY','BOUND_SCOPE');
 if(Object.hasOwn(p,'historicalArchive')){keys(p.historicalArchive,['path','bytes','sha256'],'ARCHIVE_PROFILE_PIN_KEYS');pin(p.historicalArchive,true);}
 need(same(repo,R)&&same(p.workspaceRoot,W)&&same(p.authorizedBuildRoot,B)&&same(p.sourceRoot,S),'FIXED_ROOTS_REQUIRED');
 need(p.managedInputContract===CONTRACT&&p.sourceCaptureManifestSha256===CAPTURE&&p.toolClosureComplete===false&&p.cacheAcceptance===false&&p.currentPrimaryAccepted===false,'PROFILE_AUTHORITY_SCOPE');
 need(p.nativeBudgetMs===135000&&p.leafBudgetMs===360000&&p.cleanupReserveMs===30000,'EXPLICIT_135_360_30_BUDGETS_REQUIRED');
 for(const k of ['descriptor','expectations','currentPipeOracle','node','powershell']){keys(p[k],['path','bytes','sha256'],'PROFILE_PIN_KEYS');pin(p[k],true);}
 pinEq(p.descriptor,FIXED_DESCRIPTOR,'FIXED_DESCRIPTOR_IDENTITY');pinEq(p.expectations,FIXED_EXPECTATIONS,'FIXED_EXPECTATIONS_IDENTITY');
 need(same(p.currentPipeOracle.path,path.join(repo,MODULE,'r3-terminal26-current-oracle.mjs')),'EXACT_CURRENT_ORACLE_PATH');
 need(same(p.node.path,'G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe')&&p.node.bytes===83344536&&p.node.sha256==='33b1bc1a8aca11fd5a4f2699e51019c63c0af30cf437701d07af69be7706771b','EXACT_NODE');
 need(same(p.powershell.path,'G:/YeManCC-Work/Environment/Toolchains/PowerShell/7.4.6/pwsh.exe')&&p.powershell.bytes===278048&&p.powershell.sha256==='ea5596296a0b6f1261c84c7edee40f27879aefc63f005ed541c440b73e382503','EXACT_POWERSHELL');
 keys(p.qualification,Object.keys(ROLES),'FOUR_QUALIFICATION_ROLES');for(const role of Object.keys(ROLES)){const q=p.qualification[role];keys(q,['receipt','raw'],'QUALIFICATION_KEYS');for(const item of [q.receipt,q.raw]){keys(item,['path','bytes','sha256'],'QUALIFICATION_PIN_KEYS');pin(item,true);}pinEq(q.receipt,role==='positive'?RECEIPTS.positive:RECEIPTS.negative,'EXACT_ROLE_QUALIFICATION_RECEIPT');need(inside(B,q.raw.path),'QUALIFICATION_RAW_SCOPE');}
 return true;
}
export function terminal26ArtifactProfileActive(repo){
 const file=profileFile(path.resolve(repo));try{fs.lstatSync(file);}catch(error){if(error.code==='ENOENT')return false;throw error;}
 const io=pass(),p=JSON.parse(io.current(file,'profile',true).toString('utf8').replace(/^\uFEFF/,''));return validateProfile(p,path.resolve(repo));
}
function table(expect,id){need(/^[0-9a-f]{64}$/.test(id??'')&&Object.hasOwn(expect.tables,id),'TABLE_REFERENCE');const value=expect.tables[id];need(hash(JSON.stringify(value))===id,'TABLE_DIGEST');return value;}
function flags(x){need(x.id===ID&&x.managedInputContract===CONTRACT&&x.runtimeReusePolicy===POLICY&&x.status==='COMPILED_NOT_QUALIFIED'&&x.qualificationScope==='CPP_AND_USED_FIXTURE_SOURCE_ONLY'&&x.toolClosureComplete===false&&x.javascriptHostModuleClosureComplete===false&&x.cppCompilerInputClosureComplete===true&&x.usedFixtureSourceReconstructionComplete===true&&x.sourceReady===false&&x.runtimeQualified===false&&x.currentPrimaryAccepted===false&&x.installedRuntimeAccepted===false,'R13_UNQUALIFIED_CPP_SCOPE');}
function descriptorGraph(d,e,p){
 flags(d);flags(e);need(d.schemaVersion===1&&e.schemaVersion===1&&d.kind==='CURRENT26_FIXED_CAPTURE_OWNED_COMPILED_SOURCE_DESCRIPTOR_R13'&&e.kind==='CURRENT26_FIXED_CAPTURE_OWNED_SOURCE_EXPECTATIONS_R13'&&d.cohortId===e.cohortId&&same(d.sourceRoot,S)&&same(d.consumerRepository,R),'DESCRIPTOR_IDENTITY');
 need(d.sourceExpectation.name===path.basename(p.expectations.path)&&d.sourceExpectation.bytes===p.expectations.bytes&&d.sourceExpectation.sha256===p.expectations.sha256,'DESCRIPTOR_EXPECTATIONS_BINDING');pinEq(d.captureManifest,e.capture,'EXPECTATIONS_CAPTURE');need(d.captureManifest.sha256===CAPTURE,'CAPTURE_DIGEST');
 need(d.roles.length===8&&new Set(d.roles.map(x=>x.role)).size===8,'EIGHT_EXACT_ROLES');need(e.logicalSources.length===739&&e.logicalCore.length===81&&e.managedSources.length===26,'EXACT_LOGICAL_MEMBERSHIP');
 for(const [id,value] of Object.entries(d.inputs)){pin(value,true);keys(value,['path','bytes','sha256'],'POOL_PIN_KEYS');need(hash(JSON.stringify(projection(value)))===id,'POOL_CONTENT_ID');}
 for(const r of d.roles){need(Object.hasOwn(ALL_ROLES,r.role)&&r.mutation===ALL_ROLES[r.role]&&r.status==='COMPILED_NOT_QUALIFIED','ROLE_MUTATION');need(same(r.nativePreparation.path,path.join(B,ROLE_DIR[r.role],'NATIVE-PREPARATION.json')),'FIXED_ROLE_PREPARATION_PATH');eq(e.roleBodies[r.role],{mutation:r.mutation,coreExecution:r.coreExecution,structExecution:r.structExecution,compiledGenerated:r.compiledGenerated,sourceContext:r.sourceContext},'ROLE_TABLE_BINDING');for(const name of ['sourceContext','compiledGenerated','structExecution','actualTools'])table(e,r[name]);table(e,r.coreExecution.base);
  for(const name of ['sourceInputIds','artifactInputIds','compilerInputIds']){need(Array.isArray(r[name])&&new Set(r[name]).size===r[name].length,'UNIQUE_ROLE_INPUT_IDS');for(const id of r[name])need(Object.hasOwn(d.inputs,id),'MISSING_ROLE_INPUT_MEMBER');}need(r.sourceInputIds.length===765&&r.compilerInputIds.length===1513&&r.artifactInputIds.length===(r.role==='positive'?2876:2881)&&r.sourceCount===765&&r.artifactCount===r.artifactInputIds.length,'EXACT_ROLE_INPUT_COUNTS');
 }
 for(const [name,names] of Object.entries({pipe:Object.keys(ROLES),lifecycle:['positive','bypassAdmission','recoveryInheritance','switchReturn','switchOwner'],all:Object.keys(ALL_ROLES)})){for(const field of ['sourceInputIds','artifactInputIds'])eq(d.unions[name][field],[...new Set(d.roles.filter(r=>names.includes(r.role)).flatMap(r=>r[field]))].sort(),'EXACT_'+name+'_UNION');}
}
function setEqual(a,b,code){eq([...a].sort(),[...b].sort(),code);}
function compileGraph(io,d,e,r,n,s,l,managed,layout){
 const roleDir=path.join(B,ROLE_DIR[r.role]),buildDir=path.join(roleDir,'native');
 need(n.schemaVersion===1&&n.status==='NATIVE26_COMPILED_NOT_RUNTIME_ACCEPTED'&&n.role===r.role&&n.mutation===r.mutation&&n.nativeSourceProfile===ID&&n.managedInputContract===CONTRACT&&same(n.sourceRoot,S)&&n.managedAdmissions===26&&n.actualObservedCompilerInputs===1513,'NATIVE_SOURCE_CONTRACT');
 need(n.runtimeReusePolicy===POLICY&&n.native.runtimeReusePolicy===POLICY&&n.generationPreflight.runtimeReusePolicy===POLICY&&n.generationPreflight.coreMembers===60&&n.generationPreflight.core.length===60,'STATIC_PREFLIGHT_60_DISTINCT_FROM_COMPILED_81');
 need(n.native.status==='COMPILED_NOT_EXECUTED'&&n.native.compileOnly===true&&n.native.compilerExitCode===0&&n.native.compilerSignal===null&&n.native.compilerError===null&&n.native.sourceFullTranslationUnit===true&&n.native.runBranchReached===false&&n.native.mutation===r.mutation&&n.native.changed.length===0&&n.nativeExecuted===false&&n.publicRuntimeAccepted===false&&n.sourceApprovalGranted===false&&n.errors.length===0,'COMPILE_PROVENANCE');
 pinEq(n.preparation,d.managedPreparation,'MIXED_MANAGED_PREPARATION');pinEq(n.native.sourceManifest,r.sourceManifest,'NATIVE_SOURCE_MANIFEST');pinEq(n.compilerLedger,r.compilerLedger,'NATIVE_LEDGER');pinEq(n.native.exe,r.nativeExe,'NATIVE_EXE');need(same(n.native.exe.path,path.join(buildDir,'CoordinatorFixture.exe')),'EXE_SCOPE');
 const after=n.batchAfter;need(after.status==='AUTHORITY_STABLE'&&after.fullSourceVerificationCalls===2&&after.source.snapshotIntegrityVerified===true&&after.source.manifestSha256===CAPTURE&&same(after.source.snapshotRoot,S),'CLOSED_CAPTURE_AUTHORITY');eq(after.actualTools,table(e,r.actualTools),'ACTUAL_TOOLS_TABLE');unique(after.actualTools,'ACTUAL_TOOLS_UNIQUE');
 if(n.fixtureToolImplementation){need(n.fixtureToolImplementation.sourceCaptureModified===false&&same(n.fixtureToolImplementation.root,R)&&n.sharedBatchFile===undefined&&n.sharedBatchClosed===undefined,'SINGLE_METADATA');need(n.fixtureToolImplementation.localReviewedToolFiles.length>0,'SINGLE_REVIEWED_TOOLS');for(const p of n.fixtureToolImplementation.localReviewedToolFiles)need(after.actualTools.some(t=>same(t.path,p.path)&&t.sha256===p.sha256),'SINGLE_TOOL_NOT_ACTUAL');}
 else{need(n.sharedBatchClosed===true&&typeof n.sharedBatchFile==='string','SHARED_CLOSED_METADATA');const bp=Object.values(d.inputs).find(x=>same(x.path,n.sharedBatchFile));need(bp,'SHARED_BATCH_NOT_PINNED');const batch=io.json(bp,'shared-batch');eq(batch.batchAfter,after,'SHARED_BATCH_AFTER');const selected=batch.roles.filter(x=>x.role===r.role);need(selected.length===1,'SHARED_ROLE_EXACT');pinEq(selected[0].preparation,r.nativePreparation,'SHARED_ROLE_PREPARATION');pinEq(batch.actualRunner,n.runner,'SHARED_ACTUAL_RUNNER');need(batch.status==='ALL_ORIGINAL_REMAINING_NATIVE_ROLES_COMPILED_NOT_RUNTIME_ACCEPTED'&&batch.roles.length===7&&new Set(batch.roles.map(x=>x.role)).size===7&&batch.roles.every(x=>x.status==='NATIVE26_COMPILED_NOT_RUNTIME_ACCEPTED')&&batch.errors.length===0&&batch.unreachedRoles.length===0&&batch.successfulNativeCompiles===7,'SHARED_SEVEN_COMPILED');need(same(batch.sourceRoot,S)&&batch.nativeSourceProfile===ID&&batch.managedInputContract===CONTRACT&&batch.runtimeReusePolicy===POLICY,'SHARED_SOURCE_CONTRACT');pinEq(batch.layout,d.layout,'SHARED_LAYOUT');pinEq(batch.managedPreparation,d.managedPreparation,'SHARED_MANAGED');}
 for(const p of [n.runner,...(n.originalNativePreparationImplementation?[n.originalNativePreparationImplementation]:[])])need(after.actualTools.some(x=>same(x.path,p.path)&&x.sha256===p.sha256),'NATIVE_ENTRY_NOT_ACTUAL');
 eq(io.json(Object.values(d.inputs).find(x=>same(x.path,path.join(buildDir,'COMPILE-ONLY.json'))),'compile-only'),n.native,'ORIGINAL_COMPILE_RECEIPT');
 need(s.nativeSourceProfile===ID&&s.records.length===739&&s.core.length===81&&l.inputs.length===1513,'ACTUAL_739_81_1513');
 eq(s.records.map(({path,sha256,inputRole})=>({path,sha256,...(inputRole?{inputRole}:{})})),e.logicalSources,'LOGICAL_SOURCE_MEMBERS');eq(s.core.map(({symbol,source,sha256})=>({symbol,source:source??'expanded-native-main',sha256})),e.logicalCore,'LOGICAL_CORE_MEMBERS');eq(s.structCore.map(({symbol,kind,source,sha256})=>({symbol,kind,source,sha256})),e.logicalStruct,'LOGICAL_STRUCT_MEMBERS');eq(s.compilerHeaderBindings,e.logicalCompilerHeaders,'LOGICAL_COMPILER_HEADERS');
 eq(s.compiledGenerated,table(e,r.compiledGenerated),'COMPILED_GENERATED_TABLE');eq(s.structCore,table(e,r.structExecution),'COMPILED_STRUCT_TABLE');
 const execution=structuredClone(table(e,r.coreExecution.base)),seen=new Set();for(const o of r.coreExecution.overrides){need(Number.isInteger(o.index)&&o.index>=0&&o.index<81&&!seen.has(o.index)&&execution[o.index].symbol===o.row.symbol,'CORE_OVERRIDE');seen.add(o.index);execution[o.index]=o.row;}
 eq(s.core.map(({sha256,symbol,source,...row})=>({symbol,...(source?{source}:{}),...row})),execution,'COMPLETE_EXECUTED_CORE_TABLE');
 eq({sourceExpansion:s.sourceExpansion,substitutions:s.substitutions,fragments:s.fragments,settingsMutexIsolation:s.settingsMutexIsolation,nativeGuardProvenance:s.nativeGuardProvenance,runtimeLayout:n.runtimeLayout,productSourceSuccessor:s.productSourceSuccessor,productHeaderSuccessor:s.productHeaderSuccessor,productPolicySuccessor:s.productPolicySuccessor},table(e,r.sourceContext),'COMPLETE_SOURCE_CONTEXT');
 need(s.settingsMutexIsolation.runtimeEntryAdmissionEnforced===true&&s.settingsMutexIsolation.runtimeBoundaryOnly===false&&s.settingsMutexIsolation.wholeOriginalRestored===true&&r.ownedRuntimeProof.entryAdmissionEnforced===true&&r.ownedRuntimeProof.hostToolClosure.complete===false,'ACTUAL_ENTRY_GUARD_AND_LIMITED_JS_SCOPE');
 need(same(n.runtimeRoot,path.join(roleDir,'qualification-runtime'))&&n.runtimeLayout.publicRuntime.policyId===POLICY&&same(n.runtimeLayout.publicRuntime.base,path.join(roleDir,'r')),'OWNED_RUNTIME_LAYOUT');eq(n.native.runtimeLayout,n.runtimeLayout,'NATIVE_RUNTIME_LAYOUT');eq(r.ownedRuntimeProof.layout,n.runtimeLayout,'REBUILT_RUNTIME_LAYOUT');
 const sourceIds=r.sourceInputIds.map(id=>d.inputs[id]),artifactIds=r.artifactInputIds.map(id=>d.inputs[id]),compilerIds=r.compilerInputIds.map(id=>d.inputs[id]);const pool=new Map([...sourceIds,...artifactIds].map(x=>[key(x.path),x]));
 const member=(p,label)=>{const q=pool.get(key(p.path));need(q,'DECLARED_MEMBER_MISSING:'+p.path);pinEq(q,p,label);return q;};
 const cap=io.json(d.captureManifest,'capture-manifest'),capture=new Map(cap.files.map(x=>[x.path,x]));need(cap.status==='PASS'&&same(cap.snapshotRoot,S)&&cap.files.length===11389,'CAPTURE_MANIFEST_SHAPE');
 const sourceKeys=new Set();for(const rec of s.records){relative(rec.path);need(same(rec.original,path.join(S,rec.path))&&inside(buildDir,rec.snapshot),'SOURCE_OR_SNAPSHOT_SCOPE');need(capture.get(rec.path)?.sha256===rec.sha256,'SOURCE_NOT_IN_CAPTURE');member({path:rec.original,sha256:rec.sha256},'CAPTURE_SOURCE_MEMBER');member({path:rec.snapshot,sha256:rec.sha256},'SNAPSHOT_MEMBER');sourceKeys.add(key(rec.original));}
 for(const p of managed.inputBindings.sources){relative(p.relative);need(same(p.path,path.join(S,p.relative))&&inside(B,p.snapshot)&&capture.get(p.relative)?.sha256===p.sha256,'MANAGED_CAPTURE_MEMBER');member(p,'MANAGED_SOURCE');member({path:p.snapshot,sha256:p.sha256},'MANAGED_SNAPSHOT');sourceKeys.add(key(p.path));}setEqual(sourceKeys,sourceIds.map(x=>key(x.path)),'EXACT_SOURCE_INPUT_SET');
 eq(managed.inputBindings.sources.map(({relative,sha256})=>({relative,sha256})),e.managedSources,'EXACT_MANAGED26_MEMBERS');eq(layout.expectedManagedSources,e.managedSources,'LAYOUT_MANAGED26_MEMBERS');
 need(l.status==='ACTUAL_COMPILER_INPUTS_PINNED_NOT_RUNTIME_QUALIFIED'&&l.method==='ORIGINAL_MSVC_SHOWINCLUDES_AND_EXPLICIT_THREE_TU_RECIPE'&&l.unobservedAllowlistClaimedUsed===false,'ACTUAL_THREE_TU_RECIPE');pinEq(l.sourceManifest,r.sourceManifest,'LEDGER_MANIFEST');pinEq(l.buildScript,n.native.buildScript,'LEDGER_BUILD_SCRIPT');unique(l.inputs,'UNIQUE_COMPILER_INPUTS');
 const tus=[path.join(buildDir,'fixture.cpp'),...['native/dsu_server.cpp','native/steam_original_recovery_engine.cpp'].map(p=>s.records.find(x=>x.path===p).snapshot)];eq(l.explicitTranslationUnits.map(key),tus.map(key),'THREE_ACTUAL_TRANSLATION_UNITS');eq(r.threeTranslationUnits.map(key),tus.map(key),'DESCRIPTOR_THREE_TUS');
 const log=io.read(member(l.buildLog,'BUILD_LOG'),'build-log',true).toString('utf8');const observed=[...new Set(log.split(/\r?\n/).filter(x=>/(?:including file:|包含文件:)/i.test(x)).map(x=>x.match(/[A-Z]:\\.*$/i)?.[0]?.trim()).filter(Boolean).map(key))].sort();setEqual(l.observedIncludePaths.map(key),observed,'LEDGER_SHOWINCLUDES');setEqual(s.observedIncludePaths.map(key),observed,'SOURCE_SHOWINCLUDES');setEqual(l.inputs.map(x=>key(x.path)),new Set([...observed,...tus.map(key),key(n.native.buildScript.path)]),'COMPILER_MEMBERS_EXACT');setEqual(compilerIds.map(x=>key(x.path)),l.inputs.map(x=>key(x.path)),'COMPILER_IDS_EXACT');
 for(const p of [...l.inputs,...s.compiledGenerated,...after.actualTools,...managed.inputBindings.generatedInputs,...managed.runtimeArtifacts,...managed.evaluatedCompilerInputs,...managed.inputBindings.toolchainAllowlist])member(p,'ACTUAL_INPUT_MEMBER');
 for(const h of s.compilerHeaderBindings){const rec=s.records.find(x=>x.path===h.source);need(rec?.sha256===h.sha256&&observed.includes(key(rec.snapshot)),'REQUIRED_HEADER_NOT_OBSERVED');}
 member(r.ownedRuntimeProof.wholeCompiledFixture,'WHOLE_COMPILED_FIXTURE');need(s.sourceExpansion.expandedSha256===layout.expandedSourceSha256&&inside(buildDir,s.sourceExpansion.expandedSnapshot),'SOURCE_EXPANSION_IDENTITY');
 // R13 records the original expansion as an additional source read, not as a compiler input.
 const expanded=d.fullGeneratorReadClosure.find(p=>same(p.path,s.sourceExpansion.expandedSnapshot));need(expanded,'EXPANDED_EXTRA_READ_PIN_REQUIRED');pinEq(expanded,{path:s.sourceExpansion.expandedSnapshot,sha256:s.sourceExpansion.expandedSha256,bytes:s.sourceExpansion.expandedBytes},'EXPANDED_SOURCE_PIN');io.read(expanded,'selected-role-expanded-extra-read');for(const p of s.sourceExpansion.inputs)member({path:p.file,sha256:p.sha256,bytes:p.bytes},'EXPANSION_MEMBER');
 for(const p of [...sourceIds,...artifactIds])io.read(p,'compiled-role:'+r.role);return {sourceIds,artifactIds};
}
function qualificationGraph(io,p,d,r,n,managed){
 const q=p.qualification[r.role],receipt=io.json(q.receipt,'qualification-receipt');need(receipt.postInputsStable===true&&receipt.finishedAtUtc,'QUALIFICATION_COMPLETED_STABLE');const rows=receipt.results.filter(x=>x.role===r.role);need(rows.length===1&&rows[0].accepted===true,'ONE_ACCEPTED_ROLE_ROW');const row=rows[0],t=row.terminal,x=t.rawExecution;
 pinEq(row.nativePreparation,r.nativePreparation,'QUALIFIED_NATIVE_PREPARATION');pinEq(row.raw,q.raw,'QUALIFIED_RAW');pinEq(t.program,r.nativeExe,'QUALIFIED_EXE');pinEq(row.jobProof,x.terminal,'QUALIFIED_JOB_PROOF');
 const expectedExit=r.role==='positive'?0:1;need(row.actualMutation===r.mutation&&row.expectedExit===expectedExit&&row.nativeBudgetMs===135000&&row.nativeWallDurationMs>0&&row.nativeWallDurationMs<=135000,'QUALIFIED_ROLE_AND_NATIVE135');need(t.exitCode===expectedExit&&t.signal===null&&t.error===null&&t.timedOut===false&&t.outputTruncated===false&&Number.isSafeInteger(t.pid)&&t.pid>0,'QUALIFIED_NATURAL_TERMINAL');
 need(Array.isArray(t.arguments)&&t.arguments.length===3&&t.arguments[1]==='full-pipe'&&same(t.arguments[0],row.runtime)&&same(t.cwd,row.runtime)&&same(t.arguments[2],managed.fixtureDll)&&inside(n.runtimeLayout.publicRuntime.base,row.runtime)&&/^[0-9a-f]{16}$/.test(path.basename(row.runtime))&&same(row.raw.path,path.join(row.runtime,'fixture-result.json')),'QUALIFIED_OWNED_RUNTIME_AND_MANAGED');
 const plan=io.json(receipt.plan,'qualification-plan'),selected=plan.rows.filter(x=>x.role===r.role);need(selected.length===1&&plan.nativeBudgetMs===135000&&plan.outerJobAllocationMs===165000&&plan.cleanupReserveMs===30000,'QUALIFICATION_PLAN_CLOCK');const pr=selected[0];pinEq(pr.nativePreparation,r.nativePreparation,'QUALIFIED_PLAN_PREPARATION');need(same(pr.runtime,row.runtime),'QUALIFIED_PLAN_RUNTIME');
 const job=io.json(row.jobProof,'qualification-job'),request=io.json(x.request,'qualification-request'),args=io.json(x.arguments,'qualification-arguments');eq(args,t.arguments,'QUALIFIED_ARGUMENT_FILE');eq(job.business,x.actualBusiness,'QUALIFIED_ACTUAL_BUSINESS');pinEq(job.request,x.request,'QUALIFIED_JOB_REQUEST');pinEq(job.argumentsPin,x.arguments,'QUALIFIED_JOB_ARGUMENTS');pinEq(x.helperSource,x.launcher.helper,'QUALIFIED_HELPER');pinEq(x.launcher.program,p.powershell,'QUALIFIED_POWERSHELL');
 const helperExit=expectedExit===0?0:2;
 need(x.collectionComplete===true&&x.cancelled===false&&x.ownerLost===false&&x.residualDescendants===false&&x.completion.kind==='NATURAL'&&x.completion.forcedToolFinalization===false&&x.completion.productResourcesWaived===false&&x.helperExitCode===helperExit&&x.helperSignal===null,'QUALIFIED_OWNED_COLLECTION');need(job.collectionComplete===true&&job.declaredPinsUnchanged===true&&job.timedOut===false&&job.cancelled===false&&job.ownerLost===false&&job.residualDescendants===false&&job.businessExitCode===expectedExit&&job.outputLimitExceeded===false&&job.helperExitCode===helperExit&&job.status===(expectedExit===0?'EXITED':'FAILED')&&job.error===null&&job.signal===null,'QUALIFIED_JOB_COMPLETE');
 need(x.job.activeProcessesFinal===0&&x.job.zeroObservedBeforeDispose===true&&x.job.disposed===true&&x.job.terminateRequested===false&&x.job.cleanupError===null&&job.job.activeProcessesFinal===0&&job.job.zeroObservedBeforeDispose===true,'QUALIFIED_JOB_NATURAL_ZERO');
 const budget=x.budget,business=x.actualBusiness;eq(request.budget,budget,'QUALIFIED_REQUEST_BUDGET');need(budget.originalBudgetMs===165000&&budget.allocatedBudgetMs===165000&&budget.cleanupReserveMs===30000&&budget.deadlineEpochMs===budget.allocatedAtEpochMs+165000&&t.originalBudgetMs===165000,'QUALIFIED_OUTER165');need(business.pid===t.pid&&business.atomicJobAdmission===true&&business.originalDeadlineEpochMs===budget.deadlineEpochMs&&business.runCutoffEpochMs===budget.deadlineEpochMs-30000&&business.cleanupReserveMs===30000&&business.effectiveRunRemainingAtAdmissionMs>0&&business.effectiveRunRemainingAtAdmissionMs<=135000&&job.finishedEpochMs<=business.runCutoffEpochMs,'QUALIFIED_NATIVE135_DEADLINE');eq(business.arguments,t.arguments,'QUALIFIED_NATIVE_ARGUMENTS');pinEq(business.program,t.program,'QUALIFIED_ACTUAL_IMAGE');need(same(business.cwd,t.cwd),'QUALIFIED_ACTUAL_CWD');
 need(/^\d+$/.test(job.businessExitFileTimeUtc)&&/^\d+$/.test(business.startFileTimeUtc),'QUALIFIED_HELD_HANDLE_CLOCK');const held100ns=BigInt(job.businessExitFileTimeUtc)-BigInt(business.startFileTimeUtc);need(held100ns>0n&&held100ns<=1350000000n,'QUALIFIED_HELD_HANDLE_135_LIMIT');const heldMs=Number(held100ns)/10000;
 const before=x.sourceBefore,after=x.sourceAfter;unique(before,'QUALIFIED_BEFORE_UNIQUE_BY_ROLE',true);need(before.every(v=>v.role==='sources'||v.role==='tools'),'QUALIFIED_INPUT_NAMESPACE');eq(before.map(projection),after.map(projection),'QUALIFIED_SOURCE_BEFORE_AFTER');eq(job.declaredInputsBefore,before,'QUALIFIED_JOB_SOURCE_BEFORE');eq(job.declaredInputsAfter,after,'QUALIFIED_JOB_SOURCE_AFTER');
 const sources=before.filter(v=>v.role==='sources'),tools=before.filter(v=>v.role==='tools');eq(sources.map(projection),pr.sourceInputs.map(projection),'QUALIFIED_PLAN_SOURCES');eq(tools.map(projection),pr.toolArtifacts.map(projection),'QUALIFIED_PLAN_TOOLS');eq(request.sources.map(projection),sources.map(projection),'QUALIFIED_REQUEST_SOURCES');eq(request.tools.map(projection),tools.map(projection),'QUALIFIED_REQUEST_TOOLS');need(same(request.cwd,t.cwd),'QUALIFIED_REQUEST_CWD');pinEq(request.arguments,x.arguments,'QUALIFIED_REQUEST_ARGUMENT_PIN');
 const declared=new Map(before.map(v=>[key(v.path),v]));for(const pin of [r.nativePreparation,r.sourceManifest,r.compilerLedger,r.nativeExe,d.managedManifest,...n.batchAfter.actualTools]){const found=declared.get(key(pin.path));need(found,'QUALIFIED_NATIVE_SOURCE_CLOSURE_MISSING');pinEq(found,pin,'QUALIFIED_NATIVE_SOURCE_CLOSURE_DRIFT');}
 // All historical execution input rows are checked as bytes; the 67 MB semantic raw is only hashed.
 for(const pin of [...before,q.raw,row.jobProof,x.request,x.arguments,x.helperSource,x.launcher.program,x.launcher.helper,...Object.values(x.logs),...Object.values(job.logs),t.stdout,t.stderr])io.read(pin,'historical-qualification:'+r.role);
 return {receiptPin:q.receipt,rawPin:q.raw,receiptStatus:receipt.status,row,planPin:receipt.plan,jobProof:row.jobProof,heldHandleDuration100ns:held100ns.toString(),heldHandleDurationMs:heldMs,sourcePins:sources,artifactPins:tools,semanticRawParsed:false,historicalReferenceOnly:true};
}
// Current oracle imports and fixed data, separate from the unproven historical JS compiler host.
function consumerSources(io,repo,oracle){
 const files=new Set(),pending=[oracle.path,SELF,path.join(repo,MODULE,'r3-terminal26-artifact-prerequisites.json'),path.join(repo,MODULE,'r3-current105-expectations.json'),path.join(repo,MODULE,'r3-terminal26-runtime-expectations.json')];
 while(pending.length){const file=pending.pop();if(files.has(key(file)))continue;need(inside(repo,file),'CONSUMER_IMPORT_ESCAPES_REPOSITORY');files.add(key(file));const bytes=io.current(file,'current-consumer',true);if(!file.endsWith('.mjs'))continue;const text=bytes.toString('utf8');for(const match of text.matchAll(/(?:^|[;\n])\s*(?:import\s+(?:[^'";]*?\s+from\s*)?|export\s+[^'";]*?\s+from\s*)['"]([^'"]+)['"]/g)){const spec=match[1];if(spec.startsWith('node:'))continue;need(spec.startsWith('.'),'UNKNOWN_CONSUMER_PACKAGE_IMPORT');pending.push(path.resolve(path.dirname(file),spec));}for(const match of text.matchAll(/new URL\(\s*['"](\.\.?\/[^'"]+)['"]\s*,\s*import\.meta\.url\s*\)/g))pending.push(path.resolve(path.dirname(file),match[1]));}
 const reviewed=io.json({path:path.join(repo,MODULE,'r3-reviewed-contract.json'),sha256:io.checked.get(key(path.join(repo,MODULE,'r3-reviewed-contract.json'))).pin.sha256},'reviewed-contract');for(const row of Object.values(reviewed.reviewedEvidence)){const file=path.join(repo,MODULE,relative(row.file));io.read({path:file,sha256:row.sha256},'reviewed-oracle-data');files.add(key(file));}
 return [...files].map(k=>path.relative(repo,io.checked.get(k).pin.path).replaceAll('\\','/')).sort();
}
function bindHistoricalRepositoryInputs(io,archivePin,d,role,repo){
 if(archivePin===null)return {io,resolve:p=>p};
 pin(archivePin,true);need(same(archivePin.path,path.join(ARCHIVE_ROOT,'manifest.json')),'EXACT_ARCHIVE_MANIFEST_PATH');
 const manifest=io.json(archivePin,'explicit-archive-manifest');
 keys(manifest,['schemaVersion','kind','descriptor','archiveRoot','rows'],'ARCHIVE_MANIFEST_KEYS');
 need(manifest.schemaVersion===1&&manifest.kind==='FIXED_CPP_ARCHIVED_REPOSITORY_INPUTS_V1'&&same(manifest.archiveRoot,ARCHIVE_ROOT)&&Array.isArray(manifest.rows)&&manifest.rows.length>0,'ARCHIVE_MANIFEST_SCOPE');
 pinEq(manifest.descriptor,FIXED_DESCRIPTOR,'ARCHIVE_DESCRIPTOR_IDENTITY');
 const descriptorByRole=new Map(Object.keys(ROLES).map(name=>[name,new Map(d.roles.find(r=>r.role===name).artifactInputIds.map(id=>d.inputs[id]).filter(p=>inside(repo,p.path)).map(p=>[key(p.path),p]))]));
 const mapped=new Map(),logicalKeys=new Set(),physicalKeys=new Set();
 for(const {logical,physical,roles} of manifest.rows){
  pin(logical,true);pin(physical,true);
  const logicalKey=key(logical.path),physicalKey=key(physical.path);
  need(inside(repo,logical.path)&&same(physical.path,path.join(ARCHIVE_ROOT,logical.sha256+'.bytes'))&&!logicalKeys.has(logicalKey)&&!physicalKeys.has(physicalKey),'ARCHIVE_EXACT_UNIQUE_PATHS');
  need(physical.sha256===logical.sha256&&physical.bytes===logical.bytes,'ARCHIVE_BYTES_MUST_MATCH_LOGICAL_PIN');
  logicalKeys.add(logicalKey);physicalKeys.add(physicalKey);
  const exactRoles=[...descriptorByRole].filter(([,members])=>members.get(logicalKey)?.sha256===logical.sha256).map(([name,members])=>{pinEq(members.get(logicalKey),logical,'ARCHIVE_ROLE_INPUT_IDENTITY');return name;});
  need(Array.isArray(roles)&&exactRoles.length>0,'ARCHIVE_ROLE_REQUIRED');eq([...roles].sort(),exactRoles.sort(),'ARCHIVE_EXACT_ROLE_MEMBERSHIP');
  if(roles.includes(role))mapped.set(logicalKey,{logical,physical});
 }
 const resolve=p=>{
  const row=mapped.get(key(p.path));
  if(!row||p.sha256!==row.logical.sha256)return p;
  pinEq(row.logical,p,'ARCHIVE_REQUESTED_LOGICAL_PIN');return row.physical;
 };
 const historicalRead=(p,label,body)=>{
  const physical=resolve(p);
  if(physical!==p)io.relations.push({relation:'historical-archive:'+label,logicalPath:p.path,path:physical.path,sha256:physical.sha256});
  return io.read(physical,label,body);
 };
 return {resolve,io:{read:historicalRead,json:(p,label)=>JSON.parse(historicalRead(p,label,true).toString('utf8').replace(/^\uFEFF/,''))}};
}
// A size mismatch can reject early, but equal size never substitutes for the full role hash.
function checkLiveArtifactSizes(d,r,repo,resolve){
 const dirs=new Set();
 for(const id of r.artifactInputIds){
  const p=d.inputs[id];
  if(inside(repo,p.path))need(noRedirect(resolve(p).path,dirs).size===p.bytes,'INPUT_DRIFT:'+p.path);
 }
}
export function openTerminal26ArtifactContext(role,repo,{historicalArchive=null,useConfiguredArchive=false}={}){
 need(Object.hasOwn(ROLES,role),'FOUR_PIPE_ROLE_REQUIRED');repo=path.resolve(repo);const io=pass(),file=profileFile(repo),profile=JSON.parse(io.current(file,'profile',true).toString('utf8').replace(/^\uFEFF/,''));need(validateProfile(profile,repo),'BOUND_PROFILE_REQUIRED');
 const d=io.json(profile.descriptor,'descriptor'),r=d.roles.find(x=>x.role===role);need(r,'EXACT_SELECTED_ROLE_REQUIRED');
 need(!useConfiguredArchive||historicalArchive===null,'ONLY_ONE_EXPLICIT_ARCHIVE_AUTHORITY');
 const selectedArchive=useConfiguredArchive?(profile.historicalArchive??null):historicalArchive;
 const historical=bindHistoricalRepositoryInputs(io,selectedArchive,d,role,repo);
 checkLiveArtifactSizes(d,r,repo,historical.resolve);
 const e=io.json(profile.expectations,'expectations');descriptorGraph(d,e,profile);io.read(profile.node,'node');io.read(profile.powershell,'powershell');io.read(profile.currentPipeOracle,'oracle');
 const n=io.json(r.nativePreparation,'native-preparation'),s=io.json(r.sourceManifest,'source-manifest'),l=io.json(r.compilerLedger,'compiler-ledger'),preparation=io.json(d.managedPreparation,'managed-preparation'),layout=io.json(d.layout,'layout'),managed=io.json(d.managedManifest,'managed-manifest');
 for(const p of [d.captureManifest,d.nodeBinding,e.sourceProfile])io.read(p,'source-contract');need(same(layout.sourceRoot,S)&&same(layout.workspaceRoot,W)&&same(layout.authorizedBuildRoot,B)&&layout.manifestSha256===CAPTURE&&layout.managedInputContract===CONTRACT,'LAYOUT_FIXED_ROOTS');need(preparation.status==='MANAGED26_COMPILED_NOT_RUNTIME_ACCEPTED'&&preparation.managedInputContract===CONTRACT&&preparation.managedMemberCount===26&&same(preparation.sourceRoot,S)&&same(preparation.managed.manifestPath,d.managedManifest.path)&&preparation.managed.manifestSha256===d.managedManifest.sha256,'MANAGED_PREPARATION_IDENTITY');pinEq(preparation.layout,d.layout,'MANAGED_LAYOUT');
 need(managed.status==='BUILT'&&managed.managedInputContract===CONTRACT&&managed.managedInputCount===26&&managed.terminalCleanupCompiled===true&&managed.sourceStable===true&&managed.toolchainStable===true&&managed.semanticMutation===null&&managed.runtimeArtifacts.length===3,'EXACT_MANAGED26_BUILD');need(same(n.native.managedProof.manifest,d.managedManifest.path)&&n.native.managedProof.manifestSha256===d.managedManifest.sha256&&same(n.native.managedProof.dll.path,managed.fixtureDll),'NATIVE_MANAGED_IDENTITY');pinEq(n.native.managedProof.dll,managed.runtimeArtifacts.find(x=>same(x.path,managed.fixtureDll)),'MANAGED_DLL');
 const compiled=compileGraph(historical.io,d,e,r,n,s,l,managed,layout),qualification=qualificationGraph(historical.io,profile,d,r,n,managed),sources=consumerSources(io,repo,profile.currentPipeOracle);
 const inputPins=[...io.checked.values()].map(x=>x.pin).sort((a,b)=>key(a.path).localeCompare(key(b.path)));const inputMap=new Map(inputPins.map(x=>[key(x.path),x]));
 const physical=rows=>{const m=new Map();for(const p of rows)m.set(key(p.path),inputMap.get(key(p.path)));return [...m.values()].sort((a,b)=>key(a.path).localeCompare(key(b.path)));};
 const sourcePins=physical([...compiled.sourceIds,...qualification.sourcePins].map(historical.resolve).concat(sources.map(relative=>inputMap.get(key(path.join(repo,relative))))));
 const sourceKeys=new Set(sourcePins.map(p=>key(p.path)));const artifactPins=physical([...compiled.artifactIds,...qualification.artifactPins].map(historical.resolve).concat(inputPins.filter(p=>!sourceKeys.has(key(p.path)))));
 const metadata={profile,native:n,nativePreparation:n,nativePreparationPin:r.nativePreparation,preparation,layout,managed,source:s,compilerLedger:l,roleRow:r,qualification,sourceCaptureManifest:d.captureManifest,descriptorPin:profile.descriptor,expectationsPin:profile.expectations,nativeBudgetMs:135000,leafBudgetMs:360000,cleanupReserveMs:30000,fullToolClosure:false,cacheAcceptance:false,currentPrimaryApproved:false,scope:'FIXED_CPP_ARTIFACT_SOFTWARE_ONLY',qualificationScope:'CPP_AND_USED_FIXTURE_SOURCE_ONLY',lifecycleQualified:false};
 if(selectedArchive)metadata.historicalArchive=selectedArchive;
 io.finish(profile.powershell);
 const view={inputPins,sourcePins,artifactPins,sources,artifacts:inputPins.map(x=>x.path),metadata,nestedInputRelationships:io.relations,fullToolClosure:false,cacheAcceptance:false,currentPrimaryApproved:false};
 const context=Object.freeze({kind:FIXED_ARTIFACT_PROFILE_KIND,role,scope:'FIXED_CPP_ARTIFACT_SOFTWARE_ONLY',fullToolClosure:false,cacheAcceptance:false,currentPrimaryApproved:false});live.set(context,view);return context;
}
function state(context){const value=live.get(context);need(value,'LIVE_BRANDED_CONTEXT_REQUIRED');return value;}
export function terminal26ArtifactInputs(context){return structuredClone(state(context));}
export function recheckTerminal26ArtifactContext(context){const view=state(context),io=pass();for(const p of view.inputPins)io.read(p,'recheck');io.finish(view.metadata.profile.powershell);return {status:'FIXED_SELECTED_ROLE_INPUTS_REHASHED',role:context.role,files:io.checked.size,fullToolClosure:false,cacheAcceptance:false,currentPrimaryApproved:false};}
