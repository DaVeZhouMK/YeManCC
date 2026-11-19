#!/usr/bin/env python3
"""Actual focus consumers with scalar fake ports and no real input/UI calls."""
from pathlib import Path
import argparse,ast,datetime,hashlib,json,os,re,subprocess,sys,time
sys.dont_write_bytecode=True
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Testing/YMCC/Evidence/focus-shortcut-input-evidence'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
FUNCTIONS=[
 'static void focusShortcutInputEvidence(',
 'static bool focusLightVirtualIdle(',
 'static void closeHardwareWriteGate(',
 'static bool focusLightNoGameQualified(',
 'static void focusPrepareIsolationBeforeShow(',
 'static void focusEndIsolationOnUi(',
 'static bool focusLightIsolationCurrent(',
 'static bool focusLightIsolationBegin(',
 'static bool focusMainWindowIsForeground(',
 'static bool refocusWebView(',
 'static void nudgeWebViewAfterResume(',
 'static void focusReleaseOwnedTopmost(',
 'static void focusLightHandoffCancel(',
 'static void focusClearSession(',
 'static bool focusIsolationSharedPowerChanged(',
 'static bool focusPrepareSummonSession(',
 'static void focusLightRecheckOnUi(',
 'static void controllerFrontendIsolationRefreshSettings(',
 'static bool gamepadUiInputEligible(',
 'static bool gamepadUiOutputIsolationEligible(',
 'static bool focusLightMayRefocus(',
 'static void focusLightHandoffRequest(',
 'static void focusLightHandoffCompleteOnUi(',
 'static bool focusSummonRefocus(',
 'static void focusLightForegroundChanged(',
 'static bool focusLightReturnTick(',
 'static void focusBeginReturnToPreviousWindow(',
 'static void bringToFront(',
 'static json nativeSummonCandidateSnapshot(',
 'static bool nativeYmccShortcutSummon(',
 'static json nativeAutoRegisterSummonGame(',
 'static const char* nativeGameDiscoveryStateName(',
 'static void nativeValvePublish('
]
RETIREMENT_FUNCTIONS=['static bool focusLightNoGameQualified(', 'static bool focusLightIsolationBegin(', 'static bool focusLightIsolationCurrent(', 'static void focusPrepareIsolationBeforeShow(', 'static void focusEndIsolationOnUi(', 'static bool focusMainWindowIsForeground(', 'static void focusShortcutInputEvidence(', 'static bool focusPrepareSummonSession(', 'static bool nativeYmccShortcutSummon(', 'static bool gamepadUiOutputIsolationEligible(']
MUTATIONS=[
 ('game-before-show-lifecycle','const bool gameOpen = observation && observation->game.pid;','const bool gameOpen = false;','game state was not queried before changing foreground'),
 ('game-before-show-lifecycle','!g_focusIsolationGameOpen.load(std::memory_order_acquire) && observation && !observation->game.pid &&','observation && !observation->game.pid &&','foreground handoff downgraded the known-game channel'),
 ('game-before-show-lifecycle','focusLightVirtualIdle() && (expectedPower ?','(expectedPower ?','enabled or unknown virtual output entered the no-game light route'),
 ('light-foreground-game-transition','    if (presentationEdge || g_msiNativeForegroundDesiredUi.load(std::memory_order_acquire) != msiNativeForegroundUiEligible())\n        msiNativeForegroundUiEvent();\n','    if (presentationEdge) msiNativeForegroundUiEvent();\n','game transition left original physical isolation owner stale'),
 ('light-foreground-entry','} else if (!focusLightIsolationCurrent() && focusLightIsolationBegin()) {','} else if (false) {','foreground-only entry requested physical isolation'),
 ('light-foreground-entry','if (w) focusLightRecheckOnUi(true);','if (w) msiNativeForegroundUiEvent();','window show routed physical isolation before light admission'),
 ('light-foreground-entry','if (LOWORD(w) == WA_INACTIVE) focusLightForegroundChanged();\n        focusLightRecheckOnUi(true);','if (LOWORD(w) == WA_INACTIVE) focusLightForegroundChanged();\n        msiNativeForegroundUiEvent();','window activation routed physical isolation before light admission'),
 ('light-foreground-entry','if(event==EVENT_SYSTEM_FOREGROUND && g_msiNativeForegroundObserverActive.load(std::memory_order_acquire)) {\n        focusLightForegroundChanged();\n        focusLightRecheckOnUi(true);','if(event==EVENT_SYSTEM_FOREGROUND && g_msiNativeForegroundObserverActive.load(std::memory_order_acquire)) {\n        focusLightForegroundChanged();\n        msiNativeForegroundUiEvent();','foreground observer routed physical isolation before light admission'),
 ('light-wrapper','refocusWebView(allowAltFallback && !g_focusSession.lightNoGame)','refocusWebView(allowAltFallback)','light summon injected Alt fallback'),
 ('light-handoff','pending->session == g_focusSessionSequence &&','true &&','stale handoff session acquired focus'),
 ('light-handoff-lifecycle','const auto pending = g_focusLightHandoff;\n    if (pending && pending->cancellation) pending->cancellation->cancel();','const auto pending = std::exchange(g_focusLightHandoff, {});\n    if (pending && pending->cancellation) pending->cancellation->cancel();','cancel lost unretired handoff responsibility'),
 ('light-handoff-admission','!focusLightIsolationCurrent()) {\n                    pending->result','false) {\n                    pending->result','queued stale handoff reached foreground worker'),
 ('light-settings-owner','if (g_hwnd) PostMessageW(g_hwnd, WM_LIGHT_ISOLATION_RECHECK, 0, 0);\n    appendNativeLifecycleLog("controller-frontend-isolation"','steamLightGamepadUiChanged();\n    appendNativeLifecycleLog("controller-frontend-isolation"','settings worker entered UI lease owner'),
 ('proof-expiry','now - observation->qualificationObservedAt <= 30000','true','expired proof admitted'),
 ('proof-inventory','observation->qualificationInventory == ymcc::gameInventory::invalidation.load(std::memory_order_acquire)','true','inventory invalidation admitted'),
 ('proof-cutoff','observation->qualificationObservedAt >= g_focusLightProofNotBefore.load(std::memory_order_acquire)','true','pre-sleep proof admitted after cutoff'),
 ('ticket-source','ticket->source == g_msiMainSourceRevision.load(std::memory_order_acquire)','true','changed source retained ticket'),
 ('refocus-user-choice','if (g_focusSession.lightForegroundAcquired) return focusMainWindowIsForeground();','if (g_focusSession.lightForegroundAcquired) return true;','acquired focus stole user choice'),
 ('return-identity','created != target.processCreated','false','reused process received return focus'),
 ('light-wrapper','if (!lightNoGame) queueSummonGameRegistration(target, generation);','queueSummonGameRegistration(target, generation);','light summon queued registration'),
 ('late-ready',('&& focusLightMayRefocus(GetTickCount64()) &&','if (!focusLightMayRefocus(GetTickCount64())) return false;'),('&& true &&','if (false) return false;'),'late Ready stole user choice'),
 ('stale-registration','if (g_summonRegistrationGeneration.load(std::memory_order_acquire) != summonGeneration ||\n                g_exitRequested.load(std::memory_order_acquire)) return result;',
  'if (g_exitRequested.load(std::memory_order_acquire)) return result;','stale worker wrote whitelist'),
 ('native-ui-eligibility','    if ((focusLightIsolationCurrent() || steamLightGamepadAttemptActive()) && !steamLightGamepadFallbackRequired() &&\n        !radialHasInputOwner() && !customSteamLibraryInputSuppressed()) return false;\n','    (void)steamLightGamepadAttemptActive();\n','qualified light requested new physical borrowing'),
 ('native-ui-eligibility','(focusLightIsolationCurrent() || steamLightGamepadAttemptActive()) && !steamLightGamepadFallbackRequired() &&','(focusLightIsolationCurrent() || steamLightGamepadAttemptActive()) &&','failed Steam lease left native fallback exempt'),
 ('native-ui-eligibility','    if (presentationEdge || g_msiNativeForegroundDesiredUi.load(std::memory_order_acquire) != msiNativeForegroundUiEligible())\n        msiNativeForegroundUiEvent();','    if (presentationEdge || (!g_focusSession.active && g_msiNativeForegroundDesiredUi.load(std::memory_order_acquire) != msiNativeForegroundUiEligible()))\n        msiNativeForegroundUiEvent();','active light failure did not reach native owner'),
 ('native-ui-eligibility','if (inputHostConfiguredPersona() == "disabled" &&\n        GetTickCount64() < (std::max)(acknowledged, lease->initialDeadline)) return;','(void)acknowledged;','failed Steam lease bypassed initial light attempt window'),
 ('native-ui-eligibility',('!g_focusIsolationGameOpen.load(std::memory_order_acquire) && inputHostConfiguredPersona() == "disabled" &&','focusLightNoGameQualified(GetTickCount64(), &lease->proof->sharedPower) &&'),('inputHostConfiguredPersona() == "disabled" &&','sharedPowerLifecycleCurrent(lease->proof->sharedPower) &&'),'game launch retained no-game light grace'),
 ('native-ui-eligibility','lease && lease->nativeOnly == nativeOnly && lease->proof->power == checkedProof->power ?\n                (std::max)(lease->initialDeadline, lease->acknowledgedDeadline.load(std::memory_order_acquire)) : now + kSteamLightGamepadLeaseMs','now + kSteamLightGamepadLeaseMs','virtual/source epoch churn extended the original three-second attempt'),
 ('native-ui-eligibility','request.requestId && request.persona != "disabled" &&','request.requestId == 0 && request.persona != "disabled" &&','pending'),
 ('native-ui-eligibility','!snapshot.available || !snapshot.stopped','!snapshot.stopped','pipe-unknown'),
 ('native-ui-eligibility','ticket->intent == g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) &&','true &&','virtual intent ABA revived an old light ticket'),
 ('native-ui-eligibility','focusLightNoGameQualified(GetTickCount64(), &ticket->sharedPower) &&','focusLightNoGameQualified(GetTickCount64()) &&','shared power revocation retained light authority'),
 ('old-return-timer-new-session','if (!g_focusSession.active || !g_focusSession.returning) return 0;','','old return timer cancelled new non-returning light session'),
 ('old-return-timer-new-session','if (g_focusSession.lightNoGame && (!g_focusSession.active || g_focusSession.returning ||\n                g_focusSession.lightForegroundAcquired)) {\n                KillTimer(g_hwnd, SUMMON_FOCUS_TIMER_ID);\n                return 0;\n            }','','old summon timer cancelled current return'),
 ('late-ready','if (g_focusSession.lightNoGame) g_deferFirstShow = false;','if (g_focusSession.lightNoGame && !g_focusSession.lightForegroundAcquired) g_deferFirstShow = false;','acquired cancellation retained deferred first show'),
 ('valve-publish-order','if (observed && target.generation < observed->generation) return;','(void)observed;','late no-game publication replaced newer positive observation'),
 ('light-wrapper','const bool completed = SUCCEEDED(moved) && current() &&','const bool completed = (static_cast<void>(moved), true) && current() &&','failed MoveFocus declared light foreground acquired'),
 ('light-wrapper','const bool completed = SUCCEEDED(moved) && current() &&','const bool completed = SUCCEEDED(moved) &&','refocus accepted cleared session after MoveFocus'),
 ('light-wrapper','generation == g_webviewGeneration.load(std::memory_order_acquire) &&\n            (!g_focusSession.lightNoGame || focusLightMayRefocus(GetTickCount64()));','generation == g_webviewGeneration.load(std::memory_order_acquire);','refocus continued after proof revocation'),
 ('light-handoff','if (current && !taskViewFocusHandoffPaused() && pending->result.reclaimed && focusMainWindowIsForeground()) {','if (current && !taskViewFocusHandoffPaused() && pending->result.foreground && pending->result.reclaimed && focusMainWindowIsForeground()) {','current foreground handoff rejected stale worker snapshot'),
 ('light-handoff','} else if (sequence == g_focusSessionSequence && focusLightMayRefocus(GetTickCount64())) {\n            rearmRetry();','} else if (sequence == g_focusSessionSequence && focusLightMayRefocus(GetTickCount64())) {\n            (void)sequence;','failed handoff postponed the first light retry'),
 ('refocus-user-choice','if (visible && focusMainWindowIsForeground()) {','if (visible) {','background visible nudge attempted focus'),
 ('refocus-user-choice','if (sequence == g_focusSessionSequence && g_ctrl && focusMainWindowIsForeground())\n            g_ctrl->MoveFocus','if (sequence == g_focusSessionSequence && g_ctrl)\n            g_ctrl->MoveFocus','nudge moved focus after reentrant foreground departure'),
 ('refocus-user-choice','if (sequence == g_focusSessionSequence && g_ctrl && focusMainWindowIsForeground())\n            g_ctrl->MoveFocus','if ((static_cast<void>(sequence), true) && g_ctrl && focusMainWindowIsForeground())\n            g_ctrl->MoveFocus','nudge moved focus after reentrant session clear'),
 ('light-wrapper','generation == g_webviewGeneration.load(std::memory_order_acquire) &&','(static_cast<void>(generation), true) &&','refocus accepted retired WebView generation after MoveFocus'),
 ('light-handoff-lifecycle','const bool settled = operations.settleForeground();','const bool settled = false;','accepted foreground queue was sampled before settlement'),
 ('light-handoff-lifecycle','const bool settled = operations.settleForeground();\n    state = gate(operations);','const bool settled = operations.settleForeground();\n    state = operations.foreground() == operations.request.target ? Code::alreadyForeground : Code::ready;','settlement cancellation accepted foreground'),
 ('game-before-show-lifecycle','candidates.complete = !candidates.hasUnconfirmed;','candidates.complete = true;','unknown candidate completed idle discovery'),
 ('game-before-show-lifecycle','if (status == WorkingSetRead::AccessDenied) return false;','if (status == WorkingSetRead::AccessDenied) continue;','selective access denied skipped full snapshot fallback'),
 ('game-before-show-lifecycle','return error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_FOUND\n            ? NativeOnePidResult::Rejected : NativeOnePidResult::Unavailable;','return NativeOnePidResult::Rejected;','one-PID unknown became a confirmed rejection'),
 ('light-handoff','pending->result.attempted && !g_summonAltTried && IsWindowVisible(g_hwnd)','false && pending->result.attempted && !g_summonAltTried && IsWindowVisible(g_hwnd)','rejected foreground had no bounded keyboard recovery'),
 ('light-handoff','if ((keyboard[key] & 0x80) || (GetAsyncKeyState(key) & 0x8000))','if ((static_cast<void>(key), false))','unsafe keyboard recovery crossed a summon boundary'),
 ('light-handoff','if (inserted == 1)','if (inserted == 3)','partial keyboard recovery did not release Alt or authorized navigation'),
 ('light-handoff',('originPid != origin.originPid ||\n                ymcc::lightforeground::processCreated(originPid) != origin.originCreated)\n                return skipped("keyboard-fallback-skipped-origin-identity");','originPid != origin.originPid ||\n                ymcc::lightforeground::processCreated(originPid) != origin.originCreated)\n                return skipped("keyboard-fallback-skipped-context-changed");'),('false ||\n                ymcc::lightforeground::processCreated(originPid) != origin.originCreated)\n                return skipped("keyboard-fallback-skipped-origin-identity");','false ||\n                ymcc::lightforeground::processCreated(originPid) != origin.originCreated)\n                return skipped("keyboard-fallback-skipped-context-changed");'),'unsafe keyboard recovery crossed a summon boundary'),
 ('light-handoff','!g_summonAltTried && IsWindowVisible(g_hwnd)','IsWindowVisible(g_hwnd)','unsafe keyboard recovery crossed a summon boundary'),
 ('light-handoff','const auto origin = *lightOrigin;','const auto origin = *lightOrigin;\n            if (!g_focusSession.target.valid || g_focusSession.target.hwnd != origin.origin) return skipped("old-return-target-coupling");','filtered or normalized return target blocked exact-origin summon'),
 ('light-handoff','{\"reason\", \"keyboard-fallback-complete\"},','{\"reason\", \"keyboard-fallback-complete\"}, {\"attempted\", true},','keyboard completion log confused requested recovery with actual dispatch'),
 ('game-before-show-lifecycle','if (error != ERROR_INVALID_PARAMETER && error != ERROR_NOT_FOUND) livenessKnown = false;','if (error != ERROR_INVALID_PARAMETER && error != ERROR_NOT_FOUND) livenessKnown = true;','legacy unknown rows completed an empty game observation'),
 ('light-wrapper','const bool lightNoGame = !g_focusIsolationGameOpen.load(std::memory_order_acquire) &&\n        focusLightIsolationCurrent() &&\n        (selectedLightNoGame || g_focusSession.lightNoGame);','const bool lightNoGame = !g_focusIsolationGameOpen.load(std::memory_order_acquire) &&\n        (selectedLightNoGame || g_focusSession.lightNoGame);','revoked show published light skip-registration'),
 ('light-foreground-entry','presentationEdge || g_msiNativeForegroundDesiredUi.load(std::memory_order_acquire) != msiNativeForegroundUiEligible()','g_msiNativeForegroundDesiredUi.load(std::memory_order_acquire) != msiNativeForegroundUiEligible() || presentationEdge','ordinary presentation repeated isolation eligibility'),
 ('native-ui-eligibility','focusLightNoGameQualified(GetTickCount64(), &ticket->sharedPower) &&','focusLightNoGameQualified(GetTickCount64()) && sharedPowerLifecycleCurrent(ticket->sharedPower) &&','light ticket or attempt repeated the current power read'),
 ('native-ui-eligibility','focusLightNoGameQualified(GetTickCount64(), &lease->proof->sharedPower) &&','focusLightNoGameQualified(GetTickCount64()) && sharedPowerLifecycleCurrent(lease->proof->sharedPower) &&','light ticket or attempt repeated the current power read'),
 ('native-ui-eligibility','demand && proofQualified && proof == checkedProof','demand && proofQualified','retired proof published a Steam ON intent'),
 ('native-ui-eligibility','!radialHasInputOwner() && !customSteamLibraryInputSuppressed() &&\n        g_focusLightIsolationTicket.load(std::memory_order_acquire) == intent->proof &&\n        g_steamLightGamepadIntent.load(std::memory_order_acquire) == intent;','!radialHasInputOwner() && !customSteamLibraryInputSuppressed();','retired proof reached Steam from the worker gate'),
 ('native-ui-eligibility','ticket->sharedPower = sharedPower;','ticket->sharedPower = sharedPower;\n    if (!captureSharedPowerLifecycle(ticket->sharedPower)) return false;','light ticket creation repeated the qualifying power capture'),
 ('native-ui-eligibility','g_focusLightIsolationTicket.store(ticket, std::memory_order_release);\n    if (!focusLightIsolationCurrent()) return false;','g_focusLightIsolationTicket.store(ticket, std::memory_order_release);\n    (void)focusLightIsolationCurrent();','light ticket accepted a changed suspend serial after qualification'),
 ('isolation-shared-power-order','    if(!ymcc::sleeppower::powerObservationCurrent(&readSharedSystemPower,power))return false;\n','','superseded power observation cleared current isolation'),
 ('light-no-demand-power-read','!g_exitRequested.load(std::memory_order_acquire) && gamepadUiOutputIsolationEligible() &&\n        focusLightNoGameQualified(GetTickCount64(), &lease->proof->sharedPower) &&','!g_exitRequested.load(std::memory_order_acquire) && focusLightNoGameQualified(GetTickCount64(), &lease->proof->sharedPower) &&\n        gamepadUiOutputIsolationEligible() &&','inactive output ran light qualification'),
 ('light-ui-fallback-reentry','        if (ordinaryTakesOver) g_focusLightFallbackObservation = observation;\n','','unchanged no-game observation reopened light after ordinary fallback'),
 ('light-ui-fallback-reentry','if (observation && observation->qualificationObservedAt == fallback->qualificationObservedAt &&\n        observation->qualificationPower == fallback->qualificationPower &&\n        observation->qualificationRulesEpoch == fallback->qualificationRulesEpoch &&\n        observation->qualificationInventory == fallback->qualificationInventory) return false;',
  'if (observation && observation == fallback) return false;','republished no-game proof reopened light without a fresh scan'),
 ('light-stale-intent-fast-release','g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) == ticket->intent &&\n        ','','already-stale ticket repeated light qualification'),
 ('light-stale-intent-fast-release','        g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) == lease->proof->intent &&\n','','already-stale Steam attempt repeated light qualification'),
 ('light-stale-intent-fast-release','        lease->proof->intent == g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) &&\n','','stale Steam attempt delayed ordinary isolation'),
 ('isolation-coalesced-ready',': std::pair<std::uint64_t,std::uint64_t>{generation, suspendSerial};',': std::pair<std::uint64_t,std::uint64_t>{generation & 0ULL, suspendSerial};','coalesced Ready failed to retire isolation after unknown preflight or a new period'),
 ('light-wrapper','constexpr UINT firstRetryMs = 64;','constexpr UINT firstRetryMs = 80;','first light retry was not scheduled at 64ms'),
 ('light-wrapper','} else if (g_focusSession.lightNoGame && g_summonFocusRetries == 1) {\n                    SetTimer(g_hwnd, SUMMON_FOCUS_TIMER_ID, 80, nullptr);','} else if (g_focusSession.lightNoGame && g_summonFocusRetries == 1) {','failed MoveFocus lost bounded light timer'),
 ('native-ui-eligibility','observation->qualificationObservedAt > g_focusLightSuspendProofAfter.load(std::memory_order_acquire);','true;','same-generation Ready before UI projection reused pre-suspend no-game proof'),
 ('native-ui-eligibility',('        focusLightVirtualIdle() && (expectedPower ?', ' : sharedPower)) &&\n        observation->qualificationObservedAt > g_focusLightSuspendProofAfter.load(std::memory_order_acquire);'),('        observation->qualificationObservedAt > g_focusLightSuspendProofAfter.load(std::memory_order_acquire) &&\n        focusLightVirtualIdle() && (expectedPower ?', ' : sharedPower));'),'sleep crossing the shared power read retained pre-suspend no-game proof'),
 ('light-handoff','SetTimer(g_hwnd, SUMMON_FOCUS_TIMER_ID, delay, nullptr);','(void)delay; SetTimer(g_hwnd, SUMMON_FOCUS_TIMER_ID, 80, nullptr);','failed handoff postponed the first light retry'),
 ('light-handoff','} else if (g_focusSessionSequence == sequence && focusLightMayRefocus(GetTickCount64())) {\n            rearmRetry();','} else if (g_focusSessionSequence == sequence && focusLightMayRefocus(GetTickCount64())) {\n            SetTimer(g_hwnd, SUMMON_FOCUS_TIMER_ID, 80, nullptr);','keyboard recovery postponed the first light retry'),
]
def sha(data):return hashlib.sha256(data).hexdigest()
def write(path,data):
 with path.open('xb') as stream:stream.write(data if isinstance(data,bytes) else data.encode('utf-8'))
