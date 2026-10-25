// This file orchestrates actual extracted production methods. The glue that
// places wrapper-returned time into a Sample is fixture code; WM_INPUT and full
// physicalHidCaptureRawInput remain explicitly source-only anchors.
static std::optional<std::uint64_t> queueReceipt(std::uint64_t processingNow,std::uint32_t messageBits) {
    clockMs=processingNow;queuedMessageTime32=messageBits;
    const auto messageBefore=messageTimeCalls,uptimeBefore=uptimeCalls;
    const auto order=queueClockCalls.size();const auto receipt=legionMotionMessageReceiptTick();
    CHECK(messageTimeCalls==messageBefore+1&&uptimeCalls==uptimeBefore+1);
    CHECK(queueClockCalls.size()==order+2);
    if(queueClockCalls.size()>=order+2)CHECK(queueClockCalls[order]=="message"&&queueClockCalls[order+1]=="now");
    effects.push_back({{"case",activeCase},{"event","queue-clock-wrapper"},{"processingNow",processingNow},
        {"messageTime32",messageBits},{"receivedAt",receipt?json(*receipt):json(nullptr)}});
    return receipt;
}
static void queueClockWrapperScope() {
    const auto beforeHandle=g_gm;const std::uint64_t ring=std::uint64_t{1}<<32;
    for(const auto family:{YmccFamily::Unknown,YmccFamily::RogAlly,YmccFamily::MsiClaw,YmccFamily::Gpd}){
        g_machineIdentity.family=family;const auto messages=messageTimeCalls,uptimes=uptimeCalls;
        CHECK(!legionMotionMessageReceiptTick());CHECK(messageTimeCalls==messages&&uptimeCalls==uptimes);
    }
    g_machineIdentity.family=YmccFamily::LenovoLegionGo;
    for(const auto name:{"HandheldCompanion.Devices.LegionGo","HandheldCompanion.Devices.LegionGoTabletExtra","HandheldCompanion.Devices.LegionGoSZ3",""}){
        g_machineIdentity.deviceClass=name;const auto messages=messageTimeCalls,uptimes=uptimeCalls;
        CHECK(!legionMotionMessageReceiptTick());CHECK(messageTimeCalls==messages&&uptimeCalls==uptimes);
    }
    for(const auto& model:familyTopology.at("models")){
        g_machineIdentity.deviceClass=model.at("name").get<std::string>();
        const auto receipt=queueReceipt(ring+5,0xfffffffeu);CHECK(receipt&&*receipt==ring-2);
    }
    CHECK(g_gm==beforeHandle&&g_gm==windowsSentinel&&createCalls==0&&processCalls==0&&calibrationPathCalls==0);
}
static void queueClockAgeWindow() {
    const auto window=ymcc::kVirtualOutputSourceWindowMaxAgeMs;
    auto receipt=queueReceipt(1000+window,1000);CHECK(receipt&&*receipt==1000);
    CHECK(!queueReceipt(1001+window,1000));CHECK(!queueReceipt(1000,1001));
    const std::uint64_t ring=std::uint64_t{1}<<32;
    receipt=queueReceipt(ring+5,0xfffffffeu);CHECK(receipt&&*receipt==ring-2);
    receipt=queueReceipt(ring+5,0);CHECK(receipt&&*receipt==ring);
    receipt=queueReceipt(0,0);CHECK(receipt&&*receipt==0);
    if(receipt){const auto zero=sample(*receipt);CHECK(!publish(zero));CHECK(!legionMotionSubmissionCurrent(zero,clockMs));}
    CHECK(!queueReceipt(0,1));CHECK(createCalls==0&&processCalls==0);
}
static void queuedMessagePublishCutover() {
    clockMs=1000;const auto oldEpoch=epoch();legionMotionRevoke();
    CHECK(epoch()==oldEpoch+1&&g_legionMotionEpochStartedAt.load()==1000);
    const auto queued=queueReceipt(1010,999);CHECK(queued&&*queued==999);
    if(queued){const auto late=sample(*queued);CHECK(late.source.captureEpoch==epoch());CHECK(!publish(late));}
    CHECK(!legionMotionSnapshot(clockMs)&&createCalls==0);
    const auto equal=queueReceipt(1010,1000);CHECK(equal&&*equal==1000);
    if(equal){const auto in=sample(*equal);CHECK(!publish(in));CHECK(!legionMotionSubmissionCurrent(in,clockMs));
        CHECK(!legionMotionSnapshot(clockMs));CHECK(!legionMotionSubmissionCurrent(in,clockMs));}
    const auto after=queueReceipt(1010,1001);CHECK(after);
    if(after){const auto in=sample(*after);CHECK(consume(in,1.1).has_value());const auto snap=legionMotionSnapshot(clockMs);
        CHECK(snap&&snap->capturedAt==1001&&snap->capturedAt!=clockMs);CHECK(legionMotionSubmissionCurrent(in,clockMs));}
}
static void cutoverOnlySnapshotAndFinalGuards() {
    clockMs=2000;const auto in=sample(clockMs);CHECK(consume(in).has_value());auto pending=frame(&in);
    CHECK(inputHostFrameSubmissionMayIssue(&pending));const auto unchangedEpoch=epoch();
    // Controlled interleave: production stores cutover before captureEpoch.
    // Hold epoch/lease/source constant to prove each timestamp guard independently.
    clockMs=2002;g_legionMotionEpochStartedAt.store(2001);
    CHECK(epoch()==unchangedEpoch&&g_legionMotion.current(in.context,in.source,true,clockMs,ymcc::kVirtualOutputSourceWindowMaxAgeMs).has_value());
    CHECK(!legionMotionSnapshot(clockMs));CHECK(!legionMotionSubmissionCurrent(in,clockMs));
    guardsRejectFrame(pending);
    {auto requestOnly=frame(&in);InputFrameSubmissionScope scope(&requestOnly);json response;
        CHECK(!inputHostFrameSubmissionRequestCurrent("SUBMIT_FRAME",&response,"cutover-request-only"));
        CHECK(requestOnly.rejected&&requestOnly.reason==fsb::Verdict::SourceRetired);CHECK(response.is_object()&&response.value("deviceWriteAttempted",true)==false);}
    const auto equal=sample(2001);CHECK(!publish(equal));CHECK(!legionMotionSnapshot(clockMs));CHECK(!legionMotionSubmissionCurrent(equal,clockMs));
    {auto uncertain=frame(&equal);guardsRejectFrame(uncertain);}
    // Keep a previously accepted sample but move only the cutover to equality.
    g_legionMotionEpochStartedAt.store(2000);
    CHECK(!legionMotionSnapshot(clockMs));CHECK(!legionMotionSubmissionCurrent(in,clockMs));
    {auto uncertain=frame(&in);guardsRejectFrame(uncertain);}
    g_legionMotionEpochStartedAt.store(2001);
    const auto nextTick=sample(2002);CHECK(consume(nextTick).has_value());CHECK(legionMotionSnapshot(clockMs));CHECK(legionMotionSubmissionCurrent(nextTick,clockMs));
    {auto fresh=frame(&nextTick);InputFrameSubmissionScope scope(&fresh);json response;CHECK(inputHostFrameSubmissionMayIssue(&fresh));CHECK(inputHostFrameSubmissionRequestCurrent("SUBMIT_FRAME",&response,"cutover-next-tick"));}
}
static void sideSwitchQueueCutover() {
    unsigned step=0;
    for(const auto& model:familyTopology.at("models")){
        if(!model.at("rightAllowed").get<bool>())continue;
        g_machineIdentity.deviceClass=model.at("name").get<std::string>();selectSensor(0);
        clockMs=4000+100*step++;const auto cutover=clockMs,before=epoch();
        CHECK(legionMotionSelectSensorIndex(1,g_legionMotionSelectionRevision+1));
        CHECK(epoch()==before+1&&g_legionMotionEpochStartedAt.load()==cutover);
        auto receipt=queueReceipt(cutover+5,static_cast<std::uint32_t>(cutover-1));CHECK(receipt);
        if(receipt){const auto late=sample(*receipt);CHECK(late.source.sensorIndex==1&&late.source.captureEpoch==epoch());CHECK(!publish(late));}
        receipt=queueReceipt(cutover+5,static_cast<std::uint32_t>(cutover));CHECK(receipt);
        MotionSample old=sample(cutover);if(receipt){old=sample(*receipt);CHECK(!publish(old));CHECK(!legionMotionSubmissionCurrent(old,clockMs));}
        receipt=queueReceipt(cutover+5,static_cast<std::uint32_t>(cutover+1));CHECK(receipt);
        if(receipt){old=sample(*receipt);CHECK(consume(old).has_value());}
        clockMs=cutover+10;CHECK(legionMotionSelectSensorIndex(0,g_legionMotionSelectionRevision+1));
        CHECK(g_legionMotionEpochStartedAt.load()==cutover+10&&!publish(old));
        receipt=queueReceipt(cutover+11,static_cast<std::uint32_t>(cutover+9));if(receipt)CHECK(!publish(sample(*receipt)));else CHECK(false);
        receipt=queueReceipt(cutover+11,static_cast<std::uint32_t>(cutover+10));if(receipt){CHECK(!publish(sample(*receipt)));CHECK(!legionMotionSubmissionCurrent(sample(*receipt),clockMs));}else CHECK(false);
        receipt=queueReceipt(cutover+11,static_cast<std::uint32_t>(cutover+11));if(receipt)CHECK(consume(sample(*receipt)).has_value());else CHECK(false);
    }
}
static void registrationQueueCutover() {
    const auto target=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x44440000));
    clockMs=6000;const auto before=epoch();CHECK(legionMotionRegisterRawInput(target));
    CHECK(epoch()==before+1&&g_legionMotionEpochStartedAt.load()==6000);
    auto receipt=queueReceipt(6005,5999);if(receipt)CHECK(!publish(sample(*receipt)));else CHECK(false);
    receipt=queueReceipt(6005,6000);if(receipt){CHECK(!publish(sample(*receipt)));CHECK(!legionMotionSubmissionCurrent(sample(*receipt),clockMs));}else CHECK(false);
    receipt=queueReceipt(6005,6001);if(receipt)CHECK(consume(sample(*receipt)).has_value());else CHECK(false);
    clockMs=6100;vendorRegistrationResult=false;CHECK(!legionMotionRegisterRawInput(target));
    CHECK(g_legionMotionEpochStartedAt.load()==6100&&!g_legionRawInputSubscribed.load());
    receipt=queueReceipt(6105,6100);if(receipt)CHECK(!publish(sample(*receipt)));else CHECK(false);
    clockMs=6200;vendorRegistrationResult=true;CHECK(legionMotionRegisterRawInput(target));
    CHECK(g_legionMotionEpochStartedAt.load()==6200&&g_legionRawInputSubscribed.load());
    receipt=queueReceipt(6205,6199);if(receipt)CHECK(!publish(sample(*receipt)));else CHECK(false);
    receipt=queueReceipt(6205,6200);if(receipt){CHECK(!publish(sample(*receipt)));CHECK(!legionMotionSubmissionCurrent(sample(*receipt),clockMs));}else CHECK(false);
    receipt=queueReceipt(6205,6201);if(receipt)CHECK(consume(sample(*receipt)).has_value());else CHECK(false);
}
static void queueCutoverAcross32Wrap() {
    const std::uint64_t ring=std::uint64_t{1}<<32;
    clockMs=ring-2;legionMotionRevoke();CHECK(g_legionMotionEpochStartedAt.load()==ring-2);
    auto receipt=queueReceipt(ring-1,0xfffffffdu);CHECK(receipt&&*receipt==ring-3);if(receipt)CHECK(!publish(sample(*receipt)));
    receipt=queueReceipt(ring-1,0xfffffffeu);CHECK(receipt&&*receipt==ring-2);if(receipt)CHECK(!publish(sample(*receipt)));
    receipt=queueReceipt(ring-1,0xffffffffu);CHECK(receipt&&*receipt==ring-1);if(receipt)CHECK(consume(sample(*receipt)).has_value());
    receipt=queueReceipt(ring+1,0);CHECK(receipt&&*receipt==ring);if(receipt)CHECK(consume(sample(*receipt)).has_value());
    clockMs=ring+2;CHECK(legionMotionSelectSensorIndex(1,g_legionMotionSelectionRevision+1));CHECK(g_legionMotionEpochStartedAt.load()==ring+2);
    receipt=queueReceipt(ring+5,1);CHECK(receipt&&*receipt==ring+1);if(receipt)CHECK(!publish(sample(*receipt)));
    receipt=queueReceipt(ring+5,2);CHECK(receipt&&*receipt==ring+2);if(receipt){CHECK(!publish(sample(*receipt)));CHECK(!legionMotionSubmissionCurrent(sample(*receipt),clockMs));}
    receipt=queueReceipt(ring+5,3);CHECK(receipt&&*receipt==ring+3);if(receipt)CHECK(consume(sample(*receipt)).has_value());
}
static void otherRoutesIgnoreLegionCutover() {
    g_legionMotionEpochStartedAt.store(std::numeric_limits<std::uint64_t>::max());
    for(const auto family:{YmccFamily::Unknown,YmccFamily::RogAlly,YmccFamily::MsiClaw,YmccFamily::Gpd}){
        g_machineIdentity.family=family;const auto messages=messageTimeCalls,uptimes=uptimeCalls;
        CHECK(!legionMotionMessageReceiptTick()&&messageTimeCalls==messages&&uptimeCalls==uptimes);
        auto normal=frame(nullptr);InputFrameSubmissionScope scope(&normal);json response;
        CHECK(inputHostFrameSubmissionMayIssue(&normal));CHECK(inputHostFrameSubmissionRequestCurrent("SUBMIT_FRAME",&response,"other-family-clock"));
    }
    CHECK(g_gm==windowsSentinel&&createCalls==0&&processCalls==0&&calibrationPathCalls==0);
}
