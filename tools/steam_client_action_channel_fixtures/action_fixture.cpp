// Private directed fixture. Compile only after external owned-runner authorization.
// Includes below are exact generated product bodies; no second Pending/serve algorithm.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "action_config.h"
#pragma comment(lib,"bcrypt.lib")

namespace action_ports {
constexpr DWORD providerId=41001, callerId=41002, targetId=41003;
constexpr std::uint64_t providerBirth=100, callerBirth=200, targetBirth=300;
inline ULONGLONG tick=10000;
inline DWORD context=providerId, senderPid=callerId;
inline bool callerLive=true, providerLive=true, ancestorLive=true, owned=true, sameUser=true;
inline bool nativeReady=true, inventoryComplete=true;
inline unsigned inventoryReads=0, ownershipReads=0, sends=0, windowsCreated=0, windowsClosed=0;
inline unsigned proofPidQueries=0;
inline bool forgeProofServer=false;
inline ULONG lastKernelServer=0;
inline std::wstring callerPath=L"X:\\ActionFixture\\CustomSteamLibrary\\CustomSteamLibrary.exe";
inline std::function<LRESULT(HWND,HWND,const COPYDATASTRUCT*)> transport;
inline std::function<bool()> afterSend;
inline HWND providerWindow(){return reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x101));}
inline HWND callerWindow(){return reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x102));}
inline ULONGLONG now() noexcept{return tick;}
inline DWORD processId() noexcept{return context;}
inline DWORD windowPid(HWND w,DWORD* pid) noexcept {
    if(pid)*pid=w==providerWindow()?providerId:w==callerWindow()?senderPid:0;
    return 1;
}
inline BOOL enumWindows(WNDENUMPROC fn,LPARAM arg){fn(providerWindow(),arg);return TRUE;}
inline HANDLE property(HWND,LPCWSTR){return reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1));}
inline HWND createWindow(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID){++windowsCreated;return callerWindow();}
inline BOOL destroyWindow(HWND){++windowsClosed;return TRUE;}
// Real kernel query first. Only the synthetic Peer PID representation is mapped.
inline BOOL pipeServerProcessId(HANDLE pipe,PULONG pid) noexcept {
    ++proofPidQueries;ULONG actual=0;
    if(!::GetNamedPipeServerProcessId(pipe,&actual))return FALSE;
    lastKernelServer=actual;
    if(actual!=::GetCurrentProcessId()){SetLastError(ERROR_INVALID_DATA);return FALSE;}
    *pid=forgeProofServer?callerId+1:callerId;return TRUE;
}
inline LRESULT sendMessage(HWND window,UINT message,WPARAM sender,LPARAM data,UINT,UINT,PDWORD_PTR ack){
    ++sends;if(message!=WM_COPYDATA||!transport)return 0;
    const auto previous=context;context=providerId;
    LRESULT result=0;
    try{result=transport(window,reinterpret_cast<HWND>(sender),reinterpret_cast<const COPYDATASTRUCT*>(data));}
    catch(...){context=previous;throw;}
    context=previous;*ack=static_cast<DWORD_PTR>(result);
    return afterSend?static_cast<LRESULT>(afterSend()):1;
}
inline void reset(){
    tick=10000;context=providerId;senderPid=callerId;
    callerLive=providerLive=ancestorLive=owned=sameUser=nativeReady=inventoryComplete=true;
    inventoryReads=ownershipReads=sends=windowsCreated=windowsClosed=0;
    proofPidQueries=0;forgeProofServer=false;lastKernelServer=0;
    callerPath=L"X:\\ActionFixture\\CustomSteamLibrary\\CustomSteamLibrary.exe";
    transport={};afterSend={};
}
}
namespace ymcc::processInventory {
struct Snapshot{bool complete;};
inline int resolveQuery(){return 0;}
inline ULONGLONG currentTime(){return action_ports::tick;}
inline std::shared_ptr<const Snapshot> read(int,ULONGLONG(*)(),int){
    ++action_ports::inventoryReads;return std::make_shared<Snapshot>(Snapshot{action_ports::inventoryComplete});
}
}
namespace ymcc::steaminfo {
inline constexpr std::uint32_t protocol=1;
inline constexpr wchar_t property[]=L"YeManCC.SharedSteamInformation.v1";
struct Handle {
    HANDLE value=nullptr;Handle()=default;explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
struct View {void* value=nullptr;~View(){if(value)UnmapViewOfFile(value);}View()=default;View(const View&)=delete;};
struct Peer {
    DWORD pid=0;std::uint64_t birth=0;std::wstring path;
    bool load(DWORD id){
        if(pid)return false;
        if(id!=action_ports::providerId&&id!=action_ports::callerId)return false;
        pid=id;birth=id==action_ports::providerId?action_ports::providerBirth:action_ports::callerBirth;
        path=id==action_ports::providerId?L"X:\\ActionFixture\\YeManCC.exe":action_ports::callerPath;
        return live();
    }
    bool live()const noexcept{return pid==action_ports::providerId?action_ports::providerLive:pid==action_ports::callerId&&action_ports::callerLive;}
    bool sameUser(const Peer& other)const noexcept{return action_ports::sameUser&&live()&&other.live();}
};
inline std::wstring pathKey(std::wstring value){std::replace(value.begin(),value.end(),L'/',L'\\');std::transform(value.begin(),value.end(),value.begin(),::towlower);while(!value.empty()&&value.back()==L'\\')value.pop_back();return value;}
struct Ancestry {bool live()const noexcept{return action_ports::ancestorLive;}};
inline bool callerOwned(const Peer& provider,const Peer& caller,Ancestry&,const std::shared_ptr<const processInventory::Snapshot>& table){
    ++action_ports::ownershipReads;
    return action_ports::owned&&action_ports::ancestorLive&&table&&table->complete&&provider.sameUser(caller);
}
}
// Only external leaves are replaced. The entire two product namespaces follow.
#define GetTickCount64 action_ports::now
#define GetCurrentProcessId action_ports::processId
#define GetWindowThreadProcessId action_ports::windowPid
#define EnumWindows action_ports::enumWindows
#define GetPropW action_ports::property
#define CreateWindowExW action_ports::createWindow
#define DestroyWindow action_ports::destroyWindow
#define SendMessageTimeoutW action_ports::sendMessage
#define GetNamedPipeServerProcessId action_ports::pipeServerProcessId
#include "action_channel.inc"
#include "action_producer.inc"
#undef GetTickCount64
#undef GetCurrentProcessId
#undef GetWindowThreadProcessId
#undef EnumWindows
#undef GetPropW
#undef CreateWindowExW
#undef DestroyWindow
#undef SendMessageTimeoutW
#undef GetNamedPipeServerProcessId

namespace fixture {
using namespace ymcc::steamaction;
inline unsigned checks=0,casesStarted=0,casesPassed=0;
inline std::string activeCase;
void check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
Request request(){
    Request r;r.callerPid=action_ports::callerId;r.callerBirth=action_ports::callerBirth;
    r.targetPid=action_ports::targetId;r.targetBirth=action_ports::targetBirth;r.deadline=action_ports::tick+1000;
#if ACTION_HAS_POWER
    r.powerGeneration=7;r.powerSequence=8;r.powerSerial=9;
#endif
    static std::uint64_t sequence=0;const auto seq=++sequence;
    const auto pid=::GetCurrentProcessId();const auto stamp=::GetTickCount64();
    std::memcpy(r.nonce.data(),&seq,sizeof(seq));
    std::memcpy(r.nonce.data()+8,&pid,sizeof(pid));
    const auto salt=static_cast<std::uint32_t>(stamp);std::memcpy(r.nonce.data()+12,&salt,sizeof(salt));return r;
}
#if ACTION_HAS_PROOF
// Self-owned inert object only: no ConnectNamedPipe/ReadFile/WriteFile is issued.
void createProofPipe(const Request& r,ymcc::steaminfo::Handle& proof){
    proof.value=::CreateNamedPipeW(proofPipeName(r).c_str(),
        PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,256,256,0,nullptr);
    if(!proof.value||proof.value==INVALID_HANDLE_VALUE)throw std::runtime_error("owned proof pipe creation failed");
}
#endif
struct Harness {
    Request r=request();ymcc::steaminfo::Handle mapping,done;ymcc::steaminfo::View view;
    ymcc::steaminfo::Handle proof;
    std::vector<std::function<void()>> queue;
    unsigned submissions=0,dispatches=0,claims=0,effects=0;
    bool queueAccept=true,dispatchAccept=true,throwDispatch=false;
    std::function<void()> beforeClaim;
    Wire* wire()const{return static_cast<Wire*>(view.value);}
    Harness(){
#if ACTION_HAS_PROOF
        createProofPipe(r,proof);
#endif
        const auto name=mappingName(r);SetLastError(ERROR_SUCCESS);
        mapping.value=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,static_cast<DWORD>(sizeof(Wire)),name.c_str());
        if(!mapping.value||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("owned mapping creation/collision");
        view.value=MapViewOfFile(mapping.value,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Wire));
        if(!view.value)throw std::runtime_error("owned view creation");
        Wire initial;initial.request=r;std::memcpy(wire(),&initial,sizeof(initial));
        SetLastError(ERROR_SUCCESS);done.value=CreateEventW(nullptr,TRUE,FALSE,(name+L".Done").c_str());
        if(!done.value||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("owned event creation/collision");
    }
    ~Harness(){queue.clear();}
    Submit submit(){return [this](std::function<void()> job){++submissions;if(!queueAccept)return false;queue.push_back(std::move(job));return true;};}
    Dispatch dispatch(){return [this](const Request& got,const std::function<bool()>& current,const std::function<bool()>& claim){
        ++dispatches;check(matches(got,r),"dispatcher immutable request changed");
        check(got.targetPid==action_ports::targetId&&got.targetBirth==action_ports::targetBirth,"fixed target changed");
        if(throwDispatch)throw std::runtime_error("inert dispatcher exception");
        if(beforeClaim)beforeClaim();
        if(!current())return false;
        if(!claim())return false;
        ++claims;
        if(!current())return false;
        if(dispatchAccept)++effects;
        return dispatchAccept;
    };}
    LRESULT send(const Request* supplied=nullptr,DWORD bytes=static_cast<DWORD>(sizeof(Request)),ULONG_PTR tag=messageTag){
        const auto* value=supplied?supplied:&r;COPYDATASTRUCT data{tag,bytes,const_cast<Request*>(value)};
        return serve(action_ports::providerWindow(),action_ports::callerWindow(),&data,submit(),dispatch(),[]{return action_ports::nativeReady;});
    }
    void drain(){auto jobs=std::move(queue);queue.clear();for(auto& job:jobs)job();}
    void rejected(){check(wire()->completed==1&&wire()->outcome==static_cast<LONG>(Outcome::Rejected),"expected terminal Rejected");check(effects==0,"stale effect dispatched");}
    void observe(){
        std::cout<<"{\"kind\":\"observation\",\"case\":\""<<activeCase<<"\",\"accepted\":"<<wire()->accepted
          <<",\"claimed\":"<<wire()->claimed<<",\"cancelled\":"<<wire()->cancelled<<",\"completed\":"<<wire()->completed
          <<",\"outcome\":"<<wire()->outcome<<",\"submissions\":"<<submissions<<",\"dispatches\":"<<dispatches
          <<",\"claims\":"<<claims<<",\"effects\":"<<effects<<",\"requestBytes\":"<<sizeof(Request)<<",\"wireBytes\":"<<sizeof(Wire)
          <<",\"proofPidQueries\":"<<action_ports::proofPidQueries<<",\"kernelServerPid\":"<<action_ports::lastKernelServer
          <<",\"productExecuted\":false,\"defaultExecuted\":false}"<<std::endl;
    }
};
struct Case{const char* name;std::function<void()> body;};
std::vector<Case> cases(){std::vector<Case> result={
    {"malformed-command",[]{Harness h;auto bad=h.r;bad.command=0;check(!valid(bad,action_ports::tick),"command valid");check(h.send(&bad)==0&&h.submissions==0,"command enqueued");h.observe();}},
    {"malformed-size",[]{Harness h;auto bad=h.r;--bad.size;check(!valid(bad,action_ports::tick),"request size valid");check(h.send(&bad)==0&&h.send(nullptr,static_cast<DWORD>(sizeof(Request)-1))==0&&h.send(nullptr,static_cast<DWORD>(sizeof(Request)+1))==0&&h.submissions==0,"size enqueued");h.observe();}},
    {"reserved-version-envelope",[]{Harness h;auto bad=h.r;bad.reserved=1;check(!valid(bad,action_ports::tick)&&h.send(&bad)==0,"reserved accepted");bad=h.r;++bad.version;check(!valid(bad,action_ports::tick)&&h.send(&bad)==0,"version accepted");check(h.send(nullptr,static_cast<DWORD>(sizeof(Request)),messageTag+1)==0&&h.submissions==0,"tag accepted");h.observe();}},
    {"deadline-boundaries",[]{Harness h;auto bad=h.r;bad.deadline=action_ports::tick;check(!valid(bad,action_ports::tick)&&h.send(&bad)==0,"exact expiry accepted");--bad.deadline;check(!valid(bad,action_ports::tick),"past valid");bad.deadline=action_ports::tick+requestBudgetMs+1;check(!valid(bad,action_ports::tick)&&h.send(&bad)==0,"over budget accepted");bad.deadline=action_ports::tick+requestBudgetMs;check(valid(bad,action_ports::tick),"budget boundary refused");check(h.submissions==0,"deadline enqueued");h.observe();}},
    {"nonce-zero",[]{Harness h;auto bad=h.r;bad.nonce.fill(0);check(!valid(bad,action_ports::tick)&&h.send(&bad)==0&&h.submissions==0,"zero nonce enqueued");h.observe();}},
#if ACTION_HAS_POWER
    {"power-generation-zero",[]{Harness h;auto bad=h.r;bad.powerGeneration=0;check(!valid(bad,action_ports::tick)&&h.send(&bad)==0&&h.submissions==0,"zero power generation enqueued");h.observe();}},
    {"power-expectation-matches",[]{Harness h;auto changed=h.r;++changed.powerGeneration;check(!matches(changed,h.r),"generation mismatch ignored");changed=h.r;++changed.powerSequence;check(!matches(changed,h.r),"sequence mismatch ignored");changed=h.r;++changed.powerSerial;check(!matches(changed,h.r),"serial mismatch ignored");check(valid(h.r,action_ports::tick),"valid expectation refused");h.observe();}},
#endif
    {"forged-caller-pid",[]{Harness h;action_ports::senderPid=action_ports::callerId+10;check(h.send()==0&&h.submissions==0,"forged sender enqueued");h.observe();}},
    {"forged-caller-birth",[]{Harness h;auto forged=h.r;++forged.callerBirth;check(h.send(&forged)==0&&h.submissions==0,"forged birth enqueued");h.observe();}},
    {"worker-path-refused",[]{Harness h;action_ports::callerPath=L"X:\\ActionFixture\\CustomSteamLibrary\\SteamArtworkLab.exe";check(h.send()==0&&h.submissions==0,"worker acquired shutdown authority");h.observe();}},
    {"peer-ancestry-refused",[]{Harness h;action_ports::owned=false;check(h.send()==0&&h.submissions==0&&action_ports::ownershipReads==1,"ancestry refusal ignored");h.observe();}},
    {"inventory-incomplete",[]{Harness h;action_ports::inventoryComplete=false;check(h.send()==0&&h.submissions==0&&action_ports::inventoryReads==1,"incomplete inventory accepted");h.observe();}},
    {"duplicate-accepted-once",[]{Harness h;check(h.send()==protocol,"first not acknowledged");check(h.send()==0&&h.submissions==1&&h.queue.size()==1,"duplicate enqueued twice");h.drain();check(h.effects==1,"unique dispatch lost");h.observe();}},
    {"queued-cancel",[]{Harness h;check(h.send()==protocol,"not queued");InterlockedExchange(&h.wire()->cancelled,1);h.drain();h.rejected();check(h.dispatches==0,"cancel reached dispatcher");h.observe();}},
    {"queued-expiry",[]{Harness h;check(h.send()==protocol,"not queued");action_ports::tick=h.r.deadline;h.drain();h.rejected();check(h.dispatches==0,"expiry reached dispatcher");h.observe();}},
    {"queued-power-aba",[]{Harness h;std::uint64_t epoch=7;const auto captured=epoch;COPYDATASTRUCT data{messageTag,static_cast<DWORD>(sizeof(Request)),&h.r};check(serve(action_ports::providerWindow(),action_ports::callerWindow(),&data,h.submit(),h.dispatch(),[&]{return action_ports::nativeReady&&epoch==captured;})==protocol,"not queued");action_ports::nativeReady=false;++epoch;action_ports::nativeReady=true;h.drain();h.rejected();check(h.dispatches==0,"injected power ABA reached dispatch");h.observe();}},
    {"queued-caller-exit",[]{Harness h;check(h.send()==protocol,"not queued");action_ports::callerLive=false;h.drain();h.rejected();check(h.dispatches==0,"dead caller reached dispatcher");h.observe();}},
    {"queued-provider-exit",[]{Harness h;check(h.send()==protocol,"not queued");action_ports::providerLive=false;h.drain();h.rejected();check(h.dispatches==0,"dead provider reached dispatcher");h.observe();}},
    {"queued-ancestor-exit",[]{Harness h;check(h.send()==protocol,"not queued");action_ports::ancestorLive=false;h.drain();h.rejected();check(h.dispatches==0,"dead ancestor reached dispatcher");h.observe();}},
    {"final-claim-once",[]{Harness h;check(h.send()==protocol,"not queued");Pending p(h.r);p.provider.load(action_ports::providerId);p.caller.load(action_ports::callerId);const auto name=mappingName(h.r);p.mapped.handle.value=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,name.c_str());p.mapped.view.value=MapViewOfFile(p.mapped.handle.value,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Wire));check(p.current(),"Pending not current");check(p.claim()&&!p.claim(),"claim was not once");h.drain();check(h.effects==0&&h.wire()->claimed==1,"second claim dispatched");h.observe();}},
    {"final-native-current-failure",[]{Harness h;h.beforeClaim=[]{action_ports::nativeReady=false;};check(h.send()==protocol,"not queued");h.drain();h.rejected();check(h.dispatches==1&&h.wire()->claimed==0,"native Current ignored before claim");h.observe();}},
    {"valid-fixed-target",[]{Harness h;check(valid(h.r,action_ports::tick)&&matches(h.r,h.r),"valid request refused");check(h.send()==protocol,"not queued");h.drain();check(h.dispatches==1&&h.claims==1&&h.effects==1&&h.wire()->completed==1&&h.wire()->outcome==static_cast<LONG>(Outcome::Submitted),"fixed submission failed");check(h.wire()->providerPid==action_ports::providerId&&h.wire()->providerBirth==action_ports::providerBirth,"provider receipt missing");check(WaitForSingleObject(h.done.value,0)==WAIT_OBJECT_0,"completion event not signalled");h.observe();}},
    {"dispatcher-throws-unknown",[]{Harness h;h.throwDispatch=true;check(h.send()==protocol,"not queued");h.drain();check(h.wire()->completed==1&&h.wire()->outcome==static_cast<LONG>(Outcome::Unknown)&&h.dispatches==1&&h.effects==0,"dispatcher exception outcome wrong");h.observe();}},
    {"queue-rejects",[]{Harness h;h.queueAccept=false;check(h.send()==protocol,"queue rejection lost acknowledgement");h.rejected();check(h.submissions==1&&h.queue.empty()&&h.dispatches==0,"queue rejection dispatched");h.observe();}},
    {"request-changed",[]{
        {Harness rejected;auto mismatch=rejected.r;++mismatch.targetBirth;
         check(!matches(rejected.r,mismatch)&&rejected.send(&mismatch)==0&&rejected.submissions==0,"prequeue mismatch accepted");rejected.observe();}
        // A rejected authenticated open consumes that pipe instance. A fresh
        // request below has a fresh nonce/server, never a replay of the proof.
        Harness h;check(h.send()==protocol,"fresh request not queued");++h.wire()->request.targetBirth;
        h.drain();h.rejected();check(h.dispatches==0,"mutated queued request dispatched");h.observe();
    }},
    {"dispatcher-false-failed",[]{Harness h;h.dispatchAccept=false;check(h.send()==protocol,"not queued");h.drain();check(h.wire()->completed==1&&h.wire()->outcome==static_cast<LONG>(Outcome::Failed)&&h.claims==1&&h.effects==0,"dispatcher false not Failed");h.observe();}},
#if ACTION_HAS_CANCEL_ON_RETURN
    {"client-success-cancels-on-return",[]{
        Harness h;ymcc::steamaction::Mapping retained;
        action_ports::context=action_ports::callerId;
        action_ports::transport=[&](HWND window,HWND sender,const COPYDATASTRUCT* data){
            Request got;std::memcpy(&got,data->lpData,sizeof(got));h.r=got;
            const auto name=mappingName(got);retained.handle.value=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,name.c_str());
            retained.view.value=MapViewOfFile(retained.handle.value,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Wire));
            check(retained.view.value!=nullptr,"retain successful client mapping failed");
            const auto ack=serve(window,sender,data,h.submit(),h.dispatch(),[]{return action_ports::nativeReady;});
            h.drain();return ack;
        };
#if ACTION_HAS_POWER
        const auto result=shutdown(action_ports::targetId,action_ports::targetBirth,action_ports::tick+1000,7,8,9,[]{return true;});
#else
        const auto result=shutdown(action_ports::targetId,action_ports::targetBirth,action_ports::tick+1000,[]{return true;});
#endif
        const auto* clientWire=static_cast<const Wire*>(retained.view.value);
        check(result==Outcome::Submitted&&action_ports::sends==1&&h.effects==1,"valid client submission not once");
        check(clientWire&&clientWire->completed==1&&clientWire->cancelled==1,"successful return missed CancelOnReturn");
        check(action_ports::windowsCreated==1&&action_ports::windowsClosed==1,"successful inert window leaked");
        std::cout<<"{\"kind\":\"observation\",\"case\":\""<<activeCase<<"\",\"sends\":"<<action_ports::sends<<",\"outcome\":"<<static_cast<LONG>(result)<<",\"cancelled\":"<<clientWire->cancelled<<",\"effects\":"<<h.effects<<",\"productExecuted\":false,\"defaultExecuted\":false}"<<std::endl;
    }},
