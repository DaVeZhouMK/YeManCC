"""Steam-only final-wake wiring regression. Read source only; no live actions."""

from pathlib import Path

import re

root=Path(__file__).resolve().parents[1]

main=(root/'native/main.cpp').read_text('utf-8-sig')

auto=(root/'native/sleep_recovery_auto.h').read_text('utf-8-sig')

recovery=(root/'native/sleep_steam_recovery.h').read_text('utf-8-sig')

checks=[]

def check(name,value):

    assert value,name

    checks.append(name)

    print('PASS',name)

start=main.index('const ULONGLONG eventFileTime = sgKernelPowerEventFileTime(xml);')

end=main.index('return ERROR_SUCCESS;',start)

callback=main[start:end]

witness=re.search(r'sleepRecoveryAutoRecordKernelWake\(currentPowerGeneration\(\), eventId, reason,\s*sgKernelPowerEventDataInt\(xml, "TargetState"\), eventFileTime\);',callback)

check('kernel subscriber passes parsed original UTC event time, not fallback collection time',bool(witness))

check('kernel witness recorded before shared wake message is posted',bool(witness) and witness.start()<callback.index('PostMessageW(g_hwnd, WM_SG_S0_WAKE'))

record=auto[auto.index('static void recordKernelWake('):auto.index('static void arm(')]

check('subscriber records bounded Steam evidence without arming or dispatching workers',not any(s in record for s in ['poolSubmit','notify();','g_sgWorkCv','g_sleepRecoveryAutoPending.store','resumeInputContext']))

check('kernel proof accepts only final button S0 exit or explicit S4 Target5', '(eventId==507&&reason==1)||(eventId==107&&targetState==5)' in record)

armProof=auto[auto.index('static void arm('):auto.index('static void run(')]
check('raw kernel candidate alone cannot authorize: final arm rebinds actual captured generation/epoch and boundary',all(s in armProof for s in ['original.generation==gen','original.configEpoch==epoch','kernelWake.eventFileTime>original.powerBoundaryFileTime','kernelWake.eventFileTime<=sgNowFileTime()']) and 'eventFileTime>nowFileTime' in record)
check('a synchronous prepared boundary outranks a completed prior capture at kernel proof collection','SleepSteamRecovery::wakeBoundaryFor(gen)' in record and 'preparedPending&&prepared.configEpoch==epoch' in recovery)
check('proof follows only the exact immediately transferred prepared boundary','kernelWake.generation+1==gen' in armProof and 'original.boundaryGeneration==kernelWake.generation' in armProof)
check('capture-ready promotion still requires a prior explicit wake request, not capture alone','state.phase=="wake-proof-wait"' in armProof and 'if(requested)arm(gen)' in armProof)


s0=main[main.index('case WM_SG_S0_WAKE:'):main.index('case WM_SG_S4_WAKE:')]

check('confirmed S0 wake arms Steam even if shared duplicate classification returns early',bool(re.search(r'sgHandleModernStandbyWake\(true\);\s*sleepRecoveryAutoArm\(currentPowerGeneration\(\)\);',s0)))

check('confirmed retry-abort wake preserves Steam entry after owned recovery queued',bool(re.search(r'sgAbortSleepIntent\("s0-user-power-button-wake"\);\s*sleepRecoveryAutoArm\(currentPowerGeneration\(\)\);',s0)))

s4=main[main.index('case WM_SG_S4_WAKE:'):main.index('case WM_SG_RESUME_GAME_FOCUS:')]

check('S4 has independent final-wake Steam entry on S0-capable devices',bool(re.search(r'handlePowerResumeNotification\(SgWork::WakeHibernate, "kernel-s4-wake"\);\s*sleepRecoveryAutoArm\(currentPowerGeneration\(\)\);',s4)))

check('traditional RESUMESUSPEND still routes to Steam gate',bool(re.search(r'sgBeginWakeGameRecovery\(expectedGeneration\);\s*sleepRecoveryAutoArm\(expectedGeneration\);',main)))

arm=auto[auto.index('static void arm('):auto.index('static void run(')]

check('S0 candidates do not consume seen generation before kernel confirmation','state.seenGeneration=gen;' in arm[arm.index('}else{'):])

check('S3 explicitly retains non-kernel admission','pbt-resume-suspend-s3' in arm and 'deferred=modernStandby&&!confirmed' in arm)

check('capture pins original boundary FILETIME before delayed kernel collection','value.powerBoundaryFileTime=sgNowFileTime();value.client=prepareSession();value.boundary="power-suspend-notification"' in recovery)

