import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),workspace=path.resolve(repo,'../../..');
const opts={out:path.join(workspace,'Build/Validation/VirtualInput-BugPriority-R103-20261016/rog-repro/feedback-fix'),current:path.join(repo,'native/main.cpp'),production:false};
for(let i=2;i<process.argv.length;i++){const a=process.argv[i];if(a==='--help'){console.log('node tools/xinput_feedback_identity_refresh_selftest.mjs [--current [MAIN_CPP]] [--out DIR]');process.exit(0)}if(a==='--current'){opts.production=true;if(process.argv[i+1]&&!process.argv[i+1].startsWith('--'))opts.current=path.resolve(process.argv[++i]);}else if(a==='--out'){opts.out=path.resolve(process.argv[++i]);}else throw Error('Unknown argument '+a);}
fs.mkdirSync(opts.out,{recursive:true});const run=path.join(opts.out,'runs',new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid);fs.mkdirSync(run,{recursive:true});
const sha=x=>crypto.createHash('sha256').update(x).digest('hex'),save=(n,j)=>fs.writeFileSync(path.join(run,n),JSON.stringify(j,null,2)+'\n');
const sourceBytes=fs.readFileSync(opts.current),source=sourceBytes.toString('utf8');
function extract(text,file){const masked=text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,m=>m.replace(/[^\r\n]/g,' ')),rs=[];function end(a){let n=0;for(let i=a;i<masked.length;i++){if(masked[i]==='{')n++;if(masked[i]==='}'&&--n===0)return i+1}throw Error('Unclosed')}
 function record(name,a,b,kind='function'){assert(a>=0&&b>a,name);const code=text.slice(a,b);const r={name,kind,file,source_sha256:sha(text),start_line:text.slice(0,a).split('\n').length,end_line:text.slice(0,b).split('\n').length,start_utf8_byte:Buffer.byteLength(text.slice(0,a)),end_utf8_byte_exclusive:Buffer.byteLength(text.slice(0,b)),sha256:sha(code),code};rs.push(r);return code;}
 function fn(n){for(const m of text.matchAll(new RegExp('^static[^\\r\\n]*\\b'+n+'\\s*\\(','gm'))){let i=masked.indexOf('(',m.index),d=0;for(;i<masked.length;i++){if(masked[i]==='(')d++;if(masked[i]===')'&&--d===0){i++;break}}while(/\s/.test(masked[i]))i++;if(masked.slice(i,i+8)==='noexcept'){i+=8;while(/\s/.test(masked[i]))i++;}if(masked[i]==='{')return record(n,m.index,end(i));}throw Error('Missing '+n)}
 function type(n){const m=new RegExp('^(?:struct|enum class) '+n+'\\b','m').exec(text);assert(m,n);return record(n,m.index,end(masked.indexOf('{',m.index))+1,'type')}
 const reader=text.indexOf('static void gamepadReadStateUnlocked('),a=text.indexOf('            const DWORD slotBit = (1u << i);',reader),b=masked.indexOf('if (edgeProbe || reprobeRequested || unknownBefore',a);assert(a>=0&&b>a);
 const parts={CLASSIFICATION:record('slot identity classification',a,end(masked.indexOf('{',b)),'block'),IDENTITY_ENUM:type('XInputSlotIdentity'),TARGET:type('PhysicalFeedbackTarget'),METHODS:['xinputNonVirtualConnectedMask','xinputPhysicalConnectedMask','xinputSlotIdentityVerdict','xinputSlotsMarkDiscoveryDirty','xinputSlotsRequestIdentityReprobe','hidHideUpper','hidHideIsVirtualIdentity'].map(fn).join('\n'),CURRENT:fn('physicalFeedbackSlotCurrent'),STOP:fn('physicalFeedbackStopTarget'),FINISH:fn('physicalFeedbackFinish'),READY:fn('nativePhysicalFeedbackReady'),START:fn('nativePhysicalFeedbackStart'),SCHEDULE:fn('gamepadSerialScheduleFeedbackStop')};
 return {parts,ranges:rs,record};}
