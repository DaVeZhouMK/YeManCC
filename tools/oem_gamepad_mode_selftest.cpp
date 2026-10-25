#include "../native/oem_gamepad_mode_session.h"
#include "../native/msi_oem_gamepad_mode_adapter.h"
#include "../native/oem_gamepad_mode_journal.h"
#include "../native/oem_input_contract_evidence.h"
#include "../native/oem_input_session.h"
#include "../native/oem_frontend_policy_contract.h"
#include <iostream>
#include <map>
#include <functional>
using namespace ymcc::oem::gamepad;
static unsigned cases=0;
static void check(bool x,const char* name){++cases;if(!x){std::cerr<<"FAIL "<<name<<"\n";std::exit(1);}}
struct FakeIo final : RegistryIo {
    std::map<std::wstring,Value> db;
    unsigned reads=0,writes=0;
    unsigned failWriteAt=0,unknownReadAt=0;
    bool unknownAll=false,missingKeys=false,mutateOnFailure=false;
    std::vector<Address> written;
    std::function<void(FakeIo&,const Address&,unsigned)> beforeRead;
    std::function<void(FakeIo&,const Address&,unsigned)> afterWrite;
    static std::wstring k(const Address& a){return std::to_wstring(int(a.view))+L":"+a.subkey+L"|"+a.name;}
    ReadResult read(const Address& a) override {
        ++reads;if(beforeRead)beforeRead(*this,a,reads);
        if(unknownAll||reads==unknownReadAt)return {};
        if(missingKeys)return {ReadState::MissingKey,{}};
        const auto i=db.find(k(a));
        return i==db.end()?ReadResult{ReadState::MissingValue,{}}:ReadResult{ReadState::Value,i->second};
    }
    bool write(const Address& a,const Value& v) override {
        ++writes;written.push_back(a);const bool ok=writes!=failWriteAt;
        if(ok||mutateOnFailure){if(v.exists)db[k(a)]=v;else db.erase(k(a));}
        if(afterWrite)afterWrite(*this,a,writes);
        return ok;
    }
};
struct FakeJournal final : JournalStore {
    JournalRead state{JournalState::Absent,{}};
    unsigned saves=0,failAt=0;
    std::function<void(const Record&,unsigned)> onSave;
    JournalRead load() override{return state;}
    bool save(const Record& r) override {
        ++saves;if(onSave)onSave(r,saves);if(saves==failAt)return false;
        state={JournalState::Record,r};return true;
    }
};
struct FakeGate final : AuthorityGate {
    bool allowed=true;unsigned calls=0,failAt=0;
    bool current(const Owner&) override{return ++calls!=failAt&&allowed;}
};
static const Owner owner{"test-process-nonce-20261012",7,11,13,17};
static msi::InstalledBinding binding(){return {true,std::string(msi::aiEngineSha256),
    std::string(msi::controlPageSha256),std::string(64,'a'),
    "HandheldCompanion.Devices.ClawCG3EM","MS-1T91"};}
