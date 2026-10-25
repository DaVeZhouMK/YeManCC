#include "mock_win32.h"
#include "onex_x2_windows.h"
#include "layout.generated.h"

namespace p = ymcc::onex_x2;
using test::check; using test::eq;
using Time = std::uint64_t;
constexpr p::Context initialContext{11, 23};
constexpr Time startAt = 100;
static std::vector<unsigned char> frame(unsigned id, bool pressed = true, std::size_t size = 64, bool prefix = true, unsigned reportId = 0xA7) {
    check(size >= (prefix ? 16u : 15u), "independent frame fixture fits indices");
    std::vector<unsigned char> result(size, 0); const auto offset = prefix ? 1u : 0u;
    if (prefix) result[0] = static_cast<unsigned char>(reportId);
    result[offset] = 0xB2; result[offset+1] = 0x3F; result[offset+6] = static_cast<unsigned char>(id);
    result[offset+12] = pressed ? 1 : 0; result[size-2] = 0x3F; result[size-1] = 0xB2;
    return result;
}
static std::vector<unsigned char> statusFrame(std::size_t size, unsigned status = 0x20, unsigned reportId = 0xA7) {
    check(size >= 15, "raw status fixture fits independent input capacity");
    std::vector<unsigned char> raw(size,0);raw[0]=static_cast<unsigned char>(reportId);
    raw[1]=0xB8;raw[2]=0x3F;raw[6]=static_cast<unsigned char>(status);raw[size-2]=0x3F;raw[size-1]=0xB8;
    return raw;
}
static void tick(p::WindowsTransport& t, Time now) { mock::retained(); t.tick(now); mock::retained(); }
static void boot(p::WindowsTransport& t, p::Model model = p::Model::X2, p::InputLayout layout = reportLayout, Time now = startAt) {
    t.request(model, initialContext, layout, now); tick(t, now);
    eq(t.snapshot().phase, p::Phase::Live, "matching vendor collection becomes live");
    eq(t.snapshot().bound, model, "requested exact model bound"); check(t.hasDevice(), "owned vendor device opened");
}
static void drain(p::WindowsTransport& t, Time now = 10000) {
    t.stop(now); for (auto& io : mock::w.ios) if (!io->observed && !io->terminal) mock::complete(*io, false);
    tick(t, now+1); eq(t.snapshot().phase, p::Phase::Detached, "stop plus terminal drain detaches");
    check(!t.hasDevice() && !t.popEvent(), "stopped device and events gone"); mock::assertClean();
}
static void oneDevice() { mock::reset(); mock::w.devices.push_back(mock::device()); }
static void noIo(const p::WindowsTransport& t) {
    eq(mock::w.guidCalls,0u,"unsupported/unverified selection never requests HID GUID");eq(mock::w.enumerations,0u,"unsupported/unverified selection never enumerates");
    eq(mock::w.probes,0u,"unsupported selection never opens probes"); eq(mock::w.dataOpens,0u,"unsupported selection never opens IO");
    eq(mock::w.readsCalled,0u,"unsupported selection never reads"); eq(mock::w.writesCalled,0u,"unsupported selection never writes");
    check(!t.hasDevice(),"unsupported selection owns no device"); mock::assertClean();
}
static void disabled() {
    test::section="disabled";
    for (const auto model : {p::Model::Unsupported, static_cast<p::Model>(255)}) {
        oneDevice(); p::WindowsTransport t; t.request(model,initialContext,reportLayout,100);
        for (Time now : {100u,4100u,10000u}) tick(t,now); noIo(t);
    }
    for (const auto model : {p::Model::X2,p::Model::X2MiniPro,p::Model::OneXPlayer3})
    for (const auto layout : {p::InputLayout::Unverified,p::InputLayout::LogicalFrame}) {
        oneDevice(); p::WindowsTransport t; t.request(model,initialContext,layout,100);
        for (Time now : {100u,4100u,10000u}) tick(t,now); noIo(t);
    }
}
static void unknownLayout() {
    test::section="unknown-layout"; oneDevice(); p::WindowsTransport t;
    t.request(p::Model::X2,initialContext,static_cast<p::InputLayout>(255),100); tick(t,100); tick(t,4100); noIo(t);
}
static void unverifiedRevokes() {
    test::section="unverified-revoke";
    for(const auto rejectedLayout:{p::InputLayout::Unverified,p::InputLayout::LogicalFrame,static_cast<p::InputLayout>(255)}) { oneDevice(); p::WindowsTransport t; boot(t); tick(t,4100);
    auto& r=mock::pending(false); auto& w=mock::pending(true); const auto old=t.snapshot();
    t.request(p::Model::X2,initialContext,rejectedLayout,4101);
    eq(t.snapshot().phase,p::Phase::Retaining,"Unverified immediately retires existing live session");
    check(t.snapshot().generation>old.generation && t.snapshot().publicationRevision>old.publicationRevision,"Unverified revokes generation and publication first");
    eq(t.snapshot().requested,p::Model::Unsupported,"Unverified removes live requested model");
    mock::complete(r,true,r.size,ERROR_SUCCESS,frame(0x22)); mock::complete(w,true); tick(t,4102);
    check(!t.popEvent(),"late frame after unverified request cannot publish"); tick(t,20000);
    eq(mock::w.enumerations,1u,"rejected raw layout does not re-enumerate after drain"); drain(t,20001);
    }
}
static void filtering() {
    test::section="filtering";
    struct Reject { const char* label; mock::Device d; p::Model model; };
    std::vector<Reject> bad;
    auto add=[&](const char* name,mock::Device d,p::Model model=p::Model::X2){d.path=std::wstring(name,name+std::char_traits<char>::length(name));bad.push_back({name,d,model});};
    auto d=mock::device(); d.vid=0x045E; add("wrong-vid",d);
    d=mock::device(); d.pid=0xFE01; add("wrong-pid",d);
    d=mock::device(); d.page=1; d.usage=5; add("gamepad",d);
    d=mock::device(); d.page=1; d.usage=6; add("keyboard",d);
    d=mock::device(); d.page=0xFF01; add("wrong-page",d);
    d=mock::device(); d.usage=2; add("wrong-vendor-usage",d);
    d=mock::device(); d.input=14; add("short-input",d);
    d=mock::device(); d.input=513; add("large-input",d);
    d=mock::device(); d.input=0; add("zero-input",d);
    for(USHORT n : {USHORT{0},USHORT{63},USHORT{66},USHORT{512}}) {d=mock::device();d.output=n;add("wrong-output",d);}
    d=mock::device();d.pid=0x1305;add("oxp3-excluded-pid",d,p::Model::OneXPlayer3);
    for(unsigned fault=0;fault<7;++fault){d=mock::device();switch(fault){case 0:d.probeOk=false;break;case 1:d.attributesOk=false;break;case 2:d.preparsedOk=false;break;case 3:d.capsOk=false;break;case 4:d.detailOk=false;break;case 5:d.detailSize=1;break;default:d.detailSize=65537;break;}add("failed-descriptor",d);}
    const auto base=test::scenario;
    for(const auto& c:bad){test::scenario=base+":"+c.label;mock::reset();mock::w.devices.push_back(c.d);p::WindowsTransport t;
        t.request(c.model,initialContext,reportLayout,100);tick(t,100);
        eq(t.snapshot().phase,p::Phase::Detached,"rejected collection stays detached");eq(mock::w.dataOpens,0u,"rejected collection never opened for IO");
        eq(mock::w.readsCalled,0u,"rejected collection never read");eq(mock::w.writesCalled,0u,"rejected collection never written");drain(t);}
    for(const auto model:{p::Model::X2,p::Model::X2MiniPro,p::Model::OneXPlayer3})for(USHORT pid:{USHORT{0xFE00},USHORT{0x1305}}){
        if(model==p::Model::OneXPlayer3&&pid==0x1305)continue;
        oneDevice();mock::w.devices[0].pid=pid;p::WindowsTransport t;boot(t,model);eq(mock::w.dataOpens,1u,"exact allowed VID/PID/usage accepted");drain(t);}
}
static void multiCollection(){
    test::section="filtering";mock::reset();auto game=mock::device(L"gamepad");game.page=1;game.usage=5;
    auto key=mock::device(L"keyboard");key.page=1;key.usage=6;
    mock::w.devices={game,key,mock::device(L"vendor"),mock::device(L"unvisited")};p::WindowsTransport t;boot(t);tick(t,4100);
    eq(mock::w.probes,3u,"probes stop at first exact vendor");eq(mock::w.dataOpens,1u,"only one selected vendor IO open");
    for(const auto& s:mock::w.submissions)eq(mock::object(s.device)->index,std::size_t{2},"both read and write target vendor collection only");drain(t);
}
static std::vector<unsigned char> expectedPage(p::Model model,std::size_t index,std::size_t size){
    // Independent fixed HC payload oracle; do not call production initStep/encodeReport.
    std::vector<unsigned char> payload;
    if(index==0){payload={2,0x38,0x20,1,1};for(unsigned id=1;id<=9;++id)payload.insert(payload.end(),{static_cast<unsigned char>(id),1,static_cast<unsigned char>(id),0,0,0});}
    else if(index==1){payload={2,0x38,0x20,2,1};for(unsigned id=10;id<=16;++id)payload.insert(payload.end(),{static_cast<unsigned char>(id),1,static_cast<unsigned char>(id),0,0,0});
        payload.insert(payload.end(),{0x22,2,1,0x67,0,0,0x23,2,1,0x66,0,0});
        if(model==p::Model::X2MiniPro){payload[50]=0x68;payload[56]=0x69;}}
    else if(index==2)payload={2,0x38,0x20,3,1,0x24,2,2,5,0,0,0x25,1,0x21,0,0,0};
    else payload={0,1,2};
    if(model==p::Model::X2MiniPro&&index<3)payload[2]=2;
    std::vector<unsigned char> report(size,0);const auto offset=size==65?1u:0u;const auto command=index==3?0xB2u:0xB4u;
    report[offset]=static_cast<unsigned char>(command);report[offset+1]=0x3F;report[offset+2]=1;
    std::copy(payload.begin(),payload.end(),report.begin()+offset+3);report[offset+62]=0x3F;report[offset+63]=static_cast<unsigned char>(command);return report;
}
static void initialization(){
    test::section="initialization";
    for(const auto model:{p::Model::X2,p::Model::X2MiniPro,p::Model::OneXPlayer3})for(USHORT input:{USHORT{15},USHORT{32},USHORT{64},USHORT{65},USHORT{512}})for(USHORT output:{USHORT{64},USHORT{65}}){
        oneDevice();mock::w.devices[0].input=input;mock::w.devices[0].output=output;p::WindowsTransport t;boot(t,model);
        eq(mock::w.readsCalled,1u,"single read queued at insertion");eq(mock::pending(false).size,DWORD{input},"input length independent of output length");
        tick(t,4099);eq(mock::w.writesCalled,0u,"no init write before full 4000ms insertion delay");Time now=4100;
        const std::size_t pages=model==p::Model::X2MiniPro?4u:3u;
        for(std::size_t i=0;i<pages;++i){tick(t,now);eq(mock::w.writesCalled,static_cast<unsigned>(i+1),"one expected init page at due time");
            auto& op=mock::pending(true);eq(op.retained,expectedPage(model,i,output),"exact independent 64/65-byte init payload");
            eq(t.snapshot().initIndex,i,"submission alone never advances init page");check(!t.snapshot().initialized,"pending init is not initialized");
            tick(t,now+6);eq(t.snapshot().initIndex,i,"event timeout cannot advance init");mock::complete(op,true);tick(t,now+7);
            eq(t.snapshot().initIndex,i+1,"actual terminal completion advances init");check(t.snapshot().initialized==(model==p::Model::X2MiniPro&&i==3),"only zero-delay final terminal may initialize in same tick");
            const unsigned delay=i<3?50u:0u;now+=7+delay;
            if(delay){tick(t,now-1);eq(mock::w.writesCalled,static_cast<unsigned>(i+1),"50ms is measured from terminal completion");check(!t.snapshot().initialized,"last-page delay honored");}
        }
        tick(t,now);check(t.snapshot().initialized,"all 3/4 pages terminal before initialized");eq(mock::w.writesCalled,static_cast<unsigned>(pages),"no extra init page");drain(t,now+100);
    }
}
static void retention(){
    test::section="retention";
    for(bool readFirst:{false,true})for(bool cancelSuccess:{false,true}){
        oneDevice();mock::w.cancelSucceeds=cancelSuccess;p::WindowsTransport t;boot(t);tick(t,4100);auto& r=mock::pending(false);auto& w=mock::pending(true);
        const auto* rp=r.buffer;const auto* wp=w.buffer;auto* ro=r.ov;auto* wo=w.ov;const auto device=r.device;
        t.stop(4101);eq(mock::w.cancellations,2u,"stop cancels each pending direction exactly once");
        for(Time now:{4102u,5200u,20000u}){tick(t,now);check(t.hasDevice(),"cancel success/failure and elapsed time cannot release pending handles");
            eq(t.snapshot().phase,p::Phase::Retaining,"retain until dual terminal");eq(mock::w.cancellations,2u,"repeated ticks never recancel");}
        check(t.snapshot().retainedNotice,"retained notice is diagnostic only");
        eq(r.buffer,rp,"read buffer stable");eq(w.buffer,wp,"write buffer stable");eq(r.ov,ro,"read OVERLAPPED stable");eq(w.ov,wo,"write OVERLAPPED stable");eq(r.device,device,"device handle stable");
        mock::complete(readFirst?r:w,false);tick(t,20001);check(t.hasDevice(),"first direction terminal does not free second");eq(mock::w.closeDevices,0u,"device retained after first terminal");
        mock::complete(readFirst?w:r,false);tick(t,20002);eq(t.snapshot().phase,p::Phase::Detached,"second terminal permits finalization");check(!t.hasDevice(),"device released only after both observed terminals");mock::assertClean();
    }
}
static void contextChange(){
    test::section="context-revoke";
    for(bool power:{false,true}){
        oneDevice();p::WindowsTransport t;boot(t);auto old=t.snapshot();auto& first=mock::pending(false);
        mock::complete(first,true,first.size,ERROR_SUCCESS,frame(0x22));tick(t,101);check(t.snapshot().queuedEvents>0 && t.snapshot().rearBits==1,"old publication exists before context change");
        auto& invalid=mock::pending(false);mock::complete(invalid,true,invalid.size-1);tick(t,102);
        eq(t.snapshot().invalidFrames,std::uint64_t{1},"old binding holds invalid count before rebind");
        auto& r=mock::pending(false);mock::complete(r,true,r.size,ERROR_SUCCESS,frame(0x23));auto next=initialContext;if(power)++next.power;else ++next.topology;
        t.request(p::Model::X2,next,reportLayout,103);const auto revoked=t.snapshot();
        check(revoked.generation>old.generation && revoked.publicationRevision>old.publicationRevision,"generation revoked synchronously before callback");
        eq(revoked.rearBits,std::uint8_t{0},"published rear state revoked first");check(!t.popEvent(),"old queued events revoked before callback");
        tick(t,104);check(!t.popEvent(),"already completed late read cannot publish after context change");eq(t.snapshot().validFrames,std::uint64_t{1},"late read never counted as live valid frame");
        check(!t.hasDevice(),"old source closes after observed terminal");tick(t,2102);eq(mock::w.enumerations,1u,"retirement backoff not bypassed");tick(t,2103);
        eq(t.snapshot().phase,p::Phase::Live,"new context can bind after backoff");eq(t.snapshot().context,next,"rebound session uses new context");
        check(t.snapshot().readPending,"rebound reader pending without any completed sample");
        eq(t.snapshot().validFrames,std::uint64_t{0},"rebind clears old-source valid count before first fresh completion");
        eq(t.snapshot().invalidFrames,std::uint64_t{0},"rebind clears old-source invalid count");
        check(!t.snapshot().reportIdObserved && t.snapshot().lastReportId==0,"rebind clears observed report ID before first fresh completion");
        auto& current=mock::pending(false);mock::complete(current,true,current.size,ERROR_SUCCESS,frame(0x23));tick(t,2104);auto event=t.popEvent();check(event.has_value(),"new generation frame publishes");
        eq(event->context,next,"new event context");eq(event->generation,t.snapshot().generation,"new event generation");eq(event->rearBits,std::uint8_t{2},"new generation does not inherit old rear state");drain(t);
    }
}
static void waitFailed(){
    test::section="wait-failed";oneDevice();p::WindowsTransport t;boot(t);tick(t,4100);auto& r=mock::pending(false);auto& w=mock::pending(true);
    mock::object(r.event)->waitOverride=WAIT_FAILED;tick(t,4101);
    eq(t.snapshot().phase,p::Phase::Retaining,"WAIT_FAILED retires but does not complete pending IO");eq(t.snapshot().reason,p::StopReason::ObservationFailed,"observation failure is explicit");
    const auto deadline=t.snapshot().retryAt;const auto revision=t.snapshot().publicationRevision;
    for(Time now:{4102u,5102u,10000u}){tick(t,now);eq(t.snapshot().retryAt,deadline,"repeated observation errors cannot extend retry deadline");eq(t.snapshot().publicationRevision,revision,"repeated errors cannot revoke twice");check(t.hasDevice(),"WAIT_FAILED cannot free pending IO");}
    check(t.snapshot().retainedNotice,"repeat errors do not postpone retention notice");eq(mock::w.cancellations,2u,"once-only cancellation even across observation failures");
    t.stop(10000);eq(t.snapshot().retryAt,deadline,"explicit stop while retaining does not restart deadline");
    mock::complete(r,false,r.size,ERROR_OPERATION_ABORTED,{},false);tick(t,10001);check(t.hasDevice(),"SDK Internal terminal drains even when event wait fails, retaining other direction");
    mock::complete(w,false,w.size,ERROR_OPERATION_ABORTED,{},false);tick(t,10002);check(!t.hasDevice(),"both Internal terminals drain without signaled event");mock::assertClean();
}
static void incompleteResult(){
    test::section="result-pending";
    for(DWORD error:{DWORD{ERROR_IO_INCOMPLETE},DWORD{ERROR_INVALID_HANDLE},DWORD{ERROR_ACCESS_DENIED}}){
        oneDevice();p::WindowsTransport t;boot(t);auto& r=mock::pending(false);mock::object(r.event)->signaled=true;r.observationError=error;
        tick(t,101);check(t.hasDevice() && t.snapshot().readPending,"nonterminal GOR cannot clear operation or free resource");
        if(error==ERROR_IO_INCOMPLETE)eq(t.snapshot().phase,p::Phase::Live,"IO_INCOMPLETE is transient");
        else {eq(t.snapshot().phase,p::Phase::Retaining,"other errors with pending Internal retire observation only");eq(t.snapshot().reason,p::StopReason::ObservationFailed,"nonterminal error not misclassified as terminal read failure");}
        const auto deadline=t.snapshot().retryAt;for(Time now:{102u,1102u,3000u}){tick(t,now);check(t.hasDevice(),"repeated nonterminal result retains ownership");eq(t.snapshot().retryAt,deadline,"nonterminal repeated error does not refresh retirement");}
        mock::complete(r,false,r.size,ERROR_OPERATION_ABORTED);tick(t,3001);drain(t,3002);
    }
}
static void incompleteDespiteInternal(){
    test::section="result-pending";oneDevice();p::WindowsTransport t;boot(t);auto& r=mock::pending(false);
    // A spurious terminal-looking Internal field must not override explicit IO_INCOMPLETE.
    r.ov->Internal=0;mock::object(r.event)->signaled=true;tick(t,101);
    check(t.snapshot().readPending && t.hasDevice(),"ERROR_IO_INCOMPLETE retains operation even with terminal-looking Internal");drain(t);
}
static void failures(){
    test::section="submission";
    for(bool writing:{false,true})for(bool resetFailure:{false,true}){
        oneDevice();if(resetFailure)mock::w.failResetAt=writing?2u:1u;
        else (writing?mock::w.writes:mock::w.reads).push_back({mock::Mode::Fail,ERROR_ACCESS_DENIED});
        p::WindowsTransport t;t.request(p::Model::X2,initialContext,reportLayout,100);tick(t,100);if(writing)tick(t,4100);
        eq(t.snapshot().reason,writing?p::StopReason::WriteFailed:p::StopReason::ReadFailed,"submit/reset failure retires appropriate source");
        if(resetFailure)eq(writing?mock::w.writesCalled:mock::w.readsCalled,0u,"ResetEvent failure never invokes ReadFile/WriteFile");
        eq(t.lastError(),resetFailure?DWORD{ERROR_INVALID_HANDLE}:DWORD{ERROR_ACCESS_DENIED},"transport retains submit/reset error");drain(t);
    }
}
static void immediate(){
    test::section="immediate";
    for(const auto model:{p::Model::X2,p::Model::X2MiniPro,p::Model::OneXPlayer3})for(USHORT output:{USHORT{64},USHORT{65}}){
        oneDevice();mock::w.devices[0].output=output;mock::Plan r;r.mode=mock::Mode::Immediate;r.frame=frame(0x22);mock::w.reads.push_back(r);
        const unsigned pages=model==p::Model::X2MiniPro?4u:3u;for(unsigned i=0;i<pages;++i)mock::w.writes.push_back({mock::Mode::Immediate});
        p::WindowsTransport t;boot(t,model);auto e=t.popEvent();check(e&&e->rearBits==1&&e->generation==t.snapshot().generation,"immediate read publishes same-generation event");
        for(unsigned i=0;i<pages;++i){tick(t,4100+50*i);eq(t.snapshot().initIndex,std::size_t{i+1},"immediate true write advances exactly once");eq(mock::w.submissions.back().data,expectedPage(model,i,output),"immediate init payload exact");}
        tick(t,4300);check(t.snapshot().initialized,"immediate completions initialize without GOR callbacks");drain(t);
    }
}
static void shortWrites(){
    test::section="short-write";
    for(bool immediate:{false,true})for(USHORT length:{USHORT{64},USHORT{65}}){
        oneDevice();mock::w.devices[0].output=length;if(immediate)mock::w.writes.push_back({mock::Mode::Immediate,ERROR_SUCCESS,DWORD(length-1)});
        p::WindowsTransport t;boot(t);tick(t,4100);if(!immediate){auto& io=mock::pending(true);mock::complete(io,true,DWORD(length-1));tick(t,4101);}
        eq(t.snapshot().reason,p::StopReason::WriteFailed,"successful short write is not accepted");eq(t.snapshot().initIndex,std::size_t{0},"short write cannot advance initialization");check(!t.snapshot().initialized,"short write cannot initialize");drain(t);
    }
}
static void timeout(){
    test::section="timeout";oneDevice();p::WindowsTransport t;boot(t);tick(t,4100);tick(t,4349);eq(t.snapshot().phase,p::Phase::Live,"249ms write still pending");
    tick(t,4350);eq(t.snapshot().phase,p::Phase::Retaining,"250ms write retires");eq(t.snapshot().reason,p::StopReason::WriteTimeout,"write timeout reason");
    check(t.hasDevice(),"write timeout is not a terminal IO result");eq(mock::w.cancellations,2u,"timed-out read/write canceled once");
    auto& r=mock::pending(false);auto& w=mock::pending(true);mock::complete(w,true);tick(t,4351);eq(t.snapshot().initIndex,std::size_t{0},"late successful write cannot advance retired generation");
    mock::complete(r,true,r.size,ERROR_SUCCESS,frame(0x22));tick(t,4352);check(!t.popEvent(),"late read after write timeout suppressed");
    tick(t,6349);eq(mock::w.enumerations,1u,"no early rebind");tick(t,6350);eq(mock::w.enumerations,2u,"rebind once timeout backoff elapsed");drain(t);
}
static void rebind(){
    test::section="rebind";mock::reset();p::WindowsTransport t;t.request(p::Model::X2,initialContext,reportLayout,100);tick(t,100);
    for(Time now:{101u,1000u,2099u})tick(t,now);eq(mock::w.enumerations,1u,"missing-device enumeration has 2000ms backoff");
    mock::w.devices.push_back(mock::device());tick(t,2100);eq(mock::w.enumerations,2u,"new device can bind at retry boundary");eq(t.snapshot().phase,p::Phase::Live,"retry binds exact device");drain(t);
}
static void bindFailures(){
    test::section="bind-failure";
    for(unsigned failure=0;failure<4;++failure){oneDevice();if(failure==0)mock::w.failSet=true;if(failure==1)mock::w.devices[0].openOk=false;
        if(failure>=2)mock::w.failEventAt=failure-1;p::WindowsTransport t;t.request(p::Model::X2,initialContext,reportLayout,100);tick(t,100);
        check(!t.hasDevice()&&t.snapshot().phase==p::Phase::Detached,"enumeration/open/event failure leaves no partially owned device");
        eq(mock::w.readsCalled,0u,"bind failure never submits input");eq(mock::w.writesCalled,0u,"bind failure never writes");drain(t);
    }
}
static void readerEvents(){
    test::section="reader-events";
    for(const auto model:{p::Model::X2,p::Model::X2MiniPro,p::Model::OneXPlayer3})for(bool strictZero:{false,true}){
        const bool prefix=true;
        oneDevice();mock::w.devices[0].input=prefix?33:32;mock::w.devices[0].output=65;p::WindowsTransport t;boot(t,model,strictZero?p::InputLayout::ZeroReportIdThenFrame:reportLayout);
        const auto generation=t.snapshot().generation;Time now=101;
        const std::vector<unsigned> ids={0x22,0x23,0x22,0x23,0x20,0x21,0x24};
        const std::vector<unsigned> bits={1,3,2,0,0,0,0};
        for(std::size_t i=0;i<ids.size();++i){auto& io=mock::pending(false);const bool pressed=i<2||i>=4;auto bytes=frame(ids[i],pressed,io.size,prefix,strictZero?0:0xB3);
            mock::complete(io,true,io.size,ERROR_SUCCESS,bytes);tick(t,now);auto e=t.popEvent();check(e.has_value(),"same-generation button frame creates expected event");
            eq(e->generation,generation,"reader event belongs to current generation");eq(e->context,initialContext,"reader event belongs to current context");eq(e->capturedAt,now,"event uses completion timestamp");
            eq(static_cast<unsigned>(e->rearBits),bits[i],"L4/R4 state semantic left=1 right=2");
            const auto expected=i<4?(ids[i]==0x22?p::Button::L4:p::Button::R4):ids[i]==0x20?p::Button::Turbo:ids[i]==0x21?(model==p::Model::X2MiniPro?p::Button::Keyboard:p::Button::Home):(model==p::Model::X2?p::Button::Keyboard:p::Button::Home);
            eq(e->button,expected,"exact model-specific vendor ID mapping");eq(e->kind,i<4?p::EventKind::RearChanged:p::EventKind::Click,"click and held-state event distinct");check(!t.popEvent(),"no duplicate event per frame");++now;
        }
        auto& same=mock::pending(false);mock::complete(same,true,same.size,ERROR_SUCCESS,frame(0x22,false,same.size,prefix,strictZero?0:0xB3));tick(t,now++);auto observed=t.popEvent();check(observed.has_value(),"actual redundant UP retained for cross-generation release fence");eq(observed->kind,p::EventKind::RearObserved,"redundant UP is observation, not changed state");eq(observed->rearBits,std::uint8_t{0},"redundant UP remains neutral");check(!t.popEvent(),"single redundant UP observation");
        auto& unknown=mock::pending(false);mock::complete(unknown,true,unknown.size,ERROR_SUCCESS,frame(0x99,true,unknown.size,prefix,strictZero?0:0xB3));tick(t,now++);check(!t.popEvent(),"unknown vendor ID does not fabricate key");
        auto& shortRead=mock::pending(false);mock::complete(shortRead,true,shortRead.size-1,ERROR_SUCCESS,frame(0x22,true,shortRead.size,prefix,strictZero?0:0xB3));tick(t,now++);check(!t.popEvent(),"short read cannot publish");eq(t.snapshot().invalidFrames,std::uint64_t{1},"short read recorded invalid");drain(t);
    }
}
static void terminalReadError(){
    test::section="reader-error";oneDevice();p::WindowsTransport t;boot(t);tick(t,4100);auto& r=mock::pending(false);
    mock::complete(r,false,r.size,ERROR_DEVICE_NOT_CONNECTED);tick(t,4101);eq(t.snapshot().reason,p::StopReason::ReadFailed,"observed terminal read error retires");
    eq(t.lastError(),DWORD{ERROR_DEVICE_NOT_CONNECTED},"terminal read error preserved");check(t.hasDevice(),"pending write still retained after terminal read error");drain(t);
}
static void reopenedIdentity(){
    test::section="reopen-identity";
    for(unsigned fault=0;fault<10;++fault){
        oneDevice();auto replacement=std::make_shared<mock::Device>(mock::device());
        switch(fault){case 0:replacement->page=1;replacement->usage=5;break;
        case 1:replacement->page=1;replacement->usage=6;break;case 2:replacement->input=65;break;
        case 3:replacement->output=65;break;case 4:replacement->vid=0x045E;break;case 5:replacement->pid=0xFE01;break;
        case 6:replacement->usage=2;break;case 7:replacement->attributesOk=false;break;
        case 8:replacement->preparsedOk=false;break;default:replacement->capsOk=false;break;}
        mock::w.devices[0].reopened=replacement;p::WindowsTransport t;
        t.request(p::Model::X2,initialContext,reportLayout,100);tick(t,100);
        eq(mock::w.dataOpens,1u,"race occurs only after successful zero-access probe");
        eq(mock::w.closeDevices,1u,"mismatched actual owner handle closed");eq(mock::w.createEvents,0u,"reopen identity/caps mismatch allocates no events");
        eq(mock::w.readsCalled,0u,"reopen mismatch never calls ReadFile");eq(mock::w.writesCalled,0u,"reopen mismatch never calls WriteFile");
        check(!t.hasDevice()&&t.snapshot().phase==p::Phase::Detached,"reopen mismatch cannot bind session");
        eq(t.lastError(),DWORD{ERROR_INVALID_DATA},"reopen mismatch explicitly reported");drain(t);
    }
}
static void rawFraming(){
    test::section="raw-framing";
    for(const auto model:{p::Model::X2,p::Model::X2MiniPro,p::Model::OneXPlayer3})
    for(bool reverse:{false,true}){
        oneDevice();mock::w.devices[0].input=reverse?65:64;mock::w.devices[0].output=reverse?64:65;
        p::WindowsTransport t;boot(t,model);check(!t.snapshot().reportIdObserved,"new pending source has no observed report ID");
        eq(t.snapshot().validFrames,std::uint64_t{0},"pending first read supplies no readiness evidence");
        Time now=101;
        for(unsigned id=0;id<256;++id){auto& r=mock::pending(false);auto bytes=frame(0x20,true,r.size,true,id);
            mock::complete(r,true,r.size,ERROR_SUCCESS,bytes);tick(t,now++);auto e=t.popEvent();
            check(e.has_value()&&e->button==p::Button::Turbo,"all report IDs stripped exactly once before logical decoding");
            check(t.snapshot().reportIdObserved,"raw report ID observed");eq(t.snapshot().lastReportId,static_cast<std::uint8_t>(id),"raw[0] ID retained without zero-only restriction");
            eq(t.snapshot().validFrames,std::uint64_t{id+1},"one valid frame per complete raw input");
        }
        for(int delta:{-1,1}){auto& r=mock::pending(false);mock::complete(r,true,static_cast<DWORD>(static_cast<int>(r.size)+delta),ERROR_SUCCESS,frame(0x20,true,r.size,true,0xF3));
            tick(t,now++);check(!t.popEvent(),"actual read byte count must equal independent input capacity exactly");
            eq(t.snapshot().lastReportId,std::uint8_t{255},"wrong-size report does not replace observed ID");}
        auto& raw=mock::pending(false);mock::complete(raw,true,raw.size,ERROR_SUCCESS,frame(0x20,true,raw.size,false));tick(t,now++);
        check(!t.popEvent(),"unprefixed logical frame is not retried at another offset");eq(t.snapshot().invalidFrames,std::uint64_t{3},"short long and wrong-offset frames rejected");
        tick(t,4100);eq(mock::pending(true).size,DWORD{reverse?64u:65u},"output length chosen independently after raw64/65 reads");drain(t);
    }
    oneDevice();p::WindowsTransport strict;boot(strict,p::Model::X2,p::InputLayout::ZeroReportIdThenFrame);
    auto& r=mock::pending(false);mock::complete(r,true,r.size,ERROR_SUCCESS,frame(0x20,true,r.size,true,7));tick(strict,101);
    check(!strict.popEvent()&&strict.snapshot().invalidFrames==1,"optional strict zero-ID layout rejects nonzero ID");
    check(strict.snapshot().reportIdObserved&&strict.snapshot().lastReportId==7,"strict variant retains observed rejected ID for evidence");drain(strict);
}