check('reports identify confirmed-wake implementation and export waiting state','wakeAdmission' in auto and 'waiting-confirmed-wake' in auto)

check('new gate retains separate false game-input and Continue verification','{"gameInputVerified",false},{"continueButtonVerified",false}' in arm)

run=auto[auto.index('static void run('):auto.index('static void tick(')]
check('completed RETURN foreground loss suppresses writes without ending PID-bound observation','const bool postReturnForegroundWait=verify&&inputAttempted&&' in run and 'if(!nativeOriginalOwner&&!postReturnForegroundWait&&(flag(steamRecovery,"repairable")||flag(steamRecovery,"contactRepairable"))&&!listAttempted)' in run and 'finish(gen,epoch,"blocked","steam-input-context-not-retained"' not in run)
check('pending native original-window receipt does not borrow later UI foreground as identity', 'windowReturnObserved already certifies the native exact-PID postflight' in run and 'SleepSteamRecovery::sameClientEvidence(before.value("steamClient"' in run)
check('read-only wait keeps original PID, client, tracker and actual controller samples',all(x in run for x in ['"post-return-foreground-wait"','"inputControllers"','{"tracker",report.value("tracker"','{"steamClient",report.value("steamClient"']))
check('foreground wait remains bounded by the original 60s deadline and does not promote gameplay/Continue','postReturnObservation' in auto and 'returnReplayAllowed' in auto and 'kWakeObserveLimitMs' in run and 'gameInputVerified",false' in run and 'continueButtonVerified",false' in run)
foreground=recovery[recovery.index('static Foreground foreground('):recovery.index('static bool allowedForeground(')]
check('Steam UI admission uses exact executable leaf rather than the production extension-stripping game stem','normalized.substr(separator+1)' in foreground and 'base=="steamwebhelper.exe"||base=="gameoverlayui.exe"' in foreground and 'W2U(sgBaseName(path))' not in foreground and '||!f.created' in foreground)
fixture=(root/'tools/sleep_steam_only_selftest.py').read_text('utf-8-sig')
check('Steam foreground harness consumes the actual shared production name normalizer','#include "monitor_identity.h"' in fixture and 'return nativeIdentityNormalizeName(s);' in fixture and 'return nativeIdentityNormalizeName(path);' in main)
check('same Steam-only subscriber receives actual entering506 before exit, without changing shared sleep handlers','if (entering || leaving || hibernateWake)' in callback and 'sleepRecoveryAutoRecordKernelWake' in callback)
check('final admission refuses expired proof and any exit before newer true entry',all(x in auto for x in ['proofMaximumAgeMs','proofFresh','kernelSleepEntry.eventFileTime','proofNow-kernelWake.eventFileTime<=ymcc::sleepsteam::kWakeObserveLimitMs']))
check('read worker cannot act on a proof superseded by later actual entry','!proofSupersededLocked()' in auto and 'steam-new-entry-after-action-no-replay' in auto)
check('entry callback never refunds reservations; tick may surrender only proof-only zero dispatches','!state.listAttempted&&!state.inputAttempted&&state.inputDispatches==0' in auto and 'seenBeforeAdmission' in auto)
capture=recovery[recovery.index('struct CaptureGameRules {'):recovery.index('struct Captured {')]
check('sleep capture validates exact native tracker identity before pinning preferred PID','preferredBinding.status=="tracked"' in capture and 'game.created>boundary' in capture and 'ProcessIdToSessionId' in capture)
check('Steam-only fallback rejects multiple games or incomplete tracker instead of using current UI focus','eligible!=1' in capture and 'flag(tail.meta,"truncated")' in capture and 'GetForegroundWindow' not in capture and 'steamLiveRequest' not in capture)
check('capture diagnostics separate preferred MSI candidate from selected game and prohibit later retarget','preferredPid' in capture and 'selectedPid' in capture and 'postWakeRetargetAllowed' in capture and 'targetSelection' in recovery)
check('fallback preserves existing explicit exclude/whitelist policy and never changes shared game target',all(x in capture for x in ['sgExcludes()','sgGameWhitelist()','sgNameExcludedBy(name,rules.whitelist)','nativeMonitorExcluded(name)']) and 'nativeValveCommit' not in capture)
check('both preferred and reverse joins use the existing production one-PID YMCC detector', 'nativeScanGameOnePid(game.pid,L"",rules.excludes,rules.whitelist,rules.configured,&detected)' in capture and 'ymccValidated(preferred,validation)' in capture and 'ymccValidated(game,checked)' in capture)
check('read-only join never elects/publishes or pauses the shared target', all(x not in capture for x in ['nativeValvePublish(', 'nativeDetectGame(', 'nativeValveAcquire', 'nativeValveCommit', 'sgPausePid(']))
check('a joined binding compares exact detector PID/birth/path and stable shared rule epoch', 'sameGameIdentity(game,identity)' in capture and 'g_gameRulesEpoch.load(std::memory_order_acquire)!=rules.epoch' in capture)
check('diagnostics distinguish directional validation from equality of the current shared candidate', all(x in capture for x in ['jointBindingVerified','sharedCandidateMatched','different-instance','ymccPidValidation','ymccSteamJoin']))
check('native regression harness compiles the actual shared one-PID implementation rather than a permissive recognizer mock', "main.index('enum class NativeOnePidResult {')" in fixture and "replace('__ONE_PID__',one_pid)" in fixture)
check('joint binding is not a fake gameplay or Continue success', 'gameInputVerified",false' in capture and 'continueButtonVerified",false' in capture and 'sharedCandidateModified",false' in capture)
gate=auto[auto.index('static bool wakeOperationAllowedLocked('):auto.index('static unsigned pidWindowAttempts(')]
check('Resuming Steam readiness requires admitted native final witness, not just shared lifecycle', all(x in gate for x in ['PowerLifecycle::Resuming','kernelWake.uiWakeRequested','kernelWake.requestGeneration!=gen','kernelWake.eventFileTime!=event','proofSupersededLocked()','kernel-power-507-reason-1','kernel-power-107-target-5']))
check('Steam operation alternative is bounded and excludes real suspend, retry, stale proof and disabled/superseded generations', all(x in gate for x in ['sgSleepRetryActive()','sleepSteamRecoveryAllows(gen,epoch)','now-state.armed<ymcc::sleepsteam::kWakeObserveLimitMs','utc-event<=ymcc::sleepsteam::kWakeObserveLimitMs*10000ULL','event<=boundary']))
check('Steam admission never changes shared hardware lifecycle/Ready gate or pause owners', all(x not in gate for x in ['g_powerLifecycle.store','openHardwareWriteGate','hardwareWriteAllowed','InputHost','sgResumeSleepTarget','nativeValvePublish']))
check('both native Steam input and list action preflights plus postflight use the same alternative gate', recovery.count('sleepSteamWakeOperationAllows(generation,configEpoch)')>=5 and 'if(!sleepSteamWakeOperationAllows(generation,configEpoch))return reject("steam-input-resume-cancelled")' in recovery and 'if(!sleepSteamWakeOperationAllows(generation,configEpoch))return reject("steam-recovery-cancelled")' in recovery)
check('first read, pause-release wait and continued verification consume Steam usability rather than hard shared Ready', 'now-state.armed>=1500&&steamWakeUsable' not in auto and auto.count('else if(steamWakeUsable)')>=2 and 'if(!steamWakeUsable)failure="steam-power-not-ready"' in auto)
check('queued read worker revalidates Steam operation readiness before entering CDP', 'state.phase==phase&&!proofSupersededLocked()&&wakeOperationAllowedLocked(gen,epoch)' in auto)
check('readiness evidence is exported and attached to automatic result/settling timeline with unverified gameplay', 'evidence["wakeReadiness"]=state.wakeReadiness' in auto and '{"wakeReadiness",state.wakeReadiness}' in auto and 'sleep-steam-wake-readiness' in auto and 'sharedStateModified",false' in gate and 'gameInputVerified",false' in gate)
check('R50 marker preserves native owner/send guards, captured original identity, contact rescope and bounded wake observation', 'steam-wake-round50-native-record-diagnostic' in auto and 'steam-wake-round50-native-record-diagnostic' in recovery)
check('Steam inspect retries only transient account/session reads and keeps original session fail-closed', all(x in recovery for x in ['sessionRetry','initialReadReason==\"steam-account-changed\"||initialReadReason==\"steam-session-changed\"','Sleep(75)','sameSession(original.client)','boundedReadOnly']))
check('session retry evidence is exported in both Steam inspection facts and report', 'steamSessionRetry' in recovery and 'steamSessionState' in recovery)
check('R27 uses the production native process/account wrapper in regression fixtures', "main.index('static bool sleepSteamLiveProcessMatches(')" in fixture and "replace('__LIVE_WRAPPER__',live_wrapper)" in fixture)
check('every sleep action includes the captured process witness', 'auto result=liveRequest(original.client,request,admit)' in recovery and 'auto result=steamLiveRequest(original.client.account' not in recovery)
check('queued capture reuses prepared account without rediscovery', 'if(!early){value.client=prepareSession();' in recovery)
check('account capture never reads unverified observer account and exposes redacted binding provenance', 's.account=observed.account' not in recovery and all(x in recovery for x in ['accountSource','accountPinned','foreignRegistryAccountIgnored','sleep.session.inspect']))
cdp=(root/'native/steam_live_cdp.h').read_text('utf-8-sig')
check('both ownership rechecks keep the exact captured Steam process', cdp.count('stillOwned(steam, endpoint, expectedSteamPid)')==2 and 'listeners(steam, expectedSteamPid)' in cdp)
check('fixed account discovery uses a short read budget and cannot queue behind live writes', 'discovery ? 400 : 0' in main and 'if (!liveLock.try_lock())' in main and 'readOperation == "sleep.session.inspect"' in cdp)
package=(root/'package.json').read_text('utf-8-sig')
release=(root/'tools/package-release.ps1').read_text('utf-8-sig')
check('formal release entrypoint rebuilds the mainline before packaging', '"release": "pnpm run build && pnpm run package"' in package)
identityGuards=[m.start() for m in re.finditer(re.escape('Assert-ExportBuildIdentity -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot'),release)]
packageIdentity=release.index('$exportPackageIdentity = Write-ExportPackageIdentity')
archiveIdentity=release.index('$exportArchiveReceipt = Assert-ExportArchiveIdentity')
publish=release.index('foreach ($path in $releaseItems) { Backup-ExportReleaseItem')
finished=release.index("Write-Output 'PACKAGE_OK'")
check('package staging and final publication keep the build identity gate', any(p<packageIdentity for p in identityGuards) and any(archiveIdentity<p<publish for p in identityGuards) and any(publish<p<finished for p in identityGuards))
check('rejected-PID follow-up is explicitly read-only and non-authoritative', all(x in recovery for x in ['observeRejectedYmccGame','"sampledAfterValidation",true','"authoritative",false','"admissionGranted",false','"validatorReasonAvailable"']))
check('follow-up cannot grant admission or replace the rejected candidate', 'else if(verdict!=NativeOnePidResult::Ok&&!expired())' in capture and 'return exact;' in capture and '"rejectionObservation"' in capture)
check('refused Steam candidate preserves tracker and shared-instance evidence', all(x in capture for x in ['checked["appid"]=proof.appid','checked["steamTrackerStatus"]=proof.status','sharedCandidateIdentityMatched','ownWindowObserved']))
check('early stable context cannot finalize before the final observation interval', 'complete=stable&&elapsed>=ymcc::sleepsteam::kWakeFinalizeMinimumMs' in auto and 'steam-wake-late-disconnect-observation' in auto)
check('late observation keeps the existing write deadline and polling budget', 'kWakeFinalizeMinimumMs=kWakeObserveLimitMs-kWakePollMs' in (root/'native/sleep_steam_settle.h').read_text('utf-8-sig') and 'GetTickCount64()-state.armed<ymcc::sleepsteam::kWakeObserveLimitMs' in auto)

