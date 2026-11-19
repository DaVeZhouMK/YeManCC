// Explicit query-native-cancellation group only. Include inside offline_self_test.
// This exercises the product wait helpers with owned Win32 events/threads.
// No QueryActor construction, Steam SDK invocation, target process, or device I/O.
{
  const auto qncEvent=[](bool manual=true,bool signaled=false){
    H value(CreateEventW(nullptr,manual?TRUE:FALSE,signaled?TRUE:FALSE,nullptr));
    need(value.good(),"query-native-cancel-fixture-create-event");return value;
  };
  const auto qncReason=[](const auto& action,const char* expected){
    bool refused=false;
    try{action();}
    catch(const std::runtime_error& error){
      need(std::string(error.what())==expected,"query-native-cancel-fixture-wrong-reason");refused=true;
    }
    need(refused,"query-native-cancel-fixture-expected-refusal");
  };
  // Declared after the events and result storage at every use. Both normal
  // and exceptional exits join before any borrowed event/result is destroyed.
  // Failure cleanup signals the actual events, including the completion or
  // command escape event. It never detaches or claims that SDK join is bounded.
  struct QncOwnedWorker {
    std::array<HANDLE,3> release;
    std::thread thread;
    QncOwnedWorker(HANDLE first,HANDLE second,HANDLE third):release{{first,second,third}}{}
    ~QncOwnedWorker(){
      if(thread.joinable()){
        for(const auto event:release)if(event)SetEvent(event);
        thread.join();
      }
    }
    void join(){if(thread.joinable())thread.join();}
  };
  const auto qncLateReturn=[&](bool revokeIdentity){
    auto power=qncEvent();auto identity=qncEvent();auto completion=qncEvent();
    auto entered=qncEvent();auto returned=qncEvent();
    std::atomic<bool> completed{false};std::exception_ptr failure;
    ULONGLONG waitElapsedMs=0;
    QncOwnedWorker worker(power.h,identity.h,completion.h);
    worker.thread=std::thread([&]{
      try{
        need(SetEvent(entered.h)!=FALSE,"query-native-cancel-fixture-entered-signal");
        const auto start=GetTickCount64();
        try{waitQueryReturn(completed,completion.h,5000,power.h,identity.h);}
        catch(...){failure=std::current_exception();}
        waitElapsedMs=GetTickCount64()-start;
      }catch(...){failure=std::current_exception();}
      SetEvent(returned.h);
    });
    // This is a call-boundary handshake, NOT proof of kernel wait entry.
    need(WaitForSingleObject(entered.h,5000)==WAIT_OBJECT_0,"query-native-cancel-fixture-entered-timeout");
    need(WaitForSingleObject(returned.h,0)==WAIT_TIMEOUT,"query-native-cancel-fixture-premature-return");
    // A separate delay makes cancellation late relative to that handshake;
    // scheduling may still deliver it before the helper actually blocks.
    Sleep(40);
    need(WaitForSingleObject(returned.h,0)==WAIT_TIMEOUT,"query-native-cancel-fixture-return-before-revocation");
    need(SetEvent(revokeIdentity?identity.h:power.h)!=FALSE,"query-native-cancel-fixture-revoke-signal");
    need(WaitForSingleObject(returned.h,5000)==WAIT_OBJECT_0,"query-native-cancel-fixture-cancel-did-not-wake");
    worker.join();
    qncReason([&]{if(failure)std::rethrow_exception(failure);},"normal-query-native-cancelled");
    need(waitElapsedMs<5000,"query-native-cancel-fixture-did-not-beat-original-return-budget");
    need(!completed.load()&&WaitForSingleObject(completion.h,0)==WAIT_TIMEOUT,
        "query-native-cancel-fixture-cancellation-forged-completion");
    need(queryCancellationState(power.h,identity.h)==QueryCancellationState::Revoked,
        "query-native-cancel-fixture-revocation-was-consumed");
  };
  const auto qncIdleCommand=[&](bool revokeIdentity){
    auto power=qncEvent();auto identity=qncEvent();auto command=qncEvent(false);
    auto entered=qncEvent();auto returned=qncEvent();std::exception_ptr failure;
    DWORD waited=WAIT_FAILED;
    QncOwnedWorker worker(power.h,identity.h,command.h);
    worker.thread=std::thread([&]{
      try{
        need(SetEvent(entered.h)!=FALSE,"query-native-cancel-fixture-idle-entered-signal");
        // Same helper and INFINITE idle-command budget as the actual actor.
        waited=waitQuerySignal(command.h,INFINITE,power.h,identity.h);
      }catch(...){failure=std::current_exception();}
      SetEvent(returned.h);
    });
    need(WaitForSingleObject(entered.h,5000)==WAIT_OBJECT_0,"query-native-cancel-fixture-idle-entered-timeout");
    Sleep(40);
    need(WaitForSingleObject(returned.h,0)==WAIT_TIMEOUT,"query-native-cancel-fixture-idle-returned-without-input");
    need(SetEvent(revokeIdentity?identity.h:power.h)!=FALSE,"query-native-cancel-fixture-idle-revoke-signal");
    need(WaitForSingleObject(returned.h,5000)==WAIT_OBJECT_0,"query-native-cancel-fixture-idle-not-woken");
    worker.join();
    qncReason([&]{if(failure)std::rethrow_exception(failure);},"normal-query-native-cancelled");
    need(waited==WAIT_FAILED&&WaitForSingleObject(command.h,0)==WAIT_TIMEOUT,
        "query-native-cancel-fixture-idle-granted-command");
  };

  test("query-native-cancel-state-legacy-current",[&]{
    need(queryCancellationState(nullptr,nullptr)==QueryCancellationState::Legacy,
        "query-native-cancel-fixture-legacy-state");
    requireQueryCancellation(nullptr,nullptr);
    auto power=qncEvent();auto identity=qncEvent();
    need(queryCancellationState(power.h,identity.h)==QueryCancellationState::Current,
        "query-native-cancel-fixture-current-state");
    requireQueryCancellation(power.h,identity.h);
  });
  test("query-native-cancel-pre-signaled-power",[&]{
    auto power=qncEvent(true,true);auto identity=qncEvent();auto completion=qncEvent();
    std::atomic<bool> completed{false};
    need(queryCancellationState(power.h,identity.h)==QueryCancellationState::Revoked,
        "query-native-cancel-fixture-power-not-revoked");
    qncReason([&]{requireQueryCancellation(power.h,identity.h);},"normal-query-native-cancelled");
    qncReason([&]{waitQueryReturn(completed,completion.h,5000,power.h,identity.h);},"normal-query-native-cancelled");
    need(!completed.load(),"query-native-cancel-fixture-power-granted-result");
  });
  test("query-native-cancel-pre-signaled-identity",[&]{
    auto power=qncEvent();auto identity=qncEvent(true,true);auto completion=qncEvent();
    std::atomic<bool> completed{false};
    need(queryCancellationState(power.h,identity.h)==QueryCancellationState::Revoked,
        "query-native-cancel-fixture-identity-not-revoked");
    qncReason([&]{requireQueryCancellation(power.h,identity.h);},"normal-query-native-cancelled");
    qncReason([&]{waitQueryReturn(completed,completion.h,5000,power.h,identity.h);},"normal-query-native-cancelled");
    need(!completed.load(),"query-native-cancel-fixture-identity-granted-result");
  });
  test("query-native-cancel-simultaneous-completion-power",[&]{
    auto power=qncEvent(true,true);auto identity=qncEvent();auto completion=qncEvent(true,true);
    qncReason([&]{(void)waitQuerySignal(completion.h,5000,power.h,identity.h);},"normal-query-native-cancelled");
    need(WaitForSingleObject(completion.h,0)==WAIT_OBJECT_0,"query-native-cancel-fixture-tie-consumed-completion");
  });
  test("query-native-cancel-simultaneous-completion-identity",[&]{
    auto power=qncEvent();auto identity=qncEvent(true,true);auto completion=qncEvent(true,true);
    qncReason([&]{(void)waitQuerySignal(completion.h,5000,power.h,identity.h);},"normal-query-native-cancelled");
    need(WaitForSingleObject(completion.h,0)==WAIT_OBJECT_0,"query-native-cancel-fixture-tie-consumed-completion");
  });
  test("query-native-cancel-late-power-wakes-return",[&]{qncLateReturn(false);});
  test("query-native-cancel-late-identity-wakes-return",[&]{qncLateReturn(true);});
  test("query-native-cancel-missing-peer-unavailable",[&]{
    auto power=qncEvent();auto identity=qncEvent();auto completion=qncEvent();
    std::atomic<bool> completed{true};
    const std::array<std::pair<HANDLE,HANDLE>,3> pairs{{
      {power.h,nullptr},{nullptr,identity.h},{power.h,power.h}
    }};
    for(const auto& pair:pairs){
      need(queryCancellationState(pair.first,pair.second)==QueryCancellationState::Unavailable,
          "query-native-cancel-fixture-partial-or-alias-accepted");
      qncReason([&]{requireQueryCancellation(pair.first,pair.second);},"normal-query-cancellation-unavailable");
      qncReason([&]{(void)waitQuerySignal(completion.h,5000,pair.first,pair.second);},"normal-query-cancellation-unavailable");
      qncReason([&]{waitQueryReturn(completed,completion.h,5000,pair.first,pair.second);},"normal-query-cancellation-unavailable");
    }
  });
  test("query-native-cancel-invalid-handle-unavailable",[&]{
    auto power=qncEvent();auto identity=qncEvent();auto completion=qncEvent();
    // Do not close and reuse a live handle number. Verify a non-pseudo invalid
    // sentinel and a real event handle deliberately lacking SYNCHRONIZE.
    const auto invalid=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x7ffffff0u));
    need(WaitForSingleObject(invalid,0)==WAIT_FAILED,"query-native-cancel-fixture-sentinel-not-invalid");
    H noWait(CreateEventExW(nullptr,nullptr,CREATE_EVENT_MANUAL_RESET,EVENT_MODIFY_STATE));
    need(noWait.good(),"query-native-cancel-fixture-no-wait-event");
    need(WaitForSingleObject(noWait.h,0)==WAIT_FAILED,"query-native-cancel-fixture-no-wait-access-not-refused");
    const std::array<std::pair<HANDLE,HANDLE>,4> pairs{{
      {invalid,identity.h},{power.h,invalid},{noWait.h,identity.h},{power.h,noWait.h}
    }};
    std::atomic<bool> completed{true};
    for(const auto& pair:pairs){
      need(queryCancellationState(pair.first,pair.second)==QueryCancellationState::Unavailable,
          "query-native-cancel-fixture-invalid-state-authorized");
      qncReason([&]{requireQueryCancellation(pair.first,pair.second);},"normal-query-cancellation-unavailable");
      qncReason([&]{(void)waitQuerySignal(completion.h,5000,pair.first,pair.second);},"normal-query-cancellation-unavailable");
      qncReason([&]{waitQueryReturn(completed,completion.h,5000,pair.first,pair.second);},"normal-query-cancellation-unavailable");
    }
  });
  test("query-native-cancel-completed-flag-cancelled",[&]{
    for(const bool revokeIdentity:{false,true}){
      auto power=qncEvent();auto identity=qncEvent();auto completion=qncEvent();
      std::atomic<bool> completed{true};
      need(SetEvent(revokeIdentity?identity.h:power.h)!=FALSE,"query-native-cancel-fixture-completed-revoke");
      qncReason([&]{waitQueryReturn(completed,completion.h,5000,power.h,identity.h);},"normal-query-native-cancelled");
      need(WaitForSingleObject(completion.h,0)==WAIT_TIMEOUT,"query-native-cancel-fixture-completed-forged-event");
    }
  });
  test("query-native-cancel-completion-signal-without-result",[&]{
    auto power=qncEvent();auto identity=qncEvent();auto completion=qncEvent(true,true);
    std::atomic<bool> completed{false};
    qncReason([&]{waitQueryReturn(completed,completion.h,5000,power.h,identity.h);},"context-query-return-budget");
    need(!completed.load()&&queryCancellationState(power.h,identity.h)==QueryCancellationState::Current,
        "query-native-cancel-fixture-signal-granted-result");
  });
  test("query-native-cancel-normal-completion",[&]{
    auto power=qncEvent();auto identity=qncEvent();auto completion=qncEvent();auto go=qncEvent();
    std::atomic<bool> completed{false};std::exception_ptr failure;
    QncOwnedWorker worker(go.h,power.h,identity.h);
    worker.thread=std::thread([&]{
      try{
        need(WaitForSingleObject(go.h,5000)==WAIT_OBJECT_0,"query-native-cancel-fixture-normal-gate");
        completed.store(true);
        need(SetEvent(completion.h)!=FALSE,"query-native-cancel-fixture-normal-completion-signal");
      }catch(...){failure=std::current_exception();}
    });
    need(SetEvent(go.h)!=FALSE,"query-native-cancel-fixture-normal-release");
    waitQueryReturn(completed,completion.h,5000,power.h,identity.h);
    worker.join();if(failure)std::rethrow_exception(failure);
    need(completed.load()&&queryCancellationState(power.h,identity.h)==QueryCancellationState::Current,
        "query-native-cancel-fixture-normal-result-or-current-lost");
    need(waitQuerySignal(completion.h,5000,power.h,identity.h)==WAIT_OBJECT_0,
        "query-native-cancel-fixture-normal-ready-signal");
  });
  test("query-native-cancel-idle-command-cancelled",[&]{
    qncIdleCommand(false);qncIdleCommand(true);
  });
}