static void terminalWriteError(){
    test::section="writer-error";
    for(DWORD error:{DWORD{ERROR_DEVICE_NOT_CONNECTED},DWORD{ERROR_OPERATION_ABORTED}}){
        oneDevice();p::WindowsTransport t;boot(t);tick(t,4100);auto& w=mock::pending(true);
        mock::complete(w,false,0,error);tick(t,4101);eq(t.snapshot().phase,p::Phase::Retaining,"terminal failed write retains outstanding read");
        eq(t.snapshot().reason,p::StopReason::WriteFailed,"terminal write error retires source");eq(t.lastError(),error,"terminal write error preserved");
        eq(t.snapshot().initIndex,std::size_t{0},"failed terminal write does not advance init");
        eq(mock::pending(false).cancellations,1u,"terminal failed write cancels pending read once");drain(t);
    }
}
static void immediateShortRead(){
    test::section="raw-framing";oneDevice();mock::Plan plan;plan.mode=mock::Mode::Immediate;plan.bytes=63;plan.frame=frame(0x20);
    mock::w.reads.push_back(plan);p::WindowsTransport t;boot(t);check(!t.popEvent(),"immediate successful short read cannot publish");
    eq(t.snapshot().invalidFrames,std::uint64_t{1},"immediate short byte count is invalid");check(!t.snapshot().reportIdObserved,"short read does not supply report ID evidence");drain(t);
}

