// Explicit, offline native compact ACK semantics. No default IDs or performance run.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {performance} from 'node:perf_hooks';
import {calibrationToolchainPreamble} from '../hc_reviewed_calibration/toolchain.mjs';

const invocationStart=performance.now(), startedAt=new Date().toISOString();
const here=path.dirname(fileURLToPath(import.meta.url)), repo=path.resolve(here,'../../..');
const workspace=path.resolve(repo,'../../..');
const allowed=path.join(workspace,'Testing/YMCC/Evidence/VirtualGamepadSteadyState-20261103/CompactFrameReply');
const argv=process.argv.slice(2),options={};
for(let i=0;i<argv.length;i+=2){assert(['--out','--case-group','--budget-ledger'].includes(argv[i])&&!Object.hasOwn(options,argv[i])&&argv[i+1],'Unknown/duplicate/missing option');options[argv[i]]=argv[i+1];}
assert(options['--out']&&path.isAbsolute(options['--out']),'Explicit --out ABS_FRESH_DIR required');
const caseGroup=options['--case-group']||'all';assert(['all','compact-regressions','request-budget','request-budget-negative'].includes(caseGroup),'Unknown case group');
let budgetLedger=null,priorChargedMs=0;
if(options['--budget-ledger']){assert(path.isAbsolute(options['--budget-ledger']));budgetLedger=JSON.parse(fs.readFileSync(options['--budget-ledger'],'utf8'));assert(budgetLedger.budgetMs===120000&&Array.isArray(budgetLedger.charges));for(const x of budgetLedger.charges){assert(Number.isFinite(x.elapsedMs)&&x.elapsedMs>=0&&path.isAbsolute(x.receipt));const bytes=fs.readFileSync(x.receipt);assert(crypto.createHash('sha256').update(bytes).digest('hex')===x.sha256,'Budget receipt changed');priorChargedMs+=x.elapsedMs;}assert(priorChargedMs<120000,'Original budget exhausted');}
const started=invocationStart-priorChargedMs,deadline=started+120000;
const out=path.resolve(options['--out']);
const inside=(base,p)=>{const rel=path.relative(base,p);return rel!==''&&!rel.startsWith('..')&&!path.isAbsolute(rel);};
assert(inside(allowed,out),'Output must be a fresh child of '+allowed);
assert(!/["%\r\n]/.test(out),'Unsupported shell path');
fs.mkdirSync(allowed,{recursive:true});
assert(inside(fs.realpathSync.native(allowed),path.join(fs.realpathSync.native(path.dirname(out)),path.basename(out))),'Output parent must exist within the evidence root');
fs.mkdirSync(out); // Exclusive claim: an existing directory is never reused.
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const hash=f=>sha(fs.readFileSync(f));
const save=(name,value)=>fs.writeFileSync(path.join(out,name),JSON.stringify(value,null,2)+'\n');
const remaining=()=>{const n=Math.floor(deadline-performance.now());assert(n>0,'120 second total budget exhausted');return n;};
const result={schemaVersion:1,status:'INCOMPLETE',startedAt,budgetMs:120000,out,configurations:[],performanceMeasured:false,productStarted:false,sdkOrDeviceStarted:false,newDefaultIds:0,caseGroup,priorChargedMs};
const originals=new Map(), generated=[], fragments=[];
function freeze(relative){
 const original=path.join(repo,relative),bytes=fs.readFileSync(original),dest=path.join(out,'inputs',relative);
 fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,bytes,{flag:'wx'});
 const row={relative,original,snapshot:dest,bytes:bytes.length,sha256:sha(bytes)};originals.set(original,row);return bytes.toString('utf8');
}
function put(relative,text){const dest=path.join(out,relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,text,{flag:'wx'});generated.push({path:dest,sha256:hash(dest)});return dest;}
function unique(s,needle,from=0){const n=s.indexOf(needle,from);assert(n>=0,'Missing source anchor: '+needle);assert(s.indexOf(needle,n+needle.length)<0,'Ambiguous source anchor: '+needle);return n;}
if(options['--budget-ledger']){const original=path.resolve(options['--budget-ledger']);assert(inside(allowed,original),'Budget ledger must be in this evidence family');const bytes=fs.readFileSync(original);put('inputs/budget-ledger.json',bytes);originals.set(original,{original,sha256:sha(bytes)});}
let main,diagnosticSource;
function fragment(label,start,end,source=main,sourceRelative='native/main.cpp'){assert(start>=0&&end>start);const text=source.slice(start,end);fragments.push({label,sourceRelative,start,end,line:1+source.slice(0,start).split('\n').length-1,bytes:Buffer.byteLength(text),sha256:sha(text)});return text;}
function between(label,a,b){const start=unique(main,a),end=main.indexOf(b,start+a.length);assert(end>start,'Missing end '+b);return fragment(label,start,end);}
function declaration(name,isStruct=false,source=main,sourceRelative='native/main.cpp'){
 const re=isStruct?new RegExp('^struct '+name+' \\{','m'):new RegExp('^(?:static )?[^\\n;{}]*\\b'+name+'\\([^;]*?\\)\\s*(?:noexcept\\s*)?\\{','m');
 const m=re.exec(source);assert(m,'Missing definition '+name);const begin=m.index,open=source.indexOf('{',begin);let depth=0,state='code';
 for(let i=open;i<source.length;i++){const c=source[i],n=source[i+1];
  if(state==='line'){if(c==='\n')state='code';continue;}if(state==='block'){if(c==='*'&&n==='/'){state='code';i++;}continue;}
  if(state==='"'||state==="'"){if(c==='\\'){i++;continue;}if(c===state)state='code';continue;}
  if(c==='/'&&n==='/'){state='line';i++;continue;}if(c==='/'&&n==='*'){state='block';i++;continue;}if(c==='"'||c==="'"){state=c;continue;}
  if(c==='{')depth++;if(c==='}'&&--depth===0)return fragment(name,begin,i+1+(isStruct?1:0),source,sourceRelative);
 }
 throw Error('Unclosed '+name);
}
// Each stage is created suspended and assigned to its own kill-on-close Win32
// Job before execution. Outer timeout closes the owner, cancelling descendants.
const jobSource=String.raw`
using System;using System.IO;using System.Diagnostics;using System.Runtime.InteropServices;using System.Threading;
public class CompactOwnedStage {
 [StructLayout(LayoutKind.Sequential)] struct SI {public uint cb;public IntPtr reserved,desktop,title;public uint x,y,w,h,cx,cy,fill,flags;public ushort show,cb2;public IntPtr reserved2,stdin,stdout,stderr;}
 [StructLayout(LayoutKind.Sequential)] struct PI {public IntPtr process,thread;public uint pid,tid;}
 [StructLayout(LayoutKind.Sequential)] struct SA {public int length;public IntPtr descriptor;[MarshalAs(UnmanagedType.Bool)] public bool inherit;}
 [StructLayout(LayoutKind.Sequential)] struct Basic {public long user, kernel;public uint limit;public UIntPtr min,max;public uint active;public UIntPtr affinity;public uint priority,scheduling;}
 [StructLayout(LayoutKind.Sequential)] struct Io {public ulong a,b,c,d,e,f;}
 [StructLayout(LayoutKind.Sequential)] struct Extended {public Basic basic;public Io io;public UIntPtr p,j,pp,pj;}
 [StructLayout(LayoutKind.Sequential)] struct Accounting {public long a,b,c,d;public uint faults,total,active,terminated;}
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateJobObject(IntPtr p,string n);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetInformationJobObject(IntPtr j,int c,ref Extended i,uint n);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr j,int c,out Accounting i,uint n,IntPtr r);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr j,IntPtr p);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool TerminateJobObject(IntPtr j,uint code);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool CreateProcess(string app,string args,IntPtr p,IntPtr t,bool inherit,uint flags,IntPtr env,string cwd,ref SI s,out PI pi);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFile(string f,uint access,uint share,ref SA sa,uint create,uint flags,IntPtr template);
 [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr t);
 [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h,uint ms);
 [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr h,out uint code);
 [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr h,uint code);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 public static int Run(string exe,string args,string cwd,string stdout,string stderr,string receipt,int timeout) {
  var timer=Stopwatch.StartNew();IntPtr job=IntPtr.Zero,o=IntPtr.Zero,e=IntPtr.Zero;PI pi=new PI();uint code=0xffffffff;bool timed=false,forced=false;uint active=0;string error="";
  try {
   job=CreateJobObject(IntPtr.Zero,null);if(job==IntPtr.Zero)throw new Exception("CreateJobObject");
   var limits=new Extended();limits.basic.limit=0x2000;if(!SetInformationJobObject(job,9,ref limits,(uint)Marshal.SizeOf(typeof(Extended))))throw new Exception("SetInformationJobObject");
   var sa=new SA();sa.length=Marshal.SizeOf(typeof(SA));sa.inherit=true;
   o=CreateFile(stdout,0x40000000,3,ref sa,2,0,IntPtr.Zero);e=CreateFile(stderr,0x40000000,3,ref sa,2,0,IntPtr.Zero);
   if(o==new IntPtr(-1)||e==new IntPtr(-1))throw new Exception("log handles");
   var si=new SI();si.cb=(uint)Marshal.SizeOf(typeof(SI));si.flags=0x100;si.stdout=o;si.stderr=e;
   if(!CreateProcess(exe,"\""+exe+"\" "+args,IntPtr.Zero,IntPtr.Zero,true,0x08000004,IntPtr.Zero,cwd,ref si,out pi))throw new Exception("CreateProcess:"+Marshal.GetLastWin32Error());
   if(!AssignProcessToJobObject(job,pi.process)){TerminateProcess(pi.process,126);throw new Exception("AssignProcessToJobObject");}
   if(ResumeThread(pi.thread)==0xffffffff)throw new Exception("ResumeThread");
   timed=WaitForSingleObject(pi.process,(uint)Math.Max(1,timeout-timer.ElapsedMilliseconds))!=0;
   if(!timed)GetExitCodeProcess(pi.process,out code);
   Accounting accounting;if(!QueryInformationJobObject(job,1,out accounting,(uint)Marshal.SizeOf(typeof(Accounting)),IntPtr.Zero))throw new Exception("QueryInformationJobObject");active=accounting.active;
   if(active!=0){forced=true;TerminateJobObject(job,124);while(timer.ElapsedMilliseconds<timeout){if(!QueryInformationJobObject(job,1,out accounting,(uint)Marshal.SizeOf(typeof(Accounting)),IntPtr.Zero))break;active=accounting.active;if(active==0)break;Thread.Sleep(1);}}
  }catch(Exception x){error=x.Message.Replace("\\","/").Replace("\"","'");if(job!=IntPtr.Zero)TerminateJobObject(job,125);}
  finally {if(pi.thread!=IntPtr.Zero)CloseHandle(pi.thread);if(pi.process!=IntPtr.Zero)CloseHandle(pi.process);if(o!=IntPtr.Zero&&o!=new IntPtr(-1))CloseHandle(o);if(e!=IntPtr.Zero&&e!=new IntPtr(-1))CloseHandle(e);if(job!=IntPtr.Zero)CloseHandle(job);}
  File.WriteAllText(receipt,"{\"rootPid\":"+pi.pid+",\"exitCode\":"+code+",\"timedOut\":"+timed.ToString().ToLowerInvariant()+",\"forcedJobCleanup\":"+forced.ToString().ToLowerInvariant()+",\"activeAfterWait\":"+active+",\"elapsedMs\":"+timer.ElapsedMilliseconds+",\"error\":\""+error+"\"}");
  return error!=""?125:timed?124:active!=0?123:unchecked((int)code);
 }
}
`;
function runStage(name,exe,args,cwd,allowSemanticFailure=false){
 remaining();const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});const stdout=path.join(dir,'stdout.log'),stderr=path.join(dir,'stderr.log'),receipt=path.join(dir,'process.json');
 const quote=s=>"'"+s.replaceAll("'","''")+"'";
 const script=put(name+'/owned-stage.ps1',"$ErrorActionPreference='Stop'\nAdd-Type -TypeDefinition @'\n"+jobSource+"\n'@\nexit [CompactOwnedStage]::Run("+[exe,args,cwd,stdout,stderr,receipt].map(quote).join(',')+','+Math.max(1,remaining()-2500)+')\n');
 const stageStarted=performance.now(),timeout=remaining();
 const p=spawnSync(path.join(process.env.SystemRoot||'C:/Windows','System32/WindowsPowerShell/v1.0/powershell.exe'),['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',script],{cwd,windowsHide:true,encoding:'utf8',timeout,maxBuffer:2*1024*1024});
 fs.writeFileSync(path.join(dir,'supervisor.stdout.log'),p.stdout||'');fs.writeFileSync(path.join(dir,'supervisor.stderr.log'),p.stderr||'');
 const row={name,command:{exe,args,cwd},effectiveTimeoutMs:timeout,elapsedMs:performance.now()-stageStarted,exitCode:p.status,signal:p.signal,spawnError:p.error?{code:p.error.code,message:p.error.message}:null,stdout,stderr};
 if(fs.existsSync(receipt))row.owned=JSON.parse(fs.readFileSync(receipt,'utf8'));result.configurations.push(row);save('result.json',result);
 assert(!p.error&&!p.signal&&row.owned&&row.owned.error===''&&!row.owned.timedOut&&row.owned.activeAfterWait===0&&(allowSemanticFailure?(p.status===0||p.status===1):(p.status===0&&row.owned.exitCode===0)),'Stage failed: '+name+' (raw preserved)');return row;
}
const directedPlan={"compact":1,"compact-bad-type-value":60,"compact-empty-binding":1,"compact-float-identity":2,"compact-stale-binding":1,"compact-unbound":1,"duplicate-compact-key":10,"duplicate-compact-key-conflict":10,"duplicate-motor-DOM-invalid-sample":6,"dynamic-motor-revision-and-stop":16,"full-old-type-contract":132,"full-original-tuple-missing":14,"full-original-tuple-null":14,"invalid-expected-token":6,"invalid-prepare-old-ACK":5,"invalid-token":6,"missing-compact-key":10,"mixed-any-legacy":14,"mixed-full-compact":1,"mixed-full-marker-only":1,"mixed-full-token-only":1,"motor-range-invalid":7,"new-generation-ACK":1,"numeric-overflow":12,"old-full":1,"old-full-with-binding":1,"old-generation-ACK":1,"partial-motor-invalid-sample":6,"receipt-values":6,"revoked-token-ACK":6,"typed-motor-invalid-sample":30,"unbound-host-compact":1,"unbound-host-full":1,"version-overflow":6,"wrong-request-context":1,"wrong-sequence-context":1,"directed-full-terminal-nul":1,"directed-compact-terminal-nul":1,"directed-compact-nul-suffix":1,"directed-extension-fallback":1,"directed-extension-stale-token":1,"directed-exact-16KiB":1,"directed-over-16KiB":1,"directed-full-default-values":1,"directed-valid-unicode":1,"directed-invalid-unicode":1,"directed-invalid-utf8":1,"private-pipe-full":1,"private-pipe-compact":1,"private-pipe-backpressure":1,"private-pipe-truncated":1,"private-pipe-stale-token":1};
const requestBudgetPlan={"request-budget-normal-no-diagnostic":1,"request-budget-diagnostic-small":1,"request-budget-diagnostic-exact-limit":1,"request-budget-diagnostic-one-byte-short":1,"request-budget-diagnostic-full-limit":1,"request-budget-diagnostic-over-limit":1,"request-budget-utf8-exact-limit":1,"request-budget-non-frame":1,"request-budget-unbound":1,"request-budget-invalid-token":1,"request-budget-empty":1,"request-budget-missing-terminal-brace":1,"request-budget-negative-old-order":1,"request-budget-no-diagnostic-exact-limit":1};
try {
  const sourceFiles=['native/main.cpp','native/msi_virtual_report_diagnostic_runtime.h','native/input_host_compact_frame_reply.h','native/game_feedback_receipt.h','deps/json/json.hpp','tools/virtual_ack_parser_contract_selftest.cpp','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/fixtures/inputhost_compact_reply/native.mjs','tools/fixtures/inputhost_compact_reply/native_cases.cpp'];
 for(const f of sourceFiles){remaining();const text=freeze(f);if(f==='native/main.cpp')main=text;if(f==='native/msi_virtual_report_diagnostic_runtime.h')diagnosticSource=text;}
 const nativeSnapshot=path.join(out,'inputs/native/main.cpp').replaceAll('\\','/');
 const declareCode=['InputHostGameMotorDomKeys','InputHostFastReceipt'].map(x=>declaration(x,true)).join('\n')+'\n'+['inputHostGameMotorFromDom','inputHostExpandCompactFrameReplyLocked'].map(x=>declaration(x)).join('\n');
 const scan=between('actual c7aReceiptFast through six-argument wrapper','namespace c7aReceiptFast {','void verifyAgainstDom(')+'\n} // namespace c7aReceiptFast\n';
 assert(scan.includes('scanSubmitFrameReceiptBound(')&&scan.includes('scanSubmitFrameReceipt('));
 assert(scan.includes('{"compactFrameReplyVersion",24,')&&scan.includes('{"frameReplyBinding",17,'),'Actual protocol key widths');
 put('generated/extracted.hpp',declareCode+'\n'+scan);
 const tuple=between('request DOM compact claim + original tuple and instance contract','        const bool frameCommand = std::strcmp(command, "SUBMIT_FRAME") == 0;','        if (!tupleMatches || !identityOk) {');
 const pnp=between('original target PnP predicate','        const bool requiresTargetPnp =','        if (!response.value("ok", false)');
 const success=main.match(/if \(!response\.value\("ok", false\) \|\| response\.value\("status", ""\) != expectedStatus \|\| !targetPnpAdmitted\)/g);assert(success?.length===1);
 const first=main.match(/domFirstFrame = (response\.value\("firstFrame", false\));/),elapsed=main.match(/domSubmitUs = (response\.value\("submitElapsedUs", 0ULL\));/);assert(first&&elapsed);
 put('generated/dom_contract.hpp','static DomResult actualDom(const std::string& wire,const std::string* requestedReplyBinding,const char* command="SUBMIT_FRAME",const char* expectedStatus="frame-accepted",const std::string& requestIdValue="r",unsigned long long sequenceValue=5){\nDomResult out;InputHostGameMotorDomKeys gameMotorDomKeys;gameMotorDomKeys.compact.observeWire(wire);\njson response=json::parse(wire,[&](int d,json::parse_event_t e,json& v){return gameMotorDomKeys(d,e,v);},false);\nif(response.is_discarded()||!response.is_object())return out;try{\n'+tuple+pnp+success[0]+'return out;\nif(!tupleMatches||!identityOk)return out;\nout.firstFrame='+first[1]+';out.submitElapsedUs='+elapsed[1]+';out.hostInstanceId=hostInstanceId;out.gameMotor=inputHostGameMotorFromDom(response,gameMotorDomKeys);out.accepted=true;out.expanded=compactExpanded;out.response=std::move(response);\n}catch(const std::exception& e){out.exception=e.what();}return out;}\n');
  const diagnosticGlueAnchor='#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC\r\n    inputHostMsiReportDiagnosticExtendRequest(command, requestText);\r\n#endif';
  const attach=between('actual diagnostic glue followed by one-send request binding attachment',diagnosticGlueAnchor,'    if (std::strcmp(command, "SUBMIT_FRAME") != 0) recoverySnapNoteCommand(command);');
  const diagnosticEnd=attach.indexOf('#endif')+'#endif'.length;
  assert(diagnosticEnd>0&&attach.indexOf('const std::string* requestedReplyBinding')>diagnosticEnd,'Diagnostic producer must precede final binding budget check');
  const oldOrderAttach=attach.slice(diagnosticEnd)+'\n'+attach.slice(0,diagnosticEnd)+'\n';
  const appendDiagnostic=declaration('inputHostMsiReportDiagnosticAppendOptional',false,diagnosticSource,'native/msi_virtual_report_diagnostic_runtime.h');
 const revokeLine=main.split(/\r?\n/).filter(x=>x.includes('compactReply::revokesBinding(command)'));assert(revokeLine.length===1);
 const publish=between('token publication after original accepted prepare tuple/Host/PnP','        const auto binding = response.find("frameReplyBinding");','    if (responseOut) *responseOut = std::move(response);');
 assert(publish.trimEnd().endsWith('}'));
  const diagnosticPort=String.raw`static bool fixtureDiagnosticEnabled=false,fixtureDiagnosticAppend=false;
static unsigned fixtureDiagnosticCalls=0;
static json fixtureDiagnosticMetadata={{"sourceFrameSequence",17},{"sourceGeneration",4},{"note","诊断"}};
static void inputHostMsiReportDiagnosticExtendRequest(const char* command,std::string& text){
 ++fixtureDiagnosticCalls;
 if(fixtureDiagnosticEnabled&&std::strcmp(command,"SUBMIT_FRAME")==0)
  fixtureDiagnosticAppend=inputHostMsiReportDiagnosticAppendOptional(text,"msiReportChainSource",fixtureDiagnosticMetadata);
}
`;
  put('generated/request_contract.hpp',appendDiagnostic+'\n'+diagnosticPort+'\n#define YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC\nstatic void actualRevoke(const char* command){\n'+revokeLine[0]+'\n}\nstatic bool actualAttach(const char* command,std::string& requestText){\n'+attach+'\nreturn requestedReplyBinding!=nullptr;}\nstatic bool oldOrderAttachNegativeControl(const char* command,std::string& requestText){\n'+oldOrderAttach+'\nreturn requestedReplyBinding!=nullptr;}\n#undef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC\nstatic void actualPublish(const json& response,const InputHostGameMotorDomKeys& gameMotorDomKeys){\n'+publish);
 save('source-bindings.json',{method:'verbatim current source slices; environment globals are inert fixed tuple inputs',sources:[...originals.values()],fragments,excluded:'verifyAgainstDom diagnostic, actual diagnostic metadata producer/lifecycle, product process/SDK/device, performance timing',diagnosticPort:'inert controlled metadata; actual diagnostic AppendOptional and source call ordering compiled',negativeControl:'exact current attachment block moved before exact diagnostic glue to reproduce pre-fix order',domAdapter:'source tuple, identity, PnP and success predicates; ordinary exception-to-rejection adapter',frozenSource:nativeSnapshot,offsetUnit:'JavaScript UTF-16 code units; exact fragment UTF-8 SHA256 also recorded'});
 const vcvars=process.env.YMCC_VCVARS64||'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 originals.set(vcvars,{original:vcvars,sha256:hash(vcvars)});
 const frozenCases=path.join(out,'inputs/tools/fixtures/inputhost_compact_reply/native_cases.cpp');
 const includesByPath=new Map();let semanticFailures=0;
 for(const mode of ['Release','Checked']){
  const dir=path.join(out,mode);fs.mkdirSync(dir);const compile=path.join(dir,'compile');fs.mkdirSync(compile);
  const flags=mode==='Release'?'/O2 /MT /DNDEBUG':'/Od /MTd /RTC1 /D_DEBUG';
  const rsp=put(mode+'/compile/compile.rsp','/nologo /std:c++20 /utf-8 /EHsc /W4 /permissive- /showIncludes '+flags+' /I"'+path.join(out,'generated')+'" /I"'+path.join(out,'inputs/native')+'" /I"'+path.join(out,'inputs/deps/json')+'" "'+frozenCases+'" /Fo"'+path.join(dir,'native_cases.obj')+'" /Fe"'+path.join(dir,'native_cases.exe')+'" /link /verbose:lib advapi32.lib\n');
  const batch=put(mode+'/compile/compile.cmd',calibrationToolchainPreamble(vcvars)+'chcp 65001 >nul\r\nset "CL="\r\nset "_CL_="\r\nset "LINK="\r\nset "_LINK_="\r\nset "VSCMD_SKIP_SENDTELEMETRY=1"\r\nset "VSLANG=1033"\r\nwhere cl.exe > selected-tools.txt\r\nwhere link.exe >> selected-tools.txt\r\ncl.exe @"'+rsp+'"\r\nexit /b %errorlevel%\r\n');
  const c=runStage(mode+'/compile',path.join(process.env.SystemRoot||'C:/Windows','System32/cmd.exe'),'/d /c ""'+batch+'""',compile);
  const decode=f=>{const b=fs.readFileSync(f);const utf8=b.toString('utf8');return utf8.includes('\ufffd')?new TextDecoder('gb18030').decode(b):utf8;};const text=decode(c.stdout)+'\n'+decode(c.stderr);
  const observed=[...new Set([...text.matchAll(/^\s*(?:Note: including file:|注意:\s*包含文件:)\s*(.+?)\r?$/gm)].map(m=>path.resolve(m[1])))];assert(observed.length>20,'Actual compiler include trace absent');
  for(const required of ['input_host_compact_frame_reply.h','game_feedback_receipt.h','json.hpp','extracted.hpp','dom_contract.hpp','request_contract.hpp'])assert(observed.some(f=>path.basename(f)===required),'Actual include missing '+required);
  const libraries=[...new Set([...text.matchAll(/[A-Z]:\\[^\r\n]*?\.lib\b/gi)].map(m=>path.resolve(m[0])))].filter(f=>fs.existsSync(f));
  const selected=fs.readFileSync(path.join(compile,'selected-tools.txt'),'utf8').split(/\r?\n/).filter(Boolean).filter(f=>fs.existsSync(f));assert(selected.length>=2,'Missing actual compiler/linker paths');
  const closure=[...new Set([...observed,...libraries,...selected,process.execPath,path.join(process.env.SystemRoot||'C:/Windows','System32/cmd.exe'),path.join(process.env.SystemRoot||'C:/Windows','System32/WindowsPowerShell/v1.0/powershell.exe'),...['c1xx.dll','c2.dll','mspdbcore.dll'].map(n=>path.join(path.dirname(selected.find(f=>path.basename(f).toLowerCase()==='cl.exe')),n)).filter(f=>fs.existsSync(f))])].map(f=>({path:f,bytes:fs.statSync(f).size,sha256:hash(f)}));
  for(const x of closure){const prev=includesByPath.get(x.path);assert(!prev||prev.sha256===x.sha256,'Compiled input drift '+x.path);includesByPath.set(x.path,x);}
  save(mode+'/include-trace.json',{rawStdoutSha256:hash(c.stdout),rawStderrSha256:hash(c.stderr),includes:observed,libraries,selectedTools:selected,closure});
  remaining();const test=runStage(mode+'/run',path.join(dir,'native_cases.exe'),'"'+path.join(dir,'run')+'"'+(caseGroup==='all'?'':' --case-group '+caseGroup),dir,true);
  const summary=JSON.parse(fs.readFileSync(path.join(dir,'run/summary.json'),'utf8'));test.raw=summary;
  const rows=fs.readFileSync(path.join(dir,'run/cases.tsv'),'utf8').trimEnd().split('\n').slice(1),counts={};
  for(let i=0;i<rows.length;i++){const cells=rows[i].split('\t');assert(Number(cells[0])===i+1,'Raw case identity/order');counts[cells[1]]=(counts[cells[1]]||0)+1;}
  const canonical=x=>JSON.stringify(Object.entries(x).sort((a,b)=>a[0].localeCompare(b[0])));
  assert(rows.length===summary.cases&&canonical(counts)===canonical(summary.groups),'Raw case count/group mismatch');
  if(caseGroup!=='all')assert(canonical(counts)===canonical(caseGroup==='request-budget-negative'?{'request-budget-negative-old-order':1}:caseGroup==='request-budget'?requestBudgetPlan:directedPlan),'Directed selected set mismatch; unselected groups must not execute');
  test.rawAudit={rows:rows.length,groups:counts,exactSelectedSet:caseGroup!=='all',casesSha256:hash(path.join(dir,'run/cases.tsv')),failuresSha256:hash(path.join(dir,'run/failures.jsonl')),summarySha256:hash(path.join(dir,'run/summary.json')),pipeSha256:hash(path.join(dir,'run/pipe.jsonl')),executableSha256:hash(path.join(dir,'native_cases.exe'))};
  semanticFailures+=summary.failures+(summary.complete?0:1);save('result.json',result);
 }
 for(const x of includesByPath.values()){remaining();assert(hash(x.path)===x.sha256,'Include/tool/library changed: '+x.path);}
 result.compiledClosure=[...includesByPath.values()];result.status=semanticFailures?'FAIL':'PASS';result.semanticFailures=semanticFailures;
}catch(e){result.status=performance.now()>=deadline?'TIMEOUT':'FAIL';result.error=e.stack||String(e);}
finally {
 result.sourceStability=[...originals.values()].map(x=>({original:x.original,before:x.sha256,after:fs.existsSync(x.original)?hash(x.original):null}));
 if(result.sourceStability.some(x=>x.before!==x.after)){result.status='FAIL';result.sourceDrift=true;}
 result.generated=generated;result.actualInvocationElapsedMs=performance.now()-invocationStart;result.elapsedMs=performance.now()-started;result.finishedAt=new Date().toISOString();
 result.bindingSha256=sha(JSON.stringify({sources:[...originals.values()],generated,closure:result.compiledClosure||[],stability:result.sourceStability}));
 save('result.json',result);console.log(JSON.stringify({status:result.status,out,elapsedMs:result.elapsedMs,error:result.error||null}));
 process.exitCode=result.status==='PASS'?0:1;
}
