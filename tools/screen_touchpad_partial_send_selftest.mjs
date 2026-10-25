// Source-extracted regression runner. Compiles/runs only a memory-only fixture.
// Usage: node tools/screen_touchpad_partial_send_selftest.mjs --out <directory> [--baseline <old-screen_touchpads.h>]
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const toolDir=path.dirname(fileURLToPath(import.meta.url));
const root=path.dirname(toolDir);
const args=process.argv.slice(2),options={};
for(let i=0;i<args.length;i+=2){if(!['--out','--baseline','--source'].includes(args[i])||!args[i+1])throw Error('Expected --out <directory> [--baseline <old-header>] [--source <header>]');options[args[i]]=path.resolve(args[i+1]);}
if(!options['--out'])throw Error('--out is required; generated files never go into the source tree by default.');
const out=options['--out'];fs.mkdirSync(out,{recursive:true});
const sourcePath=options['--source']||path.join(root,'native/screen_touchpads.h');
const source=fs.readFileSync(sourcePath,'utf8');
const sha=s=>crypto.createHash('sha256').update(s).digest('hex');
const pieces=[];
function slice(s,start,end,file=sourcePath){const a=s.indexOf(start);if(a<0)throw Error('Missing source anchor: '+start);const b=s.indexOf(end,a+start.length);if(b<0)throw Error('Missing source end: '+end);const text=s.slice(a,b);pieces.push({file,startLine:s.slice(0,a).split('\n').length,endLine:s.slice(0,b).split('\n').length-1,sha256:sha(text)});return text;}
function line(s,start,file=sourcePath){const a=s.indexOf(start);if(a<0)throw Error('Missing line: '+start);const b=s.indexOf('\n',a);const text=s.slice(a,b);pieces.push({file,startLine:s.slice(0,a).split('\n').length,sha256:sha(text)});return text+'\n';}
let generated='// Generated from source anchors without rewriting function bodies. No Windows SDK/API imports.\nnamespace ymcc::screenpads {\n';
for(const name of ['enum class Mode','enum class SummonPosition','enum class StandaloneSpecialMode','enum class Layout','enum class TouchFeedbackMode','struct Config','inline bool nativePad','inline Mode surfaceMode','struct Action'])generated+=line(source,name);
generated+='inline int touchFeedbackStrength(TouchFeedbackMode){return 0;}\n';
generated+=slice(source,'struct Pad {','// Fixed-size pure state machine');
generated+=slice(source,'class Core {','    struct Projection')+'};\n';
generated+=slice(source,'inline unsigned sentKeys=0;','inline bool initialized=false;');
generated+='inline Core core;inline std::mutex mutex;inline constexpr ULONG_PTR kInjectedTag=0x594d5450;inline HWND windows[2]{};\n';
generated+='struct FeedbackOnly {void cancel(){}};inline FeedbackOnly feedbackOnlyContacts;struct AutoHide {std::array<UINT32,2> specialContacts{};};inline AutoHide autoHide;\n';
generated+='inline void outputHaptic(const Action&){} inline void buttonsReleaseMapped(){} inline void cursorStop(){} inline void syncOutlineFeedback(){} inline void syncContactVisuals(){} inline void nativeInputCancel(){} inline void buttonsRelease(){} inline void autoHideSync(){}\n';
generated+=slice(source,'inline void output(const Action& a) {','// Dual PS surfaces share');
const ownerPath=path.join(root,'native/screen_touchpad_owner.h'),owner=fs.readFileSync(ownerPath,'utf8');
generated+='inline UINT_PTR ownerLifecycleEpoch=1;inline bool ownerRebindQueued=false;\n'+line(owner,'inline void ownerOnSuspend()',ownerPath)+'}\n';
let baselineSha256=null;
if(options['--baseline']){const file=options['--baseline'],old=fs.readFileSync(file,'utf8');baselineSha256=sha(old);generated+='#define PARTIAL_SEND_HAS_BASELINE 1\nnamespace baseline {using ymcc::screenpads::Action;inline unsigned sentKeys=0;inline bool sentClick=false,dryRun=false;inline unsigned long long injectedCount=0;inline DWORD inputError=0;inline constexpr ULONG_PTR kInjectedTag=0x594d5450;using ymcc::screenpads::outputHaptic;\n'+slice(old,'inline void output(const Action& a) {','inline void releaseMappedState() {',file)+'}\n';}
const include=path.join(out,'screen_touchpad_partial_send_extracted.inc');fs.writeFileSync(include,generated);
const fixture=path.join(toolDir,'screen_touchpad_partial_send_selftest.cpp');
const fixtureText=fs.readFileSync(fixture,'utf8');
if(/#include\s*[<"]windows\.h|LoadLibrary|GetProcAddress/.test(fixtureText+'\n'+generated))throw Error('Memory fixture must not include/load Windows input APIs.');
function run(exe,argv,log,extra={}){const r=spawnSync(exe,argv,{cwd:out,encoding:'utf8',windowsHide:true,...extra});if(log)fs.writeFileSync(path.join(out,log),(r.stdout||'')+(r.stderr||''));if(r.error||r.status!==0)throw Error(`${path.basename(exe)} failed (${r.status}): ${r.error?.message||r.stderr||r.stdout}`);return r.stdout;}
let imports='',compiler='';
const executable=path.join(out,process.platform==='win32'?'screen_touchpad_partial_send_selftest.exe':'screen_touchpad_partial_send_selftest');
if(process.platform==='win32'){
 const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');
 const vs=run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath']).trim();
 if(!vs)throw Error('MSVC C++ tools not found.');
 const vcvars=path.join(vs,'VC/Auxiliary/Build/vcvars64.bat');
 for(const value of [out,fixture,vcvars])if(/[\r\n"%!]/.test(value))throw Error('Unsupported shell metacharacter in build path.');
 const script=path.join(out,'compile-memory-fixture.cmd');
 fs.writeFileSync(script,[
 '@echo off','setlocal',`set "TEMP=${out}"`,`set "TMP=${out}"`,
 `call "${vcvars}" > "${out}\\compiler-environment.log" 2>&1`,'if errorlevel 1 exit /b 1',
 `cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /I"${out}" "${fixture}" /Fe:"${executable}" /Fo:"${out}\\screen_touchpad_partial_send_selftest.obj" /Fd:"${out}\\screen_touchpad_partial_send_selftest.pdb" /link /INCREMENTAL:NO > "${out}\\compile.log" 2>&1`,
 'if errorlevel 1 exit /b 1',`dumpbin /imports "${executable}" > "${out}\\binary-imports.txt"`,'if errorlevel 1 exit /b 1','exit /b 0',''].join('\r\n'));
 run(process.env.ComSpec||'cmd.exe',['/d','/c',script],'compile-driver.log');
 imports=fs.readFileSync(path.join(out,'binary-imports.txt'),'utf8');
 if(/user32\.dll|\bSendInput\b|\bGetAsyncKeyState\b|\bGetForegroundWindow\b/i.test(imports))throw Error('Input API import found; refusing execution.');
 compiler='MSVC /std:c++17 /W4 /WX /O2 /MT';
}else{
 compiler=process.env.CXX||'c++';run(compiler,['-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',out,fixture,'-o',executable],'compile.log');
}
const stdout=run(executable,[],'results.txt');
const match=stdout.match(/SUMMARY (\{[^\n]+\})/);if(!match)throw Error('Missing structured result.');const summary=JSON.parse(match[1]);if(summary.failed)throw Error('Regression failed.');
const result={...summary,completedAt:new Date().toISOString(),sourcePath,sourceSha256:sha(source),baselineSha256,sourceUnchangedDuringRun:sha(fs.readFileSync(sourcePath,'utf8'))===sha(source),fixtureSha256:sha(fixtureText),runnerSha256:sha(fs.readFileSync(fileURLToPath(import.meta.url))),extractedSha256:sha(generated),executableSha256:sha(fs.readFileSync(executable)),compiler,safety:{windowsInputHeaders:false,user32Imported:false,explicitInputApiResolution:false,ranProduct:false,actualOSInput:false},observer:'const INPUT*, UINT requested, UINT inserted, DWORD error, bool simulated; noexcept',ownershipContract:'count-based serial prefix, not consumer receipt',extractions:pieces};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));
console.log(JSON.stringify(result,null,2));
