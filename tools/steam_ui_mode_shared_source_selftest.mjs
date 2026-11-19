// Wiring regression: one verified reader, two snapshot consumers and original safety gates.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {func as extractNativeFunction} from './fixtures/hc_onex_x2_application/extract.mjs';
import {verifyProducerRead} from './fixtures/steam_mode_read/producer_read.mjs';
const sourceSelection=process.argv.slice(2);
const producerReadOnly=sourceSelection.length===1&&sourceSelection[0]==='--producer-read-only';
if(sourceSelection.length&&!(sourceSelection.length===1&&['--producer-read-only','--after-verified-reader','--public-read-owner','--decky-read-owner'].includes(sourceSelection[0]))){
 console.error('Invalid shared source selection');process.exit(2);
}else if(producerReadOnly){
 const proof=verifyProducerRead();
 console.log('PASS Directed producer fixture equals current complete native read with only Observer parameter adaptation');
 console.log(JSON.stringify({selection:'--producer-read-only',actualNames:['Directed producer fixture equals current complete native read with only Observer parameter adaptation'],passed:1,scope:'source-contract',realSteamOperations:0,producerRead:proof}));
}else{
const main=fs.readFileSync(new URL('../native/main.cpp',import.meta.url),'utf8');
const observer=fs.readFileSync(new URL('../native/steam_ui_mode_observer.h',import.meta.url),'utf8');
// Match the complete definition: following Steam helpers are not Decky readers.
assert.equal((main.match(/^static void deckySidebarInitialize\(\)\s*\{/gm)||[]).length,1,'One Decky initialization definition');
const deckyRange=extractNativeFunction(main,'deckySidebarInitialize'),decky=deckyRange.text;
const section=(text,begin,end)=>{const start=text.indexOf(begin),finish=text.indexOf(end,start+begin.length);assert(start>=0&&finish>start&&text.indexOf(begin,start+1)<0&&text.indexOf(end,finish+1)<0,'Unique complete source section '+begin);return text.slice(start,finish);};
const admission=section(decky,'config.launchAdmission=','config.connectionHealth=');
const sharedSnapshot='const auto state = g_steamModeObserver.snapshot(session, &modeCacheMiss);';
const renewal=admission.match(/\bif\s*\(force\s*&&\s*!g_steamModeObserver[.]requestAdmissionRefresh\(session,\s*ymcc::deckysession::admissionReadSatisfied\)\)\s*freshRead[.]reset\(\);/);
const sharedRenewal=!!renewal&&admission.indexOf(sharedSnapshot)>renewal.index;
const recovery=fs.readFileSync(new URL('../native/decky_sidebar_recovery.h',import.meta.url),'utf8');
const admissionRefresh=section(recovery,'class AdmissionReadRefresh {','class LoaderSessionBudget {');
const css=main.slice(main.indexOf('    cssGuard.readMode ='),main.indexOf('    cssGuard.mayLaunch ='));
const changed=main.slice(main.indexOf('    ymcc::steamui::ModeObserverConfig modeObserver;'),main.indexOf('    // Enabled CSS linkage'));
const options=sourceSelection;const suffix=options[0]==='--after-verified-reader';const publicOwner=options[0]==='--public-read-owner';const deckyOwner=options[0]==='--decky-read-owner';const deckyNames=new Set(['Decky uses a shared snapshot and retains its 30s recovery request','Decky still rechecks PID birth and root after consuming the snapshot']);const selectedNames=new Set(['Automatic frontend consumes the shared pending/known result before any direct CDP fallback','Manual refresh retains the verified direct reader']);
const actualNames=[];let passed=0,seen=0;function check(name,value){++seen;if(publicOwner&&!selectedNames.has(name)||deckyOwner&&!deckyNames.has(name))return;if(suffix&&seen<=3){console.log('REFERENCE_PREFIX '+name);return;}assert.ok(value,name);++passed;actualNames.push(name);console.log('PASS '+name)}
check('Decky uses a shared snapshot and retains its 30s recovery request',admission.includes(sharedSnapshot)&&admission.includes('static ymcc::deckysidebar::AdmissionReadRefresh freshRead;')&&/const\s+bool\s+force\s*=\s*freshRead[.]take\(GetTickCount64\(\),pending[.]admissionRetry&&pending[.]admissionRecovery\)/.test(admission)&&sharedRenewal&&admissionRefresh.includes('if (seen_ && now - last_ < 30000) return false;')&&!decky.includes('steamui::read('));
check('CSS consumes the same shared snapshot within the current Ready and has no direct reader',css.includes('g_steamModeObserver.snapshot(session, power)')&&css.includes('sharedPowerLifecycleCurrent(power)')&&!css.includes('steamui::read('));
const readOwned=section(changed,'modeObserver.readOwned =','modeObserver.changed =');
check('Shared worker alone performs the original verified UI read',readOwned.includes('ymcc::steamui::read(g_steamSettingsObserver,true,readCurrent,ymcc::steamlive::ModeTransportWait::SharedObserver,&transport)')&&readOwned.includes('if (!modeCurrent || !modeCurrent() || !captureSharedPowerLifecycle(modePower)) return cancelled();')&&readOwned.includes('sharedPowerLifecycleCurrent(modePower) && g_steamSettingsObserver.sameIdentityEpoch(identity)')&&(changed.match(/steamui::read\(/g)||[]).length===1&&readOwned.includes('session==g_steamSettingsObserver.sessionSnapshot()')&&readOwned.includes('if (!readCurrent()) return cancelled();'));
check('Mode proof diagnostics project the already-read snapshot on the existing event',changed.includes('ymcc::deckysession::diagnosticWithIdentity(session,state,g_steamSettingsObserver.identitySnapshot())')&&changed.includes('deckySidebarDiagnostic("steam-mode-read",detail,true)')&&changed.includes('detail["steamBirth"]=std::to_string(session.created)'));
const channelChanged=changed.slice(changed.indexOf('modeObserver.changed ='),changed.indexOf('modeObserver.settled ='));
check('Verified shared channel changes wake the existing Decky health owner behind the current-session and exit guard',channelChanged.includes('session==g_steamSettingsObserver.sessionSnapshot()')&&channelChanged.includes('g_exitRequested.load()')&&(channelChanged.match(/g_deckySidebarRuntime[.]requestConnectionCheck[(][)];/g)||[]).length===1&&channelChanged.indexOf('session==g_steamSettingsObserver.sessionSnapshot()')<channelChanged.indexOf('g_deckySidebarRuntime.requestConnectionCheck();'));
check('Confirmed shared mode wakes both deferred Decky and CSS',changed.includes('g_deckySidebarRuntime.requestLaunchAdmissionRefresh()')&&changed.includes('g_steamCssGuard.observeMode(session,'));
check('The shared worker does not launch Steam or change settings',!/ShellExecute|CreateProcess|steamui::set|TerminateProcess|WriteFile|SendInput|SetTimer|Sleep\(/.test(observer));
check('Demand follows each actual feature enablement',main.includes('setDemand(ymcc::steamui::ModeConsumer::Decky, state.enabled)')&&main.includes('setDemand(ymcc::steamui::ModeConsumer::Css,state.linked)'));
check('All three native shutdown paths stop the shared worker', (main.match(/g_steamModeObserver\.shutdown\(\)/g)||[]).length===3);
check('Decky still rechecks PID birth and root after consuming the snapshot',admission.includes(sharedSnapshot)&&admission.includes('const auto current=g_steamSettingsObserver.sessionSnapshot();')&&admission.includes('current.pid!=session.pid||current.created!=session.created||current.root!=session.root')&&admission.indexOf(sharedSnapshot)<admission.indexOf('const auto current=g_steamSettingsObserver.sessionSnapshot();')&&admission.indexOf('const auto current=g_steamSettingsObserver.sessionSnapshot();')<admission.indexOf('current.pid!=session.pid||current.created!=session.created||current.root!=session.root'));
check('Shared reads come from handle waits and only coalesced demand',observer.includes('WaitForMultipleObjects')&&observer.includes('if (armed_) return')&&observer.includes('if (attempts_ < 3)'));
check('An old session or event during IO invalidates delivery',observer.includes('generation != generation_ || !(session == session_) || !demand()'));
check('The successful explicit switch requests a read without relying on window events',main.includes('const auto result = ymcc::steamui::set')&&main.includes('g_steamModeObserver.requestRefresh(g_steamSettingsObserver.sessionSnapshot());'));
check('Only a newly armed deadline signals the worker',observer.includes('if (newlyArmed && wake_) SetEvent(wake_)'));
const uiRead=main.slice(main.indexOf('    ipc_on("steam.uiState",'),main.indexOf('    ipc_on("steam.uiMode.set",'));
check('Automatic frontend consumes the shared pending/known result before any direct CDP fallback',uiRead.includes('if (!args.value("force", false))')&&uiRead.includes('g_steamModeObserver.passiveSnapshot(g_steamSettingsObserver.sessionSnapshot(), &power)')&&uiRead.indexOf('if (shared.is_object()) return ownerCurrent()?shared:cancelled()')<uiRead.indexOf('return ymcc::steamui::read('));
check('Manual refresh retains the verified direct reader',uiRead.includes('return ymcc::steamui::read(g_steamSettingsObserver, args.value("force", false),ownerCurrent)')&&uiRead.includes('captureSharedPowerLifecycle(power)')&&uiRead.includes('sharedPowerLifecycleCurrent(power)'));
const settled=changed.slice(changed.indexOf('modeObserver.settled ='),changed.indexOf('modeObserver.stableBigPicture ='));
check('Pending page completion only refreshes UI, never CSS/Decky startup',settled.includes('deckySidebarPost("steam.uiStateChanged"')&&!/observeMode|requestUiRefresh|requestLaunchAdmissionRefresh|confirmAndIdle|steamui::set/.test(settled));
check('In-flight invalidation consumes attempts without spending a new lifecycle budget',observer.includes('window_.request(GetTickCount64(), inFlight_)')&&observer.includes('hints != hintGeneration_'));
const stable=section(changed,'modeObserver.stableBigPicture =','modeObserver.cssBigPicturePublished =');
const cssPublication=section(changed,'modeObserver.cssBigPicturePublished =','modeObserver.sourceChanged =');
const cssHandoff=section(observer,'cachedTiming_ = valid && returnedTick','if (notify && config_.changed)');
check('Stable Big Picture completion can release only a waiting CSS use from shared proof',cssPublication.includes('session == g_steamSettingsObserver.sessionSnapshot()')&&cssPublication.includes('!ymcc::steamcss::appearanceModeReady(session, state)')&&cssPublication.includes('g_steamCssGuard.observeMode(session, sample, stable)')&&!stable.includes('g_steamCssGuard')&&/if \(mode == "desktop"\)\s*\{\s*try \{\s*g_steamCssGuard[.]observeMode/.test(channelChanged)&&!/requestUiRefresh|confirmAndIdle|steamui::read|steamui::set|CreateProcess|ShellExecute/.test(cssPublication));
check('Stable Big Picture keeps the existing Decky admission and closed-socket health wake',stable.includes('requestLaunchAdmissionRefresh(retainedFrontendVerified,&receipt)')&&/if\(backend!=cssMode[.]end\(\)&&\*backend==false&&socketState!=cssMode[.]end\(\)&&\*socketState=="socket-closed"\)\s*g_deckySidebarRuntime[.]requestConnectionCheck\(\);/.test(stable));
check('Stable callback requires a fresh verified unchanged Big Picture result',observer.includes('stableBigPicture = !notify && result.value("mode", std::string{}) == "bigpicture"')&&observer.includes('if (stableBigPicture && config_.stableBigPicture)')&&/if \(valid && css_ && config_[.]cssBigPicturePublished &&\s*\(stableBigPicture \|\| \(notify && result[.]at\("mode"\) == "bigpicture"\)\)\)/.test(cssHandoff)&&/passivePending_ = false;\s*\}\s*if \(notifyCss && css_ && lifecycleCurrent\(\)\)/.test(cssHandoff)&&cssHandoff.indexOf('config_.cssBigPicturePublished(session, result, cssTiming, stableBigPicture)')<cssHandoff.indexOf('emitSource(std::move(sourceNotice))')&&cssHandoff.includes('config_.cssBigPicturePublished(session, result, cssTiming, stableBigPicture)'));
if(!publicOwner&&!deckyOwner)check('Directed producer fixture equals current complete native read with only Observer parameter adaptation',Boolean(verifyProducerRead()));
if(deckyOwner)assert.deepEqual(actualNames,[...deckyNames],'Exact original Decky source assertions only');
console.log('SHARED_STEAM_SOURCE_OK passed='+passed+' realSteamOperations=0');
console.log(JSON.stringify({selection:options[0]??'default',actualNames,passed,scope:'source-contract',realSteamOperations:0,deckyDefinition:{start:deckyRange.start,end:deckyRange.end}}));

}
