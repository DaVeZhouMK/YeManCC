import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const schemaFile=new URL('./consumer-schema.json',import.meta.url);
const schema=()=>JSON.parse(fs.readFileSync(schemaFile,'utf8'));
const need=(value,code)=>assert.ok(value,code);
const sha=value=>createHash('sha256').update(value).digest('hex');
const hash=file=>sha(fs.readFileSync(file));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const norm=file=>path.resolve(file).toLowerCase();
const decode=file=>fs.readFileSync(file,'utf8').replace(/^\uFEFF/,'');
const same=(left,right)=>JSON.stringify(left)===JSON.stringify(right);
function below(file,root,label){need(typeof file==='string'&&path.isAbsolute(file),label);const rel=path.relative(root,file);need(rel&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep),label);}
function exactKeys(value,keys,label){need(value&&Object.keys(value).length===keys.length&&keys.every(key=>Object.hasOwn(value,key)),label);}
function exactRows(rows,names,key,label){need(Array.isArray(rows)&&rows.length===names.length&&new Set(rows.map(row=>row[key])).size===names.length&&names.every(name=>rows.some(row=>row[key]===name)),label);}
export function cleanupFunction(text,name){
 const code=text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\])*'/g,s=>s.replace(/[^\n]/g,' '));
 const matches=[...code.matchAll(new RegExp('^static\\s+bool\\s+'+name+'\\s*\\([^;]*?\\)\\s*(?:noexcept\\s*)?\\{','gm'))];
 need(matches.length===1,'PIPE_COMPOSITION_COMPLETE_FUNCTION:'+name);const start=matches[0].index;let end=code.indexOf('{',start)+1,depth=1;
 while(depth&&end<code.length){depth+=(code[end]==='{')-(code[end]==='}');end++;}need(depth===0,'PIPE_COMPOSITION_UNCLOSED_FUNCTION');
 const fragment=text.slice(start,end);return {text:fragment,sha256:sha(fragment),line:code.slice(0,start).split('\n').length};
}
export function compositionContract(repo){
 const historicalContract=schema().ownedCleanupComposition;
 const contract=historicalContract.currentDelivery?{...historicalContract,delivery:historicalContract.currentDelivery,deliveredSources:historicalContract.currentDeliveredSources,frontDoorIntegration:historicalContract.currentFrontDoorIntegration,functions:historicalContract.currentFunctions}:historicalContract,delivery=contract.delivery,tc=contract.toolchain;
 need(hash(historicalContract.delivery.path)===historicalContract.delivery.sha256,'PIPE_COMPOSITION_HISTORICAL_DELIVERY_PIN');
 need(path.isAbsolute(delivery.path)&&hash(delivery.path)===delivery.sha256,'PIPE_COMPOSITION_DELIVERY_PIN');
 const declared=read(delivery.path);
 if(historicalContract.currentDelivery){need(same(declared.historicalDelivery,historicalContract.delivery)&&declared.newPrimaryHandleAcceptance===false&&declared.wholeMainAcceptance===false,'PIPE_COMPOSITION_CURRENT_DELIVERY_SCOPE');
  for(const row of [declared.basisDelivery,declared.basisResult])need(row&&hash(row.path)===row.sha256,'PIPE_COMPOSITION_CURRENT_BASIS_PIN');
  const basis=read(declared.basisDelivery.path);need(basis.composition.sha256===declared.basisResult.sha256&&norm(basis.composition.receipt)===norm(declared.basisResult.path),'PIPE_COMPOSITION_CURRENT_BASIS_RESULT');
 }
 need(same(declared.frontDoorIntegration,contract.frontDoorIntegration),'PIPE_COMPOSITION_FRONT_DOOR_CONTRACT');
 need(path.isAbsolute(tc.path)&&hash(path.join(tc.path,'toolchain-manifest.json'))===tc.manifestSha256&&hash(path.join(tc.path,'build-environment.json'))===tc.captureSha256,'PIPE_COMPOSITION_CAPTURED_TOOLCHAIN_PIN');
 const manifest=read(path.join(tc.path,'toolchain-manifest.json')),capture=read(path.join(tc.path,'build-environment.json'));
 need(manifest.unstableAtCopy?.length===0&&manifest.files?.length>0&&manifest.roots?.length>0,'PIPE_COMPOSITION_CAPTURED_AUTHORITY');
 // A source snapshot may relocate the same reviewed fixture. Preserve the
 // original absolute-path/hash authority and verify both origin and copied
 // bytes; a matching suffix alone can never approve changed content.
 const origins=new Set();
 for(const row of contract.deliveredSources){
  const suffix=path.sep+path.normalize(row.path),matches=declared.modifiedFiles.filter(old=>norm(old.path).endsWith(suffix.toLowerCase())&&old.sha256===row.sha256);
  need(matches.length===1&&hash(matches[0].path)===row.sha256&&hash(path.join(repo,row.path))===row.sha256,'PIPE_COMPOSITION_DELIVERED_FIXTURE_CHANGED');
  origins.add(norm(matches[0].path.slice(0,-suffix.length)));
 }
 need(origins.size===1,'PIPE_COMPOSITION_SOURCE_ORIGIN_NOT_UNIQUE');
 return {contract,manifest,capture,historicalContract};
}
export function compositionArtifactRows(repo){
 const {contract,historicalContract}=compositionContract(repo);
 return [{...historicalContract.delivery,origin:'composition.historical-delivery'}, {...contract.delivery,origin:'composition.delivery'},
  {path:path.join(contract.toolchain.path,'toolchain-manifest.json'),sha256:contract.toolchain.manifestSha256,origin:'composition.toolchain-manifest'},
  {path:path.join(contract.toolchain.path,'build-environment.json'),sha256:contract.toolchain.captureSha256,origin:'composition.toolchain-capture'}];
}
export function compositionArtifactInputs(repo){return compositionArtifactRows(repo).map(row=>row.path);}
export function verifyNestedToolFiles(rows){
 need(Array.isArray(rows)&&rows.length>0,'PIPE_COMPOSITION_NESTED_FILES_REQUIRED');
 const seen=new Map(),observed=[];
 for(const row of rows){
  need(typeof row.frozen==='string'&&path.win32.isAbsolute(row.frozen)&&/^[a-f0-9]{64}$/.test(row.sha256),'PIPE_COMPOSITION_NESTED_FILE_DECLARATION');
  const key=path.win32.resolve(row.frozen).toLowerCase(),prior=seen.get(key);
  need(!prior||prior===row.sha256,'PIPE_COMPOSITION_NESTED_PATH_HASH_CONFLICT');
  need(!prior,'PIPE_COMPOSITION_NESTED_DUPLICATE_PATH');seen.set(key,row.sha256);
  let bytes;try{bytes=fs.readFileSync(row.frozen);}catch(error){throw Error('PIPE_COMPOSITION_NESTED_FILE_MISSING_OR_UNREADABLE:'+row.frozen+':'+error.code);}
  need(sha(bytes)===row.sha256,'PIPE_COMPOSITION_NESTED_FILE_HASH:'+row.frozen);
  need(!Number.isInteger(row.sizeBytes)||bytes.length===row.sizeBytes,'PIPE_COMPOSITION_NESTED_FILE_SIZE:'+row.frozen);
  observed.push({path:row.frozen,sha256:row.sha256,bytes:bytes.length});
 }
 return observed;
}
export function sealCompositionNestedClosure({repo,owned,sourceHashes,phase}){
 const {contract,manifest}=compositionContract(repo),policy=contract.nestedToolClosure;
 need(policy?.kind==='complete-frozen-toolchain-manifest'&&policy.fullFileHashBeforeAndAfter===true&&policy.sourceAndReceiptBound===true&&policy.coverageReduced===false&&same(policy.phases,['before','after'])&&manifest.files.length===policy.expectedManifestFiles,'PIPE_COMPOSITION_NESTED_POLICY');
 need(policy.phases.includes(phase)&&['owned-cleanup-composition','primary-thread'].includes(owned.id)&&hash(owned.jobIdentity.path)===owned.jobIdentity.sha256&&hash(owned.owner.path)===owned.owner.sha256,'PIPE_COMPOSITION_NESTED_OWNER_PHASE');
 const file=path.join(owned.directory,'nested-tools.'+phase+'.json'),startedAt=new Date().toISOString();
 try{
  const files=verifyNestedToolFiles(manifest.files),snapshot={schemaVersion:1,status:'PASS',kind:policy.kind,phase,startedAt,finishedAt:new Date().toISOString(),
   manifest:{path:path.join(contract.toolchain.path,'toolchain-manifest.json'),sha256:contract.toolchain.manifestSha256},
   capture:{path:path.join(contract.toolchain.path,'build-environment.json'),sha256:contract.toolchain.captureSha256},
   sourceClosureSha256:sha(JSON.stringify(sourceHashes)),owner:owned.owner,jobIdentity:owned.jobIdentity,
   fileCount:files.length,totalBytes:files.reduce((sum,row)=>sum+row.bytes,0),filesSha256:sha(JSON.stringify(files)),files};
  fs.writeFileSync(file,JSON.stringify(snapshot,null,2)+'\n',{flag:'wx'});return {path:file,sha256:hash(file)};
 }catch(error){fs.writeFileSync(file,JSON.stringify({schemaVersion:1,status:'FAIL',phase,startedAt,error:error.message,sourceClosureSha256:sha(JSON.stringify(sourceHashes)),manifestSha256:contract.toolchain.manifestSha256},null,2)+'\n',{flag:'wx'});throw error;}
}
export function validateCompositionNestedClosure(context,{repo,owned,sourceHashes,manifest,contract}){
 const policy=contract.nestedToolClosure;need(policy?.coverageReduced===false&&policy.fullFileHashBeforeAndAfter===true&&policy.sourceAndReceiptBound===true&&same(policy.phases,['before','after'])&&policy.expectedManifestFiles===manifest.files.length,'PIPE_COMPOSITION_NESTED_POLICY');
 exactKeys(context.nestedToolClosure,policy.phases,'PIPE_COMPOSITION_NESTED_PHASES');
 let before;
 for(const phase of policy.phases){
  const pin=context.nestedToolClosure[phase];need(pin&&norm(pin.path)===norm(path.join(owned.directory,'nested-tools.'+phase+'.json'))&&hash(pin.path)===pin.sha256,'PIPE_COMPOSITION_NESTED_SNAPSHOT_PIN');
  const snapshot=read(pin.path);need(snapshot.schemaVersion===1&&snapshot.status==='PASS'&&snapshot.kind===policy.kind&&snapshot.phase===phase&&snapshot.sourceClosureSha256===sha(JSON.stringify(sourceHashes))&&same(snapshot.owner,owned.owner)&&same(snapshot.jobIdentity,owned.jobIdentity),'PIPE_COMPOSITION_NESTED_SOURCE_OWNER_BINDING');
  need(same(snapshot.manifest,{path:path.join(contract.toolchain.path,'toolchain-manifest.json'),sha256:contract.toolchain.manifestSha256})&&same(snapshot.capture,{path:path.join(contract.toolchain.path,'build-environment.json'),sha256:contract.toolchain.captureSha256}),'PIPE_COMPOSITION_NESTED_AUTHORITY_BINDING');
  need(snapshot.fileCount===manifest.files.length&&snapshot.files?.length===manifest.files.length&&snapshot.filesSha256===sha(JSON.stringify(snapshot.files)),'PIPE_COMPOSITION_NESTED_COMPLETE_FILE_COUNT');
  const seen=new Set();for(let index=0;index<manifest.files.length;index++){
   const row=snapshot.files[index],expected=manifest.files[index],key=path.win32.resolve(row.path).toLowerCase();
   need(!seen.has(key)&&key===path.win32.resolve(expected.frozen).toLowerCase()&&row.sha256===expected.sha256&&row.bytes===expected.sizeBytes,'PIPE_COMPOSITION_NESTED_MANIFEST_ROW');seen.add(key);
  }
  need(snapshot.totalBytes===snapshot.files.reduce((sum,row)=>sum+row.bytes,0),'PIPE_COMPOSITION_NESTED_TOTAL_BYTES');
  need(Number.isFinite(Date.parse(snapshot.startedAt))&&Date.parse(snapshot.finishedAt)>=Date.parse(snapshot.startedAt),'PIPE_COMPOSITION_NESTED_TIME_ORDER');
  if(phase==='before')before=snapshot;else need(snapshot.filesSha256===before.filesSha256&&Date.parse(snapshot.startedAt)>=Date.parse(before.finishedAt),'PIPE_COMPOSITION_NESTED_BEFORE_AFTER_DRIFT');
 }
 need(sha(JSON.stringify(verifyNestedToolFiles(manifest.files)))===before.filesSha256,'PIPE_COMPOSITION_NESTED_CURRENT_DRIFT');
 return {fileCount:before.fileCount,totalBytes:before.totalBytes,filesSha256:before.filesSha256,complete:true};
}
export function prepareCompositionToolchain({repo,owned}){
 const {contract,capture}=compositionContract(repo),view=path.join(owned.directory,'tc');fs.mkdirSync(view);
 fs.copyFileSync(path.join(contract.toolchain.path,'toolchain-manifest.json'),path.join(view,'toolchain-manifest.json'));
 const derived=structuredClone(capture);derived.environment.TEMP=owned.temp;derived.environment.TMP=owned.temp;
 fs.writeFileSync(path.join(view,'build-environment.json'),JSON.stringify(derived,null,2)+'\n',{flag:'wx'});
 return {path:view,original:contract.toolchain,manifestSha256:hash(path.join(view,'toolchain-manifest.json')),captureSha256:hash(path.join(view,'build-environment.json')),ownedTemp:owned.temp};
}
const roles={fixture:'fixture.cpp',command:'command.json',dependencies:'dependencies.json',executable:'fixture.exe',runtime:'result.json',buildLog:'build.log',runLog:'run.log',imports:'imports.txt'};
export function composeRegistrationContext({repo,sourceHashes,owned,job,identity,closure,toolchainView,nestedToolClosure}){
 const {contract}=compositionContract(repo),file=path.join(owned.buildOutput,'RESULT.json'),receipt=read(file);
 const artifacts=[];
 for(const row of receipt.runs??[]){const dir=path.join(owned.buildOutput,row.profile+'-'+row.variant);artifacts.push({profile:row.profile,variant:row.variant,files:Object.fromEntries(Object.entries(roles).map(([role,name])=>{const file=path.join(dir,name);return [role,{path:file,sha256:hash(file)}];}))});}
 const context={schemaVersion:1,kind:'controller-signal-owned-cleanup-composition',toolchainView,nestedToolClosure,approvedIdentityManifest:identity,runtimeSourceClosure:{path:closure,sha256:hash(closure)},sourceHashes,ownedWorkspace:owned,
  actualReceipt:{path:file,sha256:hash(file)},entry:{executable:job.executable,args:job.args,exitCode:job.exitCode,signal:job.signal,error:job.error,timedOut:job.timedOut,primaryToolsBefore:job.primaryToolsBefore,primaryToolsAfter:job.primaryToolsAfter,startedAt:job.startedAt,finishedAt:job.finishedAt},
  artifacts,delivery:contract.delivery,parentLifecycleAcceptance:'INCOMPLETE'};
 fs.writeFileSync(path.join(owned.buildOutput,'registration-context.json'),JSON.stringify(context,null,2)+'\n',{flag:'wx'});
 return context;
}
export function validateCleanupRuntime(value,variant,contract){
 const [checks,failures,salvage]=contract.expectedCounts[variant]??[];need(checks!==undefined,'PIPE_COMPOSITION_VARIANT');const positive=variant==='current';
 need(value?.pass===positive&&value.checks===checks&&Array.isArray(value.failures)&&value.failures.length===failures,'PIPE_COMPOSITION_RUNTIME_OUTCOME');
 need(value.productionOrDevicesExecuted===false&&value.physicalAndEndpointFacts==='explicit memory ports; not real Host, SDK or physical proof','PIPE_COMPOSITION_MEMORY_BOUNDARY');
 need(value.oracleLiveBeforeExit===0&&Number.isInteger(value.handlesBefore)&&value.handlesBefore>0&&value.handlesAfter===value.handlesBefore&&value.negativeOnlyRawSalvage===salvage,'PIPE_COMPOSITION_PREEXIT_PRIVATE_ZERO');
 if(!positive)need(value.failures.includes(contract.frontDoorIntegration.strictNegativeLabels[variant]),'PIPE_COMPOSITION_SPECIFIED_NEGATIVE_LABEL');
 exactRows(value.cases,contract.caseNames,'case','PIPE_COMPOSITION_SCENARIOS');const rows=Object.fromEntries(value.cases.map(row=>[row.case,row]));
 const settled=value=>need(value?.available===true&&value.stopped===true&&['active','cached','duplicates','events','failedCloseDebt','originals','pending','retained'].every(name=>value[name]===0),'PIPE_COMPOSITION_SETTLED_RESOURCE_ORACLE');
 for(const [index,name] of ['raw-slot-before-first-runtime','raw-slot-with-existing-empty-runtime'].entries())need(rows[name].negativeOnlySalvage===(variant==='unchecked-raw-slot-close'?index+1:0),'PIPE_COMPOSITION_RAW_SLOT_LOST_OWNER_WITNESS');
 const upper=rows['actual-upper-predicates-active-cache-close-debt'];need(upper.active?.available===true&&upper.active.active===1&&upper.active.pending===0&&upper.active.stopped===false&&upper.cached?.cached===1&&upper.cached.pending===0&&upper.cached.stopped===false&&upper.closeDebt?.failedCloseDebt===3&&upper.closeDebt.stopped===false,'PIPE_COMPOSITION_UPPER_PIPE_GATE_WITNESS');settled(upper.settled);need(upper.settled.historicalCloseFailures>0,'PIPE_COMPOSITION_HISTORY_NOT_LIVE_DEBT');
 const pending=rows['pending-io-and-late-completion'];need(pending.pending?.pending===1&&pending.pending.duplicates===1&&pending.pending.stopped===false,'PIPE_COMPOSITION_PENDING_LATE_COMPLETION');settled(pending.settled);
 const contention=rows['actual-wrapper-unavailable-snapshot'];need(contention.unknown?.available===false&&contention.unknown.stopped===false,'PIPE_COMPOSITION_CONTENDED_UNKNOWN');settled(contention.settled);
 const sealed=rows['sealed-with-retained-close-debt'];need(sealed.sealed?.sealed===true&&sealed.sealed.stopped===false&&sealed.sealed.failedCloseDebt===1&&sealed.settled?.sealed===true,'PIPE_COMPOSITION_SEALED_LIVE_DEBT');settled(sealed.settled);
}
export function validateCompositionFunctions(r,{parts,contract}){
 exactKeys(r.functions,Object.keys(parts),'PIPE_COMPOSITION_FUNCTION_SET');for(const [name,part] of Object.entries(parts))need(r.functions[name].sha256===part.sha256&&r.functions[name].line===part.line,'PIPE_COMPOSITION_CURRENT_FUNCTION_HASH:'+name);
 need(same(r.primaryThreadScope,contract.primaryThreadScope)&&r.primaryThreadScope.actualGlobalAndHelperCompiled===true&&r.primaryThreadScope.newPrimaryAcceptance===false&&r.primaryThreadScope.unchangedOriginalAssertions===95,'PIPE_COMPOSITION_PRIMARY_NULL_BOUNDARY');
}
export function validateOwnedCleanupComposition(r,{repo,sourceHashes,source,tools,tool,sdk,current}){
 const {contract,manifest,capture}=compositionContract(repo),front=contract.frontDoorIntegration;
 need(r?.status===front.requiredStatus&&r.pass===true&&r.wrapperIntegrated===true&&r.helperCompositionPassed===true&&Array.isArray(r.sourceChanged)&&r.sourceChanged.length===0&&r.wrapperOrigin==='actual native/main.cpp function','PIPE_COMPOSITION_ACTUAL_WRAPPER_REQUIRED');
 need(r.fullGoalComplete===false&&r.productionOrHardwareAccepted===false&&r.productsOrHostLaunched===false&&r.deviceServiceDriverSleepDeploymentOperations===false&&r.remaining?.some(v=>v.includes('UNKNOWN'))&&r.remaining.some(v=>v.includes('not executed')),'PIPE_COMPOSITION_SCOPE');
 const required=front.currentSourceDependencies,rows=r.sources?.map(row=>({path:row.relative,sha256:row.sha256})),bound=source(rows,'owned-cleanup-composition');
 need(bound.size===required.length&&required.every(name=>bound.has(name)),'PIPE_COMPOSITION_SOURCE_CLOSURE');
 for(const row of r.sources){need(norm(row.path)===norm(path.join(repo,row.relative)),'PIPE_COMPOSITION_LIVE_SOURCE_PATH');need(hash(path.join(r.out,'source',row.relative))===row.sha256,'PIPE_COMPOSITION_FROZEN_SOURCE_HASH');}
 const parts=Object.fromEntries(Object.entries(contract.functions).map(([name,file])=>[name,cleanupFunction(decode(path.join(repo,file)),name)]));
 validateCompositionFunctions(r,{parts,contract});
 current.hashes({inputHostPipeRetirementProvenLocked:r.functions.inputHostPipeRetirementProvenLocked,inputHostPrimaryThreadRetirementProvenLocked:r.functions.inputHostPrimaryThreadRetirementProvenLocked},'owned-cleanup-composition');
 const output=r.out;need(typeof output==='string'&&path.isAbsolute(output),'PIPE_COMPOSITION_ACTUAL_OUTPUT_REQUIRED');
 const context=read(path.join(output,'registration-context.json')),owned=context.ownedWorkspace,expectedPin=schema().sensorEnsure.currentMainManifest;
 need(context.kind==='controller-signal-owned-cleanup-composition'&&context.parentLifecycleAcceptance==='INCOMPLETE'&&same(context.approvedIdentityManifest,expectedPin)&&hash(expectedPin.path)===expectedPin.sha256,'PIPE_COMPOSITION_APPROVED_IDENTITY_CONTEXT');
 const approval=read(expectedPin.path);need(approval.mainSha256===sourceHashes['native/main.cpp']&&approval.sourceBindings?.['native/main.cpp']===approval.mainSha256,'PIPE_COMPOSITION_EXPLICIT_CURRENT_MAIN');
 for(const [name,pin] of Object.entries(approval.sourceBindings))need(sourceHashes[name]===pin,'PIPE_COMPOSITION_APPROVED_SOURCE_PIN');
 need(same(context.sourceHashes,sourceHashes)&&norm(context.runtimeSourceClosure.path)===norm(path.join(output,'../runtime_sourceClosure.json'))&&hash(context.runtimeSourceClosure.path)===context.runtimeSourceClosure.sha256&&same(read(context.runtimeSourceClosure.path),sourceHashes),'PIPE_COMPOSITION_RUNTIME_SOURCE_CLOSURE');
 const base=path.resolve(repo,'../../../Testing/YMCC/Evidence/VC-CaptureReceipt23/t');below(output,base,'PIPE_COMPOSITION_SHORT_OWNED_OUTPUT');
 need(owned?.id==='owned-cleanup-composition'&&norm(output)===norm(owned.buildOutput)&&owned.tempPathLength<=110&&owned.outputPathLength<=110&&norm(owned.temp)===norm(path.join(output,'../t'))&&hash(owned.jobIdentity.path)===owned.jobIdentity.sha256&&hash(owned.owner.path)===owned.owner.sha256,'PIPE_COMPOSITION_OWNED_JOB_IDENTITY');
 const job=read(owned.jobIdentity.path),owner=read(owned.owner.path);need(job.id===owned.id&&norm(job.buildOutput)===norm(output)&&norm(owner.repository)===norm(repo)&&same(owner.sourceHashes,sourceHashes),'PIPE_COMPOSITION_OWNER_SOURCE_IDENTITY');
 const view=context.toolchainView;need(view&&norm(view.path)===norm(path.join(owned.directory,'tc'))&&same(view.original,contract.toolchain)&&view.manifestSha256===contract.toolchain.manifestSha256&&hash(path.join(view.path,'toolchain-manifest.json'))===view.manifestSha256&&hash(path.join(view.path,'build-environment.json'))===view.captureSha256&&norm(view.ownedTemp)===norm(owned.temp),'PIPE_COMPOSITION_DERIVED_TOOLCHAIN_IDENTITY');
 const derived=structuredClone(capture);derived.environment.TEMP=owned.temp;derived.environment.TMP=owned.temp;
 need(same(read(path.join(view.path,'build-environment.json')),derived),'PIPE_COMPOSITION_ONLY_OWNED_TEMP_REBINDING');
 const nestedProof=validateCompositionNestedClosure(context,{repo,owned,sourceHashes,manifest,contract});
 need(Date.parse(read(context.nestedToolClosure.before.path).finishedAt)<=Date.parse(context.entry?.startedAt)&&Date.parse(read(context.nestedToolClosure.after.path).startedAt)>=Date.parse(context.entry?.finishedAt),'PIPE_COMPOSITION_NESTED_EXECUTION_WINDOW');
 const entry=context.entry,expectedArgs=['-B',path.join(repo,front.consumer),'--require-wrapper','--out',output,'--toolchain',view.path];
 need(entry?.exitCode===0&&entry.signal===null&&entry.error===null&&entry.timedOut===false&&same(entry.args,expectedArgs),'PIPE_COMPOSITION_REQUIRE_WRAPPER_INVOCATION');tool(entry.executable,tools.primary.python,undefined,'pipe-composition.python');
 for(const key of ['primaryToolsBefore','primaryToolsAfter']){const list=entry[key];exactRows(list,Object.keys(tools.primary),'role','PIPE_COMPOSITION_PRIMARY_ROLES');for(const row of list)tool(row.path,tools.primary[row.role],row.sha256,'pipe-composition.'+row.role);}
 need(same(entry.primaryToolsBefore,entry.primaryToolsAfter),'PIPE_COMPOSITION_PRIMARY_DRIFT');
 need(same(context.delivery,contract.delivery),'PIPE_COMPOSITION_DELIVERY_CONTEXT');
 const tc=r.toolchain;need(norm(tc?.path)===norm(view.path)&&tc.manifestSha256===view.manifestSha256&&tc.captureSha256===view.captureSha256,'PIPE_COMPOSITION_CAPTURE_IDENTITY');
 tool(tc.python,tools.primary.python,undefined,'pipe-composition.reported-python');sdk(capture.environment.WindowsSDKVersion,'pipe-composition');
 need(tc.tools?.length===3,'PIPE_COMPOSITION_NATIVE_TOOLS');
 for(const [index,name,role] of [[0,'cl.exe','compiler'],[1,'link.exe','linker'],[2,'dumpbin.exe','dumpbin']]){const row=tc.tools[index],approved=manifest.tools.find(value=>value.name===name&&norm(value.original)===norm(capture.tools[name].paths[0]));need(approved&&row.sha256===approved.sha256,'PIPE_COMPOSITION_CAPTURE_TOOL_PIN');tool(row.path,tools.native[role],row.sha256,'pipe-composition.'+role);}
 const authority=new Map(manifest.files.map(row=>[norm(row.frozen),row]));need(authority.size===manifest.files.length,'PIPE_COMPOSITION_DUPLICATE_CAPTURE_PATH');
 need(Array.isArray(r.boundToolInputs)&&r.boundToolInputs.length>200&&new Set(r.boundToolInputs.map(row=>norm(row.path))).size===r.boundToolInputs.length,'PIPE_COMPOSITION_TOOL_CLOSURE');
 const observed=new Map();for(const row of r.boundToolInputs){const pin=authority.get(norm(row.path));need(pin&&pin.sha256===row.sha256&&hash(row.path)===row.sha256,'PIPE_COMPOSITION_CAPTURED_INPUT_PIN');}
 const count=front.profiles.length*front.requiredVariants.length;need(r.runs?.length===count&&context.artifacts?.length===count,'PIPE_COMPOSITION_TEN_BUILD_MATRIX');
 const sourceDir=path.join(output,'source'),template=decode(path.join(repo,'tools/fixtures/input_host_owned_cleanup_composition/suite.cpp.in')),leaf=decode(path.join(repo,'tools/input_pipe_retirement_memory_selftest.cpp')),marker='using json=nlohmann::json;';need(leaf.split(marker).length===2,'PIPE_COMPOSITION_LEAF_BOUNDARY');const prefix=leaf.slice(0,leaf.indexOf(marker));
 const main=decode(path.join(repo,'native/main.cpp')),raw=main.match(/^static HANDLE g_inputHostUnadoptedPipe\s*=\s*nullptr\s*;/gm);need(raw?.length===1,'PIPE_COMPOSITION_ACTUAL_RAW_SLOT');
 const globalNames=['g_inputHostProcess','g_inputHostPrimaryThread','g_inputHostServerPipe','g_inputHostRunId','g_inputHostEpoch'];
 const actualGlobals=globalNames.map(name=>{const matches=[...main.matchAll(new RegExp('^static[^\\n;{}]*\\b'+name+'\\b[^\\n;]*;','gm'))];need(matches.length===1,'PIPE_COMPOSITION_ACTUAL_HOST_GLOBAL:'+name);return matches[0][0];}).join('\n');
 const predicates=[parts.inputHostOwnedCleanupProvenLocked.text,parts.inputHostDisabledCleanupProvenLocked.text].join('\n'),wrapper=parts.inputHostPipeRetirementProvenLocked.text,terminal='return snapshot.available && snapshot.stopped;',rawGuard='if (!CloseHandle(g_inputHostUnadoptedPipe)) return false;';
 need(wrapper.split(terminal).length===2&&wrapper.split(rawGuard).length===2&&parts.inputHostOwnedCleanupProvenLocked.text.split('inputHostPipeRetirementProvenLocked()').length===2,'PIPE_COMPOSITION_MUTATION_ANCHORS');
 for(const profile of front.profiles)for(const variant of front.requiredVariants){
  const found=r.runs.filter(row=>row.profile===profile&&row.variant===variant),art=context.artifacts.filter(row=>row.profile===profile&&row.variant===variant);need(found.length===1&&art.length===1,'PIPE_COMPOSITION_PROFILE_VARIANT');const row=found[0],positive=variant==='current',target=path.join(output,profile+'-'+variant),files=art[0].files;
  need(row.expectedNegative===!positive&&row.accepted===true&&row.exit===(positive?0:2)&&row.checks===contract.expectedCounts[variant][0],'PIPE_COMPOSITION_ACTUAL_BUILD_OUTCOME');
  exactKeys(files,Object.keys(roles),'PIPE_COMPOSITION_ARTIFACT_SET');for(const [role,name] of Object.entries(roles)){const item=files[role];need(norm(item.path)===norm(path.join(target,name))&&hash(item.path)===item.sha256,'PIPE_COMPOSITION_ACTUAL_ARTIFACT_PIN');}
  need(norm(row.result)===norm(files.runtime.path)&&row.binarySha256===files.executable.sha256,'PIPE_COMPOSITION_RUNTIME_BINARY_IDENTITY');
  const runtime=read(files.runtime.path);validateCleanupRuntime(runtime,variant,contract);need(same(row.failedAssertions,runtime.failures),'PIPE_COMPOSITION_NEGATIVE_ASSERTIONS');
  const runText=decode(files.runLog.path);need(same(JSON.parse(runText),runtime),'PIPE_COMPOSITION_ACTUAL_RUN_LOG');
  let w=wrapper,pr=predicates;
  if(variant==='ignore-pipe-gate')pr=pr.replace('inputHostPipeRetirementProvenLocked()','true');
  if(variant==='pending-only-adapter')w=w.replace(terminal,'return snapshot.available && snapshot.pending == 0;');
  if(variant==='historical-failure-is-live-debt')w=w.replace(terminal,'return snapshot.available && snapshot.stopped && snapshot.closeFailures == 0;');
  if(variant==='unchecked-raw-slot-close')w=w.replace(rawGuard,'(void)CloseHandle(g_inputHostUnadoptedPipe);');
  let generated=template;for(const [tag,value] of [['@@PIPE_LEAVES@@',prefix],['@@HOST_GLOBALS@@',actualGlobals],['@@PRIMARY_HELPER@@',parts.inputHostPrimaryThreadRetirementProvenLocked.text],['@@RAW_SLOT@@',raw[0]],['@@WRAPPER@@',w],['@@PREDICATES@@',pr]]){need(generated.split(tag).length===2,'PIPE_COMPOSITION_GENERATION_MARKER');generated=generated.replace(tag,value);}
  need(sha(generated)===files.fixture.sha256,'PIPE_COMPOSITION_WHOLE_ACTUAL_COMPOSITION_UNIT');
  const flags=profile==='Release'?['/O2','/DNDEBUG']:['/Od','/RTC1'];
  const command=[tc.tools[0].path,'/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/wd4505','/MT',...flags,'/I'+path.join(sourceDir,'native'),'/I'+path.join(sourceDir,'deps/json'),'/I'+path.join(sourceDir,'tools'),'/sourceDependencies',files.dependencies.path,files.fixture.path,'/Fo'+path.join(target,'fixture.obj'),'/Fe'+files.executable.path,'/link','/VERBOSE:LIB'];
  need(same(read(files.command.path),command),'PIPE_COMPOSITION_ACTUAL_COMPILE_PROFILE');
  const deps=read(files.dependencies.path).Data,actual=[deps.Source,...deps.Includes];need(row.actualCompilerInputs===actual.length&&actual.length>250,'PIPE_COMPOSITION_COMPILER_INCLUDE_COUNT');
  for(const file of actual){if(norm(file)===norm(files.fixture.path)){need(hash(file)===files.fixture.sha256,'PIPE_COMPOSITION_GENERATED_DEPENDENCY');continue;}
   const rel=path.relative(sourceDir,file);if(rel&&!path.isAbsolute(rel)&&!rel.startsWith('..')){const name=rel.replaceAll('\\','/');need(bound.has(name)&&hash(file)===sourceHashes[name],'PIPE_COMPOSITION_SOURCE_INCLUDE_ESCAPE');}
   else{const pin=authority.get(norm(file));need(pin&&hash(file)===pin.sha256,'PIPE_COMPOSITION_COMPILER_INPUT_ESCAPE');observed.set(norm(file),pin.sha256);}
  }
  const buildText=decode(files.buildLog.path),libraries=[...buildText.matchAll(/^\s*(?:Searching|姝ｅ湪鎼滅储)\s+(.+?\.lib)\s*:/gmi)].map(match=>match[1].trim());need(libraries.length>0,'PIPE_COMPOSITION_LINK_LIBRARY_PROOF');
  for(const file of libraries){const pin=authority.get(norm(file));need(pin&&hash(file)===pin.sha256,'PIPE_COMPOSITION_LINKER_INPUT_ESCAPE');observed.set(norm(file),pin.sha256);}
  const imports=decode(files.imports.path).toLowerCase();need(imports.includes('kernel32.dll')&&!['hid.dll','setupapi.dll','dinput8.dll','xinput','steam_api','winmm.dll','user32.dll'].some(value=>imports.includes(value)),'PIPE_COMPOSITION_NO_DEVICE_IMPORTS');
 }
 need(observed.size===r.boundToolInputs.length&&r.boundToolInputs.every(row=>observed.get(norm(row.path))===row.sha256),'PIPE_COMPOSITION_ACTUAL_TOOL_CLOSURE');
 need(norm(context.actualReceipt.path)===norm(path.join(output,'RESULT.json'))&&hash(context.actualReceipt.path)===context.actualReceipt.sha256&&same(read(context.actualReceipt.path),r),'PIPE_COMPOSITION_ORIGINAL_RECEIPT_BYTES');
 return {id:contract.id,pass:true,profiles:2,currentBuilds:2,specifiedNegativeBuilds:8,privateKernelResourcesReturnedBeforeExit:true,nestedToolClosure:nestedProof,wholeLifecycleAcceptance:false,parentLifecycleAcceptance:'INCOMPLETE'};
}
