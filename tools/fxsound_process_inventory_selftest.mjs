/** Optional selection of tools/fxsound_recovery_selftest.ps1; no new ID/gate.
 * Importing this module performs no discovery, writes, compilation or execution.
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
const here=path.dirname(fileURLToPath(import.meta.url));
const sha=value=>crypto.createHash('sha256').update(value).digest('hex');
const text=file=>fs.readFileSync(file,'utf8').replace(/^\uFEFF/,'');
const ensure=(value,message)=>{if(!value)throw Error(message);};
const inside=(root,file)=>{const rel=path.relative(root,file);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
const occurrences=(source,needle)=>source.split(needle).length-1;

// Balanced source extraction; braces in strings/comments cannot shorten a slice.
export function extractFunction(source,anchor){
 const start=source.indexOf(anchor);ensure(start>=0&&source.indexOf(anchor,start+anchor.length)<0,'MISSING_OR_DUPLICATE_ANCHOR '+anchor);
 const open=source.indexOf('{',start);ensure(open>=start,'MISSING_BODY '+anchor);
 let depth=0,state='code',escaped=false;
 for(let at=open;at<source.length;at++){
  const ch=source[at],next=source[at+1];
  if(state==='line'){if(ch==='\n')state='code';continue;}
  if(state==='block'){if(ch==='*'&&next==='/'){state='code';at++;}continue;}
  if(state==='string'||state==='char'){if(escaped){escaped=false;continue;}if(ch==='\\'){escaped=true;continue;}if(ch===(state==='string'?'"':"'"))state='code';continue;}
  if(ch==='/'&&next==='/'){state='line';at++;continue;}if(ch==='/'&&next==='*'){state='block';at++;continue;}
  if(ch==='"'){state='string';continue;}if(ch==="'"){state='char';continue;}
  if(ch==='{')depth++;if(ch==='}'&&--depth===0)return source.slice(start,at+1);
 }
 throw Error('UNTERMINATED_BODY '+anchor);
}
function slices(recovery,control){
 const processState=extractFunction(recovery,'inline ProcessState processState(');
 const identity=extractFunction(control,'inline FxIdentity identity(');
 const current=extractFunction(control,'inline bool current(');
 const identityType=control.match(/struct FxIdentity\s*\{[^}]*\};/g);ensure(identityType?.length===1,'IDENTITY_TYPE_ANCHOR');
 return {processState,identityType:identityType[0],identity,current};
}
function renderNamespace(name,slice){return `namespace ${name} {\nusing ProcessState=ymcc::fxsound::ProcessState;\nusing osfacts::executable;\n${slice.processState}\n${slice.identityType}\n${slice.identity}\n${slice.current}\n}\n`;}
export function renderProcessInventorySlices({before,afterRecovery,afterControl}){
 return renderNamespace('fx_before',before)+renderNamespace('fx_after',slices(afterRecovery,afterControl));
}
export function checkProcessInventoryBoundaries({recovery,control,mainBefore,mainAfter}){
 const candidate=slices(recovery,control);
 ensure(/processState\(ULONGLONG maxAge\s*=\s*0\)/.test(candidate.processState),'PROCESS_STATE_DEFAULT_MUST_BE_FRESH0');
 ensure(/processInventory::read\(ymcc::processInventory::resolveQuery\(\),\s*ymcc::processInventory::currentTime,\s*maxAge\)/.test(candidate.processState),'PROCESS_STATE_MUST_USE_ACTUAL_OWNER');
 ensure(!/CreateToolhelp32Snapshot|Process32FirstW|Process32NextW/.test(candidate.processState+candidate.identity),'LOCAL_PRODUCER_REMAINS');
 ensure(/processInventory::read\(ymcc::processInventory::resolveQuery\(\),\s*ymcc::processInventory::currentTime,\s*0\)/.test(candidate.identity),'IDENTITY_MUST_READ_FRESH0');
 ensure(candidate.identity.includes('GetProcessId(process)==entry.pid')&&candidate.identity.includes('WaitForSingleObject(process,0)==WAIT_TIMEOUT')&&candidate.identity.includes('heldSession==session'),'HELD_IDENTITY_GUARDS_MISSING');
 ensure(candidate.current.includes('auto now=identity()'),'CURRENT_MUST_USE_FRESH_IDENTITY');
 const observe=extractFunction(control,'inline ControlState observe(');
 ensure(/observe\(ULONGLONG maxAge\s*=\s*0\)/.test(observe),'OBSERVE_DEFAULT_MUST_BE_FRESH0');
 ensure(occurrences(observe,'processState(')===1&&observe.includes('const auto process=processState(maxAge);')&&observe.includes('port.read(process)'),'OBSERVE_MUST_FORWARD_SINGLE_RESULT');
 const port=extractFunction(recovery,'Snapshot read(std::optional<ProcessState>');
 ensure(/read\(std::optional<ProcessState> observedProcess\s*=\s*std::nullopt\) const/.test(port),'PORT_DEFAULT_OPTIONAL_MISSING');
 ensure(occurrences(port,'processState(')===1&&/observedProcess\s*\?\s*\*observedProcess\s*:\s*processState\(\)/.test(port),'PORT_MUST_LAZILY_FALL_BACK_TO_FRESH0');
 const original='const auto result=fxSoundStateJson(ymcc::fxsound::observe());',updated='const auto result=fxSoundStateJson(ymcc::fxsound::observe(250));';
 ensure(occurrences(mainBefore,original)===1&&occurrences(mainAfter,updated)===1,'MAIN_GETSTATE_HUNK_NOT_UNIQUE');
 const callback=extractFunction(mainAfter,'ipc_on("fxsound.getState",');
 ensure(callback.includes(updated),'250_CALL_NOT_INSIDE_GETSTATE');
 ensure(occurrences(mainAfter,'ymcc::fxsound::observe(250)')===1,'PASSIVE_250_NOT_EXCLUSIVE');
 ensure(mainAfter.replace(updated,original)===mainBefore,'MAIN_INVERSE_DIFFERS');
 return {status:'STATIC_BOUNDARY_ONLY',sources:['native/fxsound_recovery.h','native/fxsound_control.h','native/main.cpp'],observeDefaultAge:0,processDefaultAge:0,identityAge:0,passiveGetStateAge:250,audioBehavior:'NOT_RUN'};
}
function privatePath(root,target,exists=true){
 const candidate=path.resolve(target);ensure(inside(root,candidate),'OUTSIDE_OWNED_PRIVATE_ROOT '+candidate);
 let cursor=candidate;while(!fs.existsSync(cursor))cursor=path.dirname(cursor);
 ensure(inside(fs.realpathSync(root),fs.realpathSync(cursor)),'PRIVATE_PATH_REPARSE_ESCAPE '+candidate);
 if(exists)ensure(fs.existsSync(candidate),'INPUT_MISSING '+candidate);return candidate;
}
function pin(file,label){const raw=fs.readFileSync(file);return {file,label,bytes:raw.length,sha256:sha(raw)};}
export async function prepareProcessInventory({repoRoot,ownedPrivateRoot,sourceOverride,beforeRoot,mainHunkFile,outputDir,vcvars64='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'}={}){
 ensure(repoRoot&&ownedPrivateRoot&&outputDir,'REPO_OWNED_ROOT_OUTPUT_REQUIRED');
 repoRoot=fs.realpathSync(repoRoot);ownedPrivateRoot=fs.realpathSync(ownedPrivateRoot);
 ensure(!inside(repoRoot,ownedPrivateRoot),'PRIVATE_ROOT_MUST_BE_OUTSIDE_PRODUCT_SOURCE');
 const selected=sourceOverride?privatePath(ownedPrivateRoot,sourceOverride):repoRoot;
 const out=privatePath(ownedPrivateRoot,outputDir,false);ensure(!fs.existsSync(out),'OUTPUT_DIRECTORY_MUST_BE_NEW');
 const recoveryFile=path.join(selected,'native/fxsound_recovery.h'),controlFile=path.join(selected,'native/fxsound_control.h');
 if(sourceOverride){privatePath(ownedPrivateRoot,recoveryFile);privatePath(ownedPrivateRoot,controlFile);}
 const recovery=text(recoveryFile),control=text(controlFile),mainFile=path.join(repoRoot,'native/main.cpp'),mainRaw=fs.readFileSync(mainFile),mainBefore=mainRaw.toString('utf8');
 const original='const auto result=fxSoundStateJson(ymcc::fxsound::observe());',updated='const auto result=fxSoundStateJson(ymcc::fxsound::observe(250));';
 // Private candidate receives only this exact in-memory main hunk. It is never written to product.
 const mainAfter=sourceOverride?mainBefore.replace(original,updated):mainBefore;
 let hunkPin=null;
 if(mainHunkFile){mainHunkFile=privatePath(ownedPrivateRoot,mainHunkFile);const hunk=JSON.parse(text(mainHunkFile));hunkPin=pin(mainHunkFile,'explicit main getState hunk');
  ensure(hunk.file==='native/main.cpp'&&hunk.before===original&&hunk.after===updated,'MAIN_HUNK_CONTRACT');
  ensure(hunk.beforeMain?.sha256===sha(mainRaw)&&hunk.beforeMain?.bytes===mainRaw.length,'MAIN_BEFORE_RAW_PIN_DRIFT');
  const bytes=Buffer.from(mainAfter,'utf8');ensure(hunk.dynamicAfter?.sha256===sha(bytes)&&hunk.dynamicAfter?.bytes===bytes.length,'MAIN_AFTER_RAW_PIN_DRIFT');
 }
 const boundary=checkProcessInventoryBoundaries({recovery,control,mainBefore:sourceOverride?mainBefore:mainBefore.replace(updated,original),mainAfter});
 let before,beforeInputs=[];
 if(beforeRoot){beforeRoot=privatePath(ownedPrivateRoot,beforeRoot);const r=privatePath(ownedPrivateRoot,path.join(beforeRoot,'native/fxsound_recovery.h')),c=privatePath(ownedPrivateRoot,path.join(beforeRoot,'native/fxsound_control.h'));before=slices(text(r),text(c));beforeInputs=[pin(r,'before extraction source'),pin(c,'before extraction source')];}
 else {const file=path.join(here,'fxsound_process_inventory_before.json'),fixture=JSON.parse(text(file));before=fixture.slices;beforeInputs=[pin(file,'pinned original consumer slices')];ensure(fixture.sourcePins?.length===2,'BEFORE_SOURCE_PROVENANCE_MISSING');}
 ensure(before.processState.includes('CreateToolhelp32Snapshot')&&!before.processState.includes('processInventory::read'),'BEFORE_MUST_BE_ORIGINAL_ENUMERATION');
 const template=path.join(here,'fxsound_process_inventory_selftest.cpp'),toolchain=path.join(repoRoot,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs');
 const {calibrationToolchainPreamble}=await import(pathToFileURL(toolchain).href);
 const prefix=calibrationToolchainPreamble(vcvars64);
 for(const value of [repoRoot,out])ensure(!/["%\r\n]/.test(value),'UNSUPPORTED_BUILD_PATH');
 const commands={compile:{executable:process.env.ComSpec||'C:/Windows/System32/cmd.exe',args:['/d','/c',path.join(out,'compile.cmd')],cwd:out},before:{executable:path.join(out,'process-inventory.exe'),args:['--before'],cwd:out,expectedExitCode:1},after:{executable:path.join(out,'process-inventory.exe'),args:['--after'],cwd:out,expectedExitCode:0}};
 const sourcePins=[pin(recoveryFile,'after extraction source'),pin(controlFile,'after extraction source'),pin(mainFile,'static main boundary source'),pin(path.join(repoRoot,'native/process_inventory.h'),'actual compiled owner'),pin(path.join(repoRoot,'native/fxsound_recovery_policy.h'),'actual ProcessState declaration'),pin(path.join(repoRoot,'tools/fxsound_recovery_selftest.ps1'),'original consumer selector'),pin(template,'same TU case fixture'),pin(path.join(repoRoot,'tools/fixtures/process_inventory_ports.h'),'shared fake OS inventory ports'),pin(fileURLToPath(import.meta.url),'portable prepare driver'),pin(toolchain,'original calibration toolchain preamble'),pin(vcvars64,'vcvars64 entry'),...beforeInputs,...(hunkPin?[hunkPin]:[])];
 const fixtureText=fs.readFileSync(template,'utf8'),sliceText=renderProcessInventorySlices({before,afterRecovery:recovery,afterControl:control});
 // Exactly one compilation; both generations are in consumer-slices.inc in the same TU.
 const batch=prefix+`cd /d "${out}"\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O1 /DUNICODE /D_UNICODE /showIncludes /I"${repoRoot}" "fixture.cpp" /Fo"process-inventory.obj" /Fe"process-inventory.exe"\r\nexit /b %errorlevel%\r\n`;
 fs.mkdirSync(out);fs.writeFileSync(path.join(out,'fixture.cpp'),fixtureText);fs.writeFileSync(path.join(out,'consumer-slices.inc'),sliceText);fs.writeFileSync(path.join(out,'compile.cmd'),batch);
 sourcePins.push(pin(path.join(out,'fixture.cpp'),'actual TU'),pin(path.join(out,'consumer-slices.inc'),'verbatim extraction bytes'),pin(path.join(out,'compile.cmd'),'actual compilation command'));
 const prepared={schemaVersion:1,status:'PREPARED_NOT_EXECUTED',consumer:'tools/fxsound_recovery_selftest.ps1',selection:'ProcessInventoryOnly',outputDir:out,repoRoot,sourcePins,boundary,commands,compileCount:1,requiredOuterBudgetMs:20000,executionAndCleanup:'External ownerJob; no new budget and no cleanup claim',nativeTestsExecuted:false,fullOriginalConsumer:'NOT_RUN'};
 fs.writeFileSync(path.join(out,'prepare.json'),JSON.stringify(prepared,null,2)+'\n');return prepared;
}
export const prepareFxSoundProcessInventory=prepareProcessInventory;
export function verifyPreparedPins(prepared){for(const item of prepared.sourcePins)ensure(sha(fs.readFileSync(item.file))===item.sha256,'SOURCE_DRIFT '+item.file);}
// Caller supplies its existing execution-budget ownerJob and process runner.
// Children inherit that Job; this driver never creates/resets a budget or claims disposal.
export async function runPreparedProcessInventory(prepared,{ownerJob,runProcess,remainingMs}={}){
 ensure(ownerJob?.budgetMs===20000&&Number.isSafeInteger(ownerJob.deadlineEpochMs)&&typeof runProcess==='function','INHERITED_20S_OWNER_JOB_REQUIRED');
 const remaining=remainingMs||ownerJob.remainingMs||(()=>Math.max(0,ownerJob.deadlineEpochMs-Date.now()-(ownerJob.cleanupReserveMs||0)));
 const results=[];verifyPreparedPins(prepared);
 for(const name of ['compile','before','after']){
  const command=prepared.commands[name],timeoutMs=Math.min(20000,Math.floor(remaining()));ensure(timeoutMs>0,'TIMEOUT_INHERITED_BUDGET');
  const result=await runProcess(command.executable,command.args,{cwd:command.cwd,env:process.env,timeoutMs});
  fs.writeFileSync(path.join(prepared.outputDir,name+'.stdout.log'),result.stdout||'');fs.writeFileSync(path.join(prepared.outputDir,name+'.stderr.log'),result.stderr||'');
  fs.writeFileSync(path.join(prepared.outputDir,name+'.process.json'),JSON.stringify(result,null,2)+'\n');results.push({name,...result});
  ensure(!result.timedOut&&!result.signal&&!result.launchError&&!result.outputTruncated,name.toUpperCase()+'_PROCESS_INCOMPLETE');
  ensure(result.exitCode===(command.expectedExitCode??0),name.toUpperCase()+'_UNEXPECTED_EXIT');
  if(name==='before')ensure((result.stdout||'').includes('BEFORE_DUPLICATE_ENUMERATION snapshots=2 expected=1')&&(result.stderr||'').includes('FAIL BEFORE_DUPLICATE_ENUMERATION:'),'BEFORE_SEMANTIC_FAILURE_MISSING');
  if(name==='after')ensure((result.stdout||'').includes('PASS after actual owner + verbatim processState/identity/current'),'AFTER_SEMANTIC_RESULT_MISSING');
 }
 verifyPreparedPins(prepared);
 const result={schemaVersion:1,status:'PASS_SELECTED_SCOPE',consumer:prepared.consumer,selection:prepared.selection,results,boundary:prepared.boundary,sourcePins:prepared.sourcePins,negativeControl:{status:'EXPECTED_FAIL',rawRetained:true},cleanupVerification:'EXTERNAL_OWNER_JOB_RECEIPT_REQUIRED',fullOriginalConsumer:'NOT_RUN',audioBehavior:'NOT_RUN',summary:{selected:1,executedThisRun:1,actualPassedThisRun:1}};
 fs.writeFileSync(path.join(prepared.outputDir,'result.json'),JSON.stringify(result,null,2)+'\n');return result;
}
async function cli(){
 const args=process.argv.slice(2),values={};let execute=false;
 const mapping={'--repo-root':'repoRoot','--owned-private-root':'ownedPrivateRoot','--source-override':'sourceOverride','--before-root':'beforeRoot','--main-hunk':'mainHunkFile','--output-dir':'outputDir','--vcvars64':'vcvars64','--budget-context':'budgetContext','--budget-nonce':'budgetNonce'};
 for(let i=0;i<args.length;i++){if(args[i]==='--prepare')continue;if(args[i]==='--execute'){execute=true;continue;}const key=mapping[args[i]];ensure(key&&args[i+1]!==undefined,'UNKNOWN_OR_MISSING_ARGUMENT '+args[i]);values[key]=args[++i];}
 let ownerJob,runner;
 if(execute){ensure(values.budgetContext&&values.budgetNonce,'EXTERNAL_EXECUTION_BUDGET_CONTEXT_REQUIRED');const budget=await import(pathToFileURL(path.join(values.repoRoot,'tools/ymcc_sim/optimization/execution-budget.mjs')).href);const repoRoot=fs.realpathSync(values.repoRoot);ownerJob=budget.readBudgetWorker(values.budgetContext,values.budgetNonce,{repoRoot,coreRoot:values.ownedPrivateRoot});ensure(ownerJob.budgetMs===20000,'MUST_INHERIT_EXACT_20S_SCOPE');runner=await import(pathToFileURL(path.join(repoRoot,'tools/ymcc_sim/global/runner.mjs')).href);}
 const prepared=await prepareProcessInventory(values);
 if(execute)await runPreparedProcessInventory(prepared,{ownerJob,runProcess:runner.runProcess,remainingMs:ownerJob.remainingMs});
 console.log(JSON.stringify({status:execute?'PASS_SELECTED_SCOPE':'PREPARED_NOT_EXECUTED',outputDir:prepared.outputDir}));
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))cli().catch(error=>{console.error(error.stack);process.exitCode=1;});