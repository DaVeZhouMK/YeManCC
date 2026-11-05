// Actual retired routes, bridge implementations and startup guards; all device/IO calls mocked.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {execFileSync} from 'node:child_process';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const require=createRequire(path.join(root,'package.json'));
const {transformSync,build}=require('esbuild'),ts=require('typescript');
const args=process.argv.slice(2);
assert(args.length===0||((args.length===2||args.length===4)&&args[0]==='--evidence-dir'&&path.isAbsolute(args[1])&&(args.length===2||(args[2]==='--scope'&&['bridge','ccd'].includes(args[3])))),'Use --evidence-dir <absolute path> [--scope bridge|ccd]');
const bridgeOnly=args.length===4,ccdOnly=bridgeOnly&&args[3]==='ccd';
const out=args.length?args[1]:(process.env.YMCC_LEGACY_CPU_RETIRE_OUT||path.resolve(root,'../../../_scratch/legacy-power-pages-retirement-20261017'));
fs.mkdirSync(out,{recursive:true});
const read=file=>fs.readFileSync(path.join(root,file),'utf8');
const normalize=value=>JSON.parse(JSON.stringify(value));
const checks=[];
async function check(name,run){if(ccdOnly&&!/CCD|dormant core|native fixture producer/.test(name))return;await run();checks.push(name);}
function nativeCommandFailure(failure,runLogText,compileLogText){
 if(!failure)return;
 throw new Error('native fixture command failed: '+String(failure?.message||failure)+'\nRUNLOG:\n'+runLogText+'\nCOMPILE:\n'+compileLogText);
}
function extractNative(source,start,end){const s=source.indexOf(start),e=source.indexOf(end,s);assert(s>=0&&e>s,'native extract '+start.slice(0,40));return source.slice(s,e);}
function runNativeCcdFixture(out){
 const source=read('native/main.cpp');
 const blocks=[
  extractNative(source,'static int popcountPtr(ULONG_PTR m) {','static std::string toHexU(ULONG_PTR v) {'),
  extractNative(source,'struct CcdTopology {','static CcdTopology detectCcdTopology() {'),
  extractNative(source,'static bool ccdTopologySupported(const CcdTopology& t) {','static std::atomic<int>       g_ccdMode{0};'),
  extractNative(source,'struct CcdRestoreRecord {','struct CcdSweepResult {'),
  extractNative(source,'static bool ccdSkipProcessValue(DWORD pid','static bool ccdSkipProcess(DWORD pid'),
  extractNative(source,'static bool ccdGenerationMatches(unsigned long long sweepGeneration,','static bool ccdGenerationCurrent(unsigned long long generation) {'),
  extractNative(source,'static bool ccdRestoreAllowed(const CcdRestoreRecord& record,','struct CcdRestoreResult {'),
 ];
 const stamp=new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid+'-'+Math.random().toString(16).slice(2);
 const workDir=path.join(out,'native-pure-'+stamp);
 fs.mkdirSync(workDir,{recursive:true});
 const fixture=path.join(workDir,'ccd-native-pure.cpp');
 const exe=path.join(workDir,'ccd-native-pure.exe');
 const obj=path.join(workDir,'ccd-native-pure.obj');
 const compileLog=path.join(workDir,'ccd-native-compile.log');
 const runLog=path.join(workDir,'ccd-native-run.log');
 const body=[
  '#include <algorithm>','#include <cctype>','#include <cwctype>','#include <iostream>','#include <string>','#include <vector>',
  'using DWORD = unsigned long;','using ULONGLONG = unsigned long long;','using ULONG_PTR = unsigned long long;',
  ...blocks,
  'int main(){int failed=0;auto check=[&](const char* name,bool ok){std::cout<<name<<(ok?"=PASS":"=FAIL")<<"\\n";if(!ok)++failed;};',
  'CcdTopology valid;valid.architectureDetected=true;valid.multiCcdConfirmed=true;valid.logical=24;valid.l3Domains=3;valid.groupRepresentable=true;valid.ccdMasks={"0xff","0xff00","0xff0000"};valid.domainLogical={8,8,8};check("real_three_domains",ccdTopologySupported(valid));',
  'check("uniform_multi_ccd_performance",ccdPerformanceSupported(valid));',
  'CcdTopology ccx=valid;ccx.multiCcdConfirmed=false;check("multi_l3_without_ccd_proof_hidden",!ccdPerformanceSupported(ccx));',
  'check("die_l3_matches_reordered",ccdDieMasksMatchL3({0xff00,0xff},{0xff,0xff00},0xffff));',
  'check("single_die_two_ccx_hidden",!ccdDieMasksMatchL3({0xffff},{0xff,0xff00},0xffff));',
  'check("unknown_die_hidden",!ccdDieMasksMatchL3({},{0xff,0xff00},0xffff));',
  'check("die_mask_mismatch_hidden",!ccdDieMasksMatchL3({0xff,0xff0000},{0xff,0xff00},0xffffff));',
  'check("overlapping_dies_hidden",!ccdDieMasksMatchL3({0xff,0xffff},{0xff,0xffff},0xffff));',
  'check("package_coverage_mismatch_hidden",!ccdDieMasksMatchL3({0xff,0xff00},{0xff,0xff00},0xffffff));',
  'CcdTopology hybrid=valid;hybrid.heterogeneous=true;check("hybrid_hidden",!ccdPerformanceSupported(hybrid));',
  'CcdTopology unknown=valid;unknown.architectureDetected=false;check("unknown_architecture_hidden",!ccdPerformanceSupported(unknown));',
  'CcdTopology single=valid;single.l3Domains=1;single.ccdMasks={"0xff"};single.domainLogical={8};single.logical=8;check("single_domain_hidden",!ccdTopologySupported(single));',
  'CcdTopology empty;empty.logical=32;empty.l3Domains=0;check("empty_masks_unknown",!ccdTopologySupported(empty));',
  'CcdTopology wide;wide.architectureDetected=true;wide.logical=64;wide.l3Domains=2;wide.ccdMasks={"0xffffffff","0xffffffff00000000"};wide.domainLogical={32,32};check("exact_64bit",ccdTopologySupported(wide));',
  'CcdTopology cross=valid;cross.groupRepresentable=false;check("cross_group_rejected",!ccdTopologySupported(cross));',
  'CcdTopology overlap=valid;overlap.ccdMasks={"0xff","0xff","0xff0000"};check("overlap_rejected",!ccdTopologySupported(overlap));',
  'CcdTopology mismatch=valid;mismatch.domainLogical={8,8,7};check("logical_mismatch_rejected",!ccdTopologySupported(mismatch));',
  'std::wstring self=L"yemancc.exe";',
  'check("self_pid_skipped",ccdSkipProcessValue(42,L"yemancc.exe",42,self,999));',
  'check("self_exe_skipped",ccdSkipProcessValue(7,L"YeManCC.exe",42,self,999));',
  'check("game_pid_skipped",ccdSkipProcessValue(999,L"game.exe",42,self,999));',
  'check("system_skipped",ccdSkipProcessValue(4,L"system",42,self,0));',
  'check("normal_target",!ccdSkipProcessValue(7,L"notepad.exe",42,self,999));',
  'check("generation_current",ccdGenerationMatches(5,5,true));',
  'check("generation_stale",!ccdGenerationMatches(4,5,true));',
  'check("generation_inactive",!ccdGenerationMatches(5,5,false));',
  'check("generation_zero",!ccdGenerationMatches(0,0,true));',
  'CcdRestoreRecord rec;rec.pid=7;rec.created=100;rec.originalMask=0xff;rec.lastWrittenMask=0xff00;',
  'check("restore_owned",ccdRestoreAllowed(rec,100,0xff00));',
  'check("restore_pid_reused",!ccdRestoreAllowed(rec,101,0xff00));',
  'check("restore_external_takeover",!ccdRestoreAllowed(rec,100,0xff));',
  'CcdRestoreRecord noOriginal;noOriginal.created=100;noOriginal.lastWrittenMask=0xff00;',
  'check("restore_no_original",!ccdRestoreAllowed(noOriginal,100,0xff00));',
  'std::cout<<"failed="<<failed<<"\\n";return failed==0?0:1;}',
 ].join('\n');
 fs.writeFileSync(fixture,body,'utf8');
 const vcvars='C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat';
 assert(fs.existsSync(vcvars),'MSVC vcvars64 missing');
 const bat=path.join(workDir,'build-ccd-native.cmd');
 const sdk='C:\\Program Files (x86)\\Windows Kits\\10';
 const sdkVersion='10.0.26100.0';
 fs.writeFileSync(bat,[
  '@echo off',
  'cd /d "'+workDir+'"',
  'call "'+vcvars+'" >nul',
  'if errorlevel 1 exit /b 1',
  'cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "'+fixture+'" /I"'+sdk+'\\Include\\'+sdkVersion+'\\ucrt" /I"'+sdk+'\\Include\\'+sdkVersion+'\\um" /I"'+sdk+'\\Include\\'+sdkVersion+'\\shared" /Fo"'+obj+'" /Fe"'+exe+'" /link /LIBPATH:"'+sdk+'\\Lib\\'+sdkVersion+'\\um\\x64" /LIBPATH:"'+sdk+'\\Lib\\'+sdkVersion+'\\ucrt\\x64" > "'+compileLog+'" 2>&1',
  'if errorlevel 1 exit /b 2',
  '"'+exe+'" > "'+runLog+'" 2>&1',
 ].join('\r\n')+'\r\n','utf8');
 let failure=null;
 try{execFileSync('cmd.exe',['/d','/c',bat],{cwd:workDir,timeout:60000,stdio:'pipe'});}catch(error){failure=error;}
 nativeCommandFailure(failure,
  fs.existsSync(runLog)?fs.readFileSync(runLog,'utf8'):'',
  fs.existsSync(compileLog)?fs.readFileSync(compileLog,'utf8'):'');
 if(!fs.existsSync(runLog))throw new Error('native CCD fixture did not run: missing owned run log '+runLog);
 const output=fs.readFileSync(runLog,'utf8');
 const lines=output.split(/\r?\n/).filter(Boolean);
 assert(lines.includes('failed=0'),output);
 const names=lines.filter(line=>line.endsWith('=PASS')).map(line=>line.slice(0,-5));
 const applyIndex=source.indexOf('static CcdSweepResult ccdApplyMode(int mode');
 const applySlice=source.slice(applyIndex,applyIndex+500);
 const ensureIndex=applySlice.indexOf('ccdEnsureSelfIdentity();');
 const snapshotIndex=applySlice.indexOf('ymcc::processInventory::read(');
 assert(ensureIndex>=0&&snapshotIndex>ensureIndex,'first sweep must establish self identity before enumeration');
 assert(/ccdEnsureSelfIdentity\(\);\r?\n    return ccdSkipProcessValue/.test(source));
 assert(!source.includes('masks.empty() && l3Records >= 2'),'guessed topology fallback removed');
 assert(source.includes('std::lock_guard<std::mutex> writeLock(g_ccdWriteMutex);'));
 assert(source.includes('const bool backgroundRefresh = ccdStartWorker();'));
 assert(source.includes('r["backgroundRefresh"] = backgroundRefresh;'));
 const applyToPidOwner=source.slice(source.indexOf('static bool ccdApplyToPid(DWORD pid'),source.indexOf('static CcdSweepResult ccdApplyMode'));
 const generationAnchor=applyToPidOwner.indexOf('ccdGenerationCurrent(generation)');
 const writeAnchor=applyToPidOwner.indexOf('SetProcessAffinityMask(h, mask)');
 assert(generationAnchor>=0&&writeAnchor>=0&&generationAnchor<writeAnchor,'generation/write anchors must exist before ordering');
 assert(source.includes('WaitForSingleObject(thread, 1500)'));
 const shutdown=extractNative(source,'static void ccdShutdown() {','// SMT 开关的真正实现');
 assert(shutdown.includes('if (!thread && !running)'));
 const waitIdx=shutdown.indexOf('if (wait != WAIT_OBJECT_0)');
 assert(waitIdx>=0 && shutdown.indexOf('ccdRestoreOriginals();',waitIdx)>waitIdx);
 assert(shutdown.includes('handle retained'));
 assert(applyToPidOwner.includes('std::lock_guard<std::mutex> ownerLock(g_gameCorePolicyMutex);'));
 assert(applyToPidOwner.includes('result.ownerChanged++;'));
 assert(source.includes('ccdRestoreAllowed(record, created'));
 assert(source.includes('lastWrittenMask'));
 return {status:'PASS',checks:names.length,names,compiled:true,hardwareCalls:0,ownedWorkDir:workDir,ownedObject:obj,ownedRunLog:runLog};
}
function runNativeCcdLifecycleFixture(out){
 const source=read('native/main.cpp');
 const blocks=[
  extractNative(source,'static std::atomic<int>       g_ccdMode{0};','static void ccdEnsureSelfIdentity() {'),
  extractNative(source,'static void ccdEnsureSelfIdentity() {','static void ccdForgetProcess(DWORD pid) {'),
  extractNative(source,'static bool ccdGenerationMatches(unsigned long long sweepGeneration,','static bool ccdGenerationCurrent(unsigned long long generation) {'),
  extractNative(source,'static bool ccdGenerationCurrent(unsigned long long generation) {','static ULONGLONG ccdProcessCreated(HANDLE process) {'),
  extractNative(source,'static ULONGLONG ccdProcessCreated(HANDLE process) {','static bool ccdApplyToPid(DWORD pid,'),
  extractNative(source,'static bool ccdApplyToPid(DWORD pid,','static CcdSweepResult ccdApplyMode(int mode,'),
  extractNative(source,'static bool ccdRestoreAllowed(const CcdRestoreRecord& record,','struct CcdRestoreResult {'),
  extractNative(source,'static bool ccdStartWorker() {','static void ccdShutdown() {'),
  extractNative(source,'static void ccdShutdown() {','// SMT 开关的真正实现'),
 ];
 const stamp=new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid+'-'+Math.random().toString(16).slice(2);
 const workDir=path.join(out,'native-lifecycle-'+stamp);
 fs.mkdirSync(workDir,{recursive:true});
 const fixture=path.join(workDir,'ccd-native-lifecycle.cpp');
 const exe=path.join(workDir,'ccd-native-lifecycle.exe');
 const obj=path.join(workDir,'ccd-native-lifecycle.obj');
 const compileLog=path.join(workDir,'ccd-native-lifecycle-compile.log');
 const runLog=path.join(workDir,'ccd-native-lifecycle-run.log');
 const body=[
  '#include <algorithm>','#include <atomic>','#include <cctype>','#include <cwchar>','#include <cwctype>','#include <filesystem>','#include <iostream>','#include <mutex>','#include <string>','#include <unordered_map>','#include <unordered_set>','#include <vector>',
  'using DWORD = unsigned long;','using ULONGLONG = unsigned long long;','using ULONG_PTR = unsigned long long;','using DWORD_PTR = unsigned long long;','using HANDLE = void*;','using BOOL = int;','using LPVOID = void*;',
  '#define FALSE 0','#define TRUE 1','#define MAX_PATH 260','#define PROCESS_SET_INFORMATION 0x0200','#define PROCESS_QUERY_INFORMATION 0x0400','#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000','#define WAIT_OBJECT_0 0','#define WAIT_TIMEOUT 258',
  'struct FILETIME { DWORD dwLowDateTime=0; DWORD dwHighDateTime=0; };',
  'struct FakeProc { DWORD pid=0; ULONGLONG created=0; DWORD_PTR affinity=0; };',
  'static std::vector<FakeProc> g_fakeProcs;',
  'static int g_fakeWrites=0; static int g_fakeWarn=0; static int g_fakeRestore=0; static bool g_fakeCreateFail=false; static DWORD g_fakeWait=WAIT_OBJECT_0;',
  'static bool g_fakeTakeover=false; static DWORD g_fakeTakeoverPid=0;',
  'static std::atomic<DWORD> g_gameCorePolicyPid{0}; static std::mutex g_gameCorePolicyMutex;',
  'static FakeProc* fakeProc(DWORD pid){for(auto& p:g_fakeProcs)if(p.pid==pid)return &p;return nullptr;}',
  'static DWORD GetCurrentProcessId(){return 42;}',
  'static DWORD GetModuleFileNameW(void*,wchar_t* path,DWORD size){const wchar_t* name=L"yemancc.exe";DWORD n=(DWORD)wcslen(name);if(size<n+1)return 0;wmemcpy(path,name,n+1);return n;}',
  'static HANDLE OpenProcess(DWORD,DWORD,DWORD pid){if(!fakeProc(pid))return nullptr;if(g_fakeTakeover&&pid==g_fakeTakeoverPid)g_gameCorePolicyPid.store(pid);return (HANDLE)(ULONG_PTR)pid;}',
  'static BOOL CloseHandle(HANDLE){return TRUE;}',
  'static BOOL GetProcessTimes(HANDLE h,FILETIME* c,FILETIME*,FILETIME*,FILETIME*){auto* p=fakeProc((DWORD)(ULONG_PTR)h);if(!p)return FALSE;c->dwLowDateTime=(DWORD)p->created;c->dwHighDateTime=(DWORD)(p->created>>32);return TRUE;}',
  'static BOOL GetProcessAffinityMask(HANDLE h,DWORD_PTR* cur,DWORD_PTR* sys){auto* p=fakeProc((DWORD)(ULONG_PTR)h);if(!p)return FALSE;*cur=p->affinity;*sys=0xffff;return TRUE;}',
  'static BOOL SetProcessAffinityMask(HANDLE h,DWORD_PTR mask){auto* p=fakeProc((DWORD)(ULONG_PTR)h);if(!p)return FALSE;p->affinity=mask;++g_fakeWrites;return TRUE;}',
  'static HANDLE CreateThread(void*,size_t,DWORD(*)(LPVOID),void*,DWORD,DWORD*){return g_fakeCreateFail?nullptr:(HANDLE)(ULONG_PTR)1;}',
  'static DWORD WaitForSingleObject(HANDLE,DWORD){return g_fakeWait;}',
  'static void Sleep(DWORD){}',
  'static void writeLog(const std::string&,const std::string&){++g_fakeWarn;}',
  'static ULONGLONG focusFileTimeTicks(FILETIME t){return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}',
  'static DWORD ccdWorker(LPVOID){return 0;}',
  'static void ccdRestoreOriginals(){++g_fakeRestore;}',
  ...blocks,
  'int main(){int failed=0;auto check=[&](const char* name,bool ok){std::cout<<name<<(ok?"=PASS":"=FAIL")<<"\\n";if(!ok)++failed;};',
  'auto reset=[&](){g_fakeProcs={{100,5,0xff00},{200,6,0xff00}};g_fakeWrites=0;g_fakeWarn=0;g_fakeRestore=0;g_fakeCreateFail=false;g_fakeWait=WAIT_OBJECT_0;g_fakeTakeover=false;g_fakeTakeoverPid=0;std::lock_guard<std::mutex> lk(g_ccdMutex);g_ccdOriginal.clear();g_ccdLease={true,7,1};g_ccdThread=nullptr;g_ccdRunning.store(false);g_ccdStop.store(false);g_ccdMode.store(0);g_gameCorePolicyPid.store(0);};',
  'reset();CcdSweepResult a;bool ok=ccdApplyToPid(100,0x00ff,7,a);check("apply_verified",ok&&a.verified==1&&g_fakeWrites==1&&fakeProc(100)->affinity==0x00ff);',
  'reset();g_fakeTakeover=true;g_fakeTakeoverPid=100;fakeProc(100)->affinity=0xff00;CcdSweepResult b;bool okb=ccdApplyToPid(100,0x00ff,7,b);check("game_takeover_zero_write",!okb&&b.ownerChanged==1&&g_fakeWrites==0&&fakeProc(100)->affinity==0xff00);',
  'reset();g_ccdLease.generation=8;CcdSweepResult c;bool okc=ccdApplyToPid(100,0x00ff,7,c);check("stale_generation_zero_write",!okc&&c.staleAborted==1&&g_fakeWrites==0);',
  'reset();g_ccdThread=nullptr;g_ccdRunning.store(false);g_ccdOriginal[100]={100,5,0xff00,0x00ff};ccdShutdown();check("no_worker_quiet_restore",g_fakeRestore==1&&g_fakeWarn==0&&g_ccdThread==nullptr);',
  'reset();g_fakeCreateFail=true;bool started=ccdStartWorker();check("create_thread_fail_reported",!started&&!g_ccdRunning.load()&&g_ccdThread==nullptr);g_ccdOriginal[100]={100,5,0xff00,0x00ff};g_fakeRestore=0;ccdShutdown();check("sync_record_restore_after_create_fail",g_fakeRestore==1);',
  'reset();g_ccdThread=(HANDLE)(ULONG_PTR)1;g_ccdRunning.store(true);g_fakeWait=WAIT_TIMEOUT;g_ccdOriginal[100]={100,5,0xff00,0x00ff};ccdShutdown();check("timeout_keeps_handle_and_skips_restore",g_ccdThread==(HANDLE)(ULONG_PTR)1&&g_fakeRestore==0&&g_fakeWarn>=1);g_fakeWait=WAIT_OBJECT_0;ccdShutdown();check("later_confirmation_restores",g_ccdThread==nullptr&&g_fakeRestore==1);',
  'std::cout<<"failed="<<failed<<"\\n";return failed==0?0:1;}',
 ].join('\n');
 fs.writeFileSync(fixture,body,'utf8');
 const vcvars='C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat';
 const sdk='C:\\Program Files (x86)\\Windows Kits\\10';const sdkVersion='10.0.26100.0';
 const bat=path.join(workDir,'build-ccd-native-lifecycle.cmd');
 fs.writeFileSync(bat,['@echo off','cd /d "'+workDir+'"','call "'+vcvars+'" >nul','if errorlevel 1 exit /b 1','cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "'+fixture+'" /I"'+sdk+'\\Include\\'+sdkVersion+'\\ucrt" /I"'+sdk+'\\Include\\'+sdkVersion+'\\um" /I"'+sdk+'\\Include\\'+sdkVersion+'\\shared" /Fo"'+obj+'" /Fe"'+exe+'" /link /LIBPATH:"'+sdk+'\\Lib\\'+sdkVersion+'\\um\\x64" /LIBPATH:"'+sdk+'\\Lib\\'+sdkVersion+'\\ucrt\\x64" > "'+compileLog+'" 2>&1','if errorlevel 1 exit /b 2','"'+exe+'" > "'+runLog+'" 2>&1'].join('\r\n')+'\r\n','utf8');
 let failure=null;try{execFileSync('cmd.exe',['/d','/c',bat],{cwd:workDir,timeout:60000,stdio:'pipe'});}catch(error){failure=error;}
 nativeCommandFailure(failure,
  fs.existsSync(runLog)?fs.readFileSync(runLog,'utf8'):'',
  fs.existsSync(compileLog)?fs.readFileSync(compileLog,'utf8'):'');
 if(!fs.existsSync(runLog))throw new Error('native lifecycle fixture did not run: missing owned run log '+runLog);
 const output=fs.readFileSync(runLog,'utf8');const lines=output.split(/\r?\n/).filter(Boolean);assert(lines.includes('failed=0'),output);
 const names=lines.filter(line=>line.endsWith('=PASS')).map(line=>line.slice(0,-5));
 return {status:'PASS',checks:names.length,names,compiled:true,hardwareCalls:0,ownedWorkDir:workDir,ownedObject:obj,ownedRunLog:runLog};
}
function evaluate(file,imports){const module={exports:{}};vm.runInNewContext(transformSync(read(file),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,console,require:name=>{assert(name in imports,'unexpected import '+name);return imports[name];}});return module.exports;}
// Compile the actual card once in this original case, then execute its setup and
// render with real Vue refs/computeds/VNodes. Lifecycle/DOM/bridge ports are inert.
// Slider is an import stub: this does not claim its input or viewport acceptance.
let uvCardCompiled=null,uiReceiptConsumer=null;
function undervoltCardConsumer(bridge){
 const vue=require('vue'),sfc=require('@vue/compiler-sfc');
 const filename=path.join(root,'src/components/UndervoltCard.vue');
 if(!uvCardCompiled){
  const parsed=sfc.parse(read('src/components/UndervoltCard.vue'),{filename});assert.deepEqual(parsed.errors,[]);
  const script=sfc.compileScript(parsed.descriptor,{id:'uv-receipt-consumer'});
  const template=sfc.compileTemplate({source:parsed.descriptor.template.content,filename,id:'uv-receipt-consumer',compilerOptions:{bindingMetadata:script.bindings}});assert.deepEqual(template.errors,[]);
  uvCardCompiled={script:transformSync(script.content,{loader:'ts',format:'cjs'}).code,template:transformSync(template.code,{loader:'js',format:'cjs'}).code};
 }
 const hooks={},listeners=new Map();
 const document={activeElement:{},querySelector:()=>null};
 const port={...vue,onMounted:fn=>hooks.mounted=fn,onActivated:fn=>hooks.activated=fn,onDeactivated:fn=>hooks.deactivated=fn,onBeforeUnmount:fn=>hooks.beforeUnmount=fn};
 const imports={'vue':port,'./Slider.vue':{__esModule:true,default:{name:'InertSlider'}},'@/bridge/cpuctl':bridge};
 class InertKeyboardEvent{constructor(key){this.key=key;this.defaultPrevented=false;}preventDefault(){this.defaultPrevented=true;}}
 const globals={document,KeyboardEvent:InertKeyboardEvent,CustomEvent:class{constructor(type,options){this.type=type;this.options=options;}},window:{addEventListener:(name,fn)=>listeners.set(name,fn),removeEventListener:(name,fn)=>{if(listeners.get(name)===fn)listeners.delete(name);}}};
 const load=code=>{const module={exports:{}};vm.runInNewContext(code,{...globals,module,exports:module.exports,console,require:name=>{assert(name in imports,'unexpected card import '+name);return imports[name];}});return module.exports;};
 const component=load(uvCardCompiled.script).default;
 const state=component.setup({}, {expose(){}}),render=load(uvCardCompiled.template).render,proxy=vue.proxyRefs(state);
 const vnode=()=>render({},[],{},proxy,{},{});
 function walk(node,visit){if(Array.isArray(node)){for(const n of node)walk(n,visit);}else if(node&&typeof node==='object'&&node.type!==vue.Comment){visit(node);walk(node.children,visit);}}
 const text=()=>{const parts=[];function readText(n){if(typeof n==='string'||typeof n==='number')parts.push(String(n));else if(Array.isArray(n))n.forEach(readText);else if(n&&typeof n==='object'&&n.type!==vue.Comment)readText(n.children);}readText(vnode());return parts.join(' ');};
 const buttons=()=>{const rows=[];walk(vnode(),node=>{if(node.type==='button')rows.push(node);});return rows;};
 const dependencies=new Set();
 const dependency=file=>{if(dependencies.has(file))return;dependencies.add(file);for(const child of require.cache[file]?.children??[])dependency(child.filename);};
 dependency(require.resolve('vue'));dependency(require.resolve('@vue/compiler-sfc'));
 uiReceiptConsumer={source:'src/components/UndervoltCard.vue',actualSetupAndCompiledTemplate:true,realVueReactivity:true,slider:'IMPORT_STUB_NO_SOURCE_READ',realDomViewportAndGamepadAcceptance:'NOT_RUN',runtimeDependencies:[...new Set([...(uiReceiptConsumer?.runtimeDependencies??[]),require.resolve('vue'),require.resolve('@vue/compiler-sfc'),...dependencies])].sort()};
 return {state,hooks,listeners,text,buttons,back:()=>{const event=new InertKeyboardEvent('Escape');listeners.get('keydown')?.(event);return event;}};
}
const policy=evaluate('src/bridge/legacyCpuFeaturePolicy.ts',{});
await check('retired CCD/UV policy is a fixed inactive flag',()=>assert.equal(policy.LEGACY_CCD_UNDERVOLT_ACTIVE,false));
const manualPolicy=evaluate('src/bridge/cpuCapabilityPolicy.generated.ts',{});
await check('manual CCD and CPU offset enabled while legacy startup stays inactive',()=>{assert.equal(manualPolicy.CCD_MANUAL_CONTROL_ACTIVE,true);assert.equal(manualPolicy.CPU_VOLTAGE_OFFSET_ACTIVE,true);assert.equal(policy.LEGACY_CCD_UNDERVOLT_ACTIVE,false);});
const calls={native:[],shell:[],read:[],write:[]};
const raw={autostart:{ccd:{enabled:true,mode:2},uv:{enabled:true,preset:'risk',vendor:'intel'}}};
const rawBefore=JSON.stringify(raw);
const bridge=evaluate('src/bridge/cpuctl.ts',{
 './cpuCapabilityPolicy.generated':{CCD_MANUAL_CONTROL_ACTIVE:false,CPU_VOLTAGE_OFFSET_ACTIVE:false},
 './ipc':{invoke:async(...args)=>{calls.native.push(args);return {l3Domains:2,physicalCores:16,logical:32,ccdMasks:['ffff','ffff0000']};}},
 './api':{shell:{run:async(...args)=>{calls.shell.push(args);return {exitCode:0,stdout:'{"supported":true,"vendor":"intel","current":-75}',stderr:''};}}},
});
const settings={readSettingsSection:async key=>{calls.read.push(key);return raw;},saveSettingsSection:async(...args)=>{calls.write.push(args);}};
const autostart=evaluate('src/bridge/autostart.ts',{'./legacyCpuFeaturePolicy':policy,'./cpuctl':bridge,'./settingsRepository':settings});
await check('inactive CCD probe makes no topology query',async()=>{assert.equal((await bridge.probeCcd()).supported,false);assert.equal(calls.native.length,0);});
await check('inactive undervolt probe starts no external helper',async()=>{assert.equal((await bridge.probeUndervolt()).reason,'feature_inactive');assert.equal(calls.shell.length,0);});
for(const mode of [0,1,2,-1,NaN])await check('CCD command stays inactive for mode '+mode,async()=>{assert.equal((await bridge.setCcdMode(mode)).ok,false);assert.equal(calls.native.length,0);});
for(const value of [0,-8,-25,-75])await check('undervolt command stays inactive for value '+value,async()=>{assert.equal((await bridge.setUndervolt(value)).ok,false);assert.equal(calls.shell.length,0);});
await check('historic enabled preferences cannot activate either feature',async()=>{const cfg=await autostart.readCpuAutostart();assert.equal(cfg.ccd.enabled,false);assert.equal(cfg.uv.enabled,false);await autostart.applyCpuAutostart();await autostart.applyCpuAutostart();assert.equal(calls.read.length,0);assert.equal(calls.native.length+calls.shell.length,0);});
await check('retired autostart saves do not alter stored preferences',async()=>{await autostart.writeCcdAutostart(true,2);await autostart.writeUvAutostart(true,'risk','intel');assert.equal(calls.write.length,0);assert.equal(JSON.stringify(raw),rawBefore);});
await check('pure legacy preset conversion remains available',()=>{assert.equal(bridge.uvPresetValue('intel','risk'),-75);assert.equal(bridge.uvPresetValue('amd','safe'),-8);assert.equal(bridge.uvPresetValue('amd','unknown'),0);});
// Test-only active import verifies retained implementation without enabling the production flag.
const activePolicy={LEGACY_CCD_UNDERVOLT_ACTIVE:true};
const ccdTopology=(l3Domains,physicalCores,logical,ccdMasks,domains)=>({available:true,supported:true,coreArchitectureDetected:true,heterogeneous:false,performanceSupported:true,multiCcdConfirmed:true,reason:'',logical,physicalCores,l3Domains,ccdMasks,domains,operation:'process_affinity',effectScope:'global_process_affinity_by_l3_domain',mechanism:'SetProcessAffinityMask',physicalCcdPowerOff:false,topologySource:'windows_relation_cache_l3',lease:{owner:'native.ccd',generation:0,mode:0,active:false}});
const ccdReceipt=(overrides={})=>({ok:true,requestId:'ccd-1-123',generation:1,mode:2,operation:'process_affinity',effectScope:'global_process_affinity_by_l3_domain',mechanism:'SetProcessAffinityMask',physicalCcdPowerOff:false,targetMask:'0xffff0000',accepted:true,applicationState:'applied',applied:true,observed:null,verification:'per_process_affinity_readback',affected:{attempted:4,verified:4,openFailed:0,setFailed:0,verifyFailed:0},backgroundRefresh:true,lease:{owner:'native.ccd',generation:1,mode:2,active:true},...overrides});
let topology=ccdTopology(2,16,32,['ffff','ffff0000'],[{index:1,mask:'ffff',logical:16},{index:2,mask:'ffff0000',logical:16}]);
let ccdReply=ccdReceipt();
const intelProbe=(overrides={})=>({supported:true,available:true,vendor:'intel',current:-75,readback:true,current_semantics:'oc_mailbox_per_plane_offset',family:'retained-intel',operation:'static_offset',unit:'mv',offset:{min:-150,max:0,step:1,signed:true},scope:{kind:'intel_fivr_plane',planes:['core','cache']},encoding:'msr_0x150_offset_bits_31_21_signed11_scale1.024',command:'set_vf_direct',readbackCapability:true,identity:{vendor:'intel',family:'retained-intel',cpuid:{family:6,model:186}},planes:{core:-75,cache:-45},...overrides});
const amdProbe=(overrides={})=>({supported:true,available:true,vendor:'amd',current:0,readback:false,current_semantics:'not_readable',family:'StrixHalo',operation:'static_offset',unit:'co_step',offset:{min:-60,max:60,step:1,signed:true},scope:{kind:'amd_all_core',planes:[]},encoding:'u20',command:'set-coall-mp1-0x4C',readbackCapability:false,identity:{vendor:'amd',family:'StrixHalo',familyTag:'apu_ft6_new',cpuid:{family:26,model:112}},planes:{},...overrides});
let probeReply=intelProbe();
let uvExitCode=0,uvStderr='',uvEchoRequest=true;
let uvSetReply={vendor:'intel',ok:true,accepted:true,operation:'static_offset',unit:'mv',requested:{value:-75,scope:'intel_fivr_plane',planes:['core','cache']},applicationState:'unknown',applied:null,readback:false,observed:null,reason:'uxtu_direct_write',verification:'no_readback'};
let runtimeRoot='G:\\YMCC-Test\\PowerControl';
const shellReply=(program,args)=>{calls.shell.push([program,args]);if(args[0]==='uv'&&args[1]==='probe')return {exitCode:0,stdout:JSON.stringify(probeReply),stderr:''};if(args[0]==='uv'&&args[1]==='set')return {exitCode:uvExitCode,stdout:JSON.stringify(uvEchoRequest?{...uvSetReply,requested:{...uvSetReply.requested,value:Number(args[2])}}:uvSetReply),stderr:uvStderr};return {exitCode:0,stdout:'{}',stderr:''};};
const retained=evaluate('src/bridge/cpuctl.ts',{'./cpuCapabilityPolicy.generated':{CCD_MANUAL_CONTROL_ACTIVE:true,CPU_VOLTAGE_OFFSET_ACTIVE:true},'./ipc':{invoke:async(...args)=>{calls.native.push(args);if(args[0]==='app.powerControlDir')return runtimeRoot;if(args[0]==='cpu.setCcdMode')return ccdReply;return topology;}},'./api':{shell:{run:async(program,args)=>shellReply(program,args)}}});
await check('future gated CCD implementation retains validation and wire command',async()=>{assert.equal((await retained.probeCcd()).supported,true);const n=calls.native.length;assert.equal((await retained.setCcdMode(-1)).ok,false);assert.equal(calls.native.length,n);assert.equal((await retained.setCcdMode(2)).ok,true);assert.deepEqual(normalize(calls.native.at(-1)),['cpu.setCcdMode',{mode:2}]);});
await check('future gated UV implementation retains probe and set receipt arguments',async()=>{for(const value of [1,20,60]){const shellCount=calls.shell.length,nativeCount=calls.native.length;const r=await retained.setUndervolt(value);assert.equal(r.ok,false);assert.equal(r.accepted,false);assert.match(r.msg,/仅允许降压/);assert.equal(calls.shell.length,shellCount,'positive offsets cannot start a helper');assert.equal(calls.native.length,nativeCount,'positive offsets cannot dispatch native IPC');}assert.equal((await retained.probeUndervolt()).supported,true);assert.equal((await retained.setUndervolt(-75)).ok,true);assert.deepEqual(normalize(calls.shell.at(-1)[1]),['uv','set','-75','--receipt']);});
await check('CCD accepted pending receipt is not promoted to applied',async()=>{ccdReply=ccdReceipt({applicationState:'pending',applied:false,reason:'no_current_process_targets'});const r=await retained.setCcdMode(2);assert.equal(r.ok,true);assert.equal(r.receipt.applicationState,'pending');assert.equal(r.receipt.applied,false);assert.equal(r.receipt.observed,null);ccdReply=ccdReceipt();});
await check('CCD receipt rejects physical power-off, missing identity, invalid state and not-accepted claims',async()=>{for(const reply of [ccdReceipt({physicalCcdPowerOff:true}),ccdReceipt({requestId:''}),ccdReceipt({operation:'firmware_power_off'}),ccdReceipt({applicationState:'inline',applied:false}),ccdReceipt({accepted:false}),ccdReceipt({generation:0})]){ccdReply=reply;assert.equal((await retained.setCcdMode(2)).ok,false,JSON.stringify(reply));}ccdReply=ccdReceipt();});
await check('CCD runtime native refusal cannot be promoted by otherwise valid library-domain masks',async()=>{
 const saved=topology;const valid=ccdTopology(2,16,32,['ffff','ffff0000'],[{index:1,mask:'ffff',logical:16},{index:2,mask:'ffff0000',logical:16}]);
 try{for(const override of [{supported:false},{available:false},{singleGroupRepresentable:false},{supported:'true'},{available:undefined}]){topology={...valid,...override,reason:'native-unrepresentable'};const p=await retained.probeCcd();assert.equal(p.supported,false);assert.equal(p.available,false);assert.equal(p.reason,'native-unrepresentable');}topology={...valid,singleGroupRepresentable:true};assert.equal((await retained.probeCcd()).supported,true);}finally{topology=saved;}
});
await check('CCD requires confirmed uniform architecture and unique matching domain metadata',async()=>{
 const valid=topology;try{
  for(const override of [{coreArchitectureDetected:false},{coreArchitectureDetected:undefined},{heterogeneous:true},{heterogeneous:undefined},{multiCcdConfirmed:false},{multiCcdConfirmed:undefined},{performanceSupported:false}]){topology={...valid,...override};const p=await retained.probeCcd();assert.equal(p.supported,true,'Settings keeps original L3 capability');assert.equal(p.performanceSupported,false);}
  for(const domains of [[valid.domains[0],valid.domains[0]],[{...valid.domains[0],logical:15},valid.domains[1]]]){topology={...valid,domains};assert.equal((await retained.probeCcd()).supported,false);}
 }finally{topology=valid;}
});
await check('dormant core capability accepts three Windows L3 domains without half splitting',async()=>{topology=ccdTopology(3,12,24,['ff','ff00','ff0000'],[{index:1,mask:'ff',logical:8},{index:2,mask:'ff00',logical:8},{index:3,mask:'ff0000',logical:8}]);const p=await retained.probeCcd();assert.equal(p.supported,true);assert.equal(p.l3Domains,3);assert.deepEqual(normalize(p.ccdMasks),topology.ccdMasks);});
await check('dormant core capability preserves exact 64-bit masks',async()=>{topology=ccdTopology(2,32,64,['ffffffff','ffffffff00000000'],[{index:1,mask:'ffffffff',logical:32},{index:2,mask:'ffffffff00000000',logical:32}]);const p=await retained.probeCcd();assert.equal(p.supported,true);assert.deepEqual(normalize(p.ccdMasks),topology.ccdMasks);});
await check('dormant core capability rejects incomplete zero overlapping or wider-than-64 masks',async()=>{for(const ccdMasks of [['ffff'],['0','ffff'],['ffff','ffff'],['10000000000000000','ffff'],['bad-mask','ffff']]){topology=ccdTopology(2,16,32,ccdMasks,ccdMasks.map((mask,index)=>({index:index+1,mask,logical:16})));assert.equal((await retained.probeCcd()).supported,false,JSON.stringify(ccdMasks));}});
await check('dormant core setter requires matching applied native receipt',async()=>{for(const reply of [{applied:false,mode:2},{ok:false,error:'native rejected'},{ok:false,applied:true,mode:2},{applied:true,mode:1},null]){ccdReply=reply;assert.equal((await retained.setCcdMode(2)).ok,false);}ccdReply=ccdReceipt();});
await check('dormant UV follows native runtime root rather than fixed installation path',async()=>{await retained.probeUndervolt();assert.equal(calls.shell.at(-1)[0],runtimeRoot+'\\pawnio\\YeManTdpCtl.exe');await retained.setUndervolt(-25);assert.equal(calls.shell.at(-1)[0],runtimeRoot+'\\pawnio\\YeManTdpCtl.exe');});
await check('dormant Intel capability preserves family and verified per-plane offsets',async()=>{const p=await retained.probeUndervolt();assert.equal(p.family,'retained-intel');assert.equal(p.readback,true);assert.equal(p.currentVerified,-75);assert.equal(p.currentSemantics,'oc_mailbox_per_plane_offset');assert.deepEqual(normalize(p.planes),{core:-75,cache:-45});});
await check('dormant AMD capability never presents unreadable current as verified zero',async()=>{probeReply=amdProbe({planes:{core:0}});const p=await retained.probeUndervolt();assert.equal(p.supported,true);assert.equal(p.family,'StrixHalo');assert.equal(p.currentVerified,null);assert.equal(p.readback,false);assert.equal(p.currentSemantics,'not_readable');assert.deepEqual(normalize(p.planes),{});});
await check('dormant voltage capability distinguishes AMD co_step from Intel mV',async()=>{probeReply=intelProbe();let p=await retained.probeUndervolt();assert.equal(p.unit,'mv');assert.equal(p.currentVerified,-75);assert.deepEqual(normalize(p.offset),{min:-150,max:0,step:1,signed:true});assert.equal(p.scope.kind,'intel_fivr_plane');probeReply=amdProbe();p=await retained.probeUndervolt();assert.equal(p.unit,'co_step');assert.equal(p.currentVerified,null);assert.deepEqual(normalize(p.offset),{min:-60,max:0,step:1,signed:true});assert.equal(p.scope.kind,'amd_all_core');probeReply=intelProbe({unit:'co_step'});p=await retained.probeUndervolt();assert.equal(p.supported,false);assert.equal(p.currentVerified,null);probeReply=intelProbe({scope:{kind:'amd_all_core',planes:[]}});p=await retained.probeUndervolt();assert.equal(p.supported,false);probeReply=intelProbe();});
await check('dormant UV capability rejects truthy support unknown vendor and nonfinite fake readbacks',async()=>{for(const reply of [intelProbe({supported:'true'}),intelProbe({vendor:'unknown'}),intelProbe({current:null}),intelProbe({current:'0'})]){probeReply=reply;const p=await retained.probeUndervolt();assert.equal(p.currentVerified,null);if(reply.supported!==true||reply.vendor==='unknown')assert.equal(p.supported,false);}probeReply=intelProbe();});
await check('dormant UV set receipt keeps accepted separate from applied',async()=>{const good=await retained.setUndervolt(-75);assert.equal(good.ok,true);assert.equal(good.accepted,true);assert.equal(good.applicationState,'unknown');assert.equal(good.applied,null);assert.equal(good.receipt.readback,false);for(const reply of [{...uvSetReply,applied:true},{...uvSetReply,applicationState:'applied'},{...uvSetReply,accepted:false},{...uvSetReply,unit:'co_step'},{...uvSetReply,readback:true}]){uvSetReply=reply;assert.equal((await retained.setUndervolt(-75)).ok,false,JSON.stringify(reply));}uvSetReply={vendor:'intel',ok:true,accepted:true,operation:'static_offset',unit:'mv',requested:{value:-75,scope:'intel_fivr_plane',planes:['core','cache']},applicationState:'unknown',applied:null,readback:false,observed:null,reason:'uxtu_direct_write',verification:'no_readback'};});
await check('CPU voltage consumer rejects GPU and SA capabilities and receipts',async()=>{for(const plane of ['igpu','sa']){probeReply=intelProbe({scope:{kind:'intel_fivr_plane',planes:[plane]}});assert.equal((await retained.probeUndervolt()).supported,false);const saved=uvSetReply;uvSetReply={...saved,requested:{...saved.requested,planes:[plane]}};assert.equal((await retained.setUndervolt(-75)).ok,false);uvSetReply=saved;}probeReply=intelProbe();});
await check('dormant UV setter rejects noninteger nonfinite requests before native or helper IO',async()=>{for(const value of [NaN,Infinity,-Infinity,0.5]){const n=calls.native.length+sCalls();assert.equal((await retained.setUndervolt(value)).ok,false);assert.equal(calls.native.length+sCalls(),n);}function sCalls(){return calls.shell.length;}});
await check('legacy ROG rc6 is explained without claiming BIOS lock or an applied offset',async()=>{
 const saved=uvSetReply;uvExitCode=6;uvSetReply={...saved,vendor:'amd',unit:'co_step',ok:false,accepted:false,applicationState:'failed',requested:{value:-10,scope:'amd_all_core',planes:[]},reason:'smu_write_failed_rc_6'};
 try{const n=calls.shell.length;const r=await retained.setUndervolt(-10);assert.equal(r.ok,false);assert.equal(r.accepted,false);assert.equal(r.applied,null);assert.match(r.msg,/助手.*6/);assert.match(r.msg,/未.*(?:确认|提供|细分)|无法.*(?:区分|判断)/);assert(!/BIOS.*(?:已锁定|已禁止)/.test(r.msg));assert.equal(calls.shell.length,n+1,'failure never retries the write');}finally{uvSetReply=saved;uvExitCode=0;}
});
await check('each explicit CPU voltage attempt records one bounded terminal observation through exported app log',async()=>{
 const n=calls.native.length;await retained.setUndervolt(-25);const logs=calls.native.slice(n).filter(row=>row[0]==='log.write');assert.equal(logs.length,1);assert.match(logs[0][1].message,/^\[cpu\.voltage-offset\] /);const row=JSON.parse(logs[0][1].message.slice('[cpu.voltage-offset] '.length));assert.equal(row.requested,-25);assert.equal(row.accepted,true);assert.equal(row.applied,null);assert.equal(row.applicationState,'unknown');assert.equal(row.readback,false);assert.equal(row.unit,'mv');assert(!row.stdout&&!row.stderr,'raw output is not copied into app log');
});
await check('AMD UV distinguishes firmware response from local timeout even with the same historical status marker',async()=>{
 const saved=uvSetReply;uvExitCode=6;
 try{for(const [origin,stage,status,state,pattern] of [
  ['firmware_response','response',0xFC,'failed',/固件响应 0xFC/],
  ['firmware_response','response',0xFD,'failed',/固件响应 0xFD/],
  ['ready_wait','mailbox_ready',null,'failed',/未发送.*通道空闲超时/],
  ['response_wait','mailbox_response',null,'unknown',/已发送.*是否已应用未知/],
  ['transport','response_read',null,'unknown',/读取命令响应.*未确认应用/],
  ['gate','acquire',null,'failed',/未发送.*写入锁/],
  ['driver','open',null,'failed',/PawnIO/],
  ['module','load',null,'failed',/RyzenSMU/],
 ]){uvExitCode=origin==='driver'?3:origin==='module'?4:6;const failure={origin,stage,helperExitCode:uvExitCode,mailboxStatus:status};uvSetReply={...saved,vendor:'amd',unit:'co_step',ok:false,accepted:false,applicationState:state,requested:{value:-10,scope:'amd_all_core',planes:[]},reason:'amd_uv_'+origin+'_'+stage,failure};const n=calls.shell.length;const r=await retained.setUndervolt(-10);assert.equal(r.ok,false);assert.equal(r.accepted,false);assert.equal(r.applied,null);assert.equal(r.applicationState,state);assert.match(r.msg,pattern);assert.deepEqual(normalize(r.failure),failure);assert.equal(calls.shell.length,n+1);const log=calls.native.filter(row=>row[0]==='log.write').at(-1);const detail=JSON.parse(log[1].message.slice('[cpu.voltage-offset] '.length));assert.deepEqual(detail.failure,failure);assert.equal(detail.unit,'co_step');assert.equal(detail.readback,false);}}
 finally{uvSetReply=saved;uvExitCode=0;}
});
await check('AMD UV unverified failure never poisons the next explicit successful request',async()=>{
 const saved=uvSetReply;uvExitCode=6;uvSetReply={...saved,vendor:'amd',unit:'co_step',ok:false,accepted:false,applicationState:'unknown',requested:{value:-10,scope:'amd_all_core',planes:[]},reason:'amd_uv_response_wait_mailbox_response',failure:{origin:'response_wait',stage:'mailbox_response',helperExitCode:6,mailboxStatus:null}};
 try{const n=calls.shell.length;assert.equal((await retained.setUndervolt(-10)).accepted,false);uvExitCode=0;uvSetReply={...uvSetReply,ok:true,accepted:true,reason:'smu_command_accepted_no_readback',failure:null};const r=await retained.setUndervolt(-10);assert.equal(r.ok,true);assert.equal(r.accepted,true);assert.equal(r.applied,null);assert.equal(r.applicationState,'unknown');assert.equal(r.receipt.readback,false);assert.equal(calls.shell.length,n+2);}finally{uvSetReply=saved;uvExitCode=0;}
});
await check('CPU voltage receipt and ordinary log retain actual library alternate route without declaring applied',async()=>{
 const saved=uvSetReply;const attempts=[{transport:'mp1',command:0x4C,origin:'firmware_response',mailboxStatus:0xFE,accepted:false},{transport:'rsmu',command:0x5D,origin:'firmware_response',mailboxStatus:1,accepted:true}];const route={transport:'rsmu',command:0x5D};
 uvSetReply={...saved,vendor:'amd',unit:'co_step',requested:{value:-10,scope:'amd_all_core',planes:[]},reason:'smu_command_accepted_no_readback',attempts,route};
 try{const n=calls.shell.length;const result=await retained.setUndervolt(-10);assert.equal(calls.shell.length,n+1,'one helper owns candidate selection');assert.equal(result.accepted,true);assert.equal(result.applied,null);assert.equal(result.applicationState,'unknown');assert.deepEqual(normalize(result.receipt.attempts),attempts);assert.deepEqual(normalize(result.receipt.route),route);const log=calls.native.filter(row=>row[0]==='log.write').at(-1);const row=JSON.parse(log[1].message.slice('[cpu.voltage-offset] '.length));assert.deepEqual(row.attempts,attempts);assert.deepEqual(row.route,route);assert.equal(row.accepted,true);assert.equal(row.readback,false);}finally{uvSetReply=saved;}
});
await check('CPU voltage unknown-command error retains both library candidates and their actual firmware statuses',async()=>{
 const saved=uvSetReply;const attempts=[{transport:'mp1',command:0x4C,origin:'firmware_response',mailboxStatus:0xFE,accepted:false},{transport:'rsmu',command:0x5D,origin:'firmware_response',mailboxStatus:0xFE,accepted:false}];uvExitCode=6;
 uvSetReply={...saved,vendor:'amd',unit:'co_step',ok:false,accepted:false,applicationState:'failed',requested:{value:-10,scope:'amd_all_core',planes:[]},reason:'amd_uv_firmware_response_0xFE',failure:{origin:'firmware_response',stage:'response',helperExitCode:6,mailboxStatus:0xFE},attempts,route:{transport:'rsmu',command:0x5D}};
 try{
  const result=await retained.setUndervolt(-10);assert.equal(result.accepted,false);assert.match(result.msg,/未识别该命令/);assert.deepEqual(normalize(result.attempts),attempts);assert.equal(result.receipt.accepted,false);assert.equal(result.receipt.requested.value,-10);assert.equal(result.receipt.unit,'co_step');assert.deepEqual(normalize(result.receipt.failure),normalize(result.failure));const log=calls.native.filter(row=>row[0]==='log.write').at(-1);assert.deepEqual(JSON.parse(log[1].message.slice('[cpu.voltage-offset] '.length)).attempts,attempts);
  await verifyUndervoltCardConsumer();
 }finally{uvSetReply=saved;uvExitCode=0;}
});
async function verifyUndervoltCardConsumer(){
 const original={reply:uvSetReply,probe:probeReply,exit:uvExitCode,echo:uvEchoRequest};let setOverride=null,probeOverride=null,h;
 const base={vendor:'amd',ok:false,accepted:false,operation:'static_offset',unit:'co_step',requested:{value:-10,scope:'amd_all_core',planes:[]},applicationState:'failed',applied:null,readback:false,observed:null,reason:'amd_uv_firmware_response_0xFF',verification:'no_readback',attempts:[{transport:'mp1',command:0x4C,origin:'firmware_response',mailboxStatus:0xFF,accepted:false}],route:{transport:'mp1',command:0x4C},failure:{origin:'firmware_response',stage:'response',helperExitCode:6,mailboxStatus:0xFF}};
 const deferred=()=>{let resolve;const promise=new Promise(r=>resolve=r);return {promise,resolve};};
 try{
  probeReply=amdProbe();uvSetReply=base;uvExitCode=6;uvEchoRequest=true;
  h=undervoltCardConsumer({probeUndervolt:()=>probeOverride?probeOverride():retained.probeUndervolt(),setUndervolt:(value,target)=>setOverride?setOverride(value,target):retained.setUndervolt(value,target)});
  const opening=calls.shell.length;await h.state.activate();h.hooks.mounted();h.hooks.activated();assert.equal(calls.shell.length,opening+1,'open/activation only probe once');assert.equal(h.buttons().length,2,'receipt display adds no interactive controls');assert.equal(h.state.observed.value,null);assert.match(h.text(),/尚未提交本次请求/);
  h.state.root.value={contains:()=>true,dispatchEvent:()=>true};
  h.state.draft.value=-10;const beforeCancel=calls.shell.length;h.buttons()[1].props.onClick();assert.equal(h.state.draft.value,0);h.state.draft.value=-10;assert.equal(h.back().defaultPrevented,true);assert.equal(h.state.draft.value,0);assert.equal(calls.shell.length,beforeCancel,'button and B/Escape cancel do not submit');
  h.state.draft.value=-10;await h.buttons()[0].props.onClick();assert.equal(h.state.receipt.value.accepted,false);assert.equal(h.state.baseline.value,0);assert.equal(h.state.observed.value,null);assert.match(h.text(),/请求 -10 CO 步进 失败/);assert.match(h.text(),/本次失败回执/);assert.match(h.text(),/MP1 0x4C/);assert.match(h.text(),/固件响应 0xFF/);assert(!h.text().includes('尚无本次应用回执'));assert(!h.state.receiptText.value.includes('已接受'));assert.match(h.state.readbackText.value,/无法回读/);
  uvSetReply={...base,applicationState:'unknown',reason:'amd_uv_response_wait_mailbox_response',failure:{origin:'response_wait',stage:'mailbox_response',helperExitCode:6,mailboxStatus:null},attempts:[{...base.attempts[0],origin:'response_wait',mailboxStatus:null}]};await h.state.apply();assert.match(h.state.receiptText.value,/请求 -10 CO 步进 未确认/);assert.match(h.text(),/未取得固件响应/);assert.equal(h.state.receipt.value.applicationState,'unknown');
  uvSetReply={...base,requested:{...base.requested,value:-11}};uvEchoRequest=false;await h.state.apply();assert.equal(h.state.receipt.value,null);assert.match(h.state.receiptText.value,/未取得与本次请求匹配的有效回执/);assert(!h.text().includes('MP1 0x4C'));uvEchoRequest=true;
  uvSetReply={...base,ok:true,accepted:true,applicationState:'unknown',reason:'smu_command_accepted_no_readback',failure:null,attempts:[base.attempts[0],{transport:'rsmu',command:0x5D,origin:'firmware_response',mailboxStatus:1,accepted:true}],route:{transport:'rsmu',command:0x5D}};uvExitCode=0;await h.state.apply();assert.equal(h.state.baseline.value,-10);assert.equal(h.state.error.value,'');assert.match(h.state.receiptText.value,/已接受.*实际应用尚未确认/);assert.match(h.text(),/最终尝试通道：RSMU 0x5D/);assert.equal(h.state.observed.value,null);
  // A previous page's completion/finally cannot overwrite a new page's pending action.
  const stale=await retained.setUndervolt(-9),fresh=await retained.setUndervolt(-7),oldGate=deferred(),newGate=deferred();
  setOverride=()=>oldGate.promise;h.state.draft.value=-9;const oldTask=h.state.apply();assert.equal(h.state.busy.value,true);h.hooks.deactivated();assert.equal(h.state.submitted.value,null);assert.equal(h.state.receipt.value,null);
  await h.state.activate();setOverride=()=>newGate.promise;h.state.draft.value=-7;const newTask=h.state.apply();h.state.cancel();assert.equal(h.state.draft.value,-7);assert.equal(h.buttons()[1].props.disabled,true);assert.match(h.state.receiptText.value,/正在等待回执/);
  oldGate.resolve(stale);await oldTask;assert.equal(h.state.busy.value,true);assert.equal(h.state.submitted.value.value,-7);assert.equal(h.state.receipt.value,null);
  newGate.resolve(fresh);await newTask;assert.equal(h.state.busy.value,false);assert.equal(h.state.baseline.value,-7);assert.equal(h.state.receipt.value.requested.value,-7);
  // An obsolete read-only probe is subject to the same generation boundary.
  h.hooks.deactivated();const lateProbe=deferred();probeOverride=()=>lateProbe.promise;const oldProbe=h.state.activate();h.hooks.deactivated();probeOverride=null;await h.state.activate();lateProbe.resolve({...await retained.probeUndervolt(),vendor:'intel',currentVerified:-99});await oldProbe;assert.equal(h.state.capability.value.vendor,'amd');assert.equal(h.state.observed.value,null);
  setOverride=()=>Promise.reject(new Error('private-helper-output'));h.state.draft.value=-8;await h.state.apply();assert.equal(h.state.receipt.value,null);assert.match(h.state.receiptText.value,/未取得.*有效回执/);assert(!h.text().includes('private-helper-output'));
  // Intel records are driver write observations, including a partial failure.
  h.hooks.deactivated();setOverride=null;probeReply=intelProbe({current:null,readback:false});await h.state.activate();h.state.draft.value=-75;
  const plane=(written,reason=written?'uxtu_direct_write':'write_failed')=>({ioctl_written:written,firmware_accepted:null,readback:false,readback_mv:null,reason});
  const intel={vendor:'intel',ok:false,accepted:false,operation:'static_offset',unit:'mv',requested:{value:-75,scope:'intel_fivr_plane',planes:['core','cache']},applicationState:'unknown',applied:null,readback:false,observed:null,reason:'uxtu_direct_write_failed',verification:'no_readback',planes:{core:plane(true),cache:plane(false)}};
  uvSetReply=intel;uvExitCode=6;await h.state.apply();assert.equal(h.state.receipt.value.accepted,false);assert.equal(h.state.receipt.value.applicationState,'unknown');assert.equal(h.state.baseline.value,0);assert.equal(h.state.observed.value,null);assert.match(h.text(),/请求 -75 mV 失败/);assert.match(h.text(),/核心请求已写入/);assert.match(h.text(),/缓存写入失败/);assert.match(h.state.error.value,/核心请求已写入.*缓存写入失败.*实际偏移未知/);assert.equal(h.state.receipt.value.planes.core.ioctl_written,true);assert.equal(h.state.receipt.value.planes.core.firmware_accepted,null);assert.equal(h.state.receipt.value.planes.cache.readback_mv,null);
  const partialLog=JSON.parse(calls.native.filter(row=>row[0]==='log.write').at(-1)[1].message.slice('[cpu.voltage-offset] '.length));assert.equal(partialLog.accepted,false);assert.equal(partialLog.applicationState,'unknown');assert.deepEqual(partialLog.planes,intel.planes);
  uvSetReply={...intel,applicationState:'failed',planes:{core:plane(false),cache:plane(false)}};const allFailed=await retained.setUndervolt(-75);assert.equal(allFailed.accepted,false);assert.equal(allFailed.applicationState,'unknown');assert.equal(allFailed.receipt.planes.core.ioctl_written,false);
  uvSetReply={...intel,requested:{...intel.requested,planes:['core']},planes:{core:plane(true)}};const interrupted=await retained.setUndervolt(-75);assert.equal(interrupted.accepted,false);assert.equal(interrupted.applicationState,'unknown');assert.equal(interrupted.receipt.planes.cache,undefined,'unattempted cache has no invented result');
  // No unknown plane or invented readback/firmware acceptance enters trusted data.
  for(const planes of [{core:plane(true)},{...intel.planes,igpu:plane(true)},{...intel.planes,sa:plane(true)},{...intel.planes,mystery:plane(true)},{...intel.planes,core:{...plane(true),readback:true}},{...intel.planes,core:{...plane(true),readback_mv:0}},{...intel.planes,core:{...plane(true),firmware_accepted:true}},{...intel.planes,core:{...plane(true),ioctl_written:'true'}}]){
   uvSetReply={...intel,planes};const r=await retained.setUndervolt(-75);assert.equal(r.ok,false);assert.equal(r.receipt,undefined);assert.equal(r.route,undefined);assert.equal(r.reason,'invalid_receipt');
  }
  uvSetReply={...intel,requested:{...intel.requested,planes:[]},planes:{}};const empty=await retained.setUndervolt(-75);assert.deepEqual(normalize(empty.receipt.planes),{});assert.equal(empty.accepted,false);
  uvSetReply={...intel,ok:true,accepted:true,reason:'uxtu_direct_write',planes:{core:plane(true),cache:plane(true)}};uvExitCode=0;await h.state.apply();assert.equal(h.state.baseline.value,-75);assert.equal(h.state.error.value,'');assert.equal(h.state.receipt.value.accepted,true);assert.match(h.state.receiptText.value,/驱动写请求已提交.*固件是否接受及实际偏移未知/);assert(!h.state.receiptText.value.includes('已接受'));assert.equal(h.state.observed.value,null);
  for(const planes of [{core:plane(true)},{core:plane(true),cache:plane(false)}]){uvSetReply={...uvSetReply,planes};assert.equal((await retained.setUndervolt(-75)).receipt,undefined,'overall success cannot override missing/failed domain');}
 }finally{h?.hooks.beforeUnmount();if(h)assert.equal(h.listeners.size,0);uvSetReply=original.reply;probeReply=original.probe;uvExitCode=original.exit;uvEchoRequest=original.echo;}
}
await check('CPU voltage diagnostic rejects malformed provenance and excludes raw stderr from ordinary log',async()=>{
 const saved=uvSetReply;uvExitCode=6;uvStderr='private-token=never-copy-this';uvSetReply={...saved,ok:false,accepted:false,applicationState:'failed',reason:'smu_write_failed_rc_6',failure:{origin:'firmware_response',stage:'response',helperExitCode:999,mailboxStatus:0xFD}};
 try{
  const r=await retained.setUndervolt(-10);assert.equal(r.failure,undefined);assert.match(r.msg,/旧回执未提供/);const log=calls.native.filter(row=>row[0]==='log.write').at(-1);assert(!log[1].message.includes('never-copy-this'));assert(log[1].message.length<1024);
  const attempt={transport:'mp1',command:0x4C,origin:'firmware_response',mailboxStatus:0xFF,accepted:false};
  const good={vendor:'amd',ok:false,accepted:false,operation:'static_offset',unit:'co_step',requested:{value:-10,scope:'amd_all_core',planes:[]},applicationState:'failed',applied:null,readback:false,observed:null,reason:'amd_uv_firmware_response_0xFF',verification:'no_readback',attempts:[attempt],route:{transport:'mp1',command:0x4C},failure:{origin:'firmware_response',stage:'response',helperExitCode:6,mailboxStatus:0xFF}};
  uvEchoRequest=false;
  for(const bad of [
   {...good,requested:{...good.requested,value:-11}}, {...good,vendor:'other'}, {...good,unit:'mv'},
   {...good,requested:{...good.requested,scope:'intel_fivr_plane'}}, {...good,requested:{...good.requested,planes:['core']}},
   {...good,applied:true}, {...good,observed:0}, {...good,readback:true}, {...good,applicationState:'applied'},
   {...good,route:{transport:'rsmu',command:0x5D}}, {...good,attempts:[]},
   {...good,attempts:[{...attempt,origin:'local_io',mailboxStatus:0xFE}]},
   {...good,attempts:[{...attempt,mailboxStatus:1}]}, {...good,attempts:[{...attempt,accepted:'false'}]},
   {...good,attempts:[{...attempt,accepted:true,mailboxStatus:1},attempt]},
   {...good,failure:{...good.failure,mailboxStatus:0xFD}}, {...good,failure:{...good.failure,helperExitCode:999}},
   {...good,failure:{...good.failure,stage:'not_a_firmware_response'}}, {...good,attempts:Array(9).fill(attempt)}
  ]){
   uvSetReply=bad;const n=calls.shell.length;const result=await retained.setUndervolt(-10);
   assert.equal(calls.shell.length,n+1,'no diagnostic can request another helper');assert.equal(result.ok,false);assert.equal(result.accepted,false);assert.equal(result.applicationState,'unknown');assert.equal(result.applied,null);assert.equal(result.reason,'invalid_receipt');
   assert.equal(result.receipt,undefined);assert.equal(result.failure,undefined);assert.equal(result.route,undefined);assert.equal(result.attempts,undefined);
   const row=JSON.parse(calls.native.filter(row=>row[0]==='log.write').at(-1)[1].message.slice('[cpu.voltage-offset] '.length));assert.deepEqual(row.attempts,[]);assert.equal(row.route,null);assert.equal(row.failure,null);assert.equal(row.vendor,'');
  }
  uvSetReply=good;const wrongContext={vendor:'intel',unit:'mv',scope:{kind:'intel_fivr_plane',planes:['core','cache']}};assert.equal((await retained.setUndervolt(-10,wrongContext)).receipt,undefined);
 }finally{uvSetReply=saved;uvExitCode=0;uvStderr='';uvEchoRequest=true;}
});
await check('dormant UV refuses unavailable runtime root without fixed-path fallback',async()=>{runtimeRoot='';const n=calls.shell.length;assert.equal((await retained.probeUndervolt()).supported,false);assert.equal((await retained.setUndervolt(-25)).ok,false);assert.equal(calls.shell.length,n);});
let nativeFixture=null;
await check('actual native CCD production helpers reject guessed topology, self/game targets, stale generations and foreign restores',()=>{nativeFixture=runNativeCcdFixture(out);});
let nativeLifecycle=null;
await check('actual native CCD lifecycle handles no-worker, create failure, timeout and game takeover with fake Win32',()=>{nativeLifecycle=runNativeCcdLifecycleFixture(out);});
await check('native fixture producer rejects exit/timeout even with PASS logs and uses owned object/run outputs',()=>{
 for(const failure of [new Error('exit 9'),new Error('ETIMEDOUT')]){
  assert.throws(()=>nativeCommandFailure(failure,'',''));
  assert.throws(()=>nativeCommandFailure(failure,'failed=0\nname=PASS',''));
  assert.throws(()=>nativeCommandFailure(failure,'failed=0\nname=PASS','COMPILE_OK'));
 }
 for(const result of [nativeFixture,nativeLifecycle]){
  assert(result.ownedWorkDir.startsWith(path.join(out,'native-')));
  assert(fs.existsSync(result.ownedObject));
  assert(fs.existsSync(result.ownedRunLog));
  assert(path.dirname(result.ownedObject)===result.ownedWorkDir);
 }
});
const nav=evaluate('src/bridge/residentNavigation.ts',{});
for(const mode of [false,true])await check('sidebar and controller policy excludes retired pages in mode '+mode,()=>{const entries=[{path:'/schedule'},{path:'/tdp'},{path:'/cpu'},{path:'/fan'},{path:'/hidden',hidden:true}];assert.deepEqual(Array.from(nav.residentNavigationRoutes(entries,mode),r=>r.path),['/schedule','/fan']);assert.equal(entries.length,5);});
const source=read('src/App.vue').match(/<script setup[^>]*>([\s\S]*?)<\/script>/)?.[1];assert(source);
const app=ts.createSourceFile('App.ts',source,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
let guardedCalls=0;function visit(node,ancestors=[]){if(ts.isCallExpression(node)&&node.expression.getText(app)==='applyCpuAutostart'){assert(ancestors.some(p=>ts.isIfStatement(p)&&p.expression.getText(app).includes('LEGACY_CCD_UNDERVOLT_ACTIVE')),'autostart call lacks inactive guard');guardedCalls++;}ts.forEachChild(node,child=>visit(child,[...ancestors,node]));}visit(app);
await check('both boot and wake legacy activation owners are removed; resident restore and sleep optimization remain',()=>{assert.equal(guardedCalls,2);assert(source.includes('scheduleSleepPowerPlanOptimization();'));assert(!source.includes('applyCpuAutoEnable'));assert(!source.includes('applyAutoTdpIfNeeded'));assert(source.includes('restorePerformanceScheduleIfConfigured'));});
await check('retired source modules are physically absent and App has no direct TDP bypass',()=>{
 assert.equal(fs.existsSync(path.join(root,'src/views/CpuView.vue')),false);
 assert.equal(fs.existsSync(path.join(root,'src/views/TdpView.vue')),false);
 assert.equal(fs.existsSync(path.join(root,'src/bridge/tdpAutoApply.ts')),false);
 assert(!source.includes('readTdp(mode)'));
 assert(!source.includes("setTdp(mode, next"));
});
const loaded=[];
if(!bridgeOnly){
const vueRouter=require('vue-router');
const bundled=await build({stdin:{contents:read('src/router.ts'),loader:'ts',resolveDir:path.join(root,'src')},bundle:true,platform:'node',format:'cjs',write:false,external:['vue-router'],plugins:[{name:'isolated-views',setup(b){b.onResolve({filter:/\.vue$/},args=>({path:args.path,namespace:'views'}));b.onLoad({filter:/.*/,namespace:'views'},args=>({contents:`globalThis.__loadedPages.push(${JSON.stringify(args.path)});export default {name:${JSON.stringify(path.basename(args.path,'.vue'))}};`}));b.onResolve({filter:/^\.\/bridge\/customSteamLibrary$/},()=>({path:'library-contract',namespace:'contract'}));b.onLoad({filter:/.*/,namespace:'contract'},()=>({contents:"export const CUSTOM_STEAM_LIBRARY_ROUTE='/custom-steam-library';export const CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED=true;"}));}}]});
const module={exports:{}};
vm.runInNewContext(bundled.outputFiles[0].text,{module,exports:module.exports,__loadedPages:loaded,console,require:name=>{assert.equal(name,'vue-router');return {...vueRouter,createWebHashHistory:()=>vueRouter.createMemoryHistory()};}});
const routes=module.exports;
await check('retired pages are absent from active route catalog and lazy imports',()=>{assert(!Array.from(routes.ROUTES,r=>r.path).some(p=>p==='/cpu'||p==='/tdp'));assert(!/import\(['"]\.\/views\/(?:CpuView|TdpView)\.vue/.test(read('src/router.ts')));});
for(const old of ['/cpu','/tdp'])await check('old direct URL '+old+' resolves to performance schedule without mounting legacy UI',async()=>{await routes.router.push(old+'?from=bookmark');assert.equal(routes.router.currentRoute.value.path,'/schedule');assert.equal(routes.router.currentRoute.value.query.from,'bookmark');assert.equal(routes.router.currentRoute.value.matched.at(-1).name,'schedule');});
for(const name of ['cpu','tdp'])await check('old named link '+name+' redirects safely',async()=>{await routes.router.push({name});assert.equal(routes.router.currentRoute.value.path,'/schedule');});
await check('startup preloader never loads retired CPU/TDP pages',async()=>{await routes.preloadStartupRoutes();assert(!loaded.some(p=>/\/(?:CpuView|TdpView)\.vue$/.test(p)));assert(loaded.some(p=>p.endsWith('/PerformanceScheduleView.vue')));});
}
const report={status:'PASS',passed:checks.length,routeRuntimeAcceptance:bridgeOnly?'NOT_RUN_BRIDGE_SCOPE':'EXECUTED',selection:ccdOnly?'ccd':bridgeOnly?'bridge':'all',checks,retiredPages:['/cpu','/tdp'],nativeHardwareCalls:0,externalHelpersStarted:0,installedSettingsWritten:0,productionPolicyActive:policy.LEGACY_CCD_UNDERVOLT_ACTIVE,futureBranchTestUsedFakeServices:true,loadedViewModules:loaded,guardedAutostartCalls:guardedCalls,nativeFixture,nativeLifecycle,uiReceiptConsumer,scope:ccdOnly?'Actual CCD bridge and extracted native pure/lifecycle helpers; all hardware/OS write ports mocked; UV/routes/real DOM NOT_RUN':bridgeOnly?'Actual inactive/test-only-active bridge, compiled UndervoltCard setup/template receipt consumers, startup guards and extracted native CCD pure/lifecycle functions; router runtime/real DOM/viewport/gamepad NOT_RUN; all OS/device services mocked':'Actual source modules, compiled UndervoltCard setup/template receipt consumers and Vue Router with memory history; real DOM/viewport/gamepad NOT_RUN; OS, IPC and filesystem mocked; no product/device run'};
fs.writeFileSync(path.join(out,'retired-power-pages-test.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));

console.log('Legacy CPU inactive selftest: PASS');
