// R41 private same-process named pipes. No product/Host/Steam/device input calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include "json.hpp"
#include "input_pipe_owned_win32_backend.h"
using json=nlohmann::json;
namespace win=ymcc::testing::pipeWin32;
namespace own=ymcc::testing::pipeOwnership;
using Registry=own::Registry<win::Backend,4>;
using Operation=Registry::Operation;
static unsigned checks=0,pipeOrdinal=0;
static json cases=json::array();
static std::shared_ptr<win::HandleStats> handles=std::make_shared<win::HandleStats>();
static std::shared_ptr<own::Budget> r41ProcessBudget=std::make_shared<own::Budget>(4,4*1024*1024);
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(std::string(why)+" Win32Error="+std::to_string(GetLastError()));}
struct Pair {
    std::wstring name;
    win::Handle server,client;
    Pair(){
        name=L"\\\\.\\pipe\\YMCC-R41-own-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++pipeOrdinal);
        server=win::Handle(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,nullptr));
        check(server.valid(),"private server creation");
    }
    void openClient(){
        client=win::Handle(CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));
        check(client.valid(),"private client open");DWORD mode=PIPE_READMODE_MESSAGE;
        check(SetNamedPipeHandleState(client.get(),&mode,nullptr,nullptr)!=FALSE,"client message mode");
        ULONG peer=0;check(GetNamedPipeClientProcessId(server.get(),&peer)!=FALSE&&peer==GetCurrentProcessId(),"server peer PID mismatch");
        peer=0;check(GetNamedPipeServerProcessId(client.get(),&peer)!=FALSE&&peer==GetCurrentProcessId(),"client peer PID mismatch");
    }
    void connected(){
        openClient();auto backend=std::make_shared<win::Backend>(server.get(),handles);Registry registry(backend,r41ProcessBudget);
        auto [operation,ticket]=registry.acquire(own::Kind::Connect,{});check(bool(operation),"connect allocation");
        check(operation->issue()==own::Issue::Completed,"already connected must complete");
        const auto summary=backend->summary(operation->id());check(summary.initialError==ERROR_PIPE_CONNECTED&&summary.completionError==0,"already-connected error semantics");
    }
};
static json summaryJson(const win::Summary& s){return {{"apiReturnedTrue",s.apiReturnedTrue},{"initialError",s.initialError},{"pendingAtIssue",s.pendingAtIssue},
    {"terminal",s.terminal},{"completionError",s.completionError},{"bytes",s.byteCountKnown?json(s.bytes):json(nullptr)},{"byteCountApplicable",s.byteCountApplicable},{"byteCountKnown",s.byteCountKnown},{"byteQueryAttempted",s.byteQueryAttempted},{"byteQueryReturnedTrue",s.byteQueryReturnedTrue},{"byteQueryError",s.byteQueryError},{"polls",s.polls},{"cancels",s.cancels},
    {"cancelReturned",s.cancelReturned!=FALSE},{"cancelError",s.cancelError},{"destroyed",s.destroyed}};}