transport=(root/'native/steam_live_cdp.h').read_text('utf-8-sig')
check('cancelled native live request has explicit zero-send stage before WebSocket dispatch', transport.index('{"stage", "native-live-pre-dispatch"}') < transport.index('        sent = true;') and '{"requestDispatched", false}' in transport)
check('only locally recorded foreground veto can mark an unsent cancellation recoverable', all(x in recovery for x in ['nativeAdmissionReason=="steam-input-foreground-changed"','result.value("stage",std::string())=="native-live-pre-dispatch"','result.value("requestDispatched",json())==json(false)','result["nativePreDispatchCancelled"]=nativeForegroundCancelled']))
check('owner requires exact native zero-send proof alongside unchanged PID/session/capture/pause', all(x in auto for x in ['const bool nativeNoSend=flag(inputReceipt,"nativePreDispatchCancelled")','inputReceipt.value("requestDispatched",json())==json(false)','inputReceipt.value("nativeAdmissionReason",std::string())=="steam-input-foreground-changed"','||nativeNoSend']))
check('reservation cap stays three while exhausted native zero-send cases retain only observation', 'if(state.inputDispatches<3){state.inputAttempted=false;inputDeferred=true;}' in auto and 'else if(nativeNoSend)inputDeferred=true;' in auto)
check('native admission diagnostics distinguish identity, account/session, pause, foreground and lifecycle vetoes', all(x in recovery for x in ['steam-input-worker-expired','steam-input-recovery-cancelled','steam-input-wake-admission-revoked','steam-original-game-identity-changed','steam-native-session-changed','steam-original-game-paused','steam-input-foreground-changed']))
delayed=auto[auto.index('static bool sameOriginalWindowReadbackTransaction('):auto.index('static void run(')]
check('delayed post-action observation needs exact failed-stage native receipt and immutable original PID/birth', all(x in delayed for x in ['steam-original-window-not-confirmed','sleep-input-resume','nativeAdmissionReason','originalIdentityStable','number(beforeGame,"pid")==pid','text(afterGame,"created")==created','jointBindingVerified']))
check('fresh native original window readback preserves Steam session and pause guards', all(x in delayed for x in ['originalWindowFocused','text(fg,"role")=="original-game"','number(fg,"pid")==number(inputReceipt,"originalPid")','text(fg,"created")==text(inputReceipt,"originalCreated")','sameClientEvidence','waitForSleepRelease']))
check('delayed observation is separate from the rejected receipt and does not refund or mutate an action', all(x in auto for x in ['nativePostReturnReadback','receiptAcceptanceUnchanged','delayedOriginalWindowObserved','const bool pending=delayedWindow||']) and all(x not in delayed for x in ['state.inputAttempted=false','accepted"]=true','resumeInputContext(','reconcile(','SetForegroundWindow']))
check('later foreground changes after delayed readback retain read-only waiting and unchanged rescope gate', '(flag(priorReturn,"windowReturnObserved")||state.postReturnDelayedWindowObserved)' in auto and 'if(!nativeOriginalOwner&&!postReturnForegroundWait&&(flag(steamRecovery,"repairable")||flag(steamRecovery,"contactRepairable"))&&!listAttempted)' in auto)
check('delayed readback is logged as observation rather than action acceptance', 'flag(receipt.value("nativePostReturnReadback",json::object()),"readOnly")' in auto and 'delayedOriginalWindowObserved",postReturnDelayedWindowObserved' in auto)

