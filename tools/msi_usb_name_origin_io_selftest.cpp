#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "msi_usb_name_origin_io.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <utility>
namespace usb = ymcc::msi::usbName;
using Json = usb::Json;
using Bytes = std::vector<std::uint8_t>;
constexpr DWORD anyBudget = std::numeric_limits<DWORD>::max();
const Bytes payload{0xa1, 0xb2, 0xc3, 0xd4, 0x09, 0x06, 0x3d, 0xd8};
struct Counts {
    unsigned open=0,event=0,close=0,issue=0,wait=0,cancel=0,result=0,error=0,tick=0;
    Json json()const{return {{"open",open},{"event",event},{"close",close},{"issue",issue},
        {"wait",wait},{"cancel",cancel},{"result",result},{"error",error},{"tick",tick}};}
};
struct WaitStep {DWORD state=WAIT_OBJECT_0; std::uint64_t advance=0; DWORD budget=anyBudget;};
struct ResultStep {bool ok=true; DWORD error=0,count=8; std::uint64_t advance=0;};
struct Storage {
    HANDLE file=nullptr,event=nullptr;
    bool fileOpen=true,eventOpen=false,issued=false,terminal=true;
    std::uint8_t* buffer=nullptr; DWORD size=0; OVERLAPPED* overlapped=nullptr;
};
// All handles are local tags. No default IO construction or system-hook factory.
struct Fake {
    Counts calls; Json trace=Json::array(),violations=Json::array();
    std::vector<Storage> storage;
    std::uint64_t now=100,openAdvance=0,eventAdvance=0,issueAdvance=0;
    bool openDenied=false,eventDenied=false,immediate=true,cancelAccepted=true;
    DWORD lastError=0,issueError=ERROR_IO_PENDING,issueCount=8,cancelError=ERROR_NOT_FOUND;
    std::vector<WaitStep> waits; std::vector<ResultStep> results;
    WaitStep defaultWait; ResultStep defaultResult;
    std::size_t nextWait=0,nextResult=0;
    std::wstring expectedHub; Bytes expectedInput; DWORD expectedCode=0;
    unsigned probes=0,bufferChecks=0,overlappedChecks=0;
    std::string throwAt;
    bool throwBadAlloc=false;
    unsigned throws=0;
    void fault(const char* hook){
        if(throwAt!=hook)return;
        if(std::string(hook)=="tick"&&calls.issue==0)return;
        throwAt.clear();++throws;log("injected-exception",{{"at",hook},{"badAlloc",throwBadAlloc}});
        if(throwBadAlloc)throw std::bad_alloc();
        throw std::runtime_error("injected-fake-hook-exception");
    }
    void require(bool ok,const char* message){if(!ok)violations.push_back(message);}
    void log(const char* hook,Json fields=Json::object()){
        fields["hook"]=hook;fields["tick"]=now;trace.push_back(std::move(fields));
    }
    Storage& file(HANDLE h){for(auto&s:storage)if(s.file==h)return s;throw std::runtime_error("unknown fake file");}
    Storage& event(HANDLE h){for(auto&s:storage)if(s.event==h)return s;throw std::runtime_error("unknown fake event");}
    void identity(Storage&s,OVERLAPPED&o){
        ++overlappedChecks;
        require(s.fileOpen&&s.eventOpen,"operation used closed handles");
        require(s.overlapped==&o,"OVERLAPPED address changed");
        require(o.hEvent==s.event,"OVERLAPPED event changed");
    }
    void writePayload(Storage&s){if(s.buffer&&s.size>=payload.size())std::copy(payload.begin(),payload.end(),s.buffer);}
    usb::IoHooks hooks(){
        usb::IoHooks h;
        h.open=[this](const wchar_t*p)->HANDLE{
            ++calls.open;require(p&&std::wstring(p)==expectedHub,"wrong hub path");now+=openAdvance;
            log("open",{{"pathChars",expectedHub.size()},{"denied",openDenied}});
            if(openDenied){lastError=ERROR_ACCESS_DENIED;return INVALID_HANDLE_VALUE;}
            Storage s;s.file=reinterpret_cast<HANDLE>(std::uintptr_t{0x1000}+calls.open*0x10u);
            storage.push_back(s);return s.file;
        };
        h.event=[this]()->HANDLE{
            ++calls.event;now+=eventAdvance;log("event",{{"denied",eventDenied}});
            if(eventDenied){lastError=ERROR_NOT_ENOUGH_MEMORY;return nullptr;}
            auto&s=storage.back();s.event=reinterpret_cast<HANDLE>(std::uintptr_t{0x2000}+calls.event*0x10u);
            s.eventOpen=true;return s.event;
        };
        h.close=[this](HANDLE handle){
            ++calls.close;
            for(auto&s:storage){
                if(s.event==handle){require(s.eventOpen,"event closed twice");require(s.terminal,"event closed without terminal proof");
                    s.eventOpen=false;log("close",{{"kind","event"},{"terminal",s.terminal}});return;}
                if(s.file==handle){require(s.fileOpen,"file closed twice");require(!s.eventOpen,"file closed before event");
                    require(s.terminal,"file closed without terminal proof");s.fileOpen=false;
                    log("close",{{"kind","file"},{"terminal",s.terminal}});return;}
            }
            require(false,"unknown handle closed");
        };
        h.issue=[this](HANDLE handle,DWORD code,void*buffer,DWORD size,DWORD&count,OVERLAPPED&o){
            ++calls.issue;auto&s=file(handle);
            require(s.fileOpen&&s.eventOpen,"issue used closed handles");
            require(code==expectedCode&&size==expectedInput.size(),"wrong issue request");
            require(buffer&&size,"missing issue buffer");
            require(size==expectedInput.size()&&std::memcmp(buffer,expectedInput.data(),size)==0,"input bytes changed");
            require(o.hEvent==s.event&&o.Internal==0&&o.InternalHigh==0&&o.Offset==0&&o.OffsetHigh==0,"OVERLAPPED initialization differs");
            s.buffer=static_cast<std::uint8_t*>(buffer);s.size=size;s.overlapped=&o;s.issued=true;
            s.terminal=immediate||issueError!=ERROR_IO_PENDING;count=issueCount;writePayload(s);
            now+=issueAdvance;lastError=issueError;
            log("issue",{{"ioctl",code},{"bufferBytes",size},{"immediate",immediate},{"count",count},{"error",immediate?0:issueError}});
            fault("issue");return immediate;
        };
        h.wait=[this](HANDLE handle,DWORD budget){
            ++calls.wait;auto&s=event(handle);require(s.eventOpen&&s.fileOpen,"wait used closed handles");
            const auto step=nextWait<waits.size()?waits[nextWait++]:defaultWait;
            require(step.budget==anyBudget||step.budget==budget,"wrong wait budget");now+=step.advance;
            log("wait",{{"budgetMs",budget},{"state",step.state}});if(calls.cancel)fault("wait-after-cancel");fault("wait");return step.state;
        };
        h.cancel=[this](HANDLE handle,OVERLAPPED&o){
            ++calls.cancel;identity(file(handle),o);lastError=cancelError;
            log("cancel",{{"accepted",cancelAccepted},{"error",cancelAccepted?0:cancelError}});
            fault("cancel");return cancelAccepted; // Accepted cancellation does not prove completion.
        };
        h.result=[this](HANDLE handle,OVERLAPPED&o,DWORD&count){
            ++calls.result;auto&s=file(handle);identity(s,o);
            const auto step=nextResult<results.size()?results[nextResult++]:defaultResult;
            writePayload(s);count=step.count;now+=step.advance;lastError=step.error;
            s.terminal=step.ok||(step.error!=ERROR_IO_INCOMPLETE&&step.error!=ERROR_IO_PENDING&&step.error!=ERROR_INVALID_HANDLE);
            log("result",{{"ok",step.ok},{"error",step.ok?0:step.error},{"count",count}});if(calls.cancel)fault("result-after-cancel");fault("result");return step.ok;
        };
        h.error=[this](){++calls.error;log("error",{{"value",lastError}});if(calls.cancel)fault("error-after-cancel");fault("error");return lastError;};
        h.tick=[this](){++calls.tick;log("tick");fault("tick");return now;};
        return h;
    }
    // ASan checks the actual retained allocations, not only fake handle counters.
    void probe(){for(auto&s:storage)if(s.issued&&!s.terminal){
        ++probes;require(s.fileOpen&&s.eventOpen,"unknown completion lost handles");
        require(s.buffer&&s.size&&s.overlapped,"unknown completion lost storage addresses");
        if(!s.fileOpen||!s.eventOpen||!s.buffer||!s.size||!s.overlapped)continue;
        ++bufferChecks;const auto first=s.buffer[0],last=s.buffer[s.size-1];
        s.buffer[0]^=0x5a;s.buffer[s.size-1]^=0xa5;
        require(s.buffer[0]==static_cast<std::uint8_t>(first^0x5a)&&s.buffer[s.size-1]==static_cast<std::uint8_t>(last^0xa5),"retained buffer inaccessible");
        s.buffer[0]=first;s.buffer[s.size-1]=last;
        ++overlappedChecks;require(s.overlapped->hEvent==s.event,"retained event identity changed");
        const auto offset=s.overlapped->Offset;s.overlapped->Offset=0x1937;
        require(s.overlapped->Offset==0x1937,"retained OVERLAPPED inaccessible");s.overlapped->Offset=offset;
        log("storage-probe",{{"bufferBytes",s.size},{"eventOpen",s.eventOpen},{"fileOpen",s.fileOpen}});
    }}
};
Bytes descriptor(unsigned type=1,unsigned index=0,unsigned language=0,unsigned length=18,
                 ULONG port=1,unsigned available=18,UCHAR direction=0x80,UCHAR request=6){
    constexpr auto offset=offsetof(USB_DESCRIPTOR_REQUEST,Data);Bytes bytes(offset+available,0);
    auto*r=reinterpret_cast<USB_DESCRIPTOR_REQUEST*>(bytes.data());r->ConnectionIndex=port;
    r->SetupPacket.bmRequest=direction;r->SetupPacket.bRequest=request;
    r->SetupPacket.wValue=static_cast<USHORT>((type<<8)|index);
    r->SetupPacket.wIndex=static_cast<USHORT>(language);r->SetupPacket.wLength=static_cast<USHORT>(length);
    return bytes;
}
template<class T>Bytes structure(const T& value){Bytes bytes(sizeof(T));std::memcpy(bytes.data(),&value,sizeof(T));return bytes;}
Bytes node(USB_HUB_NODE type=UsbHub){USB_NODE_INFORMATION r{};r.NodeType=type;return structure(r);}
Bytes driverKey(ULONG port=1){USB_NODE_CONNECTION_DRIVERKEY_NAME r{};r.ConnectionIndex=port;return structure(r);}
Bytes connection(ULONG port=1){Bytes bytes(sizeof(USB_NODE_CONNECTION_INFORMATION_EX),0);reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(bytes.data())->ConnectionIndex=port;return bytes;}
struct Fixture {
    Fake fake;Json checks=Json::array(),replies=Json::array();
    unsigned constructors=0,destructors=0,queries=0,pendingCalls=0,reaps=0;
    using Owner=std::unique_ptr<usb::ReadOnlyUsbIo,std::function<void(usb::ReadOnlyUsbIo*)>>;
    void require(bool ok,const char*message){if(!ok)checks.push_back(message);}
    Owner make(usb::IoHooks hooks){++constructors;
        return Owner(new usb::ReadOnlyUsbIo(std::move(hooks)),[this](auto*io){++destructors;delete io;});}
    Owner make(){return make(fake.hooks());}
    usb::IoReply query(Owner&io,Bytes input=descriptor(),DWORD code=IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
                       std::uint64_t deadline=500,const std::wstring&hub=L"fake-hub"){
        ++queries;fake.expectedHub=hub;fake.expectedCode=code;fake.expectedInput=input;
        auto r=io->query(hub,code,std::move(input),deadline);
        const auto prefix=std::min<std::size_t>(16,r.bytes.size());
        replies.push_back({{"ok",r.ok},{"admittedBytes",r.bytes.size()},
            {"admittedBytesPrefix",Bytes(r.bytes.begin(),r.bytes.begin()+prefix)},{"receipt",r.receipt}});
        require(r.receipt.at("writesHardware")==false&&r.receipt.at("handleDesiredAccess")==0&&
                r.receipt.at("overlapped")==true,"read-only receipt differs");
        require(r.receipt.at("attempted")==r.receipt.contains("startError"),"attempted/startError receipt differs");
        return r;
    }
    std::size_t pending(Owner&io){++pendingCalls;return io->pending();}
    void reap(Owner&io){++reaps;io->reap();}
    void reply(const usb::IoReply&r,bool ok,const char*state,DWORD error){
        require(r.ok==ok,"reply ok differs");require(r.receipt.at("state")==state,"receipt state differs");
        require(r.receipt.at("error")==error,"receipt error differs");
        if(!ok)require(r.bytes.empty(),"failed/unproven read admitted bytes");
    }
    void counts(unsigned o,unsigned e,unsigned c,unsigned i,unsigned w,unsigned x,unsigned r,unsigned err){
        const auto&a=fake.calls;require(a.open==o&&a.event==e&&a.close==c&&a.issue==i&&a.wait==w&&
            a.cancel==x&&a.result==r&&a.error==err,"hook counts differ (tick recorded independently)");
    }
};
struct Suite {
    Json cases=Json::array();std::size_t failures=0;
    void run(const std::string&name,const std::string&category,const std::function<void(Fixture&)>&test){
        Fixture f;try{test(f);}catch(const std::exception&e){f.checks.push_back(std::string("exception: ")+e.what());}
        f.require(f.fake.nextWait==f.fake.waits.size(),"unused scripted waits");
        f.require(f.fake.nextResult==f.fake.results.size(),"unused scripted results");
        const bool pass=f.checks.empty()&&f.fake.violations.empty();
        if(!pass){++failures;std::cerr<<"FAIL "<<name<<'\n';}
        cases.push_back({{"case",name},{"category",category},{"passed",pass},{"failedChecks",f.checks},
            {"hookViolations",f.fake.violations},{"hookCalls",f.fake.calls.json()},
            {"publicCalls",{{"constructor",f.constructors},{"destructor",f.destructors},{"query",f.queries},
                {"pending",f.pendingCalls},{"reap",f.reaps}}},
            {"injectedExceptions",f.fake.throws},{"storageProbes",f.fake.probes},{"bufferIdentityChecks",f.fake.bufferChecks},
            {"overlappedIdentityChecks",f.fake.overlappedChecks},{"replies",f.replies},{"trace",f.fake.trace}});
    }
};
void pendingStart(Fixture&f){f.fake.immediate=false;f.fake.issueError=ERROR_IO_PENDING;}
void unknownStart(Fixture&f,DWORD error=ERROR_IO_INCOMPLETE,DWORD initial=WAIT_TIMEOUT){
    pendingStart(f);f.fake.waits={{initial,0,200},{WAIT_TIMEOUT,0,50}};
    if(initial==WAIT_OBJECT_0)f.fake.results.push_back({false,error,8,0});
    f.fake.results.push_back({false,error,8,0});
}
void reject(Suite&s,const std::string&name,Bytes bytes,DWORD code,
            const char*state="request-not-allowlisted",DWORD error=ERROR_NOT_SUPPORTED,const std::wstring&hub=L"fake-hub"){
    s.run(name,"allowlist-and-bounds",[=](Fixture&f){auto io=f.make();const auto r=f.query(io,bytes,code,500,hub);
        f.reply(r,false,state,error);f.require(f.pending(io)==0,"rejected request became pending");f.counts(0,0,0,0,0,0,0,0);});
}
int main(int argc,char**argv){
    if(argc!=2){std::cerr<<"Usage: msi_usb_name_origin_io_selftest.exe report.json\n";return 2;}
    Suite suite;
    suite.run("immediate-success","completion",[](Fixture&f){auto io=f.make();const auto r=f.query(io);
        f.reply(r,true,"completed",0);f.require(r.bytes==payload&&f.pending(io)==0,"immediate payload/pending differs");f.counts(1,1,2,1,0,0,0,0);});
    suite.run("immediate-success-zero-bytes","completion",[](Fixture&f){f.fake.issueCount=0;auto io=f.make();const auto r=f.query(io);
        f.reply(r,true,"completed",0);f.require(r.bytes.empty(),"zero count admitted bytes");f.counts(1,1,2,1,0,0,0,0);});
    for(DWORD error:std::initializer_list<DWORD>{ERROR_ACCESS_DENIED,ERROR_INVALID_FUNCTION,ERROR_OPERATION_ABORTED}){
        suite.run("immediate-terminal-error-"+std::to_string(error),"completion",[=](Fixture&f){
            f.fake.immediate=false;f.fake.issueError=error;auto io=f.make();const auto r=f.query(io);
            f.reply(r,false,"start-failed",error);f.require(f.pending(io)==0,"terminal error retained storage");f.counts(1,1,2,1,0,0,0,1);});
    }
    for(bool immediate:{true,false}){
        suite.run(immediate?"immediate-oversized-count":"pending-oversized-count","completion",[=](Fixture&f){
            f.fake.immediate=immediate;f.fake.issueCount=1000;f.fake.defaultResult.count=1000;
            auto io=f.make();const auto r=f.query(io);f.reply(r,false,"completed-without-admitted-data",ERROR_INVALID_DATA);
            f.counts(1,1,2,1,immediate?0u:1u,0,immediate?0u:1u,immediate?0u:1u);});
    }
    suite.run("pending-success","completion",[](Fixture&f){pendingStart(f);f.fake.waits={{WAIT_OBJECT_0,1,200}};
        auto io=f.make();const auto r=f.query(io);f.reply(r,true,"completed",0);
        f.require(r.bytes==payload&&f.pending(io)==0,"pending payload/pending differs");f.counts(1,1,2,1,1,0,1,1);});
    suite.run("pending-success-zero-bytes","completion",[](Fixture&f){pendingStart(f);f.fake.defaultResult.count=0;
        auto io=f.make();const auto r=f.query(io);f.reply(r,true,"completed",0);
        f.require(r.bytes.empty(),"pending zero count admitted bytes");f.counts(1,1,2,1,1,0,1,1);});
    for(DWORD error:std::initializer_list<DWORD>{ERROR_GEN_FAILURE,ERROR_OPERATION_ABORTED,ERROR_ACCESS_DENIED}){
        suite.run("pending-terminal-error-"+std::to_string(error),"completion",[=](Fixture&f){
            pendingStart(f);f.fake.results={{false,error,8,0}};auto io=f.make();const auto r=f.query(io);
            f.reply(r,false,"completed-without-admitted-data",error);f.require(f.pending(io)==0,"terminal result retained storage");f.counts(1,1,2,1,1,0,1,2);});
    }
    for(bool accepted:{true,false})for(DWORD settle:std::initializer_list<DWORD>{WAIT_OBJECT_0,WAIT_TIMEOUT}){
        suite.run("timeout-late-success-cancel-"+std::to_string(accepted)+"-settle-"+std::to_string(settle),"cancellation-race",[=](Fixture&f){
            pendingStart(f);f.fake.cancelAccepted=accepted;f.fake.waits={{WAIT_TIMEOUT,50,50},{settle,51,50}};
            auto io=f.make();const auto r=f.query(io,descriptor(),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,150);
            f.reply(r,false,"completed-without-admitted-data",WAIT_TIMEOUT);
            f.require(r.receipt.at("cancelAttempted")==true&&r.receipt.at("cancelAccepted")==accepted&&
                r.receipt.at("endTick").get<std::uint64_t>()>150&&f.pending(io)==0,"cancel race differs");
            f.counts(1,1,2,1,2,1,1,accepted?1u:2u);});
    }
    for(bool accepted:{true,false}){
        suite.run("timeout-cancel-aborted-"+std::to_string(accepted),"cancellation-race",[=](Fixture&f){
            pendingStart(f);f.fake.cancelAccepted=accepted;f.fake.waits={{WAIT_TIMEOUT,200,200},{WAIT_TIMEOUT,0,50}};
            f.fake.results={{false,ERROR_OPERATION_ABORTED,8,0}};auto io=f.make();const auto r=f.query(io);
            f.reply(r,false,"completed-without-admitted-data",WAIT_TIMEOUT);
            f.require(f.pending(io)==0,"proved abort retained storage");f.counts(1,1,2,1,2,1,1,accepted?2u:3u);});
    }
    for(DWORD initial:std::initializer_list<DWORD>{WAIT_FAILED,WAIT_ABANDONED_0}){
        suite.run("wait-nonsignal-cancel-terminal-"+std::to_string(initial),"cancellation-race",[=](Fixture&f){
            pendingStart(f);f.fake.waits={{initial,0,200},{WAIT_OBJECT_0,0,50}};
            f.fake.results={{false,ERROR_OPERATION_ABORTED,0,0}};auto io=f.make();const auto r=f.query(io);
            f.reply(r,false,"completed-without-admitted-data",WAIT_TIMEOUT);f.counts(1,1,2,1,2,1,1,2);});
    }
    suite.run("signalled-incomplete-cancel-late-success-not-ok","cancellation-race",[](Fixture&f){
        pendingStart(f);f.fake.waits={{WAIT_OBJECT_0,0,200},{WAIT_OBJECT_0,0,50}};
        f.fake.results={{false,ERROR_IO_INCOMPLETE,8,0},{true,0,8,0}};
        auto io=f.make();const auto r=f.query(io);f.reply(r,false,"completed-without-admitted-data",WAIT_TIMEOUT);f.counts(1,1,2,1,2,1,2,2);});
    for(DWORD error:std::initializer_list<DWORD>{ERROR_IO_INCOMPLETE,ERROR_IO_PENDING,ERROR_INVALID_HANDLE})for(DWORD initial:std::initializer_list<DWORD>{WAIT_TIMEOUT,WAIT_OBJECT_0}){
        suite.run("unknown-storage-error-"+std::to_string(error)+"-wait-"+std::to_string(initial),"retention",[=](Fixture&f){
            unknownStart(f,error,initial);auto io=f.make();const auto r=f.query(io);
            f.reply(r,false,"completion-unproven-retained",ERROR_IO_INCOMPLETE);
            f.require(r.receipt.at("bufferRetained")==true&&f.pending(io)==1,"unknown completion not retained");
            f.fake.probe();f.counts(1,1,0,1,2,1,initial==WAIT_OBJECT_0?2u:1u,initial==WAIT_OBJECT_0?3u:2u);
            io.reset();f.fake.probe();f.require(f.fake.calls.close==0&&f.fake.probes==2,"destructor released unknown storage");});
    }
    for(DWORD error:std::initializer_list<DWORD>{ERROR_NOT_FOUND,ERROR_ACCESS_DENIED}){
        suite.run("cancel-failed-incomplete-retains-"+std::to_string(error),"retention",[=](Fixture&f){
            unknownStart(f);f.fake.cancelAccepted=false;f.fake.cancelError=error;
            auto io=f.make();const auto r=f.query(io);f.reply(r,false,"completion-unproven-retained",ERROR_IO_INCOMPLETE);
            f.fake.probe();f.counts(1,1,0,1,2,1,1,3);});
    }
    suite.run("retained-blocking-reap-incomplete-invalid-abort-next-query","reap",[](Fixture&f){
        unknownStart(f);f.fake.waits.insert(f.fake.waits.end(),{{WAIT_TIMEOUT,0,0},{WAIT_OBJECT_0,0,0},{WAIT_OBJECT_0,0,0},{WAIT_OBJECT_0,0,0}});
        f.fake.results.insert(f.fake.results.end(),{{false,ERROR_IO_INCOMPLETE,8,0},{false,ERROR_INVALID_HANDLE,8,0},{false,ERROR_OPERATION_ABORTED,0,0}});
        auto io=f.make();const auto original=f.query(io);f.reply(original,false,"completion-unproven-retained",ERROR_IO_INCOMPLETE);f.fake.probe();
        const auto blocked=f.query(io);f.reply(blocked,false,"prior-read-completion-unproven",ERROR_IO_INCOMPLETE);
        f.require(f.fake.calls.open==1&&f.fake.calls.issue==1&&f.pending(io)==1,"retained job allowed another open/issue");
        f.reap(io);f.require(f.pending(io)==1,"incomplete reap released storage");f.fake.probe();
        f.reap(io);f.require(f.pending(io)==1,"invalid-handle reap released storage");f.fake.probe();
        f.reap(io);f.require(f.pending(io)==0&&f.fake.calls.close==2,"proved abort not reaped");
        f.require(!original.ok&&original.bytes.empty(),"reap retroactively admitted bytes");
        f.fake.immediate=true;const auto next=f.query(io);f.reply(next,true,"completed",0);f.counts(2,2,4,2,6,1,4,5);});
    suite.run("reap-timeout-failed-pending-then-late-success","reap",[](Fixture&f){
        unknownStart(f);f.fake.waits.insert(f.fake.waits.end(),{{WAIT_TIMEOUT,0,0},{WAIT_FAILED,0,0},{WAIT_OBJECT_0,0,0},{WAIT_OBJECT_0,0,0}});
        f.fake.results.insert(f.fake.results.end(),{{false,ERROR_IO_PENDING,8,0},{true,0,8,1000}});
        auto io=f.make();const auto original=f.query(io);
        for(unsigned i=0;i<3;++i){f.reap(io);f.require(f.pending(io)==1&&f.fake.calls.close==0,"nonterminal reap released storage");f.fake.probe();}
        f.reap(io);f.require(f.pending(io)==0&&!original.ok&&original.bytes.empty(),"late reap admitted original data");f.counts(1,1,2,1,6,1,3,3);});
    suite.run("query-reaps-proved-prior-completion-before-new-issue","reap",[](Fixture&f){
        unknownStart(f);f.fake.waits.push_back({WAIT_OBJECT_0,0,0});f.fake.results.push_back({true,0,8,0});
        auto io=f.make();const auto old=f.query(io);f.fake.immediate=true;const auto next=f.query(io);
        f.reply(next,true,"completed",0);f.require(!old.ok&&old.bytes.empty()&&f.pending(io)==0,"automatic reap changed old receipt");f.counts(2,2,4,2,3,1,2,2);});
    for(std::uint64_t deadline:{100ull,99ull}){
        suite.run("deadline-expired-at-entry-"+std::to_string(deadline),"deadline",[=](Fixture&f){
            auto io=f.make();const auto r=f.query(io,descriptor(),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,deadline);
            f.reply(r,false,"capture-deadline-expired",WAIT_TIMEOUT);f.counts(0,0,0,0,0,0,0,0);});
    }
    suite.run("deadline-expired-before-issue","deadline",[](Fixture&f){f.fake.eventAdvance=50;
        auto io=f.make();const auto r=f.query(io,descriptor(),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,150);
        f.reply(r,false,"deadline-before-issue",WAIT_TIMEOUT);f.counts(1,1,2,0,0,0,0,0);});
    suite.run("pending-wait-budget-less-than-200","deadline",[](Fixture&f){pendingStart(f);f.fake.waits={{WAIT_OBJECT_0,1,7}};
        auto io=f.make();const auto r=f.query(io,descriptor(),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,107);
        f.reply(r,true,"completed",0);f.counts(1,1,2,1,1,0,1,1);});
    for(unsigned phase=0;phase<4;++phase)for(std::uint64_t advance:{50ull,51ull}){
        const std::array<const char*,4>names{{"immediate-issue","pending-issue","signalled-wait","successful-result"}};
        suite.run(std::string("deadline-crossed-inside-")+names[phase]+"-advance-"+std::to_string(advance),"deadline",[=](Fixture&f){
            if(phase!=0)pendingStart(f);if(phase<2)f.fake.issueAdvance=advance;
            if(phase==2)f.fake.waits={{WAIT_OBJECT_0,advance,50}};if(phase==3)f.fake.defaultResult.advance=advance;
            auto io=f.make();const auto r=f.query(io,descriptor(),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,150);
            // Entry/before-issue treat tick == deadline as expired. Admission must
            // enforce that same boundary for success observed after issue/result.
            f.require(!r.ok&&r.bytes.empty(),"deadline-expired success admitted data");
            f.require(!r.receipt.contains("returnedBytes"),"late bytes recorded as admitted");
            f.require(r.receipt.at("endTick").get<std::uint64_t>()>=150,"fixture did not reach deadline");
            f.require(f.fake.calls.issue==1&&f.fake.calls.close==2&&f.pending(io)==0,"proved late completion not released");
            if(advance>50)f.require(r.receipt.value("completionPastCaptureDeadline",false),"late completion receipt flag missing");});
    }
    for(const auto& hook:std::vector<std::string>{"issue","wait","result","cancel","wait-after-cancel","result-after-cancel","error","error-after-cancel","tick"})
        for(bool badAlloc:{false,true}){
            suite.run("exception-retains-and-reaps-"+hook+(badAlloc?"-bad-alloc":"-runtime"),"exception-ownership",[=](Fixture&f){
                pendingStart(f);f.fake.throwAt=hook;f.fake.throwBadAlloc=badAlloc;
                const bool cancelling=hook=="cancel"||hook=="wait-after-cancel"||hook=="result-after-cancel"||hook=="error-after-cancel";
                f.fake.defaultWait.state=cancelling?WAIT_TIMEOUT:WAIT_OBJECT_0;
                f.fake.defaultResult={false,ERROR_IO_INCOMPLETE,8,0};
                auto io=f.make();bool threw=false;
                try{(void)f.query(io);}
                catch(const std::bad_alloc&){threw=badAlloc;}
                catch(const std::runtime_error&e){threw=!badAlloc&&std::string(e.what())=="injected-fake-hook-exception";}
                f.require(threw&&f.fake.throws==1,"fake hook exception was not observed");
                f.require(f.fake.calls.issue==1&&f.fake.calls.close==0&&f.pending(io)==1,"exception released pending ownership");
                f.fake.probe();f.require(f.fake.probes==1,"exception did not retain actual buffer/OVERLAPPED");
                const auto issues=f.fake.calls.issue;
                f.fake.defaultWait.state=WAIT_TIMEOUT;
                const auto blocked=f.query(io);f.reply(blocked,false,"prior-read-completion-unproven",ERROR_IO_INCOMPLETE);
                f.require(f.fake.calls.issue==issues&&f.fake.calls.open==1,"exception allowed a second read while unresolved");
                f.fake.probe();
                f.fake.defaultWait.state=WAIT_OBJECT_0;f.fake.defaultResult={true,0,8,0};
                f.reap(io);f.require(f.pending(io)==0&&f.fake.calls.close==2,"proved completion after exception not reaped");
                io.reset();f.require(f.fake.calls.close==2,"exception/reap destructor double-closed handles");
            });
        }
    suite.run("exception-after-issue-destructor-retains-unknown-storage","exception-ownership",[](Fixture&f){
        pendingStart(f);f.fake.throwAt="issue";auto io=f.make();bool threw=false;
        try{(void)f.query(io);}catch(const std::runtime_error&){threw=true;}
        f.require(threw&&f.pending(io)==1&&f.fake.calls.close==0,"throwing issue not retained");
        f.fake.probe();io.reset();f.fake.probe();
        f.require(f.fake.probes==2&&f.fake.calls.close==0,"exception/destructor freed unknown buffer/OVERLAPPED");
    });
    suite.run("exception-after-immediate-terminal-destructor-closes-proved-storage","exception-ownership",[](Fixture&f){
        f.fake.throwAt="tick";auto io=f.make();bool threw=false;
        try{(void)f.query(io);}catch(const std::runtime_error&){threw=true;}
        f.require(threw&&f.fake.calls.issue==1,"post-terminal fake tick exception not injected");
        io.reset();f.require(f.fake.calls.close==2,"proved terminal exception storage not cleaned by destructor");
    });
    suite.run("open-access-denied","platform-boundary",[](Fixture&f){f.fake.openDenied=true;
        auto io=f.make();const auto r=f.query(io);f.reply(r,false,"metadata-handle-denied",ERROR_ACCESS_DENIED);f.counts(1,0,0,0,0,0,0,1);});
    suite.run("event-unavailable-closes-file-only","platform-boundary",[](Fixture&f){f.fake.eventDenied=true;
        auto io=f.make();const auto r=f.query(io);f.reply(r,false,"event-unavailable",ERROR_NOT_ENOUGH_MEMORY);f.counts(1,1,1,0,0,0,0,1);});
    const std::array<const char*,9>hookNames{{"open","event","close","issue","wait","cancel","result","error","tick"}};
    for(unsigned missing=0;missing<hookNames.size();++missing){
        suite.run(std::string("incomplete-hooks-missing-")+hookNames[missing],"platform-boundary",[=](Fixture&f){
            auto h=f.fake.hooks();switch(missing){case 0:h.open={};break;case 1:h.event={};break;case 2:h.close={};break;
                case 3:h.issue={};break;case 4:h.wait={};break;case 5:h.cancel={};break;case 6:h.result={};break;case 7:h.error={};break;case 8:h.tick={};break;}
            f.require(!h.complete(),"incomplete hooks reported complete");bool threw=false;
            try{auto io=f.make(std::move(h));}catch(const std::runtime_error&e){threw=std::string(e.what())=="incomplete-name-probe-platform-boundary";}
            f.require(threw&&f.destructors==0,"incomplete constructor accepted");f.counts(0,0,0,0,0,0,0,0);
            f.require(f.fake.calls.tick==0,"incomplete constructor invoked tick");});
    }
    suite.run("incomplete-hooks-all-empty","platform-boundary",[](Fixture&f){usb::IoHooks h{};bool threw=false;
        try{auto io=f.make(h);}catch(const std::runtime_error&){threw=true;}
        f.require(threw&&!h.complete()&&f.fake.calls.tick==0,"empty hooks accepted/called");f.counts(0,0,0,0,0,0,0,0);});
    reject(suite,"unsupported-reset-write",node(),IOCTL_USB_RESET_HUB);
    reject(suite,"unsupported-cycle-write",connection(),IOCTL_USB_HUB_CYCLE_PORT);
    reject(suite,"unsupported-unknown-ioctl",descriptor(),0xffffffffu);
    reject(suite,"descriptor-OUT-rejected",descriptor(1,0,0,18,1,18,0),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION);
    reject(suite,"descriptor-SET_DESCRIPTOR-rejected",descriptor(1,0,0,18,1,18,0x80,7),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION);
    for(UCHAR direction:{UCHAR{0x81},UCHAR{0xa0},UCHAR{0xc0}})
        reject(suite,"descriptor-nonstandard-IN-"+std::to_string(direction),descriptor(1,0,0,18,1,18,direction),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION);
    reject(suite,"node-MI-parent-rejected",node(UsbMIParent),IOCTL_USB_GET_NODE_INFORMATION);
    reject(suite,"node-invalid-type-rejected",node(static_cast<USB_HUB_NODE>(2)),IOCTL_USB_GET_NODE_INFORMATION);
    reject(suite,"driver-key-zero-port",driverKey(0),IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME);
    reject(suite,"connection-info-zero-port",connection(0),IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX);
    reject(suite,"descriptor-zero-port",descriptor(1,0,0,18,0),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION);
    for(unsigned type:{0u,4u,255u})reject(suite,"unsupported-descriptor-type-"+std::to_string(type),descriptor(type),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION);
    reject(suite,"descriptor-zero-wLength",descriptor(1,0,0,0),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION);
    reject(suite,"descriptor-wLength-over-16384",descriptor(1,0,0,16385,1,16385),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION);
    reject(suite,"descriptor-wLength-exceeds-available",descriptor(1,0,0,19,1,18),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION);
    reject(suite,"descriptor-header-truncated",Bytes(offsetof(USB_DESCRIPTOR_REQUEST,Data)-1),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION);
    reject(suite,"node-structure-truncated",Bytes(sizeof(USB_NODE_INFORMATION)-1),IOCTL_USB_GET_NODE_INFORMATION);
    reject(suite,"driver-key-structure-truncated",Bytes(sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME)-1),IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME);
    reject(suite,"connection-info-structure-truncated",Bytes(sizeof(USB_NODE_CONNECTION_INFORMATION_EX)-1),IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX);
    reject(suite,"empty-hub-path",descriptor(),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,"invalid-query-bounds",ERROR_INVALID_PARAMETER,L"");
    reject(suite,"hub-path-over-32760",descriptor(),IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,"invalid-query-bounds",ERROR_INVALID_PARAMETER,std::wstring(32761,L'h'));
    auto oversized=node();oversized.resize(32769);
    reject(suite,"request-buffer-over-32768",oversized,IOCTL_USB_GET_NODE_INFORMATION,"invalid-query-bounds",ERROR_INVALID_PARAMETER);
    for(unsigned type:{1u,2u,3u})for(unsigned index:{0u,1u,255u})for(unsigned language:{0u,0x0409u,0x0609u}){
        const bool allowed=type==1?index==0&&language==0:type==2?language==0:index==0?language==0:language!=0;
        suite.run("descriptor-policy-type-"+std::to_string(type)+"-index-"+std::to_string(index)+"-lang-"+std::to_string(language),"allowlist-and-bounds",[=](Fixture&f){
            auto io=f.make();const auto r=f.query(io,descriptor(type,index,language));
            f.reply(r,allowed,allowed?"completed":"request-not-allowlisted",allowed?0:ERROR_NOT_SUPPORTED);
            f.counts(allowed?1u:0u,allowed?1u:0u,allowed?2u:0u,allowed?1u:0u,0,0,0,0);});
    }
    for(unsigned index=0;index<=255;++index){
        suite.run("configuration-descriptor-index-"+std::to_string(index),"allowlist-and-bounds",[=](Fixture&f){
            auto io=f.make();const auto r=f.query(io,descriptor(2,index));f.reply(r,true,"completed",0);f.counts(1,1,2,1,0,0,0,0);});
    }
    const std::vector<std::pair<DWORD,Bytes>>allowedQueries{
        {IOCTL_USB_GET_NODE_INFORMATION,node()},{IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,driverKey()},
        {IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,connection()},
        {IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,driverKey(0xffffffffu)},
        {IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,connection(0xffffffffu)}};
    for(std::size_t i=0;i<allowedQueries.size();++i){suite.run("allowlisted-hub-query-"+std::to_string(i),"allowlist-and-bounds",[=](Fixture&f){
        auto io=f.make();const auto r=f.query(io,allowedQueries[i].second,allowedQueries[i].first);f.reply(r,true,"completed",0);f.counts(1,1,2,1,0,0,0,0);});}
    suite.run("descriptor-exact-wLength-16384","allowlist-and-bounds",[](Fixture&f){auto io=f.make();
        const auto r=f.query(io,descriptor(2,255,0,16384,1,16384));f.reply(r,true,"completed",0);f.counts(1,1,2,1,0,0,0,0);});
    suite.run("exact-hub-path-and-buffer-bounds","allowlist-and-bounds",[](Fixture&f){auto input=node();input.resize(32768);
        auto io=f.make();const auto r=f.query(io,input,IOCTL_USB_GET_NODE_INFORMATION,500,std::wstring(32760,L'h'));
        f.reply(r,true,"completed",0);f.counts(1,1,2,1,0,0,0,0);});
    Json totals=Counts{}.json(),categories=Json::object();
    Json publicTotals={{"constructor",0},{"destructor",0},{"query",0},{"pending",0},{"reap",0}};
    for(const auto&c:suite.cases){
        for(const auto&key:hookNames)totals[key]=totals[key].get<std::uint64_t>()+c.at("hookCalls").at(key).get<std::uint64_t>();
        for(auto i=publicTotals.begin();i!=publicTotals.end();++i)i.value()=i.value().get<std::uint64_t>()+c.at("publicCalls").at(i.key()).get<std::uint64_t>();
        const auto category=c.at("category").get<std::string>();
        if(!categories.contains(category))categories[category]={{"cases",0},{"failures",0}};
        categories[category]["cases"]=categories[category]["cases"].get<unsigned>()+1;
        if(!c.at("passed").get<bool>())categories[category]["failures"]=categories[category]["failures"].get<unsigned>()+1;
    }
    Json report={{"testKind","standalone-all-fake-IoHooks"},{"systemHooksUsed",false},{"liveDeviceIo",false},
        {"realPlatformHandleOperations",false},{"caseCount",suite.cases.size()},{"failureCount",suite.failures},{"passed",suite.failures==0},
        {"hookCallTotals",totals},{"publicCallTotals",publicTotals},{"categoryTotals",categories},
        {"unknownStorageChecks","actual buffer/OVERLAPPED reads and writes before/after retention and destructor; ASan-compatible"},
        {"exceptionScope","fake hook runtime_error and bad_alloc; no replacement global allocator or live kernel request"},
        {"retentionScope","intentional unreaped unknown storage survives through process exit"},{"cases",std::move(suite.cases)}};
    std::ofstream output(argv[1],std::ios::binary);if(!output){std::cerr<<"Cannot open report\n";return 2;}
    output<<report.dump(2)<<'\n';output.close();if(!output){std::cerr<<"Cannot write report\n";return 2;}
    std::cout<<"MSI_USB_NAME_ORIGIN_IO_SELFTEST cases="<<report["caseCount"]<<" failures="<<suite.failures
        <<" passed="<<(suite.failures==0?"true":"false")<<'\n';
    std::cout<<"HOOK_CALL_TOTALS "<<totals.dump()<<'\n'<<"PUBLIC_CALL_TOTALS "<<publicTotals.dump()<<'\n';
    return suite.failures==0?0:1;
}
