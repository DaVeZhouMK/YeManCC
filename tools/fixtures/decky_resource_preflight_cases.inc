inline Json executeResourceWorker(const std::filesystem::path& output,const std::string& scenario){
    const bool ioFault=scenario=="worker-io-send-denied"||scenario=="worker-io-send-invalid"||scenario=="worker-io-wait-invalid";
    const bool ioWait=scenario=="worker-io-wait-invalid";
    const unsigned ioMode=scenario=="worker-io-send-denied"?1u:scenario=="worker-io-send-invalid"?2u:ioWait?3u:0u;
    const bool observation=scenario=="worker-observation-normal"||scenario=="worker-observation-protected";
    const bool diagnostic=scenario.starts_with("worker-cleanup-");
    const bool diagnosticNormal=scenario=="worker-cleanup-normal",diagnosticReady=scenario=="worker-cleanup-diagnostic-ready";
    const bool diagnosticMemory=scenario=="worker-cleanup-diagnostic-memory"||scenario=="worker-cleanup-diagnostic-memory-off",diagnosticError=scenario=="worker-cleanup-error-postwrite",diagnosticPostwrite=scenario=="worker-cleanup-diagnostic-postwrite";
    need(!diagnostic||diagnosticNormal||diagnosticReady||diagnosticMemory||diagnosticError||diagnosticPostwrite,"resource-cleanup-known-parameter");
    const bool preparation=scenario.starts_with("worker-preparation-");
    const bool initialPreparation=scenario=="worker-preparation-initial",requestPreparation=scenario=="worker-preparation-request"||scenario=="worker-preparation-request-admission";
    const bool serializePreparation=scenario=="worker-preparation-serialize",payloadPreparation=scenario=="worker-preparation-payload"||scenario=="worker-preparation-payload-off",writePreparation=scenario=="worker-preparation-write";
    const bool construction=scenario.starts_with("worker-construction-")||preparation;
    const bool constructionOwner=scenario=="worker-construction-owner"||scenario=="worker-construction-owner-off";
    const bool constructionReserve=scenario=="worker-construction-reserve",constructionReader=scenario=="worker-construction-reader"||scenario=="worker-construction-reader-admission";
    const bool postwriteAllocation=scenario=="worker-construction-postwrite";
    need(!construction||constructionOwner||constructionReserve||constructionReader||postwriteAllocation||initialPreparation||requestPreparation||serializePreparation||payloadPreparation||writePreparation,"resource-construction-known-parameter");
    const unsigned constructionStage=preparation?(initialPreparation?22u:requestPreparation?23u:serializePreparation?24u:payloadPreparation?25u:26u):constructionOwner?18u:constructionReserve?19u:constructionReader?20u:21u;
    const bool off=scenario=="worker-start-off"||scenario=="worker-construction-owner-off"||scenario=="worker-preparation-payload-off"||scenario=="worker-cleanup-diagnostic-memory-off",admission=scenario=="worker-start-admission"||scenario=="worker-construction-reader-admission"||scenario=="worker-preparation-request-admission";
    const bool deniedError=scenario=="worker-start-denied",postwrite=scenario=="worker-attach-memory",forged=scenario=="worker-forged-memory";
    const bool exhaust=scenario=="worker-reply-stall-exhaustion";
    const bool deadline=scenario=="worker-forged-deadline",stall=scenario=="worker-reply-stall"||exhaust,noFault=forged||deadline||stall||construction||diagnostic||observation||ioFault;
    need(scenario=="worker-start-retry"||off||admission||deniedError||postwrite||noFault,"resource-preflight-known-parameter");
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    const auto calibration=initializeSharedCryptoForMeasurement();
    const auto base=output/"resource-worker-fixture",steam=base/"Steam",home=base/"PowerControl"/"decky",metadata=base/"YeManCC"/"decky-package.json";
    const auto put=[](const std::filesystem::path& p,const std::string& value){std::filesystem::create_directories(p.parent_path());std::ofstream file(p,std::ios::binary);file<<value;need(file.good(),"resource-preflight-fixture-write");};
    put(steam/L"steam.exe","inert");put(steam/L".cef-enable-remote-debugging","");
    for(const auto* name:ymcc::deckysetup::managedFiles)put(home/name,"fixture-"+ymcc::deckysetup::pathText(std::filesystem::path(name)));
    const auto official=std::filesystem::path(environment(L"YMCC_RESOURCE_DATA_FILE"));need(official.is_absolute()&&std::filesystem::is_regular_file(official),"resource-preflight-official-data-input");
    std::filesystem::copy_file(official,home/ymcc::deckysetup::managedFiles[0],std::filesystem::copy_options::overwrite_existing);
    Json manifest={{"schemaVersion",1},{"owner","YMCC"},{"origin","official-release"},{"loaderVersion",ymcc::deckyidentity::version},{"loaderSha256",ymcc::deckyidentity::sha256},{"files",Json::array()}};
    for(const auto* name:ymcc::deckysetup::managedFiles)manifest["files"].push_back({{"path",ymcc::deckysetup::pathText(std::filesystem::path(name))},{"bytes",std::filesystem::file_size(home/name)},{"sha256",ymcc::deckyresources::worker::digest((home/name).wstring(),[]{return true;})}});
    put(metadata,manifest.dump());
    probe::budget=0;probe::preparationStage=0;
    const auto warm=ymcc::deckyresources::worker::prepare(steam,home,metadata,[]{return true;},true);
    need(warm.value("ready",false)&&warm.value("resourceWorkerDestroyed",false),"resource-preflight-actual-warm-prepare");
    warmClient(context);ResetEvent(controls.ready.value);const auto baseline=rtfixture::handles();
    probe::apiFailures=0;probe::apiFailureCode=0;probe::writeSuccesses=0;probe::postFailureCreates=0;
    probe::failures=0;probe::failedBytes=0;probe::resourceStarts=0;probe::skipAllocations=0;
    probe::resourceDiagnosticBudget=0;probe::resourceErrorBudget=0;probe::resourceDiagnosticHits=0;probe::resourceErrorHits=0;probe::resourceCleanupCalls=0;
    probe::firstCleanupOwnerHandles=0;probe::firstCleanupOwnerIo=0;probe::firstCleanupReaderHandles=0;probe::firstCleanupReaderIo=0;probe::firstCleanupChildClosed=false;probe::firstCleanupZero=false;
    probe::firstDiagnosticOwnerHandles=0;probe::firstDiagnosticReaderHandles=0;probe::firstErrorOwnerHandles=0;probe::firstErrorReaderHandles=0;
    probe::resourceIoMode=ioMode;probe::resourceIoArmed=ioFault;probe::resourceIoFailures=0;probe::resourceIoActualError=0;probe::resourceIoSetupError=0;probe::resourceIoCopyReleased=true;
    probe::resourceProtectRequested=false;probe::resourceObservationCopied=false;probe::resourceObservationValid=false;probe::resourceObservationFixtureReleased=true;probe::resourceObservationTrackedValid=false;
    probe::resourceObservationDuplicates=0;probe::resourceObservationProtected=0;probe::resourceObservationCloseCalls=0;probe::resourceObservationCloseFailures=0;probe::resourceObservationCloseError=0;probe::resourceObservationSetupError=0;probe::resourceObservationPid=0;probe::resourceObservationBirth=0;probe::resourceObservationTracked=nullptr;
    std::atomic<unsigned> reads{0};std::atomic<bool> firstReturned{false},denied{false};
    Json attempts=Json::array(),pageSteps=Json::array(); // Runtime owner writes; read after shutdown.
    Json firstResult=Json::object();bool firstEscaped=false;unsigned firstStarts=0,firstCleanupCalls=0;Harness* active=nullptr;
    const auto preflight=[&](){
        const auto number=++reads;
        const auto began=exhaust?GetTickCount64():0;
        if(number==1){
            probe::resourceProtectRequested=scenario=="worker-observation-protected";
            probe::budget=diagnostic?(diagnosticMemory||diagnosticError||diagnosticPostwrite?1u:0u):construction?1u:noFault?0u:1u;
            probe::preparationStage=diagnostic?(diagnosticMemory?23u:diagnosticError||diagnosticPostwrite?21u:0u):construction?constructionStage:noFault?0u:postwrite?14u:9u;
            if(diagnostic){probe::resourceDiagnosticBudget=diagnosticReady||diagnosticMemory||diagnosticPostwrite?1u:0u;probe::resourceErrorBudget=diagnosticError?1u:0u;}
            if(noFault&&!construction&&!diagnostic&&!observation&&!ioFault)SetEnvironmentVariableW(L"YMCC_RESOURCE_RESERVED_REPLY",deadline?L"deadline":stall?L"stall":L"1");
            if(ioWait)SetEnvironmentVariableW(L"YMCC_RESOURCE_RESERVED_REPLY",L"stall");
            probe::injectedCreationError=deniedError?ERROR_ACCESS_DENIED:ERROR_NOT_ENOUGH_MEMORY;
            if(postwrite)probe::handoffArmed=true;else if(!noFault)probe::prepareArmed=true;
        }
        if(exhaust&&number!=1&&pageSteps.empty())need(SetEnvironmentVariableW(L"YMCC_RESOURCE_RESERVED_REPLY",L"stall")!=FALSE,"resource-exhaustion-arm-next-real-stall");
        const auto current=[&]{return active&&active->runtime.snapshot().enabled&&!denied.load();};
        Json resource;
        try{resource=ymcc::deckyresources::worker::prepare(steam,home,metadata,current,false);}
        catch(const std::bad_alloc&){
            probe::preparationStage=0;probe::prepareArmed=false;probe::handoffArmed=false;
            if(number==1){firstEscaped=true;firstStarts=probe::resourceStarts.load();firstCleanupCalls=probe::resourceCleanupCalls.load();firstReturned=true;}
            throw; // Preserve the original Runtime preflight exception consumer.
        }
        if(noFault)SetEnvironmentVariableW(L"YMCC_RESOURCE_RESERVED_REPLY",nullptr);
        probe::trackCreations=false;probe::prepareArmed=false;probe::handoffArmed=false;probe::preparationStage=0;
        if(exhaust)attempts.push_back({{"began",began},{"returned",GetTickCount64()},{"resource",resource}});
        if(number==1){firstResult=resource;firstStarts=probe::resourceStarts.load();firstCleanupCalls=probe::resourceCleanupCalls.load();firstReturned=true;}
        return resource.value("ready",false)?std::string{}:resource.value("reason",std::string("resource-preflight-unknown"));
    };
    const auto launchAdmission=[&]{return ymcc::deckysidebar::LaunchAdmission{!denied.load(),denied?"fixture-admission-closed":"",false};};
    using Health=ymcc::deckysidebar::ConnectionHealth;
    using Action=ymcc::deckysidebar::ConnectionRecoveryWindow::Action;
    std::atomic<unsigned> pageActions{0};
    const std::function<Health()> health=stall?std::function<Health()>([&]{
        const bool ready=active&&active->runtime.snapshot().pid!=0;
        return Health{true,ready,ready?"fixture-loader-ready":"fixture-no-mirror"};
    }):std::function<Health()>{};
    Harness harness(output/"runtime",context,false,launchAdmission,{},preflight,{},{},health,[&](Action action){
        ++pageActions;
        if(exhaust){
            pageSteps.push_back({{"at",GetTickCount64()},{"reads",reads.load()},{"rebuild",action==Action::rebuild}});
            if(action==Action::rebuild)return Json{{"ok",true},{"mutated",true},{"reason","fixture-rebuild-receipt-no-real-steam"}};
        }
        return Json{{"ok",false},{"mutated",false},{"reason","fixture-inert-page-recovery"}};
    });active=&harness;
    need(harness.runtime.setEnabled(true),"resource-preflight-enable");
    need(waitFor([&]{return firstReturned.load();}),"resource-preflight-first-call-returned");
    const auto firstFailure=GetTickCount64();bool prevented=true,recovered=false;
    if(off||admission){
        if(off)prevented=harness.runtime.setEnabled(false);else denied=true;
        Sleep(1300);prevented=prevented&&reads==1&&harness.plans==0;
        if(admission){denied=false;harness.runtime.requestLaunchAdmissionRefresh();}
    }
    if(exhaust)recovered=waitFor([&]{return harness.globalConnected.load()!=0;},36000);
    else if(!off&&!deniedError&&!postwrite&&!ioFault&&!postwriteAllocation&&!writePreparation&&!diagnosticError&&!diagnosticPostwrite&&!forged&&!deadline)recovered=waitFor([&]{return harness.runtime.snapshot().pid!=0;},4500);
    else if(!off)Sleep(forged||deadline?2000:1300);
    const auto finalState=harness.runtime.snapshot();const auto elapsed=GetTickCount64()-firstFailure;
    harness.runtime.shutdown();const auto returned=harness.client->snapshot();
    const auto after=rtfixture::handles();
    const bool observationReleased=probe::releaseResourceObservation();const auto afterFixtureRelease=rtfixture::handles();
    const auto logged=ymcc::deckydiag::record("prepare",firstResult);
    const bool cleanupKnown=firstCleanupCalls>0&&probe::firstCleanupZero&&probe::firstCleanupOwnerHandles==0&&probe::firstCleanupOwnerIo==0&&probe::firstCleanupReaderHandles==0&&probe::firstCleanupReaderIo==0&&probe::firstCleanupChildClosed;
    const bool returnedAll=returned.fullyDestroyed&&returned.handles==0&&returned.pendingIo==0&&baseline==after&&context.alive()&&(diagnostic?cleanupKnown:firstEscaped||firstResult.value("resourceWorkerDestroyed",false));
    const auto* ioKey=ioWait?"resourceWorkerWaitError":"resourceWorkerSendError";
    const DWORD ioExpected=ioMode==1?ERROR_ACCESS_DENIED:ERROR_INVALID_HANDLE;
    const bool firstTyped=ioFault?!firstEscaped&&firstResult.value("reason",std::string{})==(ioWait?"resource-worker-wait-failed":"resource-worker-send-failed")&&
        firstResult.value(ioKey,0xffffffffu)==ioExpected&&logged.value(ioKey,0xffffffffu)==ioExpected:
        observation?!firstEscaped&&probe::resourceObservationValid&&!probe::resourceObservationCopied&&probe::resourceObservationDuplicates==0&&firstResult.value("ready",false)&&firstResult.value("resourceWorkerDestroyed",false):diagnostic?cleanupKnown&&(diagnosticError?firstEscaped:!firstEscaped&&
        (diagnosticNormal||diagnosticReady?firstResult.value("ready",false):firstResult.value("reason",std::string{})==(diagnosticMemory?"resource-worker-prewrite-memory-unavailable":"resource-worker-failed"))):construction?!firstEscaped&&firstResult.value("reason",std::string{})==(postwriteAllocation?"resource-worker-failed":writePreparation?"resource-worker-send-failed":"resource-worker-prewrite-memory-unavailable")&&
        firstStarts==(postwriteAllocation||(preparation&&!initialPreparation)?1u:0u)&&
        (!postwriteAllocation&&!writePreparation?firstResult.value("resourceWorkerStage",std::string{})==(requestPreparation?"prepare":serializePreparation||payloadPreparation?"send":"construct"):true):stall?firstResult.value("reason",std::string{})=="resource-worker-deadline"&&
        firstResult.value("resourceWorkerStage",std::string{})=="read"&&!firstResult.value("resourceWorkerReplyReceived",true)&&
        firstResult.value("resourceWorkerElapsedMs",0ull)>=2500&&firstResult.value("resourceWorkerProgressCount",0u)>0:
        forged||deadline?firstResult.value("resourceWorkerReplyReceived",false)&&firstResult.value("reason",std::string{})=="resource-worker-reply-invalid":postwrite?firstResult.value("resourceWorkerReadError",0u)==ERROR_NOT_ENOUGH_MEMORY:
        firstResult.value("resourceWorkerStartError",0u)==static_cast<DWORD>(deniedError?ERROR_ACCESS_DENIED:ERROR_NOT_ENOUGH_MEMORY);
    const bool behavior=ioFault?reads==1&&harness.plans==0:observation?reads==1&&harness.plans==1&&recovered:diagnostic?(diagnosticError||diagnosticPostwrite||off?reads==1&&harness.plans==0:
        diagnosticMemory?reads==2&&harness.plans==1&&recovered:reads==1&&harness.plans==1&&recovered):forged||deadline||postwrite||postwriteAllocation||writePreparation||deniedError?reads==1&&harness.plans==0:
        off?prevented&&!recovered&&reads==1:exhaust?attempts.size()==reads.load():harness.plans==1&&reads>=2&&reads<=3&&recovered&&prevented;
    Json facts={{"scenario",scenario},{"actualResourcePrepare",true},{"actualRuntimePreflight",true},{"first",firstResult},{"filteredFirst",logged},
        {"reads",reads.load()},{"plans",harness.plans.load()},{"apiFailures",probe::apiFailures.load()},{"apiFailureCode",probe::apiFailureCode.load()},
        {"ioBoundary",ioFault},{"ioMode",ioMode},{"ioFailures",probe::resourceIoFailures.load()},{"ioActualError",probe::resourceIoActualError.load()},{"ioSetupError",probe::resourceIoSetupError.load()},{"ioCopyReleased",probe::resourceIoCopyReleased.load()},
        {"observationBoundary",observation},{"observationCopied",probe::resourceObservationCopied.load()},{"observationValid",probe::resourceObservationValid.load()},{"observationDuplicateCalls",probe::resourceObservationDuplicates.load()},{"protectedCopies",probe::resourceObservationProtected.load()},{"observationCloseCalls",probe::resourceObservationCloseCalls.load()},{"observationCloseFailures",probe::resourceObservationCloseFailures.load()},{"observationCloseError",probe::resourceObservationCloseError.load()},{"observationSetupError",probe::resourceObservationSetupError.load()},{"observationPid",probe::resourceObservationPid.load()},{"observationBirth",std::to_string(probe::resourceObservationBirth.load())},{"trackedCopyIdentityValidBeforeFixtureRelease",probe::resourceObservationTrackedValid.load()},{"fixtureReleasedTrackedCopy",observationReleased},{"afterFixtureReleaseHandles",afterFixtureRelease},
        {"diagnosticBoundary",diagnostic},{"cleanupCallsAtFirstReturn",firstCleanupCalls},{"cleanupKnown",cleanupKnown},{"cleanupOwnerHandles",probe::firstCleanupOwnerHandles.load()},{"cleanupOwnerIo",probe::firstCleanupOwnerIo.load()},{"cleanupReaderHandles",probe::firstCleanupReaderHandles.load()},{"cleanupReaderIo",probe::firstCleanupReaderIo.load()},{"cleanupChildClosed",probe::firstCleanupChildClosed.load()},
        {"diagnosticHits",probe::resourceDiagnosticHits.load()},{"errorResultHits",probe::resourceErrorHits.load()},{"diagnosticOwnerHandlesBeforeFailure",probe::firstDiagnosticOwnerHandles.load()},{"diagnosticReaderHandlesBeforeFailure",probe::firstDiagnosticReaderHandles.load()},{"errorOwnerHandlesBeforeFailure",probe::firstErrorOwnerHandles.load()},{"errorReaderHandlesBeforeFailure",probe::firstErrorReaderHandles.load()},
        {"constructionBoundary",construction},{"constructorExceptionEscaped",firstEscaped},{"actualAllocationFailures",probe::failures.load()},{"failedAllocationBytes",probe::failedBytes.load()},{"resourceStartsAtFirstReturn",firstStarts},
        {"recovered",recovered},{"prevented",prevented},{"elapsedFromFailureMs",elapsed},{"phase",finalState.phase},{"reason",finalState.reason},
        {"pageActions",pageActions.load()},{"globalRecoveryLog",harness.globalRecoveryLog},
        {"beforeHandles",baseline},{"afterHandles",after},{"atExit",state(returned)},{"contextAlive",context.alive()},{"firstTyped",firstTyped}};
    if(exhaust){facts["attempts"]=attempts;facts["pageSteps"]=pageSteps;}
    std::cout<<"RESOURCE_PREFLIGHT_FACTS "<<facts.dump()<<std::endl;
    if(ioFault)need(probe::resourceIoFailures==1&&probe::resourceIoActualError==ioExpected&&probe::resourceIoSetupError==0&&probe::resourceIoCopyReleased,"resource-io-real-api-error-and-exact-fixture-copy-return");
    if(observation)need(observationReleased&&afterFixtureRelease==baseline&&probe::resourceObservationSetupError==0,"resource-observation-fixture-owned-protected-copy-release");
    if(diagnostic)need(probe::failures==(diagnosticNormal?0u:diagnosticReady?1u:2u),"resource-cleanup-labelled-real-allocation-failure-count");
    if(construction)need(probe::failures==1&&probe::failedBytes>0,"resource-construction-exactly-one-real-allocation-failure");
    need(probe::apiFailures==(ioFault?1u:noFault?0u:1u),"resource-preflight-exactly-one-labelled-api-failure");need(returnedAll,"resource-preflight-real-owner-resource-return");
    need(firstTyped&&behavior,"resource-worker-real-preflight-memory-retry-and-postwrite-fence");
    if(exhaust){
        need(pageSteps.size()==1&&pageSteps.front().at("rebuild")==true,"exhausted-resource-retry-allows-one-page-rebuild");
        const auto& page=pageSteps.front();const auto count=page.at("reads").get<size_t>();
        need(count>=2&&count<=5&&count<=attempts.size(),"page-rebuild-follows-finite-resource-window");
        for(size_t i=0;i<count;++i){
            const auto& result=attempts[i].at("resource");
            need(result.value("reason",std::string{})=="resource-worker-deadline"&&result.value("resourceWorkerDestroyed",false)&&
                !result.value("resourceWorkerReplyReceived",true),"every-stalled-worker-returns-before-page-rebuild");
        }
        // The original clock opens after the first return. A real blocked worker
        // can coalesce earlier offsets, but cannot consume +20s before this bound.
        need(attempts[count-1].at("began").get<ULONGLONG>()>=attempts.front().at("returned").get<ULONGLONG>()+20000&&
            page.at("at").get<ULONGLONG>()>=attempts[count-1].at("returned").get<ULONGLONG>(),"page-rebuild-follows-final-resource-attempt-cleanup");
        need(recovered&&harness.plans==1&&attempts.size()==count+1&&
            attempts[count].at("began").get<ULONGLONG>()>=page.at("at").get<ULONGLONG>()&&
            attempts[count].at("resource").value("ready",false)&&attempts[count].at("resource").value("resourceWorkerDestroyed",false),
            "page-rebuild-rearms-validated-resource-check-and-binding");
    }else if(stall)need(pageActions==0,"queued-resource-retry-must-not-rebuild-or-reopen-steam");
    if(ioFault)need(logged.value("resourceWorkerDestroyed",false)&&logged.value("reason",std::string{})==firstResult.value("reason",std::string{}),"resource-io-filter-keeps-original-business-and-cleanup");
    else if(observation)need(firstResult.value("ready",false)&&firstResult.value("resourceWorkerDestroyed",false),"resource-observation-original-business-and-cleanup-facts");
    else if(diagnostic)need(diagnosticNormal?logged.value("resourceWorkerDestroyed",false)&&logged.value("ready",false):diagnosticError?firstEscaped:!firstEscaped,"resource-cleanup-business-outcome-not-diagnostic-authority");
    else if(construction)need(preparation?logged.value("resourceWorkerDestroyed",false)&&logged.value("reason",std::string{})==firstResult.value("reason",std::string{})&&logged.value("resourceWorkerStage",std::string{})==firstResult.value("resourceWorkerStage",std::string{}):!postwriteAllocation?logged.value("resourceWorkerStage",std::string{})=="construct"&&logged.value("resourceWorkerDestroyed",false):logged.value("reason",std::string{})=="resource-worker-failed","resource-construction-filter-preserves-failure-scope");
    else if(forged||deadline)need(logged.value("resourceWorkerReplyReceived",false)&&logged.value("resourceWorkerDestroyed",false),"resource-preflight-forged-reply-keeps-real-framing-and-cleanup");
    else if(stall)need(logged.value("reason",std::string{})=="resource-worker-deadline"&&!logged.value("resourceWorkerReplyReceived",true)&&logged.value("resourceWorkerDestroyed",false),"resource-preflight-actual-deadline-keeps-cleanup-and-retry");
    else if(postwrite)need(logged.value("resourceWorkerReadError",0u)==ERROR_NOT_ENOUGH_MEMORY,"resource-preflight-filter-preserves-postwrite-error");
    else need(logged.value("resourceWorkerStartError",0u)==static_cast<DWORD>(deniedError?ERROR_ACCESS_DENIED:ERROR_NOT_ENOUGH_MEMORY),"resource-preflight-filter-preserves-start-error");
    for(const auto& bad:std::vector<Json>{-1,4294967296ull,"8",nullptr,true}){
        const auto filtered=ymcc::deckydiag::record("prepare",{{"resourceWorkerStartError",bad}});
        need(!filtered.contains("resourceWorkerStartError"),"resource-preflight-start-error-is-typed-dword-only");
    }
    if(ioFault||observation){
        for(const auto* key:{"resourceWorkerSendError","resourceWorkerWaitError"})for(const auto& bad:std::vector<Json>{-1,4294967296ull,"6",nullptr,true}){
            const auto filtered=ymcc::deckydiag::record("prepare",{{key,bad}});
            need(!filtered.contains(key),"resource-io-error-is-typed-dword-only");
        }
        if(observation)need(!logged.contains("resourceWorkerSendError")&&!logged.contains("resourceWorkerWaitError"),"resource-io-success-does-not-mint-error-facts");
    }
    return {{"passed",true},{"scenario",scenario},{"facts",facts},{"sharedCryptoInitialization",calibration},
        {"originalCaseId","native.typed-ready-rebind"},{"actualClientRuntime",true},{"actualKernelExhaustion",false},{"realSteam",false},{"defaultParent",false}};
}
