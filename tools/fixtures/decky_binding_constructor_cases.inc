namespace construction137 {
using namespace ymcc::deckybinding;
using namespace ymcc::deckybinding::fixture;
using namespace rtfixture;
namespace probe=ymcc::deckybinding::fixture::constructionprobe;
inline Json result(const Result& value){return {{"code",static_cast<int>(value.code)},{"error",value.error},{"fullyDestroyed",value.fullyDestroyed},{"ioPending",value.ioPending}};}
inline Json requestFor(const Context& context,std::uint64_t generation){auto request=makeRequest(generation);request["bootstrap"]["fixtureContextPid"]=context.lease.at("pid");return request;}
inline Json beginReady(Client& client,const Json& request){
    need(bool(client.begin(request,1)),"construction-original-client-begin");
    const auto packet=receive(client);need(packet.kind==Packet::message&&packet.value.at("kind")=="result"&&packet.value.at("detail").at("ok")==true,"construction-original-result");
    return packet.value;
}
inline void warmClient(const Context& context){Client warm;need(warm.enable(1),"construction-warm-enable");beginReady(warm,requestFor(context,1));need(warm.stop().fullyDestroyed,"construction-warm-return");}
inline bool preparationParameter(const std::string& value){
    return value=="serialize-retry"||value=="parse-retry"||value=="payload-retry"||value=="serialize-off"||value=="parse-admission"||value=="write-attempt-no-replay"||value=="written-no-replay";
}
inline Json executePreparation(const std::filesystem::path& output,const std::string& scenario){
    const unsigned stage=scenario.starts_with("serialize")?1u:scenario.starts_with("parse")?2u:scenario.starts_with("payload")?3u:scenario=="write-attempt-no-replay"?4u:5u;
    const bool noReplay=stage>=4,off=scenario=="serialize-off",admission=scenario=="parse-admission";
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    const auto calibration=initializeSharedCryptoForMeasurement();warmClient(context);ResetEvent(controls.ready.value);
    const auto baseline=rtfixture::handles();probe::budget=0;probe::entries=0;probe::failures=0;probe::failedBytes=0;
    probe::syntheticFailures=0;probe::writeSuccesses=0;probe::preparationStage=stage;
    Json facts;bool qualified=false;
    {
        std::atomic<bool> denied{false},firstFailure{false};std::atomic<unsigned> firstReason{0};
        Harness harness(output/"loader",context,false,[&]{return denied.load()?ymcc::deckysidebar::LaunchAdmission{false,"steam-session-unconfirmed",false}:ymcc::deckysidebar::LaunchAdmission{true,{},false};},
            {},{},{},{},{},{},{},[&](const ymcc::deckysidebar::State& observed){
                const unsigned reason=observed.reason=="mirror-binding-allocation-unavailable"?1u:observed.reason=="mirror-binding-failed"?2u:observed.reason=="mirror-binding-worker-start-failed"?3u:0u;
                if(reason){firstReason=reason;firstFailure=true;}
            });
        probe::budget=1;const auto start=GetTickCount64();need(harness.runtime.setEnabled(true),"preparation-runtime-enable");
        need(waitFor([&]{return firstFailure.load();}),"preparation-first-error-observed");
        const auto first=harness.runtime.snapshot();
        if(off)need(harness.runtime.setEnabled(false),"preparation-OFF-before-retry");
        if(admission)denied=true;
        bool healed=false;
        if(!off&&!admission&&!noReplay)healed=waitFor([&]{return harness.plans>=2&&harness.healthyGeneration>=2;});
        else{const auto until=GetTickCount64()+(noReplay?5000:1300);while(GetTickCount64()<until)Sleep(2);}
        const auto observed=harness.runtime.snapshot();const auto plans=harness.plans.load();const auto healthy=harness.healthyGeneration.load();
        const auto stopping=GetTickCount64();harness.runtime.shutdown();const auto stopMs=GetTickCount64()-stopping;
        const auto final=harness.client->snapshot();probe::budget=0;
        facts={{"stage",stage},{"syntheticPostBoundaryFault",noReplay},{"firstReasonClass",firstReason.load()},{"firstReason",first.reason},{"firstBindingRetry",first.bindingRetry},
            {"finalPhase",observed.phase},{"finalReason",observed.reason},{"finalEnabled",observed.enabled},{"plans",plans},{"healthyGeneration",healthy},{"healed",healed},
            {"elapsedMs",GetTickCount64()-start},{"stopMs",stopMs},{"atExit",state(final)},{"failureTrace",harness.failureTraceLog}};
        qualified=final.fullyDestroyed&&stopMs<3200;
        if(noReplay){
            qualified=qualified&&firstReason==3&&!first.bindingRetry&&plans==1&&healthy==0;
            bool uncertain=false,falseNoMutation=false;
            for(const auto& row:harness.failureTraceLog)if(row.value("actionPhase",std::string{})=="finished"){
                uncertain=uncertain||row.value("uncertain",false);
                falseNoMutation=falseNoMutation||(row.contains("mutated")&&row["mutated"]==false);
            }
            facts["uncertainTrace"]=uncertain;facts["falseNoMutationTrace"]=falseNoMutation;
            qualified=qualified&&uncertain&&!falseNoMutation;
        }else{
            qualified=qualified&&firstReason==1&&first.bindingRetry;
            if(off)qualified=qualified&&plans==1&&!observed.enabled;
            else if(admission)qualified=qualified&&plans==1&&!healed;
            else qualified=qualified&&healed&&plans==2&&healthy==2;
        }
    }
    facts["allocationFailures"]=probe::failures.load();facts["allocationBytes"]=probe::failedBytes.load();facts["allocationEntries"]=probe::entries.load();
    facts["syntheticFailures"]=probe::syntheticFailures.load();facts["successfulRequestWrites"]=probe::writeSuccesses.load();
    facts["beforeHandles"]=baseline;facts["afterHandles"]=rtfixture::handles();facts["contextAlive"]=context.alive();probe::preparationStage=0;
    std::cout<<"CLIENT_PREPARATION_FACTS "<<facts.dump()<<std::endl;
    need(probe::failures+probe::syntheticFailures==1,"preparation-exactly-one-labelled-fault");
    need(facts["beforeHandles"]==facts["afterHandles"]&&context.alive(),"preparation-real-handle-baseline-and-context");
    need((stage==5)==(probe::writeSuccesses>0)||(!noReplay&&probe::writeSuccesses==1),"preparation-actual-write-boundary");
    need(qualified,noReplay?"preparation-write-attempt-no-replay-and-uncertainty":"preparation-prewrite-oom-original-retry-and-fences");
    return {{"passed",true},{"scenario",scenario},{"facts",facts},{"sharedCryptoInitialization",calibration},{"originalCaseId","native.typed-ready-rebind"},{"actualClientRuntime",true},{"realSteam",false},{"actualBrowserJS",false},{"defaultParent",false}};
}
inline Json executePublication(const std::filesystem::path& output){
    namespace publication=ymcc::deckybinding::fixture::publicationprobe;
    const auto calibration=initializeSharedCryptoForMeasurement();
    publication::packets=0;publication::copies=0;publication::allocations=0;publication::bytes=0;publication::contentFailures=0;
    const auto observed=rtfixture::healthy(output/"live-watch");
    Json facts={{"packets",publication::packets.load()},{"payloadCopies",publication::copies.load()},{"publicationAllocations",publication::allocations.load()},
        {"publicationBytes",publication::bytes.load()},{"contentFailures",publication::contentFailures.load()},{"originalLiveWatch",observed}};
    std::cout<<"PACKET_PUBLICATION_FACTS "<<facts.dump()<<std::endl;
    need(observed.at("passed").get<bool>(),"publication-original-runtime-live-watch");
    need(publication::packets==192&&publication::contentFailures==0,"publication-all-original-payloads-preserved");
    need(publication::copies==0&&publication::allocations==0&&publication::bytes==0,"publication-no-deep-copy-or-new-allocation-at-handoff");
    return {{"passed",true},{"scenario","packet-publication-cost"},{"facts",facts},{"sharedCryptoInitialization",calibration},{"originalCaseId","native.typed-runtime-live-watch"},{"actualClientRuntime",true},{"realSteam",false},{"actualBrowserJS",false},{"wholeProcessCpuBenefitMeasured",false}};
}
inline Json executeRuntimeDetail(const std::filesystem::path& output){
    namespace probe=ymcc::deckybinding::fixture::detailprobe;
    namespace diagnostic=ymcc::deckybinding::fixture::diagnosticprobe;
    const auto calibration=initializeSharedCryptoForMeasurement();
    probe::packets=0;probe::copies=0;probe::payloadPackets=0;probe::payloadCopies=0;probe::allocations=0;probe::bytes=0;probe::contentFailures=0;probe::initialPackets=0;probe::watchPackets=0;
    diagnostic::packets=0;diagnostic::copies=0;diagnostic::allocations=0;diagnostic::payloadPackets=0;diagnostic::payloadCopies=0;diagnostic::bytes=0;diagnostic::contentFailures=0;
    const auto observed=rtfixture::healthy(output/"live-watch");
    Json facts={{"packets",probe::packets.load()},{"objectCopies",probe::copies.load()},{"payloadPackets",probe::payloadPackets.load()},{"payloadCopies",probe::payloadCopies.load()},
        {"detailAllocations",probe::allocations.load()},{"detailBytes",probe::bytes.load()},{"contentFailures",probe::contentFailures.load()},
        {"initialPackets",probe::initialPackets.load()},{"watchPackets",probe::watchPackets.load()},{"originalLiveWatch",observed}};
    facts["diagnosticPackets"]=diagnostic::packets.load();facts["diagnosticCopies"]=diagnostic::copies.load();facts["diagnosticAllocations"]=diagnostic::allocations.load();facts["diagnosticBytes"]=diagnostic::bytes.load();
    facts["diagnosticPayloadPackets"]=diagnostic::payloadPackets.load();facts["diagnosticPayloadCopies"]=diagnostic::payloadCopies.load();facts["diagnosticContentFailures"]=diagnostic::contentFailures.load();
    std::cout<<"RUNTIME_DETAIL_FACTS "<<facts.dump()<<std::endl;
    need(observed.at("passed").get<bool>(),"detail-original-runtime-live-watch");
    need(probe::packets==194&&probe::initialPackets==1&&probe::watchPackets==193&&probe::payloadPackets==192&&probe::contentFailures==0,"detail-all-original-frames-and-payloads-preserved");
    need(diagnostic::packets==193&&diagnostic::payloadPackets==192&&diagnostic::contentFailures==0,"detail-all-original-diagnostic-arguments-preserved");
    need(probe::copies==0&&probe::payloadCopies==0&&probe::allocations==0&&probe::bytes==0&&diagnostic::copies==0&&diagnostic::payloadCopies==0&&diagnostic::allocations==0&&diagnostic::bytes==0,"detail-no-deep-copy-or-new-allocation-at-selection");
    return {{"passed",true},{"scenario","runtime-detail-cost"},{"facts",facts},{"sharedCryptoInitialization",calibration},{"originalCaseId","native.typed-runtime-live-watch"},{"actualClientRuntime",true},{"realSteam",false},{"actualBrowserJS",false},{"wholeProcessCpuBenefitMeasured",false}};
}
inline bool resourceParameter(const std::string& value){
    return value=="completion-event-retry"||value=="owner-memory-retry"||value=="completion-event-off"||value=="owner-memory-admission"||value=="owner-denied-stops";
}
inline Json executeResource(const std::filesystem::path& output,const std::string& scenario){
    const bool event=scenario.starts_with("completion-event"),off=scenario=="completion-event-off",admission=scenario=="owner-memory-admission",deniedError=scenario=="owner-denied-stops";
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    const auto calibration=initializeSharedCryptoForMeasurement();warmClient(context);ResetEvent(controls.ready.value);
    const auto baseline=rtfixture::handles();probe::budget=0;probe::entries=0;probe::failures=0;probe::failedBytes=0;probe::syntheticFailures=0;probe::writeSuccesses=0;probe::apiFailures=0;probe::apiFailureCode=0;probe::ownerReturns=0;probe::firstOwnerCode=0;probe::firstOwnerError=0;probe::firstOwnerFullyDestroyed=false;probe::firstOwnerIoPending=false;
    probe::preparationStage=event?7u:deniedError?8u:6u;
    Json facts;bool qualified=false;
    {
        std::atomic<bool> denied{false},firstFailure{false};std::atomic<unsigned> firstReason{0};
        Harness harness(output/"loader",context,false,[&]{return denied.load()?ymcc::deckysidebar::LaunchAdmission{false,"steam-session-unconfirmed",false}:ymcc::deckysidebar::LaunchAdmission{true,{},false};},
            {},{},{},{},{},{},{},[&](const ymcc::deckysidebar::State& observed){
                const unsigned reason=observed.reason=="mirror-binding-allocation-unavailable"?1u:observed.reason=="mirror-binding-failed"?2u:observed.reason=="mirror-binding-worker-start-failed"?3u:0u;
                if(reason){firstReason=reason;firstFailure=true;}
            });
        probe::budget=1;const auto start=GetTickCount64();need(harness.runtime.setEnabled(true),"resource-runtime-enable");
        need(waitFor([&]{return firstFailure.load();}),"resource-first-error-observed");const auto first=harness.runtime.snapshot();
        if(off)need(harness.runtime.setEnabled(false),"resource-OFF-before-retry");if(admission)denied=true;
        bool healed=false;
        if(!off&&!admission&&!deniedError)healed=waitFor([&]{return harness.plans>=2&&harness.healthyGeneration>=2;});
        else{const auto until=GetTickCount64()+(deniedError?5000:1300);while(GetTickCount64()<until)Sleep(2);}
        const auto observed=harness.runtime.snapshot();const auto plans=harness.plans.load();const auto healthy=harness.healthyGeneration.load();
        const auto stopping=GetTickCount64();harness.runtime.shutdown();const auto stopMs=GetTickCount64()-stopping;const auto final=harness.client->snapshot();probe::budget=0;
        facts={{"scenario",scenario},{"firstReason",first.reason},{"firstReasonClass",firstReason.load()},{"firstBindingRetry",first.bindingRetry},{"finalEnabled",observed.enabled},{"finalReason",observed.reason},
            {"plans",plans},{"healthyGeneration",healthy},{"healed",healed},{"elapsedMs",GetTickCount64()-start},{"stopMs",stopMs},{"atExit",state(final)},{"failureTrace",harness.failureTraceLog}};
        qualified=final.fullyDestroyed&&stopMs<3200;
        if(deniedError)qualified=qualified&&firstReason==3&&!first.bindingRetry&&plans==1&&healthy==0;
        else{
            qualified=qualified&&firstReason==1&&first.bindingRetry;
            if(off)qualified=qualified&&plans==1&&!observed.enabled;
            else if(admission)qualified=qualified&&plans==1&&healthy==0;
            else qualified=qualified&&healed&&plans==2&&healthy==2;
        }
    }
    facts["allocationFailures"]=probe::failures.load();facts["allocationBytes"]=probe::failedBytes.load();facts["apiFailures"]=probe::apiFailures.load();facts["apiFailureCode"]=probe::apiFailureCode.load();facts["successfulRequestWrites"]=probe::writeSuccesses.load();
    facts["beforeHandles"]=baseline;facts["afterHandles"]=rtfixture::handles();facts["contextAlive"]=context.alive();probe::preparationStage=0;
    facts["ownerReturns"]=probe::ownerReturns.load();facts["firstOwnerCode"]=probe::firstOwnerCode.load();facts["firstOwnerError"]=probe::firstOwnerError.load();facts["firstOwnerFullyDestroyed"]=probe::firstOwnerFullyDestroyed.load();facts["firstOwnerIoPending"]=probe::firstOwnerIoPending.load();
    std::cout<<"PREWRITE_RESOURCE_FACTS "<<facts.dump()<<std::endl;
    need(probe::failures+probe::apiFailures==1,"resource-exactly-one-labelled-fault");need(facts["beforeHandles"]==facts["afterHandles"]&&context.alive(),"resource-real-handle-baseline-and-context");
    need(qualified,deniedError?"resource-nonmemory-start-error-must-not-use-memory-retry":"resource-prewrite-event-or-cleaned-owner-memory-uses-original-retry-and-fences");
    return {{"passed",true},{"scenario",scenario},{"facts",facts},{"sharedCryptoInitialization",calibration},{"originalCaseId","native.typed-ready-rebind"},{"actualClientRuntime",true},{"actualKernelExhaustion",false},{"realSteam",false},{"actualBrowserJS",false},{"defaultParent",false}};
}
inline bool creationParameter(const std::string& value){return value.starts_with("prepare-");}
inline Json executeCreation(const std::filesystem::path& output,const std::string& scenario){
    const bool off=scenario=="prepare-changed-off",admission=scenario=="prepare-job-admission",deniedError=scenario=="prepare-cancel-denied";
    const unsigned stage=scenario.starts_with("prepare-changed")?9u:scenario.starts_with("prepare-cancel")?10u:scenario.starts_with("prepare-job")?11u:12u;
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    const auto calibration=initializeSharedCryptoForMeasurement();warmClient(context);ResetEvent(controls.ready.value);const auto baseline=rtfixture::handles();
    probe::budget=0;probe::entries=0;probe::failures=0;probe::failedBytes=0;probe::apiFailures=0;probe::apiFailureCode=0;probe::ownerReturns=0;probe::firstOwnerCode=0;probe::firstOwnerError=0;probe::firstOwnerFullyDestroyed=false;probe::firstOwnerIoPending=false;probe::writeSuccesses=0;probe::postFailureCreates=0;
    for(auto& value:probe::postCreateKinds)value=0;for(auto& value:probe::postCreateErrors)value=0;
    probe::injectedCreationError=deniedError?ERROR_ACCESS_DENIED:ERROR_NOT_ENOUGH_MEMORY;probe::preparationStage=stage;
    Json facts;bool qualified=false;
    {
        std::atomic<bool> denied{false},firstFailure{false};std::atomic<unsigned> firstReason{0};
        Harness harness(output/"loader",context,false,[&]{return denied.load()?ymcc::deckysidebar::LaunchAdmission{false,"steam-session-unconfirmed",false}:ymcc::deckysidebar::LaunchAdmission{true,{},false};},{},{},{},{},{},{},{},[&](const ymcc::deckysidebar::State& observed){
            const unsigned reason=observed.reason=="mirror-binding-allocation-unavailable"?1u:observed.reason=="mirror-binding-failed"?2u:observed.reason=="mirror-binding-worker-start-failed"?3u:0u;
            if(reason){firstReason=reason;firstFailure=true;}
        });
        probe::budget=1;const auto started=GetTickCount64();need(harness.runtime.setEnabled(true),"creation-runtime-enable");need(waitFor([&]{return firstFailure.load();}),"creation-first-failure-observed");
        const auto first=harness.runtime.snapshot();if(off)need(harness.runtime.setEnabled(false),"creation-OFF-before-retry");if(admission)denied=true;
        bool healed=false;if(!off&&!admission&&!deniedError)healed=waitFor([&]{return harness.plans>=2&&harness.healthyGeneration>=2;});else{const auto until=GetTickCount64()+(deniedError?5000:1300);while(GetTickCount64()<until)Sleep(2);}
        const auto observed=harness.runtime.snapshot();const auto plans=harness.plans.load();const auto healthy=harness.healthyGeneration.load();harness.runtime.shutdown();const auto final=harness.client->snapshot();probe::budget=0;
        facts={{"stage",stage},{"firstReason",first.reason},{"firstBindingRetry",first.bindingRetry},{"finalEnabled",observed.enabled},{"plans",plans},{"healthyGeneration",healthy},{"healed",healed},{"elapsedMs",GetTickCount64()-started},{"atExit",state(final)},{"failureTrace",harness.failureTraceLog}};
        qualified=final.fullyDestroyed&&probe::postFailureCreates==0&&probe::firstOwnerError==probe::injectedCreationError&&probe::firstOwnerFullyDestroyed&&!probe::firstOwnerIoPending;
        if(deniedError)qualified=qualified&&firstReason==3&&!first.bindingRetry&&plans==1&&healthy==0;
        else{qualified=qualified&&firstReason==1&&first.bindingRetry;if(off)qualified=qualified&&plans==1&&!observed.enabled;else if(admission)qualified=qualified&&plans==1&&healthy==0;else qualified=qualified&&healed&&plans==2&&healthy==2;}
    }
    Json later=Json::array();for(unsigned i=0;i<probe::postFailureCreates&&i<4;++i)later.push_back({{"kind",probe::postCreateKinds[i].load()},{"lastErrorAfterActualCall",probe::postCreateErrors[i].load()}});
    facts["apiFailures"]=probe::apiFailures.load();facts["apiFailureCode"]=probe::apiFailureCode.load();facts["firstOwnerError"]=probe::firstOwnerError.load();facts["firstOwnerFullyDestroyed"]=probe::firstOwnerFullyDestroyed.load();facts["firstOwnerIoPending"]=probe::firstOwnerIoPending.load();facts["postFailureCreates"]=probe::postFailureCreates.load();facts["laterActualCreates"]=later;facts["successfulRequestWrites"]=probe::writeSuccesses.load();facts["beforeHandles"]=baseline;facts["afterHandles"]=rtfixture::handles();facts["contextAlive"]=context.alive();probe::preparationStage=0;
    std::cout<<"CREATION_ERROR_FACTS "<<facts.dump()<<std::endl;
    need(probe::apiFailures==1,"creation-one-injected-failure");need(facts["beforeHandles"]==facts["afterHandles"]&&context.alive(),"creation-real-handle-baseline-and-context");
    need(qualified,"creation-preserves-first-error-and-stops-further-preparation");
    return {{"passed",true},{"scenario",scenario},{"facts",facts},{"sharedCryptoInitialization",calibration},{"originalCaseId","native.typed-ready-rebind"},{"actualClientRuntime",true},{"actualKernelExhaustion",false},{"realSteam",false},{"defaultParent",false}};
}
inline bool handoffParameter(const std::string& value){return value.starts_with("handoff-");}
inline Json executeHandoff(const std::filesystem::path& output,const std::string& scenario){
    const bool deniedError=scenario=="handoff-child-denied",cancelled=scenario=="handoff-owner-cancelled",off=scenario=="handoff-reader-off",admission=scenario=="handoff-pipe-admission";
    const unsigned stage=cancelled?17u:deniedError?16u:scenario.starts_with("handoff-child")?13u:scenario.starts_with("handoff-pipe")?14u:15u;
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    const auto calibration=initializeSharedCryptoForMeasurement();warmClient(context);ResetEvent(controls.ready.value);const auto baseline=rtfixture::handles();
    probe::budget=0;probe::apiFailures=0;probe::apiFailureCode=0;probe::clientReturns=0;probe::firstClientCode=0;probe::firstClientError=0;probe::cancelInjections=0;probe::writeSuccesses=0;probe::preparationStage=stage;
    Json facts;bool qualified=false;
    {
        std::atomic<bool> denied{false},firstFailure{false};std::atomic<unsigned> firstReason{0};
        Harness harness(output/"loader",context,false,[&]{return denied.load()?ymcc::deckysidebar::LaunchAdmission{false,"steam-session-unconfirmed",false}:ymcc::deckysidebar::LaunchAdmission{true,{},false};},{},{},{},{},{},{},{},[&](const ymcc::deckysidebar::State& observed){
            const unsigned reason=observed.reason=="mirror-binding-allocation-unavailable"?1u:observed.reason=="mirror-binding-failed"?2u:observed.reason=="mirror-binding-worker-start-failed"?3u:0u;
            if(reason){firstReason=reason;firstFailure=true;}
        });
        probe::budget=1;const auto started=GetTickCount64();need(harness.runtime.setEnabled(true),"handoff-runtime-enable");need(waitFor([&]{return firstFailure.load();}),"handoff-first-failure-observed");const auto first=harness.runtime.snapshot();
        if(off)need(harness.runtime.setEnabled(false),"handoff-OFF-before-retry");if(admission)denied=true;
        bool healed=false;if(!off&&!admission&&!deniedError&&!cancelled)healed=waitFor([&]{return harness.plans>=2&&harness.healthyGeneration>=2;});else{const auto until=GetTickCount64()+((deniedError||cancelled)?5000:1300);while(GetTickCount64()<until)Sleep(2);}
        const auto observed=harness.runtime.snapshot();const auto plans=harness.plans.load();const auto healthy=harness.healthyGeneration.load();harness.runtime.shutdown();const auto final=harness.client->snapshot();probe::budget=0;
        facts={{"stage",stage},{"firstReason",first.reason},{"firstBindingRetry",first.bindingRetry},{"finalEnabled",observed.enabled},{"plans",plans},{"healthyGeneration",healthy},{"healed",healed},{"elapsedMs",GetTickCount64()-started},{"atExit",state(final)},{"failureTrace",harness.failureTraceLog}};
        const unsigned expectedError=static_cast<unsigned>(cancelled?ERROR_OPERATION_ABORTED:deniedError?ERROR_ACCESS_DENIED:ERROR_NOT_ENOUGH_MEMORY);
        qualified=final.fullyDestroyed&&probe::firstClientError==expectedError;
        if(deniedError||cancelled)qualified=qualified&&firstReason==3&&!first.bindingRetry&&plans==1&&healthy==0;
        else{qualified=qualified&&firstReason==1&&first.bindingRetry;if(off)qualified=qualified&&plans==1&&!observed.enabled;else if(admission)qualified=qualified&&plans==1&&healthy==0;else qualified=qualified&&healed&&plans==2&&healthy==2;}
    }
    facts["apiFailures"]=probe::apiFailures.load();facts["apiFailureCode"]=probe::apiFailureCode.load();facts["cancelInjections"]=probe::cancelInjections.load();facts["clientReturns"]=probe::clientReturns.load();facts["firstClientCode"]=probe::firstClientCode.load();facts["firstClientError"]=probe::firstClientError.load();facts["successfulRequestWrites"]=probe::writeSuccesses.load();facts["beforeHandles"]=baseline;facts["afterHandles"]=rtfixture::handles();facts["contextAlive"]=context.alive();probe::preparationStage=0;
    std::cout<<"HANDOFF_ERROR_FACTS "<<facts.dump()<<std::endl;need(probe::apiFailures+probe::cancelInjections==1,"handoff-one-labelled-fault-or-cancel");need(facts["beforeHandles"]==facts["afterHandles"]&&context.alive(),"handoff-real-handle-baseline-and-context");
    need(qualified,"handoff-error-retained-and-prewrite-memory-retry-fenced");return {{"passed",true},{"scenario",scenario},{"facts",facts},{"sharedCryptoInitialization",calibration},{"originalCaseId","native.typed-ready-rebind"},{"actualClientRuntime",true},{"actualKernelExhaustion",false},{"realSteam",false},{"defaultParent",false}};
}
#include "decky_resource_preflight_cases.inc"
inline Json execute(const std::filesystem::path& output,const std::string& scenario){
    if(scenario.starts_with("worker-"))return executeResourceWorker(output,scenario);
    if(handoffParameter(scenario))return executeHandoff(output,scenario);
    if(creationParameter(scenario))return executeCreation(output,scenario);
    if(resourceParameter(scenario))return executeResource(output,scenario);
    if(scenario=="runtime-detail-cost")return executeRuntimeDetail(output);
    if(scenario=="packet-publication-cost")return executePublication(output);
    if(preparationParameter(scenario))return executePreparation(output,scenario);
    need(scenario=="direct-preserve"||scenario=="runtime-heal"||scenario=="off"||scenario=="admission","construction-known-original-parameter");
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    const auto calibration=initializeSharedCryptoForMeasurement();warmClient(context);ResetEvent(controls.ready.value);
    const auto baseline=rtfixture::handles();probe::budget=0;probe::entries=0;probe::failures=0;probe::failedBytes=0;
    Json facts;bool qualified=false;
    if(scenario=="direct-preserve"){
        Client client;need(client.enable(1),"construction-direct-enable");beginReady(client,requestFor(context,1));
        const auto before=client.snapshot();detail::Handle old{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,before.processId)};
        need(old.value!=nullptr,"construction-original-current-child");const auto request=requestFor(context,2);
        probe::budget=1;bool threw=false;Result failed;
        try{failed=client.begin(request,1);}catch(const std::bad_alloc&){threw=true;}
        probe::budget=0;const auto after=client.snapshot();const bool previousLive=WaitForSingleObject(old.value,0)==WAIT_TIMEOUT;
        const auto correlated=beginReady(client,request);const auto rebound=client.snapshot();const auto stopped=client.stop();const auto final=client.snapshot();
        const bool oldReturned=WaitForSingleObject(old.value,0)==WAIT_OBJECT_0;DWORD closeError=0;need(old.close(closeError),"construction-held-child-return");
        facts={{"threwBadAlloc",threw},{"failedResult",result(failed)},{"before",state(before)},{"afterFailure",state(after)},
            {"oldChildRemainedLive",previousLive},{"oldChildReturned",oldReturned},{"sameOwnerAfterFailure",after.processId==before.processId&&after.generation==before.generation},{"beforeOwnerGeneration",before.generation},{"retryOwnerGeneration",rebound.generation},{"retryRequestGeneration",correlated.at("generation")},{"afterSameRequestRetry",state(rebound)},{"atExit",state(final)},{"stop",result(stopped)}};
        qualified=!threw&&failed.code==Code::notStarted&&failed.error==ERROR_NOT_ENOUGH_MEMORY&&previousLive&&after.processId==before.processId&&after.generation==before.generation&&after.sessions==before.sessions&&!after.canceled&&rebound.generation!=before.generation&&correlated.at("generation")=="2"&&correlated.at("id")==request.at("id")&&oldReturned&&final.fullyDestroyed;
    }else{
        std::atomic<bool> denied{false},firstFailure{false};std::atomic<unsigned> firstReason{0};
        Harness harness(output/"loader",context,false,[&]{return denied.load()?ymcc::deckysidebar::LaunchAdmission{false,"steam-session-unconfirmed",false}:ymcc::deckysidebar::LaunchAdmission{true,{},false};},
            {},{},{},{},{},{},{},[&](const ymcc::deckysidebar::State& observed){
                const unsigned reason=observed.reason=="mirror-binding-allocation-unavailable"?1u:observed.reason=="mirror-binding-failed"?2u:observed.reason=="mirror-binding-worker-start-failed"?3u:0u;
                if(reason){firstReason=reason;firstFailure=true;}
            });
        probe::budget=1;const auto start=GetTickCount64();need(harness.runtime.setEnabled(true),"construction-runtime-enable");
        need(waitFor([&]{return firstFailure.load();}),"construction-runtime-first-error-observed");
        const auto first=harness.runtime.snapshot();
        if(scenario=="off")need(harness.runtime.setEnabled(false),"construction-OFF-before-first-retry");
        if(scenario=="admission")denied=true;
        bool healed=false;
        if(scenario=="runtime-heal")healed=waitFor([&]{return harness.plans>=2&&harness.healthyGeneration>=2;});
        else{const auto until=GetTickCount64()+1300;while(GetTickCount64()<until)Sleep(2);}
        const auto observed=harness.runtime.snapshot();const auto plans=harness.plans.load();const auto healthy=harness.healthyGeneration.load();
        const auto stopping=GetTickCount64();harness.runtime.shutdown();const auto stopMs=GetTickCount64()-stopping;
        const auto final=harness.client->snapshot();probe::budget=0;
        facts={{"firstReasonClass",firstReason.load()},{"firstPhase",first.phase},{"firstReason",first.reason},{"firstBindingRetry",first.bindingRetry},
            {"finalPhase",observed.phase},{"finalReason",observed.reason},{"finalEnabled",observed.enabled},{"plans",plans},{"healthyGeneration",healthy},
            {"healed",healed},{"elapsedMs",GetTickCount64()-start},{"stopMs",stopMs},{"atExit",state(final)},
            {"failureTrace",harness.failureTraceLog},            {"originalBindingRetryWindowUnchanged",true},{"actualRuntime",true},{"actualClientOwner",true}};
        qualified=firstReason==1&&first.bindingRetry&&final.fullyDestroyed&&stopMs<3200;
        if(scenario=="runtime-heal")qualified=qualified&&healed&&plans==2&&healthy==2;
        else if(scenario=="off")qualified=qualified&&plans==1&&!observed.enabled;
        else qualified=qualified&&plans==1&&!healed;
    }
    facts["allocationFailures"]=probe::failures.load();facts["allocationBytes"]=probe::failedBytes.load();facts["allocationEntries"]=probe::entries.load();
    facts["beforeHandles"]=baseline;facts["afterHandles"]=rtfixture::handles();facts["contextAlive"]=context.alive();
    std::cout<<"CLIENT_CONSTRUCTION_FACTS "<<facts.dump()<<std::endl;
    need(probe::failures==1,"construction-exactly-one-real-allocation-fault");
    need(facts["beforeHandles"]==facts["afterHandles"]&&context.alive(),"construction-real-handle-baseline-and-inert-context");
    need(qualified,scenario=="direct-preserve"?"construction-client-typed-error-preserves-unpublished-manager-state":"construction-runtime-prepublication-oom-uses-original-retry-and-fences");
    return {{"passed",true},{"scenario",scenario},{"facts",facts},{"sharedCryptoInitialization",calibration},
        {"originalCaseId","native.typed-ready-rebind"},{"actualClientRuntime",true},{"realSteam",false},{"actualBrowserJS",false},{"defaultParent",false}};
}
}
