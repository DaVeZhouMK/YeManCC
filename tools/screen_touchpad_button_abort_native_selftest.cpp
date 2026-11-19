// Actual WinRT dispatcher interruptions and production button recovery on a
// private desktop. No SendInput, physical devices, installed app or Steam.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include "screen_touchpad_owner.h"
#include <iostream>
#include <thread>
#include <functional>
using namespace ymcc::screenpads;
#ifdef YMCC_SCREEN_TOUCHPAD_OVERFLOW_FIXTURE
namespace ymcc::screeninput {struct SurfaceFixtureAccess {static auto input(Surface& surface){return surface.independent_;}};}
#endif
static Json cases=Json::array(),details=Json::array();static bool passed=true;
static bool runtimeClosed=false,queueClosed=false,workerEnded=false;static DWORD workerError=0;
static HANDLE doneEvent=nullptr;
static bool channelOnly=false,notifyOnly=false,stopOnly=false,commandOnly=false,displayOnly=false,displayBoundaryOnly=false,channelDisplayOnly=false,presentationRecoveryOnly=false,inputOverflowOnly=false,workerRetirementOnly=false,workerRetirementQuota=false,manualChannelRecoveryOnly=false,channelStageOnly=false;
static bool wakeOnly=false,persistentOnly=false,reentryOnly=false,lateFaultOnly=false,cumulativeOnly=false;
static void check(const std::string& name,bool ok){cases.push_back({{"name",name},{"ok",ok}});passed=passed&&ok;}
static std::wstring desktopName(HDESK h){wchar_t name[256]{};DWORD bytes=0;return h&&GetUserObjectInformationW(h,UOI_NAME,name,sizeof(name),&bytes)?name:L"";}
static std::wstring inputName(){HDESK h=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);auto name=desktopName(h);if(h)CloseDesktop(h);return name;}
static void pump(){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
static HWND createOwner(){WNDCLASSW wc{};wc.lpfnWndProc=[](HWND h,UINT m,WPARAM w,LPARAM l)->LRESULT{
 if(m==WM_DISPLAYCHANGE||m==WM_DPICHANGED){queueOwnerDisplayRefresh(true);return 0;}
 if(m==WM_SETTINGCHANGE&&w==SPI_SETWORKAREA){queueOwnerDisplayRefresh(false);return 0;}
 if(m==kOwnerRebindMessage){consumeOwnerRebind((UINT_PTR)w);return 0;}
 if(m==kOwnerVisibilityMessage){consumeOwnerVisibility((UINT_PTR)w);return 0;}
 return DefWindowProcW(h,m,w,l);};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YMCC_ButtonAbortFixtureOwner";RegisterClassW(&wc);return CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);}
static void fireWake(){const auto r=ownerResumeRecovery;if(r.timer)ownerResumeTimerProc(r.window,WM_TIMER,r.timer,0);}
static ymcc::sleeppower::SharedPowerV1 fixturePower;
static UINT_PTR fixturePowerOwnerEpoch=0;
static int __cdecl fixturePowerRead(ymcc::sleeppower::SharedPowerV1* out,std::uint32_t bytes){
 if(!out||bytes!=sizeof(*out))return 0;*out=fixturePower;return 1;
}
static void fixturePowerCallbacks(){
 ownerPowerCapture=[](ymcc::sleeppower::SharedPowerV1& out){return ymcc::sleeppower::capturePower(fixturePowerRead,out);};
 ownerPowerCurrent=[](const ymcc::sleeppower::SharedPowerV1& old){return ymcc::sleeppower::powerCurrent(fixturePowerRead,old);};
}
static bool fixtureOwnerResumeReady(unsigned long long generation){
 if(fixturePowerOwnerEpoch!=ownerLifecycleEpoch){++fixturePower.suspendSerial;fixturePowerOwnerEpoch=ownerLifecycleEpoch;}
 fixturePowerCallbacks();fixturePower.generation=generation;fixturePower.phase=0;
 fixturePower.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
 return ownerResumeReady(generation);
}
static void persistentRuntimeCases(){
 Config cfg=getConfig();cfg.masterEnabled=true;cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;configure(cfg,1,true);
 const auto saved=configJson(getConfig());autoHideSetHidden(false);
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YMCC_UnrelatedSurfaceSentinel";RegisterClassW(&wc);
 HWND peer=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,wc.lpszClassName,L"",WS_POPUP,0,0,100,100,nullptr,nullptr,wc.hInstance,nullptr);
 ymcc::screeninput::Surface sentinel;
 check("runtime-peer-surface-binds-separate-shared-compositor",peer&&sentinel.attach(nativeInputRuntime,peer,99,nullptr,nullptr));
 const auto shared=winrt::get_abi(nativeInputRuntime.compositor),queue=winrt::get_abi(winrt::Windows::System::DispatcherQueue::GetForCurrentThread());
 ownerOnSuspend();queueOwnerResumeRebind();pump();release();for(int side=0;side<2;++side)nativeInputClose(side);buttonsNativeInputDisable();
 nativePadInputRuntime.compositor.Close();refresh(true);
 const auto closedError=windowError;details.push_back({{"closedRuntimeActualFailure",nativeInputFailureState()}});
 check("runtime-closed-compositor-actually-prevents-bind",closedError&&!ownerResumeControlsHealthy());
 Config off=cfg;off.enabled=false;configure(off,1,true);configure(cfg,1,true);
 check("runtime-manual-toggle-does-not-repair-closed-compositor",windowError&&!ownerResumeControlsHealthy()&&saved==configJson(getConfig()));
 fixtureOwnerResumeReady(110);fireWake();
 check("runtime-first-source-only-rebuild-still-fails",ownerResumeRecovery.repairs==1&&windowError&&!ownerResumeControlsHealthy());
 fireWake();
 check("runtime-second-attempt-retires-owned-runtime-and-windows",ownerResumeRecovery.repairs==2&&ownerResumeRecovery.runtimeRebuilds==1&&!windowError&&ownerResumeControlsHealthy());
 fireWake();fireWake();
 check("runtime-repair-verifies-twice-and-retires-timer",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerResumeRecovery.clean==2&&std::string(ownerResumeRecovery.status)=="recovered");
 check("runtime-peer-and-thread-dispatcher-survive-owned-rebuild",sentinel.active()&&sentinel.sync()&&shared==winrt::get_abi(nativeInputRuntime.compositor)&&queue==winrt::get_abi(winrt::Windows::System::DispatcherQueue::GetForCurrentThread()));
 check("runtime-rebuild-preserves-selection-and-releases-owned-signals",saved==configJson(getConfig())&&!sentKeys&&!sentClick&&!screenButtonMask.load());
 details.push_back({{"closedRuntimeRecovery",ownerResumeRecoveryState()}});
 sentinel.close();check("runtime-peer-sentinel-released",sentinel.terminalReleased());if(peer)DestroyWindow(peer);
 // A stale root HWND also survives the old source-only rebind. Exercise the
 // same production escalation without replacing the shared input dispatcher.
 ownerOnSuspend();queueOwnerResumeRebind();pump();DestroyWindow(windows[0]);refresh(true);fixtureOwnerResumeReady(111);fireWake();fireWake();fireWake();fireWake();
 check("runtime-invalid-root-hwnd-is-recreated-without-app-restart",ownerResumeRecovery.runtimeRebuilds==1&&ownerResumeRecovery.clean==2&&!ownerResumeRecovery.pending&&!windowError&&ownerResumeControlsHealthy());
 details.push_back({{"staleRootRecovery",ownerResumeRecoveryState()}});
 ownerOnSuspend();fixturePowerCallbacks();fixturePower.generation=114;fixturePower.flags=ymcc::sleeppower::kKnown;
 release();for(int side=0;side<2;++side)nativeInputClose(side);buttonsNativeInputDisable();nativePadInputRuntime.compositor.Close();refresh(true);
 check("runtime-failure-before-shared-Ready-does-not-arm-repair",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer);
 fixturePower.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;refresh(true);
 check("runtime-ordinary-bind-failure-arms-existing-bounded-recovery",ownerResumeRecovery.pending&&ownerResumeRecovery.timer&&ownerResumeRecovery.powerGeneration==114);
 fireWake();fireWake();fireWake();fireWake();
 check("runtime-ordinary-failure-recovers-without-sleep-or-user-toggle",ownerResumeControlsHealthy()&&ownerResumeRecovery.clean==2&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerResumeRecovery.runtimeRebuilds==1);
 check("runtime-repeat-fault-cannot-refund-same-power-budget",!ownerNativeInputFailed()&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer);
 fixturePower.generation=115;release();for(int side=0;side<2;++side)nativeInputClose(side);buttonsNativeInputDisable();nativePadInputRuntime.compositor.Close();refresh(true);
 Config disabled=cfg;disabled.masterEnabled=false;configure(disabled,1,true);fireWake();
 check("runtime-OFF-cancels-ordinary-fault-recovery-without-reshow",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&!buttonsVisible()&&ownerResumeRecovery.repairs==0);
 ownerPowerCapture=nullptr;ownerPowerCurrent=nullptr;configure(cfg,1,true);
 // Restore only the fixture's intentionally poisoned resource for later,
 // independent exit controls. This is not counted as an automatic repair.
 const bool fixtureRestored=rebuildOwnedInputResources([]{return initialized&&!inputTeardown;});refresh(true);
 check("runtime-fault-fixture-returns-healthy-before-independent-exit",fixtureRestored&&ownerResumeControlsHealthy());
}
static void reentryProbeCases(){
 Config cfg=getConfig();cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;
 cfg.summonEnabled=false;cfg.specialEnabled=false;cfg.rearEnabled=false;configure(cfg,1,true);autoHideSetHidden(false);
 check("reentry-probe-baseline-real-pad-sources-healthy",nativeInputSurfaces[0].active()&&nativeInputSurfaces[1].active());
 bool nestedCalled=false,nestedResult=false,outerResult=false;HRESULT nestedError=S_OK;std::string nestedStage;
 auto dispatcher=nativeInputSurfaces[0].source().Dispatcher();
 auto nested=dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{
  nestedCalled=true;nestedResult=nativeInputSurfaces[0].sync();nestedError=nativeInputSurfaces[0].error;nestedStage=nativeInputSurfaces[0].lastFailureStage();
 });
 outerResult=nativeInputSurfaces[0].sync();
 check("reentry-probe-nested-sync-callback-was-dispatched",nestedCalled);
 details.push_back({{"probe","same-source-nested-ProcessEvents"},{"nestedResult",nestedResult},{"nestedError",(DWORD)nestedError},{"nestedStage",nestedStage},{"outerResult",outerResult},{"outerError",(DWORD)nativeInputSurfaces[0].error},{"activeAfter",nativeInputSurfaces[0].active()}});
 // Re-establish sources through the same production owner, then replace the
 // selected source synchronously while an older sync is dispatching it.
 release();for(int side=0;side<2;++side)nativeInputClose(side);refresh(true);
 bool rebound=false,innerHealthy=false;void* innerSource=nullptr;DWORD innerError=0;
 dispatcher=nativeInputSurfaces[0].source().Dispatcher();
 auto rebind=dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{
  rebound=true;nativeInputClose(0);refresh(true);innerHealthy=nativeInputSurfaces[0].active();innerSource=winrt::get_abi(nativeInputSurfaces[0].source());innerError=windowError;
 });
 const bool oldResult=nativeInputSync(0,true,true);
 const auto oldError=windowError;const bool activeAfter=nativeInputSurfaces[0].active();const bool sameSource=innerSource&&innerSource==winrt::get_abi(nativeInputSurfaces[0].source());
 check("reentry-probe-replacement-callback-was-dispatched",rebound);
 check("reentry-repair-older-sync-does-not-close-new-source",innerHealthy&&activeAfter&&sameSource&&!oldError);
 details.push_back({{"probe","replace-source-during-older-nativeInputSync"},{"newSourceHealthyInsideCallback",innerHealthy},{"innerWindowError",innerError},{"olderSyncResult",oldResult},{"olderWindowError",oldError},{"activeAfterOldReturns",activeAfter},{"sameNewSourceAfterOldReturns",sameSource},{"latestFailure",nativeInputFailureState()}});
 // Retire the deliberately broken previous source before the independent visibility probe.
 if(!nativeInputSurfaces[0].active()){release();for(int side=0;side<2;++side)nativeInputClose(side);refresh(true);}
 bool changed=false,changedHealthy=false;auto shifted=cfg;shifted.scale=std::min(200,cfg.scale+5);
 dispatcher=nativeInputSurfaces[0].source().Dispatcher();
 auto newer=dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{
  changed=true;nativeInputClose(0);configure(shifted,1,true);changedHealthy=nativeInputSurfaces[0].active()&&IsWindowVisible(windows[0]);
 });
 refresh(true);
 check("reentry-repair-outer-refresh-cannot-hide-new-selection",changed&&changedHealthy&&nativeInputSurfaces[0].active()&&IsWindowVisible(windows[0])&&!windowError);
 details.push_back({{"probe","replace-selection-during-outer-refresh"},{"callbackRan",changed},{"newSourceHealthyInsideCallback",changedHealthy},{"newSourceActiveAfter",nativeInputSurfaces[0].active()},{"newInputVisibleAfter",!!IsWindowVisible(windows[0])},{"windowErrorAfter",windowError}});
 // Fault observations above are not field-cause assertions. Retire all probe
 // state before the existing independent terminal cleanup controls.
 shutdownForOwner();
}
static void wakeCases(){
 Config cfg=getConfig();cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=15;configure(cfg,1,true);
 const auto saved=configJson(getConfig());
 check("wake-ordinary-Ready-without-resume-does-not-start",!fixtureOwnerResumeReady(100)&&!ownerResumeRecovery.timer);
 ownerOnSuspend();queueOwnerResumeRebind();pump();
 // Reproduce the exported missing-window/source state with the same error;
 // this does not claim to recreate the unknown original Windows COM failure.
 for(int side=0;side<2;++side){nativeInputClose(side);ShowWindow(windows[side],SW_HIDE);ShowWindow(outlines[side],SW_HIDE);}
 buttonsNativeInputDisable();for(HWND h:buttonWindows)if(h)ShowWindow(h,SW_HIDE);for(HWND h:buttonOutlines)if(h)ShowWindow(h,SW_HIDE);windowError=(DWORD)E_UNEXPECTED;
 check("wake-field-state-is-missing-with-saved-intent",!ownerResumeControlsHealthy()&&saved==configJson(getConfig())&&windowError==(DWORD)E_UNEXPECTED);
 check("wake-committed-Ready-arms-finite-check",fixtureOwnerResumeReady(101)&&ownerResumeRecovery.timer&&ownerResumeRecovery.repairs==0);
 const auto deadline=ownerResumeRecovery.deadline,timer=ownerResumeRecovery.timer;
 fixtureOwnerResumeReady(101);check("wake-duplicate-Ready-does-not-renew-or-rearm",ownerResumeRecovery.deadline==deadline&&ownerResumeRecovery.timer==timer);
 fireWake();check("wake-missing-pad-and-buttons-recreated",ownerResumeRecovery.repairs==1&&ownerResumeControlsHealthy()&&!windowError&&saved==configJson(getConfig()));
 fireWake();check("wake-first-clean-check-keeps-one-verification",ownerResumeRecovery.clean==1&&ownerResumeRecovery.pending&&ownerResumeRecovery.timer);
 fireWake();check("wake-two-clean-checks-stop-all-recovery-work",ownerResumeRecovery.clean==2&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&std::string(ownerResumeRecovery.status)=="recovered");
 check("wake-original-error-retained-in-compact-receipt",ownerResumeRecovery.lastError==(DWORD)E_UNEXPECTED);
 const HWND actual=windows[0];windows[0]=windows[1];check("wake-live-source-must-match-current-pad-window",!ownerResumeControlsHealthy());windows[0]=actual;
 ownerResumeTimerProc(owner.load(),WM_TIMER,timer,0);check("wake-old-timer-cannot-restart-completed-episode",!ownerResumeRecovery.timer&&ownerResumeRecovery.repairs==1&&!fixtureOwnerResumeReady(101));
 queueOwnerResumeRebind();check("wake-late-resume-notification-cannot-rebuild-verified-sources",!ownerRebindQueued&&!consumeOwnerRebind(ownerLifecycleEpoch)&&ownerResumeControlsHealthy());
 ownerOnSuspend();queueOwnerResumeRebind();pump();autoHide.lastTouch=GetTickCount64()-15001;autoHideSetHidden(true);
 fixtureOwnerResumeReady(102);fireWake();fireWake();check("wake-auto-hidden-artwork-is-healthy-not-a-missing-source",autoHide.hidden&&ownerResumeRecovery.repairs==0&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer);
 ownerOnSuspend();queueOwnerResumeRebind();pump();fixtureOwnerResumeReady(102);
 const auto timerEnd=GetTickCount64()+2000;
 while(ownerResumeRecovery.pending&&GetTickCount64()<timerEnd){pump();MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
 check("wake-real-timer-dispatch-verifies-and-retires",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerResumeRecovery.clean==2&&ownerResumeRecovery.repairs==0);
 ownerOnSuspend();queueOwnerResumeRebind();pump();fixtureOwnerResumeReady(103);const auto old=ownerResumeRecovery.timer;ownerOnSuspend();ownerResumeTimerProc(owner.load(),WM_TIMER,old,0);
 check("wake-suspend-withdraws-timer-and-rejects-late-callback",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer);
 queueOwnerResumeRebind();const auto delayedEpoch=ownerLifecycleEpoch;fixtureOwnerResumeReady(103);
 check("wake-Ready-supersedes-already-queued-early-rebind",!consumeOwnerRebind(delayedEpoch)&&!ownerRebindQueued&&ownerResumeRecovery.pending);pump();cancelOwnerResumeRecovery("fixture-next-case");ownerOnSuspend();
 queueOwnerResumeRebind();pump();fixtureOwnerResumeReady(103);fixturePower.flags|=ymcc::sleeppower::kNotificationPending;fireWake();
 check("wake-shared-power-gate-cancels-before-repair",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerResumeRecovery.repairs==0);fixturePower.flags&=~ymcc::sleeppower::kNotificationPending;ownerOnSuspend();
 ownerOnSuspend();queueOwnerResumeRebind();pump();fixtureOwnerResumeReady(103);
 const auto sharedDeadline=ownerResumeRecovery.deadline,sharedTimer=ownerResumeRecovery.timer;
 ++fixturePower.publicationRevision;
 check("wake-diagnostic-publication-keeps-current-scope-and-budget",ownerResumeCurrent()&&ownerResumeRecovery.deadline==sharedDeadline&&ownerResumeRecovery.timer==sharedTimer);
 ++fixturePower.observedNotificationSequence;fireWake();
 check("wake-same-generation-notification-revokes-old-episode",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerResumeRecovery.repairs==0);
 const auto releaseEpoch=ownerLifecycleEpoch;
 ownerSharedPowerChanged(ymcc::sleeppower::SharedPowerV1{});const auto withdrawnEpoch=ownerLifecycleEpoch;
 ownerSharedPowerChanged(ymcc::sleeppower::SharedPowerV1{});
 check("wake-common-nonready-delivery-releases-only-once",withdrawnEpoch==releaseEpoch+1&&ownerLifecycleEpoch==withdrawnEpoch&&!sentKeys&&!sentClick);
 fixturePower.generation=104;fixturePower.phase=0;fixturePower.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
 ownerSharedPowerChanged(fixturePower);const auto commonDeadline=ownerResumeRecovery.deadline;
 ownerSharedPowerChanged(fixturePower);
 check("wake-common-ready-delivery-keeps-one-recovery-budget",ownerResumeRecovery.pending&&ownerResumeRecovery.deadline==commonDeadline&&ownerResumeRecovery.powerScope.generation==104);
 ownerSharedPowerChanged(ymcc::sleeppower::SharedPowerV1{});ownerSharedPowerChanged(fixturePower);
 check("wake-query-cancel-without-new-suspend-does-not-refund-repair-window",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerResumeRecovery.deadline==commonDeadline);
 ++fixturePower.suspendSerial;fixturePower.generation=105;
 ownerSharedPowerChanged(fixturePower);
 check("wake-coalesced-nonready-ready-retires-old-owner-from-shared-serial",ownerResumeRecovery.pending&&ownerResumeRecovery.powerScope.suspendSerial==fixturePower.suspendSerial);
 cancelOwnerResumeRecovery("fixture-next-case");
 inputPowerAllowed=[] {return false;};
 const auto blockedInputs=injectedCount;Action blocked;blocked.keys=1;blocked.dx=20;blocked.tap=true;output(blocked);
 check("wake-common-closed-gate-forbids-new-OS-input",injectedCount==blockedInputs&&!sentKeys&&!sentClick);
 const auto buttonWork=standaloneRequests.load();buttonEventAtHit(getConfig(),2,998,0,true);buttonEventAtHit(getConfig(),2,998,2,true);
 check("wake-common-closed-gate-forbids-dedicated-key-dispatch",standaloneRequests.load()==buttonWork&&!buttonContacts[2]&&!screenButtonMask.load());
 check("wake-common-closed-gate-forbids-virtual-contact-output",!snapshot().present);
 inputPowerAllowed=nullptr;
 cancelOwnerResumeRecovery("fixture-next-case");
 queueOwnerResumeRebind();pump();fixtureOwnerResumeReady(104);cfg.masterEnabled=false;configure(cfg,1,true);fireWake();
 check("wake-explicit-OFF-stays-OFF-without-repair",!buttonsVisible()&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerResumeRecovery.repairs==0);
 cfg.masterEnabled=true;configure(cfg,1,true);ownerOnSuspend();queueOwnerResumeRebind();pump();fixtureOwnerResumeReady(105);
 setContext("elite",true);fireWake();check("wake-profile-switch-cancels-old-episode",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer);setContext("steamdeck",true);configure(cfg,1,true);
 ownerOnSuspend();queueOwnerResumeRebind();pump();fixtureOwnerResumeReady(106);ownerResumeRecovery.deadline=GetTickCount64();fireWake();
 check("wake-expired-window-never-repairs-or-keeps-timer",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerResumeRecovery.repairs==0&&std::string(ownerResumeRecovery.status)=="expired");
 ownerOnSuspend();queueOwnerResumeRebind();pump();fixtureOwnerResumeReady(107);
 for(int attempt=0;attempt<4;++attempt){ShowWindow(windows[0],SW_HIDE);fireWake();}
 check("wake-repeated-failure-stops-after-three-repairs",ownerResumeRecovery.repairs==3&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&std::string(ownerResumeRecovery.status)=="failed");
 refresh();persistentRuntimeCases();ownerOnSuspend();queueOwnerResumeRebind();pump();fixtureOwnerResumeReady(108);const auto exitTimer=ownerResumeRecovery.timer;quiesceForOwner();ownerResumeTimerProc(owner.load(),WM_TIMER,exitTimer,0);
 check("wake-exit-cancels-timer-and-does-not-reshow",inputTeardown&&!initialized&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&!buttonsVisible());
 details.push_back({{"wakeRecovery",ownerResumeRecoveryState()},{"failureInjectionScope","exported missing HWND/source state only, not the original COM cause"}});
 shutdownForOwner();
}
static void lateFaultCases(){
 Config cfg=getConfig();cfg.masterEnabled=true;cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=0;configure(cfg,1,true);
 fixturePowerCallbacks();ownerOnSuspend();queueOwnerResumeRebind();pump();fixtureOwnerResumeReady(210);fireWake();fireWake();
 const auto saved=configJson(getConfig());const auto healthyDeadline=ownerResumeRecovery.deadline;
 check("late-fault-wake-verified-healthy-before-fault",!ownerResumeRecovery.pending&&ownerResumeRecovery.repairs==0&&ownerResumeRecovery.clean==2&&ownerResumeControlsHealthy());
 // Retire only the original input source and hide its input/artwork, the same
 // observable failure state as field logs. This is not the Windows 0x8000FFFF cause.
 nativeInputClose(0);ShowWindow(windows[0],SW_HIDE);ShowWindow(outlines[0],SW_HIDE);windowError=ERROR_NOT_READY;
 ++nativeInputFailure.sequence;nativeInputFailure.error=ERROR_NOT_READY;nativeInputFailure.stage="fixture-retired-source";
 const bool lateStarted=ownerNativeInputFailed();
 check("late-fault-after-healthy-wake-starts-bounded-recovery",lateStarted&&ownerResumeRecovery.pending);
#ifdef YMCC_LATE_FAULT_OLD_CONTROL
 if(!lateStarted)return;
#endif
 const auto firstTimer=ownerResumeRecovery.timer,firstDeadline=ownerResumeRecovery.deadline;
 const bool repeated=ownerNativeInputFailed();
 check("late-fault-repeat-cannot-rearm-active-window",!repeated&&firstTimer==ownerResumeRecovery.timer&&firstDeadline==ownerResumeRecovery.deadline);
 fireWake();fireWake();fireWake();
 check("late-fault-first-repair-restores-same-intent",ownerResumeControlsHealthy()&&!ownerResumeRecovery.pending&&ownerResumeRecovery.repairs==1&&saved==configJson(getConfig()));
 details.push_back({{"firstLateRecovery",ownerResumeRecoveryState()},{"healthyDeadline",healthyDeadline}});
 WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"YMCC_LateFaultPeer";RegisterClassW(&wc);
 HWND peer=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,wc.lpszClassName,L"",WS_POPUP,0,0,80,80,nullptr,nullptr,wc.hInstance,nullptr);
 ymcc::screeninput::Surface sentinel;const bool peerBound=peer&&sentinel.attach(nativeInputRuntime,peer,99,nullptr,nullptr);
 const auto shared=winrt::get_abi(nativeInputRuntime.compositor),queue=winrt::get_abi(winrt::Windows::System::DispatcherQueue::GetForCurrentThread());
 release();for(int side=0;side<2;++side)nativeInputClose(side);buttonsNativeInputDisable();nativePadInputRuntime.compositor.Close();refresh(true);
 check("late-fault-real-closed-compositor-starts-next-remaining-episode",windowError&&ownerResumeRecovery.pending);
 fireWake();fireWake();fireWake();fireWake();
 check("late-fault-remaining-two-attempts-can-rebuild-owned-runtime",ownerResumeControlsHealthy()&&!ownerResumeRecovery.pending&&ownerResumeRecovery.repairs==1&&ownerResumeRecovery.runtimeRebuilds==1);
 check("late-fault-shared-monitor-runtime-and-dispatcher-survive",peerBound&&sentinel.active()&&sentinel.sync()&&shared==winrt::get_abi(nativeInputRuntime.compositor)&&queue==winrt::get_abi(winrt::Windows::System::DispatcherQueue::GetForCurrentThread()));
 details.push_back({{"secondLateRecovery",ownerResumeRecoveryState()}});
 nativeInputClose(0);ShowWindow(windows[0],SW_HIDE);windowError=ERROR_NOT_READY;++nativeInputFailure.sequence;
 check("late-fault-last-cumulative-repair-is-still-available",ownerNativeInputFailed()&&ownerResumeRecovery.pending);
 fireWake();fireWake();fireWake();
 check("late-fault-last-repair-does-not-rebuild-again",ownerResumeControlsHealthy()&&ownerResumeRecovery.repairs==1&&ownerResumeRecovery.runtimeRebuilds==0);
 windowError=ERROR_NOT_READY;++nativeInputFailure.sequence;
 check("late-fault-three-cumulative-repairs-do-not-refund",!ownerNativeInputFailed()&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer);
 const auto scope=fixturePower;fixturePower.flags|=ymcc::sleeppower::kNotificationPending;
 check("late-fault-closed-shared-gate-never-repairs",!ownerNativeInputFailed()&&!ownerResumeRecovery.pending);fixturePower=scope;
 ++fixturePower.publicationRevision;
 check("late-fault-diagnostic-republication-does-not-refund",!ownerNativeInputFailed()&&!ownerResumeRecovery.pending);
 fixturePower.generation=211;refresh(true);check("late-fault-fresh-source-restores-before-independent-OFF",ownerResumeControlsHealthy());
 windowError=ERROR_NOT_READY;++nativeInputFailure.sequence;
 check("late-fault-new-power-generation-has-finite-allowance",ownerNativeInputFailed()&&ownerResumeRecovery.pending);
 const auto oldTimer=ownerResumeRecovery.timer;cfg.masterEnabled=false;configure(cfg,1,true);fireWake();
 check("late-fault-OFF-stops-pending-work-without-show",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&!buttonsVisible());
 ownerResumeTimerProc(owner.load(),WM_TIMER,oldTimer,0);
 check("late-fault-old-timer-cannot-revive-OFF",!ownerResumeRecovery.timer&&!buttonsVisible());
 sentinel.close();check("late-fault-peer-source-released",sentinel.terminalReleased());if(peer)DestroyWindow(peer);
 ownerPowerCapture=nullptr;ownerPowerCurrent=nullptr;
}
static void cumulativeFaultCases(){
 Config cfg=getConfig();cfg.masterEnabled=true;cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=0;
 configure(cfg,1,true);fixturePowerCallbacks();ownerOnSuspend();queueOwnerResumeRebind();pump();fixtureOwnerResumeReady(310);fireWake();fireWake();
 const auto settings=configJson(getConfig());
 check("cumulative-healthy-wake-uses-zero-repairs",!ownerResumeRecovery.pending&&ownerResumeRecovery.clean==2&&ownerResumeControlsHealthy());if(!passed)return;
 WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"YMCC_CumulativeFaultPeer";RegisterClassW(&wc);
 HWND peer=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,wc.lpszClassName,L"",WS_POPUP,0,0,80,80,nullptr,nullptr,wc.hInstance,nullptr);
 ymcc::screeninput::Surface sentinel;const bool peerBound=peer&&sentinel.attach(nativeInputRuntime,peer,99,nullptr,nullptr);
 const auto shared=winrt::get_abi(nativeInputRuntime.compositor),queue=winrt::get_abi(winrt::Windows::System::DispatcherQueue::GetForCurrentThread());
 const auto retire=[](){nativeInputClose(0);ShowWindow(windows[0],SW_HIDE);ShowWindow(outlines[0],SW_HIDE);windowError=ERROR_NOT_READY;++nativeInputFailure.sequence;nativeInputFailure.error=ERROR_NOT_READY;nativeInputFailure.stage="fixture-retired-source";};
 const auto cleanup=[&](){sentinel.close();check("cumulative-peer-source-released",sentinel.terminalReleased());if(peer)DestroyWindow(peer);};
 for(unsigned episode=1;episode<=3;++episode){
  retire();const bool started=ownerNativeInputFailed();check("cumulative-episode-"+std::to_string(episode)+"-starts-within-shared-limit",started);if(!passed){cleanup();return;}
  const auto deadline=ownerResumeRecovery.deadline;const auto timer=ownerResumeRecovery.timer;const bool repeat=ownerNativeInputFailed();
  check("cumulative-episode-"+std::to_string(episode)+"-repeat-cannot-rearm",!repeat&&deadline==ownerResumeRecovery.deadline&&timer==ownerResumeRecovery.timer);if(!passed){cleanup();return;}
  fireWake();fireWake();fireWake();
  const auto state=ownerResumeRecoveryState();details.push_back({{"episode",episode},{"recovery",state}});
  check("cumulative-episode-"+std::to_string(episode)+"-recovers-in-one-repair",!ownerResumeRecovery.pending&&ownerResumeRecovery.repairs==1&&ownerResumeRecovery.clean==2&&ownerResumeControlsHealthy());if(!passed){cleanup();return;}
  check("cumulative-episode-"+std::to_string(episode)+"-rebuild-only-at-second-total-repair",ownerResumeRecovery.runtimeRebuilds==(episode==2?1u:0u)&&state["faultBudget"].value("repairs",0u)==episode&&state["faultBudget"].value("runtimeRebuilds",0u)==(episode>=2?1u:0u));if(!passed){cleanup();return;}
  check("cumulative-episode-"+std::to_string(episode)+"-peer-and-settings-preserved",peerBound&&sentinel.active()&&sentinel.sync()&&shared==winrt::get_abi(nativeInputRuntime.compositor)&&queue==winrt::get_abi(winrt::Windows::System::DispatcherQueue::GetForCurrentThread())&&settings==configJson(getConfig())&&!sentKeys&&!sentClick&&!screenButtonMask.load());if(!passed){cleanup();return;}
 }
 retire();check("cumulative-fourth-fault-refused-after-three-actual-repairs",!ownerNativeInputFailed()&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer);
 ++fixturePower.publicationRevision;check("cumulative-publication-does-not-refund-or-repeat-rebuild",!ownerNativeInputFailed()&&ownerResumeRecoveryState()["faultBudget"].value("runtimeRebuilds",0u)==1);
 auto off=cfg;off.masterEnabled=false;configure(off,1,true);configure(cfg,1,true);windowError=ERROR_NOT_READY;++nativeInputFailure.sequence;
 check("cumulative-manual-OFF-ON-does-not-reset-automatic-budget",!ownerNativeInputFailed()&&!ownerResumeRecovery.pending);
 ++fixturePower.generation;windowError=ERROR_NOT_READY;++nativeInputFailure.sequence;
 check("cumulative-new-power-scope-has-independent-finite-budget",ownerNativeInputFailed());fireWake();fireWake();fireWake();
 check("cumulative-first-repair-in-new-scope-does-not-rebuild",ownerResumeControlsHealthy()&&ownerResumeRecovery.repairs==1&&ownerResumeRecovery.runtimeRebuilds==0&&ownerResumeRecoveryState()["faultBudget"].value("runtimeRebuilds",0u)==0);
 // Failure persists within one episode: the original second-step owned rebuild
 // must still work, including an actually closed compositor (RO_E_CLOSED).
 release();for(int side=0;side<2;++side)nativeInputClose(side);buttonsNativeInputDisable();nativePadInputRuntime.compositor.Close();refresh(true);
 check("cumulative-actual-closed-runtime-starts-after-first-repair",windowError&&ownerResumeRecovery.pending);fireWake();fireWake();fireWake();
 check("cumulative-actual-closed-runtime-recovers-on-next-total-repair",ownerResumeControlsHealthy()&&!ownerResumeRecovery.pending&&ownerResumeRecovery.repairs==1&&ownerResumeRecovery.runtimeRebuilds==1&&ownerResumeRecoveryState()["faultBudget"].value("repairs",0u)==2);
 check("cumulative-peer-still-healthy-after-real-runtime-close",sentinel.active()&&sentinel.sync()&&shared==winrt::get_abi(nativeInputRuntime.compositor));
 cleanup();ownerPowerCapture=nullptr;ownerPowerCurrent=nullptr;
}
static void inputMailboxCases(){
  using Input=ymcc::screeninput::InputChannel::Input;auto input=std::make_shared<Input>();
  struct Receiver{std::shared_ptr<Input> input;std::array<ymcc::screeninput::Event,64> events{};unsigned count=0;bool lost=false;};Receiver receiver{input};
  WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YMCC_ActualInputMailboxReceiver";
  wc.lpfnWndProc=[](HWND h,UINT m,WPARAM w,LPARAM l)->LRESULT{if(m==WM_NCCREATE){SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);return TRUE;}auto* r=(Receiver*)GetWindowLongPtrW(h,GWLP_USERDATA);if(r&&m==ymcc::screeninput::kChannelInputMessage){if(w==r->input->cookie)r->count=r->input->take(r->events,r->lost);return 0;}return DefWindowProcW(h,m,w,l);};RegisterClassW(&wc);
  HWND h=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,&receiver);input->target=h;
  const auto event=[](ymcc::screeninput::Phase phase,DWORD time){return ymcc::screeninput::Event{0,7,PT_TOUCH,phase,10.f,20.f,false,time,true};};
  std::thread publisher([&]{input->publish(event(ymcc::screeninput::Phase::Down,1000),input->epoch.load());for(DWORD i=1001;i<1500;++i)input->publish(event(ymcc::screeninput::Phase::Move,i),input->epoch.load());input->publish(event(ymcc::screeninput::Phase::Up,1500),input->epoch.load());});publisher.join();pump();
  check("channel-mailbox-real-worker-to-UI-preserves-edges-and-source-time",!receiver.lost&&receiver.count==3&&receiver.events[0].phase==ymcc::screeninput::Phase::Down&&receiver.events[1].sourceTime==1499&&receiver.events[2].phase==ymcc::screeninput::Phase::Up&&receiver.events[2].sourceTime==1500);
  auto old=input->epoch.load();input->publish(event(ymcc::screeninput::Phase::Down,1600),old);input->fence();input->admitted=true;input->publish(event(ymcc::screeninput::Phase::Down,1601),old);pump();
  check("channel-mailbox-cancel-fences-queued-and-in-flight-old-epoch",receiver.count==0&&!receiver.lost);
  for(DWORD i=0;i<70;++i)input->publish(event(i%2?ymcc::screeninput::Phase::Up:ymcc::screeninput::Phase::Down,1700+i),input->epoch.load());pump();
  check("channel-mailbox-edge-overflow-is-explicit-cancel-not-orphan-UP",receiver.lost&&receiver.count==0);
  DestroyWindow(h);input->fence();
}
static void independentResetCases(){
 Config cfg=getConfig();cfg.masterEnabled=true;cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=0;configure(cfg,1,true);
 const auto saved=configJson(getConfig());const DWORD uiThread=GetCurrentThreadId();
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YMCC_FullResetUnrelatedPeer";RegisterClassW(&wc);
 HWND peer=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,wc.lpszClassName,L"",WS_POPUP,0,0,100,100,nullptr,nullptr,wc.hInstance,nullptr);ymcc::screeninput::Surface sentinel;
 check("full-reset-original-controls-and-unrelated-peer",ownerResumeControlsHealthy()&&!nativePadInputRuntime.inputChannel&&sentinel.attach(nativeInputRuntime,peer,99,nullptr,nullptr));
 const auto shared=winrt::get_abi(nativeInputRuntime.compositor),queue=winrt::get_abi(winrt::Windows::System::DispatcherQueue::GetForCurrentThread());
 fixtureOwnerResumeReady(191115);nativePadInputRuntime.compositor.Close();refresh(true);
 check("full-reset-real-closed-runtime-fault-arms-bounded-verifier",windowError&&ownerResumeRecovery.pending);
 fireWake();check("full-reset-first-repair-keeps-normal-input-path",ownerResumeRecovery.repairs==1&&!nativePadInputRuntime.isolatedInput);
 fireWake();check("full-reset-second-repair-rebuilds-compositor-once",ownerResumeRecovery.repairs==2&&ownerResumeRecovery.runtimeRebuilds==1&&!windowError&&ownerResumeControlsHealthy());
 nativePadInputRuntime.compositor.Close();refresh(true);fireWake();
 details.push_back({{"fullResetRecovery",ownerResumeRecoveryState()},{"fullResetState",state()}});
 check("full-reset-third-repair-creates-distinct-native-input-apartment",ownerResumeRecovery.repairs==3&&ownerResumeRecovery.channelResets==1&&nativePadInputRuntime.inputChannel&&nativePadInputRuntime.inputChannel->healthy()&&nativePadInputRuntime.inputChannel->threadId()!=uiThread);
 if(!passed){sentinel.close();DestroyWindow(peer);shutdownForOwner();return;}
 check("full-reset-all-selected-real-sources-rebound",!windowError&&ownerResumeControlsHealthy()&&nativeInputSurfaces[0].usesIndependentInput()&&nativeInputSurfaces[1].usesIndependentInput()&&nativeButtonSurfaces[1].usesIndependentInput()&&nativeButtonSurfaces[2].usesIndependentInput());
 fireWake();fireWake();check("full-reset-two-clean-checks-stop-original-verifier",ownerResumeRecovery.clean==2&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&std::string(ownerResumeRecovery.status)=="recovered");
 check("full-reset-no-shared-queue-or-peer-change",sentinel.sync()&&sentinel.active()&&shared==winrt::get_abi(nativeInputRuntime.compositor)&&queue==winrt::get_abi(winrt::Windows::System::DispatcherQueue::GetForCurrentThread()));
 check("full-reset-selection-signals-and-mouse-pass-preserved",saved==configJson(getConfig())&&!sentKeys&&!sentClick&&!screenButtonMask.load()&&nativeInputSurfaces[0].ensureMousePass()&&nativeButtonSurfaces[1].ensureMousePass());
 const auto oldBudget=ownerFaultBudget.repairs;windowError=ERROR_NOT_READY;++nativeInputFailure.sequence;check("full-reset-cannot-refund-or-create-fourth-automatic-repair",!ownerNativeInputFailed()&&ownerFaultBudget.repairs==oldBudget&&ownerFaultBudget.channelResets==1);windowError=0;
 auto off=cfg;off.masterEnabled=false;configure(off,1,true);check("full-reset-OFF-hides-input-and-artwork-without-new-work",!buttonsVisible()&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer);
 configure(cfg,1,true);check("full-reset-explicit-ON-rebinds-current-channel",!windowError&&ownerResumeControlsHealthy());
 inputMailboxCases();
 const auto beforeEvents=eventCount;nativeInputSurfaces[0].drainIndependent(0);check("full-reset-stale-input-cookie-has-no-authority",eventCount==beforeEvents);
 sentinel.close();DestroyWindow(peer);shutdownForOwner();check("full-reset-joins-owned-thread-before-three-terminal-confirmations",!nativePadInputRuntime.inputChannel&&exitInputReleaseState().value("verified",false));
 ownerPowerCapture=nullptr;ownerPowerCurrent=nullptr;
}
static void mailboxQuotaCases(){
  using Input=ymcc::screeninput::InputChannel::Input;auto input=std::make_shared<Input>();
  struct Receiver{std::shared_ptr<Input> input;bool down=false,cancelled=false;unsigned deliveries=0;};Receiver receiver{input};
  WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YMCC_QuotaInputMailboxReceiver";
  wc.lpfnWndProc=[](HWND h,UINT m,WPARAM w,LPARAM l)->LRESULT{if(m==WM_NCCREATE){SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);return TRUE;}auto* r=(Receiver*)GetWindowLongPtrW(h,GWLP_USERDATA);if(r&&m==ymcc::screeninput::kChannelInputMessage){if(w!=r->input->cookie)return 0;std::array<ymcc::screeninput::Event,64> events{};bool lost=false;const auto n=r->input->take(events,lost);++r->deliveries;if(lost){r->down=false;r->cancelled=true;}else for(unsigned i=0;i<n;++i){if(events[i].phase==ymcc::screeninput::Phase::Down)r->down=true;if(events[i].phase==ymcc::screeninput::Phase::Up)r->down=false;}return 0;}return DefWindowProcW(h,m,w,l);};RegisterClassW(&wc);
  HWND h=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,&receiver);input->target=h;
  input->publish(ymcc::screeninput::Event{0,7,PT_TOUCH,ymcc::screeninput::Phase::Down,1.f,1.f,false,1000,true},input->epoch.load());pump();check("notify-quota-prior-DOWN-really-delivered-to-owner",receiver.down&&receiver.deliveries==1);
  unsigned posted=0;SetLastError(0);while(posted<20000&&PostMessageW(h,WM_APP+620,0,0))++posted;const auto failure=GetLastError();
  check("notify-quota-real-Win32-posted-limit-reached",posted>0&&posted<20000&&failure==ERROR_NOT_ENOUGH_QUOTA);
  std::thread release([&]{input->publish(ymcc::screeninput::Event{0,7,PT_TOUCH,ymcc::screeninput::Phase::Up,1.f,1.f,false,1001,true},input->epoch.load());});release.join();pump();
  check("notify-quota-final-UP-failure-delivers-cancel-without-another-input",!receiver.down&&receiver.cancelled&&receiver.deliveries==2);
  details.push_back({{"quotaPosted",posted},{"quotaFailure",failure},{"cancelled",receiver.cancelled},{"deliveries",receiver.deliveries}});input->fence();DestroyWindow(h);shutdownForOwner();check("notify-quota-terminal-release",exitInputReleaseState().value("verified",false));
}
static HANDLE stopHoldEntered=nullptr,stopHoldRelease=nullptr;static WNDPROC stopOriginalProc=nullptr;
static LRESULT CALLBACK stopHoldProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_APP+624){SetEvent(stopHoldEntered);WaitForSingleObject(stopHoldRelease,4000);return 0;}
 return CallWindowProcW(stopOriginalProc,h,m,w,l);
}
static void channelStopCases(){
 ymcc::screeninput::InputChannel channel;HRESULT error=S_OK;
 check("stop-channel-starts-real-owned-input-apartment",channel.start(error)&&channel.healthy());
 HWND target=nullptr;for(HWND h=FindWindowExW(HWND_MESSAGE,nullptr,L"YeManTouchpadInputApartment",nullptr);h;h=FindWindowExW(HWND_MESSAGE,h,L"YeManTouchpadInputApartment",nullptr)){DWORD pid=0;if(GetWindowThreadProcessId(h,&pid)==channel.threadId()&&pid==GetCurrentProcessId()){target=h;break;}}
 check("stop-channel-locates-only-owned-message-window",target!=nullptr);if(!passed){channel.shutdown();shutdownForOwner();return;}
 stopHoldEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);stopHoldRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 stopOriginalProc=(WNDPROC)SetWindowLongPtrW(target,GWLP_WNDPROC,(LONG_PTR)stopHoldProc);
 const bool requested=SendNotifyMessageW(target,WM_APP+624,0,0)!=FALSE;const bool held=requested&&WaitForSingleObject(stopHoldEntered,1000)==WAIT_OBJECT_0;
 check("stop-channel-fixture-cooperatively-holds-only-its-worker",held&&stopOriginalProc);
 unsigned posted=0;DWORD failure=0;if(held){while(posted<20000&&PostMessageW(target,WM_APP+620,0,0))++posted;failure=GetLastError();}
 check("stop-channel-real-worker-posted-queue-is-full",posted>0&&posted<20000&&failure==ERROR_NOT_ENOUGH_QUOTA);
 std::thread release([]{Sleep(150);SetEvent(stopHoldRelease);});const auto started=GetTickCount64();const bool stopped=channel.shutdown();const auto elapsed=GetTickCount64()-started;release.join();
 check("stop-channel-full-queue-still-receives-stop-and-joins",stopped&&!channel.healthy());
 details.push_back({{"posted",posted},{"failure",failure},{"stopped",stopped},{"elapsedMs",elapsed}});
 // The necessary old control is cleaned by a separate explicit sent signal;
 // this is not credited as the old production shutdown succeeding.
 if(!stopped&&IsWindow(target))SendNotifyMessageW(target,WM_APP+523,0,0);
 const bool final=channel.shutdown();check("stop-channel-owned-resource-cleanup-after-control",final);
 check("stop-channel-repeat-close-stays-clean",channel.shutdown());
 CloseHandle(stopHoldEntered);CloseHandle(stopHoldRelease);stopHoldEntered=stopHoldRelease=nullptr;stopOriginalProc=nullptr;
 shutdownForOwner();check("stop-channel-original-owner-terminal-release",exitInputReleaseState().value("verified",false));
}
static void channelCommandCases(){
 ymcc::screeninput::InputChannel channel;HRESULT error=S_OK;check("command-quota-real-owned-input-apartment-starts",channel.start(error));
 HWND target=nullptr;for(HWND h=FindWindowExW(HWND_MESSAGE,nullptr,L"YeManTouchpadInputApartment",nullptr);h;h=FindWindowExW(HWND_MESSAGE,h,L"YeManTouchpadInputApartment",nullptr)){DWORD pid=0;if(GetWindowThreadProcessId(h,&pid)==channel.threadId()&&pid==GetCurrentProcessId()){target=h;break;}}
 check("command-quota-only-owned-message-window-found",target!=nullptr);if(!passed){channel.shutdown();shutdownForOwner();return;}
 auto visual=nativePadInputRuntime.compositor.CreateSpriteVisual();visual.Size({64.f,64.f});std::shared_ptr<ymcc::screeninput::InputChannel::Input> input;
 check("command-quota-attaches-real-WinRT-source",channel.attach(visual,owner.load(),99,input,error)&&input&&input->ready.load());
 stopHoldEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);stopHoldRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);stopOriginalProc=(WNDPROC)SetWindowLongPtrW(target,GWLP_WNDPROC,(LONG_PTR)stopHoldProc);
 const bool held=SendNotifyMessageW(target,WM_APP+624,0,0)&&WaitForSingleObject(stopHoldEntered,1000)==WAIT_OBJECT_0;check("command-quota-owned-worker-cooperatively-held",held);
 unsigned posted=0;DWORD failure=0;if(held){while(posted<20000&&PostMessageW(target,WM_APP+620,0,0))++posted;failure=GetLastError();}check("command-quota-real-posted-limit-reached",posted>0&&posted<20000&&failure==ERROR_NOT_ENOUGH_QUOTA);
 std::thread release([]{Sleep(150);SetEvent(stopHoldRelease);});const auto started=GetTickCount64();const bool synced=channel.sync(input,error);const auto elapsed=GetTickCount64()-started;release.join();
 check("command-quota-real-source-sync-reaches-worker-and-returns",synced&&error==S_OK&&channel.healthy());details.push_back({{"posted",posted},{"postFailure",failure},{"sync",synced},{"error",(DWORD)error},{"elapsedMs",elapsed}});
 check("command-quota-source-closed-on-owning-STA",channel.close(input,error));input.reset();check("command-quota-thread-joined-and-queue-returned",channel.shutdown());
 CloseHandle(stopHoldEntered);CloseHandle(stopHoldRelease);stopHoldEntered=stopHoldRelease=nullptr;stopOriginalProc=nullptr;shutdownForOwner();check("command-quota-original-owner-terminal-release",exitInputReleaseState().value("verified",false));
}
#ifdef YMCC_SCREEN_TOUCHPAD_OVERFLOW_FIXTURE
#ifdef YMCC_CHANNEL_CLEANUP_RECEIPT_FIXTURE
#include "screen_touchpad_cleanup_receipt_cases.inc"
#else
static void manualChannelRecoveryCases(){
 using Input=ymcc::screeninput::InputChannel::Input;using Phase=ymcc::screeninput::Phase;
 Config cfg=getConfig();cfg.masterEnabled=true;cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=0;configure(cfg,1,true);
 const auto saved=configJson(getConfig());fixtureOwnerResumeReady(170101);
 nativePadInputRuntime.compositor.Close();refresh(true);fireWake();fireWake();
 nativePadInputRuntime.compositor.Close();refresh(true);fireWake();fireWake();fireWake();
 const auto channel=nativePadInputRuntime.inputChannel;
 check("manual-channel-three-real-repairs-establish-live-independent-worker",channel&&channel->healthy()&&ownerResumeControlsHealthy()&&ownerFaultBudget.repairs==3&&ownerFaultBudget.channelResets==1&&!ownerResumeRecovery.pending);
 if(!passed){shutdownForOwner();return;}
 const auto budget=ownerResumeRecoveryState()["faultBudget"];const auto shared=winrt::get_abi(nativeInputRuntime.compositor);
 const DWORD originalThread=channel->threadId();configure(cfg,1,true);
 check("manual-channel-healthy-explicit-selection-keeps-current-worker",channel->healthy()&&channel->threadId()==originalThread&&!channel->closedForSettings());
 const auto stop=[&]{
  HWND target=nullptr;for(HWND h=FindWindowExW(HWND_MESSAGE,nullptr,L"YeManTouchpadInputApartment",nullptr);h;h=FindWindowExW(HWND_MESSAGE,h,L"YeManTouchpadInputApartment",nullptr)){DWORD pid=0;if(GetWindowThreadProcessId(h,&pid)==channel->threadId()&&pid==GetCurrentProcessId()){target=h;break;}}
  HANDLE thread=OpenThread(SYNCHRONIZE,FALSE,channel->threadId());const bool sent=target&&SendNotifyMessageW(target,WM_APP+523,0,0);
  const DWORD wait=thread?WaitForSingleObject(thread,3000):WAIT_FAILED;if(thread)CloseHandle(thread);pump();
  return sent&&wait==WAIT_OBJECT_0&&!IsWindow(target)&&channel->closedForSettings();
 };
 const auto edge=[](const std::shared_ptr<Input>& input,UINT32 id){const auto epoch=input->epoch.load();std::thread publisher([=]{input->publish(ymcc::screeninput::Event{input->surface,id,PT_TOUCH,Phase::Down,30.f,40.f,false,GetTickCount(),true},epoch);});publisher.join();SendMessageW(input->target,ymcc::screeninput::kChannelInputMessage,input->cookie,0);};
 auto oldLeft=ymcc::screeninput::SurfaceFixtureAccess::input(nativeInputSurfaces[0]),oldRight=ymcc::screeninput::SurfaceFixtureAccess::input(nativeInputSurfaces[1]);edge(oldLeft,1101);edge(oldRight,1102);
 check("manual-channel-real-mailbox-contacts-held-before-independent-stop",nativeInputSurfaces[0].hasContacts()&&nativeInputSurfaces[1].hasContacts());
 const bool firstStop=stop();
 check("manual-channel-terminal-worker-clean-and-old-bindings-still-present",firstStop&&!channel->healthy()&&nativeInputSurfaces[0].root()==windows[0]&&nativeInputSurfaces[1].root()==windows[1]);
 check("manual-channel-retirement-releases-held-signals-before-settings",!nativeInputSurfaces[0].hasContacts()&&!nativeInputSurfaces[1].hasContacts()&&!sentKeys&&!sentClick&&!screenButtonMask.load());
 const bool direct=configure(cfg,1,true);
 check("manual-channel-direct-same-settings-detach-before-restarting",direct&&channel==nativePadInputRuntime.inputChannel&&channel->healthy()&&ownerResumeControlsHealthy()&&!windowError&&ymcc::screeninput::SurfaceFixtureAccess::input(nativeInputSurfaces[0])!=oldLeft);
 if(!passed){shutdownForOwner();return;}
 const auto count=eventCount;edge(oldLeft,1103);pump();
 check("manual-channel-late-old-input-cannot-affect-replacement",eventCount==count&&!nativeInputSurfaces[0].hasContacts());
 const bool secondStop=stop();const auto stoppedThread=channel->threadId();if(!configure(cfg,1))refresh();
 check("manual-channel-automatic-configure-does-not-restart-retired-worker",secondStop&&!channel->healthy()&&channel->threadId()==stoppedThread&&ownerResumeRecoveryState()["faultBudget"]==budget);
 auto off=cfg;off.masterEnabled=false;configure(off,1,true);
 check("manual-channel-explicit-OFF-never-reopens-retired-worker",!channel->healthy()&&channel->threadId()==stoppedThread&&!buttonsVisible());
 configure(cfg,1,true);
 check("manual-channel-explicit-ON-recovers-without-refunding-auto-budget",channel->healthy()&&ownerResumeControlsHealthy()&&!windowError&&ownerResumeRecoveryState()["faultBudget"]==budget);
 const bool thirdStop=stop();presentationSuspended.store(true);configure(cfg,1,true);
 check("manual-channel-presentation-lease-keeps-replacement-unstarted",thirdStop&&!channel->healthy()&&!buttonsVisible());
 presentationSuspended.store(false);refresh(true);
 check("manual-channel-lease-return-establishes-current-controls",channel->healthy()&&ownerResumeControlsHealthy()&&!windowError);
 check("manual-channel-selection-budget-and-shared-compositor-preserved",saved==configJson(getConfig())&&ownerResumeRecoveryState()["faultBudget"]==budget&&shared==winrt::get_abi(nativeInputRuntime.compositor));
 details.push_back({{"manualChannelRecovery",ownerResumeRecoveryState()},{"firstThread",originalThread},{"finalThread",channel->threadId()},{"scope","Original owner and real worker Stop; mailbox edge port is not physical touch/capture proof"}});
 shutdownForOwner();check("manual-channel-terminal-release-confirmed-three-rounds",!nativePadInputRuntime.inputChannel&&exitInputReleaseState().value("verified",false));
 ownerPowerCapture=nullptr;ownerPowerCurrent=nullptr;
}
#endif // YMCC_CHANNEL_CLEANUP_RECEIPT_FIXTURE
static void workerRetirementCases(){
 using Input=ymcc::screeninput::InputChannel::Input;using Phase=ymcc::screeninput::Phase;
 Config cfg=getConfig();cfg.masterEnabled=true;cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=0;configure(cfg,1,true);
 release();for(int i=0;i<2;++i)nativeInputClose(i);buttonsNativeInputDisable();const bool isolated=nativePadInputRuntime.useFreshInputChannel();refresh(true);
 const auto channel=nativePadInputRuntime.inputChannel;auto left=ymcc::screeninput::SurfaceFixtureAccess::input(nativeInputSurfaces[0]),right=ymcc::screeninput::SurfaceFixtureAccess::input(nativeInputSurfaces[1]);
 check("worker-retirement-real-independent-sources-ready",isolated&&left&&right&&channel&&channel->healthy()&&ownerResumeControlsHealthy());
 if(!passed){shutdownForOwner();return;}
 const auto edge=[](const std::shared_ptr<Input>& input,UINT32 id){const auto epoch=input->epoch.load();std::thread publisher([=]{input->publish(ymcc::screeninput::Event{input->surface,id,PT_TOUCH,Phase::Down,30.f,40.f,false,GetTickCount(),true},epoch);});publisher.join();SendMessageW(input->target,ymcc::screeninput::kChannelInputMessage,input->cookie,0);};
 edge(left,903);edge(right,904);
 bool held=false;{std::lock_guard<std::mutex> lock(mutex);held=core.pads[0].down&&core.pads[1].down;}
 check("worker-retirement-two-real-mailbox-contacts-held-before-stop",held&&nativeInputSurfaces[0].hasContacts()&&nativeInputSurfaces[1].hasContacts());
 HWND target=nullptr;for(HWND h=FindWindowExW(HWND_MESSAGE,nullptr,L"YeManTouchpadInputApartment",nullptr);h;h=FindWindowExW(HWND_MESSAGE,h,L"YeManTouchpadInputApartment",nullptr)){DWORD pid=0;if(GetWindowThreadProcessId(h,&pid)==channel->threadId()&&pid==GetCurrentProcessId()){target=h;break;}}
 // Stop only the fixture's owning event loop, without calling Surface close or
 // owner release. This models an independently terminated native dispatcher.
 unsigned quotaPosted=0;DWORD quotaError=0;
 if(workerRetirementQuota){while(quotaPosted<20000&&PostMessageW(owner.load(),WM_APP+620,0,0))++quotaPosted;quotaError=GetLastError();check("worker-retirement-real-UI-posted-queue-is-full",quotaPosted>0&&quotaPosted<20000&&quotaError==ERROR_NOT_ENOUGH_QUOTA);}
 HANDLE retiredThread=nullptr;
 if(workerRetirementQuota)retiredThread=OpenThread(SYNCHRONIZE|THREAD_QUERY_LIMITED_INFORMATION,FALSE,channel->threadId());
 const bool requested=target&&SendNotifyMessageW(target,WM_APP+523,0,0);const auto deadline=GetTickCount64()+3000;
 if(workerRetirementQuota){
  const DWORD waited=retiredThread?WaitForSingleObject(retiredThread,3000):WAIT_FAILED;
  // No PeekMessage/DispatchMessage occurs while the worker retires. Its posted
  // queue notification must fail; asynchronous sent notification bypasses it.
  const bool stillFull=!PostMessageW(owner.load(),WM_APP+620,0,0)&&GetLastError()==ERROR_NOT_ENOUGH_QUOTA;
  bool lossPending=false;{std::lock_guard<std::mutex> lock(left->mutex);lossPending=left->overflow&&left->posted;}
  bool heldBeforePump=false;{std::lock_guard<std::mutex> lock(mutex);heldBeforePump=core.pads[0].down&&core.pads[1].down;}
  check("worker-retirement-quota-worker-exited-before-UI-pump-and-loss-is-pending",retiredThread&&waited==WAIT_OBJECT_0&&stillFull&&lossPending&&heldBeforePump);
  if(retiredThread)CloseHandle(retiredThread);
 }
 while(GetTickCount64()<deadline){pump();bool heldNow=false;{std::lock_guard<std::mutex> lock(mutex);heldNow=core.pads[0].down||core.pads[1].down;}if(!channel->healthy()&&!IsWindow(target)&&!heldNow)break;MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
 bool released=false;{std::lock_guard<std::mutex> lock(mutex);released=!core.pads[0].down&&!core.pads[1].down&&!core.clicks[0].active&&!core.clicks[1].active&&!sentKeys&&!sentClick&&!screenButtonMask.load();}
 check("worker-retirement-owned-loop-ended-without-owner-close",requested&&!channel->healthy()&&!IsWindow(target)&&initialized&&!inputTeardown);
 check("worker-retirement-loop-stop-cancels-both-owned-contacts-without-polling",released&&!nativeInputSurfaces[0].hasContacts()&&!nativeInputSurfaces[1].hasContacts()&&!left->admitted.load()&&!right->admitted.load());
 details.push_back({{"requested",requested},{"quotaPosted",quotaPosted},{"quotaError",quotaError},{"released",released},{"leftAdmitted",left->admitted.load()},{"rightAdmitted",right->admitted.load()},{"leftError",(DWORD)nativeInputSurfaces[0].error},{"leftStage",nativeInputSurfaces[0].lastFailureStage()},{"threadHealthy",channel->healthy()},{"edgePort","Private fixture Input mailbox; real HWND dispatch/source close. No actual OS touch or physical capture proof."}});
 const auto before=eventCount;edge(left,905);pump();check("worker-retirement-new-down-cannot-enter-retired-source",eventCount==before);
 shutdownForOwner();check("worker-retirement-owner-cleanup-joins-and-confirms-release",!nativePadInputRuntime.inputChannel&&!channel->healthy()&&exitInputReleaseState().value("verified",false));
}
static void inputOverflowCases(){
 using Input=ymcc::screeninput::InputChannel::Input;using Phase=ymcc::screeninput::Phase;
 Config cfg=getConfig();cfg.masterEnabled=true;cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=0;configure(cfg,1,true);
 release();for(int i=0;i<2;++i)nativeInputClose(i);buttonsNativeInputDisable();const bool isolated=nativePadInputRuntime.useFreshInputChannel();refresh(true);
 struct Probe{int action=0;unsigned cancels=0;bool syncCalled=false,syncResult=false;};Probe probe;
 const auto sink=[](HWND h,const ymcc::screeninput::Event& event,void* context){
  nativeInputSink(h,event,nullptr);auto& p=*static_cast<Probe*>(context);if(event.phase!=Phase::Cancel)return;++p.cancels;const int action=p.action;p.action=0;
  if(action==1)throw std::runtime_error("fixture-cancel-sink-failure");
  if(action==4){p.action=5;throw std::runtime_error("fixture-first-cancel-failed");}
  if(action==5){p.syncCalled=true;p.syncResult=nativeInputSurfaces[0].sync();}
  if(action==6){p.syncCalled=true;p.syncResult=nativeInputSurfaces[0].sync();throw std::runtime_error("fixture-sync-then-cancel-failed");}
  if(action==2)queueOwnerDisplayRefresh(true);
  if(action==3)quiesceForOwner();
 };
 nativeInputClose(0);const bool attached=nativeInputSurfaces[0].attach(nativePadInputRuntime,windows[0],0,sink,&probe);
 auto input=ymcc::screeninput::SurfaceFixtureAccess::input(nativeInputSurfaces[0]);const auto channel=nativePadInputRuntime.inputChannel;
 check("input-overflow-actual-independent-source-and-owned-mailbox-ready",isolated&&attached&&input&&channel&&channel->healthy()&&nativeInputSurfaces[0].active());
 if(!passed){shutdownForOwner();return;}
 // Data edges are an explicit private fixture port. Real Input::publish,
 // HWND dispatch, Surface drain/cancel and production native sink are used.
 const auto send=[](const std::shared_ptr<Input>& target,Phase phase,UINT32 id,UINT64 epoch){
  std::thread publisher([=]{target->publish(ymcc::screeninput::Event{0,id,PT_TOUCH,phase,30.f,40.f,false,GetTickCount(),true},epoch);});publisher.join();
  SendMessageW(target->target,ymcc::screeninput::kChannelInputMessage,target->cookie,0);
 };
 const auto burst=[](const std::shared_ptr<Input>& target){const auto epoch=target->epoch.load();
  std::thread publisher([=]{for(unsigned i=0;i<70;++i)target->publish(ymcc::screeninput::Event{0,707,PT_TOUCH,i%2?Phase::Up:Phase::Down,35.f,45.f,false,GetTickCount(),true},epoch);});publisher.join();
  bool overflow=false;{std::lock_guard<std::mutex> lock(target->mutex);overflow=target->overflow;}
  SendMessageW(target->target,ymcc::screeninput::kChannelInputMessage,target->cookie,0);return overflow;
 };
 const auto down=[](){std::lock_guard<std::mutex> lock(mutex);return core.pads[0].down;};
 const auto released=[](){std::lock_guard<std::mutex> lock(mutex);return !core.pads[0].down&&!core.pads[1].down&&!core.clicks[0].active&&!core.clicks[1].active&&!sentKeys&&!sentClick&&!screenButtonMask.load();};
 send(input,Phase::Down,601,input->epoch.load());check("input-overflow-real-mailbox-down-reaches-production-core",down()&&nativeInputSurfaces[0].hasContacts());
 const auto oldEpoch=input->epoch.load();const bool overflow=burst(input);
 check("input-overflow-bounded-edge-loss-cancels-existing-contact",overflow&&released()&&!nativeInputSurfaces[0].hasContacts()&&probe.cancels==1);
 const auto oldCount=eventCount;send(input,Phase::Down,801,oldEpoch);
 check("input-overflow-old-epoch-cannot-replay-contact",eventCount==oldCount&&!down());
 send(input,Phase::Down,802,input->epoch.load());
 details.push_back({{"afterOverflowAdmitted",input->admitted.load()},{"sourceActive",nativeInputSurfaces[0].active()},{"sourceError",(DWORD)nativeInputSurfaces[0].error},{"lastFailureStage",nativeInputSurfaces[0].lastFailureStage()},{"nextDownObserved",down()}});
 check("input-overflow-fresh-touch-recovers-on-same-channel-without-manual-refresh",down()&&input->admitted.load()&&nativeInputSurfaces[0].active()&&channel==nativePadInputRuntime.inputChannel);
 if(!passed){shutdownForOwner();return;}
 send(input,Phase::Up,802,input->epoch.load());check("input-overflow-fresh-UP-clears-contact-ownership",!down()&&!nativeInputSurfaces[0].hasContacts());
 refresh(true);probe.action=1;send(input,Phase::Down,803,input->epoch.load());const bool failedCancel=burst(input);
 check("input-overflow-failed-cancel-sink-is-not-readmitted",failedCancel&&!input->admitted.load()&&!down()&&!nativeInputSurfaces[0].active()&&FAILED(nativeInputSurfaces[0].error));
 refresh(true);check("input-overflow-explicit-source-sync-can-restore-after-cancel-failure",input->admitted.load()&&ownerResumeControlsHealthy());
 refresh(true);probe.action=4;probe.syncCalled=probe.syncResult=false;
 send(input,Phase::Down,860,input->epoch.load());send(input,Phase::Down,861,input->epoch.load());const bool firstFailed=burst(input);
 check("input-overflow-first-failed-cancel-cannot-be-overwritten-by-later-sync",firstFailed&&probe.syncCalled&&!probe.syncResult&&!input->admitted.load()&&!down()&&!nativeInputSurfaces[0].hasContacts()&&nativeInputSurfaces[0].error==E_FAIL);
 refresh(true);probe.action=6;probe.syncCalled=probe.syncResult=false;send(input,Phase::Down,862,input->epoch.load());const bool syncFirst=burst(input);
 check("input-overflow-reentrant-sync-before-throw-never-reopens-gate",syncFirst&&probe.syncCalled&&!probe.syncResult&&!input->admitted.load()&&!down()&&FAILED(nativeInputSurfaces[0].error));
 if(!passed){shutdownForOwner();return;}
 refresh(true);probe.action=2;send(input,Phase::Down,804,input->epoch.load());const bool displayLoss=burst(input);
 check("input-overflow-display-cancel-reentry-cannot-readmit-before-settlement",displayLoss&&ownerDisplayRefresh.pending&&!input->admitted.load()&&!IsWindowVisible(windows[0])&&released());
 const auto end=GetTickCount64()+1500;while(ownerDisplayRefresh.pending&&GetTickCount64()<end){pump();MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
 check("input-overflow-deferred-display-owner-restores-current-channel",!ownerDisplayRefresh.pending&&input->admitted.load()&&ownerResumeControlsHealthy()&&channel==nativePadInputRuntime.inputChannel);
 probe.action=3;send(input,Phase::Down,805,input->epoch.load());const bool exitLoss=burst(input);
 check("input-overflow-terminal-cancel-reentry-never-readmits",exitLoss&&inputTeardown&&!initialized&&!input->admitted.load()&&!nativeInputSurfaces[0].active()&&released());
 const auto retired=windows[0];const auto child=input->target;shutdownForOwner();const auto finalCount=eventCount;send(input,Phase::Down,806,input->epoch.load());
 check("input-overflow-shutdown-joins-channel-and-confirms-three-release-rounds",!channel->healthy()&&!IsWindow(retired)&&!IsWindow(child)&&eventCount==finalCount&&exitInputReleaseState().value("verified",false));
 details.push_back({{"edgePort","Private mailbox Input publication; no OS touch generation/physical capture/device output"},{"cancels",probe.cancels},{"finalRelease",exitInputReleaseState()}});
}
#endif
static void presentationRecoveryCases(){
 Config cfg=getConfig();cfg.masterEnabled=true;cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=0;configure(cfg,1,true);
 const auto saved=configJson(getConfig());const auto start=[](unsigned long long generation){ownerOnSuspend();if(!queueOwnerResumeRebind())throw std::runtime_error("fixture-owner-rebind-not-queued");pump();if(!fixtureOwnerResumeReady(generation))throw std::runtime_error("fixture-owner-verifier-not-started");};start(220657);fireWake();
 check("presentation-recovery-first-real-clean-check-before-summon",ownerResumeRecovery.pending&&ownerResumeRecovery.clean==1&&ownerResumeRecovery.repairs==0);
 const auto deadline=ownerResumeRecovery.deadline;const auto checks=ownerResumeRecovery.checks;const auto root=windows[0];const auto originalSource=winrt::get_abi(nativeInputSurfaces[0].source());
 // Actual summon callback uses this transient lease and refresh. The hidden
 // HWND is intentional and must not spend a native fault-repair allowance.
 presentationSuspended.store(true);refresh(true);fireWake();fireWake();
 details.push_back({{"whilePresentationHidden",ownerResumeRecoveryState()},{"deadlineUnchanged",ownerResumeRecovery.deadline==deadline},{"sameRoot",windows[0]==root}});
 check("presentation-recovery-hidden-lease-does-not-probe-repair-or-renew",ownerResumeRecovery.pending&&ownerResumeRecovery.repairs==0&&ownerFaultBudget.repairs==0&&ownerResumeRecovery.checks==checks&&ownerResumeRecovery.clean==0&&ownerResumeRecovery.deadline==deadline&&ownerResumeRecovery.timer&&nativeInputSurfaces[0].root()==root&&originalSource==winrt::get_abi(nativeInputSurfaces[0].source()));
 if(!passed){presentationSuspended.store(false);shutdownForOwner();ownerPowerCapture=nullptr;ownerPowerCurrent=nullptr;return;}
 presentationSuspended.store(false);refresh(true);const auto end=GetTickCount64()+2000;
 while(ownerResumeRecovery.pending&&GetTickCount64()<end){pump();MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
 check("presentation-recovery-restored-lease-requires-two-new-clean-checks",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerResumeRecovery.clean==2&&ownerResumeRecovery.checks==checks+2&&ownerResumeRecovery.repairs==0&&ownerResumeControlsHealthy()&&ownerResumeRecovery.deadline==deadline&&saved==configJson(getConfig()));
 start(220658);presentationSuspended.store(true);refresh(true);const auto expiryTimer=ownerResumeRecovery.timer;ownerResumeRecovery.deadline=GetTickCount64()-1;fireWake();
 check("presentation-recovery-original-deadline-expires-without-repair",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerResumeRecovery.repairs==0&&std::string(ownerResumeRecovery.status)=="expired");
 presentationSuspended.store(false);refresh(true);ownerResumeTimerProc(owner.load(),WM_TIMER,expiryTimer,0);
 check("presentation-recovery-late-expired-timer-cannot-restart-budget",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerFaultBudget.repairs==0);
 start(220659);presentationSuspended.store(true);refresh(true);auto off=cfg;off.masterEnabled=false;configure(off,1,true);fireWake();
 check("presentation-recovery-OFF-wins-over-hidden-lease",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerResumeRecovery.repairs==0&&std::string(ownerResumeRecovery.status)=="disabled"&&!buttonsVisible());
 presentationSuspended.store(false);configure(cfg,1,true);start(220660);presentationSuspended.store(true);refresh(true);const auto timer=ownerResumeRecovery.timer;const auto retired=owner.load();shutdownForOwner();ownerResumeTimerProc(retired,WM_TIMER,timer,0);presentationSuspended.store(false);
 check("presentation-recovery-exit-cancels-hidden-verifier-and-confirms-release",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&exitInputReleaseState().value("verified",false));
 ownerPowerCapture=nullptr;ownerPowerCurrent=nullptr;
}
static void channelDisplayCases(){
 Config cfg=getConfig();cfg.masterEnabled=true;cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=0;configure(cfg,1,true);
 const auto saved=configJson(getConfig());const auto sharedQueue=winrt::get_abi(winrt::Windows::System::DispatcherQueue::GetForCurrentThread());
 fixtureOwnerResumeReady(220653);nativePadInputRuntime.compositor.Close();refresh(true);fireWake();fireWake();
 nativePadInputRuntime.compositor.Close();refresh(true);fireWake();fireWake();fireWake();
 const auto channel=nativePadInputRuntime.inputChannel;const auto budget=ownerFaultBudget.repairs;
 check("channel-display-actual-third-repair-establishes-owned-event-thread",channel&&channel->healthy()&&channel->threadId()!=GetCurrentThreadId()&&ownerFaultBudget.channelResets==1&&budget==3&&!ownerResumeRecovery.pending&&ownerResumeControlsHealthy());
 if(!passed){shutdownForOwner();return;}
 // Supply the existing native sink with owned fixture edges. This does not
 // emulate OS touch generation, physical capture or the independent mailbox.
 const auto down=[](int surface,UINT32 id){nativeInputSink(windows[surface],ymcc::screeninput::Event{surface,id,PT_TOUCH,ymcc::screeninput::Phase::Down,30.f,40.f,false,GetTickCount(),true},nullptr);};
 const auto clear=[](){std::lock_guard<std::mutex> lock(mutex);return !core.pads[0].down&&!core.pads[1].down&&!sentKeys&&!sentClick&&!screenButtonMask.load();};
 const auto wait=[](){const auto deadline=GetTickCount64()+1500;while(ownerDisplayRefresh.pending&&GetTickCount64()<deadline){pump();MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}};
 down(0,901);down(1,902);bool held=false;{std::lock_guard<std::mutex> lock(mutex);held=core.pads[0].down&&core.pads[1].down;}
 check("channel-display-fixture-native-sink-establishes-two-owned-contacts",held);
 for(int i=0;i<1024;++i)SendMessageW(owner.load(),i%2?WM_DPICHANGED:WM_DISPLAYCHANGE,32,0);
 check("channel-display-first-storm-withdraws-both-contacts-and-windows",clear()&&!IsWindowVisible(windows[0])&&!IsWindowVisible(windows[1])&&!buttonsVisible());
 wait();check("channel-display-real-timer-restores-same-independent-channel",!ownerDisplayRefresh.pending&&channel==nativePadInputRuntime.inputChannel&&channel->healthy()&&ownerResumeControlsHealthy()&&nativeInputSurfaces[0].usesIndependentInput()&&nativeButtonSurfaces[1].usesIndependentInput());
 bool withdrawn=true,restored=true;const auto completed=ownerDisplayRefresh.completed;
 for(int round=0;round<8;++round){
  down(0,910+round);for(int i=0;i<128;++i)SendMessageW(owner.load(),i%2?WM_DPICHANGED:WM_DISPLAYCHANGE,32,0);
  // Same transient lease and refresh calls used by the actual YMCC summon
  // callback. No game, WebView, monitor or public power policy is controlled.
  presentationSuspended.store(true);refresh(true);wait();
  withdrawn=withdrawn&&clear()&&!IsWindowVisible(windows[0])&&!buttonsVisible()&&!ownerDisplayRefresh.pending;
  presentationSuspended.store(false);refresh(true);
  restored=restored&&ownerResumeControlsHealthy()&&channel==nativePadInputRuntime.inputChannel&&channel->healthy()&&nativeInputSurfaces[0].ensureMousePass()&&nativeButtonSurfaces[1].ensureMousePass();
 }
 check("channel-display-eight-summon-lease-storms-release-before-restoration",withdrawn);
 check("channel-display-eight-summon-lease-storms-preserve-input-and-mouse-pass",restored);
 check("channel-display-no-fourth-repair-or-shared-UI-queue-reset",ownerFaultBudget.repairs==budget&&ownerFaultBudget.channelResets==1&&sharedQueue==winrt::get_abi(winrt::Windows::System::DispatcherQueue::GetForCurrentThread())&&saved==configJson(getConfig()));
 cfg.autoHideSeconds=15;configure(cfg,1,true);autoHideStopTimer();autoHide.lastTouch=GetTickCount64()-16000;autoHideSync();const auto idleTouch=autoHide.lastTouch;
 SendMessageW(owner.load(),WM_DISPLAYCHANGE,32,0);wait();
 check("channel-display-hidden-idle-deadline-survives-independent-cancel",autoHide.hidden&&autoHide.lastTouch==idleTouch&&!autoHide.timer&&ownerResumeControlsHealthy());
 const auto before=eventCount;nativeInputSurfaces[0].drainIndependent(0);check("channel-display-old-input-cookie-cannot-revive-contact",before==eventCount&&clear());
 SendMessageW(owner.load(),WM_DISPLAYCHANGE,32,0);const auto timer=ownerDisplayRefresh.timer;const auto retired=owner.load();
 details.push_back({{"displayRecovery",ownerDisplayRefreshState()},{"nativeRecovery",ownerResumeRecoveryState()},{"nativeChannelThread",channel->threadId()},{"completedBeforeSummonStorms",completed},{"edgePort","nativeInputSink; OS touch generation and mailbox not emulated"}});
 shutdownForOwner();ownerDisplayRefreshTimerProc(retired,WM_TIMER,timer,0);
 check("channel-display-exit-joins-event-thread-and-rejects-late-timer",!nativePadInputRuntime.inputChannel&&!channel->healthy()&&!ownerDisplayRefresh.pending&&!ownerDisplayRefresh.timer&&exitInputReleaseState().value("verified",false));
 ownerPowerCapture=nullptr;ownerPowerCurrent=nullptr;
}
static void displayBoundaryCases(){
 Config cfg=getConfig();cfg.masterEnabled=true;cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=15;configure(cfg,1,true);
 const auto wait=[&]{const auto end=GetTickCount64()+1000;while(ownerDisplayRefresh.pending&&GetTickCount64()<end){pump();MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}};
 autoHideStopTimer();autoHide.lastTouch=GetTickCount64()-16000;autoHideSync();const auto hiddenTouch=autoHide.lastTouch;const bool initiallyHidden=autoHide.hidden;
 SendMessageW(owner.load(),WM_DISPLAYCHANGE,32,0);const bool preserved=autoHide.hidden&&autoHide.lastTouch==hiddenTouch;wait();
 check("display-boundary-hidden-idle-deadline-survives-release-and-reflow",initiallyHidden&&preserved&&autoHide.hidden&&autoHide.lastTouch==hiddenTouch&&!autoHide.timer&&!IsWindowVisible(outlines[0]));
 autoHideSetHidden(false);autoHideStopTimer();autoHide.lastTouch=GetTickCount64()-14000;autoHideSync();const auto nearTouch=autoHide.lastTouch;
 SendMessageW(owner.load(),WM_DISPLAYCHANGE,32,0);wait();
 check("display-boundary-visible-idle-deadline-not-restarted",!autoHide.hidden&&autoHide.lastTouch==nearTouch&&autoHide.timer);
 cfg.autoHideSeconds=0;configure(cfg,1,true);
 const auto dispatcher=nativeInputSurfaces[0].source().Dispatcher();auto callback=std::make_shared<bool>(false);
 auto action=dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[callback]{*callback=true;});
 SendMessageW(owner.load(),WM_DISPLAYCHANGE,32,0);const auto timer=ownerDisplayRefresh.timer;const auto completed=ownerDisplayRefresh.completed;
 cfg.scale+=1;configure(cfg,1,true);refresh(true);SendMessageW(owner.load(),WM_DPICHANGED,32,0);
 check("display-boundary-configure-and-direct-refresh-cannot-bypass-fence",!*callback&&ownerDisplayRefresh.pending&&ownerDisplayRefresh.timer==timer&&!IsWindowVisible(windows[0])&&!IsWindowVisible(outlines[0])&&!buttonsVisible());
 wait();check("display-boundary-configuration-joins-single-reserved-refresh",*callback&&!ownerDisplayRefresh.pending&&ownerDisplayRefresh.completed==completed+1&&ownerResumeControlsHealthy());
 SendMessageW(owner.load(),WM_DISPLAYCHANGE,32,0);auto off=cfg;off.masterEnabled=false;configure(off,1,true);
 check("display-boundary-OFF-immediately-releases-signals-while-pending",!sentKeys&&!sentClick&&!screenButtonMask.load()&&!IsWindowVisible(windows[0]));wait();
 check("display-boundary-OFF-is-not-revived-by-deferred-refresh",!IsWindowVisible(windows[0])&&!buttonsVisible());configure(cfg,1,true);
}
static void displayTransitionCases(){
 Config cfg=getConfig();cfg.masterEnabled=true;cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=0;configure(cfg,1,true);const auto saved=configJson(cfg);
 check("display-transition-initial-native-controls-healthy",ownerResumeControlsHealthy());
 const auto dispatcher=nativeInputSurfaces[0].source().Dispatcher();const auto original=winrt::get_abi(nativeInputSurfaces[0].source());auto callback=std::make_shared<bool>(false);
 auto work=dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[callback]{*callback=true;});
 SendMessageW(owner.load(),WM_DISPLAYCHANGE,32,MAKELPARAM(1280,800));
 check("display-transition-no-input-dispatch-inside-display-message",!*callback&&ownerDisplayRefresh.pending&&ownerDisplayRefresh.timer);
 if(!passed){dispatcher.ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);shutdownForOwner();return;}
 check("display-transition-withdraws-owned-input-and-artwork-during-settlement",!IsWindowVisible(windows[0])&&!IsWindowVisible(outlines[0])&&!buttonsVisible()&&!sentKeys&&!sentClick&&!screenButtonMask.load());
 const auto firstTimer=ownerDisplayRefresh.timer,firstSerial=ownerDisplayRefresh.serial;const auto completed=ownerDisplayRefresh.completed;
 for(int i=0;i<1024;++i)SendMessageW(owner.load(),i%3==0?WM_DISPLAYCHANGE:i%3==1?WM_DPICHANGED:WM_SETTINGCHANGE,i%3==2?SPI_SETWORKAREA:32,0);
 check("display-transition-1024-events-merge-without-extending-first-timer",ownerDisplayRefresh.timer==firstTimer&&ownerDisplayRefresh.serial==firstSerial&&ownerDisplayRefresh.completed==completed);
 const auto wait=[&]{const auto end=GetTickCount64()+1000;while(ownerDisplayRefresh.pending&&GetTickCount64()<end){pump();MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}};wait();
 check("display-transition-real-Windows-timer-refreshes-once-after-handler",*callback&&!ownerDisplayRefresh.pending&&!ownerDisplayRefresh.timer&&ownerDisplayRefresh.completed==completed+1&&ownerResumeControlsHealthy());
 check("display-transition-preserves-real-input-source-and-mouse-pass",original==winrt::get_abi(nativeInputSurfaces[0].source())&&nativeInputSurfaces[0].ensureMousePass()&&nativeButtonSurfaces[1].ensureMousePass());
 bool storms=true;for(int round=0;round<8;++round){const auto before=ownerDisplayRefresh.completed;for(int i=0;i<128;++i)SendMessageW(owner.load(),i%2?WM_DPICHANGED:WM_DISPLAYCHANGE,32,MAKELPARAM(1280,round%2?700:800));wait();storms=storms&&!ownerDisplayRefresh.pending&&ownerDisplayRefresh.completed==before+1&&ownerResumeControlsHealthy();}
 check("display-transition-eight-new-fullscreen-message-storms-recover",storms);
 // A new display event can arrive while old refresh drains the real input
 // dispatcher. Its epoch withdraws the old pass instead of exposing it again.
 auto nested=std::make_shared<bool>(false);auto action=dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[nested]{*nested=true;SendMessageW(owner.load(),WM_DISPLAYCHANGE,32,MAKELPARAM(1920,1080));});
 SendMessageW(owner.load(),WM_DISPLAYCHANGE,32,0);auto timer=ownerDisplayRefresh.timer;ownerDisplayRefreshTimerProc(owner.load(),WM_TIMER,timer,0);
 check("display-transition-nested-display-invalidates-old-refresh",*nested&&ownerDisplayRefresh.pending&&!IsWindowVisible(windows[0]));wait();
 check("display-transition-newer-pass-completes-after-nested-event",ownerResumeControlsHealthy()&&!ownerDisplayRefresh.pending);
 auto off=cfg;SendMessageW(owner.load(),WM_DISPLAYCHANGE,32,0);timer=ownerDisplayRefresh.timer;off.masterEnabled=false;configure(off,1,true);ownerDisplayRefreshTimerProc(owner.load(),WM_TIMER,timer,0);
 check("display-transition-OFF-before-timer-stays-hidden",!buttonsVisible()&&!IsWindowVisible(windows[0])&&!ownerDisplayRefresh.pending);configure(cfg,1,true);
 SendMessageW(owner.load(),WM_DISPLAYCHANGE,32,0);timer=ownerDisplayRefresh.timer;ownerOnSuspend();ownerDisplayRefreshTimerProc(owner.load(),WM_TIMER,timer,0);
 check("display-transition-suspend-revokes-old-timer",!ownerDisplayRefresh.pending&&!ownerDisplayRefresh.timer);refresh(true);
 check("display-transition-settings-and-cursor-policy-retained",saved==configJson(getConfig())&&state().value("cursorPolicyVersion",0)==19);
 SendMessageW(owner.load(),WM_DISPLAYCHANGE,32,0);timer=ownerDisplayRefresh.timer;const auto retired=owner.load();shutdownForOwner();ownerDisplayRefreshTimerProc(retired,WM_TIMER,timer,0);
 check("display-transition-exit-revokes-timer-and-confirms-release",!ownerDisplayRefresh.pending&&!ownerDisplayRefresh.timer&&exitInputReleaseState().value("verified",false));details.push_back(ownerDisplayRefreshState());
}
static void channelStageCompletionCases(){
#ifdef YMCC_CHANNEL_STAGE_COMPLETION_FIXTURE
 using namespace ymcc::screeninput;WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YMCC_ChannelCompletionPrivateRoot";RegisterClassW(&wc);
 HWND root=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,wc.lpszClassName,L"",WS_POPUP,0,0,120,100,nullptr,nullptr,wc.hInstance,nullptr);
 check("completion-owned-private-root-exists",root!=nullptr);if(!passed)return;
 Runtime runtime;runtime.isolatedInput=true;Surface surface;
 const bool healthy=surface.attach(runtime,root,2,nullptr,nullptr,false);check("completion-current-original-healthy-bind-succeeds",healthy&&surface.usesIndependentInput());surface.close();
 stageFixtureFailRect.store(true);const bool failed=surface.attach(runtime,root,2,nullptr,nullptr,false);const auto actualError=surface.error;
 check("completion-actual-client-rect-error-retains-original-HRESULT",!failed&&actualError==HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE));
 check("completion-completed-command-retains-current-inner-stage",std::string(surface.lastFailureStage())=="attach-owned-client-rect");
 recordNativeInputFailure(true,2,surface);auto first=nativeInputFailureState();
 check("completion-existing-native-failure-carrier-has-current-stage",first["stage"]=="attach-owned-client-rect"&&first["error"]==(DWORD)actualError&&first["index"]==2);
 check("completion-first-surface-and-channel-retire",surface.terminalReleased()&&runtime.retireInputChannel());
 runtime.inputChannel=std::make_shared<InputChannel>();HRESULT error=S_OK;const bool started=runtime.inputChannel->start(error);check("completion-timeout-current-original-worker-starts",started&&runtime.inputChannel->healthy());if(!passed){runtime.retireInputChannel();DestroyWindow(root);return;}
 HANDLE finalWorker=OpenThread(SYNCHRONIZE,FALSE,runtime.inputChannel->threadId());
 stageFixtureFailRect.store(true);stageFixtureDelayMs.store(900);const bool timed=surface.attach(runtime,root,2,nullptr,nullptr,false);const auto timeoutError=surface.error;const std::string timeoutStage=surface.lastFailureStage();
 check("completion-original750ms-return-is-still-timeout",!timed&&timeoutError==HRESULT_FROM_WIN32(ERROR_TIMEOUT));
 check("completion-timeout-does-not-label-unconfirmed-inner-API-as-cause",timeoutStage=="attach-owned-command");
 recordNativeInputFailure(true,2,surface);auto second=nativeInputFailureState();
 check("completion-current-carrier-keeps-timeout-and-command-scope",second["error"]==(DWORD)timeoutError&&second["stage"]=="attach-owned-command");
 const DWORD ended=finalWorker?WaitForSingleObject(finalWorker,3000):WAIT_FAILED;if(finalWorker)CloseHandle(finalWorker);
 // Complete the original Surface acknowledgement while the retired channel
 // still retains its State, then release the original channel thread handle.
 surface.close();const bool retired=runtime.retireInputChannel();check("completion-original-late-worker-and-surface-cleanup",ended==WAIT_OBJECT_0&&retired&&surface.terminalReleased());
 details.push_back({{"completeFailure",first},{"timeoutFailure",second},{"originalWaitMs",750},{"privateDoneDelayMs",900},{"actualInnerLeaf","GetClientRect(nullptr)"}});
 check("completion-fixture-owned-root-destroyed",DestroyWindow(root)!=FALSE);