static void validLayoutChange(){
    test::section="layout-change";
    for(const auto model:{p::Model::X2,p::Model::X2MiniPro,p::Model::OneXPlayer3})for(bool fromZero:{false,true}){
        oneDevice();const auto before=fromZero?p::InputLayout::ZeroReportIdThenFrame:reportLayout;
        const auto after=fromZero?reportLayout:p::InputLayout::ZeroReportIdThenFrame;
        p::WindowsTransport t;boot(t,model,before);auto& first=mock::pending(false);
        mock::complete(first,true,first.size,ERROR_SUCCESS,frame(0x22,true,first.size,true,0));tick(t,101);
        eq(t.snapshot().rearBits,std::uint8_t{1},"old layout published held rear state");check(t.snapshot().queuedEvents>0,"old layout has queued event to revoke");
        tick(t,4100);auto& r=mock::pending(false);auto& w=mock::pending(true);const auto old=t.snapshot();
        t.request(model,initialContext,after,4101);
        eq(t.snapshot().phase,p::Phase::Retaining,"same model/context valid-layout change must retire old contract");
        eq(t.snapshot().reason,p::StopReason::SelectionChanged,"layout change is an explicit selection retirement");
        check(t.snapshot().generation>old.generation&&t.snapshot().publicationRevision>old.publicationRevision,"valid-layout change revokes source synchronously");
        eq(t.snapshot().rearBits,std::uint8_t{0},"old layout held state revoked");check(!t.popEvent(),"old layout queue revoked before delayed completion");
        mock::complete(r,true,r.size,ERROR_SUCCESS,frame(0x23,true,r.size,true,0));mock::complete(w,true);tick(t,4102);
        check(!t.popEvent()&&t.snapshot().validFrames==1,"late old-layout read is neither published nor counted");
        eq(t.snapshot().initIndex,std::size_t{0},"late old-contract write cannot progress init");check(!t.hasDevice(),"old layout owner handle fully drained");
        tick(t,6100);eq(mock::w.enumerations,1u,"layout change respects retirement backoff");tick(t,6101);
        eq(t.snapshot().phase,p::Phase::Live,"new layout rebinds same model and context");
        check(t.snapshot().readPending&&!t.snapshot().reportIdObserved,"new contract awaits fresh full input");
        eq(t.snapshot().validFrames,std::uint64_t{0},"layout rebind resets valid-frame evidence");eq(t.snapshot().invalidFrames,std::uint64_t{0},"layout rebind resets invalid-frame evidence");
        auto& nonzero=mock::pending(false);mock::complete(nonzero,true,nonzero.size,ERROR_SUCCESS,frame(0x20,true,nonzero.size,true,0x5A));tick(t,6102);
        auto e=t.popEvent();check(e.has_value()==fromZero,"new layout contract governs nonzero ID acceptance");
        eq(t.snapshot().validFrames,std::uint64_t{fromZero?1u:0u},"new-layout valid count");eq(t.snapshot().invalidFrames,std::uint64_t{fromZero?0u:1u},"new-layout invalid count");
        eq(t.snapshot().lastReportId,std::uint8_t{0x5A},"new contract retains observed report ID");
        auto& zero=mock::pending(false);mock::complete(zero,true,zero.size,ERROR_SUCCESS,frame(0x20,true,zero.size,true,0));tick(t,6103);
        e=t.popEvent();check(e.has_value(),"zero-ID valid input works after either layout change");eq(e->generation,t.snapshot().generation,"post-layout event uses new generation");drain(t);
    }
}
static void stopImmediateRelease(){
    test::section="stop-quiescent";oneDevice();mock::Plan immediate;immediate.mode=mock::Mode::Immediate;immediate.frame=frame(0x22);
    mock::w.reads.push_back(immediate);p::WindowsTransport t;boot(t);
    check(t.hasDevice()&&!t.snapshot().readPending&&!t.snapshot().writePending,"live device with immediate read has no pending IO");
    check(t.snapshot().queuedEvents>0,"quiescent source has event to clear");const auto generation=t.snapshot().generation;
    t.stop(101); // No later owner tick is assumed or invoked before assertions.
    check(!t.hasDevice(),"stop immediately closes device with no pending IO");eq(t.snapshot().phase,p::Phase::Detached,"quiescent stop detached synchronously");
    eq(mock::w.closeDevices,1u,"quiescent stop closes actual owner handle now");eq(mock::w.closeEvents,2u,"quiescent stop closes both events now");
    eq(mock::w.cancellations,0u,"quiescent stop has no nonexistent IO to cancel");check(!t.popEvent(),"quiescent stop revokes queued events");
    check(t.snapshot().generation>generation,"quiescent stop revokes generation");mock::assertClean();t.stop(102);mock::assertClean();
}
static void stopPendingRetention(){
    test::section="stop-pending";
    for(bool withWrite:{false,true}){oneDevice();p::WindowsTransport t;boot(t);if(withWrite)tick(t,4100);
        const Time now=withWrite?4101:101;t.stop(now);
        check(t.hasDevice()&&t.snapshot().phase==p::Phase::Retaining,"stop cannot immediately close pending source");
        eq(mock::w.closeDevices,0u,"pending device not closed by stop finalize");eq(mock::w.closeEvents,0u,"pending events not closed by stop finalize");
        eq(mock::w.cancellations,withWrite?2u:1u,"pending stop cancels each direction once");
        mock::retained();tick(t,now+10000);check(t.hasDevice(),"elapsed time never replaces pending terminal proof");drain(t,now+10001);
    }
}

