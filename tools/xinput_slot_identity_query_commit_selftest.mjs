// Source-exact offline regression for the slot path-query commit boundary.
// Only inert path API/clock/feedback/Host counters are linked. Production slices
// are inserted unchanged; --baseline must be an actual captured old main.cpp.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const workspace=path.resolve(repo,'../../..');
const opts={current:path.join(repo,'native/main.cpp'),baseline:null,out:path.join(workspace,'Build/Validation/MSI-LateReturnEvents-R112-20261016/common-fix/query-commit'),expectRegression:false};
for(let i=2;i<process.argv.length;i++){
 const arg=process.argv[i];
 if(arg==='--help'){console.log('node tools/xinput_slot_identity_query_commit_selftest.mjs [--current MAIN_CPP] [--baseline OLD_MAIN_CPP] [--out DIR] [--expect-regression]');process.exit(0);}
 if(arg==='--expect-regression'){opts.expectRegression=true;continue;}
 if(['--current','--baseline','--out'].includes(arg)){assert(process.argv[i+1]&&!process.argv[i+1].startsWith('--'),arg+' requires a path');opts[arg.slice(2)]=path.resolve(process.argv[++i]);}else throw Error('Unknown argument '+arg);
}
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function load(file){const bytes=fs.readFileSync(file);return {file,bytes,text:bytes.toString('utf8'),sha256:sha(bytes)};}
function extractSource(s){
 const text=s.text,masked=text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,m=>m.replace(/[^\r\n]/g,' ')),ranges=[];
 function endBrace(open){let n=0;for(let i=open;i<masked.length;i++){if(masked[i]==='{')n++;if(masked[i]==='}'&&--n===0)return i+1}throw Error('Unclosed brace in '+s.file)}
 function record(name,a,b,kind='function'){assert(a>=0&&b>a,'Invalid range '+name);const code=text.slice(a,b);const r={name,kind,source:s.file,source_sha256:s.sha256,start_line:text.slice(0,a).split('\n').length,end_line:text.slice(0,b).split('\n').length,start_utf8_byte:Buffer.byteLength(text.slice(0,a)),end_utf8_byte_exclusive:Buffer.byteLength(text.slice(0,b)),sha256:sha(code),code};ranges.push(r);return code;}
 function fn(name){const rx=new RegExp('^static[^\\r\\n]*\\b'+name+'\\s*\\(','gm');for(const m of text.matchAll(rx)){let i=masked.indexOf('(',m.index),n=0;for(;i<masked.length;i++){if(masked[i]==='(')n++;else if(masked[i]===')'&&--n===0){i++;break}}while(/\s/.test(masked[i]))i++;if(masked.slice(i,i+8)==='noexcept'){i+=8;while(/\s/.test(masked[i]))i++;}if(masked[i]==='{')return record(name,m.index,endBrace(i));}throw Error('Missing function '+name);}
 function type(name){const m=new RegExp('^(?:struct|enum class) '+name+'\\b','m').exec(text);assert(m,'Missing type '+name);return record(name,m.index,endBrace(masked.indexOf('{',m.index))+1,'type')}
 function fragment(name,start,end,from){const a=text.indexOf(start,from),b=text.indexOf(end,a);assert(a>=0&&b>a,'Missing block '+name);return record(name,a,b,'block')}
 function constant(name){const m=new RegExp('^static constexpr [^\\r\\n;]*\\b'+name+'\\s*=[^;]+;','m').exec(text);assert(m,'Missing constant '+name);return record(name,m.index,m.index+m[0].length,'constant')}
 const r=text.indexOf('static void gamepadReadStateUnlocked(');assert(r>=0);
 const a=text.indexOf('            const DWORD slotBit = (1u << i);',r);assert(a>=0);const probe=masked.indexOf('if (edgeProbe || reprobeRequested || unknownBefore',a);assert(probe>a);
 const classification=record('successful-slot identity refresh',a,text.includes('xinputFeedbackIdentityProbe')?text.indexOf('            YmccXInputStateSecret secret{};',probe):endBrace(masked.indexOf('{',probe)),'block');
 const selA=text.indexOf('    bool multipleActive = false;',r),selIf=masked.indexOf('if (admittedMask != 0)',selA);assert(selA>=0&&selIf>selA);
 const selection=record('single-source selection',selA,endBrace(masked.indexOf('{',selIf)),'block');
 const msiA=text.indexOf('    if (admittedMask == 0 && !externalPriority && msiDinputMainSourceEligible())',r);assert(msiA>=0);
 const msi=record('MSI DInput fallback',msiA,endBrace(masked.indexOf('{',msiA)),'block');
 const call=text.indexOf('        hostFrame = inputHostSubmitPad(',text.indexOf('static void inputCaptureOnGamepad('));assert(call>=0);
 const arg=fragment('live-or-neutral call argument','            live || submitNeutral,','\n',call).trim().replace(/,$/,'');
 const methodNames=['xinputNonVirtualConnectedMask','xinputPhysicalConnectedMask','xinputSlotIdentityVerdict','xinputSlotsMarkDiscoveryDirty','xinputSlotsRequestIdentityReprobe','hidHideUpper','hidHideIsVirtualIdentity','inputCanonicalizeStickAxis','inputCanonicalizeGamepad','msiControllerNeutralCleanupAllowed','inputHostPhysicalPublicationSourceAdmitted'];
 const policyStart=text.indexOf('static std::array<ymcc::input::identityRefresh::Slot,4> g_xinputFeedbackIdentity;');
 const policy=policyStart<0?'':record('identity policy globals',policyStart,text.indexOf('// Sampling owns g_gamepadReadMx.',policyStart),'declarations')+fn('xinputFeedbackIdentityProbe');
 const parts={IDENTITY_POLICY:policy,CONSTANTS:['kXInputSlotReprobeBackoffMs','kInputStickCenterDeadzone','kXInputPermissiveAnyConnectedInput'].map(constant).join('\n')+'\nstatic constexpr int XINPUT_GAMEPAD_TRIGGER_THRESHOLD=30;',IDENTITY_ENUM:type('XInputSlotIdentity'),METHODS:methodNames.map(fn).join('\n'),READ_PREFIX:fragment('read discovery flag consumption','    const auto diagnosticReadPower=currentPowerGeneration();','    // 发现轮次',r),CLASSIFICATION:classification,ADMISSION:fragment('physical source admission','    const DWORD physicalMask = xinputPhysicalConnectedMask();','    // R10-R2 M3',r),SELECTION:selection,MSI_FALLBACK:msi,LIVE_OR_NEUTRAL:arg,FEEDBACK_TARGET:type('PhysicalFeedbackTarget'),FEEDBACK_METHODS:['physicalFeedbackSlotCurrent','physicalFeedbackStopTarget','physicalFeedbackFinish','nativePhysicalFeedbackReady'].map(fn).join('\n'),FEEDBACK_TICKET:fn('nativeRunTouchHapticTicket')};
 return {parts,ranges,hasRefresh:/if \(edgeProbe \|\| reprobeRequested \|\| unknownBefore \|\| dirtyDiscovery \|\| boundaryFullScan\)/.test(classification)};
}
fs.mkdirSync(opts.out,{recursive:true});
const run=path.join(opts.out,'runs',new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid);fs.mkdirSync(run,{recursive:true});
const writeJson=(name,value)=>fs.writeFileSync(path.join(run,name),JSON.stringify(value,null,2)+'\n');
const sourceInputs={current:load(opts.current)};if(opts.baseline)sourceInputs.baseline=load(opts.baseline);
const extracted=Object.fromEntries(Object.entries(sourceInputs).map(([name,s])=>[name,extractSource(s)]));
const template=load(path.join(repo,'tools/fixtures/xinput_slot_identity_query_commit_fixture.cpp'));
const deps=['deps/json/json.hpp','native/touch_haptic_dispatch.h','native/xinput_identity_refresh_policy.h'].map(f=>load(path.join(repo,f)));
for(const name of ["HcControllerFeedbackCapabilityCatalog.h","HcControllerFeedbackCapabilityCatalog.generated.h"])deps.push(load(path.join(repo,'capabilities',name)));
const inc=path.join(run,'extracted');fs.mkdirSync(inc,{recursive:true});for(const d of deps)fs.writeFileSync(path.join(inc,path.basename(d.file)),d.bytes);
for(const [variant,x]of Object.entries(extracted))for(const r of x.ranges)fs.writeFileSync(path.join(inc,variant+'-'+r.name.replace(/[^a-zA-Z0-9_-]/g,'_')+'.txt'),r.code);
writeJson('source-ranges.json',{sources:Object.fromEntries(Object.entries(sourceInputs).map(([name,s])=>[name,{file:s.file,sha256:s.sha256,ranges:extracted[name].ranges.map(({code,...r})=>r)}])),fixture:{file:template.file,sha256:template.sha256},dependencies:deps.map(({file,sha256})=>({file,sha256})),sourceSubstitutions:[],scope:'Exact identity classification, feedback observation, source admission/selection and publication predicate; real path API/clock/Host/feedback writes are inert. Not the full main.cpp, Win32 transport, Host or consumer.'});
const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';assert(fs.existsSync(vcvars),'Set YMCC_VCVARS64 to vcvars64.bat');
const q=s=>{assert(!/["\r\n]/.test(s));return '"'+s+'"';};
function command(label,exe,args,cwd,expected){const result=spawnSync(exe,args,{cwd,encoding:'utf8',timeout:60000});fs.writeFileSync(path.join(cwd,label+'.stdout.log'),result.stdout??'');fs.writeFileSync(path.join(cwd,label+'.stderr.log'),result.stderr??'');writeJson(label+'-'+path.basename(cwd)+'.command.json',{exe,args,cwd,expected,status:result.status,error:result.error?String(result.error):null});assert(!result.error&&result.status===expected,label+' unexpected status '+result.status+'; see '+cwd);return result.stdout;}
const results={};
for(const [variant,x]of Object.entries(extracted))for(const diagnostic of [false,true]){
 const label=variant+(diagnostic?'-diagnostic-on':'-diagnostic-off'),dir=path.join(run,label);fs.mkdirSync(dir,{recursive:true});
 let code='#include "HcControllerFeedbackCapabilityCatalog.h"\n'+template.text;for(const [name,part]of Object.entries(x.parts)){const marker='@@'+name+'@@';assert(code.includes(marker),'Missing fixture marker '+name);assert(code.split(marker).length===2,'Duplicate marker '+name);code=code.replace(marker,()=>part);}assert(!/@@\w+@@/.test(code));fs.writeFileSync(path.join(dir,'fixture.cpp'),code);
 const batch=path.join(dir,'compile.cmd');fs.writeFileSync(batch,'@echo off\r\ncall '+q(vcvars)+' >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 /WX /O2 /DFIXTURE_EXPECT_REFRESH=1'+(diagnostic?' /DYMCC_MSI_REPORT_CHAIN_DIAGNOSTIC':'')+' '+q(path.join(dir,'fixture.cpp'))+' /I'+q(inc)+' /Fo'+q(path.join(dir,'fixture.obj'))+' /Fe'+q(path.join(dir,'fixture.exe'))+'\r\nexit /b %errorlevel%\r\n');
 command('build','cmd.exe',['/d','/c',batch],dir,0);
 const expectRegression=variant==='baseline'||opts.expectRegression;
 const result=JSON.parse(command('execute',path.join(dir,'fixture.exe'),[],dir,expectRegression?1:0));
 const failed=result.cases.filter(c=>!c.passed);
 assert(result.failedCases===(expectRegression?12:0),'Unexpected failure count '+label);
 assert(!expectRegression||failed.every(c=>c.oldRegressionWitness),'Non-regression case failed '+label);
 assert(result.blockedQueries===18,'Missing blocked query coverage '+label);
 results[label]={...result,expectedRegression:expectRegression};writeJson(label+'.results.json',result);
}
const scopeStability={};for(const [variant,s]of Object.entries(sourceInputs)){const end=load(s.file),x=extractSource(end),changed=extracted[variant].ranges.filter(r=>!x.ranges.some(v=>v.name===r.name&&v.sha256===r.sha256));scopeStability[variant]={fullFileChanged:end.sha256!==s.sha256,changedRelatedNames:changed.map(x=>x.name),relatedSlicesStable:changed.length===0};assert(changed.length===0,'Related source changed during regression: '+changed.map(r=>r.name));}
const summary={passed:true,run,profiles:Object.fromEntries(Object.entries(results).map(([label,r])=>[label,{cases:r.caseCount,checks:r.checks,failedCases:r.failedCases,blockedQueries:r.blockedQueries,expectedRegression:r.expectedRegression}])),scopeStability,sourceExact:true,sourceCodeRewrittenByHarness:false,productionModifiedByHarness:false,productionHostOrDeviceExecuted:false,claim:'Only the path-query-to-identity-mask commit boundary is validated. No full Host submission concurrency, field root cause, device feature or external consumer acceptance.'};
writeJson('summary.json',summary);fs.writeFileSync(path.join(opts.out,'summary.json'),JSON.stringify(summary,null,2)+'\n');console.log(JSON.stringify(summary,null,2));
