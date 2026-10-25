import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const read=p=>fs.readFileSync(path.join(root,p),'utf8');
const main=read('native/main.cpp'),runtime=read('native/steam_original_recovery_runtime.h'),auto=read('native/sleep_recovery_auto.h');
const checks=[];
function check(name,fn){fn();checks.push(name);}
function body(source,name){const match=new RegExp('\\b'+name+'\\([^;{}]*\\)\\s*\\{').exec(source);assert.ok(match,name+' definition missing');const begin=match.index+match[0].length-1;let depth=1;for(let i=begin+1;i<source.length;i++){if(source[i]==='{')depth++;if(source[i]==='}'&&!--depth)return source.slice(begin+1,i);}throw Error('unterminated '+name);}
check('event host has no process launcher, shell, or periodic timer',()=>{
  assert.doesNotMatch(runtime,/\b(?:CreateProcess\w*|ShellExecute\w*|SetTimer|CreateTimerQueueTimer|TerminateProcess|TerminateThread|system|_wpopen)\s*\(/);
  assert.doesNotMatch(runtime,/\bSleep\s*\(|\.(?:wait_for|wait_until)\s*\(/);
});
check('game comes only from the existing unique valve',()=>{
  assert.match(runtime,/nativeValveReadCurrentTarget\(&game,&touch\)/);
  assert.doesNotMatch(runtime,/nativeValveAcquire|nativeScanGames|CreateToolhelp32Snapshot|Process32First|wuxia\.exe/i);
  assert.match(runtime,/SteamGameLink::tracking\(client,\{game\.pid,game\.processCreated,game\.path\}\)/);
});
check('Steam custom library is never executed',()=>assert.doesNotMatch(runtime,/CustomSteamLibrary|RecoveryHost\.exe|driver\.exe|node\.exe|powershell\.exe/i));
check('same valve identity is filtered before queueing ordinary heartbeat work',()=>{
  const b=body(runtime,'steamOriginalRecoveryTargetChanged');
  assert.match(b,/observedTarget==next\)\{c->observedRules=rules;return;\}/);
  assert.ok(b.indexOf('observedTarget==next')<b.indexOf('enqueue('));
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
  assert.match(main,/g_powerLifecycle\.store\(PowerLifecycle::Ready, std::memory_order_release\);\s*steamOriginalRecoveryPowerReady\(generation\)/);
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
console.log(JSON.stringify({schema:'ymcc.steam-original-recovery-runtime-source-selftest.v1',passed:true,checks:checks.length,names:checks,steamActions:0,sleepActions:0,gameLaunches:0},null,2));
