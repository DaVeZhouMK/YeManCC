// R115 Win5 continuity audit: compiles unmodified actual production function slices.
// Expected current red contracts return exit 1; compilation/infrastructure errors return 2.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {inflateRawSync} from 'node:zlib';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const workspace=path.resolve(repo,'../../..');
const options={source:path.join(repo,'native/main.cpp'),out:path.join(workspace,'Build/Validation/VirtualInput-HelperLease-R115-20261016/win5-regression'),case:''};
for(let i=2;i<process.argv.length;i++){
 const arg=process.argv[i];
 if(arg==='--help'){console.log('node tools/win5_handoff_continuity_r115_selftest.mjs [--source MAIN_CPP] [--out DIR] [--case NAME]\nExit 1 means a real production contract is red; no expected-failure-to-green conversion.');process.exit(0);}
 assert(['--source','--out','--case'].includes(arg),'Unknown '+arg);assert(process.argv[i+1],'Missing value for '+arg);
 const value=process.argv[++i];options[arg.slice(2)]=arg==='--case'?value:path.resolve(value);
}
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const save=(dir,name,value)=>fs.writeFileSync(path.join(dir,name),JSON.stringify(value,null,2)+'\n');
function slices(bytes){
 const text=bytes.toString('utf8'),masked=text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,s=>s.replace(/[^\r\n]/g,' '));
 const ranges=[];
 function end(brace){let depth=0;for(let i=brace;i<masked.length;i++){if(masked[i]==='{')depth++;if(masked[i]==='}'&&--depth===0)return i+1;}throw Error('Unclosed block');}
 function bounds(name){const m=new RegExp('static [^;{}\\n]+\\b'+name+'\\([^;{}]*\\)\\s*\\{').exec(text);assert(m,'Missing actual definition: '+name);return [m.index,end(masked.indexOf('{',m.index))];}
 function record(name,start,finish,executed=true){assert(start>=0&&finish>start,name);const code=text.slice(start,finish);ranges.push({name,startLine:text.slice(0,start).split('\n').length,endLine:text.slice(0,finish).split('\n').length,sha256:hash(code),executed,code});return code;}
 function fn(name,executed=true){return record(name,...bounds(name),executed);}
 function region(name,start,endName){return record(name,text.indexOf(start),bounds(endName)[1]);}
 const parts={
  CLASSIFIER:fn('hidHideUpper')+'\n'+fn('hidHideIsVirtualIdentity'),
  MASKS:fn('xinputNonVirtualConnectedMask')+'\n'+fn('xinputPhysicalConnectedMask'),
  FENCE:['inputHostExitFenceRaised','inputHostLifecycleWorkIsCleanup','inputHostExitFenceBlocks','inputHostExitFenceLogRefusal'].map(n=>fn(n)).join('\n'),
  QUEUE:'static bool inputHostQueueLifecycleWorkInternal(const char* reason, bool* coalescedOut);\n'+fn('inputHostQueueLifecycleWork')+'\n'+fn('inputHostQueueLifecycleWorkInternal'),
  RAISE:fn('inputHostRaiseExitFence'),VISIBLE:fn('ymccOpenXInputVisiblePersona'),RESOLVE:fn('ymccOpenXInputResolveVirtualEndpoint'),
  REFRESH:region('physical refresh outcome + actual helper','struct YmccSlotPhysicalRefreshOutcome {','ymccOpenXInputCycleDisplacedPhysical'),
  BUDGET:region('entire intent budget implementation','struct YmccSlotIntentBudget {','ymccSlotIntentOperations'),
  PLAN:region('entire plan declarations + queue + gates + schedule + full step','struct YmccSlotHandoffPlan {','ymccOpenXInputSlotHandoffStep'),
  GUARD:fn('ymccOpenXInputVirtualSlotGuard'),SLOTGUARD:fn('ymccOpenXInputSlotGuard')
 };
 const [workerStart,workerEnd]=bounds('inputHostRunQueuedLifecycleWork');
 const suspendedStart=text.indexOf('    if (g_externalPriorityVirtualSuspended.load',workerStart);
 assert(suspendedStart>workerStart&&suspendedStart<workerEnd);
 const suspendedEnd=end(masked.indexOf('{',suspendedStart));
 parts.SUSPENDED_DISPATCH=record('actual external-priority dequeued-work suppression',suspendedStart,suspendedEnd);
 const auditNames=['exitCleanupThreadProc','inputHostStop','beginAsyncExit','physicalReleaseWindowCancelBorrow','physicalReleaseWindowClose','physicalReleaseWindowCloseDutyPump','physicalReleaseWindowDutyHandOffToT0','hidHideRestorePhysicalTransaction','inputHostStopLocked','inputHostClearLocked','inputHostRunQueuedLifecycleWork','observeStartupInputGate','hidHideObserveRawInputDevice','hidHideResolveRawInputIdentity'];
 for(const name of auditNames)fn(name,false);
 return {parts,ranges};
}
function fieldEvidence(out){
 const zipPath=path.join(workspace,'日志/Win5/YeManCC-logs-20261016-101717-Win5.zip');
 if(!fs.existsSync(zipPath))return {available:false,path:zipPath};
 const bytes=fs.readFileSync(zipPath),rawDir=path.join(out,'raw');fs.mkdirSync(rawDir,{recursive:true});
 let end=-1;for(let i=bytes.length-22;i>=Math.max(0,bytes.length-65557);i--)if(bytes.readUInt32LE(i)===0x06054b50){end=i;break;}
 assert(end>=0,'ZIP end directory');let offset=bytes.readUInt32LE(end+16);const count=bytes.readUInt16LE(end+10),inventory=[],files=[];
 const wanted=new Set(['native-lifecycle.log','native-detail.log','virtual-gamepad.log','input-host-fatal.log','YeManCC-export-manifest.txt','yeman-settings.json']);
 for(let i=0;i<count;i++){
  assert.equal(bytes.readUInt32LE(offset),0x02014b50);const method=bytes.readUInt16LE(offset+10),compressed=bytes.readUInt32LE(offset+20),size=bytes.readUInt32LE(offset+24),n=bytes.readUInt16LE(offset+28),extra=bytes.readUInt16LE(offset+30),comment=bytes.readUInt16LE(offset+32),local=bytes.readUInt32LE(offset+42),name=bytes.subarray(offset+46,offset+46+n).toString('utf8');
  inventory.push({name,size,compressed,method});
  if(wanted.has(name)){
   assert(size<16*1024*1024,'Unexpected selected log size');assert.equal(bytes.readUInt32LE(local),0x04034b50);
   const start=local+30+bytes.readUInt16LE(local+26)+bytes.readUInt16LE(local+28),packed=bytes.subarray(start,start+compressed);
   assert([0,8].includes(method),'Unsupported selected method');const raw=method===8?inflateRawSync(packed):packed;assert.equal(raw.length,size);
   fs.writeFileSync(path.join(rawDir,name),raw);files.push({entry:name,size,sha256:hash(raw)});
  }
  offset+=46+n+extra+comment;
 }
 save(out,'zip-inventory.json',inventory);
 const rawLines=fs.readFileSync(path.join(rawDir,'native-lifecycle.log'),'utf8').split(/\r?\n/),rows=[];
 for(let i=0;i<rawLines.length;i++)try{const row=JSON.parse(rawLines[i]);rows.push({line:i+1,row,raw:rawLines[i]});}catch{}
 const selected=rows.filter(({row})=>['gamepad-slot-handoff','gamepad-slot-handoff-queue','gamepad-slot-handoff-exhausted','gamepad-openxinput-virtual-slot-guard','input-host-physical-release-window','begin-async-exit','input-host-exit-fence','input-host-stop-request','input-host-stop-result'].includes(row.event));
 save(out,'field-selected-lines.json',selected);
 fs.writeFileSync(path.join(out,'field-selected-original.jsonl'),selected.map(x=>x.raw).join('\n')+'\n');
 const handoffs=rows.filter(x=>x.row.event==='gamepad-slot-handoff'),exits=rows.filter(x=>x.row.event==='begin-async-exit');
 const compact=selected.filter(x=>x.row.event.startsWith('gamepad-slot-handoff')).map(({line,row})=>({line,...Object.fromEntries(['time','event','action','reason','epoch','intentOps','assigned','accepted','cycleOk','rollbackNeeded','rollbackVerified'].filter(k=>k in row).map(k=>[k,row[k]]))}));
 save(out,'field-handoff-timeline.json',compact);
 return {available:true,path:zipPath,sha256:hash(bytes),bytes:bytes.length,entries:inventory.length,selectedFiles:files,lifecycleLines:rawLines.length,
  handoffCount:handoffs.length,exhaustionCount:rows.filter(x=>x.row.event==='gamepad-slot-handoff-exhausted').length,
  handoffsWithR113Outcome:handoffs.filter(x=>'physicalRefreshOutcome'in x.row).length,beginAsyncExitRows:exits.length,
  releaseWindowRows:rows.filter(x=>x.row.event==='input-host-physical-release-window').length,
  guardRows:rows.filter(x=>x.row.event==='gamepad-openxinput-virtual-slot-guard').length,
  claim:'Historical Win5 context only. Not R114 acceptance, not proof the R115 synthetic boundary states happened in the field.'};
}
function command(dir,label,exe,args,timeout=60000){
 const r=spawnSync(exe,args,{cwd:dir,encoding:'utf8',timeout});
 fs.writeFileSync(path.join(dir,label+'.stdout.log'),r.stdout??'');fs.writeFileSync(path.join(dir,label+'.stderr.log'),r.stderr??'');
 save(dir,label+'.command.json',{exe,args,cwd:dir,status:r.status,signal:r.signal,error:r.error?String(r.error):null});assert(!r.error,label+': '+r.error);
 return r;
}
try{
 fs.mkdirSync(options.out,{recursive:true});
 const run=path.join(options.out,'runs',new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid);fs.mkdirSync(run,{recursive:true});
 const bytes=fs.readFileSync(options.source),actual=slices(bytes),fixturePath=path.join(repo,'tools/fixtures/win5_handoff_continuity_r115/fixture.cpp'),fixtureBytes=fs.readFileSync(fixturePath),jsonPath=path.join(repo,'deps/json/json.hpp'),jsonBytes=fs.readFileSync(jsonPath);
 const extracted=path.join(run,'extracted');fs.mkdirSync(extracted);fs.writeFileSync(path.join(run,'source-main.cpp.snapshot'),bytes);fs.writeFileSync(path.join(extracted,'json.hpp'),jsonBytes);
 for(const r of actual.ranges)fs.writeFileSync(path.join(extracted,r.name.replace(/[^\w-]/g,'_')+'.txt'),r.code);
 const bindings={source:{path:options.source,sha256:hash(bytes)},fixture:{path:fixturePath,sha256:hash(fixtureBytes)},json:{path:jsonPath,sha256:hash(jsonBytes)},ranges:actual.ranges.map(({code,...r})=>r),productionRewrites:[],
  execution:'Whole actual mask/classifier/budget/queue/fence/schedule/gate/blocker/step/guard bodies. Only inert process-local driver queries, assignment/cycle results, clock, logging, and unrelated MSI admission/audit leaves are supplied. Full stop/release/worker bodies are saved and inspected; only the exact worker external-priority suppression block is executed, not the whole worker.',
  scopeExclusions:['native helper attempt/lease ownership','CtrlAlt','device/service/Steam/product operations','full application build/package','field functional/CPU acceptance']};
 save(run,'source-bindings.json',bindings);
 let cpp=fixtureBytes.toString('utf8');for(const [key,code]of Object.entries(actual.parts)){const marker='@@'+key+'@@';assert.equal(cpp.split(marker).length,2,key);cpp=cpp.replace(marker,()=>code);}assert(!/@@\w+@@/.test(cpp));
 const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';assert(fs.existsSync(vcvars),'Missing compiler bootstrap');
 const quote=s=>{assert(!/["\r\n%]/.test(s));return '"'+s+'"';};
 const results=[];
 for(const diagnostic of [false,true]){
  const dir=path.join(run,diagnostic?'diagnostic-on':'diagnostic-off');fs.mkdirSync(dir);fs.writeFileSync(path.join(dir,'fixture.cpp'),cpp);
  const compile=path.join(dir,'compile.cmd');fs.writeFileSync(compile,'@echo off\r\ncall '+quote(vcvars)+' >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 /WX /O2'+(diagnostic?' /DYMCC_MSI_REPORT_CHAIN_DIAGNOSTIC':'')+' '+quote(path.join(dir,'fixture.cpp'))+' /I'+quote(extracted)+' /Fo'+quote(path.join(dir,'fixture.obj'))+' /Fe'+quote(path.join(dir,'fixture.exe'))+'\r\nexit /b %errorlevel%\r\n');
  const build=command(dir,'compile','cmd.exe',['/d','/c',compile]);assert.equal(build.status,0,'Fixture compilation failed; see '+dir);
  const execute=command(dir,'execute',path.join(dir,'fixture.exe'),options.case?[options.case]:[]);assert([0,1].includes(execute.status),'Unexpected fixture exit');
  const result=JSON.parse(execute.stdout);assert.equal(execute.status,result.failedCases?1:0);save(dir,'results.json',result);results.push(result);
 }
 assert.deepEqual(results[0],results[1],'Diagnostic mode changes behavior');
 const again=slices(fs.readFileSync(options.source));const changed=actual.ranges.filter(r=>!again.ranges.some(a=>a.name===r.name&&a.sha256===r.sha256));
 assert.equal(changed.length,0,'Audited source scope changed during run; rerun from a stable actual-source snapshot');
 const field=fieldEvidence(options.out);save(options.out,'field-evidence.json',field);
 const result=results[0],red=result.cases.filter(c=>!c.passed),summary={at:new Date().toISOString(),run,sourceSha256:hash(bytes),caseFilter:options.case||null,
  caseCount:result.caseCount,checkCount:result.checkCount,failedCases:result.failedCases,failedChecks:result.failedChecks,redCases:red.map(c=>({name:c.name,checks:c.checks.filter(k=>!k.passed)})),
  diagnosticModesIdentical:true,auditedRangesStable:true,productionModifiedByHarness:false,productionRewrittenByHarness:false,deviceIOExecuted:false,field,
  result:result.failedCases?'RED_ACTUAL_SOURCE_CONTRACTS':'PASS',limitations:bindings.execution};
 save(run,'summary.json',summary);save(options.out,'summary.json',summary);
 console.log(JSON.stringify({result:summary.result,run,cases:summary.caseCount,checks:summary.checkCount,failedCases:summary.failedCases,failedChecks:summary.failedChecks,redCases:red.map(c=>c.name),diagnosticModesIdentical:true,sourceSha256:summary.sourceSha256},null,2));
 process.exitCode=result.failedCases?1:0;
}catch(error){console.error(error.stack??String(error));process.exitCode=2;}