struct TestRig {
    msi::FrontendAdapter adapter{binding()};FakeIo io;FakeJournal journal;FakeGate gate;PolicySession session;
    TestRig(){auto p=adapter.patches(Intent::VirtualCapture);io.db[FakeIo::k(p[0].address)]=msi::regSz(u"1");io.db[FakeIo::k(p[1].address)]=msi::regSz(u"Desktop");}
    Result acquire(bool authorized=true){return session.acquire({owner,Intent::VirtualCapture,authorized},adapter,io,journal,gate);}
    Result release(){return session.release(owner,adapter,io,journal);}
    Address address(unsigned i){return adapter.patches(Intent::VirtualCapture)[i].address;}
};
static void policyTests(){
    TestRig t;auto c=t.adapter.capability();
    check(c.policyCapabilityVerified&&!c.allModeWritersExcluded&&c.virtualRoute==msi::dinputRoute,"MSI virtual route DInput2, not complete mode lock");
    check(c.nativeRoute==msi::xinputRoute,"MSI native route XInput1");
    for(Vendor vendor:{Vendor::Unknown,Vendor::AsusRog,Vendor::Lenovo}){
        UnverifiedAdapter unavailable(vendor);PolicySession s;FakeIo io;FakeJournal j;FakeGate g;
        check(s.acquire({owner,Intent::VirtualCapture,true},unavailable,io,j,g).status==Status::Unsupported,"unverified vendor unavailable");
        check(io.reads==0&&io.writes==0&&j.saves==0,"unverified vendor has no I/O");
    }
    for(unsigned mutation=0;mutation<6;++mutation){
        auto b=binding();if(mutation==0)b.installedIdentityVerified=false;
        if(mutation==1)b.aiEngineHash[0]='0';if(mutation==2)b.controlPageHash[0]='0';
        if(mutation==3)b.identityReceiptSha256="bad";if(mutation==4)b.board="PREFIX-MS-1T91";
        if(mutation==5)b.deviceClass="HandheldCompanion.Devices.ROGAlly";
        msi::FrontendAdapter invalid(b);check(!invalid.capability().policyCapabilityVerified&&invalid.patches(Intent::VirtualCapture).empty(),"unbound MSI adapter does not expose writes");
    }
    auto b=binding();for(char& ch:b.aiEngineHash)if(ch>='A'&&ch<='F')ch=char(ch-'A'+'a');
    check(msi::FrontendAdapter(b).capability().policyCapabilityVerified,"SHA case-insensitive identity");
    check(t.address(0).view==RegistryView::View32&&t.address(1).view==RegistryView::View32,"effective 32-bit keys explicit");
    check(t.address(0).subkey.find(L"WOW6432Node")==std::wstring::npos,"no duplicate WOW redirection");
    check(t.address(1).subkey.ends_with(L"OsdEditor")&&t.address(1).name==L"ControlModeUserSet","preference is OsdEditor not temporary AI key");
    check(t.acquire(false).status==Status::Unauthorized&&t.io.writes==0,"consent required");
    auto badOwner=owner;badOwner.processNonce="";
    check(t.session.acquire({badOwner,Intent::VirtualCapture,true},t.adapter,t.io,t.journal,t.gate).status==Status::InvalidOwner,"invalid owner rejected");
    t.journal.onSave=[&](const Record& r,unsigned save){
        if(save==2)check(r.entries[0].mayHaveWritten&&t.io.writes==0,"WAL persisted before first write");
        if(save==3)check(r.entries[1].mayHaveWritten&&t.io.writes==1,"WAL persisted before second write");
    };
    Address shadow=t.address(0);shadow.view=RegistryView::View64;t.io.db[FakeIo::k(shadow)]=msi::regSz(u"shadow64");
    auto acquired=t.acquire();check(acquired.status==Status::Acquired&&acquired.policyVerified&&acquired.leaseActive,"acquire policy verified");
    check(!acquired.hardwareModeVerified&&!acquired.allModeWritersExcluded,"policy not physical mode proof");
    check(t.io.writes==2&&t.io.db[FakeIo::k(shadow)]==msi::regSz(u"shadow64"),"32-bit writes do not alter 64-bit shadow");
    check(t.acquire().status==Status::AlreadyOwned&&t.io.writes==2,"idempotence never reasserts settings");
    auto next=owner;++next.hostEpoch;++next.powerGeneration;
    check(t.session.handoff(owner,next,t.adapter,t.journal,t.gate).status==Status::AlreadyOwned&&t.io.writes==2,"persona/power handoff no config writes");
    check(t.release().status==Status::Stale&&t.io.writes==2,"old owner cannot restore transferred lease");
    auto restored=t.session.release(next,t.adapter,t.io,t.journal);check(restored.status==Status::Released&&!t.session.ownsLease(),"release clears pending lease");
    check(t.io.written[2]==t.address(1)&&t.io.written[3]==t.address(0),"preference restored before reenable auto");
    check(t.io.db[FakeIo::k(t.address(0))]==msi::regSz(u"1")&&t.io.db[FakeIo::k(t.address(1))]==msi::regSz(u"Desktop"),"original bytes restored");
    check(!t.journal.state.record.pending,"durable release record retained");
    check(t.release().status==Status::NoLease,"double release is no-op");
    {
        TestRig r;r.io.db.erase(FakeIo::k(r.address(0)));r.io.db.erase(FakeIo::k(r.address(1)));
        check(r.acquire().status==Status::Acquired,"missing values on existing keys can be set");
        check(r.release().status==Status::Released&&r.io.db.empty(),"missing originals restored by value deletion only");
    }
    {
        TestRig r;for(const auto& p:r.adapter.patches(Intent::VirtualCapture))r.io.db[FakeIo::k(p.address)]=p.desired;
        check(r.acquire().status==Status::Acquired&&r.io.writes==0,"preexisting desired values not owned");
        r.io.db[FakeIo::k(r.address(0))]=msi::regSz(u"external");
        check(r.release().status==Status::Released&&r.io.writes==0,"preexisting values never reverted");
    }
    {
        TestRig r;check(r.acquire().status==Status::Acquired,"external-change setup");
        r.io.db[FakeIo::k(r.address(0))]=msi::regSz(u"external");auto v=r.release();
        check(v.status==Status::Released&&v.externalChangesPreserved==1&&r.io.writes==3,"external differing value preserved");
    }
    for(unsigned which=0;which<2;++which){
        TestRig r;if(which)r.io.missingKeys=true;else r.io.unknownAll=true;
        check(r.acquire().status==Status::ReadUnknown&&r.io.writes==0&&r.journal.saves==0,"unknown/missing key no creation or write");
    }
    for(Value original:{Value{true,4,{1,0,0,0}},Value{true,1,{0}},Value{false,1,{}}}){
        TestRig r;r.io.db[FakeIo::k(r.address(0))]=original;
        check(r.acquire().status!=Status::Acquired&&r.io.writes==0,"wrong original type/shape does not get overwritten");
    }
    {
        TestRig r;r.journal.state.state=JournalState::Invalid;
        check(r.acquire().status==Status::JournalInvalid&&r.io.writes==0,"corrupt journal refuses takeover");
    }
    for(unsigned failSave=1;failSave<=3;++failSave){
        TestRig r;r.journal.failAt=failSave;
        check(r.acquire().status==Status::JournalFailure&&r.io.writes==(failSave==3?1u:0u),"failed WAL prevents next write");
        if(r.session.ownsLease())check(r.release().status==Status::Released,"WAL failure can clean acquired subset");
    }
    for(unsigned failGate=1;failGate<=4;++failGate){
        TestRig r;r.gate.failAt=failGate;auto v=r.acquire();
        check(v.status==Status::Stale,"latest owner gate checked across transaction");
        check(r.io.writes==(failGate==1||failGate==2?0u:failGate==3?1u:2u),"stale generation blocks subsequent write");
        if(r.session.ownsLease()){
            check(r.acquire().status==Status::Busy,"failed lease never mislabeled already acquired");
            check(r.release().status==Status::Released,"stale partial transaction recoverable");
        }
    }
    {
        TestRig r;check(r.acquire().status==Status::Acquired,"restore dependency setup");
        r.io.failWriteAt=3;
        check(r.release().status==Status::CleanupPending&&r.io.writes==3,"failed preference restore prevents automatic reenable");
        check(r.io.db[FakeIo::k(r.address(0))]==msi::regSz(u"0"),"automatic remains disabled until dependent cleanup resolves");
        check(r.release().status==Status::Released,"explicit cleanup retry restores both dependencies");
    }
    for(unsigned failWrite=1;failWrite<=2;++failWrite){
        TestRig r;r.io.failWriteAt=failWrite;r.io.mutateOnFailure=true;
        check(r.acquire().status==Status::WriteFailure,"ambiguous write failure retains recovery debt");
        check(r.release().status==Status::Released,"write-false with side effect compare-restores");
    }
    {
        TestRig r;r.io.afterWrite=[&](FakeIo& io,const Address&,unsigned n){if(n==2)io.db[FakeIo::k(r.address(0))]=msi::regSz(u"external");};
        check(r.acquire().status==Status::Conflict,"full final readback detects earlier key changed");
        check(r.release().externalChangesPreserved==1,"partial conflict preserves external mutation");
    }
    {
        TestRig r;r.io.beforeRead=[&](FakeIo& io,const Address& a,unsigned n){if(n==3)io.db[FakeIo::k(a)]=msi::regSz(u"external");};
        check(r.acquire().status==Status::Conflict&&r.io.writes==0,"pre-write comparison refuses external change");
        check(r.release().status==Status::Released,"no-write conflict closes journal");
    }
    {
        TestRig r;check(r.acquire().status==Status::Acquired,"crash setup");
        PolicySession recovered;
        check(recovered.acquire({owner,Intent::VirtualCapture,true},r.adapter,r.io,r.journal,r.gate).status==Status::CleanupPending,"crash debt blocks new acquisition");
        check(recovered.recover(r.adapter,r.io,r.journal).status==Status::Recovered,"crash recovery restores exact pending values");
        check(!r.journal.state.record.pending,"recovery durable closure");
    }
    {
        TestRig r;check(r.acquire().status==Status::Acquired,"unknown cleanup setup");r.io.unknownAll=true;
        check(r.release().status==Status::CleanupPending&&r.session.ownsLease(),"unknown cleanup retains lease without write");
        r.io.unknownAll=false;check(r.release().status==Status::Released,"later explicit cleanup closes debt");
    }
    {
        TestRig r;check(r.acquire().status==Status::Acquired,"journal close failure setup");r.journal.failAt=r.journal.saves+1;
        check(r.release().status==Status::JournalFailure&&r.session.ownsLease(),"failed durable closure not success");
        check(r.release().status==Status::Released,"retry closure compares original no duplicate writes");
    }
}
static void arbiterTests(){
    TestRig r;const auto c=r.adapter.capability();ModeArbiter a;
    check(!a.begin({owner,Intent::VirtualCapture,false},c),"arbiter requires authorized intent");
    check(a.begin({owner,Intent::VirtualCapture,true},c),"arbiter takes semantic DInput target");
    Deviation d{owner,Witness::FirmwareReadback,msi::xinputRoute,true,true,true,false,1000,1100,HardwareState::NativeGamepad,TransitionOrigin::Unknown};
    auto unknown=d;unknown.witness=Witness::Unknown;check(a.observe(unknown).decision==Decision::Unknown,"no writer from unknown witness");
    unknown=d;unknown.owner.hostEpoch--;check(a.observe(unknown).decision==Decision::Stale,"old epoch no correction");
    unknown=d;unknown.physicalIdentityVerified=false;check(a.observe(unknown).decision==Decision::Unknown,"no virtual/unknown identity correction");
    unknown=d;unknown.routeOwned=false;check(a.observe(unknown).decision==Decision::NotOwned,"unowned source no correction");
    unknown=d;unknown.exitFence=true;check(a.observe(unknown).decision==Decision::NotOwned,"exit fence no correction");
    unknown=d;unknown.hardwareReady=false;check(a.observe(unknown).decision==Decision::NotOwned,"suspended hardware no correction");
    unknown=d;unknown.now=3001;check(a.observe(unknown).decision==Decision::Unknown,"expired witness no correction");
    unknown=d;unknown.observed.vendor=Vendor::AsusRog;check(a.observe(unknown).decision==Decision::Unknown,"other vendor cannot trigger MSI writer");
    for(auto origin:{TransitionOrigin::ExplicitUserIntent,TransitionOrigin::PhysicalTopology,TransitionOrigin::PhysicalSwitch,TransitionOrigin::OfficialShortcut}) {
        auto physical=d;physical.origin=origin;
        check(a.observe(physical).decision==Decision::ProtectedPhysicalChoice,"manual/topology state never forcibly corrected");
    }
    for(auto state:{HardwareState::DetachedDualInput,HardwareState::FpsHardware,HardwareState::ControllerOff}) {
        auto physical=d;physical.state=state;
        check(a.observe(physical).decision==Decision::ProtectedPhysicalChoice,"Dual/FPS/off state is not an OEM automatic fault");
    }
    auto unmappedFps=d;unmappedFps.state=HardwareState::FpsHardware;unmappedFps.observed=unsupportedRoute;
    check(a.observe(unmappedFps).decision==Decision::ProtectedPhysicalChoice,"unmapped FPS state cannot be treated as auto error or corrected");
    auto unclassified=d;unclassified.state=HardwareState::Unknown;
    check(a.observe(unclassified).decision==Decision::Unknown,"unclassified hardware state no mode write");
    const auto first=a.observe(d);check(first.decision==Decision::QueueExistingWriter&&first.route==msi::dinputRoute,"correction queues DInput2 existing writer only");
    check(a.observe(d).decision==Decision::Exhausted,"no uncontrolled reassertion");
    auto correct=d;correct.observed=msi::dinputRoute;check(a.observe(correct).decision==Decision::Correct,"observed correct mode");
    check(a.observe(d).decision==Decision::Exhausted,"correct sample never replenishes write budget");
    auto next=owner;++next.hostEpoch;++next.powerGeneration;check(a.handoff(owner,next),"arbiter power/persona handoff");
    d.owner=next;check(a.observe(d).decision==Decision::Exhausted,"handoff never replenishes correction budget");
    check(!a.end(owner)&&a.end(next),"only current owner releases");
    check(!a.begin({owner,Intent::VirtualCapture,true},c),"same session cannot restart correction loop");
    auto later=next;++later.session;check(a.begin({later,Intent::VirtualCapture,true},c),"new authorized session can receive new budget");
    ModeArbiter unavailable;check(!unavailable.begin({owner,Intent::VirtualCapture,true},UnverifiedAdapter(Vendor::AsusRog).capability()),"unverified ROG cannot arm arbiter");
}
static void journalTests(){
    TestRig r;check(r.acquire().status==Status::Acquired,"journal setup");
    const auto encoded=encodeRecord(*r.session.record());const auto decoded=decodeRecord(encoded.dump());
    check(decoded.state==JournalState::Record&&r.adapter.validRecord(decoded.record),"strict record roundtrip valid");
    for(unsigned mutation=0;mutation<12;++mutation){
        auto j=encoded;
        switch(mutation){
        case 0:j["schema"]=2;break;case 1:j["pending"]=1;break;case 2:j["owner"]["session"]=-1;break;
        case 3:j["vendor"]=99;break;case 4:j["entries"][0]["view"]=99;break;
        case 5:j["entries"][0]["original"]["bytes"][0]=256;break;
        case 6:j["owner"]["hostEpoch"]=1.5;break;case 7:j["entries"][0]["subkey"]="\ninvalid";break;
        case 8:j["owner"]["processNonce"]="short";break;case 9:j["extra"]=true;break;
        case 10:j["entries"]=nlohmann::json::array();break;case 11:j["entries"][0]["original"]["type"]=uint64_t(1)<<40;break;
        }
        check(decodeRecord(j.dump()).state==JournalState::Invalid,"malformed record fails strict codec");
    }
    for(unsigned mutation=0;mutation<6;++mutation){
        auto record=*r.session.record();switch(mutation){
        case 0:record.entries[0].patch.address.view=RegistryView::View64;break;
        case 1:record.entries[0].patch.address.subkey=L"SOFTWARE\\unrelated";break;
        case 2:record.entries[0].patch.desired=msi::regSz(u"1");break;
        case 3:record.binding="different-installation";break;
        case 4:record.entries[0].original={true,4,{1,0,0,0}};break;
        case 5:record.pending=false;break;
        }
        check(!r.adapter.validRecord(record),"tampered whitelist record refused");
        FakeJournal j;j.state={JournalState::Record,record};PolicySession s;FakeIo io;
        check(s.recover(r.adapter,io,j).status==Status::JournalInvalid&&io.writes==0,"tampered recovery no writes");
    }
    check(decodeRecord("{\"schema\":1,\"schema\":1}").state==JournalState::Invalid,"duplicate names invalid");
    check(decodeRecord(std::string(65537,'a')).state==JournalState::Invalid,"record size bounded");
    check(decodeRecord(std::string(12,'[')+"0"+std::string(12,']')).state==JournalState::Invalid,"overdeep journal rejected");
    check(decodeRecord("not-json").state==JournalState::Invalid,"invalid JSON no exception escapes");
}
static InputContract contract(){
    InputContract c;c.owner=owner;c.capability=msi::FrontendAdapter(binding()).capability();
    c.identity={Vendor::Msi,Frontend::MsiCenterM,true,true,c.capability.binding};c.userAuthorized=true;
    c.route=msi::dinputRoute;c.physicalSource="USB/MSI1902/MAIN";c.sourceGeneration=23;
    c.virtualTarget="SWD/HIDMAESTRO/TEST";c.virtualTargetEpoch=29;c.persona="steamdeck";c.hostRunId="FAKE-HOST-RUN-1";return c;
}
static ChainProofs proofs(const InputContract& c){
    ChainProofs p;const ProofToken token{c.owner,c.physicalSource,c.sourceGeneration};
    p.policy={c.owner,c.capability.binding,PolicyValue::VirtualCaptureCompatible,true,1000};
    p.mode={token,ModeWitness::PhysicalSourceIdentity,c.route,true,1000,HardwareState::VirtualCaptureCompatible,TransitionOrigin::Unknown};
    p.capture={token,true,true,false,1000,101,true,fullGamepadFields,std::string(64,'a')};p.isolation={token,true,true,true,false,1000,IsolationWitness::IndependentSourceAccessDenied};
    p.submit={token,c.virtualTarget,c.persona,c.virtualTargetEpoch,1001,101,202,true,true,true,false,false,
        true,true,true,c.hostRunId,std::string(64,'a'),std::string(64,'b')};return p;
}
static void chainTests(){
    auto c=contract();const auto base=proofs(c);
    check(admit(c)==Admission::Accepted,"semantic contract admitted");
    auto neutral=evaluate(c,base,1100);check(neutral.stage==ChainStage::ReadyForStimulus&&!neutral.nonNeutralCorrelated&&!neutral.gameConsumptionProven,"neutral readiness not effectiveness proof");
    auto pressed=base;pressed.capture.nonNeutral=true;pressed.submit.nonNeutralReportSubmitted=true;pressed.submit.mappedStimulusExpected=true;
    check(evaluate(c,pressed,1100).stage==ChainStage::StimulusSubmitted,"same source non-neutral capture/submission correlation");
    auto unsubmitted=pressed;unsubmitted.submit.nonNeutralReportSubmitted=false;
    check(evaluate(c,unsubmitted,1100).stage==ChainStage::StimulusNotSubmitted,"mapped physical stimulus with neutral report is a broken closure");
    auto chosen=base;chosen.mode.state=HardwareState::FpsHardware;chosen.mode.observedRoute=unsupportedRoute;
    check(evaluate(c,chosen,1100).stage==ChainStage::ProtectedPhysicalChoice&&evaluate(c,chosen,1100).requiresUserAction,"FPS physical choice stays explicit despite unmapped capture route");
    chosen=base;chosen.mode.origin=TransitionOrigin::PhysicalTopology;chosen.mode.observedRoute=msi::xinputRoute;
    check(evaluate(c,chosen,1100).stage==ChainStage::ProtectedPhysicalChoice,"topology change not mislabeled OEM auto mode fault");
    auto unproven=base;unproven.isolation.witness=IsolationWitness::Unknown;
    check(evaluate(c,unproven,1100).stage==ChainStage::IsolationUnknown,"self/neutral observer cannot prove source isolation");
    auto consumer=pressed;
    consumer.consumer={c.owner,c.virtualTarget,c.hostRunId,c.virtualTargetEpoch,202,1005,
        ConsumerKind::IndependentVirtualReader,true,true,true};
    check(evaluate(c,consumer,1100).stage==ChainStage::ExternalReadConfirmedWindow&&!evaluate(c,consumer,1100).gameConsumptionProven,"independent virtual read not Steam consumption");
    consumer.consumer.kind=ConsumerKind::Steam;
    check(evaluate(c,consumer,1100).stage==ChainStage::ConsumerConfirmedWindow&&evaluate(c,consumer,1100).gameConsumptionProven,"strict same-target Steam stimulus window only");
    for(unsigned i=0;i<6;++i){
        auto mismatch=consumer;
        if(i==0)mismatch.consumer.owner.hostEpoch--;
        if(i==1)mismatch.consumer.hostRunId="OLD-HOST";
        if(i==2)mismatch.consumer.virtualTargetEpoch--;
        if(i==3)mismatch.consumer.reportSequence--;
        if(i==4)mismatch.consumer.stimulusCorrelationVerified=false;
        if(i==5)mismatch.consumer.targetIdentityVerified=false;
        check(evaluate(c,mismatch,1100).stage==ChainStage::StimulusSubmitted&&!evaluate(c,mismatch,1100).gameConsumptionProven,"unmatched external observation is not game consumption proof");
    }
    for(unsigned i=0;i<5;++i){
        auto incomplete=base;
        if(i==0)incomplete.capture.canonicalSourceBound=false;
        if(i==1)incomplete.capture.availableFields&=~fieldTriggers;
        if(i==2)incomplete.capture.normalizedInputHash="label-not-hash";
        if(i==3)incomplete.capture.availableFields&=~fieldButtons;
        if(i==4)incomplete.capture.availableFields&=~fieldRightStick;
        check(evaluate(c,incomplete,1100).stage==ChainStage::CaptureUnknown,"partial/dead snapshot does not prove canonical full capture");
    }
    for(unsigned i=0;i<6;++i){
        auto notSubmitted=base;
        if(i==0)notSubmitted.submit.backendSubmitted=false;
        if(i==1)notSubmitted.submit.backendReceiptVerified=false;
        if(i==2)notSubmitted.submit.frameBuilt=false;
        if(i==3)notSubmitted.submit.sourceNormalizedInputHash=std::string(64,'c');
        if(i==4)notSubmitted.submit.hostRunId="OLD-HOST";
        if(i==5)notSubmitted.submit.submittedFrameHash="unknown";
        check(evaluate(c,notSubmitted,1100).stage==ChainStage::SubmitUnknown,"IPC ACK/label is not correlated backend submission");
    }
    check(!evaluate(c,pressed,1100).lastingStabilityProven,"single stimulus not durable stability");
    for(unsigned mutation=0;mutation<10;++mutation){
        auto invalid=c;
        switch(mutation){case 0:invalid.userAuthorized=false;break;case 1:invalid.owner.session=0;break;
        case 2:invalid.identity.frontend=Frontend::AsusArmouryCrateSE;break;case 3:invalid.identity.installedComponentsVerified=false;break;
        case 4:invalid.capability.captureCapabilityVerified=false;break;case 5:invalid.route=msi::xinputRoute;break;
        case 6:invalid.virtualTarget=invalid.physicalSource;break;case 7:invalid.sourceGeneration=0;break;
        case 8:invalid.virtualTargetEpoch=0;break;case 9:invalid.identity.capabilityBinding="different";break;}
        check(admit(invalid)!=Admission::Accepted&&evaluate(invalid,base,1100).stage==ChainStage::InvalidContract,"invalid family/route/token contract rejected");
    }
    for(unsigned mutation=0;mutation<24;++mutation){
        auto p=base;ChainStage expected=ChainStage::StaleEvidence;
        switch(mutation){
        case 0:--p.capture.token.owner.hostEpoch;break;case 1:--p.submit.token.owner.powerGeneration;break;
        case 2:--p.mode.token.sourceGeneration;break;case 3:p.isolation.token.physicalSource="virtual";break;
        case 4:p.policy.capabilityBinding="other";break;
        case 5:p.policy.exactReadback=false;expected=ChainStage::PolicyUnknown;break;
        case 6:p.policy.readback=PolicyValue::Automatic;expected=ChainStage::PolicyDeviation;break;
        case 7:p.mode.witness=ModeWitness::Unknown;expected=ChainStage::ModeUnknown;break;
        case 8:p.mode.observedRoute=msi::xinputRoute;expected=ChainStage::ModeDeviation;break;
        case 9:p.capture.sourceIsPhysical=false;expected=ChainStage::CaptureUnknown;break;
        case 10:p.capture.readSucceeded=false;expected=ChainStage::CaptureUnknown;break;
        case 11:p.capture.sampleSequence=0;expected=ChainStage::CaptureUnknown;break;
        case 12:p.capture.tick=1;expected=ChainStage::CaptureUnknown;break;
        case 13:p.isolation.externalVisibilityVerified=false;expected=ChainStage::IsolationUnknown;break;
        case 14:p.isolation.externalNonNeutralObserved=true;expected=ChainStage::IsolationLeak;break;
        case 15:p.isolation.ownerCanCapture=false;expected=ChainStage::IsolationUnknown;break;
        case 16:p.submit.hostAccepted=false;expected=ChainStage::SubmitUnknown;break;
        case 17:p.submit.virtualTargetEpoch--;expected=ChainStage::SubmitUnknown;break;
        case 18:p.submit.persona="other";expected=ChainStage::SubmitUnknown;break;
        case 19:p.submit.sourceSampleSequence--;expected=ChainStage::SubmitUnknown;break;
        case 20:p.submit.virtualTarget="physical";expected=ChainStage::SubmitUnknown;break;
        case 21:p.submit.tick=999;expected=ChainStage::SubmitUnknown;break;
        case 22:p.submit.effectiveTargetMatches=false;expected=ChainStage::SubmitUnknown;break;
        case 23:p.submit.targetPresent=false;expected=ChainStage::SubmitUnknown;break;
        }
        const auto result=evaluate(c,p,1100);check(result.stage==expected&&!result.gameConsumptionProven&&result.existingTargetMustNotBeRebuilt,"chain pinpoints broken stage without target rebuild");
    }
    check(frontendVendor(Frontend::AsusArmouryCrate)==Vendor::AsusRog&&frontendVendor(Frontend::AsusArmouryCrateSE)==Vendor::AsusRog,"ROG machine distinct from AC/SE identities");
    for(auto f:{Frontend::AsusArmouryCrateSE,Frontend::AsusArmouryCrate,Frontend::LenovoLegionSpace}) {
        UnverifiedInputProvider future(f);
        check(!future.capability().policyCapabilityVerified&&future.acquirePolicy({owner,Intent::VirtualCapture,true}).status==Status::Unsupported,"future frontend policy remains disabled");
        check(!future.enqueueExistingModeWriter({owner,msi::dinputRoute,"fake"})&&!future.inspect(owner).identity.installedComponentsVerified,"future frontend no queue/no identity fabrication");
    }
    // Fake-only future provider proves generic contract can represent a different
    // route without inventing any ASUS/Lenovo firmware mode or registry key.
    auto asus=c;asus.capability.vendor=Vendor::AsusRog;
    asus.capability.binding="FAKE-ONLY-ASUS-CONTRACT";asus.capability.virtualRoute={Vendor::AsusRog,InputProtocol::RawHid,"fake.capture"};
    asus.identity={Vendor::AsusRog,Frontend::AsusArmouryCrateSE,true,true,asus.capability.binding};asus.route=asus.capability.virtualRoute;
    asus.route.auxiliaryProtocols={InputProtocol::DirectInput};
    asus.capability.virtualRoute=asus.route;
    check(admit(asus)==Admission::Accepted&&evaluate(asus,proofs(asus),1100).submitVerified,"generic fake-only provider no MSI numeric-mode assumption");
    auto composite=msi::xinputRoute;composite.auxiliaryProtocols={InputProtocol::RawHid};
    check(composite.supported(),"composite XInput plus vendor HID source does not imply DInput");
    composite.auxiliaryProtocols={InputProtocol::XInput};check(!composite.supported(),"duplicate composite protocol rejected");
    auto badEnum=c;badEnum.intent=Intent(255);check(admit(badEnum)!=Admission::Accepted,"unknown semantic intent rejected");
    auto invalidProtocol=msi::xinputRoute;invalidProtocol.protocol=InputProtocol(255);check(!invalidProtocol.supported(),"unknown transport enum rejected");
    auto captureOnly=c;captureOnly.capability.policyCapabilityVerified=false;captureOnly.capability.firmwareModeCapabilityVerified=false;
    check(admit(captureOnly)==Admission::Accepted,"verified capture can exist without a writable official policy/firmware API");
    auto policyUnknown=base;policyUnknown.policy.exactReadback=false;
    check(evaluate(captureOnly,policyUnknown,1100).stage==ChainStage::PolicyUnknown&&assessSignal(captureOnly,policyUnknown,1100).submitVerified,"official query unknown does not erase independently witnessed signal chain");
    ModeArbiter readonly;
    check(readonly.begin({c.owner,Intent::VirtualCapture,true},captureOnly.capability),"capture-only session can hold source ownership");
    Deviation unsupported{c.owner,Witness::FirmwareReadback,msi::xinputRoute,true,true,true,false,1000,1100,HardwareState::NativeGamepad,TransitionOrigin::Unknown};
    check(readonly.observe(unsupported).decision==Decision::Unsupported,"capture capability never grants firmware mode writes");
    PolicySession noPolicy;FakeIo noIo;FakeJournal noJournal;FakeGate noGate;
    class CaptureOnlyAdapter final:public Adapter {
        Capability c_;
    public:explicit CaptureOnlyAdapter(Capability c):c_(std::move(c)){}
        Capability capability()const override{return c_;}
        std::vector<Patch> patches(Intent)const override{return {};}
        bool validRecord(const Record&)const override{return false;}
    } capOnly(captureOnly.capability);
    check(noPolicy.acquire({c.owner,Intent::VirtualCapture,true},capOnly,noIo,noJournal,noGate).status==Status::Unsupported&&noIo.writes==0,"capture verification never grants official config writes");
    const auto separated=chainReceipt(captureOnly,policyUnknown,1100);
    check(separated["stage"]=="policy-unknown"&&separated["signalStage"]=="ready-for-stimulus"&&separated["signalSubmitVerified"].get<bool>(),"log separates policy and signal acceptance");
    const auto receipt=chainReceipt(c,pressed,1100);
    check(receipt["stage"]=="stimulus-submitted"&&!receipt["humanGestureProven"].get<bool>()&&!receipt["oemWriterAttributed"].get<bool>(),"diagnostic does not fabricate click/writer proof");
    check(!receipt["modeWriteIssued"].get<bool>()&&!receipt["virtualTargetRebuildRequested"].get<bool>(),"evidence helper is observation-only");
}
static void ownershipTests(){
    auto c=contract();InputSession session;
    check(session.acquire(c)==OwnershipResult::Acquired,"single controller input owner acquired");
    check(session.acquire(c)==OwnershipResult::AlreadyOwned,"identical contract does not reset session");
    auto conflicting=c;conflicting.route=msi::xinputRoute;
    check(session.acquire(conflicting)==OwnershipResult::InvalidContract,"native mode cannot replace DInput virtual contract");
    conflicting=c;conflicting.physicalSource="OTHER/CONTROLLER";
    check(session.acquire(conflicting)==OwnershipResult::Conflict,"same owner cannot silently replace capture source");
    conflicting=c;conflicting.owner.session++;
    check(session.acquire(conflicting)==OwnershipResult::Busy,"competing owner cannot acquire source");
    auto proof=proofs(c).mode;proof.observedRoute=msi::xinputRoute;proof.state=HardwareState::NativeGamepad;
    check(session.deviation(proof,1100,true,true,false).decision==Decision::QueueExistingWriter,"controller-owned arbiter uses verified mode proof");
    auto wrongSource=proof;wrongSource.token.physicalSource="OTHER";
    check(session.deviation(wrongSource,1100,true,true,false).decision==Decision::Stale,"foreign source cannot ask mode correction");
    auto moved=c;moved.owner.hostEpoch++;moved.owner.powerGeneration++;moved.sourceGeneration++;
    check(session.handoff(c.owner,moved,false,false)==OwnershipResult::Conflict,"handoff requires retained physical ownership");
    check(session.handoff(c.owner,moved,true,false)==OwnershipResult::AlreadyOwned,"power handoff keeps source/target and budget");
    check(session.inspect(proofs(c),1100).stage==ChainStage::StaleEvidence,"old proof invalid after generation handoff");
    proof=proofs(moved).mode;proof.observedRoute=msi::xinputRoute;proof.state=HardwareState::NativeGamepad;
    check(session.deviation(proof,1100,true,true,false).decision==Decision::Exhausted,"power/source generation never replenishes budget");
    auto persona=moved;persona.owner.hostEpoch++;persona.persona="ps5";persona.virtualTarget="SWD/NEW/TARGET";persona.virtualTargetEpoch++;
    check(session.handoff(moved.owner,persona,true,false)==OwnershipResult::Conflict,"virtual target replacement requires release proof");
    check(session.handoff(moved.owner,persona,true,true)==OwnershipResult::AlreadyOwned,"C type explicit persona handoff accepts release evidence");
    check(session.release(moved.owner)==OwnershipResult::Stale,"prior epoch owner cannot release");
    check(session.release(persona.owner)==OwnershipResult::Released,"current epoch release");
    check(session.acquire(persona)==OwnershipResult::Stale,"same session cannot recreate correction loop");
}
static void frontendAndMappingTests(){
    auto c=contract();FrontendPolicyObservation p;
    p.owner=c.owner;p.identity=c.identity;p.manualSelection=ManualControlSelection::Gamepad;
    p.autoPolicy=AutoSwitchPolicy::Disabled;p.source=ObservationSource::OfficialConfigRead;
    p.installedVersionBound=true;p.exactOfficialStateRead=true;p.tick=1000;p.officialStateReceipt="FAKE-OFFICIAL-READ-1";
    check(recognize(c,p,1100)==PolicyRecognition::OfficialPolicy,"manual and auto policy separate official fields");
    p.source=ObservationSource::ProviderCache;
    check(recognize(c,p,1100)==PolicyRecognition::CacheOnly,"cached gamepad_mode show not device/official read");
    p.source=ObservationSource::ProcessInventory;
    check(recognize(c,p,1100)==PolicyRecognition::InventoryOnly,"process inventory not policy source");
    p.source=ObservationSource::ParsedDeviceResponse;
    check(recognize(c,p,1100)==PolicyRecognition::Unknown,"firmware packet is not official Auto strategy");
    p.source=ObservationSource::OfficialConfigRead;p.installedVersionBound=false;
    check(recognize(c,p,1100)==PolicyRecognition::Unknown,"unbound official-version query unknown");
    c.capability.vendor=Vendor::AsusRog;c.capability.binding="FAKE-ASUS-AUTO";
    c.identity={Vendor::AsusRog,Frontend::AsusArmouryCrateSE,true,true,c.capability.binding};
    p.identity=c.identity;p.installedVersionBound=true;p.manualSelection=ManualControlSelection::Auto;p.autoPolicy=AutoSwitchPolicy::Enabled;
    check(recognize(c,p,1100)==PolicyRecognition::AutoContextIncomplete,"ASUS Auto needs actual foreground and library/profile data");
    p.foregroundExePath="G:/FAKE/game.exe";p.foregroundVerified=true;p.registrationVerified=true;p.profileMatchVerified=true;
    p.profileMatch=ProfileMatch::ExePathMismatch;
    check(recognize(c,p,1100)==PolicyRecognition::AutoPathMismatch,"official registered exe mismatch explicit");
    p.profileMatch=ProfileMatch::RegisteredNoCustomProfile;
    check(recognize(c,p,1100)==PolicyRecognition::AutoContextIncomplete,"registered path receipt required");
    p.registeredGamePath=p.foregroundExePath;
    check(recognize(c,p,1100)==PolicyRecognition::OfficialAutoContext,"registered game without custom profile context recorded");
    p.profileMatch=ProfileMatch::ExactRegisteredProfile;
    check(recognize(c,p,1100)==PolicyRecognition::AutoContextIncomplete,"custom profile requires actual profile identity");
    p.effectiveProfileId="FAKE-PROFILE-1";
    check(recognize(c,p,1100)==PolicyRecognition::OfficialAutoContext,"matched profile observation not simulated Auto mode write");
    p.manualSelection=ManualControlSelection::Desktop;p.foregroundVerified=false;
    check(recognize(c,p,1100)==PolicyRecognition::OfficialPolicy&&p.autoPolicy==AutoSwitchPolicy::Enabled,"manual override doesn't overwrite persistent auto-policy field");
    p.owner.hostEpoch--;
    check(recognize(c,p,1100)==PolicyRecognition::Stale,"official read prior epoch rejected");
    c=contract();MappingWriteFacts f;f.owner=c.owner;f.capabilityBinding=c.capability.binding;
    f.protocolProfileId="FAKE-PROTOCOL";f.firmwareVersion="FAKE-FIRMWARE";f.endpointIdentity="FAKE-ENDPOINT";
    f.userAuthorized=true;f.exactProtocolVerified=true;f.endpointVerified=true;
    f.currentOwner=true;f.singleYmccWriterOwned=true;f.hardwareReady=true;
    f.requestedScope=scopeRearButtons;f.approvedScope=scopeRearButtons;
    f.originalExactStateVerified=true;f.originalRestorable=true;f.durableWriteAheadCommitted=true;
    f.originalSource=ObservationSource::ParsedDeviceResponse;
    check(admitMapping(c,f)==MappingAdmission::Allowed,"all-fake exact scoped write admission");
    auto wide=f;wide.requestedScope=allMappingScopes;
    check(admitMapping(c,wide)==MappingAdmission::ScopeExceeded,"full Game/default/button sequence is not M1/M2-only");
    for(unsigned i=0;i<10;++i){
        auto denied=f;MappingAdmission expected=MappingAdmission::OriginalUnknown;
        if(i==0)denied.originalExactStateVerified=false;
        if(i==1)denied.originalRestorable=false;
        if(i==2)denied.originalSource=ObservationSource::ProviderCache;
        if(i==3)denied.originalSource=ObservationSource::UiLabel;
        if(i==4){denied.durableWriteAheadCommitted=false;expected=MappingAdmission::JournalMissing;}
        if(i==5){denied.externalOverrideObserved=true;expected=MappingAdmission::ExternalOverride;}
        if(i==6){denied.singleYmccWriterOwned=false;expected=MappingAdmission::NotOwned;}
        if(i==7){denied.owner.powerGeneration--;expected=MappingAdmission::Stale;}
        if(i==8){denied.exitFence=true;expected=MappingAdmission::Stale;}
        if(i==9){denied.exactProtocolVerified=false;expected=MappingAdmission::IdentityUnknown;}
        check(admitMapping(c,denied)==expected,"mapping gate preserves version/scope/ownership/restore boundaries");
    }
}
int main(){policyTests();arbiterTests();journalTests();chainTests();ownershipTests();frontendAndMappingTests();
    std::cout<<"OEM_GAMEPAD_MODE_SELFTEST_OK cases="<<cases<<" registry=fake controller_io=none runtime_graft=none\n";
}