static void statusAtInitializationBoundary(p::Model model){
    test::section="initialization-status-boundary";
    const unsigned pages=model==p::Model::X2MiniPro?4u:3u;
    for(USHORT output:{USHORT{64},USHORT{65}})for(bool immediate:{true,false}){
        oneDevice();mock::w.devices[0].input=output==64?65:64;mock::w.devices[0].output=output;
        if(immediate)for(unsigned page=0;page<pages;++page)mock::w.writes.push_back({mock::Mode::Immediate});
        p::WindowsTransport t;boot(t,model,reportLayout,0);const auto generation=t.snapshot().generation;
        for(unsigned page=0;page<pages;++page){
            const Time due=4000+50*page;tick(t,due);
            eq(mock::w.writesCalled,page+1,"one initial page at exact 4000/4050/4100/4150ms boundary");
            eq(mock::w.submissions.back().data,expectedPage(model,page,output),"boundary init page matches independent output oracle");
            if(!immediate){
                eq(t.snapshot().initIndex,std::size_t{page},"pending initial page has not reached terminal");
                auto& w=mock::pending(true);mock::complete(w,true);tick(t,due);
            }
            eq(t.snapshot().initIndex,std::size_t{page+1},"initial page terminal is observed at its exact timestamp");
        }
        // X2/OXP3: third completion at 4100 leaves a 50ms delay until 4150.
        // Mini: fourth page completes immediately in startWrite at 4150 with
        // zero delay, AFTER that tick's advance. No extra tick may pre-settle it
        // before the B8 read. Pending Mini terminal observation instead already
        // runs advance at 4150 and is checked as the complementary ordering.
        const bool settledByAsyncMini=model==p::Model::X2MiniPro&&!immediate;
        eq(t.snapshot().initialized,settledByAsyncMini,"initialization state immediately before exact-due status read");
        eq(t.snapshot().reinitializations,0u,"no prior status retry");
        check(!t.snapshot().writePending,"all initial page writes proven terminal before status boundary");
        auto& r=mock::pending(false);mock::complete(r,true,r.size,ERROR_SUCCESS,statusFrame(r.size));
        tick(t,4150); // service(read) MUST run before advance inside the real transport.
        check(t.snapshot().initialized,"status read locally settles due initialization");
        eq(t.snapshot().phase,p::Phase::Live,"exact-due status read preserves live source");
        eq(t.snapshot().validFrames,std::uint64_t{1},"B8/20 is one valid raw status frame");
        eq(t.snapshot().invalidFrames,std::uint64_t{0},"raw status framing is valid");
        check(!t.popEvent(),"status does not fabricate button event");
        eq(t.snapshot().reinitializations,0u,"status retry is deferred for 200ms");
        eq(mock::w.writesCalled,pages,"no immediate reinit submission at 4150");
        tick(t,4349);eq(mock::w.writesCalled,pages,"no retry at 199ms after status");
        tick(t,4350);eq(mock::w.writesCalled,pages+1,"exact-due B8/20 must start retry page at 4350");
        eq(t.snapshot().reinitializations,1u,"status schedules one reinitialization");
        check(t.snapshot().writePending&&!t.snapshot().initialized,"reinit page submission awaits its real terminal");
        eq(t.snapshot().initIndex,std::size_t{0},"reinit submission alone cannot complete first page");
        eq(mock::pending(true).retained,expectedPage(model,0,output),"4350ms retry is the correct first init page");
        eq(t.snapshot().generation,generation,"in-source reconfiguration does not fake a rebind");
        auto& retry=mock::pending(true);mock::complete(retry,true);tick(t,4351);
        eq(t.snapshot().initIndex,std::size_t{1},"retry advances only after actual terminal");drain(t,4352);
    }
}
static void statusInitX2(){statusAtInitializationBoundary(p::Model::X2);statusAtInitializationBoundary(p::Model::OneXPlayer3);}
static void statusInitMini(){statusAtInitializationBoundary(p::Model::X2MiniPro);}
static void statusWriteWatchdogOrdering(){
    test::section="status-watchdog-order";
    for(const auto model:{p::Model::X2,p::Model::X2MiniPro})for(bool terminalBeforeTick:{false,true}){
        oneDevice();p::WindowsTransport t;boot(t,model,reportLayout,0);tick(t,4000);
        const auto generation=t.snapshot().generation;auto& w=mock::pending(true);auto& r=mock::pending(false);
        if(terminalBeforeTick)mock::complete(w,true);
        mock::complete(r,true,r.size,ERROR_SUCCESS,statusFrame(r.size));tick(t,4250);
        if(terminalBeforeTick){
            eq(t.snapshot().phase,p::Phase::Live,"read status must not run watchdog before observing terminal write");
            eq(t.snapshot().reason,p::StopReason::None,"same-tick proven write terminal cannot become artificial timeout");
            eq(t.snapshot().initIndex,std::size_t{1},"already-terminal write completes before watchdog check");
            eq(t.snapshot().generation,generation,"terminal-at-deadline source not revoked");
        }else{
            eq(t.snapshot().phase,p::Phase::Retaining,"status cannot suppress a genuinely pending write timeout");
            eq(t.snapshot().reason,p::StopReason::WriteTimeout,"still-pending 250ms write retires via existing advance watchdog");
            eq(t.snapshot().initIndex,std::size_t{0},"nonterminal write does not advance init");
            check(t.hasDevice(),"watchdog still retains pending write until terminal");
        }
        eq(t.snapshot().reinitializations,0u,"status during unfinished initialization cannot start retry");drain(t,4251);
    }
}