def save(path,value):write(path,json.dumps(value,ensure_ascii=False,indent=2)+'\n')
def replace_once(text,before,after):
 if isinstance(before,tuple):
  if not isinstance(after,tuple) or len(before)!=len(after):raise AssertionError('compound mutation shape mismatch')
  for source,replacement in zip(before,after):text=replace_once(text,source,replacement)
  return text
 if text.count(before)!=1:raise AssertionError('source anchor is not unique: '+before)
 return text.replace(before,after,1)
def diagnostic_main(args):
 started=time.perf_counter();deadline=started+args.BudgetSeconds
 output=args.EvidenceDir.resolve();output.mkdir(parents=True,exist_ok=True)
 run=output/('diagnostic-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')+'-'+str(os.getpid()));run.mkdir()
 result={'pass':False,'run':str(run),'mode':'diagnostic','profiles':[],'compatibilityProfiles':[],'negativeControls':[],
         'fullFixtureAcceptance':'NOT_RUN','realInputOrUiExecuted':False,'sharedLimitSeconds':args.BudgetSeconds,
         'rendererPort':'inert callback; no browser execution or pixel proof'}
 files=['native/controller_isolation_evidence_runtime.h','tools/focus_shortcut_input_evidence_selftest.cpp',
        'tools/focus_shortcut_input_evidence_selftest.py','tools/virtual_sample_tree_perf_selftest.py',
        'native/sleep_power_state.h','deps/json/json.hpp','native/game_overlay_surface.h','native/msi_native_foreground_isolation_runtime.h','native/virtual_output_status.h','native/sleep_power_observation.h']
 inputs={file:(REPO/file).read_bytes() for file in files}
 raw=(REPO/'native/main.cpp').read_bytes();source=raw.decode('utf-8-sig').replace('\r\n','\n')
 def process(label,command,limit,env):
  remaining=deadline-time.perf_counter()
  if remaining<=0:raise TimeoutError('diagnostic shared budget exhausted')
  began=time.perf_counter();receipt={'command':command,'timeoutSeconds':min(limit,remaining)}
  try:
   child=subprocess.run(command,cwd=run,env=env,capture_output=True,encoding='utf-8',errors='replace',timeout=receipt['timeoutSeconds'])
   receipt['exitCode']=child.returncode
   write(run/(label+'-stdout.txt'),child.stdout);write(run/(label+'-stderr.txt'),child.stderr)
   return child
  except subprocess.TimeoutExpired as error:
   receipt['timedOut']=True
   for name,value in [('stdout',error.stdout),('stderr',error.stderr)]:write(run/(label+'-'+name+'.txt'),value or b'')
   raise
  finally:receipt['elapsedSeconds']=time.perf_counter()-began;save(run/(label+'-process.json'),receipt)
 try:
  module=ast.parse(inputs[files[3]].decode('utf-8-sig'));ctx={'re':re}
  exec(compile(ast.Module(body=[node for node in module.body if isinstance(node,ast.FunctionDef) and node.name in ['clean','match_brace','source_function']],type_ignores=[]),'<parser>','exec'),ctx)
  def take(text,signature):
   masked=re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/',lambda match:' '*len(match.group()),text)
   for match in re.finditer(re.escape(signature),masked):
    opening=masked.index('(',match.start());depth=0;closing=None
    for position in range(opening,len(masked)):
     if masked[position]=='(':depth+=1
     elif masked[position]==')':
      depth-=1
      if depth==0:closing=position;break
    if closing is None:raise AssertionError('unclosed diagnostic function signature: '+signature)
    body=masked.find('{',closing);declaration=masked.find(';',closing)
    if body>=0 and (declaration<0 or body<declaration):return text[match.start():ctx['match_brace'](text,body)+1]
   raise AssertionError('missing complete diagnostic source function: '+signature)
  header=inputs[files[0]].decode('utf-8-sig').replace('\r\n','\n')
  wrapper=take(source,'static void focusShortcutInputEvidence(')
  call='controllerIsolationSummonEvidence(phase,generation,targetCaptured);'
  if wrapper.count(call)!=1 or wrapper.index(call)>wrapper.index('if(!g_keyboardModifierEventGate.load'):raise AssertionError('summon diagnostic must precede the original keyboard gate')
  opening=source.index('{',source.index('struct InputCaptureGamepadSnapshot {'));closing=ctx['match_brace'](source,opening)
  snapshot=source[source.rfind('struct InputCaptureGamepadSnapshot {',0,opening+1):closing+2]
  if not snapshot.endswith('};'):raise AssertionError('canonical capture type boundary')
  write(run/'actual_controller_diagnostic_types.inc',snapshot+'\n')
  symbols={name:take(header,('static json ' if name in ['controllerIsolationPhysicalEvidence','controllerIsolationWindowEvidence','controllerIsolationPresentationEvidence'] else 'static void ')+name+'(')
           for name in ['controllerIsolationPhysicalEvidence','controllerIsolationWindowEvidence','controllerIsolationPresentationEvidence','controllerIsolationSummonEvidence','controllerIsolationOverlayRendererProbe']}
  surface=inputs['native/game_overlay_surface.h'].decode('utf-8-sig').replace('\r\n','\n')
  runtime=inputs['native/msi_native_foreground_isolation_runtime.h'].decode('utf-8-sig').replace('\r\n','\n')
  records=take(surface,'static void gameOverlaySurfaceRecord(')
  show=take(surface,'static bool gameOverlaySurfaceShow(')
  receipt=take(runtime,'static void msiNativeForegroundReceipt(')
  work=take(runtime,'static void msiNativeForegroundHandleWork(')
  restore=take(runtime,'static bool msiNativeForegroundRestoreLocked(')
  if not all(name+'(' in records for name in ['controllerIsolationPresentationEvidence','controllerIsolationPhysicalEvidence']):raise AssertionError('surface record diagnostic wiring missing')
  if show.count('controllerIsolationOverlayRendererProbe();')!=1 or 'if(begun)controllerIsolationOverlayRendererProbe();' not in show:raise AssertionError('renderer probe must be once after successful begin')
  if 'controllerIsolationPhysicalEvidence()' not in receipt:raise AssertionError('native receipt canonical evidence missing')
  for body,phase in [(work,'before-hide'),(restore,'before-return')]:
   marker='msiNativeForegroundReceipt("'+phase+'"'
   if marker not in body or body.index(marker)>body.index('msiNativeForegroundVisibility('):raise AssertionError(phase+' diagnostic is not before visibility write')
  source_audit={'functionHashes':{name:sha(body.encode()) for name,body in symbols.items()},
                'fullWiringFunctions':{name:{'sha256':sha(body.encode()),'source':body} for name,body in [
                 ('focusShortcutInputEvidence',wrapper),('gameOverlaySurfaceRecord',records),('gameOverlaySurfaceShow',show),
                 ('msiNativeForegroundReceipt',receipt),('msiNativeForegroundHandleWork',work),('msiNativeForegroundRestoreLocked',restore)]},
                'scope':'Complete wiring functions inspected; only the five diagnostic functions and original boundary wrapper execute.'}
  save(run/'SOURCE-AUDIT.json',source_audit)
  for file,data in inputs.items():write(run/('fixture.cpp' if file.endswith('_selftest.cpp') else Path(file).name),data)
  write(run/'native-main.cpp',raw)
  result['sourceHashes']={file:sha(data) for file,data in inputs.items()};result['mainSourceSha256']=sha(raw)
  version_file=VC.parent/'Microsoft.VCToolsVersion.default.txt';version=version_file.read_text(encoding='utf-8-sig').strip()
  toolroot=VC.parents[2]/'Tools/MSVC'/version/'bin/Hostx64/x64';sdkroot=Path('C:/Program Files (x86)/Windows Kits/10')
  sdk=max((entry.name for entry in (sdkroot/'Include').iterdir() if entry.is_dir()),key=lambda name:tuple(map(int,name.split('.'))))
  tools=[VC,version_file,toolroot/'cl.exe',toolroot/'link.exe',toolroot/'dumpbin.exe',toolroot/'c1xx.dll',toolroot/'c2.dll']
  result['tools']={str(file):sha(file.read_bytes()) for file in tools};result['msvcVersion']=version;result['windowsSdkVersion']=sdk
  env={key:value for key,value in os.environ.items() if key.upper() in {'SYSTEMROOT','WINDIR','SYSTEMDRIVE','COMSPEC','TEMP','TMP','PROGRAMFILES','PROGRAMFILES(X86)','PROGRAMW6432','PROCESSOR_ARCHITECTURE','NUMBER_OF_PROCESSORS'}}
  env['PATH']=str(toolroot)+os.pathsep+str(Path(os.environ['SystemRoot'])/'System32')+os.pathsep+os.environ['SystemRoot']
  def build(name,flags,selected_header,selected_wrapper,compatibility=False):
   write(run/(name+'-header.h'),selected_header)
   include=name+'.inc'
   port='static void controllerIsolationSummonEvidenceCompatibilityPort(const char*,unsigned long long,bool) noexcept {}\n#define controllerIsolationSummonEvidence controllerIsolationSummonEvidenceCompatibilityPort\n' if compatibility else ''
   write(run/include,'#include "'+name+'-header.h"\n'+port+selected_wrapper+'\n'+('#undef controllerIsolationSummonEvidence\n' if compatibility else ''))
   exe=run/(name+'.exe')
   if len(str(exe))>=250:raise AssertionError('diagnostic output path exceeds MAX_PATH recipe')
   command=[str(toolroot/'cl.exe'),'/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX',*flags,
            '/DFOCUS_CONTROLLER_DIAGNOSTIC=1','/DFOCUS_ACTUAL_INCLUDE="'+include+'"','/sourceDependencies',str(run/(name+'-dependencies.json')),
            str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(exe),'/link','/VERBOSE:LIB','/INCREMENTAL:NO','/NODEFAULTLIB:user32.lib','/NODEFAULTLIB:dwmapi.lib']
   bat=run/(name+'.cmd');write(bat,'@echo off\ncall "'+str(VC)+'" '+sdk+' -vcvars_ver='+version+' >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(command)+'\nexit /b %errorlevel%\n')
   child=process(name+'-compile',[os.environ.get('COMSPEC','cmd.exe'),'/d','/c',str(bat)],60,env)
   if child.returncode:raise AssertionError('diagnostic compile failed: '+name)
   imported=process(name+'-imports',[str(toolroot/'dumpbin.exe'),'/imports',str(exe)],15,env)
   if imported.returncode:raise AssertionError('diagnostic import audit failed: '+name)
   forbidden=[name for name in ['user32.dll','win32u.dll','dwmapi.dll','ole32.dll','shell32.dll','xinput','setupapi.dll','cfgmgr32.dll','hid.dll','deviceiocontrol','sendinput','setforegroundwindow'] if name in imported.stdout.lower()]
   save(run/(name+'-import-audit.json'),{'pass':not forbidden,'forbiddenMatches':forbidden})
   if forbidden:raise AssertionError('diagnostic imported a real UI or device terminal')
   return exe
  profile_errors=[]
  for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]:
   exe=build(name,flags,header,wrapper);child=process(name+'-run',[str(exe)],15,env);row=json.loads(child.stdout)
   result['profiles'].append({'profile':name,'binarySha256':sha(exe.read_bytes()),**row})
   if child.returncode or not row.get('pass') or row.get('cases')!=['physical','window','presentation','boundary','renderer','renderer-error']:profile_errors.append(name+': '+row.get('error','diagnostic case set mismatch'))
   print(name+': '+str(row['checks'])+' diagnostic checks; '+('PASS' if row.get('pass') else row.get('error','FAIL')),flush=True)
   compat=build(name+'-wrapper-compat',flags,header,wrapper,True);checked=process(name+'-wrapper-compat-run',[str(compat),'--case','wrapper-compat'],15,env);compat_row=json.loads(checked.stdout)
   result['compatibilityProfiles'].append({'profile':name,'binarySha256':sha(compat.read_bytes()),**compat_row})
   if checked.returncode or not compat_row.get('pass') or compat_row.get('cases')!=['wrapper-compat']:raise AssertionError('original wrapper compatibility failed: '+name)
  current_fields=re.findall(r'\{"currentSession",([^}]+)\}',header)
  if len(current_fields)!=1 or not all(term in current_fields[0] for term in ['current->session==session','currentPowerGeneration()==power','viewGeneration']):raise AssertionError('current renderer identity expression is missing or ambiguous')
  current=current_fields[0]
  negatives=[('missing-independent-call','boundary',header,replace_once(wrapper,call,''),'independent-keyboard-gate'),
             ('release-proof-overclaim','physical',replace_once(header,'{"gameReleaseReceiptProven",false}','{"gameReleaseReceiptProven",true}'),wrapper,'physical-does-not-prove-release'),
             ('stale-renderer-accepted','renderer',replace_once(header,current,'(static_cast<void>(current),static_cast<void>(power),static_cast<void>(viewGeneration),true)'),wrapper,'renderer-old-session-or-generation')]
  for name,selected,selected_header,selected_wrapper,expected in negatives:
   exe=build(name,['/O2','/DNDEBUG','/MT'],selected_header,selected_wrapper);child=process(name+'-run',[str(exe),'--case',selected],15,env);row=json.loads(child.stdout)
   accepted=child.returncode==1 and row.get('pass') is False and row.get('error')==expected and row.get('selectedCase')==selected
   result['negativeControls'].append({'name':name,'semanticFailure':accepted,'expectedFailure':expected,'binarySha256':sha(exe.read_bytes()),'report':row})
   if not accepted:raise AssertionError('diagnostic negative did not reach its semantic assertion: '+name)
   print(name+': expected semantic failure',flush=True)
  if any((REPO/file).read_bytes()!=data for file,data in inputs.items()):raise AssertionError('diagnostic source drift; frozen results retained')
  if take((REPO/'native/main.cpp').read_text(encoding='utf-8-sig'),'static void focusShortcutInputEvidence(')!=wrapper:raise AssertionError('boundary wrapper drift')
  if any(sha(file.read_bytes())!=result['tools'][str(file)] for file in tools):raise AssertionError('diagnostic tool drift')
  if profile_errors:raise AssertionError('; '.join(profile_errors))
  result['pass']=True
 except Exception as error:result['error']=str(error)
 result['elapsedSeconds']=time.perf_counter()-started;save(run/'result.json',result)
 print('Evidence: '+str(run),flush=True)
 if not result['pass']:print(result.get('error','diagnostic fixture failed'),file=sys.stderr,flush=True)
 return 0 if result['pass'] else 1