#else
 throw std::runtime_error("channel-completion-fixture-port-not-compiled");
#endif
}

static void worker(HDESK desktop){
 HWND host=nullptr;bool apartment=false;std::vector<HWND> originals;
 try{
  if(!SetThreadDesktop(desktop))throw std::runtime_error("private desktop binding failed");
  winrt::init_apartment(winrt::apartment_type::single_threaded);apartment=true;
  if(channelStageOnly){channelStageCompletionCases();}else{
  host=createOwner();if(!host)throw std::runtime_error("private owner unavailable");
  dryRun=true;setNativeInputCandidate(true);setContext("steamdeck",true);
  Config cfg;cfg.enabled=false;cfg.specialMask=3;cfg.specialEnabled=true;cfg.summonPosition=SummonPosition::Off;cfg.summonEnabled=false;cfg.touchFeedback=TouchFeedbackMode::Off;cfg.autoHideSeconds=0;
  initializeForOwner(GetModuleHandleW(nullptr),host,cfg,1,L"owned.exe");pump();
  if(!wakeOnly){
  check("initial-two-native-special-keys-visible",buttonVisibilityState(cfg)["artworkVisibleMask"]==6&&nativeButtonSurfaces[1].active()&&nativeButtonSurfaces[2].active()&&!windowError);
  for(int round=0;round<3;++round){
   int index=round%2+1,other=index==1?2:1;bool called=false,inSync=false;bool callingSync=false;
   const auto oldRetries=buttonInputRecovery.retries,oldRecovered=buttonInputRecovery.recovered;
   auto dispatcher=nativeButtonSurfaces[index].source().Dispatcher();
   auto action=dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{called=true;inSync=callingSync;nativeButtonSurfaces[index].close();});
   const bool queued=!called;callingSync=true;const bool restored=buttonNativeInputSync(index,true);callingSync=false;
   check("round-"+std::to_string(round)+"-callback-ran-inside-real-source-sync",queued&&called&&inSync);
   check("round-"+std::to_string(round)+"-aborted-sync-rebound-once",restored&&nativeButtonSurfaces[index].active()&&buttonInputRecovery.retries==oldRetries+1&&buttonInputRecovery.recovered==oldRecovered+1);
   check("round-"+std::to_string(round)+"-both-input-and-artwork-still-visible",buttonVisibilityState(cfg)["inputVisibleMask"]==6&&buttonVisibilityState(cfg)["artworkVisibleMask"]==6&&nativeButtonSurfaces[other].active());
   check("round-"+std::to_string(round)+"-mouse-pass-policy-still-active",nativeButtonSurfaces[index].ensureMousePass()&&nativeButtonSurfaces[other].ensureMousePass());
   details.push_back(buttonVisibilityState(cfg));
  }
  ShowWindow(buttonWindows[1],SW_HIDE);ShowWindow(buttonOutlines[1],SW_HIDE);nativeButtonSurfaces[1].close();
  check("missing-input-detected-with-saved-selection-intact",buttonsNeedRefresh(cfg)&&effectiveSpecialMask(getConfig())==3);
  const bool refreshed=configure(cfg,1,true);
  check("unchanged-settings-repair-native-window-and-artwork",refreshed&&!windowError&&nativeButtonSurfaces[1].active()&&buttonVisibilityState(cfg)["artworkVisibleMask"]==6);
  const auto uploads=nativeButtonSurfaces[1].uploads,retries=buttonInputRecovery.retries;
  check("healthy-settings-avoid-extra-source-refresh",!configure(cfg,1,true)&&nativeButtonSurfaces[1].uploads==uploads&&buttonInputRecovery.retries==retries);
  }
  if(inputOverflowOnly||workerRetirementOnly||manualChannelRecoveryOnly){
#ifdef YMCC_SCREEN_TOUCHPAD_OVERFLOW_FIXTURE
   if(manualChannelRecoveryOnly)manualChannelRecoveryCases();else if(workerRetirementOnly)workerRetirementCases();else inputOverflowCases();
#else
   throw std::runtime_error("input-overflow-fixture-port-not-compiled");
#endif
  }else if(presentationRecoveryOnly)presentationRecoveryCases();else if(channelDisplayOnly)channelDisplayCases();else if(displayBoundaryOnly)displayBoundaryCases();else if(displayOnly){displayBoundaryCases();displayTransitionCases();}else if(commandOnly)channelCommandCases();else if(stopOnly)channelStopCases();else if(notifyOnly)mailboxQuotaCases();else if(channelOnly)independentResetCases();else if(cumulativeOnly)cumulativeFaultCases();else if(lateFaultOnly)lateFaultCases();else if(reentryOnly)reentryProbeCases();else if(persistentOnly)persistentRuntimeCases();else wakeCases();
#ifdef YMCC_CUMULATIVE_OLD_CONTROL
  if(cumulativeOnly&&!passed)throw std::runtime_error("old-control-necessary-missed-cumulative-rebuild");
#endif
#ifdef YMCC_LATE_FAULT_OLD_CONTROL
  if(lateFaultOnly&&!passed)throw std::runtime_error("old-control-necessary-late-fault-refusal");
#endif
  // The negative intentionally leaves the compositor unusable. Retire that
  // fixture fault before setting up the independent terminal controls below.
  if(persistentOnly){shutdownForOwner();nativePadInputRuntime.compositor=nullptr;}
  if(!channelOnly&&!notifyOnly&&!stopOnly&&!commandOnly&&!displayOnly&&!displayBoundaryOnly&&!channelDisplayOnly&&!presentationRecoveryOnly&&!inputOverflowOnly&&!workerRetirementOnly&&!manualChannelRecoveryOnly){
  // wakeCases intentionally exits once; start the original terminal controls
  // afresh with the exact pre-existing two-key configuration.
  shutdownForOwner();setContext("steamdeck",true);initializeForOwner(GetModuleHandleW(nullptr),host,cfg,1,L"owned.exe");pump();
  for(HWND h:buttonWindows)if(h)originals.push_back(h);for(HWND h:buttonOutlines)if(h)originals.push_back(h);
  for(auto& surface:nativeButtonSurfaces)if(surface.child())originals.push_back(surface.child());
  bool terminalCalled=false;auto dispatcher=nativeButtonSurfaces[1].source().Dispatcher();
  const auto retries=buttonInputRecovery.retries;
  auto terminal=dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{terminalCalled=true;quiesceForOwner();});
  refresh();
  check("exit-during-dispatch-is-not-retried-or-reshown",terminalCalled&&inputTeardown&&!initialized&&buttonInputRecovery.retries==retries&&!buttonsVisible());
  check("late-settings-cannot-revive-terminal-owner",!configure(cfg,1,true)&&!buttonsVisible());
  shutdownForOwner();
  bool gone=true;for(HWND h:originals)gone=gone&&!IsWindow(h);
  check("all-original-owned-windows-destroyed",gone);
  check("terminal-release-confirmed-three-times",exitInputReleaseState().value("verified",false));
  }
  }
 }catch(const winrt::hresult_error& e){passed=false;workerError=(DWORD)e.code();details.push_back({{"winrtError",(long)e.code()}});shutdownForOwner();}
 catch(const std::exception& e){passed=false;details.push_back({{"exception",e.what()}});shutdownForOwner();}
 try{
  nativeInputRuntime.compositor=nullptr;nativePadInputRuntime.compositor=nullptr;
  if(nativePadInputRuntime.queue){auto action=nativePadInputRuntime.queue.ShutdownQueueAsync();const auto deadline=GetTickCount64()+4000;
   while(action.Status()==winrt::Windows::Foundation::AsyncStatus::Started&&GetTickCount64()<deadline){pump();MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
   queueClosed=action.Status()==winrt::Windows::Foundation::AsyncStatus::Completed;if(queueClosed)action.GetResults();nativePadInputRuntime.queue=nullptr;
  }else queueClosed=true;
  runtimeClosed=!nativePadInputRuntime.queue&&!nativePadInputRuntime.compositor&&!nativeInputRuntime.queue&&!nativeInputRuntime.compositor;
 }catch(...){passed=false;}
 if(host)DestroyWindow(host);if(apartment)winrt::uninit_apartment();workerEnded=true;SetEvent(doneEvent);
}
int wmain(int argc,wchar_t** argv){
 if(argc<2||argc>3)return 2;if(argc==3){const std::wstring option=argv[2];if(option!=L"--wake-only"&&option!=L"--persistent-only"&&option!=L"--reentrant-only"&&option!=L"--late-fault-only"&&option!=L"--cumulative-only"&&option!=L"--independent-reset-only"&&option!=L"--channel-notify-only"&&option!=L"--channel-stop-only"&&option!=L"--channel-command-only"&&option!=L"--display-transition-only"&&option!=L"--display-boundary-only"&&option!=L"--channel-display-only"&&option!=L"--presentation-recovery-only"&&option!=L"--input-overflow-only"&&option!=L"--worker-retirement-only"&&option!=L"--worker-retirement-quota-only"&&option!=L"--manual-channel-recovery-only"&&option!=L"--channel-stage-only")return 2;wakeOnly=true;persistentOnly=option==L"--persistent-only";reentryOnly=option==L"--reentrant-only";lateFaultOnly=option==L"--late-fault-only";cumulativeOnly=option==L"--cumulative-only";channelOnly=option==L"--independent-reset-only";notifyOnly=option==L"--channel-notify-only";stopOnly=option==L"--channel-stop-only";commandOnly=option==L"--channel-command-only";displayOnly=option==L"--display-transition-only";displayBoundaryOnly=option==L"--display-boundary-only";channelDisplayOnly=option==L"--channel-display-only";presentationRecoveryOnly=option==L"--presentation-recovery-only";inputOverflowOnly=option==L"--input-overflow-only";workerRetirementOnly=option==L"--worker-retirement-only"||option==L"--worker-retirement-quota-only";workerRetirementQuota=option==L"--worker-retirement-quota-only";manualChannelRecoveryOnly=option==L"--manual-channel-recovery-only";channelStageOnly=option==L"--channel-stage-only";}
#ifndef YMCC_CHANNEL_STAGE_COMPLETION_FIXTURE
 if(channelStageOnly)return 2;
#endif
 const auto inputBefore=inputName();const auto name=L"YMCC_ButtonAbort_"+std::to_wstring(GetCurrentProcessId());
 HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop)return 3;
 doneEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::thread task(worker,desktop);
 if(WaitForSingleObject(doneEvent,20000)!=WAIT_OBJECT_0){Json fail={{"ok",false},{"error","worker-timeout"},{"cleanupProven",false}};std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<fail.dump(2);file.close();ExitProcess(4);}
 task.join();CloseHandle(doneEvent);const bool closed=CloseDesktop(desktop)!=FALSE;const bool inputUnchanged=inputBefore==inputName();
 check("private-desktop-closed",closed);check("input-desktop-unchanged",inputUnchanged);check("runtime-and-dispatcher-queue-closed",runtimeClosed&&queueClosed&&workerEnded);
 Json result={{"ok",passed},{"cases",cases},{"details",details},{"checks",cases.size()},{"workerError",workerError},{"realWinRT",true},{"hiddenDesktop",true},{"inputDesktopUnchanged",inputUnchanged},{"runtimeClosed",runtimeClosed},{"queueClosed",queueClosed},{"workerEnded",workerEnded},{"privateDesktopClosed",closed},{"systemInputInjected",false},{"productStarted",false},{"deviceWrites",0}};
 std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<result.dump(2);std::cout<<"checks="<<cases.size()<<" ok="<<passed<<"\n";return passed?0:1;
}