#endif
    {"unknown-outcome-no-retry",[]{
        Harness h;ymcc::steamaction::Mapping retained;
        action_ports::context=action_ports::callerId;
        action_ports::transport=[&](HWND window,HWND sender,const COPYDATASTRUCT* data){
            Request got;std::memcpy(&got,data->lpData,sizeof(got));h.r=got;
            const auto name=mappingName(got);retained.handle.value=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,name.c_str());
            retained.view.value=MapViewOfFile(retained.handle.value,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Wire));
            check(retained.view.value!=nullptr,"retain client mapping failed");
            return serve(window,sender,data,h.submit(),h.dispatch(),[]{return action_ports::nativeReady;});
        };
        action_ports::afterSend=[]{return false;};
#if ACTION_HAS_POWER
        const auto result=shutdown(action_ports::targetId,action_ports::targetBirth,action_ports::tick+1000,7,8,9,[]{return true;});
#else
        const auto result=shutdown(action_ports::targetId,action_ports::targetBirth,action_ports::tick+1000,[]{return true;});
#endif
        check(result==Outcome::Unknown&&action_ports::sends==1&&h.submissions==1,"unknown result resent or not observed");
        auto* clientWire=static_cast<Wire*>(retained.view.value);
        check(clientWire&&clientWire->cancelled==1,"unknown return failed to cancel queued request");
        h.drain();check(h.dispatches==0&&h.effects==0&&clientWire->completed==1&&clientWire->outcome==static_cast<LONG>(Outcome::Rejected),"late queue dispatched after unknown return");
        check(action_ports::windowsCreated==1&&action_ports::windowsClosed==1,"inert sender lifetime leaked");
        std::cout<<"{\"kind\":\"observation\",\"case\":\""<<activeCase<<"\",\"sends\":"<<action_ports::sends<<",\"outcome\":"<<static_cast<LONG>(result)<<",\"cancelled\":"<<clientWire->cancelled<<",\"dispatches\":"<<h.dispatches<<",\"productExecuted\":false,\"defaultExecuted\":false}"<<std::endl;
    }},
