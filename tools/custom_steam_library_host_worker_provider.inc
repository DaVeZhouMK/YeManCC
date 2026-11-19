// Private HOST_CHAIN variant, included inside LibraryFactsIpc.
static bool chainKnown(const std::string& n){
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
return n=="chain-observer-power-revoked-preserves"||n=="chain-observer-shutdown-commit";
#endif

#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
return ChainTyped::known(n);
#else
#ifdef YMCC_LIBRARY_TYPED_SHUTDOWN
if(ChainTyped::known(n))return true;
#endif
return n=="chain-single-commit"||n=="chain-current-dedupe"||n=="chain-close-retains-commit"||n=="chain-final-unknown-preserves"||n=="chain-final-present-preserves"||n=="chain-nonzero-worker-continues";
#endif
}
static std::wstring chainEnv(const wchar_t* k){const auto z=GetEnvironmentVariableW(k,nullptr,0);if(!z)return {};std::wstring v(z,L'\0');const auto n=GetEnvironmentVariableW(k,v.data(),z);check(n&&n<z,"CHAIN environment read");v.resize(n);return v;}
static bool chainSame(const fs::path& a,const fs::path& b){return canonicalPathKey(fs::absolute(a))==canonicalPathKey(fs::absolute(b));}
static void chainPath(const fs::path& p,const fs::path& root){
    check(p.is_absolute()&&within(p,root),"CHAIN absolute owned path");
    for(auto a=p;!a.empty();a=a.parent_path()){check(!isReparsePoint(a),"CHAIN reparse ancestor");if(a==a.parent_path())break;}
}
static void chainSet(const wchar_t* key,const fs::path& p){check(SetEnvironmentVariableW(key,p.c_str())!=FALSE,"CHAIN child environment");}
static json chainSeed(const fs::path& root){
    check(!fs::exists(root/L"transaction"),"CHAIN fresh transaction");
    Fixture f(root/L"transaction",true);textFile(f.local,validLocal);
    textFile(f.root/L"Game"/L"Game.exe","owned game marker; never launched");fs::create_directories(f.root/L"Trainer");
    writeJsonAtomic(f.data/L"data-root.json",dataRootManifest(f.data));
    auto c=defaultLibraryConfig();c["roots"]=json::array({toUtf8((f.root/L"Game").wstring())});c["trainerRoots"]=json::array({toUtf8((f.root/L"Trainer").wstring())});
    c["manualGameDirectories"]=json::array();c["firstRunDefaultScan"]["status"]="completed";
    writeJsonAtomic(f.data/L"config"/L"library-config.json",c);
    writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",json::object()}});
    writeJsonAtomic(f.data/L"state"/L"library-scan.json",{{"schemaVersion",1},{"games",json::array({{
        {"gameDirectory",toUtf8((f.root/L"Game").wstring())},{"directoryName","Host worker fixture"},{"primaryExecutable",toUtf8((f.root/L"Game"/L"Game.exe").wstring())},
        {"status","ready"},{"primaryRole","main-game"},{"selectionSource","manual"},{"scanComplete",true}}})},
        {"roots",json::array()},{"missingGames",json::array()},{"unscannedGames",json::array()}});
    g_workerDataRoot=f.data;
    chainSet(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",f.data);chainSet(L"YEMAN_GAME_WHITELIST_PATH",f.root/L"whitelist.txt");chainSet(L"YEMAN_PLAYER_BLACKLIST_PATH",f.root/L"blacklist.txt");
    chainSet(L"YMCC_CSL_CHAIN_ROOT",root);
    check(SetEnvironmentVariableW(L"YMCC_CSL_CHAIN_PROVIDER_PID",std::to_wstring(GetCurrentProcessId()).c_str())!=FALSE,"CHAIN provider env");
    check(SetEnvironmentVariableW(L"YMCC_CSL_CHAIN_CLI_EXIT",nullptr)!=FALSE,"CHAIN clear inherited fault");
    const auto before=readBinaryFile(f.target.path),localBefore=readBinaryFile(f.local);
    writeAtomic(root/L"shortcuts.before.bin",before);writeAtomic(root/L"localconfig.before.bin",localBefore);
    json seed={{"root",toUtf8(root.wstring())},{"shortcutsBeforeSha256",sha256(before)},{"localBeforeSha256",sha256(localBefore)},
        {"gameMarkerSha256",sha256(readBinaryFile(f.root/L"Game"/L"Game.exe"))},{"steamMarkerSha256",sha256(readBinaryFile(f.root/L"Steam"/L"steam.exe"))}};
#ifdef YMCC_LIBRARY_TYPED_SHUTDOWN
    if(ChainTyped::enabled){
        seed["steamClient"]=ChainTyped::client();
        if(ChainTyped::ackMode(ChainTyped::name))seed["typedActionReturnedEvent"]=toUtf8(ChainTyped::returnedEventName);
    }
#endif
    ExclusiveJsonWrite(root/L"seed.json",seed);return seed;
}
static int chainCli(int argc,wchar_t** argv){
    const fs::path root=chainEnv(L"YMCC_CSL_CHAIN_ROOT");
    check(root.is_absolute()&&chainSame(root,fs::path(modulePath()).parent_path().parent_path()),"CHAIN env root/module parentparent");
    chainPath(root,root);check(chainSame(modulePath(),root/L"CustomSteamLibrary"/L"SteamArtworkLab.exe"),"CHAIN worker image");
    const auto tx=root/L"transaction",data=tx/L"data";chainPath(data,root);
    check(chainSame(chainEnv(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT"),data),"CHAIN data root");
    for(const auto key:{L"YEMAN_GAME_WHITELIST_PATH",L"YEMAN_PLAYER_BLACKLIST_PATH"})chainPath(chainEnv(key),tx);
    const auto pidText=chainEnv(L"YMCC_CSL_CHAIN_PROVIDER_PID");
    check(!pidText.empty()&&pidText.find_first_not_of(L"0123456789")==std::wstring::npos,"CHAIN numeric PID");
    const auto pid=std::stoull(pidText);check(pid&&pid<=MAXDWORD&&pid!=GetCurrentProcessId(),"CHAIN provider PID");
    const bool commit=std::wstring(argv[1])==L"--commit-add-library-to-steam";
    check(commit||std::wstring(argv[1])==L"--plan-add-library-to-steam","CHAIN exact CLI");
    bool account=false,steam=false,selection=false,game=false,output=false,confirm=false;
    for(int i=2;i<argc;++i){
        const std::wstring key=argv[i];
        if(key==L"--confirm-steam-closed"){check(commit&&!confirm,"CHAIN confirmation");confirm=true;continue;}
        check(i+1<argc,"CHAIN CLI value");const std::wstring value=argv[++i];
        if(key==L"--account"){check(!account&&value==L"19627","CHAIN account");account=true;}
        else if(key==L"--selection-mode"){check(!selection&&value==L"explicit-game-directories","CHAIN explicit selection");selection=true;}
        else{
            const fs::path file=value;chainPath(file,root);
            if(key==L"--steam-root"){check(!steam&&chainSame(file,tx/L"Steam"),"CHAIN Steam root");steam=true;}
            else if(key==L"--select-game"){check(!game&&chainSame(file,tx/L"Game"),"CHAIN selected game");game=true;}
            else if(key==L"--output"){check(!output&&chainSame(file,data/L"state"/L"steam-add-plan.json"),"CHAIN output");output=true;}
            else if(key==L"--state")check(chainSame(file,data/L"state"/L"library-scan.json"),"CHAIN state");
            else check(false,"CHAIN unknown CLI option");
        }
    }
    check(account&&steam&&selection&&game&&output&&(!commit||confirm),"CHAIN complete CLI");
    identityCallerActive=true;factsIpcRealClient=true;factsIpcOwnedProviderPid=DWORD(pid);discoveryProducerPorts=true;g_workerDataRoot=data;
    const auto fault=chainEnv(L"YMCC_CSL_CHAIN_CLI_EXIT");check(fault.empty()||fault==L"0"||fault==L"1","CHAIN exact exit fault");
    nativeObserveResourceBaseline();const auto before=handles();
    si::Handle stage(OpenEventW(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,(L"Local\\YMCC.LibraryFacts.Stage."+pidText).c_str()));check(stage.value!=nullptr,"CHAIN stage event");
    unsigned signals=0;selftestMoveFault=[&](const std::wstring&,const std::wstring& to)->DWORD{if(to.ends_with(L"transaction.pending.json")){check(SetEvent(stage.value)!=FALSE,"CHAIN real stage signal");++signals;}return ERROR_SUCCESS;};
    std::ostringstream out,err;auto* oldOut=std::cout.rdbuf(out.rdbuf());auto* oldErr=std::cerr.rdbuf(err.rdbuf());
    int code=77;std::string error;
    try{if(fault!=L"1")code=workerMainNotUsedBySelftest(argc,argv);}catch(const std::exception& e){code=2;error=e.what();}catch(...){code=2;error="unknown worker exception";}
    std::cout.rdbuf(oldOut);std::cerr.rdbuf(oldErr);selftestMoveFault={};closeHandle(stage);
    json args=json::array();for(int i=0;i<argc;++i)args.push_back(toUtf8(argv[i]));
    ExclusiveJsonWrite(root/toWide("cli-"+std::to_string(GetCurrentProcessId())+".json"),{
        {"pid",GetCurrentProcessId()},{"providerPid",DWORD(pid)},{"code",code},{"args",args},{"actualArgs",args},{"mode",commit?"commit":"plan"},
        {"productWorkerEntered",fault!=L"1"},{"injectedExit",fault==L"1"},{"stageSignals",signals},{"stdout",out.str()},{"stderr",err.str()},{"error",error},
        {"externalProcessAttempts",identityCallerProcesses},{"networkCalls",identityCallerNetwork},
        {"metadata",{{"originalWorkerCli",true},{"root",toUtf8(root.wstring())},{"inertDiscoveryPorts",true}}},
        {"ports",{{"priorityWriteAttempts",discoveryProducerPriorityWrites},{"automaticDriveDiscovery",discoveryProducerDriveDiscovery},{"allowedOwnedProcessReads",factsIpcAllowedProcessReads}}},
        {"requestResources",resourceReceipt()},{"handleCountBefore",before},{"handleCountAfter",handles()},{"allKernelResourcesReturnedProven",false}});
    std::cout << out.str();std::cerr << err.str();
    return code; // Injected 77 returns normally; no termination.
}
static json chainReadback(const std::string& name,const fs::path& root,const json& host,const Provider& state){
    const auto tx=root/L"transaction",data=tx/L"data",target=tx/L"Steam"/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf",local=target.parent_path()/L"localconfig.vdf";
    const auto seed=ExclusiveJsonRead(root/L"seed.json");
    const auto before=readBinaryFile(root/L"shortcuts.before.bin"),localBefore=readBinaryFile(root/L"localconfig.before.bin"),after=readBinaryFile(target),localAfter=readBinaryFile(local);
    bool passed=sha256(before)==seed.at("shortcutsBeforeSha256").get<std::string>()&&sha256(localBefore)==seed.at("localBeforeSha256").get<std::string>()&&
        sha256(readBinaryFile(tx/L"Game"/L"Game.exe"))==seed.at("gameMarkerSha256").get<std::string>()&&sha256(readBinaryFile(tx/L"Steam"/L"steam.exe"))==seed.at("steamMarkerSha256").get<std::string>()&&!hasStages(tx)&&pendingJournals(data).empty();
    json calls=json::array(),complete=json::array(),backups=json::array();unsigned commits=0,success=0,injected=0,signals=0;
    for(const auto& entry:fs::directory_iterator(root)){
        const auto leaf=entry.path().filename().wstring();if(!leaf.starts_with(L"cli-")||!leaf.ends_with(L".json"))continue;
        chainPath(entry.path(),root);const auto c=ExclusiveJsonRead(entry.path());const auto& args=c.at("args");
        passed=passed&&leaf==toWide("cli-"+std::to_string(c.at("pid").get<DWORD>())+".json")&&c.at("providerPid")==GetCurrentProcessId()&&
            c.at("externalProcessAttempts")==0&&c.at("networkCalls")==0&&c.at("requestResources").value("returned",false)&&c.at("ports").at("priorityWriteAttempts")==0&&c.at("ports").at("automaticDriveDiscovery")==0&&
            args.is_array()&&args.size()>1&&chainSame(fs::path(toWide(args[0].get<std::string>())),root/L"CustomSteamLibrary"/L"SteamArtworkLab.exe");
        if(c.value("mode",std::string{})=="commit"){++commits;if(c.at("code")==0)++success;}
        if(c.value("injectedExit",false)){++injected;passed=passed&&c.at("code")==77&&!c.value("productWorkerEntered",true)&&c.at("stageSignals")==0;}
        signals+=c.value("stageSignals",0u);calls.push_back(c);
    }
    for(const auto& entry:fs::recursive_directory_iterator(data)){
        check(!isReparsePoint(entry.path()),"CHAIN final data reparse");
        if(entry.path().filename()==L"transaction.complete.json")complete.push_back(toUtf8(entry.path().wstring()));
        if(entry.path().filename()==L"shortcuts.before.vdf"){const bool same=readBinaryFile(entry.path())==before;passed=passed&&same;backups.push_back({{"path",toUtf8(entry.path().wstring())},{"equalsBefore",same}});}
    }
    unsigned expected=name=="chain-nonzero-worker-continues"?2u:1u;
#ifdef YMCC_LIBRARY_TYPED_SHUTDOWN
    const bool typed=ChainTyped::known(name),typedSuccess=name=="chain-typed-shutdown-commit"
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
||name=="chain-observer-shutdown-commit"
#endif
;
    if(typed)expected=typedSuccess?1u:0u;
#else
    const bool typed=false,typedSuccess=false;
#endif
    const auto& workerCalls=host.at("workerCalls");const size_t hostCount=workerCalls.is_array()?workerCalls.size():workerCalls.get<size_t>();
    passed=passed&&calls.size()==expected&&commits==expected&&injected==(expected==2?1u:0u)&&
        host.value("passed",false)&&host.at("name")==name&&hostCount==calls.size()&&host.at("replies").is_array()&&!host.at("replies").empty()&&host.at("externalProcessAttempts")==0&&host.at("networkCalls")==0;
    const bool refusal=name=="chain-final-unknown-preserves"||name=="chain-final-present-preserves";
    if(typed&&!typedSuccess){
        passed=passed&&success==0&&signals==0&&after==before&&localAfter==localBefore&&complete.empty()&&backups.empty()&&state.targetStarted&&state.targetJoined;
    }else if(refusal){
        passed=passed&&success==0&&signals>=1&&after==before&&localAfter==localBefore&&complete.empty()&&!backups.empty();
        if(name=="chain-final-unknown-preserves"){bool unavailable=false;for(const auto& row:state.observations)if(!row.value("available",true))unavailable=true;passed=passed&&unavailable&&state.rejections>=1&&!state.targetStarted;}
        else passed=passed&&state.targetStarted&&state.targetJoined&&!state.observations.empty()&&state.observations.back().at("presence")==1;
    }else{
        passed=passed&&success==1&&signals==1&&after!=before&&localAfter!=localBefore&&complete.size()==1&&backups.size()==1&&readShortcuts(target,"19627").size()==1&&(!state.targetStarted||typedSuccess)&&state.presenceRequests>=2;
        const auto result=loadJsonDocument(data/L"state"/L"last-steam-add.json");
        passed=passed&&result.value("committed",false)&&result.at("shortcutsAfterSha256")==sha256(after)&&result.at("localConfigBeforeSha256")==sha256(localBefore)&&result.at("localConfigAfterSha256")==sha256(localAfter);
        for(const auto key:{"backup","localConfigBackup","transactionReceipt"}){
            const fs::path file=toWide(result.at(key).get<std::string>());chainPath(file,data);passed=passed&&fs::is_regular_file(file);
            if(std::string(key)=="backup")passed=passed&&readBinaryFile(file)==before;
            if(std::string(key)=="localConfigBackup")passed=passed&&readBinaryFile(file)==localBefore;
            if(std::string(key)=="transactionReceipt")passed=passed&&loadJsonDocument(file).at("targetAfterSha256")==sha256(after);
        }
    }
    return {{"passed",passed},{"cliReceipts",calls},{"expectedWorkerCalls",expected},{"commitCalls",commits},{"successfulCommitCalls",success},{"injectedExitCalls",injected},{"stageSignals",signals},
        {"completeTransactions",complete},{"shortcutBackups",backups},{"shortcutsPreserved",after==before},{"localPreserved",localAfter==localBefore},{"stagesCleaned",!hasStages(tx)},
        {"shortcutsBeforeSha256",sha256(before)},{"shortcutsAfterSha256",sha256(after)},{"localBeforeSha256",sha256(localBefore)},{"localAfterSha256",sha256(localAfter)},{"independentDiskReadback",true}};
}
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
static fs::path resourceTraceRoot;
static unsigned resourceTraceSerial=0;
static void resourceTrace(const char* phase){
    check(!resourceTraceRoot.empty(),"Owned resource trace root");
    ExclusiveJsonWrite(resourceTraceRoot/toWide("resource-stage-"+std::to_string(++resourceTraceSerial)+".json"),
        {{"phase",phase},{"activeStage",ObserverCensus::activeStage},{"censusStages",ObserverCensus::probe.stages},{"observerResources",ObserverResources::receipt()}});
}
#else
static void resourceTrace(const char*){}
#endif
static int serveHostDiagnosticBody(const std::string& name,const fs::path& output){
    check(chainKnown(name),"CHAIN exact provider case");const auto root=fs::path(modulePath()).parent_path();chainPath(output,root);check(chainSame(output,root/L"provider.json"),"CHAIN provider receipt");
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
    resourceTraceRoot=root;
    ObserverCensus::sink=[&](const char* label,const ObserverCensus::Snapshot& value){
        const auto file=root/toWide(std::string("census-")+label+".json");const auto document=value.json();
        const auto bytes=document.dump(2).size();check(bytes<=maxJsonBytes,"One self census exceeds original JSON limit");
        ExclusiveJsonWrite(file,document);
        return json{{"id",label},{"path",toUtf8(file.wstring())},{"bytes",bytes},{"sha256ComputedAfterRunByConsumer",true},
            {"processHandleCountBeforeQuery",value.countBefore},{"processHandleCountAfterQuery",value.countAfter},{"tableHandleCount",value.rows.size()}};
    };
    resourceTrace("provider-entered");
#endif
    auto desktop=std::make_unique<OwnedDesktop>();Provider state;state.name=name;state.root=root;state.desktop=desktop->full();provider=&state;
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
    ChainObserver::Lifetime observerLifetime;
    ChainObserver::onCollect=[&](std::uint32_t mask,const si::Facts& facts,const json& detail){
        ++state.requests;if(mask&si::Presence)++state.presenceRequests;
        json row=detail;row["mask"]=mask;row["available"]=facts.available;row["presence"]=facts.presence;row["pid"]=facts.processPid;row["birth"]=std::to_string(facts.processBirth);row["ownedCensusOnly"]=true;state.observations.push_back(std::move(row));
    };
#endif
    const auto window=makeWindow();state.baseline=handles();
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
    ObserverResources::baseline();
    const auto censusBeforeSetup=ObserverCensus::take("censusBeforeSetup");
    resourceTrace("after-censusBeforeSetup");
#endif
    state.staged.value=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\YMCC.LibraryFacts.Stage."+std::to_wstring(GetCurrentProcessId())).c_str());check(state.staged.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"CHAIN fresh stage event");
    state.stop.value=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\YMCC.LibraryFacts.Stop."+std::to_wstring(GetCurrentProcessId())).c_str());check(state.stop.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"CHAIN fresh stop event");
    si::Handle actionReturned;std::wstring actionReturnedName;bool actionReturnedCreated=false,actionReturnedClosed=false;
#ifdef YMCC_LIBRARY_TYPED_SHUTDOWN
    const bool ack=ChainTyped::ackMode(name);
    if(ack){
        nativeObserveResourceBaseline();
        actionReturnedName=L"Local\\YMCC.LibraryFacts.ActionReturned."+std::to_wstring(GetCurrentProcessId());
        actionReturned.value=nativeObserveCreateEvent(nullptr,TRUE,FALSE,actionReturnedName.c_str());
        check(actionReturned.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"Ack new manual reset unsignalled Returned event");actionReturnedCreated=true;
    }
    if(ChainTyped::known(name)){
        state.startTarget();si::Peer actual;check(actual.load(state.target.pid),"Typed seed actual target identity");
        check(si::pathKey(actual.path)==si::pathKey((root/L"SteamOwned"/L"steam.exe").wstring()),"Typed seed image");
        ChainTyped::bind(name,window,root/L"SteamOwned",state.target.pid,actual.birth,[&]{state.finishTarget();},actionReturned.value,actionReturnedName);
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
        observerLifetime.start(root/L"SteamOwned",state.target.pid,actual.birth);
#endif
    }
#endif
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
    const auto censusObserverLive=ObserverCensus::take("censusObserverLive");
    resourceTrace("after-censusObserverLive");
#endif
    const auto seed=chainSeed(root);
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
    const auto censusAfterSeed=ObserverCensus::take("censusAfterSeed");
    resourceTrace("after-censusAfterSeed");
#endif
#ifdef YMCC_LIBRARY_TYPED_SHUTDOWN
    if(!ack)nativeObserveResourceBaseline();
#else
    nativeObserveResourceBaseline();
#endif
    OwnedChild child;
    child.start(root/L"CustomSteamLibrary"/L"CustomSteamLibrary.exe",{L"--host-worker-chain-client",toWide(name),(root/L"host.json").wstring()},desktop->full());
#ifdef YMCC_LIBRARY_TYPED_SHUTDOWN
    if(ChainTyped::enabled)ChainTyped::borrowActualHost(child.process,child.pid);
#endif
    check(MessagePumpUntil(child,10000),"CHAIN Host bounded completion");child.join(1000);const auto exit=child.exitCode;
#ifdef YMCC_LIBRARY_TYPED_SHUTDOWN
    if(ChainTyped::enabled)ChainTyped::finishHostObservation();
#endif
    child.close();
#ifdef YMCC_LIBRARY_TYPED_SHUTDOWN
    if(ChainTyped::enabled)ChainTyped::targetAliveAtHostEnd=state.target.process&&WaitForSingleObject(state.target.process,0)==WAIT_TIMEOUT;
#endif
    state.finishTarget();
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
    ChainObserver::waitRetired();observerLifetime.finish();
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
    const auto censusObserverStopped=ObserverCensus::take("censusObserverStopped");
    resourceTrace("after-censusObserverStopped");
#endif
#endif
    const auto host=ExclusiveJsonRead(root/L"host.json"),readback=chainReadback(name,root,host,state);
    if(actionReturned.value){check(nativeObserveClose(actionReturned.value)!=FALSE,"Ack Returned event owner close");actionReturned.value=nullptr;actionReturnedClosed=true;}
    resourceTrace("before-owned-final-close");
    closeHandle(state.staged);closeHandle(state.stop);const auto after=handles();resourceTrace("before-window-close");
    removeWindow(window);provider=nullptr;resourceTrace("before-desktop-close");desktop.reset();resourceTrace("after-desktop-close");
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
    const auto censusAfterUi=ObserverCensus::take("censusAfterUi");
    resourceTrace("after-censusAfterUi");
    wchar_t retainedText[4]{};const auto retainedSize=GetEnvironmentVariableW(L"YMCC_LIBRARY_OBSERVER_RETAIN_EVENT",retainedText,4);
    check(!retainedSize||(retainedSize==1&&retainedText[0]==L'1'),"Exact retained resource control flag");
    HANDLE controlEvent=nullptr;
    if(retainedSize){controlEvent=ObserverResources::createEventW(nullptr,TRUE,FALSE,nullptr);check(controlEvent!=nullptr,"Retained resource control acquire");}
    const auto resourceAtDecision=ObserverResources::receipt();
    const bool observerResourcesReturned=resourceAtDecision.value("allObserverTrackedResourcesReturned",false);
    const auto censusAtDecision=ObserverCensus::take("censusAtDecision");
    resourceTrace("after-censusAtDecision");
    bool controlEventReturned=false;
    if(controlEvent){check(ObserverResources::closeHandle(controlEvent)!=FALSE,"Control event actual cleanup");controlEvent=nullptr;controlEventReturned=true;}
    const auto resourceAfterControlCleanup=ObserverResources::receipt();
    const auto censusAfterControlCleanup=ObserverCensus::take("censusAfterControlCleanup");
    resourceTrace("after-censusAfterControlCleanup");
    const json observerResourceAttribution={{"resourceAtDecision",resourceAtDecision},{"resourceAfterControlCleanup",resourceAfterControlCleanup},
        {"controlRetainedAtDecision",retainedSize!=0},{"controlEventReturned",controlEventReturned},{"observedResourceDecisionPassed",observerResourcesReturned},
        {"censusBeforeSetup",toUtf8((root/L"census-censusBeforeSetup.json").wstring())},{"censusObserverLive",toUtf8((root/L"census-censusObserverLive.json").wstring())},{"censusAfterSeed",toUtf8((root/L"census-censusAfterSeed.json").wstring())},
        {"censusObserverStopped",toUtf8((root/L"census-censusObserverStopped.json").wstring())},{"censusAfterUi",toUtf8((root/L"census-censusAfterUi.json").wstring())},{"censusAtDecision",toUtf8((root/L"census-censusAtDecision.json").wstring())},{"censusAfterControlCleanup",toUtf8((root/L"census-censusAfterControlCleanup.json").wstring())},
        {"setupDifference",ObserverCensus::Probe::difference(censusBeforeSetup,censusObserverLive)},
        {"seedDifference",ObserverCensus::Probe::difference(censusObserverLive,censusAfterSeed)},
        {"originalWindowDifference",ObserverCensus::Probe::difference(censusBeforeSetup,censusObserverStopped)},
        {"afterUiDifference",ObserverCensus::Probe::difference(censusBeforeSetup,censusAfterUi)},
        {"decisionDifference",ObserverCensus::Probe::difference(censusAfterUi,censusAtDecision)},
        {"controlCleanupDifference",ObserverCensus::Probe::difference(censusAfterUi,censusAfterControlCleanup)},
        {"selfOnly",true},{"handleSlotContinuityIsNotKernelObjectIdentity",true},{"allKernelOrSdkReturnedProven",false}};
#else
    const bool observerResourcesReturned=true;const json observerResourceAttribution=nullptr;
#endif
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
    const auto attributionFile=root/L"observer-resource-attribution.json";
    ExclusiveJsonWrite(attributionFile,observerResourceAttribution);
    const json attributionReference={{"path",toUtf8(attributionFile.wstring())},{"bytes",observerResourceAttribution.dump(2).size()},
        {"sha256ComputedAfterRunByConsumer",true},{"observedResourceDecisionPassed",observerResourcesReturned},{"controlRetainedAtDecision",retainedSize!=0}};
#else
    const json attributionReference=nullptr;
#endif
    bool typedPassed=true;json typedReceipt=nullptr;
#ifdef YMCC_LIBRARY_TYPED_SHUTDOWN
    if(ChainTyped::enabled){
        typedReceipt=ChainTyped::receipt();
        typedReceipt["returnedEventName"]=toUtf8(actionReturnedName);typedReceipt["returnedEventCreated"]=actionReturnedCreated;typedReceipt["returnedEventClosed"]=actionReturnedClosed;
        typedPassed=typedReceipt.value("passed",false)&&(!ack||(actionReturnedCreated&&actionReturnedClosed));
    }
#endif
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
    const auto observerReceipt=ChainObserver::receipt();typedPassed=typedPassed&&observerReceipt.value("passed",false);
#else
    const json observerReceipt=nullptr;
#endif
    const bool passed=observerResourcesReturned&&typedPassed&&exit==0&&readback.value("passed",false)&&nativeObservedResourcesReturned()&&state.callbackErrors==0&&state.presenceRequests>=2&&
        processStarts==processJoins&&processCloseFailures==0&&exceptionalJoins==0&&cleanupFailures==0&&desktopStarts==1&&desktopCloses==1;
    ExclusiveJsonWrite(output,{{"passed",passed},{"name",name},{"providerPid",GetCurrentProcessId()},{"hostPid",child.pid},{"childExitCode",exit},{"host",host},{"writer",host},{"seed",seed},{"readback",readback},
        {"requests",state.requests},{"presenceRequests",state.presenceRequests},{"serveRejections",state.rejections},{"callbackErrors",state.callbackErrors},{"observations",state.observations},
        {"observerChain",observerReceipt},{"observerResourceAttribution",attributionReference},{"typedShutdown",typedReceipt},{"targetStarted",state.targetStarted},{"targetJoined",state.targetJoined},{"hostJoined",true},{"actualPublicServe",true},{"actualProcessInventoryAncestry",true},{"ownedPresenceCensusOnly",true},
        {"requestResources",resourceReceipt()},{"handleCountBefore",state.baseline},{"handleCountAfter",after},{"allKernelResourcesReturnedProven",false},
        {"processStarts",processStarts},{"processJoins",processJoins},{"processCloseFailures",processCloseFailures},{"exceptionalJoins",exceptionalJoins},{"cleanupFailures",cleanupFailures},
        {"desktopStarts",desktopStarts},{"desktopCloses",desktopCloses},{"privateDesktop",true},{"desktopSwitchedVisible",false},{"registryReadsDenied",factsIpcRegistryDenials}});
    return passed?0:1;
}
static int serveHost(const std::string& name,const fs::path& output){
#ifndef YMCC_LIBRARY_OBSERVER_RESOURCES
 return serveHostDiagnosticBody(name,output);
#else
 try{return serveHostDiagnosticBody(name,output);}
 catch(const std::exception& error){
    const auto root=fs::path(modulePath()).parent_path();chainPath(output,root);check(chainSame(output,root/L"provider.json"),"Diagnostic owned receipt");
    ExclusiveJsonWrite(root/L"provider-resource-error.json",{{"error",error.what()},{"activeStage",ObserverCensus::activeStage}});
    ExclusiveJsonWrite(root/L"provider-resource-failure.json",{{"error",error.what()},{"activeStage",ObserverCensus::activeStage},
        {"completedCensusStages",ObserverCensus::probe.stages},{"observerResources",ObserverResources::receipt()},
        {"requestResources",resourceReceipt()},{"observerStopped",ChainObserver::observerStopped},{"registryDeleted",ChainObserver::registryDeleted}});
    throw;
 }
#endif
}
static int chainEntry(int argc,wchar_t** argv){
    if(argc!=5||std::wstring(argv[2])!=L"--host-worker-chain"||!chainKnown(toUtf8(argv[3])))return 2;
    const auto root=fs::absolute(argv[1]);
    const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
    check(within(root,build)&&!fs::exists(root),"IPC controller new owned Build root");
    const fs::path hostExe=argv[4];check(hostExe.is_absolute(),"CHAIN absolute Host fixture EXE");
    auto evidence=fs::absolute(fs::path(__FILE__)).parent_path();
    while(!evidence.empty()&&evidence.filename()!=L"Evidence"&&evidence!=evidence.parent_path())evidence=evidence.parent_path();
    check(evidence.filename()==L"Evidence","CHAIN source Evidence ancestor");
    const auto evidenceParent=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L"..").lexically_normal();
    check(within(evidenceParent,evidence),"CHAIN private source parent in Evidence");
    chainPath(hostExe,evidenceParent);check(fs::is_regular_file(hostExe),"CHAIN Host fixture exists");
    check(!chainSame(hostExe,modulePath()),"CHAIN distinct Host fixture");
    fs::create_directories(root/L"CustomSteamLibrary");fs::create_directories(root/L"SteamOwned");
    for(const auto& file:{root/L"YeManCC.exe",root/L"CustomSteamLibrary"/L"SteamArtworkLab.exe",root/L"CustomSteamLibrary"/L"NotWorker.exe",root/L"SteamOwned"/L"steam.exe"})check(fs::copy_file(modulePath(),file,fs::copy_options::none),"IPC inert PE copy");
    check(fs::copy_file(hostExe,root/L"CustomSteamLibrary"/L"CustomSteamLibrary.exe",fs::copy_options::none),"CHAIN Host PE copy");
    const auto selfHash=sha256(readBinaryFile(modulePath())),hostHash=sha256(readBinaryFile(hostExe));json copies=json::array();
    for(const auto& file:{root/L"YeManCC.exe",root/L"CustomSteamLibrary"/L"SteamArtworkLab.exe",root/L"CustomSteamLibrary"/L"NotWorker.exe",root/L"SteamOwned"/L"steam.exe"}){
        check(sha256(readBinaryFile(file))==selfHash,"CHAIN selfcopy bytes");copies.push_back({{"path",toUtf8(file.wstring())},{"sha256",selfHash}});
    }
    check(sha256(readBinaryFile(root/L"CustomSteamLibrary"/L"CustomSteamLibrary.exe"))==hostHash,"CHAIN Host copy bytes");
    copies.push_back({{"path",toUtf8((root/L"CustomSteamLibrary"/L"CustomSteamLibrary.exe").wstring())},{"sha256",hostHash}});ExclusiveJsonWrite(root/L"pe-copies.json",copies);
    OwnedChild child;const auto name=toUtf8(argv[3]);child.start(root/L"YeManCC.exe",{L"--facts-ipc-provider",argv[3],(root/L"provider.json").wstring()});
    child.join(10000);const auto exit=child.exitCode;child.close();
    ExclusiveJsonWrite(root/L"provider-process.json",{{"pid",child.pid},{"exitCode",exit},{"joined",child.joined},{"processClosed",child.process==nullptr},{"threadClosed",child.thread==nullptr},{"originalControllerResourceStarts",processStarts},{"originalControllerResourceJoins",processJoins}});
    const auto receipt=ExclusiveJsonRead(root/L"provider.json");
    bool retained=true;for(const auto& copy:copies)retained=retained&&sha256(readBinaryFile(fs::path(toWide(copy.at("path").get<std::string>()))))==copy.at("sha256").get<std::string>();
    const bool passed=exit==0&&receipt.value("passed",false)&&receipt.at("readback").value("passed",false)&&retained&&processStarts==processJoins&&processCloseFailures==0&&exceptionalJoins==0&&cleanupFailures==0;
    const json raw={{"allPassed",passed},{"caseCount",1},{"failedCount",passed?0:1},{"cases",json::array({{{"name",name},{"passed",passed},{"error",passed?"":"Actual Host/Worker/IPC/file/resource assertion failed"}}})},
        {"fixtures",toUtf8(root.wstring())},{"identityProcessCalls",receipt.at("host").at("externalProcessAttempts")},{"identityNetworkCalls",receipt.at("host").at("networkCalls")},
        {"realSteamFilesModified",false},{"actualPublicFactsIpc",true},{"actualWorkerFileTransaction",true},{"wholeMachineAbsenceProven",false},{"peCopies",copies},{"peCopiesRetained",retained},{"provider",receipt}};
    ExclusiveJsonWrite(root/L"summary.json",raw);std::cout<<raw.dump(2)<<"\n";return passed?0:1;
}
