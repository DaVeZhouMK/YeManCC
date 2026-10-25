#pragma once
// Included after the actual native TU by the offline runner only.
static bool replayMsiMotionEvidence(const std::wstring& fixturePath) {
    json fixture;std::ifstream input{fspath::path(fixturePath)};if(!input)return false;input>>fixture;
    json checks=json::array();auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"ok",ok}});};
    auto identity=[](const json& row){MachineIdentity result;result.resolved=row.value("resolved",false);result.family=YmccFamily::MsiClaw;
        result.manufacturer=row.value("manufacturer",std::string());result.product=row.value("product",std::string());
        result.systemProduct=row.value("systemProduct",std::string());result.deviceClass=row.value("deviceClass",std::string());
        result.gyroMatrixId=row.value("gyroMatrixId",std::string());result.accelMatrixId=row.value("accelMatrixId",std::string());return result;};
    const MachineIdentity a2=identity(fixture.at("a2Identity"));const MachineIdentity g3=identity(fixture.at("g3Identity"));
    check("recorded 258V and G3E select distinct actual capability library entries",a2.product=="MS-1T52"&&g3.product=="MS-1T91"&&ymcc::msi_sensor::motionCapability(a2.deviceClass,a2.product)!=ymcc::msi_sensor::motionCapability(g3.deviceClass,g3.product));
    const auto& a2Init=fixture.at("a2Init");const auto& g3Init=fixture.at("g3Init");
    check("258V archive still proves real WinRT gyro and absent default accel",a2Init.value("gyro",false)&&!a2Init.value("accel",false)&&a2Init.value("sensorFamily",std::string())=="winrt");
    check("G3E archive proves absent default typed sensors, not invented COM data",!g3Init.value("gyro",true)&&!g3Init.value("accel",true));
    check("actual fallback plan fills 258V missing component and G3E missing pair",inputCaptureMsiLegacyNeeded(a2,true,false)&&inputCaptureMsiLegacyNeeded(g3,false,false)&&!inputCaptureMsiLegacyNeeded(a2,true,true));
    HcMotionSelectorReceipt a2Receipt,g3Receipt;
    check("actual selector now admits both recorded MSI identities",inputCaptureResolveMsiMotionSelector(a2,a2Receipt)&&inputCaptureResolveMsiMotionSelector(g3,g3Receipt));
    g_machineIdentity=a2;g_hcMotionSelector=a2Receipt;
    {std::lock_guard<std::mutex> lock(g_sensorMetaMx);g_sensorMeta=InputCaptureSensorMeta{};g_sensorMeta.gyroFamily="winrt";}
    InputCaptureSensorSink sink;UINT64 generation=0;size_t samples=0;bool exact=true,matrix=true,timestamps=true;
    for(const auto& row:fixture.at("a2Samples")) {
        const UINT64 current=row.at("generation").get<UINT64>();if(current!=generation){sink.ResetSamples();sink.SetBindingGeneration(current);generation=current;}
        const auto& raw=row.at("raw");const double x=raw.at("x"),y=raw.at("y"),z=raw.at("z");
        sink.OnRawGyro(x,y,z,row.at("timestamp").get<UINT64>(),row.at("receipt").get<ULONGLONG>(),generation);
        exact&=sink.gyroOk&&sink.gx==x&&sink.gy==y&&sink.gz==z;timestamps&=sink.gyroTimestamp100ns==row.at("timestamp").get<UINT64>();
        double gx,gy,gz,ax,ay,az;inputCaptureApplyHcMatrixCandidate(x,y,z,0,0,0,gx,gy,gz,ax,ay,az);
        const auto& candidate=row.at("candidate");matrix&=gx==candidate.at("gx").get<double>()&&gy==candidate.at("gy").get<double>()&&gz==candidate.at("gz").get<double>();++samples;
    }
    check("all recorded 258V raw samples retain values and device timestamps through actual sink",samples>1000&&exact&&timestamps);
    check("all recorded 258V established WinRT matrix candidates are unchanged",matrix);
    check("258V recorded absence of accel is not upgraded into false paired output",!inputCaptureMotionPairAdmitted(true,true,true,false,true,generation,generation,inputCaptureMotionSelectorBound(),generation));
    g_machineIdentity=g3;g_hcMotionSelector=g3Receipt;
    g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;
    g_msiDinput.enabled.store(true);g_msiDinput.productId.store(0x1902);g_msiDinput.mainInterfaceMatched.store(true);
    g_msiDinputRouteDinputArmed.store(false);g_msiDinputRouteConfirmed.store(false);g_msiDinputSourceValid.store(false);
    g_xinputPhysicalSourceLocked=false;g_xinputPhysicalSourceMask=0;g_xinputConnectedMask=0;
    g_inputHostExitFence.store(false);g_physicalReleaseWindowNeutral.store(false);
    inputHostLateHideStateRestoreForTest(InputHostLateHideStateSnapshot{});
    {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue.clear();}
    {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);g_msiMainSource=HidHideMsiMainSourceRecord{};}
    bool rejectedZero=false,heldOldSlot=false,newGenerationQueued=false;
    for(const auto& row:fixture.at("g3Events")) {
        const std::string event=row.value("event",std::string());const std::string phase=row.value("phase",std::string());
        if(event=="input-host-started"){g_inputHostRunId=row.at("runId").get<std::string>();g_inputHostEpoch=row.at("epoch").get<unsigned long long>();}
        if(event=="msi-claw-dinput-route"&&phase=="armed")g_msiDinputRouteDinputArmed.store(true);
        if(event=="gamepad-physical-source-admission"&&row.value("policy",std::string())=="hc-single-selected-pre-target-source-bus") {
            g_xinputPhysicalSourceMask=row.at("sourceMask").get<DWORD>();g_xinputPhysicalSourceLocked=true;g_xinputConnectedMask=g_xinputPhysicalSourceMask;
            heldOldSlot=!inputHostPhysicalPublicationSourceAdmitted(true);
        }
        if(event=="late-hide-exec-decision") {
            const auto epoch=row.at("hostGen").get<unsigned long long>();const auto run=row.at("runId").get<std::string>();
            rejectedZero=std::string(inputHostLateHideExecDecisionFor(false,false,true,run,epoch,run,epoch,true,false,row.at("recordGen").get<unsigned long long>(),0,false))==row.at("decision").get<std::string>();
            g_xinputTopologyEpoch.store(row.at("sourceGen").get<unsigned long long>());
        }
        if(event=="msi-main-source-record"&&phase=="published") {
            HidHideMsiMainSourceRecord record;record.valid=true;record.generation=row.at("generation").get<unsigned>();record.interfacePath=row.at("interfacePath").get<std::string>();
            record.usbInstance=row.value("usbInstance",std::string());record.vendorId=row.at("vendorId").get<unsigned>();record.productId=row.at("productId").get<unsigned>();
            std::lock_guard<std::mutex> lock(g_msiMainSourceMx);g_msiMainSource=record;
        }
        if(event=="msi-claw-dinput-route"&&phase=="confirmed") {
            g_msiDinputRouteConfirmed.store(true);g_msiDinputSourceValid.store(true);inputHostCoordinatorLateSourceVisibilityTick(true,true);
            InputHostLateHidePendingView view;inputHostLateHidePendingRead(&view);
            newGenerationQueued=view.hasPending&&view.episode.recordGen==2&&view.episode.runId==g_inputHostRunId&&g_xinputPhysicalSourceLocked;
        }
    }
    check("recorded generation-zero source refusal remains a safe refusal",rejectedZero);
    check("recorded old PID1901 source slot is held while mode2 unconfirmed",heldOldSlot);
    check("recorded later generation2 now queues via actual BUS gate despite old lock",newGenerationQueued);
    check("recorded confirmed current DInput source may resume publication",inputHostPhysicalPublicationSourceAdmitted(true));
    const bool ok=std::all_of(checks.begin(),checks.end(),[](const auto& c){return c.at("ok").template get<bool>();});
    json report={{"ok",ok},{"checks",checks},{"a2SamplesReplayed",samples},{"g3EventsReplayed",fixture.at("g3Events").size()},
        {"inputHashes",fixture.at("inputs")},{"hardwareVerified",false},{"limitations",fixture.at("limitations")}};
    std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-real-log-replay-result.json");out<<report.dump(2);return ok&&out.good();
}
