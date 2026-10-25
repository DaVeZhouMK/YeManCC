static bool msiControllerTransitionLoggingRegression(){
    unsigned cases=0,failed=0;std::ostringstream report;auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;report<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    const auto machine=g_machineIdentity;const bool detailed=g_domainLogEnabled[1].load();
    const auto oldInventory=g_msiControllerTraceInventoryForTest;const auto oldSink=g_msiControllerTraceEmitForTest;
    const auto oldOfficial=g_msiControllerOfficialStackForTest;unsigned officialQueries=0;
    g_msiControllerOfficialStackForTest=[&](){++officialQueries;return json{{"modeWriterProven",false},{"ctwInactiveVerified",false}};};
    const auto oldSequence=g_msiControllerTraceSequence.load();unsigned probes=0;std::vector<json> rows;
    g_msiControllerTraceInventoryForTest=[&](){++probes;return json{{"known",true},{"complete",true},{"interfaces",json::array({{{"product","؉"},{"productUtf16",json::array({1545})},{"interfacePath","fixture:exact-msi-metadata"}}})}};};
    g_msiControllerTraceEmitForTest=[&](const json& row){rows.push_back(row);};
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";
    const bool armed=g_msiDinputRouteDinputArmed.load(),confirmed=g_msiDinputRouteConfirmed.load(),valid=g_msiDinputSourceValid.load(),owed=g_msiDinputReturnOwed.load();
    g_domainLogEnabled[1]=false;msiControllerTraceLog("disabled",json::object(),true);
    check("detail off emits nothing and opens no metadata API",rows.empty()&&probes==0&&officialQueries==0);
    g_domainLogEnabled[1]=true;
    for(const char* board:{"MS-1T41","MS-1T42","MS-1T52","MS-1T8K","MS-1T91"}){g_machineIdentity.product=board;const auto before=rows.size();msiControllerTraceLog("mode2-write-end",{{"sent",true},{"deviceModeVerified",false}},true);check("known MSI boards admit diagnostic only",rows.size()==before+1);}
    check("raw product code units remain exact",rows.back().at("physicalNamespace").at("interfaces")[0].at("productUtf16")==json::array({1545}));
    check("write receipt never turns into device mode proof",rows.back().at("facts").at("sent")==true&&rows.back().at("facts").at("deviceModeVerified")==false);
    check("external Steam correspondence stays explicitly unverified",rows.back().at("externalSteamInterfaceMapping").get<std::string>().starts_with("unverified"));
    check("diagnostic permission never authorizes hiding",rows.back().at("traceChangesControlPolicy")==false&&rows.back().at("diagnosticOnly")==true&&!rows.back().contains("hideAuthorized"));
    check("steady source context is labelled stable not assumed external visible",rows.back().at("contextStableAcrossInventory")==true);
    const auto beforeOfficial=officialQueries;
    check("official stack snapshot never claims a mode writer or inactivity",rows.back().at("officialStack").at("modeWriterProven")==false&&rows.back().at("officialStack").at("ctwInactiveVerified")==false);
    const auto beforeProbe=probes;msiControllerTraceLog("source-published",json::object(),false);
    check("BUS binding trace performs no metadata inventory I/O",probes==beforeProbe&&officialQueries==beforeOfficial&&rows.back().at("inventoryRequested")==false&&!rows.back().contains("physicalNamespace"));
    const auto sequence=rows.back().at("traceSequence").get<uint64_t>();msiControllerTraceLog("source-revoked",json::object(),false);check("timeline sequence increases without controlling a session",rows.back().at("traceSequence").get<uint64_t>()==sequence+1);
    check("trace leaves all existing MSI authority bits unchanged",g_msiDinputRouteDinputArmed.load()==armed&&g_msiDinputRouteConfirmed.load()==confirmed&&g_msiDinputSourceValid.load()==valid&&g_msiDinputReturnOwed.load()==owed);
    for(const auto family:{YmccFamily::AsusRogAlly,YmccFamily::AsusRogAllyX,YmccFamily::AsusRogAllyClassic,YmccFamily::AsusRogAllyXClassic,YmccFamily::Unknown}){g_machineIdentity.family=family;const auto before=rows.size();const auto beforeQueries=probes;msiControllerTraceLog("non-msi",json::object(),true);check("ROG/common gain no trace rows or metadata queries",rows.size()==before&&probes==beforeQueries);}
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.product="MS-unknown";const auto size=rows.size();msiControllerTraceLog("unknown",json::object(),true);check("unknown MSI board refuses diagnostics expansion",rows.size()==size);
    g_machineIdentity.product="MS-1T91";g_machineIdentity.manufacturer="ASUSTeK COMPUTER INC.";msiControllerTraceLog("spoofed-board",json::object(),true);check("wrong manufacturer with MSI board text refuses",rows.size()==size);
    check("new detailed event is not resident or streaming",!nativeLifecycleEventIsBase("msi-controller-transition")&&!nativeLifecycleRecordIsStreaming("msi-controller-transition",json::object()));
    HidHideVisibilitySnapshot applicationSnap;applicationSnap.valid=true;applicationSnap.inverseRaw="--inv-off";applicationSnap.applications={L"C:\\SOFT\\YeMan\\YeManCC.exe",L"C:\\测试\\YeManControllerVisibilityProbe.exe"};
    const auto applicationFacts=msiControllerVisibilityApplications(applicationSnap,true);
    check("existing snapshot application paths remain exact including Unicode",applicationFacts["paths"].size()==2 && applicationFacts["paths"][1]==W2U(applicationSnap.applications[1]));
    check("inverse off is parsed but never asserts Steam or observer exemption",applicationFacts["inverseState"]=="off" && applicationFacts["resolvedSteamExemption"]=="unknown" && applicationFacts["resolvedObserverExemption"]=="unknown" && applicationFacts["externalSuppression"]=="unverified");
    const auto unknownApplications=msiControllerVisibilityApplications(applicationSnap,false);
    check("failed readback cannot reuse stale application paths or assert inverse off",unknownApplications["paths"].is_null() && unknownApplications["inverseState"]=="unknown" && unknownApplications["readbackKnown"]==false);
    applicationSnap.inverseRaw="";check("missing inverse remains unknown",msiControllerVisibilityApplications(applicationSnap,true)["inverseState"]=="unknown");
    applicationSnap.inverseRaw="--inv-on";check("inverse on is recognized without authorizing a policy change",msiControllerVisibilityApplications(applicationSnap,true)["inverseState"]=="on");
    check("ordinary builds and non-MSI/detailed-off cannot launch observer",!ymcc::msi::visibility::observerEligible(false,true,true) && !ymcc::msi::visibility::observerEligible(true,false,true) && !ymcc::msi::visibility::observerEligible(true,true,false));
    check("candidate eligibility has exactly three gates",ymcc::msi::visibility::observerEligible(true,true,true));
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.product="MS-1T91";g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";
    const auto beforeApplicationTrace=probes;msiControllerTraceLog("hide-targets-readback",{{"applicationSnapshot",applicationFacts}},false);
    check("actual application snapshot trace adds no physical inventory I/O",probes==beforeApplicationTrace && rows.back()["facts"]["applicationSnapshot"]==applicationFacts);
    g_machineIdentity=machine;g_domainLogEnabled[1]=detailed;g_msiControllerTraceInventoryForTest=oldInventory;g_msiControllerTraceEmitForTest=oldSink;g_msiControllerTraceSequence=oldSequence;g_msiControllerOfficialStackForTest=oldOfficial;
    report<<"cases="<<cases<<" failed="<<failed<<"\n";std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-controller-transition-logging-regression.txt");out<<report.str();return failed==0&&out.good();
}