const current=extract(source,opts.current),baselineFile=path.join(opts.out,'baseline-fragments.json');
let baseline;if(fs.existsSync(baselineFile)){baseline=JSON.parse(fs.readFileSync(baselineFile,'utf8'));}else{assert(!source.includes('xinputFeedbackIdentityProbe'),'Provide original baseline-fragments.json before testing production integration');baseline={parts:current.parts,ranges:current.ranges.map(({code,...r})=>r)};fs.writeFileSync(baselineFile,JSON.stringify(baseline,null,2)+'\n');}
const p=baseline.parts;assert(p.CLASSIFICATION.includes('dirtyDiscovery || boundaryFullScan'),'Baseline must contain source-identity discovery adoption');
const helper=`
static std::array<ymcc::input::identityRefresh::Slot,4> g_xinputFeedbackIdentity;
static ymcc::input::identityRefresh::DeferredStop g_xinputDeferredFeedbackStop;
// Sampling owns g_gamepadReadMx. Never acquire g_feedbackPulseMx here.
static bool xinputFeedbackIdentityProbe(DWORD slot,bool available,const std::wstring& path,XInputSlotIdentity kind,
 bool edge,bool requested,bool unknownBefore,unsigned long long epoch,unsigned long long power) {
    using namespace ymcc::input::identityRefresh;
    const auto result=g_xinputFeedbackIdentity[slot].observe({available,edge,requested,unknownBefore,path,
        kind==XInputSlotIdentity::Physical?Kind::Physical:kind==XInputSlotIdentity::Virtual?Kind::Virtual:Kind::Unknown,
        epoch,g_xinputSlotIdentityEpoch.load(std::memory_order_acquire),power,currentPowerGeneration()});
    if(result.advanceGeneration)++g_feedbackSlotGeneration[slot];
    if(result.notifyStop)if(const auto pending=g_xinputDeferredFeedbackStop.pendingFor(slot))
        (void)gamepadSerialScheduleFeedbackStop(pending,0);
    return result.retry;
}
`;
const candidate={...p};
function replaceOnce(s,old,next,label){assert(s.split(old).length===2,'Expected one '+label);return s.replace(old,()=>next);}
candidate.CLASSIFICATION=replaceOnce(p.CLASSIFICATION,'            const DWORD slotBit = (1u << i);',`            const DWORD slotBit = (1u << i);
            const auto feedbackProbeEpoch=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire);
            const auto feedbackProbePower=currentPowerGeneration();
            bool feedbackIdentityRetry=false;`,'classification prefix');
candidate.CLASSIFICATION=replaceOnce(candidate.CLASSIFICATION,'                    ++g_feedbackSlotGeneration[i];\r\n                    // OpenXInput',`                    feedbackIdentityRetry=xinputFeedbackIdentityProbe(i,false,L"",XInputSlotIdentity::Unknown,
                        edgeProbe,reprobeRequested,unknownBefore,feedbackProbeEpoch,feedbackProbePower);
                    // OpenXInput`,'unavailable generation');
candidate.CLASSIFICATION=replaceOnce(candidate.CLASSIFICATION,'                    ++g_feedbackSlotGeneration[i];\r\n                    const std::wstring slotPath',`                    const std::wstring slotPath`,'available generation');
candidate.CLASSIFICATION=replaceOnce(candidate.CLASSIFICATION,'                    if (slotIdentity == XInputSlotIdentity::Unknown) {',`                    feedbackIdentityRetry=xinputFeedbackIdentityProbe(i,true,slotPath,slotIdentity,
                        edgeProbe,reprobeRequested,unknownBefore,feedbackProbeEpoch,feedbackProbePower);
                    if (slotIdentity == XInputSlotIdentity::Unknown) {`,'complete identity proof');
