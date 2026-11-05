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
static ymcc::virtualAdaptation::FrameSelector& virtualAdaptationSelector(){static ymcc::virtualAdaptation::FrameSelector value;return value;}
static void virtualAdaptationFlush() noexcept {
    if(!virtualAdaptationFacts().pending())return;
    try{static std::mutex mx;std::unique_lock<std::mutex> lock(mx,std::try_to_lock);if(!lock)return;
        static ULONGLONG retryAt=0;const auto now=GetTickCount64();if(now<retryAt)return;
        for(unsigned n=0;n<8;++n){size_t index=0;ymcc::adaptlog::Fact value;if(!virtualAdaptationFacts().take(index,value))break;
            auto result=ymcc::adaptlog::RecordResult::retry;try{value.sample["capturedTickMs"]=value.capturedAt;
                result=virtualAdaptationJournal().recordResult(fspath::path(yemancc_data_dir())/L"virtual-adaptation.log",value.model,value.category.c_str(),value.identity,value.sample,nativeLifecycleTimestamp());}catch(...){}
            virtualAdaptationFacts().finish(index,result!=ymcc::adaptlog::RecordResult::retry);if(result==ymcc::adaptlog::RecordResult::retry){retryAt=now+5000;break;}
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
        json sample={{"observedFrom",event},{"producer",virtualAdaptationSanitized(detail)},{"producerFieldsUnmodifiedExceptTransportSecrets",true},
            {"scope",startup?"first-unique-startup-readback-shape;missing-operation-context-unknown;not-complete-operation-timeline-or-external-game-success":
                "first-step-witness-within-operation;not-complete-timeline-or-external-game-success"}};
        if(startup)sample["operationContextKnown"]=detail.contains("requestId")&&!detail["requestId"].is_null();
        virtualAdaptationFacts().offer({modelAdaptationMachine(),std::move(identity),std::move(sample),"virtual-step-witness"});
    }catch(...){}
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
static json virtualAdaptationStatus(){return {{"file","virtual-adaptation.log"},{"alwaysOn",true},{"maxBytes",8ull*1024*1024},
    {"maxRecords",4096},{"firstFramePolicy","up to8 outcome/neutral/admission combinations per exact context;64 contexts per native process;no eviction"},
    {"facts",virtualAdaptationFacts().status()},{"journal",virtualAdaptationJournal().status()},
    {"frameBuildFailures",g_virtualAdaptationBuildFailures.load()},{"frameSelected",virtualAdaptationSelector().selected()},{"frameCapacityDrops",virtualAdaptationSelector().capacityDrops()},
    {"frameBusyDrops",virtualAdaptationSelector().busyDrops()},{"frameUnknownContextDrops",virtualAdaptationSelector().unknownDrops()},
    {"scope","sample gaps explicit;missing stage is unknown;API/ACK is not external Steam/game/rumble proof"}};}