tracking=(root/'native/sleep_steam_tracking.h').read_text('utf-8-sig')
check('native primary parser scans the complete byte-bounded snapshot within original Steam birth and capture boundary', all(x in tracking for x in ['nativePrimaryProcessInventoryText','at<earliest||at>latest','nativePrimaryMaximumBytes=2U*1024U*1024U','bytes.size()>nativePrimaryMaximumBytes','result.rows.size()>32','0.0.0.0:0']))
primaryRead=recovery[recovery.index('static NativePrimaryRead readNativePrimary('):recovery.index('static json nativePrimaryJoin(')]
check('native primary read wrapper does not clip later primary updates or removals by line count', 'nativePrimaryProcessInventoryText(tail.bytes' in primaryRead and '20000' not in primaryRead and 'flag(tail.meta,"truncated")' in primaryRead)
check('malformed, removed, nonlocal and incomplete native primary evidence cannot authorize the original', all(x in recovery for x in ['!tracker.complete||!primary.inventory.complete','if(row.removed)','if(!row.local)','row.pid==pid&&originalTracked']))
check('unobserved primary fallback requires one exact tracker member and is not called verified', all(x in recovery for x in ['members==1&&originalTracked','unique-tracker-native-primary-unobserved','nativePrimaryVerified",false']))
check('pre-sleep preferred and reverse joins require native primary allowance before YMCC one-PID validation', all(x in recovery for x in ['flag(preferredPrimary,"originalAllowed")','if(!flag(rowPrimary,"originalAllowed"))','nativePrimaryRejectedCandidates','!nativePrimaryAllowsOriginal(client,chosen,binding.appid,']))
check('R34 uses one cumulative1000ms capture budget with separate rule/read/parse diagnostics and unchanged original boundary authority', all(x in recovery for x in ['captureReadBudgetMs=1000','maximumMs",captureReadBudgetMs','now-started>captureReadBudgetMs','ymcc-rules-snapshot','tracker-read','tracker-parse','stageElapsedMs','candidate-identity-and-ymcc-validation','postWakeRetargetAllowed",false']))
check('fresh original primary is required at input admission, postflight and list rescope', recovery.count('nativePrimaryAllowsOriginal(original.client,original.game,')>=2 and 'nativePrimaryAllowsOriginal(client,game,originalAppid,' in recovery and 'steam-native-primary-game-changed' in recovery)
check('wake primary proof is recorded and contributes to current original joint verification', all(x in recovery for x in ['{"nativePrimary",primary}','flag(original.targetSelection,"jointBindingVerified")&&flag(primary,"originalAllowed")','nativePrimaryAllowsOriginal",flag(primary,"originalAllowed")']))
check('original verified primary proof is not downgraded to legacy unique tracker authority after capture', all(x in recovery for x in ['primaryWasVerified&&!flag(primary,"nativePrimaryVerified")','steam-native-primary-proof-lost','requireVerified||flag(proof,"nativePrimaryVerified")']))
owner=(root/'native/input_chain_owner_evidence.h').read_text('utf-8-sig')
check('R38 native owner helper included before boot use', '#include "input_chain_owner_evidence.h"' in main and main.index('#include "input_chain_owner_evidence.h"')<main.index('ymcc::inputChainOwner::readSelf()'))
check('R38 self owner read only occurs at existing boot boundary', main.count('ymcc::inputChainOwner::readSelf()')==1 and 'appendNativeLifecycleLog("boot-single-instance-acquired",\n            {{"nativeOwner", ymcc::inputChainOwner::readSelf()}})' in main)
check('R38 optional boot detail has original no-detail fallback', 'catch (...) {\n        appendNativeLifecycleLog("boot-single-instance-acquired");' in main)
check('R38 owner query is actual self GetProcessTimes and exact FILETIME', all(x in owner for x in ['GetProcessTimes(GetCurrentProcess()','value.LowPart=created.dwLowDateTime','value.HighPart=created.dwHighDateTime','std::to_string(raw.creationFiletime)']))
check('R38 owner projection failure and missing birth cannot promote', all(x in owner for x in ['raw.querySucceeded&&raw.pid!=0&&raw.creationFiletime!=0','observed?"observed":"unknown"','catch(...) { return Json(nullptr); }']))
check('R38 owner is local-only and never proves game or Steam consumption', all(x in owner for x in ['native-self-process-identity','GetProcessTimes-self','{"gameInputUnverified",true}','{"steamConsumptionProven",false}']))
check('R38 adds no self OS query at InputChain frame counting', 'readSelf' not in main[main.index('struct InputChainCounters {'):main.index('// GP-926R17 §4：IntentHost')])