struct Case { const char* name; void (*run)(); };

static unsigned writePermissionCalls = 0;
static bool writePermissionOpen = false;
static bool gatedPermission() { ++writePermissionCalls; return writePermissionOpen; }
static bool closesBeforeSubmit() { return ++writePermissionCalls == 1; }
static void writePermissionGate() {
    test::section="write-permission";
    oneDevice(); p::WindowsTransport t; boot(t); writePermissionCalls=0; writePermissionOpen=false;
    for(Time now : {4100u,4200u,4300u}) t.tick(now,gatedPermission);
    check(writePermissionCalls>=3,"closed gate checked on every due attempt");
    eq(mock::w.writesCalled,0u,"closed write gate prevents OS submission while phase is live");
    eq(t.snapshot().phase,p::Phase::Live,"write denial alone does not retire input");
    eq(t.snapshot().initIndex,std::size_t{0},"write denial cannot advance initialization");
    check(!t.snapshot().writePending && t.snapshot().readPending,"closed gate retains reader without prepared write");
    auto& read=mock::pending(false);mock::complete(read,true,read.size,ERROR_SUCCESS,frame(0x22));
    t.tick(4301,gatedPermission);check(t.popEvent().has_value(),"read publication continues with write gate closed");
    writePermissionOpen=true;t.tick(4302,gatedPermission);eq(mock::w.writesCalled,1u,"reopening gate submits first page once");
    writePermissionOpen=false;auto& write=mock::pending(true);mock::complete(write,true);t.tick(4303,gatedPermission);
    eq(t.snapshot().initIndex,std::size_t{1},"pending write terminal collected after gate closes");
    t.tick(5304,gatedPermission);eq(mock::w.writesCalled,1u,"next page blocked while read remains live");
    writePermissionOpen=true;t.tick(5305,gatedPermission);eq(mock::w.writesCalled,2u,"reopening resumes next page");drain(t);
}
static void writePermissionWithdraw() {
    test::section="write-permission-withdraw";
    oneDevice();p::WindowsTransport t;boot(t);writePermissionCalls=0;
    t.tick(4100,closesBeforeSubmit);eq(mock::w.writesCalled,0u,"permission closing after preparation prevents OS write");
    eq(writePermissionCalls,2u,"permission checked before preparation and before WriteFile");
    check(!t.snapshot().writePending && t.snapshot().readPending,"withdrawn unsubmitted ticket does not disturb reader");
    eq(t.snapshot().initIndex,std::size_t{0},"withdrawal retains same initialization page");
    eq(t.snapshot().phase,p::Phase::Live,"withdrawal does not retire session");
    writePermissionCalls=0;t.tick(4101,closesBeforeSubmit);eq(mock::w.writesCalled,0u,"repeated withdrawal remains unsubmitted");
    writePermissionOpen=true;t.tick(4102,gatedPermission);eq(mock::w.writesCalled,1u,"fresh permission submits prepared page after withdrawals");
    auto& write=mock::pending(true);mock::complete(write,true);t.tick(4103,gatedPermission);
    eq(t.snapshot().initIndex,std::size_t{1},"only actual terminal success advances page");drain(t);
}
static void boundPathLifetime() {
    test::section="bound-path";
    oneDevice();mock::w.devices[0].path=L"owned-vendor-endpoint";p::WindowsTransport t;
    check(t.devicePath().empty(),"detached path empty");boot(t);eq(t.devicePath(),std::wstring(L"owned-vendor-endpoint"),"path comes from validated owner endpoint");
    t.stop(102);eq(t.devicePath(),std::wstring(L"owned-vendor-endpoint"),"retiring in-flight handle retains associated path");
    auto& read=mock::pending(false);mock::complete(read,false);tick(t,103);
    check(t.devicePath().empty() && !t.hasDevice(),"quiescent close clears associated path");mock::assertClean();
}


