static bool msiControllerCauseDiagnosticsRegression(){
    unsigned cases=0,failed=0;std::ostringstream out;auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;out<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    using namespace ymcc::msi::cause;
    check("CTW front process matches case-insensitively",ctwActiveProcess(L"MSI Center M.EXE"));
    check("CTW launcher is activity not only foreground",ctwActiveProcess(L"MSI_Center_M_Launcher.exe"));
    check("ControlMode server recorded but not falsely added to CTW active rule",relevantProcess(L"MSI_Center_M_Server_ControlMode.exe")&&!ctwActiveProcess(L"MSI_Center_M_Server_ControlMode.exe"));
    check("similar process name does not match",!relevantProcess(L"MSI Center M fake.exe"));
    std::array<BYTE,64> frame{};frame[0]=16;frame[3]=60;frame[4]=39;
    for(BYTE mode:{BYTE(1),BYTE(2),BYTE(4)}){frame[5]=mode;auto r=response(frame.data(),64);check("strict CTW mode response is recorded",r.value("modeKnown",false)&&r.at("observedMode")==mode);check("response never grants writer or control proof",r.at("deviceModeVerified")==false&&r.at("modeWriterProven")==false);}
    check("short response never reads mode byte",!response(frame.data(),5).value("modeKnown",true));
    frame[4]=36;check("switch write echo is not a query reply",!response(frame.data(),64).value("modeKnown",true));frame[4]=39;
    frame[5]=3;check("unknown device mode remains unknown",!response(frame.data(),64).value("modeKnown",true));frame[5]=1;
    frame[0]=0;auto loose=response(frame.data(),64);check("CTW loose match retained but unexpected header not signed",loose.at("ctwResponseMatched")==true&&loose.at("modeKnown")==false&&loose.at("reportedMode")==1);frame[0]=16;
    frame[0]=15;check("query report ID 0x0F is not a response report ID",!response(frame.data(),64).value("modeKnown",true));frame[0]=16;
    const std::array<BYTE,16> fieldReply={16,0,0,60,39,1,0,0,0,0,0,0,0,0,0,0};
    auto field=response(fieldReply.data(),fieldReply.size());check("205444 real reply prefix records XInput response",field.value("modeKnown",false)&&field.at("observedMode")==1&&field.at("responseParserVersion")==2);
    auto raw=response(frame.data(),64);check("raw report evidence is bounded not silently converted",raw.at("first16").size()==16&&raw.at("first16")[4]==39);
    ProbeGate good;good.diagnosticBuild=good.detailed=good.knownBoard=good.powerReady=good.routeArmed=good.returnOwed=good.samePhysicalLocation=true;good.epoch=good.anchorEpoch=4;good.power=good.anchorPower=2;
    check("explicit diagnostic lost-source same-location gate opens",probeAllowed(good));
    auto deny=[&](const char* name,auto mutate){auto f=good;mutate(f);check(name,!probeAllowed(f));};
    deny("normal production build issues no query",[](auto& f){f.diagnosticBuild=false;});deny("detail off issues no query",[](auto& f){f.detailed=false;});deny("ROG and unknown board issue no query",[](auto& f){f.knownBoard=false;});deny("exit issues no query",[](auto& f){f.exitFence=true;});deny("suspend/resume unstable phase issues no query",[](auto& f){f.powerReady=false;});deny("disarmed route issues no query",[](auto& f){f.routeArmed=false;});deny("working physical source issues no query",[](auto& f){f.sourceValid=true;});deny("no owned mode debt issues no query",[](auto& f){f.returnOwed=false;});deny("different USB location issues no query",[](auto& f){f.samePhysicalLocation=false;});deny("different Host epoch issues no query",[](auto& f){f.anchorEpoch=3;});deny("unbound Host issues no query",[](auto& f){f.epoch=f.anchorEpoch=0;});deny("different power generation issues no query",[](auto& f){f.anchorPower=1;});
    // Exercise the actual retained-slot reclamation without touching OS handles.
    const auto completion=g_completionForTest;Io* held=new Io;held->pending=true;held->buffer[4]=38;retained()=held;
    g_completionForTest=[](Io*,DWORD&,DWORD& err){err=ERROR_IO_INCOMPLETE;return false;};
    check("pending cancellation retains handle event overlapped and buffer allocation",!reap()&&retained()==held&&retained()->buffer[4]==38);
    g_completionForTest=[](Io* p,DWORD& bytes,DWORD& err){p->pending=false;bytes=0;err=ERROR_OPERATION_ABORTED;return true;};
    check("cancelled IO is reclaimed only after actual terminal receipt",reap()&&retained()==nullptr);g_completionForTest=completion;
    // Actual production wrapper, with no OS/HID/process/service activity.
    const auto machine=g_machineIdentity;const bool detail=g_domainLogEnabled[1].load(),exit=g_exitRequested.load(),fence=g_inputHostExitFence.load(),armed=g_msiDinputRouteDinputArmed.load(),valid=g_msiDinputSourceValid.load(),owed=g_msiDinputReturnOwed.load();const auto power=g_powerGeneration.load(),epoch=g_inputHostEpochMirror.load(),rows=g_msiControllerModeProbeRows.load();const auto phase=g_powerLifecycle.load();
    MsiForegroundIsolationState anchor;{std::lock_guard<std::mutex> lock(g_msiForegroundIsolationMx);anchor=g_msiForegroundIsolation;g_msiForegroundIsolation.epoch=4;g_msiForegroundIsolation.power=2;g_msiForegroundIsolation.mainGeneration=8;g_msiForegroundIsolation.mainLocations={L"fixture:physical-location"};}
    const auto io=g_msiControllerModeProbeIoForTest;unsigned queries=0;g_msiControllerModeProbeIoForTest=[&](){++queries;return json{{"modeKnown",true},{"observedMode",1},{"queryWriteOk",true},{"modeSwitchIssued",false}};};
    const auto oldAppDir=g_appDataDir;const auto emittedDir=fspath::path(g_yemanccDataDir)/(L"cause-diagnostic-native-log-"+std::to_wstring(GetTickCount64()));
    fspath::create_directories(emittedDir);g_nativeDetailLog.close();g_nativeDetailLog.clear();g_appDataDir=emittedDir.wstring();
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";
    g_domainLogEnabled[1]=true;g_exitRequested=false;g_inputHostExitFence=false;g_powerGeneration=2;g_inputHostEpochMirror=4;g_powerLifecycle=PowerLifecycle::Ready;g_msiDinputRouteDinputArmed=true;g_msiDinputSourceValid=false;g_msiDinputReturnOwed=true;
    msiControllerProbeBeforeRearm("route-arm");check("actual wrapper never probes initial start",queries==0);
    msiControllerProbeBeforeRearm("dinput-route-window-resend");check("actual wrapper only probes explicit diagnostic build",queries==(kMsiControllerCauseDiagnosticBuild?1u:0u));
    check("observed mode1 never changes route/debt/source admission",g_msiDinputRouteDinputArmed.load()&&!g_msiDinputSourceValid.load()&&g_msiDinputReturnOwed.load());
    const auto before=queries;g_msiDinputSourceValid=true;msiControllerProbeBeforeRearm("dinput-route-window-resend");check("actual wrapper skips live source",queries==before);g_msiDinputSourceValid=false;
    g_machineIdentity.family=YmccFamily::AsusRogAlly;msiControllerProbeBeforeRearm("dinput-route-window-resend");check("actual wrapper skips ROG",queries==before);
    check("query event does not bypass detailed gate",!nativeLifecycleEventIsBase("msi-controller-mode-probe")&&!nativeLifecycleRecordIsStreaming("msi-controller-mode-probe",json::object()));
    g_nativeDetailLog.flush();g_nativeDetailLog.close();g_nativeDetailLog.clear();
    std::vector<json> emitted;std::ifstream emittedFile(emittedDir/L"native-detail.log");std::string line;
    while(std::getline(emittedFile,line)){auto r=json::parse(line,nullptr,false);if(!r.is_discarded()&&r.value("event","")=="msi-controller-mode-probe")emitted.push_back(r);}
    check("real detailed-file writer carries query and skipped-gate records",emitted.size()==(kMsiControllerCauseDiagnosticBuild?2u:0u));
    if(kMsiControllerCauseDiagnosticBuild&&emitted.size()==2){check("emitted observed-mode receipt is not control authority",emitted[0].at("io").at("observedMode")==1&&emitted[0].at("deviceModeVerified")==false&&emitted[0].at("controlModeSwitchIssued")==false);check("real writer attaches process and local timestamp",emitted[0].contains("time")&&emitted[0].at("pid")==GetCurrentProcessId());}
    g_appDataDir=oldAppDir;
    g_machineIdentity=machine;g_domainLogEnabled[1]=detail;g_exitRequested=exit;g_inputHostExitFence=fence;g_powerGeneration=power;g_inputHostEpochMirror=epoch;g_powerLifecycle=phase;g_msiDinputRouteDinputArmed=armed;g_msiDinputSourceValid=valid;g_msiDinputReturnOwed=owed;g_msiControllerModeProbeRows=rows;g_msiControllerModeProbeIoForTest=io;{std::lock_guard<std::mutex> lock(g_msiForegroundIsolationMx);g_msiForegroundIsolation=anchor;}
    out<<"cases="<<cases<<" failed="<<failed<<"\n";std::ofstream file(fspath::path(g_yemanccDataDir)/L"msi-controller-cause-diagnostics-regression.txt");file<<out.str();return failed==0&&file.good();
}
