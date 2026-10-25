import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
// Source-exact health settlement regression; never launches Host/SDK/device IO.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const workspace=path.resolve(repo,'../../..');
const opts={current:path.join(repo,'native/main.cpp'),baseline:null,out:path.join(workspace,'Build/Validation/MSI-LateReturnEvents-R112-20261016/common-health-fix/regression'),expectRegression:false,expectReaderGap:false,expectSemanticFailure:false,baselineHeader:null};
for(let i=2;i<process.argv.length;i++){
 const a=process.argv[i];if(a==='--help'){console.log('node tools/input_host_recovery_health_selftest.mjs [--current MAIN_CPP] [--baseline OLD_MAIN_CPP] [--out DIR] [--expect-regression] [--expect-reader-gap] [--expect-semantic-failure] [--baseline-header HEADER]');process.exit(0);}
 if(a==='--expect-semantic-failure'){opts.expectSemanticFailure=true;continue;}
 if(a==='--expect-reader-gap'){opts.expectReaderGap=true;continue;}
 if(a==='--expect-regression'){opts.expectRegression=true;continue;}
 assert(['--current','--baseline','--out','--baseline-header'].includes(a),'Unknown '+a);assert(process.argv[i+1]&&!process.argv[i+1].startsWith('--'),a+' requires path');opts[a==='--baseline-header'?'baselineHeader':a.slice(2)]=path.resolve(process.argv[++i]);
}
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');const load=file=>{const bytes=fs.readFileSync(file);return {file,bytes,text:bytes.toString('utf8'),sha256:sha(bytes)};};
function extract(s){
 const text=s.text,mask=text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,m=>m.replace(/[^\r\n]/g,' ')),ranges=[];
 function end(a){let n=0;for(let i=a;i<mask.length;i++){if(mask[i]==='{')n++;if(mask[i]==='}'&&--n===0)return i+1;}throw Error('Unclosed brace');}
 function record(name,a,b){assert(a>=0&&b>a,name);const code=text.slice(a,b);ranges.push({name,file:s.file,startLine:text.slice(0,a).split('\n').length,endLine:text.slice(0,b).split('\n').length,sha256:sha(code),code});return code;}
 function fn(name){for(const m of text.matchAll(new RegExp('^static[^\\r\\n]*\\b'+name+'\\s*\\(','gm'))){let i=mask.indexOf('(',m.index),n=0;for(;i<mask.length;i++){if(mask[i]==='(')n++;if(mask[i]===')'&&--n===0){i++;break;}}while(/\s/.test(mask[i]))i++;if(mask.slice(i,i+8)==='noexcept'){i+=8;while(/\s/.test(mask[i]))i++;}if(mask[i]==='{')return record(name,m.index,end(i));}throw Error('Missing '+name);}
 const pub=text.indexOf('static json inputHostSubmitPad('),a=text.indexOf('    bool suppressPublication = false;',pub),b=text.indexOf('    if (suppressPublication) {',a);assert(a>pub&&b>a);
 const h=text.indexOf('                // GP-926R13 B2',pub),hb=text.indexOf('\n            } else',h);assert(h>pub&&hb>h);
 const fixed=text.includes('static ymcc::input::recoveryHealth::Window g_inputHostRecoveryHealth;');
 const adapterNames=[...(text.includes('static bool inputHostRecoveryHealthTrackingLocked()')?['inputHostRecoveryHealthTrackingLocked']:[]),'inputHostRecoveryHealthContextLocked','inputHostRecoveryHealthSource','inputHostRecoveryHealthLatestSourceLocked','inputHostRecoveryHealthObserveLocked','inputHostRecoveryHealthAcceptedLocked'];
 const helper=fixed?adapterNames.map(fn).join('\n'):'';
 const rc=text.indexOf('            const auto recoveryHealthRequestContext =',pub);let request='';if(rc>=0){const re=text.indexOf('            accepted = frameTextReady',rc);assert(re>rc);request=record('health request context/read capture',rc,re);}
 let producerPrevious='',producerCommit='';if(text.includes('const auto previousHealthSource =')){const pa=text.indexOf('        const auto previousHealthSource =',text.indexOf('static void gamepadReadStateUnlocked('));producerPrevious=record('canonical producer previous source',pa,text.indexOf('\n',pa)+1);const pc=text.indexOf('        g_inputCaptureGamepadSnapshot.readContinuityGeneration =',pa),pe=text.indexOf('        statusSourceWindow =',pc);producerCommit=record('canonical producer continuity commit',pc,pe);}
 let frameContext='',frameLocal='';
 if(text.includes('struct InputFrameSubmission {')){
  const ca=text.indexOf('struct InputFrameSubmission {'),cb=text.indexOf('static std::atomic<unsigned long long> g_commonFrameEvidenceRows',ca);
  frameContext=record('independent submission context ABI only',ca,cb)+`
static ymcc::input::frameSubmission::Current inputHostFrameSubmissionCurrent(bool,bool=false){return {};}
static void inputHostFrameSubmissionNote(ymcc::input::frameSubmission::Verdict,const char*,bool){}
`;
  frameContext+=fn('inputHostWindowsMotionSubmissionCurrent');
  frameLocal='InputFrameSubmission frameSubmission;';
 }
 const parts={FRAME_CONTEXT:frameContext,FRAME_LOCAL:frameLocal,PRODUCER_PREVIOUS:producerPrevious,PRODUCER_COMMIT:producerCommit,HELPERS:helper,SOURCE_GATE:fn('inputHostPhysicalPublicationSourceAdmitted'),SUPPRESS:record('source suppression classification lock',a,b),ACK:record('accepted health branch',h,hb),REQUEST:request,MAINTENANCE:fn('inputHostRecoveryMaintenanceTick')};
 return {parts,ranges,fixed};
}
fs.mkdirSync(opts.out,{recursive:true});const run=path.join(opts.out,'runs',new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid);fs.mkdirSync(run,{recursive:true});const save=(n,j)=>fs.writeFileSync(path.join(run,n),JSON.stringify(j,null,2)+'\n');
const sources={current:load(opts.current)};if(opts.baseline)sources.baseline=load(opts.baseline);const slices=Object.fromEntries(Object.entries(sources).map(([n,s])=>[n,extract(s)]));
const fixture=load(path.join(repo,'tools/fixtures/input_host_recovery_health_fixture.cpp')),deps=['deps/json/json.hpp','native/input_host_recovery_health.h','native/sleep_recovery_diagnostics.h','native/virtual_output_status.h','native/input_frame_submission.h','native/legion_motion_source.h','native/windows_motion_submission.h','native/touch_haptic_dispatch.h','native/game_feedback_receipt.h','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'].map(p=>load(path.join(repo,p)));if(opts.baselineHeader)deps[1]=load(opts.baselineHeader);const inc=path.join(run,'extracted');fs.mkdirSync(inc,{recursive:true});for(const d of deps)fs.writeFileSync(path.join(inc,path.basename(d.file)),d.bytes);
save('source-bindings.json',{sources:Object.fromEntries(Object.entries(sources).map(([n,s])=>[n,{file:s.file,sha256:s.sha256,fixed:slices[n].fixed,ranges:slices[n].ranges.map(({code,...r})=>r)}])),fixture:{file:fixture.file,sha256:fixture.sha256},dependencies:deps.map(({file,sha256})=>({file,sha256})),runnerSha256:sha(fs.readFileSync(fileURLToPath(import.meta.url))),productionSubstitutions:[],scope:'Actual health/suppression/maintenance and production helper with inert Host/power/snapshots/receipts; no full inputHostSubmitPad or transport/device execution; frame guard TLS intentionally unbound here and its environment Current/Note seams inert; production motion types and Windows admission helper included, null motion only and Legion seam rejects nonnull; motion not accepted or tested; separate input_frame_submission_selftest executes that contract.'});
for(const [n,x]of Object.entries(slices))for(const r of x.ranges)fs.writeFileSync(path.join(inc,n+'-'+r.name.replace(/[^\w-]/g,'_')+'.txt'),r.code);
const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';assert(fs.existsSync(vcvars));const quote=s=>{assert(!/["\r\n]/.test(s));return '"'+s+'"';};
function command(label,exe,args,cwd,expected){const r=spawnSync(exe,args,{cwd,encoding:'utf8',timeout:60000});fs.writeFileSync(path.join(cwd,label+'.stdout.log'),r.stdout??'');fs.writeFileSync(path.join(cwd,label+'.stderr.log'),r.stderr??'');save(label+'-'+path.basename(cwd)+'.command.json',{exe,args,cwd,status:r.status,expected,error:r.error?String(r.error):null});assert(!r.error&&r.status===expected,label+' failed '+r.status+'; see '+cwd);return r.stdout;}
const results={};for(const [name,x]of Object.entries(slices))for(const diag of [false,true]){
 const label=name+(diag?'-diagnostic-on':'-diagnostic-off'),dir=path.join(run,label);fs.mkdirSync(dir,{recursive:true});let cpp=fixture.text;for(const [n,c]of Object.entries(x.parts)){const m='@@'+n+'@@';assert(cpp.split(m).length===2,n);cpp=cpp.replace(m,()=>c);}assert(!/@@\w+@@/.test(cpp));fs.writeFileSync(path.join(dir,'fixture.cpp'),cpp);
 const cmd=path.join(dir,'compile.cmd');fs.writeFileSync(cmd,calibrationToolchainPreamble(vcvars)+'cl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 /WX /O2 /DHEALTH_FIXED='+(x.fixed?1:0)+(diag?' /DYMCC_MSI_REPORT_CHAIN_DIAGNOSTIC':'')+' '+quote(path.join(dir,'fixture.cpp'))+' /I'+quote(inc)+' /Fo'+quote(path.join(dir,'fixture.obj'))+' /Fe'+quote(path.join(dir,'fixture.exe'))+'\r\nexit /b %errorlevel%\r\n');command('build','cmd.exe',['/d','/c',cmd],dir,0);
 const old=name==='baseline'||opts.expectRegression,readerGap=opts.expectReaderGap,semanticFailure=opts.expectSemanticFailure;
 assert(!semanticFailure||(!old&&!readerGap),'Semantic failure mode cannot mix legacy expectations');
 const j=JSON.parse(command('execute',path.join(dir,'fixture.exe'),[],dir,(old||readerGap||semanticFailure)?1:0));
 save(label+'.results.json',j); // Preserve the unchanged fixture's original assertions before validating expected failures.
 const failed=j.cases.filter(c=>!c.passed);const legacyGap=readerGap && !x.ranges.find(r=>r.name==='inputHostRecoveryHealthSource')?.code.includes('xinput-any-connected');
 assert(semanticFailure?failed.length>0&&failed.every(c=>c.checks.some(check=>check.passed===false)):failed.length===(old?28:readerGap?10+(legacyGap?1:0):0),'Unexpected failing count '+label+': '+failed.length);
 assert(!old||failed.every(c=>c.regressionWitness),'Non-witness failed');assert(!readerGap||failed.every(c=>c.readerGapWitness||c.candidateGapWitness),'Unexpected reader-gap failure');
 results[label]={cases:j.caseCount,checks:j.checks,failedCases:j.failedCases,failedNames:failed.map(c=>c.name),expectedOldFailures:old,expectedReaderGapFailures:readerGap,expectedSemanticFailures:semanticFailure};
}
const stability={};for(const [n,s]of Object.entries(sources)){const end=load(s.file),again=extract(end),changed=slices[n].ranges.filter(r=>!again.ranges.some(v=>v.name===r.name&&v.sha256===r.sha256));assert(changed.length===0,'Related source drift');stability[n]={wholeFileChanged:end.sha256!==s.sha256,relatedRangesStable:true,sha256AtEnd:end.sha256};}
for(const d of deps)assert.equal(load(d.file).sha256,d.sha256,'production dependency drift: '+d.file);
assert.equal(load(fixture.file).sha256,fixture.sha256,'fixture drift');
const result={passed:true,run,profiles:results,sourceStability:stability,productionModifiedByHarness:false,sourceRewrittenByHarness:false,deviceOrHostExecuted:false,newThreadsOrDeviceIO:false,claim:'Source/receipt health budget settlement only; not full Host or physical device acceptance.'};save('summary.json',result);fs.writeFileSync(path.join(opts.out,'summary.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result,null,2));