def arguments():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--case',dest='Case',choices=['diagnostic'],help='Run only controller-isolation diagnostic consumers with inert OS and renderer ports.')
 parser.add_argument('-EvidenceDir','--evidence-dir',dest='EvidenceDir',type=Path,default=OUT,help='Evidence parent; each invocation creates an exclusive run directory.')
 parser.add_argument('-BaselineSource','--baseline-source',dest='BaselineSource',type=Path,help='Explicit before/main.cpp; omitted means baseline comparison is NOT_RUN.')
 parser.add_argument('-CaseFilter','--case-filter',dest='CaseFilter',help='Comma-separated existing C++ scenarios sharing one compile per profile; default runs the complete original consumer fixture.')
 parser.add_argument('-NormalCaseFilter',dest='NormalCaseFilter',help='Explicit normal-case subset of CaseFilter; none selects only semantic controls in a scoped continuation.')
 parser.add_argument('-MutationFilter',dest='MutationFilter',help='Explicit comma-separated original mutation indices for a scoped continuation.')
 parser.add_argument('-BudgetSeconds','--budget-seconds',dest='BudgetSeconds',type=float,default=270,help='Shared invocation limit, at most the original 270 seconds.')
 args=parser.parse_args()
 if not 0<args.BudgetSeconds<=270:parser.error('BudgetSeconds must be greater than 0 and at most 270')
 if args.Case and any([args.BaselineSource,args.CaseFilter,args.NormalCaseFilter,args.MutationFilter]):parser.error('--case diagnostic is exclusive of legacy/baseline/mutation filters')
 return args