static unsigned bindPermissionCalls=0, denyBindAt=0;
static bool bindPathPermission(const std::wstring& path) {
    eq(path,std::wstring(L"mock-vendor"),"permission checks actual selected vendor endpoint");
    return ++bindPermissionCalls!=denyBindAt;
}
static void bindingPermission() {
    test::section="binding-permission";
    for(unsigned denied:{1u,2u}) {
        oneDevice();p::WindowsTransport t;bindPermissionCalls=0;denyBindAt=denied;
        t.request(p::Model::X2,initialContext,reportLayout,100);t.tick(100,nullptr,bindPathPermission);
        eq(mock::w.readsCalled,0u,"denied binding never starts first read");eq(mock::w.writesCalled,0u,"denied binding never initializes");
        eq(mock::w.dataOpens,denied==1?0u:1u,"candidate permission precedes open; second permission follows owner validation");
        check(!t.hasDevice() && !t.popEvent(),"denied binding owns no device and cannot publish");
        eq(mock::w.createEvents,0u,"denied binding rejected before event allocation");
        eq(t.lastError(),DWORD{ERROR_OPERATION_ABORTED},"denial records cancellation without fake success");mock::assertClean();
        denyBindAt=0;bindPermissionCalls=0;t.tick(2099,nullptr,bindPathPermission);
        eq(mock::w.readsCalled,0u,"failed binding retry remains rate limited");
        t.tick(2100,nullptr,bindPathPermission);check(t.hasDevice(),"later stable binding can recover");
        eq(bindPermissionCalls,2u,"stable binding checks before and after owner open");eq(mock::w.readsCalled,1u,"stable binding starts one reader");drain(t);
    }
}

