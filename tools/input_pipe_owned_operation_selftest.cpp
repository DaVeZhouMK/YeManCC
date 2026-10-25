// R40: real C++ ownership with deterministic API-result models, not real pipes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>
#include "json.hpp"
#include "input_pipe_owned_operation_model.h"
using json=nlohmann::json;
namespace own=ymcc::testing::pipeOwnership;
static unsigned long long checks=0;
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct Backend {
    struct Row {
        own::Kind kind{};const own::OverlappedStorage* ov=nullptr;unsigned char* data=nullptr;size_t size=0;
        bool alive=true,pending=false,terminal=false,issued=false;
        own::Issue nextIssue=own::Issue::Pending;own::Poll nextPoll=own::Poll::Pending;
        unsigned cancels=0,polls=0;
    };
    std::map<uint64_t,Row> rows;uint64_t next=0;unsigned destroys=0,destroyPending=0,lateWrites=0,backendIssueCalls=0;
    bool throwCreate=false;
    uint64_t create(own::Kind kind,const own::OverlappedStorage* ov,unsigned char* data,size_t n){
        if(throwCreate)throw std::bad_alloc();
        Row row;row.kind=kind;row.ov=ov;row.data=data;row.size=n;rows.emplace(++next,row);return next;
    }
    own::Issue issue(uint64_t id) noexcept {
        auto& r=rows.at(id);++backendIssueCalls;r.issued=true;r.pending=r.nextIssue==own::Issue::Pending;r.terminal=!r.pending;return r.nextIssue;
    }
    void cancel(uint64_t id) noexcept {++rows.at(id).cancels;}
    own::Poll poll(uint64_t id) noexcept {auto& r=rows.at(id);++r.polls;return r.nextPoll;}
    void destroy(uint64_t id,const own::OverlappedStorage* ov,const unsigned char* data,size_t n) noexcept {
        auto& r=rows.at(id);++destroys;destroyPending+=r.pending;r.alive=false;
        if(r.ov!=ov||r.data!=data||r.size!=n)++destroyPending;
    }
    void complete(uint64_t id,bool publishResult=true){auto& r=rows.at(id);r.pending=false;r.terminal=true;if(publishResult)r.nextPoll=own::Poll::Terminal;}
    void writeLate(uint64_t id,unsigned char value){
        auto& r=rows.at(id);check(r.alive&&r.pending,"late completion storage not retained");
        check(r.ov!=nullptr,"operation storage missing");
        // Dereference only a live, self-pinned candidate owner; never the
        // expired baseline OVERLAPPED/stack pointer in the witness below.
        if(r.kind==own::Kind::Read)for(size_t i=0;i<r.size;++i)r.data[i]=value;
        ++lateWrites;
    }
};
using Registry=own::Registry<Backend,4>;
static uint64_t hash(const std::vector<unsigned char>& v){uint64_t h=1469598103934665603ULL;for(auto c:v){h^=c;h*=1099511628211ULL;}return h;}
static void basic(){
 for(auto kind:{own::Kind::Write,own::Kind::Read,own::Kind::Connect})for(auto issue:{own::Issue::Completed,own::Issue::Rejected,own::Issue::Pending}){
    auto b=std::make_shared<Backend>();auto budget=std::make_shared<own::Budget>(4,65536);Registry registry(b,budget);
    std::vector<unsigned char> source(kind==own::Kind::Connect?0:64,17);auto acquired=registry.acquire(kind,source);auto operation=acquired.first;auto ticket=acquired.second;acquired.first.reset();
    check(operation&&budget->count==1&&budget->bytes==source.size(),"reservation failed");const auto id=operation->id();const auto ov=operation->overlapped();const auto data=operation->data();
    b->rows.at(id).nextIssue=issue;check(operation->issue()==issue,"issue result changed");source.assign(99,88);
    check(operation->overlapped()==ov&&operation->data()==data,"issued object/buffer relocated");
    if(issue!=own::Issue::Pending){check(operation->phase()==own::Phase::Terminal,"immediate result not terminal");operation.reset();check(ticket.weak.expired()&&budget->count==0,"terminal owner not reclaimed");}
    else{
        operation->requestCancel();operation->retire();operation.reset();
        check(!ticket.weak.expired()&&budget->count==1&&b->destroys==0,"caller release destroyed pending operation");
        for(auto observation:{own::Poll::Pending,own::Poll::Unavailable,own::Poll::Pending}){
            b->rows.at(id).nextPoll=observation;check(registry.poll(ticket),"pending ticket lookup failed");check(!ticket.weak.expired(),"unknown/cancel released pending pin");
        }
        b->writeLate(id,41);auto pending=ticket.weak.lock();check(pending->data()==data&&pending->overlapped()==ov,"late pointers changed");
        if(kind==own::Kind::Read)check(pending->bytes()==std::vector<unsigned char>(64,41),"late read did not reach owned buffer");
        if(kind==own::Kind::Write)check(pending->bytes()==std::vector<unsigned char>(64,17),"source mutation leaked into owned write buffer");pending.reset();
        b->complete(id);check(registry.poll(ticket),"terminal ticket query failed");check(ticket.weak.expired()&&budget->count==0&&budget->bytes==0,"terminal pin not released");
    }
    check(b->destroys==1&&b->destroyPending==0,"unsafe or duplicate resource destruction");
 }
}
static void capAndGenerations(){
 auto b=std::make_shared<Backend>();auto budget=std::make_shared<own::Budget>(4,256);Registry first(b,budget);std::array<Registry::Ticket,4> tickets;
 for(unsigned i=0;i<4;++i){auto [op,ticket]=first.acquire(own::Kind::Read,std::vector<unsigned char>(64,static_cast<unsigned char>(i)));check(bool(op),"cap fill failed");tickets[i]=ticket;(void)op->issue();op->retire();}
 check(budget->count==4&&budget->bytes==256,"cap accounting");const auto issued=b->backendIssueCalls;
 for(int i=0;i<10000;++i){Registry anotherSession(b,budget);auto blocked=anotherSession.acquire(own::Kind::Write,{1});check(!blocked.first&&budget->count==4&&b->backendIssueCalls==issued,"new session evicted pending owner or bypassed budget");}
 const auto old=tickets[0];const auto id=old.weak.lock()->id();b->complete(id);check(first.poll(old),"reap first");check(old.weak.expired(),"old weak owner not expired");
 auto [replacement,token]=first.acquire(own::Kind::Write,std::vector<unsigned char>(32,9));check(bool(replacement)&&token.slot==old.slot&&token.generation>old.generation,"slot generation not advanced");
 check(!first.poll(old),"stale ticket touched replacement");check(replacement->phase()==own::Phase::Ready,"stale result changed ready owner");replacement.reset();
 check(token.weak.expired()&&budget->count==3,"unissued owner not released");
 for(unsigned i=1;i<4;++i){const auto x=tickets[i].weak.lock()->id();b->complete(x);}
 first.pollAll();check(budget->count==0&&budget->bytes==0&&b->destroyPending==0,"cap test leaked after proved completions");
 // Byte cap is independent from the object count and leaves no reservation.
 auto tooBig=first.acquire(own::Kind::Read,std::vector<unsigned char>(257,1));check(!tooBig.first&&budget->count==0,"byte cap not enforced");
 b->throwCreate=true;bool threw=false;try{(void)first.acquire(own::Kind::Read,{1,2,3});}catch(const std::bad_alloc&){threw=true;}
 check(threw&&budget->count==0&&budget->bytes==0,"creation exception lost budget reservation");
}
static void detachedOwner(){
 auto b=std::make_shared<Backend>();auto budget=std::make_shared<own::Budget>(4,65536);Registry::Ticket detached;uint64_t id=0;
 {
    Registry localWrapper(b,budget);auto pair=localWrapper.acquire(own::Kind::Read,std::vector<unsigned char>(128,2));id=pair.first->id();detached=pair.second;pair.first->issue();pair.first->requestCancel();pair.first->retire();
 }
 check(!detached.weak.expired()&&budget->count==1&&b->destroys==0,"wrapper shutdown lost pending storage");
 for(int i=0;i<100;++i){check(Registry::pollDetached(detached),"detached poll failed");check(budget->count==1&&b->destroys==0,"elapsed polling became completion proof");}
 b->writeLate(id,66);b->complete(id,false);b->rows.at(id).nextPoll=own::Poll::Unavailable;
 check(Registry::pollDetached(detached)&&!detached.weak.expired(),"unknown observation freed memory despite missing proof");
 b->rows.at(id).nextPoll=own::Poll::Terminal;check(Registry::pollDetached(detached),"detached terminal poll failed");
 check(detached.weak.expired()&&budget->count==0&&b->destroys==1&&b->destroyPending==0,"detached owner did not reclaim exactly once");
}
static void crossRegistryAndUnsoundAdapter(){
 auto b=std::make_shared<Backend>();auto budget=std::make_shared<own::Budget>(4,65536);Registry::Ticket old;uint64_t oldId=0;
 {Registry previous(b,budget);auto pair=previous.acquire(own::Kind::Read,{1,2,3});old=pair.second;oldId=pair.first->id();pair.first->issue();}
 Registry next(b,budget);auto pair=next.acquire(own::Kind::Write,{4,5,6});
 check(pair.second.slot==old.slot&&pair.second.generation==old.generation,"cross-registry numeric collision control absent");
 check(!next.poll(old)&&pair.first->phase()==own::Phase::Ready,"old epoch ticket reached a different live operation");
 pair.first.reset();b->complete(oldId);Registry::pollDetached(old);check(budget->count==0&&b->destroyPending==0,"cross-registry completion lost owner");
 // Negative oracle: an adapter that invents Terminal while the model says
 // Pending breaks the guarantee. No real delayed I/O exists, and the saved
 // pointer is never dereferenced after this deliberate model-only release.
 Registry broken(b,budget);auto bad=broken.acquire(own::Kind::Read,{7,8,9});const auto id=bad.first->id();bad.first->issue();bad.first.reset();
 b->rows.at(id).nextPoll=own::Poll::Terminal;broken.poll(bad.second);
 check(b->destroyPending==1&&bad.second.weak.expired(),"unsound-terminal adapter counterexample was not detected");
 check(budget->count==0,"counterexample accounting failed");
 b->rows.at(id).pending=false; // Model ledger only; never access released memory.
}
static uint64_t random64(uint64_t& x){x^=x<<13;x^=x>>7;x^=x<<17;return x;}
static void histories(){
 uint64_t seed=0x2026101540ULL;unsigned long long lateCompletions=0;
 for(int n=0;n<10000;++n){
    auto b=std::make_shared<Backend>();auto budget=std::make_shared<own::Budget>(4,65536);Registry registry(b,budget);std::vector<unsigned char> source(size_t(random64(seed)%129),static_cast<unsigned char>(seed));
    const auto originalHash=hash(source);auto [op,ticket]=registry.acquire(static_cast<own::Kind>(n%3),source);const auto id=op->id();op->issue();op.reset();source.clear();
    for(unsigned step=0,steps=unsigned(random64(seed)%32);step<steps;++step){auto live=ticket.weak.lock();check(bool(live),"history owner disappeared");
        switch(random64(seed)%6){case 0:live->requestCancel();break;case 1:live->retire();break;case 2:b->rows.at(id).nextPoll=own::Poll::Unavailable;break;case 3:b->rows.at(id).nextPoll=own::Poll::Pending;break;case 4:b->writeLate(id,static_cast<unsigned char>(step));++lateCompletions;break;default:break;}
        live.reset();check(registry.poll(ticket),"history poll not found");check(budget->count==1&&b->destroyPending==0,"cancel/tick/unknown caused early reclamation");
        auto again=ticket.weak.lock();if(b->rows.at(id).kind==own::Kind::Write)check(hash(again->bytes())==originalHash,"write buffer mutated before completion");
    }
    b->complete(id);registry.poll(ticket);check(ticket.weak.expired()&&budget->count==0&&b->destroyPending==0,"history final completion retained or damaged owner");
 }
 check(lateCompletions>10000,"history did not exercise enough late buffer uses");
}
// Source-extracted baseline witness. No expired pointer is dereferenced.
namespace baselineModel {
 static DWORD modelError=ERROR_SUCCESS;static bool pending=false;static unsigned waits=0,closedPending=0;
 static DWORD FakeGetLastError(){return modelError;}
 static HANDLE FakeCreateEventW(void*,BOOL,BOOL,const wchar_t*){return reinterpret_cast<HANDLE>(uintptr_t(0x101));}
 static BOOL Start(DWORD*,OVERLAPPED*){pending=true;modelError=ERROR_IO_PENDING;return FALSE;}
 static BOOL FakeWriteFile(HANDLE,const void*,DWORD,DWORD* b,OVERLAPPED* ov){return Start(b,ov);}
 static BOOL FakeReadFile(HANDLE,void*,DWORD,DWORD* b,OVERLAPPED* ov){return Start(b,ov);}
 static BOOL FakeConnectNamedPipe(HANDLE,OVERLAPPED* ov){DWORD n=0;return Start(&n,ov);}
 static DWORD FakeWaitForSingleObject(HANDLE,DWORD){++waits;return WAIT_TIMEOUT;}
 static BOOL FakeCancelIoEx(HANDLE,OVERLAPPED*){return TRUE;}
 static BOOL FakeGetOverlappedResult(HANDLE,OVERLAPPED*,DWORD*,BOOL){modelError=ERROR_IO_INCOMPLETE;return FALSE;}
 static BOOL FakeCloseHandle(HANDLE){closedPending+=pending;return TRUE;}
#define GetLastError FakeGetLastError
#define CreateEventW FakeCreateEventW
#define WriteFile FakeWriteFile
#define ReadFile FakeReadFile
#define ConnectNamedPipe FakeConnectNamedPipe
#define WaitForSingleObject FakeWaitForSingleObject
#define CancelIoEx FakeCancelIoEx
#define GetOverlappedResult FakeGetOverlappedResult
#define CloseHandle FakeCloseHandle
 #include "actual-baseline-helpers.hpp"
#undef GetLastError
#undef CreateEventW
#undef WriteFile
#undef ReadFile
#undef ConnectNamedPipe
#undef WaitForSingleObject
#undef CancelIoEx
#undef GetOverlappedResult
#undef CloseHandle

