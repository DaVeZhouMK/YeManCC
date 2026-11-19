import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import crypto from 'node:crypto';
const argv=process.argv.slice(2),allowed=new Set(['--carrier-frame-only','--carrier-frame-queued-only','--carrier-frame-failure-only','--prepare-only','--checked']);
assert(argv.every(x=>allowed.has(x))&&new Set(argv).size===argv.length,'Unknown or duplicate HUD recovery argument');
const frameOnly=argv.includes('--carrier-frame-only'),queuedOnly=argv.includes('--carrier-frame-queued-only'),failureOnly=argv.includes('--carrier-frame-failure-only'),prepareOnly=argv.includes('--prepare-only'),checked=argv.includes('--checked');
assert(Number(frameOnly)+Number(queuedOnly)+Number(failureOnly)<=1&&(!prepareOnly||frameOnly||queuedOnly||failureOnly),'A single explicit boundary selection is required for prepare-only');
const directed=frameOnly||queuedOnly||failureOnly;
const root=process.cwd(),out=path.resolve(process.env.YMCC_HUD_RECOVERY_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/hud-bounded-recovery-20261017/native');
if(directed)assert(!fs.existsSync(out),'Fresh directed evidence directory required');fs.mkdirSync(out,{recursive:true});
const header=fs.readFileSync('native/monitor_desktop_hud.h','utf8');assert.equal(header.split('class Carrier {').length,2);
const cases=[];function check(name,value){assert(value,name);cases.push(name)}
check('renderer exit and hang observer is actually registered',header.includes('add_ProcessFailed(')&&header.includes('COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED')&&header.includes('COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE'));
check('GPU utility failures do not automatically spend renderer budget',!header.includes('COREWEBVIEW2_PROCESS_FAILED_KIND_GPU_PROCESS_EXITED'));
check('failure callback has weak state lifetime',header.includes('[weak=std::weak_ptr<State>(s)]')&&header.includes('const auto state=weak.lock();if(!state||!state->owner||!args)return S_OK;')&&header.includes('[weak](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs* args)'));
check('registration/navigation failures share bounded retirement',header.includes('if(FAILED(failureHook)){owner->fail(s,')&&header.includes('if(FAILED(s->view->Navigate('));
// The original three-site count predates hold/fade and queued-lease recovery.
// Keep its no-collector invariant, pinning every current owned timer role.
const timerRoles=[...header.matchAll(/SetTimer\([^,]+,([^,]+),/g)].map(m=>m[1]);
const roleCounts=Object.fromEntries([...new Set(timerRoles)].sort().map(name=>[name,timerRoles.filter(x=>x===name).length]));
check('only known owned hold/fade/lease timers, no collector cadence',!header.includes('SetInterval')&&JSON.stringify(roleCounts)===JSON.stringify({holdTimer:3,leaseTimer:4,radialFadeTimer:2}));
const helper=fs.readFileSync(path.join(root,'tools/monitor_hud_timer_api_boundary.h'));
fs.writeFileSync(path.join(out,'timer-api-boundary.h'),helper,{flag:directed?'wx':'w'});
const exposed=directed?'#include "timer-api-boundary.h"\n'+header.replace('class Carrier {','#define SetTimer ::carrierproof::setTimer\n#define KillTimer ::carrierproof::killTimer\nclass Carrier {\npublic: // Original fixture exposure; production methods otherwise intact.')+'\n#undef SetTimer\n#undef KillTimer\n':header.replace('class Carrier {','class Carrier {\npublic: // test exposure only; actual production methods unchanged');
const generated=path.join(out,'carrier-under-test.h');fs.writeFileSync(generated,exposed,{flag:directed?'wx':'w'});
const selectedCases=failureOnly?['replacement-SetTimer-failure-fail-and-close']:queuedOnly?['queued-old-WM_TIMER-after-renewal']:frameOnly?['normal-frame-renewal','queued-old-WM_TIMER-after-renewal','replacement-SetTimer-failure-fail-and-close']:null;
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const expectedFrameKill=(header.slice(header.indexOf(' bool frame('),header.indexOf(' bool close(',header.indexOf(' bool frame('))).match(/KillTimer\(s->hwnd,leaseTimer\);/g)||[]).length;
assert(expectedFrameKill===0||expectedFrameKill===1,'Unexpected frame timer shape');
const prepared={status:'PREPARED_NOT_RUN',prepared:true,passed:false,selectedCases,defaultGroupSelected:!directed,checked,wiringCases:cases,sourceSha256:sha(Buffer.from(header)),generatedHeader:generated,generatedHeaderSha256:sha(Buffer.from(exposed)),expectedFrameKill,helperSha256:sha(helper)};
if(prepareOnly){fs.writeFileSync(path.join(out,'PREPARED.json'),JSON.stringify(prepared,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(prepared));process.exit(0);}
const exe=path.join(out,'hud-recovery-test.exe'),cpp=path.resolve('tools/monitor_hud_recovery_selftest.cpp');
const flags=checked?'/Od /RTC1 /D_DEBUG /MTd':'/O2 /MT';
const command='@echo off\r\nset "PATH=C:\\Windows\\System32;C:\\Windows;%PATH%"\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" 10.0.26100.0 >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /DUNICODE /D_UNICODE /DNOMINMAX /DCARRIER_PROOF_EXPECT_FRAME_KILL='+expectedFrameKill+' '+flags+' "'+cpp+'" /I"'+out+'" /I"'+path.join(root,'native')+'" /I"'+path.join(root,'deps/webview2/build/native/include')+'" /I"'+path.join(root,'deps/json')+'" /Fo"'+path.join(out,'hud-recovery-test.obj')+'" /Fe"'+exe+'" /link user32.lib gdi32.lib ole32.lib oleaut32.lib dcomp.lib comctl32.lib CoreMessaging.lib WindowsApp.lib\r\nif errorlevel 1 exit /b 1\r\n"'+exe+'"'+(frameOnly?' --carrier-frame-only':queuedOnly?' --carrier-frame-queued-only':failureOnly?' --carrier-frame-failure-only':'')+'\r\nexit /b %errorlevel%\r\n';
fs.writeFileSync(path.join(out,'run.cmd'),command);const run=spawnSync('cmd.exe',['/d','/c',path.join(out,'run.cmd')],{encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:4e6});
fs.writeFileSync(path.join(out,'BUILD-AND-TEST.log'),(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,((run.stdout||'')+(run.stderr||'')).slice(-10000));
const text=run.stdout.replaceAll('\r\n','\n'),start=text.indexOf('{\n');assert(start>=0,'native JSON report missing');const native=JSON.parse(text.slice(start));
if(directed){assert.deepEqual(native.selectedCases,selectedCases);assert.equal(native.originalDefaultGroupExecuted,false);assert.equal(native.frameKillExpected,expectedFrameKill);}
const result={...native,wiringCases:cases,sourceSha256:sha(Buffer.from(header)),selectedCases:directed?native.selectedCases:null,scope:'Actual production Carrier and isolated owned Win32 HWNDs; inert environment, no installed app/game/driver or real renderer crash'};
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
