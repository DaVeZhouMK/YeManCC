// Explicit consumers of actual T1 production slices; never part of the default Win5 group.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
import {renderT1CloseBorrow} from './close_borrow_consumer.mjs';
import {calibrationToolchainPreamble} from '../hc_reviewed_calibration/toolchain.mjs';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=p=>{const b=fs.readFileSync(p);return {path:p,bytes:b.length,sha256:sha(b)};};
const root=path.dirname(fileURLToPath(import.meta.url));
const selections={
 't1-open-diagnostic-scope':{template:'queued_borrow.cpp',pending:false,power:true,runtimeArgs:['diagnostic-scope'],cases:['open-log-success','open-log-refused','open-log-expired','open-log-rollback']},
 't1-close-publication':{template:'close_publication.cpp',close:true,cases:['close-no-new-request','close-new-request-at-idle-publication','close-active-refresh-keeps-new-request-pending']},
 't1-queued-borrow':{template:'queued_borrow.cpp',pending:false,power:true,cases:[
  'cancel-before-dispatch','power-before-dispatch','host-before-dispatch','intent-before-dispatch',
  'persona-before-dispatch','expired-before-dispatch','slow-pre-open-cancel','normal-current',
  'new-current-after-cancel','second-cancel-before-dispatch']},
 't1-pending-borrow':{template:'pending_borrow.cpp',pending:true,power:true,cases:[
  'pending-valid-current','pending-deadline-on-dispatch','pending-expired-before-pump',
  'pending-stage0-cancel','pending-host-changed','pending-intent-changed','pending-power-edge']},
 't1-pending-borrow-publication':{template:'pending_borrow_publication.cpp',pending:true,power:false,cases:[
  'pending-cancel-during-context-read','pending-new-owner-during-context-read']}
};
export const t1BorrowCases=Object.freeze(Object.keys(selections));
// Pure source preparation: no output writes, compiler, executable or device activity.
export function renderT1Borrow({repo,source,case:caseName}) {
 if(!Object.hasOwn(selections,caseName))throw Error('Unknown T1 case: '+caseName);
 const selection=selections[caseName];
 if(selection.close)return renderT1CloseBorrow({repo,source,selection,consumerPath:fileURLToPath(import.meta.url)});
 if(!path.isAbsolute(source))throw Error('Absolute source required');
 const sourceBytes=fs.readFileSync(source),text=sourceBytes.toString('utf8').replace(/\r\n/g,'\n');
 const names=[...(selection.pending?['physicalReleaseWindowDeferredConsumeDecision']:[]),
  'physicalReleaseWindowCancelFromStage','physicalReleaseWindowAdmissionBlocked','physicalReleaseWindowCancelBorrow',
  ...(selection.power?['physicalReleaseWindowOnPowerEdge']:[])];
 const members=names.map(n=>extract(text,n));
 members.push(...['physicalReleaseWindowCaptureQueuedOpen','physicalReleaseWindowQueuedContextRetired',
  'physicalReleaseWindowQueuedOpenRetired','physicalReleaseWindowRetireQueuedOpen',
  'physicalReleaseWindowOpen','physicalReleaseWindowToggleRequest',
  ...(selection.pending?['physicalReleaseWindowPump']:[])].map(n=>extract(text,n)));
 const start=text.indexOf('static constexpr ULONGLONG kPhysicalReleaseWindowMs');
 const end=text.indexOf('// ── GP-GBRETURN-1（M1/M3/M4/M5）',start);
 if(start<0||end<start)throw Error('Window declaration source drift');
 const region=text.slice(start,end);
 if(!region.includes('struct PhysicalReleaseWindowQueuedOpen {'))throw Error('Early queued-open declaration required');
 const constants=(text.match(/^static constexpr ULONGLONG kT1[^\n]+/gm)??[]).join('\n');
 if(!constants)throw Error('T1 constants absent');
 const deferred=(text.match(/^static constexpr ULONGLONG kPhysicalReleaseWindowDeferredRetryMs[^\n]+/gm)??[]).join('\n');
 if(selection.pending&&!deferred)throw Error('Deferred retry constant absent');
 const declarations=(selection.pending?deferred+'\nenum class PhysicalReleaseWindowDeferredDecision:int {None=0,Wait=1,Consume=2};\n':'')+
  constants+'\n'+region+'\nenum class PhysicalReleaseWindowCancelAction:int { None=0,Cleared=1,WaitInFlight=2,AbortInFlightOpen=3 };';
 const template=path.join(root,selection.template),jsonPath=path.join(repo,'deps/json/json.hpp');
 const diagnosticHeaderPath=path.join(repo,'native/input_host_diagnostic_dispatch.h');
 let cpp=fs.readFileSync(template,'utf8');
 for(const [marker,value] of [['@@DECLARATIONS@@',declarations],['@@METHODS@@',members.map(x=>x.code).join('\n')]]){
  if(cpp.split(marker).length!==2)throw Error('Template marker drift '+marker);cpp=cpp.replace(marker,()=>value);
 }
 if(/@@\w+@@/.test(cpp))throw Error('Unexpanded template marker');
 const inputs=[{path:source,bytes:sourceBytes.length,sha256:sha(sourceBytes)},pin(template),pin(jsonPath),pin(fileURLToPath(import.meta.url)),
  pin(path.join(repo,'tools/fixtures/legion_motion_consumer/extract_main.mjs')),
  pin(path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs')),pin(diagnosticHeaderPath)];
 const sourceManifest={source:inputs[0],declarationsSha256:sha(declarations),
  consumedMembers:members.map(({code,...r})=>r),newlineNormalization:'CRLF to LF only',inputs,
  boundary:'Actual production Toggle, queued Open, Cancel/Admission'+(selection.pending?', full Pump':'')+
   (selection.power?' and PowerEdge.':'; PowerEdge not selected.')+
   ' Explicit in-memory clock/device/slot/GameBar/log ports. Full UI setting consumers, complete Main, real OS/PnP IO, real thread scheduling and long-term resource behavior are not proved.'};
 return {cpp,sourceBytes,sourceManifest,expectedCases:[...selection.cases],jsonPath,diagnosticHeaderPath,runtimeArgs:[...(selection.runtimeArgs??[])]};
}
export function runT1Borrow({repo,source,out,case:caseName}) {
 if(!path.isAbsolute(out)||fs.existsSync(out))throw Error('Fresh absolute output required');
 const prepared=renderT1Borrow({repo,source,case:caseName});
 const {cpp,sourceManifest,expectedCases,jsonPath,diagnosticHeaderPath}=prepared;
 fs.mkdirSync(out,{recursive:true});
 const save=(name,value)=>fs.writeFileSync(path.join(out,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});
 save('source-manifest.json',sourceManifest);
 fs.writeFileSync(path.join(out,'main.cpp.snapshot'),prepared.sourceBytes,{flag:'wx'});
 const profiles=[];
 const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 for(const config of ['Release','Checked']) {
  const dir=path.join(out,config);fs.mkdirSync(dir);
  fs.writeFileSync(path.join(dir,'fixture.cpp'),cpp,{flag:'wx'});fs.copyFileSync(jsonPath,path.join(dir,'json.hpp'));
  fs.copyFileSync(diagnosticHeaderPath,path.join(dir,'input_host_diagnostic_dispatch.h'));
  const flags=config==='Release'?'/MT /O2':'/MTd /Od /RTC1';
  fs.writeFileSync(path.join(dir,'build.cmd'),calibrationToolchainPreamble(vcvars)+
   `cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX ${flags} fixture.cpp /Fofixture.obj /Fefixture.exe\r\nexit /b %errorlevel%\r\n`,{flag:'wx'});
  const execute=(name,exe,args)=>{
   const start=performance.now(),startedAtUtc=new Date().toISOString();
   const r=spawnSync(exe,args,{cwd:dir,windowsHide:true,timeout:60000,maxBuffer:4*1024*1024});
   const stdout=path.join(dir,name+'.stdout.log'),stderr=path.join(dir,name+'.stderr.log');
   fs.writeFileSync(stdout,r.stdout??'',{flag:'wx'});fs.writeFileSync(stderr,r.stderr??'',{flag:'wx'});
   return {name,exe,args,startedAtUtc,endedAtUtc:new Date().toISOString(),elapsedMs:performance.now()-start,
    exit:r.status,signal:r.signal,error:r.error?String(r.error):null,stdout:pin(stdout),stderr:pin(stderr)};
  };
  // Keep the corrected private command recipe: cwd + a plain build.cmd argument.
  const compile=execute('compile','C:\\Windows\\System32\\cmd.exe',['/d','/c','build.cmd']);
  if(compile.exit!==0||compile.error){profiles.push({config,compile,runtime:null,result:null});break;}
  const runtime=execute('runtime',path.join(dir,'fixture.exe'),prepared.runtimeArgs??[]);
  let result=null;try{result=JSON.parse(fs.readFileSync(runtime.stdout.path,'utf8'));}catch{}
  const scopeMatches=result?.case===caseName&&JSON.stringify(result?.cases?.map(x=>x.name))===JSON.stringify(expectedCases);
  profiles.push({config,compile,runtime,result,scopeMatches,binary:pin(path.join(dir,'fixture.exe')),compiledSource:pin(path.join(dir,'fixture.cpp'))});
  if(runtime.error||!scopeMatches||runtime.exit!==0||result?.pass!==true||!result.cases.every(x=>x.pass===true))break;
 }
 const closing=renderT1Borrow({repo,source,case:caseName});
 const consumedStable=closing.cpp===cpp&&sourceManifest.inputs.slice(1).every((x,i)=>x.sha256===closing.sourceManifest.inputs[i+1].sha256);
 const result={status:profiles.length===2&&profiles.every(x=>x.runtime?.exit===0&&x.result?.pass===true&&x.result.cases.every(c=>c.pass===true)&&x.scopeMatches)&&consumedStable?'PASS':'FAIL',
  case:caseName,expectedCases,profiles,sourceManifest:pin(path.join(out,'source-manifest.json')),
  consumedSourceStable:consumedStable,wholeSourceStable:closing.sourceManifest.source.sha256===sourceManifest.source.sha256,
  hardwareIo:false,productMainExecuted:false,wholeNativeBuilt:false,defaultOldCasesExecuted:false};
 save('RESULT.json',result);return result;
}