candidate.CLASSIFICATION+='\n            if(feedbackIdentityRetry)g_xinputSlotReprobeMask.fetch_or(slotBit,std::memory_order_release);';
candidate.TARGET=replaceOnce(p.TARGET,'    std::shared_ptr<const HidHideMsiMainSourceRecord> record;',`    ymcc::input::identityRefresh::Proof xinputIdentity;
    std::shared_ptr<const HidHideMsiMainSourceRecord> record;`,'target identity');
candidate.READY=replaceOnce(p.READY,'if(physicalFeedbackSlotCurrent(target)){','if(physicalFeedbackSlotCurrent(target)&&g_xinputFeedbackIdentity[target.slot].capture(target.identityEpoch,token.power).valid){','ready verified owner');
candidate.START=replaceOnce(p.START,'        receipt.backend=Backend::XInput;receipt.slot=target.slot;',`        if(target.slot<4)target.xinputIdentity=g_xinputFeedbackIdentity[target.slot].capture(target.identityEpoch,target.powerGeneration);
        receipt.backend=Backend::XInput;receipt.slot=target.slot;`,'start proof capture');
candidate.START=replaceOnce(candidate.START,'if(!physicalFeedbackSlotCurrent(target)){','if(!physicalFeedbackSlotCurrent(target)||!target.xinputIdentity.valid){','start proof gate');
candidate.STOP=replaceOnce(p.STOP,'const PhysicalFeedbackTarget& target,bool touch=false) {','const PhysicalFeedbackTarget& target,bool touch=false,bool* unresolved=nullptr) {','stop outcome');
candidate.STOP=replaceOnce(candidate.STOP,'        if(physicalFeedbackSlotCurrent(target)) { XINPUT_VIBRATION zero{};(void)g_feedbackSetState(target.slot,&zero); }',`        if(target.slot>=4 || !(g_xinputConnectedMask&(1u<<target.slot)) ||
            (g_xinputPhysicalSourceLocked&&!(g_xinputPhysicalSourceMask&(1u<<target.slot))))return;
        const auto state=g_xinputFeedbackIdentity[target.slot].stop(target.xinputIdentity,
            g_xinputSlotIdentityEpoch.load(std::memory_order_acquire),currentPowerGeneration());
        if(state==ymcc::input::identityRefresh::Stop::Wait){if(unresolved)*unresolved=true;return;}
        if(state==ymcc::input::identityRefresh::Stop::Ready){XINPUT_VIBRATION zero{};(void)g_feedbackSetState(target.slot,&zero);}`,'stop identity owner');
candidate.FINISH=replaceOnce(p.FINISH,'    physicalFeedbackStopTarget(g_feedbackPulseTarget,g_feedbackPulseTouch);g_feedbackPulseActive=false;',`    bool unresolved=false;
    physicalFeedbackStopTarget(g_feedbackPulseTarget,g_feedbackPulseTouch,&unresolved);
    if(unresolved){
        const auto plan=g_xinputDeferredFeedbackStop.plan(generation,g_feedbackPulseTarget.slot,GetTickCount64(),
            static_cast<unsigned>(kXInputSlotReprobeBackoffMs));
        if(plan.retain&&gamepadSerialScheduleFeedbackStop(generation,static_cast<int>(plan.delayMs)))return;
    }
    g_xinputDeferredFeedbackStop.clear();g_feedbackPulseActive=false;`,'bounded unknown retirement');
candidate.SCHEDULE=replaceOnce(p.SCHEDULE,'    gamepadSerialStartLocked();g_gamepadSerialFeedbackStop=FeedbackStopDeadline{generation,GetTickCount64()+(unsigned)durationMs};',`    gamepadSerialStartLocked();const auto due=GetTickCount64()+(unsigned)durationMs;
    if(!g_gamepadSerialFeedbackStop||ymcc::input::identityRefresh::replaceDeadline(
        g_gamepadSerialFeedbackStop->generation,g_gamepadSerialFeedbackStop->due,generation,due))
        g_gamepadSerialFeedbackStop=FeedbackStopDeadline{generation,due};`,'generation-owned deadline');