static void finish(const std::shared_ptr<Operation>& operation,const std::shared_ptr<win::Backend>& backend){
    if(operation->phase()==own::Phase::Terminal)return;
    const DWORD wait=backend->waitEvent(operation->id(),2000);
    const DWORD initialWaitError=wait==WAIT_FAILED?GetLastError():wait;
    const auto result=operation->poll();
    if(result!=own::Poll::Terminal){
        operation->requestCancel();(void)backend->waitEvent(operation->id(),2000);(void)operation->poll();
        throw std::runtime_error("private operation incomplete after bounded wait:"+std::to_string(initialWaitError));
    }
    check(operation->phase()==own::Phase::Terminal,"event/query must produce terminal proof");
}
static std::vector<unsigned char> perform(HANDLE pipe,own::Kind kind,const std::vector<unsigned char>& payload){
    auto backend=std::make_shared<win::Backend>(pipe,handles);Registry registry(backend,r41ProcessBudget);auto [op,ticket]=registry.acquire(kind,payload);check(bool(op),"peer operation allocation");
    (void)op->issue();finish(op,backend);const auto summary=backend->summary(op->id());check(summary.completionError==0,"peer I/O status");check(summary.byteCountApplicable&&summary.byteCountKnown,"successful peer bytes unavailable");check(summary.bytes<=op->size(),"peer transfer exceeds buffer");
    return std::vector<unsigned char>(op->bytes().begin(),op->bytes().begin()+summary.bytes);
}
static void record(const char* name,const win::Summary& summary){cases.push_back({{"case",name},{"actualWin32",true},{"sameProcessPeers",true},{"result",summaryJson(summary)}});}
static void clean(){check(r41ProcessBudget->count==0&&r41ProcessBudget->bytes==0,"pending operation r41ProcessBudget leaked");check(handles->live==0&&handles->closeFailures==0,"owned handles leaked/failed to close");}
static void pendingConnect(){
 Pair pair;auto backend=std::make_shared<win::Backend>(pair.server.get(),handles);Registry registry(backend,r41ProcessBudget);auto [op,ticket]=registry.acquire(own::Kind::Connect,{});
 check(bool(op),"pending connect allocation");const auto id=op->id();check(op->issue()==own::Issue::Pending,"connect should await private peer");pair.openClient();finish(op,backend);
 auto result=backend->summary(id);check(result.terminal&&result.completionError==0&&result.pendingAtIssue,"pending connect completion");check(!result.byteCountApplicable&&!result.byteCountKnown&&result.bytes==0,"Connect bytes must be normalized, not published");record("pending-connect",result);
}
static void bufferedRead(){
 Pair pair;pair.connected();std::vector<unsigned char> payload(64,0x31);(void)perform(pair.client.get(),own::Kind::Write,payload);
 auto backend=std::make_shared<win::Backend>(pair.server.get(),handles);Registry registry(backend,r41ProcessBudget);auto [op,ticket]=registry.acquire(own::Kind::Read,std::vector<unsigned char>(64));
 (void)op->issue();finish(op,backend);auto result=backend->summary(op->id());check(result.completionError==0&&result.bytes==64&&op->bytes()==payload,"buffered read contents");record("data-already-buffered-read",result);
}
static void detachedDelayedRead(){
 Pair pair;pair.connected();Registry::Ticket ticket;std::weak_ptr<win::Backend> weakBackend;uint64_t id=0;win::Summary before;
 {
    auto backend=std::make_shared<win::Backend>(pair.server.get(),handles);weakBackend=backend;Registry registry(backend,r41ProcessBudget);auto acquired=registry.acquire(own::Kind::Read,std::vector<unsigned char>(64));
    check(bool(acquired.first),"detached read allocation");id=acquired.first->id();ticket=acquired.second;
    check(acquired.first->issue()==own::Issue::Pending,"empty pipe read must be pending");before=backend->summary(id);acquired.first->retire();
 }
 pair.server.reset();check(!ticket.weak.expired()&&!weakBackend.expired()&&r41ProcessBudget->count==1,"pending owner did not outlive caller/registry/original pipe");
 std::vector<unsigned char> payload(64,0x7B);(void)perform(pair.client.get(),own::Kind::Write,payload);
 auto owner=ticket.weak.lock();auto backend=weakBackend.lock();check(bool(owner)&&bool(backend),"detached read disappeared");finish(owner,backend);
 const auto result=backend->summary(id);check(result.completionError==0&&result.bytes==64&&owner->bytes()==payload,"late read data/byte result");
 check(result.pipeIdentity==before.pipeIdentity&&result.overlappedIdentity==before.overlappedIdentity&&result.bufferIdentity==before.bufferIdentity,"pending Win32 identity changed");
 owner.reset();check(ticket.weak.expired()&&r41ProcessBudget->count==0&&backend->summary(id).destroyed,"detached read not reclaimed");backend.reset();check(weakBackend.expired(),"backend not reclaimed after final operation");record("read-after-caller-registry-and-source-handle-release",result);
}
static void cancelRead(){
 Pair pair;pair.connected();auto backend=std::make_shared<win::Backend>(pair.server.get(),handles);Registry registry(backend,r41ProcessBudget);auto [op,ticket]=registry.acquire(own::Kind::Read,std::vector<unsigned char>(64));
 check(op->issue()==own::Issue::Pending,"cancel read must start pending");op->requestCancel();op->retire();check(op->phase()==own::Phase::Retired&&r41ProcessBudget->count==1,"cancel return prematurely declared terminal");finish(op,backend);
 const auto result=backend->summary(op->id());check(result.completionError==ERROR_OPERATION_ABORTED&&result.cancels==1,"real canceled read status");record("pending-read-real-cancel",result);
}
static void cancelAfterCallerRelease(){
 Pair pair;pair.connected();Registry::Ticket ticket;std::weak_ptr<win::Backend> weakBackend;uint64_t id=0;
 {
    auto backend=std::make_shared<win::Backend>(pair.server.get(),handles);weakBackend=backend;Registry registry(backend,r41ProcessBudget);
    auto acquired=registry.acquire(own::Kind::Read,std::vector<unsigned char>(64));ticket=acquired.second;id=acquired.first->id();
    check(acquired.first->issue()==own::Issue::Pending,"detached cancel read must start pending");acquired.first->requestCancel();acquired.first->retire();
 }
 pair.server.reset();check(!ticket.weak.expired()&&!weakBackend.expired()&&r41ProcessBudget->count==1,"cancel return released unobserved owner");
 auto backend=weakBackend.lock();auto owner=ticket.weak.lock();finish(owner,backend);const auto result=backend->summary(id);
 check(result.completionError==ERROR_OPERATION_ABORTED&&result.cancels==1&&!result.byteCountKnown,"detached cancel result semantics");
 owner.reset();check(ticket.weak.expired()&&backend->summary(id).destroyed&&r41ProcessBudget->count==0,"detached canceled owner not reclaimed");
 backend.reset();check(weakBackend.expired(),"detached canceled backend not reclaimed");record("canceled-read-after-last-caller-and-source-handle-release",result);
}
static void cancelAfterCompletion(){
 Pair pair;pair.connected();auto backend=std::make_shared<win::Backend>(pair.server.get(),handles);Registry registry(backend,r41ProcessBudget);auto [op,ticket]=registry.acquire(own::Kind::Read,std::vector<unsigned char>(32));
 check(op->issue()==own::Issue::Pending,"race read must begin pending");std::vector<unsigned char> payload(32,0x28);(void)perform(pair.client.get(),own::Kind::Write,payload);
 check(backend->waitEvent(op->id(),2000)==WAIT_OBJECT_0,"completed read event before cancellation");op->requestCancel();check(op->phase()==own::Phase::Pending,"cancellation became terminal observation");finish(op,backend);
 const auto result=backend->summary(op->id());check(result.completionError==0&&result.bytes==32&&op->bytes()==payload,"completed read changed into canceled result");
 check(!result.cancelReturned&&result.cancelError==ERROR_NOT_FOUND,"completed operation should not be found by cancellation");record("cancel-after-already-completed-read",result);
}
static void peerClosed(){
 Pair pair;pair.connected();auto backend=std::make_shared<win::Backend>(pair.server.get(),handles);Registry registry(backend,r41ProcessBudget);auto [op,ticket]=registry.acquire(own::Kind::Read,std::vector<unsigned char>(64));
 check(op->issue()==own::Issue::Pending,"disconnect read must begin pending");pair.client.reset();finish(op,backend);auto result=backend->summary(op->id());
 check(result.completionError==ERROR_BROKEN_PIPE||result.completionError==ERROR_PIPE_NOT_CONNECTED||result.completionError==ERROR_NO_DATA,"closed peer terminal status unrecognized");record("pending-read-peer-close",result);
}
static void pendingWrite(){
 Pair pair;pair.connected();auto backend=std::make_shared<win::Backend>(pair.server.get(),handles);Registry::Ticket ticket;uint64_t id=0;win::Summary before;
 std::vector<unsigned char> payload(1024*1024);for(size_t n=0;n<payload.size();++n)payload[n]=static_cast<unsigned char>(n%251);
 {Registry registry(backend,r41ProcessBudget);auto acquired=registry.acquire(own::Kind::Write,payload);id=acquired.first->id();ticket=acquired.second;check(acquired.first->issue()==own::Issue::Pending,"large write should encounter backpressure");before=backend->summary(id);acquired.first->retire();}
 payload.clear();pair.server.reset();check(!ticket.weak.expired()&&r41ProcessBudget->count==1,"pending write storage lost after caller exits");
 const auto received=perform(pair.client.get(),own::Kind::Read,std::vector<unsigned char>(1024*1024));check(received.size()==1024*1024,"large write peer transfer length");
 for(size_t n=0;n<received.size();++n)check(received[n]==static_cast<unsigned char>(n%251),"large pending write payload corrupted");
 auto op=ticket.weak.lock();finish(op,backend);const auto result=backend->summary(id);check(result.bytes==1024*1024&&result.completionError==0,"large write result");
 check(result.pipeIdentity==before.pipeIdentity&&result.overlappedIdentity==before.overlappedIdentity&&result.bufferIdentity==before.bufferIdentity,"large write pending identity changed");
 op.reset();check(ticket.weak.expired()&&backend->summary(id).destroyed,"large write not reclaimed");record("backpressured-write-after-caller-release",result);
}
static void partialMessage(){
 Pair pair;pair.connected();std::vector<unsigned char> payload(128,0x3D);(void)perform(pair.client.get(),own::Kind::Write,payload);
 auto backend=std::make_shared<win::Backend>(pair.server.get(),handles);Registry registry(backend,r41ProcessBudget);auto [op,ticket]=registry.acquire(own::Kind::Read,std::vector<unsigned char>(64));
 const auto issue=op->issue();finish(op,backend);const auto result=backend->summary(op->id());
 check(result.completionError==ERROR_MORE_DATA&&result.byteCountKnown&&result.byteQueryAttempted&&result.byteQueryError==ERROR_MORE_DATA&&result.bytes==64&&op->bytes()==std::vector<unsigned char>(64,0x3D),"partial message status/bytes/query proof");
 check(issue==own::Issue::Rejected||issue==own::Issue::Pending,"partial message classification");record("message-larger-than-read-buffer",result);
 const auto tail=perform(pair.server.get(),own::Kind::Read,std::vector<unsigned char>(64));check(tail==std::vector<unsigned char>(64,0x3D),"partial message tail");
}
static void immediateReadDenied(){
 Pair pair;pair.connected();HANDLE limited=nullptr;check(DuplicateHandle(GetCurrentProcess(),pair.server.get(),GetCurrentProcess(),&limited,GENERIC_WRITE,FALSE,0)!=FALSE,"limited-access handle duplicate");win::Handle limitedOwner(limited);
 auto backend=std::make_shared<win::Backend>(limited,handles);Registry registry(backend,r41ProcessBudget);auto [op,ticket]=registry.acquire(own::Kind::Read,std::vector<unsigned char>(64));
 check(op->issue()==own::Issue::Rejected&&op->phase()==own::Phase::Terminal,"read denial must be immediate terminal failure");const auto result=backend->summary(op->id());check(result.initialError==ERROR_ACCESS_DENIED&&result.completionError==ERROR_ACCESS_DENIED,"read denial raw error");record("read-through-write-only-handle",result);
}
static void classificationControls(){
 const auto connect=win::classifyBytes(own::Kind::Connect,true,0,123,456);check(!connect.applicable&&!connect.known&&connect.count==0,"undefined Connect bytes leaked");
 for(auto error:{ERROR_IO_INCOMPLETE,ERROR_INVALID_HANDLE,ERROR_GEN_FAILURE,ERROR_OPERATION_ABORTED}){
    auto unknown=win::classifyBytes(own::Kind::Read,false,error,64,64);check(unknown.applicable&&!unknown.known&&unknown.count==0,"failed query published fabricated bytes");
 }
 const auto partial=win::classifyBytes(own::Kind::Read,false,ERROR_MORE_DATA,32,64);check(partial.known&&partial.count==32,"completed partial message bytes lost");
 check(!win::classifyBytes(own::Kind::Read,false,ERROR_MORE_DATA,65,64).known,"out-of-range partial count accepted");
 check(!win::classifyBytes(own::Kind::Write,false,ERROR_MORE_DATA,32,64).known,"read-only partial count rule applied to write");
 for(auto code:{ERROR_INVALID_HANDLE,ERROR_INVALID_PARAMETER,ERROR_GEN_FAILURE,ERROR_ACCESS_DENIED,ERROR_SUCCESS}){
    check(win::classify(false,code,WAIT_OBJECT_0)==own::Poll::Unavailable,"query unknown/infrastructure error became terminal");
    check(win::classify(false,code,WAIT_TIMEOUT)==own::Poll::Unavailable,"unsignaled unknown query became terminal");
 }
 check(win::classify(false,ERROR_IO_INCOMPLETE,WAIT_OBJECT_0)==own::Poll::Pending,"incomplete status ignored");
 check(win::classify(false,ERROR_OPERATION_ABORTED,WAIT_TIMEOUT)==own::Poll::Terminal,"actual aborted result not terminal");
 check(win::classify(false,ERROR_BROKEN_PIPE,WAIT_TIMEOUT)==own::Poll::Unavailable,"nonterminal event proof missing");
 check(win::classify(false,ERROR_BROKEN_PIPE,WAIT_OBJECT_0)==own::Poll::Terminal,"known failed completion not terminal");
 cases.push_back({{"case","classification-unknown-error-controls"},{"actualWin32",false},{"scope","pure result-mapping controls; no real handle invalidation"}});
}
int main(){try{
 DWORD before=0,after=0;check(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"initial process handle count");
 pendingConnect();clean();bufferedRead();clean();detachedDelayedRead();clean();cancelRead();clean();cancelAfterCallerRelease();clean();cancelAfterCompletion();clean();peerClosed();clean();pendingWrite();clean();partialMessage();clean();immediateReadDenied();clean();classificationControls();
 check(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE,"final process handle count");check(after==before,"private test process leaked handles");
 std::cout<<json({{"pass",true},{"checks",checks},{"cases",cases},{"privatePipePairs",pipeOrdinal},{"ownedHandlesCreated",handles->created},{"ownedHandlesClosed",handles->closed},{"ownedHandleCloseFailures",handles->closeFailures},{"processHandlesBefore",before},{"processHandlesAfter",after},{"logicalPendingCountAfter",r41ProcessBudget->count},{"logicalPayloadBytesAfter",r41ProcessBudget->bytes},
 {"realSameProcessWin32PipesUsed",true},{"productsOrInputDevicesUsed",false},{"productionChanged",false},{"slowUncancelableKernelFaultForced",false},{"CPUOrCtrlRootCauseClaim",false},{"scope","isolated adapter correctness; real normal/cancel/broken/partial/backpressure paths, unknown mapping is synthetic control; no production exit integration or pooling"}}).dump(2)<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
