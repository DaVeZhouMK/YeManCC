// Passive first witnesses of existing virtual-controller steps, never actions.
static json virtualAdaptationSanitized(const json& value){
    if(value.is_object()){json out=json::object();for(auto it=value.begin();it!=value.end();++it){
        const auto& key=it.key();if(key=="startupNonce"||key=="frameReplyBinding"||key=="authToken"||key=="accessToken"||key=="refreshToken"||key=="password")continue;
        out[key]=virtualAdaptationSanitized(it.value());}return out;}
    if(value.is_array()){json out=json::array();for(const auto& item:value)out.push_back(virtualAdaptationSanitized(item));return out;}
    return value;
}
static ymcc::adaptlog::VirtualFacts& virtualAdaptationFacts(){static ymcc::adaptlog::VirtualFacts value;return value;}
static ymcc::adaptlog::Journal& virtualAdaptationJournal(){static ymcc::adaptlog::Journal value(8ull*1024*1024,4096,32768);return value;}
static std::atomic<unsigned long long> g_virtualAdaptationBuildFailures{0};
static std::atomic<unsigned long long> g_virtualAdaptationStepBuildFailures{0};
static ymcc::virtualAdaptation::FrameSelector& virtualAdaptationSelector(){static ymcc::virtualAdaptation::FrameSelector value;return value;}
static void virtualAdaptationFlush() noexcept {
    if(!virtualAdaptationFacts().pending())return;
    try{static std::mutex mx;std::unique_lock<std::mutex> lock(mx,std::try_to_lock);if(!lock)return;
        static ULONGLONG retryAt=0;const auto now=GetTickCount64();if(now<retryAt)return;
        for(unsigned n=0;n<8;++n){size_t index=0;ymcc::adaptlog::Fact value;if(!virtualAdaptationFacts().take(index,value))break;
            auto result=ymcc::adaptlog::RecordResult::retry;auto outcome=ymcc::adaptlog::JournalDrainOutcome::unknown;
            try{value.sample["capturedTickMs"]=value.capturedAt;
                result=virtualAdaptationJournal().recordResult(fspath::path(yemancc_data_dir())/L"virtual-adaptation.log",value.model,value.category.c_str(),value.identity,value.sample,nativeLifecycleTimestamp(),&outcome);}catch(...){}
            virtualAdaptationFacts().finish(index,result!=ymcc::adaptlog::RecordResult::retry,outcome);if(result==ymcc::adaptlog::RecordResult::retry){retryAt=now+5000;break;}
        }
    }catch(...){}
}
static void virtualAdaptationObserveRecord(const char* event,const json& detail) noexcept {
    if(!event||!detail.is_object())return;
    if(std::strncmp(event,"input-host-",11)&&std::strncmp(event,"gamepad-",8)&&std::strncmp(event,"hidhide.",8)&&
        std::strncmp(event,"wake-input-chain-",17)&&std::strcmp(event,"physical-controller-identity")&&
        std::strcmp(event,"virtual-start-protection")&&std::strcmp(event,"input-target-intent-evidence"))return;
    try {
        if(!std::strcmp(event,"input-host-command-result")&&detail.value("command","")=="SUBMIT_FRAME")return;
        if(!std::strcmp(event,"physical-controller-identity")&&detail.value("snapshotReason","")=="repeat")return;
        if(!std::strcmp(event,"input-host-player-start-work")&&detail.value("action","")=="consuming")return;
        bool selected=false;for(const auto* allowed:{"input-host-onex-start-refresh","input-host-switch-execution","input-host-switch-recovery",
            "input-host-wake-execution","input-host-open-result","input-host-open-skipped","input-host-stop-request","input-host-stop-result",
            "input-host-cleanup-evidence","input-host-transport-fault","input-host-persona-transition","input-host-persona-transition-requested",
            "input-host-runtime-recovery","input-host-recovery-exhausted","input-host-recovery-fallback",
            "input-host-switch-audit","input-host-switch-verification","input-host-rog-off-execution","input-host-player-start-work","input-host-command-result","input-host-native-helper-diagnostic",
            "gamepad-openxinput-slot-guard","gamepad-openxinput-assign-slot","gamepad-vibrate-request","gamepad-vibrate","gamepad-vibrate-stop",
            "physical-controller-identity","virtual-start-protection","input-target-intent-evidence","input-host-started",
            "input-host-release-receipt","input-host-release-visibility","input-host-frame-admission","hidhide.visibility-snapshot",
            "hidhide.p-hid-refreshed","input-host-target-return-record","input-host-tuple-recovery","input-host-recovery-episode-new",
            "input-host-recovery-episode-healthy","input-host-recovery-episode-exhausted","input-host-xbox-startup","wake-input-chain-first-frame",
            "wake-input-chain-sustained","gamepad-xinput-virtual-endpoint-slot"})if(!std::strcmp(event,allowed)){selected=true;break;}
        if(!selected)return;
        json identity={{"event",event}};
        // Operation identity is intentional here. Per-frame sequences, elapsed clocks,
        // counters and arbitrary raw values live only in the bounded first sample.
        for(const auto* key:{"policy","repairRevision","stage","phase","reason","status","state","evidence","lastInspectionState","lastInspectionEvidence","pauseObservedMask","observedMask","ok","action","outcome","backend","persona",
            "deviceId","command","hostSubmission","slot","targetSlot","settledSlot0","accepted","readback","sourcePath","sourceRoot",
            "requestId","ticket","runId","hostEpoch","epoch","hostInstanceId","powerGeneration","requestGeneration","sourceGeneration","generation","wakeSerial","episodeId","writerInstanceId","intentRevision","presentationGeneration",
            "driverOperationMayHaveIssued","freshSameRootSource","externalSuppressionVerified","filterAttachmentProven","newTargetCreatedHere","visibilityRestored","visibilityHeld","quarantined","hostExitConfirmed","candidateOutcome","stopInvoked","spent","awaitingCleanup"})
            if(detail.contains(key))identity[key]=detail[key];
        const bool recovery=std::strcmp(event,"input-host-runtime-recovery")==0||
            std::strcmp(event,"input-host-recovery-exhausted")==0||std::strcmp(event,"input-host-recovery-fallback")==0;
        bool recoveryContextKnown=false;
        if(recovery){
            const auto positive=[&](const char* key){const auto it=detail.find(key);return it!=detail.end()&&
                ((it->is_number_unsigned()&&it->get<unsigned long long>()>0)||
                 (it->is_number_integer()&&it->get<long long>()>0));};
            const auto& writer=ymcc::sleepfactlog::writerIdentity();
            json context={{"serialDomain",std::strcmp(event,"input-host-runtime-recovery")==0?"runtime-incident":"terminal-physical-fallback"},
                {"writerPid",writer.pid},{"writerProcessCreation100ns",writer.creation100ns?json(std::to_string(writer.creation100ns)):json(nullptr)}};
            for(const auto* key:{"serial","requestId"})context[key]=detail.contains(key)?detail[key]:json(nullptr);
            recoveryContextKnown=writer.pid!=0&&writer.creation100ns!=0&&positive("serial")&&positive("requestId");
            const auto nativeType=[&](const char* key){const auto it=detail.find(key);return it==detail.end()?"missing":
                it->is_number_unsigned()?"unsigned-integer":it->is_number_integer()?"signed-integer":it->is_number_float()?"float":it->type_name();};
            context["serialType"]=nativeType("serial");context["requestIdType"]=nativeType("requestId");
            context["operationContextKnown"]=recoveryContextKnown;
            identity["recoveryOperationContext"]=std::move(context);
            json shape=json::object();
            for(const auto* key:{"recoveryRequestId","wave","origin","nativeWriterAttempt","demandRevision","via","error",
                "pending","queued","monitoring","wavePermit","automaticAttemptLimit","ownedCleanupProven","ownedVisibilityRestored","freshPhysicalRead"})
                shape[key]=detail.contains(key)?detail[key]:json(nullptr);
            identity["recoveryStepShape"]=std::move(shape);
        }
        const bool cleanup=std::strcmp(event,"input-host-cleanup-evidence")==0;
        const bool visibility=std::strcmp(event,"input-host-release-visibility")==0;
        bool cleanupRequestKnown=false;
        if(cleanup){
            json shape=json::object();
            for(const auto* key:{"localCleanupResult","coldReturnContinuationArmed"})
                shape[key]=detail.contains(key)?detail[key]:json(nullptr);
            const auto evidence=detail.find("cleanupEvidence");
            for(const auto* key:{"checks","blockers","unknown","physicalReturnVerified","msiReturnDutyApplies",
                "msiReturnOwedSampled","historySessionReady","runIdPresent"})
                shape[key]=evidence!=detail.end()&&evidence->is_object()&&evidence->contains(key)?(*evidence)[key]:json(nullptr);
            identity["cleanupResultShape"]=std::move(shape);
            const auto request=detail.find("closingRequestId");
            cleanupRequestKnown=request!=detail.end()&&((request->is_number_unsigned()&&request->get<unsigned long long>()>0)||
                (request->is_number_integer()&&request->get<long long>()>0));
            identity["cleanupRequestContext"]=nullptr;
            if(cleanupRequestKnown){const auto& writer=ymcc::sleepfactlog::writerIdentity();
                identity["cleanupRequestContext"]={{"closingRequestId",*request},{"writerPid",writer.pid},
                    {"writerProcessCreation100ns",writer.creation100ns?json(std::to_string(writer.creation100ns)):json(nullptr)}};
            }
        }
        if(visibility){
            json shape=json::object();
            for(const auto* key:{"snapshotPhase","disposition","hostAlive","localReleaseAck","shutdownAck",
                "physicalHiddenByHidHide","virtualEndpointAbsent","cloakOn"})
                shape[key]=detail.contains(key)?detail[key]:json(nullptr);
            identity["releaseVisibilityShape"]=std::move(shape);
        }
        const bool startup=std::strcmp(event,"input-host-xbox-startup")==0;
        if(startup){
            // The producer may have no operation ID. Retain first distinct check
            // shapes rather than pretending phase alone identifies an operation.
            // Missing attempted flags stay unknown; numeric defaults are not calls.
            json shape=json::object();
            for(const auto* key:{"nodePresent","statusRead","nodeStatus","nodeProblem"})
                shape[key]=detail.contains(key)?detail[key]:json(nullptr);
            shape["slotReadback"]=nullptr;
            const auto slots=detail.find("slotReadback");
            if(slots!=detail.end()&&slots->is_array()){
                shape["slotReadback"]=json::array();
                for(const auto& slot:*slots){
                    json observed=json::object();
                    for(const auto* key:{"slot","stage","stateAttempted","pathAttempted","targetInterfaceAttempted",
                        "targetInterfaceMatched","indexAttempted","confirmAttempted","stateResult","confirmResult","mappedSlot"})
                        observed[key]=slot.is_object()&&slot.contains(key)?slot[key]:json(nullptr);
                    shape["slotReadback"].push_back(std::move(observed));
                }
            }
            identity["startupReadbackShape"]=std::move(shape);
        }
        virtualAdaptationFacts().offerLazy(modelAdaptationMachine(),std::move(identity),"virtual-step-witness",[&]{
            json sample={{"observedFrom",event},{"producer",virtualAdaptationSanitized(detail)},{"producerFieldsUnmodifiedExceptTransportSecrets",true},
                {"scope",startup?"first-unique-startup-readback-shape;missing-operation-context-unknown;not-complete-operation-timeline-or-external-game-success":
                    "first-step-witness-within-operation;not-complete-timeline-or-external-game-success"}};
            if(recovery){
                sample["operationContextKnown"]=recoveryContextKnown;
                sample["recoveryEvidenceScope"]="first-step/result-shape;cached-writer+serial-domain+serial+request;not-complete-recovery-timeline-or-game-input-success";
                sample["runtimeAndFallbackSerialDomainsAreDistinct"]=true;
            }
            if(startup)sample["operationContextKnown"]=detail.contains("requestId")&&!detail["requestId"].is_null();
            if(cleanup||visibility){
                sample["operationContextKnown"]=cleanup&&cleanupRequestKnown;
                sample["terminalEvidenceScope"]="first-result-shape;positive-closingRequestId-scoped-by-cached-writer;absent-operation-id-unknown;not-complete-stop-timeline-or-physical-return";
            }
            return sample;
        });
    }catch(...){g_virtualAdaptationStepBuildFailures.fetch_add(1,std::memory_order_relaxed);}
}
static void virtualAdaptationFrameReceipt(const ymcc::virtualAdaptation::FrameStamp& stamp,const json& frame,const std::string& wireText,
    const std::string& requestId,const json& receipt) noexcept {
    const bool sdkWitness=receipt.is_object()&&receipt.contains("sdkFirstWitnesses");
    const bool selected=virtualAdaptationSelector().select(stamp);if(!selected&&!sdkWitness)return;
    try {
        const std::string runId(stamp.runId.data()),hostId(stamp.hostId.data());
        json identity={{"epoch",stamp.epoch},{"power",stamp.power},{"sourceGeneration",stamp.sourceGeneration},{"topology",stamp.topology},
            {"semanticNeutral",stamp.neutral},{"nativeAccepted",stamp.accepted},{"sourceAdmitted",stamp.sourceAdmitted},{"runId",runId},{"hostInstanceId",hostId}};
        if(sdkWitness){identity["sdkWitnesses"]=json::array();for(const auto& item:receipt["sdkFirstWitnesses"]){
            json key=json::object();if(item.is_object()){
                if(item.contains("outcome"))key["outcome"]=item["outcome"];
                const auto sample=item.find("sample");if(sample!=item.end()&&sample->is_object())for(const auto* field:{"ordinal","selection","reason","request","targetCreationLifetimeEvidence"})
                    if(sample->contains(field))key[field]=virtualAdaptationSanitized((*sample)[field]);}
            identity["sdkWitnesses"].push_back(std::move(key));}
            if(identity["sdkWitnesses"].empty()&&receipt.contains("sdkFirstWitnessCoverage")){
                identity["sdkCoverageGap"]=json::object();for(const auto* key:{"dropped","emissionFailures","processSampleLimit","replyBudgetDeferralLimit"})
                    if(receipt["sdkFirstWitnessCoverage"].contains(key))identity["sdkCoverageGap"][key]=receipt["sdkFirstWitnessCoverage"][key];
            }}

        json actual=wireText.empty()?frame:json::parse(wireText,nullptr,false);
        json replay=json::object();
        if(actual.is_object())for(const auto* key:{"command","buttons","backButtons","lt","rt","ltRaw","rtRaw","lx","ly","rx","ry","raw","imu","touchpads","screenButtons",
            "protocolVersion","configHash","configRevision","epoch","persona","powerGeneration","profileIdentity","targetId","monotonicTick","motionPairProofId","normalizedInputHash","runId","sampleSequence","sequence","requestId"})
            if(actual.contains(key))replay[key]=actual[key];
        json source={{"snapshotKnown",t_commonFrameSource!=nullptr},{"hardwarePacketFreshnessProven",false},{"physicalIdentityBindingProven",false}};
        if(const auto* current=t_commonFrameSource){const auto& pad=current->pad;
            source.update({{"inputSource",current->inputSource},{"readAttemptSequence",current->readAttemptSequence},{"readStartedTick",current->readStartedTick},
                {"readCompletedTick",current->readCompletedTick},{"readPowerGeneration",current->readPowerGeneration},{"readTopologyGeneration",current->readTopologyGeneration},
                {"readContinuityGeneration",current->readContinuityGeneration},{"primarySlot",current->primarySlot},{"live",current->live},{"readContextStable",current->readContextStable},
                {"pad",{{"buttons",pad.wButtons},{"ltByte",pad.bLeftTrigger},{"rtByte",pad.bRightTrigger},{"lxShort",pad.sThumbLX},{"lyShort",pad.sThumbLY},{"rxShort",pad.sThumbRX},{"ryShort",pad.sThumbRY}}}});}
        json sample={{"source",std::move(source)},{"requestId",requestId},{"commandSequence",stamp.commandSequence},{"wireSampleSequence",stamp.wireSample},{"wireMonotonicTick",stamp.wireTick},
            {"replayRequest",std::move(replay)},{"preparedTextAvailable",!wireText.empty()},{"requestJsonDecoded",actual.is_object()},
            {"transportSecretsRetained",false},{"requestFields","allowlisted-controls-and-tuple;startupNonce-and-transport-auth-omitted"},
            {"bindingCapturedBeforeRequest",true},{"bindingTruncated",stamp.bindingTruncated},{"nativeReceiptJson",virtualAdaptationSanitized(receipt)},
            {"fastReceiptUsed",stamp.fastReceipt},{"frameScope","actual-submit-control-values;transport-auth-omitted;not-driver-report-bytes"},
            {"nativeAccepted",stamp.accepted},{"hostSdkDeliveryProven",false},{"physicalSuppressionProven",false},{"steamConsumptionProven",false},{"gameConsumptionProven",false}};
        virtualAdaptationFacts().offer({modelAdaptationMachine(),std::move(identity),std::move(sample),sdkWitness?"virtual-sdk-first-witness":"virtual-submit-first-witness"});
    }catch(...){++g_virtualAdaptationBuildFailures;}
}
static json virtualAdaptationCapacityWitness(unsigned shape,const ymcc::virtualAdaptation::FrameStamp& s){
    return {{"shape",shape},{"selectionDecision","capacity-rejected"},
        {"epoch",s.epoch},{"power",s.power},{"sourceContinuityGeneration",s.sourceGeneration},{"topologyGeneration",s.topology},
        {"semanticNeutral",s.neutral},{"nativeAccepted",s.accepted},{"sourceAdmitted",s.sourceAdmitted},
        {"commandSequence",s.commandSequence},{"wireSampleSequence",s.wireSample},{"wireTickMs",s.wireTick},
        {"fastReceiptUsed",s.fastReceipt},{"bindingTruncated",s.bindingTruncated},
        {"runId",std::string(s.runId.data())},{"hostInstanceId",std::string(s.hostId.data())}};
}
static json virtualAdaptationContextEvidence(const ymcc::virtualAdaptation::FrameSelector::ExportSnapshot& observed){
    json result={{"schemaVersion",1},{"snapshotAvailable",observed.available},
        {"snapshotAtomicWithSiblingStats",false},{"selectionOnly",true},
        {"scope","retained selector tuples and first capacity-rejected outcome shapes;not all contexts,raw frames,or external delivery"}};
    if(!observed.available){result["reason"]="selector-lock-busy;context-evidence-unknown";return result;}
    result["knownContextsAtSnapshot"]=observed.knownContexts;
    result["maxCapacityOutcomeWitnesses"]=8;
    result["retainedContexts"]=json::array();result["capacityFirstOutcomes"]=json::array();
    for(unsigned i=0;i<observed.knownContexts;++i){const auto& c=observed.contexts[i];
        result["retainedContexts"].push_back({{"contextOrdinal",i+1},{"epoch",c.epoch},{"power",c.power},
            {"sourceContinuityGeneration",c.source},{"topologyGeneration",c.topology},{"selectedOutcomeMask",c.selectedOutcomeMask}});
    }
    auto& highest=result["capacityHighestPowerEvidence"];
    highest={{"schemaVersion",1},{"power",observed.capacityHighestPower?json(observed.capacityHighestPower):json(nullptr)},
        {"firstOutcomes",json::array()}};
    for(unsigned i=0;i<8;++i){
        if(observed.capacityShapeMask&(1u<<i))result["capacityFirstOutcomes"].push_back(virtualAdaptationCapacityWitness(i,observed.capacityFirst[i]));
        if(observed.capacityHighestPowerMask&(1u<<i))highest["firstOutcomes"].push_back(virtualAdaptationCapacityWitness(i,observed.capacityHighestPowerFirst[i]));
    }
    result["outcomeMaskEncoding"]="bit index=neutral:1|nativeAccepted:2|sourceAdmitted:4;seen-mask has bit(1<<index)";
    result["rejectedWitnessScope"]="first per8 outcome shapes across all unretained tuples;not first per context,unique-context count,SDK receipt,or complete timeline";
    return result;
}
static json virtualAdaptationStatus(){const auto contexts=virtualAdaptationSelector().knownContexts();return {{"file","virtual-adaptation.log"},{"alwaysOn",true},{"maxBytes",8ull*1024*1024},
    {"maxRecords",4096},{"frameKnownContexts",contexts},{"frameMaxContexts",64},{"frameContextLimitReached",contexts==64},
    {"frameCapacityDropScope","attempts-with-unretained-context;not-unique-input-types-or-game-delivery-failures;known-context-new-outcomes-and-SDK-witnesses-remain-eligible"},{"firstFramePolicy","up to8 outcome/neutral/admission combinations per exact context;64 contexts per native process;no eviction"},
    {"facts",virtualAdaptationFacts().status()},{"journal",virtualAdaptationJournal().status()},
    {"stepBuildFailures",g_virtualAdaptationStepBuildFailures.load()},{"frameBuildFailures",g_virtualAdaptationBuildFailures.load()},{"frameSelected",virtualAdaptationSelector().selected()},{"frameCapacityDrops",virtualAdaptationSelector().capacityDrops()},
    {"frameBusyDrops",virtualAdaptationSelector().busyDrops()},{"frameUnknownContextDrops",virtualAdaptationSelector().unknownDrops()},
    {"frameContextEvidence",virtualAdaptationContextEvidence(virtualAdaptationSelector().snapshotForExport())},
    {"scope","sample gaps explicit;missing stage is unknown;API/ACK is not external Steam/game/rumble proof"}};}