def main():
 args=arguments()
 if args.Case=='diagnostic':return diagnostic_main(args)
 started=time.perf_counter();deadline=started+args.BudgetSeconds
 output=args.EvidenceDir.resolve();output.mkdir(parents=True,exist_ok=True)
 run=output/('run-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')+'-'+str(os.getpid()));run.mkdir()
 result={'pass':False,'run':str(run),'profiles':[],'negativeControls':[],'realInputOrUiExecuted':False,
         'baselineComparison':'NOT_RUN','caseFilter':args.CaseFilter,'fullFixtureAcceptance':'NOT_RUN','compileLimitSeconds':120,'executionLimitSeconds':15,'sharedLimitSeconds':args.BudgetSeconds}
 def process(label,command,limit,env):
  remaining=deadline-time.perf_counter()
  if remaining<=0 or limit<=0:raise TimeoutError('shared fixture or step budget exhausted')
  began=time.perf_counter();effective=min(limit,remaining);receipt={'command':command,'cwd':str(run),'timeoutSeconds':effective,'exitCode':None}
  try:
   child=subprocess.run(command,cwd=run,env=env,capture_output=True,encoding='utf-8',errors='replace',timeout=effective)
   receipt['exitCode']=child.returncode
   stdout,stderr=child.stdout,child.stderr
  except subprocess.TimeoutExpired as error:
   receipt['timedOut']=True
   stdout=error.stdout or '';stderr=error.stderr or ''
   if isinstance(stdout,bytes):stdout=stdout.decode('utf-8','replace')
   if isinstance(stderr,bytes):stderr=stderr.decode('utf-8','replace')
   raise
  except OSError as error:
   receipt['spawnError']=str(error)
   raise
  finally:
   receipt['elapsedSeconds']=time.perf_counter()-began
   save(run/(label+'-process.json'),receipt)
   if 'stdout' in locals():write(run/(label+'-stdout.txt'),stdout);write(run/(label+'-stderr.txt'),stderr)
  return child
 try:
  paths=['native/input_idle_diagnostics.h','tools/focus_shortcut_input_evidence_selftest.cpp',
         'tools/focus_shortcut_input_evidence_selftest.py','tools/virtual_sample_tree_perf_selftest.py','deps/json/json.hpp','native/steam_light_gamepad_runtime.h','native/light_foreground_handoff.h','native/game_process_inventory.h','native/sleep_power_state.h','native/msi_native_foreground_isolation_runtime.h']
  inputs={path:(REPO/path).read_bytes() for path in paths}
  inputs['native/sleep_power_observation.h']=(REPO/'native/sleep_power_observation.h').read_bytes()
  for header in ['native/sleep_power_revocation.h','native/sleep_power_dispatch.h']:inputs[header]=(REPO/header).read_bytes()
  retired_policy='Retired by user direction 2026-11-13.' in inputs['native/steam_light_gamepad_runtime.h'].decode('utf-8-sig')
  result['retiredSourcePolicy']=retired_policy
  table_name='retiredCases' if retired_policy else 'cases'
  case_table=re.search(r'>> '+table_name+r'=\{([\s\S]*?)\n  \};',inputs[paths[1]].decode('utf-8-sig'))
  if case_table is None:raise AssertionError('canonical C++ case table missing')
  expected_cases=re.findall(r'\{"([^"]+)",\w+\}',case_table.group(1))
  if not expected_cases or len(set(expected_cases))!=len(expected_cases):raise AssertionError('empty or duplicate canonical cases')
  selected_cases=None if args.CaseFilter is None else args.CaseFilter.split(',')
  if selected_cases is not None:
   if not selected_cases or len(selected_cases)!=len(set(selected_cases)) or any(case not in expected_cases for case in selected_cases):raise AssertionError('unknown, empty or duplicate case filter: '+args.CaseFilter)
   expected_cases=selected_cases
  normal_cases=selected_cases
  if args.NormalCaseFilter is not None:
   normal_cases=[] if args.NormalCaseFilter=='none' else args.NormalCaseFilter.split(',')
   if selected_cases is None or len(normal_cases)!=len(set(normal_cases)) or any(case not in selected_cases for case in normal_cases):raise AssertionError('invalid normal case subset')
  mutation_indices=None
  if args.MutationFilter is not None:
   mutation_indices=[int(value) for value in args.MutationFilter.split(',')]
   if not mutation_indices or len(mutation_indices)!=len(set(mutation_indices)):raise AssertionError('empty or duplicate mutation filter')
  result['normalCaseFilter']=normal_cases;result['mutationFilter']=mutation_indices
  module=ast.parse(inputs[paths[3]].decode('utf-8-sig'));ctx={'re':re}
  exec(compile(ast.Module(body=[node for node in module.body if isinstance(node,ast.FunctionDef) and node.name in ['clean','match_brace','source_function']],type_ignores=[]),'<parser>','exec'),ctx)
  raw=(REPO/'native/main.cpp').read_bytes();source=raw.decode('utf-8-sig').replace('\r\n','\n')
  blocks={signature:ctx['source_function'](source,signature) for signature in (RETIREMENT_FUNCTIONS if retired_policy else FUNCTIONS)}
  blocks[paths[-1]+':msiNativeForegroundUiEligible']=ctx['source_function'](inputs[paths[-1]].decode('utf-8-sig').replace('\r\n','\n'),'static bool msiNativeForegroundUiEligible(')
  result['mainSourceSha256']=sha(raw);write(run/'native-main.cpp',raw)
  def braced(text,anchor):
   masked=ctx['clean'](text)
   if masked.count(anchor)!=1:raise AssertionError('block anchor is not unique: '+anchor)
   begin=masked.index(anchor);opening=masked.index('{',begin)
   return text[begin:ctx['match_brace'](text,opening)+1]
  types=[braced(source,'struct '+name+' {')+';' for name in (['NativeDetectedGame','FocusTargetSnapshot','FocusSessionState','FocusLightIsolationTicket','NativeGameValveSnapshot'] if retired_policy else ['NativeDetectedGame','FocusTargetSnapshot','FocusSessionState','FocusLightIsolationTicket','FocusLightHandoffState','NativeGameValveSnapshot'])]
  types.append(braced(source,'enum class NativeGameDiscoveryState {')+';')
  types.insert(0,types.pop())
  types_text='\n\n'.join(types)+'\n';write(run/'actual_focus_types.inc',types_text);result['focusTypesSha256']=sha(types_text.encode())
  if retired_policy:
   generated='\n\n'.join(blocks.values())+'\n'
   fixture_source=inputs[paths[1]].decode('utf-8-sig').replace('\r\n','\n')
   original_start=fixture_source.index('#else\n// Actual shortcut wrapper')
   legacy_source=fixture_source[original_start:]
   generated+='\n'+braced(legacy_source,'struct Control {')+';\n'
   generated+='\n'.join(ctx['source_function'](legacy_source,n) for n in ['static void peekCases(', 'static Control run(', 'static void legacyCases('])+'\n'
   mutations=[]
   for name,signature,message in [('retired-selection','static bool focusLightNoGameQualified(','retired no-game admission reopened'),('retired-selection','static bool focusLightIsolationBegin(','retired ticket creation reopened'),('retired-selection','static bool focusLightIsolationCurrent(','retired ticket became current')]:
    body=blocks[signature];mutations.append((name,body,replace_once(body,'return false;','return true;'),message))
   body=blocks['static void focusPrepareIsolationBeforeShow('];mutations.append(('retired-game-latch',body,replace_once(body,'const bool gameOpen = observation && observation->game.pid;','const bool gameOpen = false;'),'game latch lost during retired admission'))
   key=paths[-1]+':msiNativeForegroundUiEligible';body=blocks[key];mutations.append(('retired-ui-route',body,replace_once(body,'return gamepadUiOutputIsolationEligible();','return (static_cast<void>(gamepadUiOutputIsolationEligible()), false);'),'retired exemption changed ordinary output isolation'))
   for _,before,after,_ in mutations:replace_once(generated,before,after)
   result['functionHashes']={signature:sha(block.encode()) for signature,block in blocks.items()}
   result['portScope']='Actual focus/ordinary eligibility and source-exact original 24 diagnostic controls; all OS/UI/metadata queue terminals inert. No active lease implementation.'
  else:
   wndproc=ctx['source_function'](source,'static LRESULT CALLBACK WndProc(')
   show_window=ctx['source_function'](source,'static void showWindowAnimated(')
   pre_show=show_window[show_window.index('{')+1:show_window.index('// 非常驻时')]
   hide_window=ctx['source_function'](source,'static void hideWindowAnimated(')
   post_hide=braced(hide_window,'if (isMainWindow) {\n        focusEndIsolationOnUi();')
   ready=ctx['source_function'](source,'static void finalizeWebViewRenderReady(')
   timer_blocks=[braced(wndproc,'if (w == '+name+') {') for name in ['SUMMON_FOCUS_TIMER_ID','RETURN_GAME_FOCUS_TIMER_ID']]
   ready_block=braced(ready,'if (focusSequence == g_focusSessionSequence && g_focusSession.active && !g_focusSession.returning && focusLightMayRefocus(GetTickCount64()) &&')
   ready_show=braced(ready,'if (g_deferFirstShow) {')
   owner_event_blocks=[]
   for message,boundary in [('WM_SHOWWINDOW','\n        if (w) {'),('WM_ACTIVATE','\n        if (LOWORD(w) != WA_INACTIVE) ymcc::screenpads::release();'),('WM_SIZE','\n        msiNativeForegroundUiEvent();')]:
    anchor='\n    case '+message+':'
    if wndproc.count(anchor)!=1:raise AssertionError('window owner case is not unique: '+message)
    begin=wndproc.index(anchor);end=wndproc.index(boundary,begin)
    next_case=wndproc.index('\n    case ',begin+len(anchor))
    if end>=next_case:raise AssertionError('window owner boundary crossed cases: '+message)
    if message=='WM_SIZE':end+=len(boundary)
    block=wndproc[begin:end]
    owner_event_blocks.append(block)
   foreground_observer=ctx['source_function'](inputs[paths[-1]].decode('utf-8-sig').replace('\r\n','\n'),'static void CALLBACK msiNativeForegroundObserved(')
   generated='\n\n'.join(blocks.values())
   generated=replace_once(generated,'static void focusBeginReturnToPreviousWindow(','static void actualFocusBeginReturnToPreviousWindow(')
   generated=replace_once(generated,'static void bringToFront(','static void actualBringToFront(')
   generated=replace_once(generated,'static bool refocusWebView(','static bool actualRefocusWebView(')
   generated=replace_once(generated,'static bool gamepadUiInputEligible(','static bool actualGamepadUiInputEligible(')
   generated=replace_once(generated,'static bool gamepadUiOutputIsolationEligible(','static bool actualGamepadUiOutputIsolationEligible(')
   generated+='\nstatic LRESULT actualFocusTimer(WPARAM w) {\n'+'\n'.join(timer_blocks)+'\nreturn 0;\n}\n'
   generated+='\nstatic void actualLateReadyFocus(unsigned long long focusSequence = g_focusSessionSequence) {\n'+ready_show+'\n'+ready_block+'\n}\n'
   generated+='\nstatic void actualMainWindowOwnerEvent(UINT message, WPARAM w) {\nHWND h=g_hwnd;\nswitch(message) {\n'+'\nbreak;\n'.join(block.replace('PostMessageW(h, ymcc::gesturesummon::kRefreshMessage, 0, 0);','(void)h;') for block in owner_event_blocks)+'\nbreak;\n}\n}\n'
   generated+='\n'+foreground_observer.replace('msiNativeForegroundObserved(','actualForegroundObserved(',1)+'\n'
   generated+='\nstatic void actualMainWindowBeforeShow(HWND hwnd, int showCmd) {\n'+pre_show+'\n}\n'
   generated+='\nstatic void actualMainWindowAfterHide(bool isMainWindow) {\n'+post_hide+'\n}\n'
   generated+='\n#define poolSubmit steamLightGamepadPoolSubmit\n#define steamLightGamepadUiChanged actualSteamLightGamepadUiChanged\n'+inputs['native/steam_light_gamepad_runtime.h'].decode('utf-8-sig').replace('#pragma once','',1)+'\n#undef steamLightGamepadUiChanged\n#undef poolSubmit\n'
   handoff_source=inputs['native/light_foreground_handoff.h'].decode('utf-8-sig').replace('\r\n','\n')
   write(run/'actual_light_cancellation.inc',braced(handoff_source,'struct Cancellation {')+';\n')
   handoff_blocks={anchor:braced(handoff_source,anchor) for anchor in [
    'enum class Code : DWORD {','inline const char* reason(Code code)', 'inline DWORD error(Code code)',
    'template<class Operations> inline Code gate(', 'template<class Operations> inline Code handoff(']}
   handoff_blocks['enum class Code : DWORD {']+=';'
   windows_operations=braced(handoff_source,'struct WindowsOperations {')
   settle_foreground=braced(windows_operations,'bool settleForeground() const')
   generated+='\nnamespace ymcc::lightforeground::detail {\n'+'\n\n'.join(handoff_blocks.values())+'\n}\n'
   generated+='\n#define SendMessageTimeoutW fakeForegroundSendMessageTimeout\nstruct WindowsSettlePort : WindowsSettlePortState {\n'+settle_foreground+'\n};\n#undef SendMessageTimeoutW\n'
   producer_blocks={signature:ctx['source_function'](source,signature) for signature in [
    'static ULONGLONG nativeProcessCreatedFromHandle(', 'static NativeOnePidResult nativeScanGameOnePid(',
    'static bool nativeGameDiscoveryTopologyRelevant(', 'static ymcc::gameInventory::WorkingSetRead nativeGameDiscoveryWorkingSetHint(',
    'static bool nativeScanGameConfiguredBest(', 'static NativeDetectedGame nativeScanGame(']}
   producer_enum=braced(source,'enum class NativeOnePidResult {')+';'
   producer_constants=[]
   for constant in ['SG_MIN_WS','SG_CAPTURE_MIN_WS']:
    matches=re.findall(r'^static const uint64_t\s+'+constant+r'\s*=[^;]+;',source,re.M)
    if len(matches)!=1:raise AssertionError('producer threshold anchor is not unique: '+constant)
    producer_constants.append(matches[0])
   inventory_source=inputs['native/game_process_inventory.h'].decode('utf-8-sig').replace('\r\n','\n')
   inventory_begin=inventory_source.index('inline std::atomic<unsigned long long> invalidation{')
   inventory_end=inventory_source.rindex('} // namespace ymcc::gameInventory')
   inventory_core=inventory_source[inventory_begin:inventory_end]
   inventory_interval=re.findall(r'^inline constexpr ULONGLONG topologyIntervalMs\s*=\s*[^;]+;',inventory_source[:inventory_begin],re.M)
   if len(inventory_interval)>1:raise AssertionError('producer topology interval anchor is not unique')
   if inventory_interval:inventory_core=inventory_interval[0]+'\n'+inventory_core
   producer_ports=['GetLastError','SetLastError','GetTickCount64','GetCurrentProcessId','OpenProcess','CloseHandle',
    'GetProcessMemoryInfo','QueryFullProcessImageNameW','ProcessIdToSessionId','GetProcessTimes','WaitForSingleObject',
    'IsWindowVisible','IsIconic','GetForegroundWindow','GetAncestor']
   producer_prefix='\n'.join('#pragma push_macro("'+name+'")\n#undef '+name+'\n#define '+name+' port'+name for name in producer_ports)
   producer_suffix='\n'.join('#pragma pop_macro("'+name+'")' for name in reversed(producer_ports))
   producer_generated=producer_prefix+'\nnamespace gameObservationProducer {\n'+producer_enum+'\n'+'\n'.join(producer_constants)
   producer_generated+='\nnamespace ymcc::gameInventory {\n'+inventory_core+'\n}\n'+'\n\n'.join(producer_blocks.values())+'\n}\n'+producer_suffix+'\n'
   generated+='\n'+producer_generated
   for case,before,after,expected in MUTATIONS:replace_once(generated,before,after)
   write(run/'actual_focus.inc',generated)
   result['functionHashes']={signature:sha(block.encode()) for signature,block in blocks.items()}
   result['consumerBlockHashes']={name:sha(block.encode()) for name,block in zip(['summon-timer','return-timer','render-ready-focus','render-ready-show'],timer_blocks+[ready_block,ready_show])}
   result['consumerBlockHashes'].update({name:sha(block.encode()) for name,block in zip(['window-show-owner-prefix','window-activate-owner-prefix','window-size-owner-prefix','foreground-observer'],owner_event_blocks+[foreground_observer])})
   result['windowOwnerAdapter']='Existing gesture refresh post replaced with an inert statement; isolation call order and observer body are actual production source.'
   result['consumerBlockHashes']['before-show-policy']=sha(pre_show.encode())
   result['consumerBlockHashes']['after-hide-policy']=sha(post_hide.encode())
   result['consumerBlockHashes'].update({'light-foreground:'+anchor:sha(block.encode()) for anchor,block in handoff_blocks.items()})
   result['consumerBlockHashes']['light-foreground:settleForeground']=sha(settle_foreground.encode())
   result['consumerBlockHashes'].update({'game-observation:'+signature:sha(block.encode()) for signature,block in producer_blocks.items()})
   result['consumerBlockHashes']['game-observation:inventory-core']=sha(inventory_core.encode())
   result['consumerBlockHashes']['game-observation:one-pid-enum']=sha(producer_enum.encode())
   result['consumerBlockHashes']['game-observation:thresholds']=sha('\n'.join(producer_constants).encode())
   result['consumerBlockHashes']['game-observation:generated']=sha(producer_generated.encode())
   result['gameObservationAdapter']='Actual one-PID, configured/full/legacy scans and inventory/next; isolated scalar Win32 ports and OS snapshot leaf providers.'
   mutations=MUTATIONS
  if mutation_indices is not None:
   allowed_indices={index for index,(case,*_) in enumerate(mutations,1) if selected_cases is None or case in selected_cases}
   if any(index not in allowed_indices for index in mutation_indices):raise AssertionError('unknown or mismatched mutation selection')
  if normal_cases==[] and not any((selected_cases is None or case in selected_cases) and (mutation_indices is None or index in mutation_indices) for index,(case,*_) in enumerate(mutations,1)):raise AssertionError('empty execution selection')
  if retired_policy:write(run/'actual_focus.inc',generated)
  result['generatedSha256']=sha(generated.encode())
  baseline=None
  if args.BaselineSource is not None:
   baseline=args.BaselineSource.resolve();baseline_raw=baseline.read_bytes()
   old=ctx['source_function'](baseline_raw.decode('utf-8-sig').replace('\r\n','\n'),'static bool nativeYmccShortcutSummon(')
   write(run/'baseline-main.cpp',baseline_raw)
   write(run/'baseline_focus.inc',replace_once(old,'nativeYmccShortcutSummon(','baselineNativeYmccShortcutSummon(')+'\n')
   result.update(baselineSource=str(baseline),baselineSha256=sha(baseline_raw),baselineComparison='NOT_RUN_SELECTED_SCOPE' if args.CaseFilter else 'SELECTED')
  for path,data in inputs.items():
   write(run/('fixture.cpp' if path.endswith('_selftest.cpp') else Path(path).name),data)
  result['sourceHashes']={path:sha(data) for path,data in inputs.items()}
  version_file=VC.parent/'Microsoft.VCToolsVersion.default.txt';version=version_file.read_text(encoding='utf-8-sig').strip()
  toolroot=VC.parents[2]/'Tools/MSVC'/version/'bin/Hostx64/x64'
  sdkroot=Path('C:/Program Files (x86)/Windows Kits/10')
  sdk=max((entry.name for entry in (sdkroot/'Include').iterdir() if entry.is_dir()),key=lambda name:tuple(map(int,name.split('.'))))
  tools=[VC,version_file,toolroot/'cl.exe',toolroot/'link.exe',toolroot/'dumpbin.exe',toolroot/'c1xx.dll',toolroot/'c2.dll']
  result['tools']={str(path):sha(path.read_bytes()) for path in tools};result['msvcVersion']=version;result['windowsSdkVersion']=sdk
  env={key:value for key,value in os.environ.items() if key.upper() in {'SYSTEMROOT','WINDIR','SYSTEMDRIVE','COMSPEC','TEMP','TMP','PROGRAMFILES','PROGRAMFILES(X86)','PROGRAMW6432','PROCESSOR_ARCHITECTURE','NUMBER_OF_PROCESSORS'}}
  env['PATH']=str(toolroot)+os.pathsep+str(Path(os.environ['SystemRoot'])/'System32')+os.pathsep+os.environ['SystemRoot']
  result['environment']={key:value for key,value in env.items() if key.upper() not in {'TEMP','TMP'}}
  def build(name,flags,include):
   build_started=time.perf_counter()
   exe=run/(name+'.exe')
   if max(len(str(run/(name+suffix))) for suffix in ['.obj','.exe','.cmd'])>=250:raise AssertionError('native output path exceeds supported MAX_PATH recipe')
   command=[str(toolroot/'cl.exe'),'/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX',*flags,
            '/DFOCUS_LIGHT_RETIRED='+str(int(retired_policy)),'/sourceDependencies',str(run/(name+'-dependencies.json')),'/DFOCUS_BASELINE_AVAILABLE='+str(int(baseline is not None)),'/DFOCUS_ACTUAL_INCLUDE="'+include+'"',str(run/'fixture.cpp'),'/I'+str(run),
            '/Fo'+str(run/(name+'.obj')),'/Fe'+str(exe),'/link','/VERBOSE:LIB','/INCREMENTAL:NO','/NODEFAULTLIB:user32.lib']
   bat=run/(name+'.cmd')
   write(bat,'@echo off\ncall "'+str(VC)+'" '+sdk+' -vcvars_ver='+version+' >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(command)+'\nexit /b %errorlevel%\n')
   child=process(name+'-compile',[os.environ.get('COMSPEC','cmd.exe'),'/d','/c',str(bat)],120,env)
   if child.returncode:raise AssertionError('compile failed: '+name)
   imported=process(name+'-imports',[str(toolroot/'dumpbin.exe'),'/imports',str(exe)],120-(time.perf_counter()-build_started),env)
   if imported.returncode:raise AssertionError('import audit failed: '+name)
   lowered=imported.stdout.lower();banned=[name for name in ['user32.dll','win32u.dll','getasynckeystate','getkeyboardstate','setforegroundwindow','sendinput','ole32.dll','shell32.dll'] if name in lowered]
   save(run/(name+'-import-audit.json'),{'pass':not banned,'forbiddenMatches':banned})
   if banned:raise AssertionError('real input/UI import')
   return exe
  for name,flags in ([] if normal_cases==[] else [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]):
   exe=build(name,flags,'actual_focus.inc')
   for selected in normal_cases if normal_cases is not None else [None]:
    suffix='-'+selected if normal_cases is not None and len(normal_cases)>1 else ''
    command=[str(exe)]+(['--case',selected] if selected is not None else []);child=process(name+'-run'+suffix,command,15,env)
    row=json.loads(child.stdout)
    result['profiles'].append({'profile':name,**row})
    if child.returncode:raise AssertionError('fixture failed: '+name+': '+child.stderr)
    if row.get('pass') is not True:raise AssertionError('fixture did not report pass')
    if row.get('controlScenarios')!=(0 if selected is not None else 24) or row.get('selectedCase')!=(selected or ''):raise AssertionError('normal selection mismatch')
    actual_cases=row.get('lightScenarios',[]);expected_selection=[selected] if selected is not None else expected_cases
    if len(actual_cases)!=len(expected_selection) or set(actual_cases)!=set(expected_selection):raise AssertionError('normal case set mismatch')
    if row['baselineComparisons']!=(24 if baseline is not None and selected is None else 0):raise AssertionError('baseline selection mismatch')
    print(name+': '+str(row['checks'])+' checks, '+str(row['controlScenarios'])+' legacy scenarios, '+str(len(row['lightScenarios']))+' light scenarios',flush=True)
  for index,(case,before,after,expected) in enumerate(mutations,1):
   if selected_cases is not None and case not in selected_cases:continue
   if mutation_indices is not None and index not in mutation_indices:continue
   include='mutation'+str(index)+'.inc';mutated=replace_once(generated,before,after);write(run/include,mutated)
   exe=build('neg'+str(index),['/O2','/DNDEBUG','/MT'],include)
   child=process('neg'+str(index)+'-run',[str(exe),'--case',case],15,env)
   row=json.loads(child.stdout)
   accepted=child.returncode==1 and child.stderr.strip()==expected and row.get('pass') is False and row.get('selectedCase')==case and row.get('checks',0)>0 and row.get('controlScenarios')==0 and row.get('baselineComparisons')==0 and row.get('lightScenarios')==[]
   receipt={'mutationIndex':index,'case':case,'expectedFailure':expected,'exitCode':child.returncode,'semanticFailure':accepted,'sourceSha256':sha(mutated.encode())}
   result['negativeControls'].append(receipt)
   if not accepted:raise AssertionError('negative did not fail its semantic assertion: '+case)
   print('Negative '+case+': semantic rejection',flush=True)
  if mutation_indices is not None and set(mutation_indices)!={row['mutationIndex'] for row in result['negativeControls']}:raise AssertionError('unknown or mismatched mutation selection')
  if (REPO/'native/main.cpp').read_bytes()!=raw:raise AssertionError('main source drift; frozen result retained')
  if any((REPO/path).read_bytes()!=data for path,data in inputs.items()):raise AssertionError('input drift')
  if baseline is not None and baseline.read_bytes()!=baseline_raw:raise AssertionError('baseline drift')
  if any(sha(path.read_bytes())!=result['tools'][str(path)] for path in tools):raise AssertionError('tool drift')
  if baseline is not None and not args.CaseFilter:result['baselineComparison']='PASS'
  result['pass']=True
 except Exception as error:result['error']=str(error)
 result['elapsedSeconds']=time.perf_counter()-started
 save(run/'result.json',result)
 print('Evidence: '+str(run),flush=True)
 if not result['pass']:print(result.get('error','fixture failed'),file=sys.stderr,flush=True)
 return 0 if result['pass'] else 1
if __name__=='__main__':raise SystemExit(main())