 static json witness(){json rows=json::array();for(int kind=0;kind<3;++kind){pending=false;waits=closedPending=0;DWORD bytes=0,err=0;char buffer[16]{};
    const auto pipe=reinterpret_cast<HANDLE>(uintptr_t(0x202));const bool result=kind==2?inputHostWaitClientLocked(pipe,1000,&err):inputHostPipeIo(pipe,kind==0,buffer,16,1000,&bytes,&err);
    check(!result&&pending&&closedPending==1&&waits==2&&err==WAIT_TIMEOUT,"actual baseline pending witness changed");
    rows.push_back({{"kind",kind},{"pendingAtHelperReturn",pending},{"closedEventPending",closedPending},{"reportedError",err},{"expiredPointerDereferenced",false}});
 }return rows;}
}
int main(){try{const auto witness=baselineModel::witness();basic();capAndGenerations();detachedOwner();crossRegistryAndUnsoundAdapter();histories();
 std::cout<<json({{"pass",true},{"checks",checks},{"baseline",witness},{"candidateSelfPinRetainsAfterCallerOrWrapperExit",true},{"deliberatelyUnsoundTerminalAdapterCounterexamples",1},{"crossRegistryNumericTicketCollisionRejected",true},{"pendingCountCap",4},{"seededHistories",10000},{"realCppLateReadBufferWrites",true},{"actualWin32PipeOrCompletionAdapterImplemented",false},{"productionChanged",false},{"poolingImplemented",false},{"wholeProcessCpuOrCtrlCauseClaim",false},{"requiredIntegration","one process-wide budget; serialized access; nonthrowing backend issue; trustworthy terminal classification; keep weak reaper tickets across real shutdown"}}).dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