command=(root/'native/input_chain_command_evidence.h').read_text('utf-8-sig')
host=(root/'InputHost/Program.cs').read_text('utf-8-sig')
lifetime=(root/'InputHost/TargetCreationLifetimeEvidence.cs').read_text('utf-8-sig')
check('R39 command diagnostic header is included', '#include "input_chain_command_evidence.h"' in main)
check('R39 lifecycle capture comes from actual merged envelope before pipe write', main.index('lifecycleCommandEvidence = ymcc::inputChainCommand::capture(request, g_inputHostHostInstanceId)')<main.index('const bool written = connected && inputHostPipeIo(pipe, true'))
check('R39 command diagnostic capture excludes SUBMIT_FRAME', 'if (ymcc::inputChainCommand::supported(command))' in main and all(x in command for x in ['PREPARE_TARGET','RECREATE_TARGET','RELEASE_TARGET','SHUTDOWN']) and 'std::strcmp(c,"SUBMIT_FRAME")' not in command)
check('R39 projection preserves request protocol namespace and consumption unknown', all(x in command for x in ['native-lifecycle-command-receipt','input-host-bound-protocol;not-current-shared-power','"gameInputUnverified",true','"steamConsumptionProven",false']))
check('R39 accepted and rejected existing rows append diagnostic separately', 'append(acceptedLine,lifecycleCommandEvidence,response,true,expectedStatus)' in main and 'append(rejectedLine,lifecycleCommandEvidence,response,false,expectedStatus)' in main)
check('R39 target lifetime changes at SDK object create and closes before release', 'Created(targetCreationAttempt, _controller)' in host and 'private bool ReleaseLocalLocked(out string detail)\n        {\n            _targetCreationLifetime?.ReleaseAttempt();' in host)
check('R39 failed prepare has finally-closing old diagnostic token', 'if (!targetCreationPrepared) _targetCreationLifetime?.Failure(targetCreationAttempt);' in host)
check('R39 host reply append is bounded with original response fallback', 'TryAppend(response, envelope.Command, MaxMessageBytes) ?? response' in host and all(x in lifetime for x in ['MaximumEvidenceBytes = 2048','return originalResponse','LifecycleCommand(command)']))
check('R39 host evidence has no OS incarnation or Steam ownership', all(x in lifetime for x in ['host-local-SDK-object-creation-lifetime-diagnostic-only','osIncarnationProven','sdkOwnershipProven','steamConsumptionProven','gameInputUnverified']))
check('R39 host helper does not enter submit frame method', '_targetCreationLifetime' not in host[host.index('private bool TrySubmitFrame('):host.index('private bool TrySubmitFrame(')+2500] if 'private bool TrySubmitFrame(' in host else host.count('_targetCreationLifetime?.Created')==1)
link=(root/'native/steam_game_link_runtime.h').read_text('utf-8-sig')
focus=(root/'native/steam_native_focus_feedback.h').read_text('utf-8-sig')
live=(root/'native/steam_live.js').read_text('utf-8-sig')
check('R41 native feedback is requested only by independent original-instance link read', '{"operation","sleep.inspect"},{"appids",json::array({appid})},{"runningLinkRead",true},{"nativeFocusFeedbackRead",true}' in link and 'nativeFocusFeedbackRead' not in recovery and 'nativeFocusFeedbackRead' not in auto)
check('R41 native callback comparisons cannot authorize linked or wake actions', 'compareNativeFocus(read,appid,game.pid)' in link and 'diagnostic-only-no-admission' in focus and 'originalInstanceReattached",false' in focus)
check('R41 asynchronous native read retains original identity/epoch postflight', link.index('compareNativeFocus(read,appid,game.pid)')<link.index('const auto mergeIdentity')<link.index('originalIdentityBracketMatched')<link.index('state.observe(e)'))
check('R41 read flag rejects setters and malformed flags before subscription', "typeof request.nativeFocusFeedbackRead !== 'boolean' || request.operation !== 'sleep.inspect' || request.runningLinkRead !== true" in live)
check('R41 temporary native subscription is bounded and own cleanup failures quarantine new registrations', all(x in live for x in ['RegisterForFocusChangeEvents(observe)','unregister.call(subscription)','__ymccNativeFocusReadQuarantine','native-focus-callback-timeout','},1000)','if(result.callbackReceived)complete()']))
check('R41 skips configuration RPCs only on typed independent read and refreshes UI snapshot after callback', 'inputSnapshot(request.runningLinkRead!==true)' in live and 'request.runningLinkRead===true?snapshot():before' in live)
check('R42 exports computed transport contact after state observation', 'detail[\"transportConnected\"]=s.transportConnected' in link and link.index('state.observe(e)') < link.index('detail[\"transportConnected\"]=s.transportConnected') < link.index('publish(c,std::move(detail)'))
check('R43 preserves transport contact through steamGameLink.get readout', 'const bool transportConnected=flag(result,\"transportConnected\")' in link and 'result[\"transportConnected\"]=transportConnected' in link and link.index('const bool transportConnected=flag(result,\"transportConnected\")') < link.index('result.update(base())') < link.index('result[\"transportConnected\"]=transportConnected'))
check('R43 clears stale transport contact on freshness, user-off and epoch cancellation', 'if(!flag(result,\"fresh\")){result[\"contactConnected\"]=false;result[\"transportConnected\"]=false;' in link and 'current-link-read-not-fresh' in link and 'user-disabled' in link and 'config-epoch-changed' in link)
check('R43 records transport contact edges in lifecycle history', 'const bool previousTransportConnected=flag(c->snapshot,\"transportConnected\")' in link and 'const bool transportChanged=previousTransportConnected!=transportConnected' in link and 'if(logChange||contactChanged||transportChanged)' in link)
check('R49 keeps the initial original-window miss as exact-transaction read-only observations', all(x in auto for x in ['sameOriginalWindowReadbackTransaction','pendingOriginalWindowReadback','waitingInitialOriginalWindow','initialWindowReadbackSamples','nativeWindowReadbackWait','receiptAcceptanceUnchanged']))
check('R49 late polls can observe the original window without changing its rejected RETURN receipt', 'delayedOriginalWindowReadback(priorReturn,state.before,report)' in auto and 'state.postReturnDelayedWindowObserved=true' in auto and 'nativePostReturnReadback' in auto)
check('R49 records primitive native results without taking admission or input authority from them', all(x in live for x in ['const nativeValue=await bounded(SteamClient.Apps.RaiseWindowForGame(gameid))','nativeNumber','diagnostic-only-no-admission','nativeMeaningVerified:false','booleanValue:nativeBoolean?nativeValue:null']) and 'result.value("nativeReturn",json::object())' in recovery)
identity=(root/'native/steam_native_record_identity.h').read_text('utf-8-sig')
record=(root/'native/steam_native_record_diagnostic.h').read_text('utf-8-sig')
winrecord=(root/'native/steam_native_record_win32.h').read_text('utf-8-sig')
bind=recovery[recovery.index('static void bindNativeRecordIdentity('):recovery.index('static json nativeRecordDiagnostic(',recovery.index('static void bindNativeRecordIdentity('))]
prefetch=recovery[recovery.index('static json prefetchNativeRecordIdentity('):recovery.index('static void bindNativeRecordIdentity(')]
check('R50 strips complete private identity before returning any native public reply', recovery.index('auto observed=ymcc::steamrecordidentity::consume(reply)') < recovery.index('if(privateIdentity)*privateIdentity=std::move(observed)') < recovery.index('return reply;') and identity.index('reply.erase(it)') < identity.index('if(!privateValue.is_object())'))
check('R50 private flag permits reads only and requires known full identity getter', "!['sleep.session.inspect','sleep.inspect'].includes(request.operation)" in live and 'ConvertTo64BitString(){return this.m_ulSteamID.toString()}' in live and '(BigInt(full)&0xffffffffn)!==BigInt(account)' in live)
check('R50 exact running-link proof brackets diagnostic before publication', all(x in link for x in ['e.tracker==TrackerStatus::Exact&&e.primary==PrimaryStatus::Exact&&e.validator==ValidatorStatus::Accepted','prefetchNativeRecordIdentity(client,game,appid,privateRecordIdentity','nativeRecordReadGeneration=currentPowerGeneration()']))
check('R50 capture only adopts earlier exact prefetch and never waits for identity RPC or probes memory', all(x in bind for x in ['beforeCapture(prior.witness,value.boundaryGeneration,value.configEpoch,value.at,value.powerBoundaryFileTime)','prior.game.created!=value.game.created','prior.client.account!=value.client.account','jointBindingVerified']) and not any(x in bind for x in ['liveRequest(', 'observe(', 'ReadProcessMemory', 'Sleep(']))
check('R50 witness rejects future reads, different owner and cache older than60s', all(x in identity for x in ['w.generation==generation','w.epoch==epoch','w.observedTick<=captureTick','captureTick-w.observedTick<=60000','w.observedFileTime<=captureFileTime']))
check('R50 diagnostic errors cannot escape optional sampling or dereference null capture input', 'catch(...)' in prefetch and 'row.is_object()' in prefetch and 'captured.inputBefore.is_object()?captured.inputBefore:json::object()' in recovery and 'catch(...) {return unavailable("native-record-original-read-unavailable")' in recovery)
check('R50 fixed module diagnostic has no process writing, unknown calls, injection or device changes', all(x not in record+winrecord for x in ['WriteProcessMemory','CreateRemoteThread','LoadLibrary','VirtualAllocEx','DeviceIoControl','SetForegroundWindow']) and all(x in winrecord for x in ['ReadProcessMemory','verifiedDiskProfile','exactProcess','fileStamp','loadedImageFullyHashed']))
check('R50 record output is explicitly diagnostic and never consumed by action owner or settlement', 'diagnostic-only-no-admission' in record and not any(x in auto+(root/'native/sleep_steam_settle.h').read_text('utf-8-sig') for x in ['nativeRecordDiagnostic','nativeRecordPrefetch','steamrecord::observe']))
check('R50 private record probe does not claim atomicity, lease or physical pre-sleep identity', all(x in record for x in ['atomicSnapshot','abaExcluded','loginGenerationLease']) and 'result["physicalPreSleepIdentityVerified"]=false' in recovery and 'nativeRecord["uiAndNativeAtomic"]=false' in recovery)
check('R50 settled bounded native return survives post-call veto with no invented acknowledgment', 'const nativeReturn=settledNativeReturn=' in live and '...(settledNativeReturn ? { nativeReturn: settledNativeReturn } : {})' in live and 'actionAcknowledged' not in live[live.rindex('  } catch (error) {'):])
check('R50 native host postflight retains bounded settled evidence on original-session refusal without ACK', 'steamreturnevidence::bounded(result)' in main and 'refused["nativeReturnEvidenceVersion"]="steam-native-return-evidence-v1"' in main and 'if (operation == "sleep.input-resume")' in main and 'return refused;' in main)
print(f'Steam final-wake source wiring: {len(checks)} checks PASS')