#if ACTION_HAS_PROOF
    {"forged-pipe-server-pid",[]{
        Harness h;action_ports::forgeProofServer=true;
        check(h.send()==0,"foreign server PID accepted");
        check(action_ports::proofPidQueries==1&&action_ports::lastKernelServer==::GetCurrentProcessId(),"negative did not query actual owned kernel pipe");
        check(h.submissions==0&&h.wire()->accepted==0&&h.effects==0,"forged server reached queue");h.observe();
    }},
    {"pipe-open-fail",[]{
        Harness h;check(::CloseHandle(h.proof.value)!=FALSE,"owned proof close failed");h.proof.value=nullptr;
        check(h.send()==0&&h.submissions==0&&h.wire()->accepted==0,"missing proof pipe accepted");
        check(action_ports::proofPidQueries==0&&h.effects==0,"failed open reached PID query/dispatch");h.observe();
    }},
    {"proof-replay-refused",[]{
        Harness h;check(h.send()==protocol&&h.submissions==1,"first proof not accepted");
        const auto queries=action_ports::proofPidQueries;
        // Even a malicious shared-wire reset cannot make the held one-instance
        // proof accept a second connection while its first Pending owns it.
        InterlockedExchange(&h.wire()->accepted,0);
        check(h.send()==0&&h.submissions==1&&h.queue.size()==1,"same proof replay enqueued twice");
        check(action_ports::proofPidQueries==queries,"busy replay unexpectedly acquired another connection");
        InterlockedExchange(&h.wire()->accepted,1);h.drain();check(h.effects==1,"original proof lost its one dispatch");h.observe();
    }},
    {"proof-kernel-current-pid",[]{
        Harness h;const auto name=proofPipeName(h.r);
        check(name==std::wstring(L"\\\\.\\pipe\\")+mappingName(h.r).substr(6),"proof path literal differs from local pipe prefix");
        ymcc::steaminfo::Handle client(::CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,
            OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
        check(client.value&&client.value!=INVALID_HANDLE_VALUE,"owned client cannot open before ConnectNamedPipe");
        ULONG actual=0;check(::GetNamedPipeServerProcessId(client.value,&actual)!=FALSE,"actual kernel server PID query failed");
        check(actual==::GetCurrentProcessId(),"kernel server is not fixture process");
        check(action_ports::proofPidQueries==0&&h.submissions==0&&h.effects==0,"kernel-only case used mapped identity/dispatcher");
        std::cout<<"{\"kind\":\"observation\",\"case\":\""<<activeCase<<"\",\"kernelServerPid\":"<<actual
          <<",\"kernelCurrentPid\":"<<::GetCurrentProcessId()<<",\"identityMapped\":false,\"ioOperationsStarted\":0,\"productExecuted\":false,\"defaultExecuted\":false}"<<std::endl;
    }},
#endif
};
#if ACTION_AUTH_SUCCESSOR
    // Same original case identities; this epoch prepares only proof-affected
    // suffixes. No malformed or earlier identity rejection is repeated.
    const auto unchanged=[](const Case& c){const std::string_view name=c.name;
        return name=="malformed-command"||name=="malformed-size"||name=="reserved-version-envelope"||
            name=="deadline-boundaries"||name=="nonce-zero"||name=="power-generation-zero"||
            name=="power-expectation-matches"||name=="forged-caller-pid"||name=="forged-caller-birth"||
            name=="worker-path-refused"||name=="peer-ancestry-refused"||name=="inventory-incomplete";};
    result.erase(std::remove_if(result.begin(),result.end(),unchanged),result.end());