// Capture the public completion observation the exit owner can put in its receipt.
// The application supplies its existing owner lock; the transport adds no lock.
struct ExitReceipt { bool resourcesCompleted; p::Phase phase; DWORD error; };
static ExitReceipt captureExit(const p::WindowsTransport& t) noexcept {
    return {t.stopped(),t.snapshot().phase,t.lastError()};
}
static_assert(noexcept(std::declval<const p::WindowsTransport&>().stopped()),"completion observation is noexcept");
static_assert(noexcept(std::declval<p::WindowsTransport&>().pollRetirement(Time{})),"retirement poll is noexcept");
static std::vector<unsigned> portLedger() {
    const auto& w=mock::w;
    return {w.guidCalls,w.enumerations,w.interfaces,w.probes,w.dataOpens,w.destroys,
        w.readsCalled,w.writesCalled,w.waits,w.results,w.resets,w.cancellations,w.closeCalls,
        w.createEvents,w.prepAllocated,w.prepFreed,w.attributeCalls,w.capsCalls,w.errorsRead};
}
static void completionIs(const p::WindowsTransport& t,bool expected) {
    const auto ports=portLedger();
    for(unsigned i=0;i<3;++i)eq(captureExit(t).resourcesCompleted,expected,"typed exit receipt reports actual resource completion");
    eq(portLedger(),ports,"stopped observation invokes no API port");
}
static void retirementPoll(p::WindowsTransport& t,Time now) {
    const auto reads=mock::w.readsCalled,writes=mock::w.writesCalled,opens=mock::w.dataOpens;
    const auto enumerations=mock::w.enumerations,resets=mock::w.resets,events=mock::w.createEvents;
    const auto waits=mock::w.waits,results=mock::w.results,closes=mock::w.closeCalls,cancels=mock::w.cancellations;
    const auto attempts=t.bindAttempts();mock::retained();t.pollRetirement(now);mock::retained();
    eq(mock::w.readsCalled,reads,"retirement poll submits no read");eq(mock::w.writesCalled,writes,"retirement poll submits no write");
    eq(mock::w.dataOpens,opens,"retirement poll opens no device");eq(mock::w.enumerations,enumerations,"retirement poll never enumerates");
    eq(mock::w.resets,resets,"retirement poll resets no event");eq(mock::w.createEvents,events,"retirement poll creates no event");
    eq(t.bindAttempts(),attempts,"retirement poll cannot attempt rebind");
    check(mock::w.waits-waits<=2 && mock::w.results-results<=2,"each retirement poll observes at most two operations");
    check(mock::w.closeCalls-closes<=3 && mock::w.cancellations-cancels<=2,"each retirement poll makes bounded close/cancel attempts");
}
static void staysStopped(p::WindowsTransport& t,Time now) {
    completionIs(t,true);const auto ports=portLedger();
    for(unsigned i=0;i<3;++i){t.stop(now+i);retirementPoll(t,now+i);tick(t,now+i);completionIs(t,true);}
    eq(portLedger(),ports,"repeated stop/poll/late owner ticks do not reopen or touch closed resources");
    check(!t.popEvent(),"stopped source cannot publish old input");mock::assertClean();
}
static void completionObservation() {
    test::section="completion-observation";oneDevice();p::WindowsTransport t;completionIs(t,true);
    t.request(p::Model::X2,initialContext,reportLayout,100);
    retirementPoll(t,10000);completionIs(t,true);noIo(t);
    mock::Plan immediate;immediate.mode=mock::Mode::Immediate;immediate.frame=frame(0x22);mock::w.reads.push_back(immediate);
    tick(t,10000);eq(t.snapshot().phase,p::Phase::Live,"normal tick still owns binding");completionIs(t,false);
    const auto ports=portLedger();retirementPoll(t,100000);eq(portLedger(),ports,"retirement poll leaves a live session untouched");
    check(t.popEvent().has_value(),"live observation does not revoke queued input");
    t.stop(100001);completionIs(t,true);staysStopped(t,100002);
}
static void completionPending(unsigned mask) {
    test::section="completion-pending";
    for(bool cancelSucceeds:{true,false}) {
        oneDevice();mock::w.cancelSucceeds=cancelSucceeds;
        if(mask==2){mock::Plan immediate;immediate.mode=mock::Mode::Immediate;immediate.frame=frame(0x20);
            mock::w.reads.push_back(immediate);mock::w.reads.push_back(immediate);}
        p::WindowsTransport t;boot(t);if(mask&2)tick(t,4100);
        const Time now=(mask&2)?4101:101;completionIs(t,false);t.stop(now);completionIs(t,false);
        eq(t.snapshot().readPending,(mask&1)!=0,"read remains owned when pending");
        eq(t.snapshot().writePending,(mask&2)!=0,"write remains owned when pending");
        eq(mock::w.closeCalls,mock::w.probes,"stop cannot close any resource with pending IO");
        const auto cancels=mock::w.cancellations;const auto generation=t.snapshot().generation;
        retirementPoll(t,now+p::Session::retainedNoticeMs);completionIs(t,false);
        check(t.snapshot().retainedNotice,"elapsed cancellation budget retains the actual pending owner");
        retirementPoll(t,now+1000000);t.stop(now+1000001);completionIs(t,false);
        eq(mock::w.cancellations,cancels,"repeat stop/poll does not renew cancellation budget");
        eq(t.snapshot().generation,generation,"repeat stop does not create a new retirement generation");
        if(mask&1){auto& r=mock::pending(false);mock::complete(r,true,r.size,ERROR_SUCCESS,frame(0x22,true,r.size));
            retirementPoll(t,now+1000002);check(!t.popEvent(),"late successful input cannot publish during exit");
            if(mask&2){completionIs(t,false);eq(mock::w.closeDevices,0u,"partial terminal result cannot free write owner");}}
        if(mask&2){auto& w=mock::pending(true);mock::complete(w,false,0,ERROR_OPERATION_ABORTED,{},false);
            retirementPoll(t,now+1000003);}
        completionIs(t,true);staysStopped(t,now+1000004);
    }
}
static void completionRead(){completionPending(1);}
static void completionWrite(){completionPending(2);}
static void completionBoth(){completionPending(3);}
static void completionClose(unsigned mask) {
    test::section="completion-close";oneDevice();mock::Plan immediate;immediate.mode=mock::Mode::Immediate;
    immediate.frame=frame(0x20);mock::w.reads.push_back(immediate);p::WindowsTransport t;boot(t);
    std::vector<mock::Object*> owned;unsigned eventIndex=0;
    for(auto& o:mock::w.objects)if(o->kind==mock::Kind::Device||o->kind==mock::Kind::Event){
        const auto bit=o->kind==mock::Kind::Device?1u:(2u<<eventIndex++);
        if(mask&bit){o->closeFailures=2;o->closeError=ERROR_SHARING_VIOLATION;}
        owned.push_back(o.get());
    }
    t.stop(101);eq(t.snapshot().phase,p::Phase::Detached,"quiescent session detaches even when a close fails");
    completionIs(t,false);eq(t.lastError(),DWORD{ERROR_SHARING_VIOLATION},"failed close records the port's error");
    eq(t.hasDevice(),(mask&1)!=0,"failed device close retains exact owner slot");
    check(!t.devicePath().empty(),"incomplete resource release preserves bound identity");
    t.stop(102);completionIs(t,false);retirementPoll(t,103);completionIs(t,true);
    for(const auto* o:owned){eq(o->closeAttempts,o->closeError==ERROR_SHARING_VIOLATION?3u:1u,"retry closes the same failed field without double-closing successful fields");
        check(o->closed,"successful retry actually closes memory resource");}
    eq(t.devicePath(),std::wstring{},"complete release clears bound identity");staysStopped(t,104);
}
static void completionDeviceClose(){completionClose(1);}
static void completionReadEventClose(){completionClose(2);}
static void completionWriteEventClose(){completionClose(4);}
static void completionAllClose(){completionClose(7);}
static void completionProbeClose() {
    test::section="completion-probe";oneDevice();mock::w.failProbeClose=true;p::WindowsTransport t;
    t.request(p::Model::X2,initialContext,reportLayout,100);tick(t,100);
    completionIs(t,false);check(t.hasDevice(),"failed probe close transfers exact handle into retained owner slot");
    eq(mock::w.dataOpens,0u,"failed probe close cannot be overwritten by an IO owner open");
    eq(mock::w.createEvents,0u,"failed probe close cannot allocate events");eq(mock::w.readsCalled,0u,"failed probe close submits no read");
    retirementPoll(t,10000);completionIs(t,false);mock::w.failProbeClose=false;
    retirementPoll(t,10001);completionIs(t,true);eq(mock::w.probes,1u,"retry releases original probe without enumeration");
    t.stop(10002);staysStopped(t,10003);
}
static void completionBlocksRebind() {
    test::section="completion-rebind";oneDevice();mock::Plan immediate;immediate.mode=mock::Mode::Immediate;
    mock::w.reads.push_back(immediate);p::WindowsTransport t;boot(t);mock::Object* retainedEvent=nullptr;
    for(auto& o:mock::w.objects)if(o->kind==mock::Kind::Event){retainedEvent=o.get();break;}
    check(retainedEvent!=nullptr,"test owns an allocated event port");retainedEvent->failClose=true;
    const auto opens=mock::w.dataOpens,enumerations=mock::w.enumerations;const auto attempts=t.bindAttempts();
    t.request(p::Model::X2,{initialContext.power+1,initialContext.topology},reportLayout,101);
    tick(t,2101);completionIs(t,false);check(!t.hasDevice(),"device can close while an event is retained");
    tick(t,10000);eq(t.bindAttempts(),attempts,"close failure blocks binding even after retry deadline");
    eq(mock::w.enumerations,enumerations,"retained event prevents new enumeration");eq(mock::w.dataOpens,opens,"retained event cannot be overwritten by a new binding");
    retirementPoll(t,10001);completionIs(t,false);retainedEvent->failClose=false;
    retirementPoll(t,10002);completionIs(t,true);eq(mock::w.dataOpens,opens,"successful close poll still never rebinds");
    tick(t,10003);check(t.hasDevice(),"existing requested-session policy may bind only from a later normal tick");
    eq(mock::w.dataOpens,opens+1,"normal recovery binds once after complete release");drain(t,10004);staysStopped(t,10006);
}

