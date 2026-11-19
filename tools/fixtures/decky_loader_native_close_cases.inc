// Existing native.typed-runtime-live-watch parameters; no Steam or installed Loader.
inline Json loaderNativeFinalClose(const std::filesystem::path& output){
    loaderCloseProbe::Release originalLoaderCloseProbe;
    Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness harness(output,context,false,{},[](Json& request,unsigned){request["bootstrap"]["fixtureFailureTrace"]=true;});
    // The original context provider now fails after its first captured use.
    // A post-start relay must retain that use, not sample the replacement state.
    harness.poisonContextAfterResult=true;
    const auto beforeHandles=handles();const auto handleSnapshotBefore=handleTable();const auto beforeThreads=threadCount();harness.start();
    const bool drained=waitFor([&]{return harness.burstDone.load();});const auto activeThreads=threadCount();
    const auto active=harness.client->snapshot();const auto plansBeforeExit=harness.plans.load();
    bool childExitDrained=false;
    {
        detail::Handle loader{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE|SYNCHRONIZE,FALSE,harness.runtime.snapshot().pid)};
        need(loader.value&&harness.runtime.ownsLiveProcess(loader.value),"exit-probe-target-is-exact-runtime-owned-inert-loader");
        harness.runtime.requestExitFence();
        need(!harness.runtime.requestTransportRecovery(),"terminal-session-exit-rejects-late-transport-recovery");
        need(!harness.runtime.requestConnectionCheck(),"terminal-session-exit-rejects-late-health-demand");
        need(!harness.runtime.requestLaunchAdmissionRefresh(),"terminal-session-exit-rejects-late-admission");
        need(!harness.runtime.requestPreparationRefresh(999),"terminal-session-exit-rejects-late-preparation");
        need(!harness.runtime.requestBindingRefresh(999),"terminal-session-exit-rejects-late-binding");
        need(!harness.runtime.setEnabled(true),"terminal-session-exit-cannot-be-reenabled-by-late-callback");
        need(TerminateProcess(loader.value,1073807364u)!=FALSE,"own-inert-loader-replays-historical-exit-code");
        need(WaitForSingleObject(loader.value,1000)==WAIT_OBJECT_0,"own-inert-loader-exit-observed");
        childExitDrained=waitFor([&]{return harness.client->snapshot().fullyDestroyed&&harness.runtime.snapshot().phase=="disabled";});
        need(childExitDrained&&harness.loaderFaultPublications==0,"confirmed-exit-child-death-is-cleanup-not-loader-failure");
    }
    harness.runtime.observeSteam(1,true);harness.runtime.shutdown();
    const auto finalCloseFacts=loaderCloseProbe::facts(),nativeDebt=loaderCloseProbe::retainedFacts();
    if(loaderCloseProbe::mode=="joint")need(finalCloseFacts.at("terminateError")==5&&finalCloseFacts.at("fallbackError")==6&&
        finalCloseFacts.at("terminateRefusals")>=1&&finalCloseFacts.at("fallbackRefusals")>=1&&finalCloseFacts.at("jobActiveProcesses")==0,
        "joint-close-probe-did-not-reach-real-terminate-and-native-close-refusal");
    need(nativeDebt.at("matched")==1&&nativeDebt.at("handles")==1&&!nativeDebt.at("fullyDestroyed").get<bool>(),
        "refused-loader-job-must-remain-explicit-retained-debt");
    loaderCloseProbe::allowOriginalClose();
    Json nextIngress;
    {
        Harness following(output/"following-runtime",context,false);following.start();
        need(waitFor([&]{return following.burstDone.load();}),"following-original-watch-did-not-drain");
        nextIngress={{"frames",following.diagnostics.load()},{"plans",following.plans.load()}};
        following.runtime.shutdown();
    }
    const auto retainedAfter=ymcc::deckybinding::Owner::retainedFailures();
    need(retainedAfter.empty(),"next-original-runtime-ingress-did-not-return-loader-debt");
    const auto remaining=harness.client->snapshot();const bool cefAlive=context.alive();
    Json watchTrace=Json::array();Json bindingStart;
    for(const auto& row:harness.failureTraceLog){
        if(row.value("actionOperation",std::string{})!="binding")continue;
        if(row.value("actionPhase",std::string{})=="started")bindingStart=row;
        if(row.value("actionPhase",std::string{})=="observation")watchTrace.push_back(row);
    }
    need(harness.throwFailureContext.load()&&bindingStart.is_object()&&watchTrace.size()==1,"post-start-watch-missing-or-stale-packets-misattributed");
    const auto& fault=watchTrace.at(0);
    for(const auto* key:{"actionId","generation","nativePowerGeneration","steamPid","steamBirth","recoveryEpisode"})need(fault.at(key)==bindingStart.at(key),"watch-error-resampled-a-late-session-or-power");
    need(fault.at("steamPid")==worker::number(context.lease.at("pid"))&&fault.at("nativePowerGeneration")==123,"watch-error-lost-original-context-after-provider-failed");
    need(fault["steamFailure"].value("domain",std::string{})=="cdp-protocol"&&fault["steamFailure"].value("code",0)==-32000,"watch-failure-lost-real-protocol-code");
    need(fault.dump().size()<=2304&&fault.dump().find("secret")==std::string::npos&&fault.dump().find("private-token")==std::string::npos,"watch-failure-leaked-private-text");
    return {{"id","native.typed-runtime-drains-live-watch-without-extra-reader-thread"},
        {"passed",drained&&harness.diagnostics==192&&remaining.fullyDestroyed&&beforeHandles==handles()&&activeThreads==beforeThreads+1&&cefAlive&&harness.plans==plansBeforeExit},
        {"loaderFinalNativeClose",finalCloseFacts},{"nativeDebtBeforeIngress",nativeDebt},{"retainedAfterIngress",retainedAfter.size()},{"nextRuntimeIngress",nextIngress},{"watchFailureTrace",watchTrace},{"watchContextProviderFailedAfterResult",harness.throwFailureContext.load()},
        {"diagnosticFrames",harness.diagnostics.load()},{"watchDrained",drained},{"beforeThreads",beforeThreads},{"activeThreads",activeThreads},
        {"beforeHandles",beforeHandles},{"afterHandles",handles()},{"handleSnapshotBefore",handleSnapshotBefore},{"handleSnapshotAfter",handleTable()},{"active",state(active)},{"atExit",state(remaining)},{"cefUnaffected",cefAlive},
        {"terminalExitFenced",true},{"lateRecoveryOrEnableAccepted",false},{"plansAfterExitFence",harness.plans.load()-plansBeforeExit},
        {"historicalChildExitCode",1073807364u},{"childExitBeforeExplicitShutdownDrained",childExitDrained},{"loaderFaultPublicationsAfterFence",harness.loaderFaultPublications.load()}};
}
inline Json loaderNativeJointLive(const std::filesystem::path& output){
    struct ExactInertCleanup {
        detail::Handle child;
        ~ExactInertCleanup() noexcept {
            if(child.value&&WaitForSingleObject(child.value,0)==WAIT_TIMEOUT){TerminateProcess(child.value,995);WaitForSingleObject(child.value,1000);}
            const auto original=loaderCloseProbe::held.exchange(nullptr);
            if(original)SetHandleInformation(original,HANDLE_FLAG_PROTECT_FROM_CLOSE,0);
            ymcc::deckybinding::Owner::retryRetainedCleanup(0);
        }
    } cleanup;
    Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness harness(output,context,false);const auto beforeHandles=handles();harness.start();
    need(waitFor([&]{return harness.burstDone.load();}),"live-joint-original-watch-drain");
    const auto originalPid=harness.runtime.snapshot().pid;
    cleanup.child.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE|SYNCHRONIZE,FALSE,originalPid);
    need(cleanup.child.value&&harness.runtime.ownsLiveProcess(cleanup.child.value),"live-joint-exact-owned-inert-child");
    const auto childIdentity=identity(cleanup.child.value);
    harness.runtime.observeSteam(0,true);
    need(waitFor([&]{return harness.runtime.snapshot().phase=="waiting-steam";}),"live-joint-original-steam-absence-stop-not-finished");
    const auto facts=loaderCloseProbe::facts(),debt=loaderCloseProbe::retainedFacts();
    const auto heldJob=loaderCloseProbe::held.load();
    struct JobMembers {DWORD assigned=0,count=0;ULONG_PTR ids[64]{};} members;
    need(QueryInformationJobObject(heldJob,JobObjectBasicProcessIdList,&members,sizeof(members),nullptr)!=FALSE&&
        members.count>=1&&members.count<=64&&members.assigned==members.count&&members.count==facts.at("jobActiveProcesses").get<DWORD>(),
        "live-joint-exact-job-member-count-not-observed");
    Json initialJobMembers=Json::array();bool originalMember=false;
    for(DWORD index=0;index<members.count;++index){
        need(members.ids[index]!=0&&members.ids[index]<=MAXDWORD,"live-joint-invalid-job-member");
        const auto pid=static_cast<DWORD>(members.ids[index]);detail::Handle peer{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)};
        BOOL sameJob=FALSE;need(peer.value&&IsProcessInJob(peer.value,heldJob,&sameJob)&&sameJob,"live-joint-member-identity-unconfirmed");
        initialJobMembers.push_back({{"pid",pid},{"identity",identity(peer.value)},{"sameOwnedJob",true}});
        if(pid==originalPid)originalMember=true;
    }
    need(originalMember,"live-joint-original-root-is-not-in-exact-job");
    std::cout<<"LIVE_JOINT_PREFLIGHT "<<Json{{"facts",facts},{"debt",debt},{"childLive",WaitForSingleObject(cleanup.child.value,0)==WAIT_TIMEOUT},{"members",initialJobMembers}}.dump()<<'\n'<<std::flush;
    need(facts.at("terminateError")==5&&facts.at("fallbackError")==6&&facts.at("jobActiveProcesses").get<DWORD>()>=1&&originalMember,
        "live-joint-native-errors-or-child-live-not-reached");
    need(debt.at("matched")==1&&debt.at("handles")==1&&!debt.at("fullyDestroyed").get<bool>(),
        "live-joint-native-handle-debt-was-lost");
    const auto plansBefore=harness.plans.load();
    harness.runtime.observeSteam(1,true);
    need(waitFor([&]{return harness.runtime.snapshot().reason=="owned-loader-cleanup-pending";}),"live-joint-protected-debt-not-parked");
    const auto protectedJobIngresses=loaderCloseProbe::jobIngresses.load();
    std::cout<<"LIVE_JOINT_PROTECTED "<<Json{{"runtime",runtimeState(harness)},{"plansBefore",plansBefore},{"plansNow",harness.plans.load()},
        {"jobIngresses",protectedJobIngresses},{"originalChildLive",WaitForSingleObject(cleanup.child.value,0)==WAIT_TIMEOUT}}.dump()<<'\n'<<std::flush;
    need(harness.runtime.snapshot().pid==0&&protectedJobIngresses==1&&WaitForSingleObject(cleanup.child.value,0)==WAIT_TIMEOUT,
        "live-joint-protected-child-granted-replacement");
    harness.runtime.observeSteam(0,true);
    need(waitFor([&]{return harness.runtime.snapshot().phase=="waiting-steam";}),"live-joint-second-absence-not-finished");
    loaderCloseProbe::allowOriginalClose();
    harness.runtime.observeSteam(1,true);
    need(waitFor([&]{return harness.runtime.snapshot().reason=="owned-loader-cleanup-pending";}),"live-joint-active-child-not-parked-after-unprotect");
    const auto retainedActive=ymcc::deckybinding::Owner::retainedFailures();
    const auto activeJobIngresses=loaderCloseProbe::jobIngresses.load();
    need(harness.runtime.snapshot().pid==0&&activeJobIngresses==1&&retainedActive.size()==1&&
        retainedActive[0].jobActiveProcesses>=1&&!retainedActive[0].fullyDestroyed&&WaitForSingleObject(cleanup.child.value,0)==WAIT_TIMEOUT,
        "live-joint-unprotected-live-child-was-falsely-clean-or-replaced");
    need(TerminateProcess(cleanup.child.value,995)!=FALSE&&WaitForSingleObject(cleanup.child.value,1000)==WAIT_OBJECT_0,
        "live-joint-exact-fixture-child-did-not-exit");
    need(waitFor([&]{JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};return QueryInformationJobObject(heldJob,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr)&&info.ActiveProcesses==0;},1000),
        "live-joint-actual-job-zero-not-observed");
    DWORD closeError=0;need(cleanup.child.close(closeError),"live-joint-fixture-query-handle-return");
    need(harness.runtime.requestTransportRecovery(),"live-joint-original-recovery-ingress-refused");
    need(waitFor([&]{return harness.runtime.snapshot().pid!=0&&harness.plans>plansBefore;}),"live-joint-original-ingress-did-not-resume-after-child-exit");
    const auto replacementPid=harness.runtime.snapshot().pid;Json replacementIdentity;
    {
        detail::Handle replacement{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,replacementPid)};
        need(replacement.value&&harness.runtime.ownsLiveProcess(replacement.value),"live-joint-replacement-actual-owned-process-unconfirmed");
        replacementIdentity=identity(replacement.value);
        need(replacementIdentity.at("birth")!=childIdentity.at("birth"),"live-joint-did-not-observe-new-process-birth");
    }
    need(loaderCloseProbe::jobIngresses==2,"live-joint-recovery-created-unexpected-number-of-jobs");
    const auto remainingDebt=ymcc::deckybinding::Owner::retainedFailures();need(remainingDebt.empty(),"live-joint-retained-debt-not-returned");
    harness.runtime.shutdown();const auto afterHandles=handles();const auto final=harness.client->snapshot();
    return {{"id","native.typed-runtime-drains-live-watch-without-extra-reader-thread"},
        {"passed",beforeHandles==afterHandles&&final.fullyDestroyed&&context.alive()},
        {"variant","same-runtime-live-child-joint-close-refusal"},{"beforeHandles",beforeHandles},{"afterHandles",afterHandles},
        {"loaderFinalNativeClose",facts},{"nativeDebtBeforeIngress",debt},{"retainedActiveHandles",retainedActive[0].handles},
        {"retainedActiveJobProcesses",retainedActive[0].jobActiveProcesses},{"retainedAfterIngress",remainingDebt.size()},
        {"originalChildIdentity",childIdentity},{"originalPid",originalPid},{"initialJobMembers",initialJobMembers},{"replacementPid",replacementPid},{"replacementIdentity",replacementIdentity},
        {"protectedJobIngresses",protectedJobIngresses},{"activeJobIngresses",activeJobIngresses},{"finalJobIngresses",loaderCloseProbe::jobIngresses.load()},{"noReplacementWhileProtectedOrActive",true},
        {"atExit",state(final)},{"cefUnaffected",context.alive()}};
}
inline Json loaderNativeSameRuntime(const std::filesystem::path& output){
    Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness harness(output,context,false);const auto beforeHandles=handles();harness.start();
    need(waitFor([&]{return harness.burstDone.load();}),"native-debt-initial-watch-drain");
    harness.runtime.setEnabled(false);
    const auto refused=loaderCloseProbe::facts(),debt=loaderCloseProbe::retainedFacts();
    need(refused.value("nativeCloseRefusals",0)==1&&refused.value("nativeCloseError",0)==6&&
        debt.value("matched",0)==1&&debt.value("handles",0)==1&&!debt.value("fullyDestroyed",true),
        "same-runtime-refused-job-debt-was-lost");
    const auto plansBeforeRetry=harness.plans.load();
    need(harness.runtime.setEnabled(true),"same-runtime-reenable-refused");
    need(waitFor([&]{return harness.runtime.snapshot().reason=="owned-loader-cleanup-pending";}),
        "same-runtime-protected-debt-did-not-park-launch");
    const auto protectedState=harness.runtime.snapshot();const auto protectedDebt=loaderCloseProbe::retainedFacts();
    need(protectedState.pid==0&&protectedDebt.value("matched",0)==1&&!protectedDebt.value("fullyDestroyed",true),
        "protected-native-debt-granted-a-replacement-loader");
    loaderCloseProbe::allowOriginalClose();
    need(harness.runtime.requestTransportRecovery(),"original-transport-ingress-refused-after-unprotect");
    const bool restored=waitFor([&]{return harness.runtime.snapshot().pid!=0&&harness.plans.load()>plansBeforeRetry;});
    if(!restored)std::cout<<"LOADER_NATIVE_RECOVERY_FAILURE "<<Json{{"runtime",runtimeState(harness)},{"plans",harness.plans.load()},
        {"nativeDebtCount",ymcc::deckybinding::Owner::retainedFailures().size()},{"globalRecovery",harness.globalRecoveryLog},
        {"loaderStops",harness.loaderStopLog}}.dump()<<'\n'<<std::flush;
    need(restored,"original-transport-ingress-did-not-recover-native-debt");
    const auto recoveredPid=harness.runtime.snapshot().pid;
    const auto retainedAfter=ymcc::deckybinding::Owner::retainedFailures();
    need(retainedAfter.empty(),"same-runtime-original-ingress-left-native-debt");
    harness.runtime.shutdown();
    const auto afterHandles=handles();const auto remaining=harness.client->snapshot();
    const bool passed=beforeHandles==afterHandles&&remaining.fullyDestroyed&&context.alive();
    return {{"id","native.typed-runtime-drains-live-watch-without-extra-reader-thread"},{"passed",passed},
        {"variant","same-runtime-protected-native-close-recovery"},{"refused",refused},{"debtBeforeRetry",debt},
        {"protectedLaunchPid",protectedState.pid},{"protectedDebt",protectedDebt},{"recoveredPid",recoveredPid},
        {"retainedAfterIngress",retainedAfter.size()},{"plans",harness.plans.load()},{"actualDiagnosticFrames",harness.diagnostics.load()},
        {"beforeHandles",beforeHandles},{"afterHandles",afterHandles},{"atExit",state(remaining)},{"cefUnaffected",context.alive()}};
}
inline Json loaderNativePausedRecovery(const std::filesystem::path& output){
    struct AfterFailureCleanup {
        ~AfterFailureCleanup() noexcept {
            const auto original=loaderCloseProbe::held.exchange(nullptr);
            if(original){SetHandleInformation(original,HANDLE_FLAG_PROTECT_FROM_CLOSE,0);ymcc::deckybinding::Owner::retryRetainedCleanup(0);}
        }
    } afterFailureCleanup;
    Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    std::atomic<bool> connected{true};std::atomic<unsigned> pageSteps{0};
    const auto health=[&]{
        ymcc::deckysidebar::ConnectionHealth value{true,connected.load(),"fixture-mirror-state",false,"native-resource-fixed-health"};
        value.transportConnected=connected.load();return value;
    };
    const auto step=[&](ymcc::deckysidebar::ConnectionRecoveryWindow::Action){++pageSteps;return Json{{"ok",true},{"mutated",false},{"uncertain",false}};};
    Harness harness(output,context,false,{},{},{},{},{},health,step);const auto beforeHandles=handles();harness.start();
    need(waitFor([&]{return harness.burstDone.load();}),"paused-native-debt-initial-drain");
    harness.runtime.setEnabled(false);connected=false;
    need(!harness.runtime.requestConnectionCheck(),"OFF-native-debt-read-revived-runtime");
    need(harness.runtime.setEnabled(true),"paused-native-debt-reenable");
    need(waitFor([&]{return harness.runtime.snapshot().reason=="owned-loader-cleanup-pending";}),"paused-native-debt-not-retained");
    const auto prior=loaderCloseProbe::pausedReads.load();need(harness.runtime.requestConnectionCheck(),"paused-native-debt-check-refused");
    need(waitFor([&]{return loaderCloseProbe::pausedReads.load()>prior;}),"local-native-debt-was-not-parked-before-recovery-ladder");
    const auto debt=loaderCloseProbe::retainedFacts();
    need(debt.value("matched",0)==1&&!debt.value("fullyDestroyed",true)&&pageSteps==0&&loaderCloseProbe::pausedAttempts==0,
        "local-native-debt-spent-page-or-recovery-authority");
    const auto pausedReadCount=loaderCloseProbe::pausedReads.load();
    loaderCloseProbe::allowOriginalClose();
    need(harness.runtime.requestTransportRecovery(),"paused-native-debt-original-ingress-refused");
    need(waitFor([&]{return harness.runtime.snapshot().pid!=0;}),"paused-native-debt-did-not-recover");
    const auto priorConnected=loaderCloseProbe::connectedReads.load();
    connected=true;need(harness.runtime.requestConnectionCheck(),"recovered-native-debt-health-check-refused");
    need(waitFor([&]{return loaderCloseProbe::connectedReads.load()>priorConnected;}),"recovered-native-debt-full-health-not-consumed");
    harness.runtime.shutdown();
    const auto afterHandles=handles();const auto remaining=harness.client->snapshot();
    return {{"id","native.typed-runtime-drains-live-watch-without-extra-reader-thread"},
        {"passed",beforeHandles==afterHandles&&remaining.fullyDestroyed&&context.alive()&&pageSteps==0},
        {"variant","local-native-debt-parks-page-recovery"},{"beforeHandles",beforeHandles},{"afterHandles",afterHandles},
        {"debtBeforeIngress",debt},{"pausedHealthReads",pausedReadCount},{"recoveryAttemptsWhilePaused",loaderCloseProbe::pausedAttempts.load()},
        {"pageSteps",pageSteps.load()},{"retainedAfterIngress",ymcc::deckybinding::Owner::retainedFailures().size()},
        {"atExit",state(remaining)},{"cefUnaffected",context.alive()}};
}