#endif
    return result;
}
void summary(const char* status){
    std::cout<<"{\"kind\":\"summary\",\"status\":\""<<status<<"\",\"mode\":\"action-channel\",\"casesStarted\":"<<casesStarted
      <<",\"casesPassed\":"<<casesPassed<<",\"checks\":"<<checks<<",\"productExecuted\":false,\"defaultExecuted\":false}"<<std::endl;
}
}
int main(int argc,char** argv){
    using namespace fixture;
    const auto all=cases();
    if(argc<2||argc>3||std::string_view(argv[1])!="--action-channel"){summary("SELECTION_REJECTED");return 2;}
    const std::string_view selected=argc==3?std::string_view(argv[2]):std::string_view{};
    if(argc==3&&(selected.empty()||std::none_of(all.begin(),all.end(),[&](const Case& c){return selected==c.name;}))){summary("SELECTION_REJECTED");return 2;}
    try{
        for(const auto& c:all){
            if(argc==3&&selected!=c.name)continue;
            action_ports::reset();activeCase=c.name;++casesStarted;const auto before=checks;
            c.body();++casesPassed;
            std::cout<<"{\"kind\":\"case\",\"case\":\""<<c.name<<"\",\"status\":\"PASS\",\"checks\":"<<(checks-before)<<",\"productExecuted\":false,\"defaultExecuted\":false}"<<std::endl;
        }
        if(!casesStarted){summary("SELECTION_REJECTED");return 2;}
        summary("PASS");return 0;
    }catch(const std::exception& e){
        std::cout<<"{\"kind\":\"case\",\"case\":\""<<activeCase<<"\",\"status\":\"FAIL\",\"productExecuted\":false,\"defaultExecuted\":false}"<<std::endl;
        std::cerr<<activeCase<<": "<<e.what()<<std::endl;summary("FAIL");return 1;
    }catch(...){summary("FAIL");return 1;}
}