candidate.POLICY_HELPER=helper;
const oldOnly={...p,POLICY_HELPER:''};
if(opts.production){assert(source.includes('xinputFeedbackIdentityProbe'),'Production feedback policy is not integrated');current.parts.POLICY_HELPER=current.record('xinputFeedbackIdentityProbe',source.indexOf('static std::array<ymcc::input::identityRefresh::Slot,4> g_xinputFeedbackIdentity;'),source.indexOf('\n}',source.indexOf('static bool xinputFeedbackIdentityProbe('))+2);}
const tested=opts.production?current.parts:candidate;
const header=fs.readFileSync(path.join(repo,'native/xinput_identity_refresh_policy.h'));
const template=fs.readFileSync(path.join(repo,'tools/fixtures/xinput_feedback_identity_refresh_fixture.cpp'),'utf8');
const deps=[['json.hpp',fs.readFileSync(path.join(repo,'deps/json/json.hpp'))],['touch_haptic_dispatch.h',fs.readFileSync(path.join(repo,'native/touch_haptic_dispatch.h'))],['xinput_identity_refresh_policy.h',header]];
for(const name of ["HcControllerFeedbackCapabilityCatalog.h","HcControllerFeedbackCapabilityCatalog.generated.h"])deps.push([name,fs.readFileSync(path.join(repo,'capabilities',name))]);
const inc=path.join(run,'extracted');fs.mkdirSync(inc,{recursive:true});for(const[n,b]of deps)fs.writeFileSync(path.join(inc,n),b);
for(const [variant,parts]of [['old',oldOnly],['fixed',tested]])for(const[n,c]of Object.entries(parts))fs.writeFileSync(path.join(inc,variant+'-'+n+'.txt'),c);
const receipt={mode:opts.production?'production':'candidate-not-yet-integrated',currentFile:opts.current,currentFileSha256:sha(sourceBytes),baselineRanges:baseline.ranges,currentRanges:current.ranges.map(({code,...r})=>r),headerSha256:sha(header),parts:Object.fromEntries(Object.entries(tested).map(([n,c])=>[n,sha(c)])),noWholeMainCopyOrCompile:true,noDeviceOrSDK:true};save('source-ranges.json',receipt);
function command(label,exe,args,cwd,expected=0){const r=spawnSync(exe,args,{cwd,encoding:'utf8',timeout:60000});fs.writeFileSync(path.join(run,label+'.stdout.txt'),r.stdout??'');fs.writeFileSync(path.join(run,label+'.stderr.txt'),r.stderr??'');save(label+'.command.json',{exe,args,cwd,status:r.status,expected,error:r.error?String(r.error):null});assert(!r.error&&r.status===expected,label+' failed unexpectedly; see outputs');return r.stdout;}
const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
const results={};for(const [variant,parts]of [['old',oldOnly],['fixed',tested]])for(const profile of ['off','on']){
 const dir=path.join(run,variant+'-'+profile);fs.mkdirSync(dir,{recursive:true});let code='#include "HcControllerFeedbackCapabilityCatalog.h"\n'+template;for(const[n,c]of Object.entries(parts)){assert(code.includes('@@'+n+'@@'),n);code=code.replace('@@'+n+'@@',()=>c);}assert(!/@@\w+@@/.test(code));fs.writeFileSync(path.join(dir,'fixture.cpp'),code);
 const batch=path.join(dir,'compile.cmd');fs.writeFileSync(batch,'@echo off\r\ncall "'+vcvars+'" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DFIXED_POLICY='+(variant==='fixed'?1:0)+(profile==='on'?' /DYMCC_MSI_REPORT_CHAIN_DIAGNOSTIC':'')+' "'+path.join(dir,'fixture.cpp')+'" /I"'+inc+'" /Fo"'+path.join(dir,'fixture.obj')+'" /Fe"'+path.join(dir,'fixture.exe')+'"\r\nexit /b %errorlevel%\r\n');
 command(variant+'-'+profile+'-build','cmd.exe',['/d','/c',batch],dir);
 const j=JSON.parse(command(variant+'-'+profile+'-execute',path.join(dir,'fixture.exe'),[],dir,variant==='old'?1:0));results[variant+'-'+profile]=j;save(variant+'-'+profile+'-results.json',j);
 if(variant==='fixed')assert(j.failedCases===0,'Candidate invariant failed');else assert(j.failedCases>0,'Old chain must reproduce missing-zero failure');
}
// Generate only a reviewable patch. Never write a whole production main.cpp.
const edits=[];function edit(old,next,label){const start=source.indexOf(old);assert(start>=0&&source.indexOf(old,start+1)<0,'Patch uniqueness '+label);edits.push({start,end:start+old.length,old,next,label});}
if(!opts.production){
 edit('#include "input_host_transport_state.h"','#include "input_host_transport_state.h"\r\n#include "xinput_identity_refresh_policy.h"','include policy');
 for(const key of ['CLASSIFICATION','TARGET','READY','START','STOP','FINISH','SCHEDULE'])edit(p[key],candidate[key],key);
 const verdict=current.ranges.find(r=>r.name==='xinputSlotIdentityVerdict');const verdictCode=p.METHODS.slice(p.METHODS.indexOf('static XInputSlotIdentity xinputSlotIdentityVerdict'));
 // Insert helper immediately after its actual identity verdict definition.
 const exactVerdict=source.slice(Buffer.byteLength(source.slice(0,0)),0);void exactVerdict;
 const method=extract(source,opts.current);const vr=method.ranges.find(r=>r.name==='xinputSlotIdentityVerdict');const vcode=Buffer.from(source).subarray(vr.start_utf8_byte,vr.end_utf8_byte_exclusive).toString('utf8');edit(vcode,vcode+'\r\n'+helper,'identity probe helper');
 const disconnect='            g_xinputSlotIdentityResolvedEpoch[i] = 0;';edit(disconnect,disconnect+'\r\n            g_xinputFeedbackIdentity[i].disconnect();\r\n            if(const auto pending=g_xinputDeferredFeedbackStop.pendingFor(i))(void)gamepadSerialScheduleFeedbackStop(pending,0);','disconnect retirement');
 const hunks=edits.sort((a,b)=>a.start-b.start).map(e=>{const line=source.slice(0,e.start).split('\n').length,a=e.old.replace(/\r\n/g,'\n').split('\n'),b=e.next.replace(/\r\n/g,'\n').split('\n');return '@@ -'+line+','+a.length+' +'+line+','+b.length+' @@\n'+a.map(x=>'-'+x).join('\n')+'\n'+b.map(x=>'+'+x).join('\n')+'\n';});
 fs.writeFileSync(path.join(opts.out,'main-integration.patch'),('--- a/native/main.cpp\n+++ b/native/main.cpp\n'+hunks.join('')).replace(/\r?\n/g,'\r\n'));
 save('patch-edits.json',edits.map(({old,next,...e})=>({...e,old_sha256:sha(old),next_sha256:sha(next)})));
}
const end=extract(fs.readFileSync(opts.current,'utf8'),opts.current);const drift=current.ranges.filter(r=>!end.ranges.some(x=>x.name===r.name&&x.sha256===r.sha256));assert(drift.length===0,'Relevant source changed during run');
const summary={passed:true,mode:receipt.mode,run,profiles:Object.fromEntries(Object.entries(results).map(([k,j])=>[k,{cases:j.caseCount,checks:j.checks,failedCases:j.failedCases}])),header:path.join(repo,'native/xinput_identity_refresh_policy.h'),header_sha256:sha(header),source_range_drift:false,main_cpp_edited:false,productionHostSdkOrDeviceExecuted:false,output:path.join(opts.out,'summary.json')};save('summary.json',summary);fs.writeFileSync(path.join(opts.out,'summary.json'),JSON.stringify(summary,null,2)+'\n');console.log(JSON.stringify(summary,null,2));
