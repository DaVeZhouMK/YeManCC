import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const read=p=>fs.readFileSync(path.join(root,p),'utf8');
const args=process.argv.slice(2);assert.ok(args.length===0||(args.length===1&&['--trial-pair-only','--tracker-evidence-only'].includes(args[0])),'unsupported recovery source selection');
const trialPairOnly=args[0]==='--trial-pair-only',trackerEvidenceOnly=args[0]==='--tracker-evidence-only';
const main=args.length?'':read('native/main.cpp'),runtime=trialPairOnly?'':read('native/steam_original_recovery_runtime.h'),auto=args.length?'':read('native/sleep_recovery_auto.h');
const checks=[];
function check(name,fn){fn();checks.push(name);}
function body(source,name){
 const mask=source.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,s=>s.replace(/[^\n]/g,' '));
 const re=new RegExp('^static[^\\n;]*\\b'+name+'\\s*\\(','gm');
 for(const hit of mask.matchAll(re)){let i=hit.index+hit[0].length,depth=1;while(i<mask.length&&depth){if(mask[i]==='(')depth++;if(mask[i]===')')depth--;i++;}while(/\s/.test(mask[i]))i++;if(mask.slice(i,i+8)==='noexcept'){i+=8;while(/\s/.test(mask[i]))i++;}if(mask[i]!=='{')continue;const begin=i++;depth=1;for(;i<mask.length;i++){if(mask[i]==='{')depth++;if(mask[i]==='}'&&!--depth)return source.slice(begin+1,i);}}
 throw Error(name+' definition missing');
}
if(!trialPairOnly){
check('a new route cannot inherit an earlier game or route tracker refusal',()=>{
  const route=body(runtime,'runRoute');
  const marker=route.indexOf('{"phase","route-started"}'),end=route.indexOf('const auto configEpoch=',marker);
  assert.ok(marker>=0&&end>marker,'route-start diagnostic publication missing');
  const start=route.slice(marker,end);
  assert.match(start,/\{"trackerRead",nullptr\}/);
  assert.match(start,/\{"trackerGameId",""\}/);
  assert.match(route,/refusal\["trackerRead"\]=/);
  assert.match(body(runtime,'publish'),/c->snapshot\.update\(value\)/);
});
check('native Steam identity refusal reports this round without reaching the engine',()=>{
  const route=body(runtime,'runRoute');
  const classify=route.indexOf('ymcc::steamappstatewin::gameIdMatches(fullGameId,expectedApp)');
  const gate=route.indexOf('if(!idMatches||((fullGameId>>24)&255)!=2)',classify);
  const end=route.indexOf('if(pending.kind==Trigger::Wake',gate);
  const engine=route.indexOf('steamOriginalRecoveryEngineHandle(');
  assert.ok(classify>=0&&gate>classify&&end>gate&&engine>end);
  const refusal=route.slice(gate,end);
  assert.match(refusal,/\{"reason","non-type2-gameid"\}/);
  assert.match(refusal,/\{"target",asJson\(scope\.game\)\}/);
  assert.match(refusal,/\{"trackerGameId",tracked\.gameId\}/);
  assert.match(refusal,/\{"appid",tracked\.binding\.appid\}/);
  assert.match(refusal,/idMatches\?"native-app":"unknown"/);
  assert.match(refusal,/\{"actionAuthority",false\}/);
  assert.match(refusal,/\breturn;\s*}\s*$/);
  assert.match(route,/privateIdentity\.gameId64!=fullGameId/);
});
check('the durable route event distinguishes both startup proof sources from direct tracking',()=>{
  const route=body(runtime,'runRoute');
  assert.match(route,/request\["identitySource"\]\s*=\s*handoffSelected\?"public-startup-handoff"\s*:\s*historicalHandoffSelected\?"public-historical-startup-handoff"\s*:\s*"direct-steam-process-row"\s*;/);
});
}
if(!args.length){
check('event host has no process launcher, shell, or periodic timer',()=>{
  assert.doesNotMatch(runtime,/\b(?:CreateProcess\w*|ShellExecute\w*|SetTimer|CreateTimerQueueTimer|TerminateProcess|TerminateThread|system|_wpopen)\s*\(/);
  const take=body(runtime,'takeNext');
  assert.doesNotMatch(runtime.replace(take,''),/\bSleep\s*\(|\.(?:wait_for|wait_until)\s*\(/);
  assert.match(take,/c->cv\.wait\(lock/);
  assert.match(take,/c->cv\.wait_for\(lock,std::chrono::milliseconds\(front\.notBeforeTick-now\)\)/);
  assert.match(take,/if\(c->stop\)return false/);
  // Only the owned queue wait is a timed-wait exception.
  assert.doesNotMatch(take,/\bSleep\s*\(/);
  const timed='c->cv.wait_for(lock,std::chrono::milliseconds(front.notBeforeTick-now));';
  assert.equal(take.split(timed).length-1,1);
  assert.doesNotMatch(take.replace(timed,''),/\.(?:wait_for|wait_until)\s*\(/);
});
check('game comes only from the existing unique valve',()=>{
  assert.match(runtime,/nativeValveReadCurrentTarget\(&game,&touch\)/);
  assert.doesNotMatch(runtime,/nativeValveAcquire|nativeScanGames|CreateToolhelp32Snapshot|Process32First|wuxia\.exe/i);
  assert.match(runtime,/SteamGameLink::tracking\(client,\{game\.pid,game\.processCreated,game\.path\},/);
  assert.match(runtime,/SteamGameLink::trackingOriginal\(client,\{game\.pid,game\.processCreated,game\.path\},original\.selectedProcess,trackerAllowed\)/);
  assert.match(runtime,/nativeSteamNativeObservationTracking\(client,\{game\.pid,game\.processCreated,game\.path\},trackerAllowed\)/);
});
check('Steam custom library is never executed',()=>assert.doesNotMatch(runtime,/CustomSteamLibrary|RecoveryHost\.exe|driver\.exe|node\.exe|powershell\.exe/i));
check('same valve identity is filtered before queueing ordinary heartbeat work',()=>{
  const b=body(runtime,'steamOriginalRecoveryTargetChanged');
  const compact=b.replace(/\s+/g,'');
  const first=compact.indexOf('observedTarget.pid==pid'),discover=compact.indexOf('SteamOriginalRecovery::identity(pid,birth,path)'),enqueue=compact.indexOf('SteamOriginalRecovery::enqueue(');
  assert.ok(first>=0&&discover>first&&enqueue>discover,'cached identity before discovery and enqueue');
  assert.ok(compact.includes('observedTarget.birth==birth')&&compact.includes('_wcsicmp(c->observedTarget.path.c_str(),path.c_str())==0'),'full cached process identity');
  assert.ok(compact.includes('resumeDeferred=!c->stop&&pid&&birth&&c->observedRules!=rules&&c->deferredWake!=0'),'only existing deferred wake may resume same target');
  assert.ok(compact.includes('if(c->targetObserved&&c->observedTarget==next){sameObserved=true;resumeDeferred=c->observedRules!=rules&&c->deferredWake!=0;c->observedRules=rules;}'),'locked identity race retains deferred rules');
  const returns=[...compact.matchAll(/if\(sameObserved\)\{if\(resumeDeferred\)steamOriginalRecoveryPowerReady\(currentPowerGeneration\(\)\);return;\}/g)];
  assert.equal(returns.length,2,'same identity return guards missing');assert.ok(returns[0].index<discover&&returns[1].index>discover&&returns[1].index<enqueue,'same identity cannot enter ordinary queue');
});
check('healthy UI/window events cannot reopen a recovery job',()=>assert.match(body(runtime,'steamOriginalRecoveryBindingReady'),/enabled\.load\(\)&&!c->ownsRunning\.load\(\)/));
check('ordinary private identity ready callback queues only existing TargetChanged work',()=>{
  const b=body(runtime,'steamOriginalRecoveryIdentityReady');
  for(const piece of ['c->ownsRunning.load()','g_sleepRecoveryAutoConfigEpoch','g_gameRulesEpoch','c->boundGameId!=gameId','c->boundAppId!=appid','Trigger::TargetChanged'])assert.ok(b.includes(piece),piece);
  assert.doesNotMatch(b,/EngineHandle|liveRequest|admission\.|saveJson|recoveryAllowed|startupPending\s*=/);
});
check('power callbacks queue and never call the engine',()=>{
  for(const name of ['steamOriginalRecoverySuspend','steamOriginalRecoveryWake','steamOriginalRecoveryPowerReady'])
    assert.doesNotMatch(body(runtime,name),/EngineHandle|liveRequest|saveJson|handle\(/);
});
check('wake only runs after the existing power Ready commit',()=>{
  assert.match(body(runtime,'steamOriginalRecoveryPowerReady'),/PowerLifecycle::Ready/);
  const commit=body(main,'commitPowerResume'),ready=commit.indexOf('g_powerLifecycle.store(PowerLifecycle::Ready, std::memory_order_release);'),notify=commit.indexOf('sleepRecoveryAutoPowerReady(generation);',ready+1);
  assert.ok(ready>=0&&notify>ready,'Ready commit before recovery notification');
  assert.match(commit,/if \(phase == PowerLifecycle::Ready\) \{\s*sleepRecoveryAutoPowerReady\(generation\);/,'already committed Ready stays idempotent');
  const relay=body(auto,'sleepRecoveryAutoPowerReady');assert.ok(relay.indexOf('SleepRecoveryAuto::powerReady(generation)')>=0&&relay.indexOf('SleepRecoveryAuto::powerReady(generation)')<relay.indexOf('steamOriginalRecoveryPowerReady(generation)'),'shared recovery relay before original Running consumer');
  assert.match(auto,/sleepRecoveryAutoArm\(unsigned long long generation\).*steamOriginalRecoveryWake\(generation\)/);
});
check('new sleep and unique identity/rules changes revoke dispatch',()=>{
  const b=body(runtime,'sameScopeNow');
  for(const piece of ['latestPower>generation','g_exitRequested','g_gameRulesEpoch','nativeValveReadCurrentTarget','scope.game','scope.steam'])assert.ok(b.includes(piece),piece);
});
check('public event file is saved before adding the private account',()=>{
  assert.ok(runtime.indexOf('saveJson(root()+L"\\\\last-event.json",request)')<runtime.indexOf('request["account64"]='));
  assert.match(runtime,/&privateIdentity/);
});
check('export is read-only',()=>{
  assert.doesNotMatch(body(runtime,'steamOriginalRecoveryExportSnapshot'),/EngineHandle|enqueue|liveRequest|handle\(/);
  assert.match(main,/addRotated\(dataDir\+L"\\\\steam-original-recovery.jsonl"\)/);
});
check('ordinary shutdown joins the debugger owner instead of killing it',()=>{
  const b=body(runtime,'steamOriginalRecoveryShutdown');assert.match(b,/worker\.join\(\)/);assert.doesNotMatch(b,/\b(?:TerminateProcess|TerminateThread)\s*\(|\.detach\s*\(/);
});
check('UI exit cancels recovery before launching cleanup, without a blocking join',()=>{
  const b=body(main,'beginAsyncExit');
  assert.ok(b.indexOf('steamOriginalRecoveryRequestStop()')<b.indexOf('tryStartExitCleanupWorker('));
  assert.ok(b.indexOf('g_exitRequested.compare_exchange_strong')<b.indexOf('steamOriginalRecoveryRequestStop()'));
  const cancel=body(runtime,'steamOriginalRecoveryRequestStop');
  assert.match(cancel,/c->stop=true;c->pending.clear\(\)/);
  assert.match(cancel,/SetEvent/);
  assert.doesNotMatch(cancel,/\.join\(|saveJson|publish\(|EngineHandle|WaitFor/);
});
check('window exit requires actual cleanup completion and never detaches its owner',()=>{
  const b=main.slice(main.indexOf('case WM_APP_EXIT_READY:'),main.indexOf('case WM_POWER_RESUME_READY:'));
  assert.match(b,/WaitForSingleObject\(g_exitCleanupThread,0\)/);
  assert.match(b,/cleanupWait!=WAIT_OBJECT_0\|\|!steamOriginalRecoveryStopped\(\)/);
  assert.ok(b.indexOf('return 0;')<b.indexOf('CloseHandle(g_exitCleanupThread)'));
  assert.doesNotMatch(b,/steamOriginalRecoveryShutdown\(|worker-detached|\.join\(/);
  assert.doesNotMatch(b,/SetTimer\(/);
  assert.match(main,/ymcc::exitLifecycle::waitForCleanupOrMessages\(cleanup\)/);
  assert.match(main,/SendMessageW\(g_hwnd,WM_APP_EXIT_READY/);
});
check('window destruction only cancels recovery; background owner performs the join',()=>{
  const start=main.indexOf('case WM_DESTROY:',main.indexOf('case WM_APP_EXIT_READY:'));
  const b=main.slice(start,start+2700);
  assert.match(b,/steamOriginalRecoveryRequestStop\(\)/);
  assert.doesNotMatch(b,/steamOriginalRecoveryShutdown\(\)/);
  assert.match(body(main,'exitCleanupThreadProc'),/steamOriginalRecoveryShutdown\(\)/);
});
check('new recovery worker cannot start after exit intent',()=>{
  assert.match(body(runtime,'steamOriginalRecoveryStart'),/control\|\|g_exitRequested.load/);
});
check('cleanup thread launch failure retains responsibility and retries without UI cleanup',()=>{
  const b=body(main,'tryStartExitCleanupWorker');assert.match(b,/CreateThread/);
  assert.doesNotMatch(b,/inputHostStop|DestroyWindow|postExitReadyOnce/);
  assert.match(b,/retryOnExitMessageWait/);
  const ready=main.slice(main.indexOf('case WM_APP_EXIT_READY:'),main.indexOf('case WM_POWER_RESUME_READY:'));
  assert.match(ready,/WaitForSingleObject\(g_exitCleanupThread,0\):WAIT_TIMEOUT/);
});
check('failed exit-ready post clears the coalescing bit',()=>{
  const b=body(main,'postExitReadyOnce');assert.match(b,/!PostMessageW/);assert.match(b,/g_exitReadyPosted.store\(false\)/);
});
check('exit message wakes use a nonblocking peek before waiting on the cleanup HANDLE again',()=>{
  const start=main.indexOf('auto ready=ymcc::exitLifecycle::waitForCleanupOrMessages(cleanup)');
  const end=main.indexOf('const auto messageResult=GetMessageW',start);
  const b=main.slice(start,end);assert.match(b,/PeekMessageW\(&msg,nullptr,0,0,PM_REMOVE\)/);
  assert.doesNotMatch(b,/\bGetMessageW\(/);
});
check('early WM_QUIT keeps cleanup ownership and enters the same exit protocol',()=>{
  const start=main.indexOf('auto ready=ymcc::exitLifecycle::waitForCleanupOrMessages(cleanup)');
  const end=main.indexOf('// 兜底：',start);const b=main.slice(start,end);
  assert.match(b,/if\(msg.message==WM_QUIT\)\{\s*g_exitCode.store[\s\S]*?continue;/);
  assert.match(b,/beginAsyncExit\(g_hwnd,msg.wParam,"wm-quit"\)/);
});
check('completed cleanup is distinct from failed creation and cannot restart after queued READY',()=>{
  assert.match(body(main,'tryStartExitCleanupWorker'),/g_exitCleanupCompleted\|\|g_exitCleanupThread/);
  const ready=main.slice(main.indexOf('case WM_APP_EXIT_READY:'),main.indexOf('case WM_POWER_RESUME_READY:'));
  assert.match(ready,/!CloseHandle\(g_exitCleanupThread\)\)\s*\{[\s\S]*?return 0;/);
  assert.ok(ready.indexOf('CloseHandle(g_exitCleanupThread)')<ready.indexOf('g_exitCleanupCompleted=true')); 
  const start=main.indexOf('    MSG msg{};');
  const loop=main.slice(start,main.indexOf('// 兜底：',start));
  assert.ok(loop.indexOf('if(g_exitCleanupCompleted)')<loop.indexOf('tryStartExitCleanupWorker(g_hwnd)'));
});
check('legacy list recovery yields ownership for the exact original PID and birth',()=>{
  assert.match(auto,/steamOriginalRecoveryOwnsRunning/);
  assert.match(auto,/if\(!nativeOriginalOwner&&!postReturnForegroundWait/);
});
}
if(!trackerEvidenceOnly)check('embedded Steam trial matches its declared source and payload',()=>{
  const directory=path.join(root,'native/steam_original_recovery_engine');
  const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
  const receipt=JSON.parse(fs.readFileSync(path.join(directory,'build-receipt.json'),'utf8'));
  assert.equal(receipt.schema,'ymcc.generic-trial-build.v1');assert.equal(receipt.abi,6);assert.equal(receipt.dispatcherAbi,1);
  const generator=fs.readFileSync(path.join(directory,'embed-trial.mjs'),'utf8');
  const list=/\bconst inputs=\[([^\]]+)\];/.exec(generator);assert.ok(list,'trial source declarations missing');
  assert.match(list[1],/^\s*(?:'[^'\r\n]+'\s*,?\s*)+$/,'trial source declarations must be literal paths');
  const names=[...list[1].matchAll(/'([^']+)'/g)].map(match=>match[1]);
  assert.ok(names.length>0&&new Set(names).size===names.length,'duplicate trial source declaration');
  assert.deepEqual(Object.keys(receipt.sourcePins).sort(),[...names].sort(),'trial source set mismatch');
  for(const name of names){const file=path.resolve(directory,name),relative=path.relative(root,file);assert.ok(relative&&!relative.startsWith('..')&&!path.isAbsolute(relative),'trial source outside repository');assert.equal(digest(fs.readFileSync(file)),receipt.sourcePins[name],'trial source bytes stale: '+name);}
  const header=fs.readFileSync(path.join(directory,'embedded_trial.h'));
  assert.equal(digest(header),receipt.embeddedHeaderSha256,'trial embedded header stale');
  const text=header.toString('utf8'),sha=/inline constexpr char sha256\[\]="([a-f0-9]{64})";/.exec(text),image=/inline constexpr unsigned char image\[\]=\{([\s\S]*?)\};/.exec(text);
  assert.ok(sha&&image,'trial embedded declarations missing');assert.equal(sha[1],receipt.sha256,'trial embedded digest label');
  assert.match(image[1].replace(/0x[a-f0-9]{2}/gi,''),/^[,\s]*$/,'trial embedded payload is not literal bytes');
  const payload=Buffer.from([...image[1].matchAll(/0x([a-f0-9]{2})/gi)].map(match=>parseInt(match[1],16)));
  assert.equal(payload.length,receipt.bytes,'trial embedded payload length');assert.equal(digest(payload),receipt.sha256,'trial embedded payload digest');
});
console.log(JSON.stringify({schema:'ymcc.steam-original-recovery-runtime-source-selftest.v1',passed:true,checks:checks.length,names:checks,steamActions:0,sleepActions:0,gameLaunches:0},null,2));
