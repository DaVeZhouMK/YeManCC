// Offline actual-main TU; inert source/metadata functions only, no product entry,
// SDK backend, pipe, observer, HID/registry read/write or real log sink.
#define YMCC_MSI_FOREGROUND_ISOLATION_TEST 1
#define YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC 1
#define YMCC_MSI_REPORT_CHAIN_TEST 1
#define YMCC_MSI_VISIBILITY_DIAGNOSTIC 1
#include <iostream>
#include "../native/main.cpp"
int wmain() {
    using namespace ymcc::msi::virtualReportDiag;
    unsigned n=0,failed=0;
    auto check=[&](const char* name,bool ok){++n;failed+=!ok;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';};
    std::vector<json> logs;
    g_msiVirtualReportTestLog=[&](const char* event,const json& body){logs.push_back({{"event",event},{"detail",body}});};
    const auto setup=[&] {
        g_msiVirtualReportDiagnostic.Close();g_msiVirtualReportDiagnostic.ClearHost();logs.clear();
        g_machineIdentity.family=YmccFamily::MsiClaw;
        g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";
        g_machineIdentity.product="ClawCG3EM | MS-1T91";
        g_machineIdentity.deviceClass="HandheldPC";
        g_inputReady.store(true);g_powerLifecycle.store(PowerLifecycle::Ready);
        g_exitRequested.store(false);g_inputHostExitFence.store(false);g_inputHostPowerAdmissionRevoked.store(false);
        g_domainLogEnabled[1].store(true);
        g_inputHostRunId="native-pair-fake-run";g_inputHostEpoch=701;g_inputHostBoundPowerGeneration=17;
        g_inputHostStartupNonce="fake-startup";g_inputHostConfigRevision=1;
        inputHostSetBoundPersona("dualsense-edge");g_inputHostSequence=23;g_inputHostSampleSequence=41;
    };
    const auto hello=[](const json& cap) {inputHostApplyHelloCapabilities({{"detail","{\"hidhideStateQuery\":true}"},{"msiReportChainDiagnosticVersion",cap}});};
    const auto prepare=[&] { auto t=inputHostEnvelopeLocked("PREPARE_TARGET",false).dump();inputHostMsiReportDiagnosticExtendRequest("PREPARE_TARGET",t);return t;};
    std::string exportedPrepareBase,exportedPrepareWithDiagnostic;
    const auto authorize=[&](const char* id) {g_msiVirtualReportDiagnostic.Request(id);hello(1);exportedPrepareBase=inputHostEnvelopeLocked("PREPARE_TARGET",false).dump();exportedPrepareWithDiagnostic=prepare();const auto p=json::parse(exportedPrepareWithDiagnostic);check("explicit accepted offer only",p.contains("msiReportChainDiagnostic"));inputHostMsiReportDiagnosticAcceptPrepare(true,{{"msiReportChainDiagnosticArmed",true},{"msiReportChainDiagnosticSessionId",id}});};
    setup();hello(1);const auto ordinary=inputHostEnvelopeLocked("PREPARE_TARGET",false).dump();auto unchanged=ordinary;inputHostMsiReportDiagnosticExtendRequest("PREPARE_TARGET",unchanged);
    check("capability and detailed log alone do not arm",unchanged==ordinary && !g_msiVirtualReportDiagnostic.active() && logs.empty());
    check("existing slim capability unchanged",g_inputHostHidHideStateQuery);
    for(const json cap:{json(nullptr),json("1"),json(true),json(2),json(-1)}) {setup();g_msiVirtualReportDiagnostic.Request("old-host");hello(cap);check("old malformed unsupported Host gets no new field",!json::parse(prepare()).contains("msiReportChainDiagnostic"));}
    setup();g_msiVirtualReportDiagnostic.Request("ROG-denied");hello(1);g_machineIdentity.family=YmccFamily::AsusRogAllyClassic;check("ROG cannot authorize MSI metadata",!json::parse(prepare()).contains("msiReportChainDiagnostic"));
    setup();g_msiVirtualReportDiagnostic.Request("GPD-denied");hello(1);g_machineIdentity.manufacturer="GPD";check("non-MSI manufacturer cannot authorize",!json::parse(prepare()).contains("msiReportChainDiagnostic"));
    setup();g_msiVirtualReportDiagnostic.Request("log-off");hello(1);g_domainLogEnabled[1].store(false);check("virtual detailed gate false gets original PREPARE",!json::parse(prepare()).contains("msiReportChainDiagnostic"));
    setup();g_msiVirtualReportDiagnostic.Request("exit-fence");hello(1);g_inputHostExitFence.store(true);check("exit fence never offers",!json::parse(prepare()).contains("msiReportChainDiagnostic"));
    setup();g_msiVirtualReportDiagnostic.Request("power-gate");hello(1);g_powerLifecycle.store(PowerLifecycle::Suspended);check("power lifecycle non-ready never offers",!json::parse(prepare()).contains("msiReportChainDiagnostic"));
    setup();authorize("same-source-session");check("native bound explicit ACK arms",g_msiVirtualReportDiagnostic.active());
    const auto seq=g_inputHostSequence,sample=g_inputHostSampleSequence;
    g_msiDinput.bindCount=9;
    XINPUT_GAMEPAD original{};original.wButtons=XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_LEFT_SHOULDER;original.bLeftTrigger=111;original.bRightTrigger=7;original.sThumbLX=-1234;original.sThumbLY=321;original.sThumbRX=-41;original.sThumbRY=999;
    const auto captured=GetTickCount64();msiDinputPublishCanonicalFrame(original,captured);
    XINPUT_GAMEPAD snapshot{};SourceStamp stamp;
    check("real canonical snapshot succeeds",msiDinputCanonicalSnapshot(snapshot,&stamp));
    check("stamp source generation is same published binding not Host epoch",stamp.valid && stamp.generation==9 && stamp.generation!=g_inputHostEpoch && stamp.sequence>0);
    check("raw stamp data matches EXACT canonical sample",stamp.buttons==snapshot.wButtons && stamp.leftTrigger==snapshot.bLeftTrigger && stamp.rightTrigger==snapshot.bRightTrigger && stamp.lx==snapshot.sThumbLX && stamp.ry==snapshot.sThumbRY && stamp.capturedTick==captured);
    original.wButtons=XINPUT_GAMEPAD_B;g_msiDinput.bindCount=10;msiDinputPublishCanonicalFrame(original,captured+1);
    check("later publish cannot relabel already-captured sample",stamp.generation==9 && stamp.buttons==(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_LEFT_SHOULDER));
    SourceStamp newer;check("next sample has its own generation and ordinal",msiDinputCanonicalSnapshot(snapshot,&newer) && newer.generation==10 && newer.sequence>stamp.sequence);
    msiDinputInvalidateCanonicalFrame();newer=stamp;check("invalid canonical clears diagnostic provenance",!msiDinputCanonicalSnapshot(snapshot,&newer) && !newer.valid && newer.sequence==0);
    // No source for neutral generated without a physical canonical sample.
    const auto missing=inputHostMsiReportDiagnosticFrameMetadata({},true,0);check("synthetic neutral has no fabricated source",missing.empty());
    const auto metadata=inputHostMsiReportDiagnosticFrameMetadata(stamp,false,1);
    check("valid physical metadata exact original sequence generation",json::parse(metadata).at("sourceFrameSequence")==stamp.sequence && json::parse(metadata).at("sourceGeneration")==stamp.generation);
    inputHostMsiReportDiagnosticRecordSourceAfterAttempt(stamp,false,1,true);
    check("physical edge logger distinguishes original and submitted",logs.back()["detail"]["rawXinputButtons"]==stamp.buttons && logs.back()["detail"]["submittedCanonicalButtons"]==1);
    FocusFrameContent content;content.buttons=1;const json frame=focusIsolationAssembleFrame(content);
    const std::string realFrameHash=inputHostCanonicalFrameHash(1,0,0,0,0,0,0);
    const auto fast=inputHostBuildSubmitFrameText(frame,"paired-wire",24,42,captured,realFrameHash,"no-pair");
    json dom=inputHostEnvelopeLocked("SUBMIT_FRAME",false);for(auto it=frame.begin();it!=frame.end();++it)dom[it.key()]=it.value();dom["requestId"]="paired-wire";dom["sampleSequence"]=42;dom["monotonicTick"]=captured;dom["normalizedInputHash"]=realFrameHash;dom["motionPairProofId"]="no-pair";
    auto a=fast,b=dom.dump();check("original real fast serializer is byte equivalent to DOM",a==b);
    {MsiVirtualReportFrameMetadataScope scope(metadata);inputHostMsiReportDiagnosticExtendRequest("SUBMIT_FRAME",a);inputHostMsiReportDiagnosticExtendRequest("SUBMIT_FRAME",b);}
    check("active diagnostic fast/slow requests byte-equivalent",a==b);
    { // Write ONLY caller's fixed scratch fixture; never appdata/product logs.
        const json paired={{"schemaVersion",1},{"prepare",{{"base",exportedPrepareBase},{"withDiagnostic",exportedPrepareWithDiagnostic}}},
            {"frame",{{"base",fast},{"withSource",a},{"sourceSequence",stamp.sequence},{"sourceGeneration",stamp.generation}}}};
        std::ofstream artifact(L"G:\\YeManCC-Work\\_scratch\\msi-virtual-report-pair-20261012-1439\\native-fake\\native-wire-fixture.json",std::ios::binary);
        artifact<<paired.dump(2);artifact.close();check("native wire fixture saved in scratch only",!artifact.fail());
    }
    auto encoded=json::parse(a);const auto m=encoded.at("msiReportChainSource");encoded.erase("msiReportChainSource");check("optional source leaves ALL control values exactly original",encoded==dom);
    check("optional extension preserves ALL original wire bytes",a.substr(0,fast.size()-1)==fast.substr(0,fast.size()-1));
    check("envelope counters unaffected by diagnostics",g_inputHostSequence==seq && g_inputHostSampleSequence==sample);
    check("thread local frame metadata lifetime is cleared",g_msiVirtualReportFrameMetadata==nullptr);
    auto noScope=fast;inputHostMsiReportDiagnosticExtendRequest("SUBMIT_FRAME",noScope);check("no scope means no source injection",noScope==fast);
    const auto logCount=logs.size();inputHostMsiReportDiagnosticRecordSourceAfterAttempt(stamp,false,1,true);check("stable physical/submitted held frame no repeated log",logs.size()==logCount);
    inputHostMsiReportDiagnosticRecordSourceAfterAttempt(stamp,true,0,true);check("neutral transition with held source records same source",logs.size()==logCount+1 && logs.back()["detail"]["rawXinputButtons"]==stamp.buttons && logs.back()["detail"]["semanticNeutral"]==true);
    inputHostMsiReportDiagnosticRecordSourceAfterAttempt(stamp,false,1,true);check("return from neutral records held-source repress candidate",logs.size()==logCount+2);
    g_msiVirtualReportTestLog=[](const char*,const json&){throw 42;};auto text=fast;inputHostMsiReportDiagnosticRecordSourceAfterAttempt(stamp,false,2,true);check("logger throw disables ONLY future diagnostic metadata",!g_msiVirtualReportDiagnostic.active());inputHostMsiReportDiagnosticExtendRequest("SUBMIT_FRAME",text);check("logging failure keeps ordinary request bytes",text==fast);
    g_msiVirtualReportTestLog=[&](const char* event,const json& body){logs.push_back({{"event",event},{"detail",body}});};
    setup();authorize("no-auto-rearm");g_msiVirtualReportDiagnostic.ClearHost();hello(1);check("Host clear does not auto-rearm consumed request",!json::parse(prepare()).contains("msiReportChainDiagnostic"));
    setup();g_msiVirtualReportDiagnostic.Request("bad-ack");hello(1);prepare();inputHostMsiReportDiagnosticAcceptPrepare(true,{{"msiReportChainDiagnosticArmed",true},{"msiReportChainDiagnosticSessionId","wrong"}});check("wrong diagnostic ACK does not arm",!g_msiVirtualReportDiagnostic.active());
    std::string malformed="{";check("malformed base not rewritten",!inputHostMsiReportDiagnosticAppendOptional(malformed,"msiReportChainSource",json()) && malformed=="{");
    auto duplicate=a;check("duplicate optional field never appended",!inputHostMsiReportDiagnosticAppendOptional(duplicate,"msiReportChainSource",m) && duplicate==a);
    std::cout<<"MSI_NATIVE_REPORT_PAIR checks="<<n<<" failures="<<failed<<" product=false SDK=false hardware=false realEnvironmentRead=false logSink=fake\n";
    return failed?1:0;
}
