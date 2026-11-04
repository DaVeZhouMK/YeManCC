// Additional cases for the original residency consumer, using the SAME inert
// namespace and the exact production decision/retry/sender definitions above.
namespace sleep_timeout_fixture {
static json lateScenarios=json::array();
static void lateReset(){reset();g_sgRetrySleepTimeout=false;g_sgResleepEnabled=true;g_sgRetryEntryFailure=false;g_powerLifecycle=PowerLifecycle::Suspended;}
static void advanceMs(ULONGLONG ms){tick+=ms;fixtureFileTime+=ms*10000;}
static void observe507(int reason,ULONGLONG elapsed,ULONGLONG deliveryDelay=0){
    at(elapsed+deliveryDelay);g_sgLastKernel507EventFileTime=epoch+elapsed*10000;
    s0WakeMessage(reason==1?1:2,reason);
}
static void manualCallback(){manualWakeCallback(fixtureFileTime,fixtureFileTime);}
static void requireSend(unsigned expected=1){
    check(monitorCalls==0&&apiCalls==expected&&std::all_of(apiArguments.begin(),apiArguments.end(),[](const auto& args){return args==std::array<BOOLEAN,3>{FALSE,FALSE,FALSE};}),"late-wake shared sender requires tripleFALSE and zero monitor calls");
}
static const json* findLateCase(const json& payload,const std::string& id){for(const auto& c:payload.at("cases"))if(c.at("id")==id)return &c;throw std::runtime_error("late payload case missing: "+id);}
static void addCompletedFields(const json& c){
    std::string fields="<Data Name=\"DurationInUs\">"+std::to_string(c.at("durationUs").get<ULONGLONG>())+"</Data>";
    for(const auto& pair:std::array<std::pair<const char*,const char*>,3>{{{"softwareDripsUs","DripsResidencyInUs"},{"hardwareDripsUs","HwDripsResidencyInUs"},{"sleepEntered","SleepEntered"}}}){
        if(c.contains(pair.first)&&!c.at(pair.first).is_null())fields+="<Data Name=\""+std::string(pair.second)+"\">"+(c.at(pair.first).is_boolean()?(c.at(pair.first).get<bool>()?"true":"false"):std::to_string(c.at(pair.first).get<ULONGLONG>()))+"</Data>";
    }
    completed(fields,fixtureFileTime);
}
template<class F> static void lateScenario(const std::string& name,F body,const json& provenance=json::object()){
    lateReset();const auto first=sleepEvidenceChecks;
    try{body();check(monitorCalls==0,"late-wake shared sender requires tripleFALSE and zero monitor calls");for(const auto& values:apiArguments)check(values==std::array<BOOLEAN,3>{FALSE,FALSE,FALSE},"late-wake shared sender requires tripleFALSE and zero monitor calls");}
    catch(...){lateScenarios.push_back({{"name",name},{"status","failed"},{"checks",sleepEvidenceChecks-first},{"mockSuspendCalls",apiCalls},{"monitorCalls",monitorCalls},{"sharedBudgetConsumed",g_sgAutomaticSleepBudget.requests()},{"provenance",provenance}});std::cerr<<"LATE_WAKE_SCENARIO: "<<name<<'\n';throw;}
    check(sleepEvidenceChecks>first,name+": selected late-wake assertions executed");
    lateScenarios.push_back({{"name",name},{"status","passed"},{"checks",sleepEvidenceChecks-first},{"mockSuspendCalls",apiCalls},{"monitorCalls",monitorCalls},{"sharedBudgetConsumed",g_sgAutomaticSleepBudget.requests()},{"timerWrites",timerWrites},{"legacyFactMonitorPreference",g_sgFactMonitorEnabled.load()},{"provenance",provenance}});
}
static void replayCase(const json& c){
    acLine=c.at("acAtWake").get<bool>()?1:0;
    check(!g_sgFactMonitorEnabled.load(),"retired fact-monitor preference is OFF; no high-frequency gating claim");
    sgArmNonUserWakeGuard("late-wake-source-original506");
    at(c.at("elapsedFromFormattedKernelTimesMs").get<ULONGLONG>());addCompletedFields(c);
    observe507(c.at("reason"),c.at("elapsedFromFormattedKernelTimesMs"));
    check(sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::UnexpectedWake,"actual nonmanual507 schedules original shared unexpected-wake transaction");
    check(timers.count(retryTimerId)&&g_sgRetryDueTick-tick==SG_NON_USER_WAKE_CLASSIFY_MS,"late wake keeps original two-second human-classification grace");
    check(!apiCalls&&!monitorCalls,"detection does not bypass original deferred dispatch");
    const auto due=g_sgRetryDueTick;advanceMs(1999);sgDispatchSameModeRetry();
    check(!apiCalls&&!monitorCalls&&g_sgRetryDueTick==due,"no power API before two-second grace");
    dispatch();requireSend();check(g_sgAutomaticSleepBudget.requests()==1,"single late wake consumes shared budget once");
    for(unsigned i=0;i<8;++i){s0WakeMessage(2,c.at("reason").get<int>());sgEvaluateExternalDeviceWake();}
    requireSend();check(g_sgAutomaticSleepBudget.requests()==1&&!timers.count(retryTimerId),"duplicates share accepted in-flight request without another timer or budget");
}
} // namespace sleep_timeout_fixture
static unsigned lateWakeNativeTests(const json& payload){
    using namespace sleep_timeout_fixture;
    const auto initial=sleepEvidenceChecks;lateScenarios=json::array();
    check(payload.at("schemaVersion")==1&&payload.at("realPowerApiCallsAllowed")==false,"late-wake payload denies real power calls");
#ifdef YMCC_SLEEP_LATE_WAKE_MONITOR_MUTATION
    const auto& c=*findLateCase(payload,"rog-stable-ac-20min-control");
    lateScenario(c.at("id").get<std::string>(),[&]{replayCase(c);},c);
#else
    for(const auto& c:payload.at("cases"))lateScenario(c.at("id").get<std::string>(),[&]{replayCase(c);},c);
    lateScenario("late507-reason7-wrapper-at20min",[]{observe507(7,1200000);dispatch();requireSend();});
    lateScenario("late507-reason28-no-reason-whitelist",[]{observe507(28,1200000);dispatch();requireSend();});
    lateScenario("zero-DRIPS-nonmanual507-still-detected",[]{at(1200000);completed("<Data Name=\"DurationInUs\">1200000000</Data><Data Name=\"DripsResidencyInUs\">0</Data><Data Name=\"HwDripsResidencyInUs\">0</Data><Data Name=\"SleepEntered\">true</Data>",fixtureFileTime);observe507(5,1200000);dispatch();requireSend();});
    lateScenario("missing-DRIPS-no-supply-or-device-change-required",[]{observe507(55,1200000);dispatch();requireSend();});
    lateScenario("ACDC-changes-alone-no-wake-no-sender",[]{at(1200000);const auto reads=powerReads;for(unsigned i=0;i<12;++i){acLine=static_cast<BYTE>(i%2);sgNoteExternalDeviceAcDcChange();}check(!g_sgNonUserWakeObserved&&!sgSleepRetryActive()&&!timerWrites&&!apiCalls&&!monitorCalls&&powerReads==reads,"supply changes remain diagnostic-only and never authorize power");});
    lateScenario("device-noise-while-suspended-is-not-wake",[]{at(1200000);sgNoteExternalDeviceNodeChange();check(!sgSleepRetryActive()&&!timerWrites&&!apiCalls&&!g_sgNonUserWakeObserved,"device enumeration alone in suspended phase never proves wake");});
    lateScenario("no-original-sleep-intent-refuses",[]{g_sgLastPowerButtonSleepIntentFileTime=g_sgLatestUserSleepEntryFileTime=0;observe507(5,1200000);check(!sgSleepRetryActive()&&!apiCalls&&!timerWrites,"no prior admitted intent cannot request sleep");});
    lateScenario("retired-original-intent-refuses",[]{sgClearUnexpectedWake("user-retired",true);observe507(5,1200000);check(!sgSleepRetryActive()&&!apiCalls&&!timerWrites,"retired original cannot be reactivated by late507");});
    lateScenario("nonuser-option-OFF-refuses",[]{g_sgResleepEnabled=false;observe507(5,1200000);check(!sgSleepRetryActive()&&!apiCalls&&!timerWrites,"feature OFF stops all late wake actions");});
    lateScenario("global-guard-OFF-refuses",[]{g_guardEnabled=false;observe507(5,1200000);check(!sgSleepRetryActive()&&!apiCalls&&!timerWrites,"guard OFF refuses late wake");});
    lateScenario("non-AoAc-S3-does-not-invent507-classification",[]{modern=false;observe507(5,1200000);check(!sgSleepRetryActive()&&!apiCalls&&!timerWrites,"S3 platform outside original S0 reason classification");});
    lateScenario("S4-task-refuses-nonmanual507-retry",[]{g_sgTask.mode=SgSleepMode::S4;observe507(5,1200000);check(!sgSleepRetryActive()&&!apiCalls&&!timerWrites,"S4 user action cannot become unexpected S3 repair");});
    lateScenario("automatic-classic-pending-refuses-lateS0",[]{g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::Sleep;observe507(5,1200000);check(!sgSleepRetryActive()&&!apiCalls&&!timerWrites,"original classic request retains ownership against stale S0messages");});
    lateScenario("late-delivery-does-not-inflate-short-sleep",[]{observe507(5,119999,1800000);check(!sgSleepRetryActive()&&!apiCalls&&!timerWrites,"original507 duration below120seconds does not become20minute sleep on delivery");});
    lateScenario("120seconds-inclusive-threshold-control",[]{observe507(5,120000);dispatch();requireSend();});
    lateScenario("manual507-callback-before-UI-at-due-vetoes",[]{observe507(5,1200000);advanceMs(2000);manualCallback();sgDispatchSameModeRetry();check(!apiCalls&&!monitorCalls&&!sgSleepRetryActive(),"callback manual veto wins before queued UI wake branch");});
    lateScenario("manual507-within2second-grace-vetoes",[]{observe507(5,1200000);advanceMs(1999);manualCallback();s0WakeMessage(1,1);dispatch();check(!apiCalls&&!monitorCalls&&!sgSleepRetryActive(),"manual branch wins within original grace and cancels transaction");});
    lateScenario("manual507-after-shared-route-log-vetoes",[]{observe507(5,1200000);afterLog=[](const char* event){if(std::string(event)=="sleep-request-route")manualCallback();};dispatch();check(!apiCalls&&!monitorCalls,"callback before shared route preparation cannot be ignored");});
    lateScenario("manual507-after-shared-dispatch-log-vetoes",[]{observe507(5,1200000);afterLog=[](const char* event){if(std::string(event)=="sleep-request-dispatch")manualCallback();};dispatch();check(!apiCalls&&!monitorCalls,"final sender repeats human cancellation after ordinary log");});
    lateScenario("manual507-during-privilege-preparation-vetoes",[]{observe507(5,1200000);afterPrivilege=[]{manualCallback();};dispatch();check(!apiCalls&&!monitorCalls,"privilege boundary cannot overtake callback manual veto");});
    lateScenario("manual507-during-detection-log-vetoes",[]{afterLog=[](const char* event){if(std::string(event)=="external-device-wake-evaluated")manualCallback();};observe507(5,1200000);dispatch();check(!sgSleepRetryActive()&&!apiCalls&&!monitorCalls,"actual dispatcher rejects callback human wake even if Evaluate had already cached eligible age before logging");});
    lateScenario("manual-original-before-request-but-late-callback-vetoes",[]{observe507(5,1200000);const auto originalWake=fixtureFileTime+1999*10000;afterLog=[=](const char* event){if(std::string(event)=="sleep-request-dispatch")manualWakeCallback(originalWake,originalWake);};dispatch();check(!apiCalls&&!monitorCalls,"original manual wake before sender start still cancels original intent");});
    lateScenario("old-manual507-cannot-cancel-newer-intent",[]{at(1200000);manualWakeCallback(epoch-1,epoch-1);observe507(5,1200000);dispatch();requireSend();});
    lateScenario("duplicate507-and-device-notifications-share-one-request",[]{observe507(5,1200000);for(unsigned i=0;i<20;++i){s0WakeMessage(2,5);sgNoteExternalDeviceNodeChange();sgNoteExternalDeviceAcDcChange();}check(timerWrites==1,"duplicate wake sources preserve single initial scheduler");dispatch();requireSend();});
    lateScenario("shared-budget-exhaustion-never-refills",[]{for(unsigned i=0;i<3;++i)check(g_sgAutomaticSleepBudget.consume(),"preexisting requests consume original cumulative budget");observe507(5,1200000);check(!sgSleepRetryActive()&&!apiCalls&&g_sgAutomaticSleepBudget.requests()==3,"late wake cannot fork or refill exhausted budget");});
    lateScenario("rejected-sender-shared-three-attempt-budget",[]{apiAccepted=false;observe507(5,1200000);dispatch();dispatch();dispatch();check(apiCalls==3&&monitorCalls==0&&g_sgAutomaticSleepBudget.requests()==3&&!sgSleepRetryActive(),"rejected sender reuses original bounded delayed attempts");for(unsigned i=0;i<8;++i)s0WakeMessage(2,5);check(apiCalls==3,"later duplicates do not create unbounded loop");});
    lateScenario("OFF-during-two-second-grace-prevents-send",[]{observe507(5,1200000);g_sgResleepEnabled=false;dispatch();check(!apiCalls&&!monitorCalls&&!sgSleepRetryActive(),"option revoked after classification wins before API");});
    lateScenario("archived-DC88min-manual-followup-cancels-real-UIbranch",[&]{const auto& c=*findLateCase(payload,"rog-dc-88min-reason55");replayCase(c);advanceMs(46991);manualCallback();s0WakeMessage(1,1);check(!sgSleepRetryActive()&&g_sgLastPowerButtonSleepIntentFileTime==0&&aborts==1,"archived later manual wake follows actual user branch and ends accepted retry");requireSend();},*findLateCase(payload,"rog-dc-88min-reason55"));
    lateScenario("manual507-UI-without-queued-retry-clears-origin",[]{at(1200000);manualCallback();s0WakeMessage(1,1);check(g_sgLastPowerButtonSleepIntentFileTime==0&&!sgSleepRetryActive()&&uiRecoveryCalls>0,"actual no-retry manual branch invokes only inert outside-scope UI recovery");});
    lateScenario("no-highfrequency-precondition-event-driven-only",[]{check(!g_sgFactMonitorEnabled.load(),"actual legacy fact-monitor preference default OFF");const auto reads=powerReads;at(1200000);check(!timerWrites&&!apiCalls&&powerReads==reads,"elapsed time alone adds no polling action");observe507(5,1200000);dispatch();requireSend();for(const auto& fact:facts)if(fact.first=="sleep-retry-request")check(fact.second.at("kind")=="unexpected-wake"&&fact.second.at("accepted")==true,"ordinary source facts retain actual sender acceptance");});
#endif
    lateReset();g_sgResleepEnabled=false;::captured.clear();
    return sleepEvidenceChecks-initial;
}