static void completionWriteTimeout() {
    test::section="completion-timeout";oneDevice();p::WindowsTransport t;boot(t);tick(t,4100);tick(t,4350);
    eq(t.snapshot().reason,p::StopReason::WriteTimeout,"actual write watchdog starts retirement at its existing deadline");completionIs(t,false);
    retirementPoll(t,1000000);completionIs(t,false);check(t.snapshot().retainedNotice,"timeout is not terminal completion");
    for(auto& io:mock::w.ios)if(!io->observed&&!io->terminal)mock::complete(*io,false);
    retirementPoll(t,1000001);completionIs(t,true);eq(mock::w.dataOpens,1u,"timeout drain cannot auto-bind a still requested model");
    t.stop(1000002);staysStopped(t,1000003);
}
static void completionPendingCloseRetry() {
    test::section="completion-terminal-close";oneDevice();p::WindowsTransport t;boot(t);tick(t,4100);t.stop(4101);
    retirementPoll(t,1000000);completionIs(t,false);
    for(auto& o:mock::w.objects)if(o->kind==mock::Kind::Device||o->kind==mock::Kind::Event)o->closeFailures=1;
    for(auto& io:mock::w.ios)if(!io->observed&&!io->terminal)mock::complete(*io,false);
    retirementPoll(t,1000001);eq(t.snapshot().phase,p::Phase::Detached,"both terminal results finish session retirement");
    eq(mock::outstanding(),0u,"both terminal results were actually observed");completionIs(t,false);
    eq(mock::w.failedCloses,3u,"each owned field records its terminal-stage close failure");
    retirementPoll(t,1000002);completionIs(t,true);staysStopped(t,1000003);
}

static void completionLivePoll() {
    test::section="completion-live-poll";oneDevice();p::WindowsTransport t;boot(t);tick(t,4100);
    const auto ports=portLedger();retirementPoll(t,4350);eq(portLedger(),ports,"retirement poll cannot service or time out a live session");
    eq(t.snapshot().phase,p::Phase::Live,"only the normal input tick advances a live session watchdog");completionIs(t,false);
    t.stop(4351);for(auto& io:mock::w.ios)if(!io->observed&&!io->terminal)mock::complete(*io,false);
    retirementPoll(t,4352);completionIs(t,true);staysStopped(t,4353);
}

static const Case cases[]={
    {"completion-live-poll",completionLivePoll},
    {"completion-write-timeout",completionWriteTimeout},{"completion-pending-close-retry",completionPendingCloseRetry},
    {"completion-observation",completionObservation},
    {"completion-pending-read",completionRead},{"completion-pending-write",completionWrite},{"completion-pending-both",completionBoth},
    {"completion-close-device",completionDeviceClose},{"completion-close-read-event",completionReadEventClose},
    {"completion-close-write-event",completionWriteEventClose},{"completion-close-all",completionAllClose},
    {"completion-close-probe",completionProbeClose},{"completion-blocks-rebind",completionBlocksRebind},
    {"binding-permission",bindingPermission},
    {"write-permission",writePermissionGate},{"write-permission-withdraw",writePermissionWithdraw},{"bound-path",boundPathLifetime},
    {"status-init-boundary-x2",statusInitX2},{"status-init-boundary-mini",statusInitMini},{"status-write-watchdog-order",statusWriteWatchdogOrdering},
    {"valid-layout-change",validLayoutChange},{"stop-immediate-release",stopImmediateRelease},{"stop-pending-retention",stopPendingRetention},{"disabled",disabled},{"unknown-layout",unknownLayout},{"unverified-revokes",unverifiedRevokes},
    {"collection-filtering",filtering},{"reopened-identity",reopenedIdentity},{"raw-framing",rawFraming},{"multi-collection",multiCollection},{"initialization",initialization},
    {"retention",retention},{"context-change",contextChange},{"wait-failed",waitFailed},
    {"incomplete-result",incompleteResult},{"incomplete-despite-internal",incompleteDespiteInternal},
    {"submission-failures",failures},{"immediate",immediate},{"short-writes",shortWrites},
    {"timeout",timeout},{"rebind",rebind},{"bind-failures",bindFailures},{"reader-events",readerEvents},{"terminal-read-error",terminalReadError},{"terminal-write-error",terminalWriteError},{"immediate-short-read",immediateShortRead}
};
int main(int argc,char** argv){
    if(argc!=2){std::fprintf(stderr,"usage: onex_windows.exe <case>\n");return 2;}
    for(const auto& c:cases)if(c.name==std::string(argv[1])){test::scenario=c.name;c.run();
        std::printf("{\"status\":\"pass\",\"scenario\":\"%s\",\"checks\":%llu,\"waitCallsLastWorld\":%u,\"realDeviceApiCalls\":0}\n",c.name,static_cast<unsigned long long>(test::checks),mock::w.waits);return 0;}
    std::fprintf(stderr,"unknown case\n");return 2;
}
