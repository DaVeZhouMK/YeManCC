// Supplemental original cancel case: actual worker/injectBootstrap against
// own loopback CDP. Discovery adapters above do not prove public TCP ownership.
static Json workerScopeObservation(){
    unsigned liveStates=0,liveParents=0;
    for(const auto& weak:workerScopeStates)if(!weak.expired())++liveStates;
    for(const auto& weak:workerScopeParents)if(!weak.expired())++liveParents;
    return {{"statesObserved",workerScopeStates.size()},{"liveStates",liveStates},{"liveParentReferences",liveParents},
        {"fixtureSafetyExcluded",true},{"independentClientLeafCleanup",false},
        {"actualFragmentReads",workerScopeFragmentReads.load()},{"actualFinalReads",workerScopeFinalReads.load()},
        {"partialScopeOwned",workerScopePartialOwned.load()}};
}
static int workerBootstrapChild(ymcc::deckybinding::ChildChannel& channel,const std::string& payload){
    using namespace ymcc::deckybinding;Json observation;int resultCode=2;
    std::filesystem::path summary;detail::Handle fragmentSignal;Json watchFailures=Json::array();
    try{
        const auto outer=Json::parse(payload);const auto kind=outer.at("kind").get<std::string>();summary=outer.at("summary").get<std::string>();
        const auto request=outer.at("request");const auto parsed=worker::parse(request.dump());
        workerScopeRoot=parsed.steam.root;workerScopeSteamPid=parsed.steam.pid;
        workerScopeMode=kind=="fragment-cancel"?5u:kind=="fragment-reset"?6u:kind=="fragment-complete"?7u:kind=="deny"?2u:kind=="callback"?3u:kind=="context"?4u:1u;
        if(workerScopeMode.load()>=5){
            const auto event=outer.at("fragmentEvent").get<std::string>();std::wstring name(event.begin(),event.end());
            fragmentSignal.value=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str());if(!fragmentSignal.value)throw std::runtime_error("worker fragment signal");
            workerScopeFragmentSignal=fragmentSignal.value;
        }
        WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("worker scope WSA");
        {
            CdpFixture peer(false,true,false,false,0,false,true);peer.run=request.at("bootstrap").at("runId").get<std::string>();workerScopePort=peer.port;
            resultCode=worker::run(request.dump(),channel.cancelEvent(),[&](const Json& packet){
                if(packet.at("kind")=="diagnostic"&&packet.at("detail").value("bootstrapStage",std::string{})=="failed")watchFailures.push_back(packet.at("detail"));
                const auto bytes=packet.dump();return bool(channel.writeFrame(std::vector<std::uint8_t>(bytes.begin(),bytes.end()),2500));
            });
            // Constructor failure safety is an independent fixture reference;
            // drop it BEFORE ownership assertions. Never close a client leaf.
            constructorSafety.reset();
            if(kind=="success"||workerScopeMode.load()>=5){
                const auto until=GetTickCount64()+1500;
                while(GetTickCount64()<until&&std::any_of(workerScopeStates.begin(),workerScopeStates.end(),[](const auto& weak){return !weak.expired();}))Sleep(1);
            }
            observation=workerScopeObservation();observation.update({{"actualWorkerRun",true},{"actualInjectBootstrap",true},
                {"discoveryAdapters",2},{"discoveryReads",workerScopeDiscoveryReads.load()},{"ownerReads",workerScopeOwnerReads.load()},
                {"originalWorkerExit",resultCode},{"closeCallsOnTarget",refusedCloseCalls.load()},
                {"constructorCloseAccepted",constructorCloseAccepted.load()},{"kind",kind},{"loopbackHeadersReceived",peer.httpHeadersRead.load()},
                {"watchFailures",watchFailures}});
        }
        WSACleanup();
    }catch(const std::exception& error){observation["fixtureError"]=error.what();resultCode=92;}
    if(!summary.empty()){std::ofstream file(summary,std::ios::binary);file<<observation.dump(2);file.close();if(!file)resultCode=93;}
    return resultCode;
}
static int workerBootstrapOnly(const char* output,const std::string& kind){
    using namespace ymcc::deckybinding;
    Json raw{{"selection","--worker-bootstrap-only"},{"selectedOriginalCase","cancel wakes passive receive promptly"},
        {"kind",kind},{"realSteam",false},{"actualBrowserJS",false},{"fullTypedParent",false}};
    const bool fragment=kind=="fragment-cancel"||kind=="fragment-reset"||kind=="fragment-complete";
    if(!fragment&&kind!="success"&&kind!="deny"&&kind!="callback"&&kind!="context")return 64;
    try{
        const auto identity=worker::contextIdentity(GetCurrentProcess());
        const auto root=std::filesystem::path(identity.at("path").get<std::string>()).parent_path().string();
        Json request{{"schema",worker::schema},{"id",std::string(32,'a')},{"generation","17"},
            {"steam",{{"root",root},{"pid",GetCurrentProcessId()},{"created",identity.at("birth")},{"account",1},
                {"windowsSession",identity.at("session")},{"identitySource","shared-steam-observer.v1"}}},
            {"bootstrap",{{"endpoint","ws://127.0.0.1:1/mirror"},{"token",std::string(64,'c')},{"runId",std::string(32,'b')}}},
            {"requireFreshContext",false},{"retainedFrontendOnly",false}};
        detail::Handle fragmentEvent;std::string fragmentName;
        if(fragment){
            fragmentName="Local\\YMCC-Worker-Fragment-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+kind;
            const std::wstring name(fragmentName.begin(),fragmentName.end());fragmentEvent.value=CreateEventW(nullptr,TRUE,FALSE,name.c_str());
            if(!fragmentEvent.value||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("worker fragment parent event");
        }
        Owner owner;if(!owner.start())throw std::runtime_error("worker scope owner start");const auto before=owner.snapshot();
        const auto held=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,before.processId);
        if(!held)throw std::runtime_error("worker scope original child lease");
        struct Held {HANDLE value;~Held(){CloseHandle(value);}} child{held};
        const std::string summary=std::filesystem::absolute(output).string()+".child.json";
        const auto payload=std::string("worker-bootstrap-")+Json{{"kind",kind},{"summary",summary},{"request",request},{"fragmentEvent",fragmentName}}.dump();
        if(!owner.writeFrame(std::vector<std::uint8_t>(payload.begin(),payload.end()),2500,before.generation))throw std::runtime_error("worker scope request");
        Json packets=Json::array(),result;
        for(unsigned count=0;count<64;++count){
            const auto incoming=owner.readFrame(10000,before.generation);if(!incoming)throw std::runtime_error("worker scope original result absent");
            auto packet=Json::parse(incoming.payload);packets.push_back(packet);
            if(packet.at("kind")=="result"){result=packet.at("detail");break;}
        }
        if(!result.is_object())throw std::runtime_error("worker scope no result");
        const bool fragmentCompletion=!fragment||WaitForSingleObject(fragmentEvent.value,5000)==WAIT_OBJECT_0;
        const bool resetTerminal=kind!="fragment-reset"||WaitForSingleObject(held,5000)==WAIT_OBJECT_0;
        const auto began=GetTickCount64();const auto stopped=owner.stop(500,1500);const auto after=owner.snapshot();
        DWORD childExit=STILL_ACTIVE;const bool exited=WaitForSingleObject(held,0)==WAIT_OBJECT_0&&GetExitCodeProcess(held,&childExit);
        Json observed;{std::ifstream file(summary,std::ios::binary);if(!file)throw std::runtime_error("worker scope summary absent");file>>observed;}
        const bool returned=stopped.fullyDestroyed&&after.fullyDestroyed&&exited&&after.handles==0&&after.activeCalls==0&&after.pendingIo==0&&after.jobActiveProcesses==0;
        const bool success=kind=="success"||fragment;const auto expected=kind=="deny"?"mirror-bootstrap-cleanup-incomplete":kind=="callback"?"mirror-bootstrap-callback-failed":"mirror-bootstrap-context-failed";
        const bool outcome=success?result.value("ok",false)&&result.value("mutated",false)&&result.value("watching",false)&&(kind=="fragment-reset"?(childExit==0||childExit==2):childExit==0):
            !result.value("ok",true)&&!result.value("mutated",true)&&result.value("reason",std::string{})==expected&&childExit==2;
        const bool resources=success?observed.value("statesObserved",0u)==3&&observed.value("liveStates",99u)==0&&observed.value("liveParentReferences",99u)==0:
            observed.value("statesObserved",0u)==1&&observed.value("liveStates",0u)==1&&observed.value("liveParentReferences",0u)==2;
        const bool deny=kind!="deny"||(result.value("error",DWORD{0})==ERROR_ACCESS_DENIED&&result.contains("closeAccepted")&&result["closeAccepted"]==false&&result.contains("finalNotification")&&result["finalNotification"]==false&&observed.value("closeCallsOnTarget",0u)==1);
        const bool fragmentOutcome=!fragment||(fragmentCompletion&&resetTerminal&&observed.value("actualFragmentReads",0u)>=2&&observed.value("partialScopeOwned",false)&&
            (kind!="fragment-complete"||observed.value("actualFinalReads",0u)>=1)&&
            (kind!="fragment-reset"||!observed.value("watchFailures",Json::array()).empty()));
        raw.update({{"passed",outcome&&resources&&deny&&returned&&!after.jobTerminationAttempted&&fragmentOutcome},{"result",result},{"packets",packets},{"observed",observed},
            {"fragmentCompletionObserved",fragmentCompletion},{"resetTerminalBeforeStop",resetTerminal},
            {"ownerDestroyed",returned},{"childExitCode",childExit},{"jobTerminationAttempted",after.jobTerminationAttempted},
            {"handles",after.handles},{"activeCalls",after.activeCalls},{"pendingIo",after.pendingIo},{"jobActiveProcesses",after.jobActiveProcesses},{"ownerStopMs",GetTickCount64()-began}});
    }catch(const std::exception& error){raw["passed"]=false;raw["fixtureError"]=error.what();}
    std::ofstream file(output,std::ios::binary);file<<raw.dump(2);file.close();return raw.value("passed",false)?0:2;
}
