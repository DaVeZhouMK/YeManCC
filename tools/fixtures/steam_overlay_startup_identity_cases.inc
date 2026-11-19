// Directed boundaries of the original startup overlay consumer. Registry/file,
// queue and Steam transport remain the existing inert ports; publication and
// epoch assignment are the actual Observer's original controlled fact entry.
static int startupOverlaySharedFixture(int argc,char** argv){
    if(argc!=2&&argc!=4)return 2;
    if(std::string(argv[1])!="--startup-overlay-shared")return 2;
    const std::string selection=argc==2?"all":std::string(argv[2])=="--case"?argv[3]:"";
    if(selection!="all"&&selection!="initial"&&selection!="revocation"&&selection!="readback"&&selection!="lifecycle"&&selection!="early-owner-control")return 2;
    using Json=nlohmann::json;
    const auto enabled=[] {
        g_steamSettingsObserver.stop();reset();
        writeBirth=100;writePresence=-2;writeRegistered=writePid=activePid=777;
        sharedConfigForeign=false;writeAccountError=writeRegisteredError=writeRootError=ERROR_SUCCESS;
        registryReads=privateConfigReads=sharedConfigReads=presenceReads=setControlledDiscovery=0;
        beforeSectionPrepareHook={};writeGateHook={};beforeSetPureOwnerHook={};sharedConfigHook={};presenceHook={};writeGateRepeats=1;
        prefs["startupDesired"]={{"steamOverlayOnBoot",true},{"unrelated","KEEP"}};
        disk=ymcc::steamsettings::patch(disk,Json::object(),0);running=true;publishWriteIdentity();
    };
    const auto noPrivateReads=[] {return registryReads==0&&privateConfigReads==0&&presenceReads==0;};
    const auto noClaim=[] {return !prefs["startupDesired"].contains("steamOverlayAppliedBoot");};
    const auto checkNoReplay=[] {
        const auto writes=liveSetCalls;steamOverlayStartupIdentityPublished();flush();
        check(liveSetCalls==writes,"source callback never replays a consumed startup mutation");
    };
    Json boundaries=Json::array();
    if(selection=="early-owner-control"){
        enabled();steamOverlayStartupInitialize();beforeSetPureOwnerHook=[] {++writeBirth;publishWriteIdentity();};flush();
        check(liveSetCalls==0&&setControlledDiscovery==0&&noPrivateReads()&&!g_steamOverlayStartupPending,"claim held but revoked public source stops before controlled Live discovery and never replays");
        std::cout<<"STARTUP_SHARED_RESULT "<<Json{{"ok",true},{"selection",selection},{"checks",passed},{"realSteamOperations",0}}.dump()<<'\n';return 0;
    }
    if(selection=="all"||selection=="initial"){
    enabled();g_steamSettingsObserver.stop();steamOverlayStartupInitialize();
    check(jobs.empty()&&noClaim()&&g_steamOverlayStartupPending,"initial Unknown retains startup intent without a boot claim or private discovery");
    publishWriteIdentity();steamOverlayStartupIdentityPublished();
    check(jobs.size()==1&&liveCalls==0&&noClaim(),"first real public identity wakes existing shared queue before any claim");
    flush();check(liveSetCalls==1&&liveGetCalls==1&&!g_steamOverlayStartupPending&&prefs["startupDesired"]["steamOverlayStartupResult"]["liveConfirmed"]==true,"settled first source completes one SET and independent GET");
    check(noPrivateReads()&&writes==0&&backups==0&&setPureOwnerSupplied,"live startup uses no private PID/account/root/presence discovery and supplies pure owner");checkNoReplay();

    enabled();steamOverlayStartupInitialize();writeBirth=101;publishWriteIdentity();steamOverlayStartupIdentityPublished();
    check(jobs.size()==1,"new birth before queued execution coalesces on original worker");flush();
    check(liveSetCalls==1&&prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==true&&noPrivateReads(),"queued task takes current published identity without old PID-birth authority");

    enabled();steamOverlayStartupInitialize();beforeSectionPrepareHook=[] {++writeBirth;publishWriteIdentity();steamOverlayStartupIdentityPublished();};flush();
    check(liveSetCalls==1&&!g_steamOverlayStartupPending&&noPrivateReads(),"ownership revoked inside original claim lock keeps opportunity for already-signaled new epoch");
    }
    if(selection=="all"||selection=="revocation"){
    const std::vector<std::pair<const char*,std::function<void()>>> changes{
        {"same-pid-new-birth",[]{++writeBirth;}},
        {"different-pid",[]{writePid=writeRegistered=activePid=778;}},
        {"different-account",[]{account="43";}},
        {"account-unreadable",[]{writeAccountError=ERROR_ACCESS_DENIED;}},
        {"registered-pid-unreadable",[]{writeRegisteredError=ERROR_ACCESS_DENIED;}},
        {"registered-pid-mismatch",[]{writeRegistered=778;}},
        {"root-unreadable",[]{writeRootError=ERROR_ACCESS_DENIED;}},
        {"presence-unknown",[]{writePresence=-1;}}
    };
    for(const auto& item:changes){
        enabled();steamOverlayStartupInitialize();beforeSetPureOwnerHook=[mutate=item.second]{mutate();publishWriteIdentity();};flush();
        check(liveSetCalls==0&&setControlledDiscovery==0&&noPrivateReads()&&!g_steamOverlayStartupPending,"claim held but revoked public source stops before controlled Live discovery and never replays");
        const auto before=liveSetCalls;steamOverlayStartupIdentityPublished();flush();check(liveSetCalls==before,"late source notification cannot restore spent boot authority");
        boundaries.push_back(item.first);
    }
    }
    if(selection=="all"||selection=="readback"){
    enabled();steamOverlayStartupInitialize();liveHook=[] {++writeBirth;publishWriteIdentity();};flush();
    check(liveSetCalls==1&&liveGetCalls==0&&prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false&&noPrivateReads(),"identity revoked after SET cannot issue GET or confirm old epoch");checkNoReplay();
    enabled();steamOverlayStartupInitialize();liveGetHook=[] {++writeBirth;publishWriteIdentity();};flush();
    check(liveSetCalls==1&&liveGetCalls==1&&prefs["startupDesired"]["steamOverlayStartupResult"]["ok"]==false&&noPrivateReads(),"identity revoked during independent GET cannot confirm startup success");checkNoReplay();
    }
    if(selection=="all"||selection=="lifecycle"){
    enabled();g_steamSettingsObserver.stop();steamOverlayStartupInitialize();prefs["startupDesired"]["steamOverlayOnBoot"]=false;publishWriteIdentity();steamOverlayStartupIdentityPublished();flush();
    check(liveCalls==0&&noClaim()&&!g_steamOverlayStartupPending,"OFF during initial Unknown cancels the startup intent before durable claim");
    enabled();g_steamSettingsObserver.stop();steamOverlayStartupInitialize();g_exitRequested=true;publishWriteIdentity();steamOverlayStartupIdentityPublished();
    check(jobs.empty()&&liveCalls==0&&noClaim(),"exit while waiting for first source schedules no new work");
    enabled();poolOk=false;steamOverlayStartupInitialize();check(jobs.empty()&&noClaim()&&g_steamOverlayStartupPending,"existing pool refusal neither consumes boot nor fabricates success");
    poolOk=true;++writeBirth;publishWriteIdentity();steamOverlayStartupIdentityPublished();flush();check(liveSetCalls==1&&!g_steamOverlayStartupPending,"next natural source event can submit an initially refused one-shot intent");

    enabled();steamOverlayStartupInitialize();liveMode="live-transport-uncertain";flush();
    const auto before=liveSetCalls;steamOverlayStartupIdentityPublished();flush();check(liveSetCalls==before&&prefs["startupDesired"]["steamOverlayStartupResult"]["safeFileRetry"]==false,"uncertain startup result never replays on public source refresh");
    enabled();steamOverlayStartupInitialize();writeGateRepeats=100;flush();check(noPrivateReads()&&liveSetCalls==1&&liveGetCalls==1,"repeated final action admission only consumes the same published identity");
    enabled();running=false;writePresence=0;publishWriteIdentity();
    presenceHook=[] {running=true;writePresence=1;publishWriteIdentity();steamOverlayStartupIdentityPublished();};
    steamOverlayStartupInitialize();flush();
    check(liveSetCalls==1&&liveGetCalls==1&&writes==0&&!g_steamOverlayStartupPending,"Steam appearing at negative-source claim boundary preserves the boot opportunity for its new published live identity");
    enabled();steamOverlayStartupInitialize();running=false;writePresence=0;publishWriteIdentity();steamOverlayStartupIdentityPublished();flush();
    check(liveCalls==0&&writes==1&&backups==1&&prefs["startupDesired"]["steamOverlayStartupResult"]["persisted"]==true&&presenceReads>0,"published offline still uses original fresh absence/backup/write/readback fences");
    }
    g_steamSettingsObserver.stop();
    std::cout<<"STARTUP_SHARED_RESULT "<<Json{{"ok",true},{"selection",selection},{"checks",passed},{"revocationBoundaries",boundaries},{"actualObserverPublication",true},{"sameOriginalSettingsQueue",true},{"realSteamOperations",0},{"realFilesystemAcceptance",false}}.dump()<<'\n';return 0;
}
