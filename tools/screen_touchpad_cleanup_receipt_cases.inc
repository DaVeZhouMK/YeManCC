// Original manual-channel selector, enabled only by its explicit cleanup
// recipe. Exceptions are controlled diagnostic ports, not a Win5 field trace.
namespace ymcc::screeninput {
struct ChannelCleanupFixtureAccess {
 static bool acknowledgeJoinedEmptyStateForFixture(InputChannel& channel) noexcept {
  const auto state=channel.state_;
  if(!state||!channel.thread_||WaitForSingleObject(channel.thread_,0)!=WAIT_OBJECT_0||state->closed.load()||
     state->window.load()||state->queue||state->queueShutdown||state->seed||state->dispatcher||state->compositor||state->queueCompletion)return false;
  for(const auto& input:state->inputs)if(input)return false;
  // The product's unconfirmed-cleanup refusal has already been asserted.
  // Only this owned fault fixture acknowledges its known injected exception
  // after proving original COM/HWND cleanup and actual thread termination.
  state->closed.store(true,std::memory_order_release);return true;
 }
};
}
static void manualChannelRecoveryCases(){
 using namespace ymcc::screeninput;
 Config cfg=getConfig();cfg.masterEnabled=true;cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=0;configure(cfg,1,true);
 release();for(int side=0;side<2;++side)nativeInputClose(side);buttonsNativeInputDisable();
 const bool isolated=nativePadInputRuntime.useFreshInputChannel();refresh(true);const auto channel=nativePadInputRuntime.inputChannel;
 check("cleanup-receipt-real-independent-controls-ready",isolated&&channel&&channel->healthy()&&ownerResumeControlsHealthy());
 if(!passed){shutdownForOwner();return;}
 const auto clean=channel->cleanupFailure();const auto healthyState=ymcc::screenpads::state();
 check("cleanup-receipt-healthy-path-has-no-extra-failure-object",SUCCEEDED(clean.error)&&!healthyState.contains("nativeInputChannelFailure"));
 const auto shared=winrt::get_abi(nativeInputRuntime.compositor);const auto tid=channel->threadId();
 HWND workerWindow=nullptr;for(HWND h=FindWindowExW(HWND_MESSAGE,nullptr,L"YeManTouchpadInputApartment",nullptr);h;h=FindWindowExW(HWND_MESSAGE,h,L"YeManTouchpadInputApartment",nullptr)){DWORD pid=0;if(GetWindowThreadProcessId(h,&pid)==tid&&pid==GetCurrentProcessId()){workerWindow=h;break;}}
 HANDLE thread=OpenThread(SYNCHRONIZE,FALSE,tid);check("cleanup-receipt-exact-owned-thread-and-window",thread&&workerWindow);
 if(!thread||!workerWindow){if(thread)CloseHandle(thread);shutdownForOwner();return;}
 cleanupFixtureFailures=2;cleanupFixtureControllerCalls=0;
 const bool sent=PostMessageW(workerWindow,WM_APP+523,0,0)!=FALSE;const DWORD waited=WaitForSingleObject(thread,3000);CloseHandle(thread);
 check("cleanup-receipt-original-worker-stopped-naturally",sent&&waited==WAIT_OBJECT_0&&!IsWindow(workerWindow));pump();
 const auto failure=channel->cleanupFailure();
 check("cleanup-receipt-first-native-error-is-not-lost",failure.error==E_ACCESSDENIED&&std::string(failure.stage)=="input-controller-close"&&failure.surface==0);
 check("cleanup-receipt-second-error-keeps-first-receipt",cleanupFixtureFailures.load()==0&&cleanupFixtureControllerCalls.load()==3&&failure.error==E_ACCESSDENIED);
 check("cleanup-receipt-original-error-and-strict-retirement-unchanged",channel->lastError()==S_OK&&failure.stopping&&!failure.complete&&!channel->closedForSettings()&&!channel->shutdown()&&channel->threadId()==tid);
 const auto observed=ymcc::screenpads::state();bool visible=false;
 if(observed.contains("nativeInputChannelFailure")){
  const auto& receipt=observed["nativeInputChannelFailure"];if(receipt.contains("cleanup")){const auto& cleanup=receipt["cleanup"];visible=receipt.value("hresult",0UL)==(unsigned long)E_ACCESSDENIED&&cleanup.value("hresult",0UL)==(unsigned long)E_ACCESSDENIED&&cleanup.value("stage",std::string{})=="input-controller-close"&&cleanup.value("surface",-1)==0&&cleanup.value("stopping",false)&&!cleanup.value("complete",true);}
 }
 check("cleanup-receipt-existing-state-delivers-exact-failed-cleanup",visible);
 const bool acknowledged=ChannelCleanupFixtureAccess::acknowledgeJoinedEmptyStateForFixture(*channel);
 check("cleanup-receipt-diagnostic-cleanup-proves-empty-original-STA",acknowledged);
 details.push_back({{"cleanupReceipt",observed.value("nativeInputChannelFailure",Json{})},{"diagnosticCleanupPortUsed",true},{"originalRefusalPreserved",!failure.complete},{"scope","Two controlled original Close failures and final successful Close; verifies diagnostic delivery and unchanged strict refusal, not real failed-cleanup recovery"}});
 shutdownForOwner();
 check("cleanup-receipt-final-owned-handles-and-three-confirmations",acknowledged&&!nativePadInputRuntime.inputChannel&&exitInputReleaseState().value("verified",false));
 check("cleanup-receipt-unrelated-shared-compositor-kept",shared==winrt::get_abi(nativeInputRuntime.compositor));
 ownerPowerCapture=nullptr;ownerPowerCurrent=nullptr;
}
