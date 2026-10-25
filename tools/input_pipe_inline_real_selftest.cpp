// R43 same-process private Win32 pipes: inline trace, pending query trace,
// explicit Disconnect->Close, and a successful new epoch beside old pending I/O.
#include "input_pipe_owned_bridge.h"
#include "json.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using json=nlohmann::json;
namespace own=ymcc::testing::pipeOwnership;
namespace win=ymcc::testing::pipeWin32;
namespace bridge=ymcc::testing::pipeBridge;
using Hub=bridge::RetirementHub<win::Backend>;
using Endpoint=bridge::Endpoint<win::Backend>;
static unsigned checks=0,pairs=0;
static json cases=json::array();
static auto handles=std::make_shared<win::HandleStats>();
static auto r43ProcessBudget=std::make_shared<own::Budget>(4,4*1024*1024);
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(std::string(why)+" error="+std::to_string(GetLastError()));}
static json resultJson(const bridge::Result& r){return {{"ok",r.ok},{"error",r.error},{"transferred",r.transferred},{"byteCountKnown",r.byteCountKnown},{"terminalAtReturn",r.terminalAtReturn},{"retired",r.retired},{"ticket",r.ticket}};}
static json trace(const win::Summary& s){return {{"apiReturnedTrue",s.apiReturnedTrue},{"initialError",s.initialError},{"pendingAtIssue",s.pendingAtIssue},{"terminal",s.terminal},{"terminalFromApiReturn",s.terminalFromApiReturn},{"completionError",s.completionError},{"lastQueryReturnedTrue",s.lastQueryReturnedTrue},{"lastPollError",s.lastPollError},{"lastEventWait",s.lastEventWait},{"byteCountApplicable",s.byteCountApplicable},{"byteCountKnown",s.byteCountKnown},{"bytes",s.byteCountKnown?json(s.bytes):json(nullptr)},{"byteQueryAttempted",s.byteQueryAttempted},{"byteQueryReturnedTrue",s.byteQueryReturnedTrue},{"byteQueryError",s.byteQueryError},{"polls",s.polls},{"cancels",s.cancels},{"destroyed",s.destroyed},{"ownedPipe",s.pipeIdentity},{"ownedOverlapped",s.overlappedIdentity},{"ownedBuffer",s.bufferIdentity},{"ownedEvent",s.eventIdentity}};}
struct Pair {
 std::wstring name;win::Handle server,client;
 Pair(){name=L"\\\\.\\pipe\\YMCC-R43-inline-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++pairs);server=win::Handle(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,nullptr));check(server.valid(),"private server creation");}
 void connect(){client=win::Handle(CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));check(client.valid(),"private client creation");DWORD mode=PIPE_READMODE_MESSAGE;check(SetNamedPipeHandleState(client.get(),&mode,nullptr,nullptr)!=FALSE,"message mode");ULONG pid=0;check(GetNamedPipeClientProcessId(server.get(),&pid)!=FALSE&&pid==GetCurrentProcessId(),"client peer PID");pid=0;check(GetNamedPipeServerProcessId(client.get(),&pid)!=FALSE&&pid==GetCurrentProcessId(),"server peer PID");auto backend=std::make_shared<win::Backend>(server.get(),handles);auto hub=std::make_shared<Hub>();Endpoint endpoint(backend,r43ProcessBudget,hub);const auto r=endpoint.run(own::Kind::Connect,nullptr,0,1000);check(r.ok&&backend->summary(r.operationId).initialError==ERROR_PIPE_CONNECTED,"connect normalization");}
};
struct Io {bridge::Result result;win::Summary summary;};
static Io io(HANDLE pipe,own::Kind kind,std::vector<unsigned char>& data){auto backend=std::make_shared<win::Backend>(pipe,handles);auto hub=std::make_shared<Hub>();Endpoint endpoint(backend,r43ProcessBudget,hub);auto r=endpoint.run(kind,data.data(),static_cast<DWORD>(data.size()),1000);check(!r.retired,"normal private I/O unexpectedly retired");return {r,backend->summary(r.operationId)};}
static void inlineAndPartial(){
 Pair pair;pair.connect();std::vector<unsigned char> message(64,0x68),read(64,0xA7);
 auto wrote=io(pair.client.get(),own::Kind::Write,message);auto got=io(pair.server.get(),own::Kind::Read,read);
 check(wrote.result.ok&&got.result.ok&&read==message,"inline payload correctness");check(wrote.summary.apiReturnedTrue&&got.summary.apiReturnedTrue&&wrote.summary.terminalFromApiReturn&&got.summary.terminalFromApiReturn,"inline branch not observed");
 cases.push_back({{"case","real-inline-write-read"},{"actualWin32",true},{"write",trace(wrote.summary)},{"read",trace(got.summary)},{"payloadBytesVerified",message.size()}});
 std::vector<unsigned char> big(128,0x5E),half(64,0xA7),tail(64,0xA7);check(io(pair.client.get(),own::Kind::Write,big).result.ok,"partial setup write");auto first=io(pair.server.get(),own::Kind::Read,half);auto last=io(pair.server.get(),own::Kind::Read,tail);
 check(!first.result.ok&&first.result.error==ERROR_MORE_DATA&&first.summary.initialError==ERROR_MORE_DATA&&!first.summary.apiReturnedTrue&&!first.summary.terminalFromApiReturn,"partial branch lost failure");check(first.result.byteCountKnown&&first.result.transferred==64&&half==std::vector<unsigned char>(64,0x5E),"partial bytes lost");check(last.result.ok&&tail==half,"partial tail lost");
 cases.push_back({{"case","real-more-data-and-tail"},{"actualWin32",true},{"partialResult",resultJson(first.result)},{"partial",trace(first.summary)},{"tail",trace(last.summary)}});
}
static void actualPending(){
 Pair pair;pair.connect();auto backend=std::make_shared<win::Backend>(pair.server.get(),handles);using Registry=own::Registry<win::Backend>;Registry registry(backend,r43ProcessBudget);auto acquired=registry.acquire(own::Kind::Read,std::vector<unsigned char>(64));auto op=std::move(acquired.first);check(op&&op->issue()==own::Issue::Pending,"real read not pending");const auto issued=backend->summary(op->id());check(!issued.apiReturnedTrue&&issued.initialError==ERROR_IO_PENDING&&!issued.terminalFromApiReturn,"pending issue provenance");
 std::vector<unsigned char> message(64,0x72);check(io(pair.client.get(),own::Kind::Write,message).result.ok,"pending peer write");check(backend->waitEvent(op->id(),1000)==WAIT_OBJECT_0&&op->poll()==own::Poll::Terminal,"pending read completion");const auto complete=backend->summary(op->id());check(complete.lastQueryReturnedTrue&&!complete.terminalFromApiReturn&&complete.bytes==64&&op->bytes()==message,"pending completed bytes/provenance");op.reset();check(r43ProcessBudget->count==0,"pending r43ProcessBudget retained");cases.push_back({{"case","real-pending-read-completion"},{"actualWin32",true},{"issued",trace(issued)},{"completed",trace(complete)}});
}
class SelectiveCancel:public win::Backend {
 bool suppress_;
public:
 unsigned suppressed=0;
 SelectiveCancel(HANDLE h,bool suppress):win::Backend(h,handles),suppress_(suppress){}
 void cancel(std::uint64_t id)noexcept{if(suppress_)++suppressed;else win::Backend::cancel(id);}
};
static void coexistAndDisconnect(bool write){
 using B=SelectiveCancel;using BHub=bridge::RetirementHub<B>;using BE=bridge::Endpoint<B>;
 Pair oldPair;oldPair.connect();auto hub=std::make_shared<BHub>();std::weak_ptr<B> weak;const DWORD size=write?1024*1024:64;std::vector<unsigned char> oldCaller(size,write?0x39:0xA7);bridge::Result oldResult;win::Summary oldIssued;
 {auto backend=std::make_shared<B>(oldPair.server.get(),true);weak=backend;BE endpoint(backend,r43ProcessBudget,hub);oldResult=endpoint.run(write?own::Kind::Write:own::Kind::Read,oldCaller.data(),size,0,0);oldIssued=backend->summary(oldResult.operationId);check(backend->suppressed==1,"cancel suppression not exercised");}
 check(oldResult.retired&&oldResult.error==WAIT_TIMEOUT&&r43ProcessBudget->count==1&&hub->tracked()==1&&!weak.expired(),"old pending lifetime not held");
 const auto frozenOld=resultJson(oldResult);if(write)std::fill(oldCaller.begin(),oldCaller.end(),static_cast<unsigned char>(0xA7));
 Pair nextPair;nextPair.connect();std::vector<unsigned char> nextMessage(64,0x4B),nextCaller(64,0xA7);check(io(nextPair.client.get(),own::Kind::Write,nextMessage).result.ok,"new epoch setup");bridge::Result nextResult;win::Summary nextSummary;
 {auto next=std::make_shared<B>(nextPair.server.get(),false);BE endpoint(next,r43ProcessBudget,hub);nextResult=endpoint.run(own::Kind::Read,nextCaller.data(),64,1000);nextSummary=next->summary(nextResult.operationId);}
 check(nextResult.ok&&nextResult.ticket>oldResult.ticket&&nextCaller==nextMessage&&r43ProcessBudget->count==1&&hub->tracked()==1,"new epoch could not succeed beside old pending with room");check(oldIssued.pipeIdentity!=nextSummary.pipeIdentity&&oldIssued.overlappedIdentity!=nextSummary.overlappedIdentity&&oldIssued.bufferIdentity!=nextSummary.bufferIdentity,"old/new storage identities overlap");
 const auto frozenNew=resultJson(nextResult);const BOOL disconnected=DisconnectNamedPipe(oldPair.server.get());const DWORD disconnectError=disconnected?ERROR_SUCCESS:GetLastError();check(disconnected!=FALSE,"explicit disconnect failed");oldPair.server.reset();
 auto sameNameCreate=[&](){HANDLE h=CreateNamedPipeW(oldPair.name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,nullptr);const DWORD error=h==INVALID_HANDLE_VALUE?GetLastError():ERROR_SUCCESS;return std::make_pair(win::Handle(h),error);};
 auto whileHeld=sameNameCreate();check(!whileHeld.first.valid(),"one-instance same-name pipe recreated while old server lease held");const DWORD sameNameHeldError=whileHeld.second;
 auto retained=weak.lock();check(bool(retained),"old backend expired before terminal observation");const auto deadline=GetTickCount64()+2000;while(hub->tracked()&&GetTickCount64()<deadline){(void)hub->sweep();if(hub->tracked())Sleep(1);}
 const auto terminal=retained->summary(oldResult.operationId);check(hub->tracked()==0&&terminal.terminal&&terminal.destroyed&&r43ProcessBudget->count==0,"disconnect did not yield reaped terminal");check(terminal.completionError!=ERROR_SUCCESS,"disconnected pending I/O appeared successful");
 check(resultJson(oldResult)==frozenOld&&resultJson(nextResult)==frozenNew&&nextCaller==nextMessage&&oldCaller==std::vector<unsigned char>(size,0xA7),"old disconnect completion changed request/new epoch/caller");retained.reset();check(weak.expired(),"retired backend retained after observer release");
 auto afterReap=sameNameCreate();const bool sameNameAcceptedWithOldClient=afterReap.first.valid();const DWORD sameNameAfterReapError=afterReap.second;afterReap.first.reset();oldPair.client.reset();auto afterAllClosed=sameNameCreate();check(afterAllClosed.first.valid(),"same-name creation failed after old server/client leases closed");afterAllClosed.first.reset();
 cases.push_back({{"case",write?"real-disconnect-pending-write-beside-new-epoch":"real-disconnect-pending-read-beside-new-epoch"},{"actualWin32",true},{"cancelDeliberatelySuppressed",true},{"realKernelCancelFailureInduced",false},{"explicitDisconnect",true},{"disconnectError",disconnectError},{"originalServerHandleClosedAfterDisconnect",true},{"oldBeforeDisconnect",trace(oldIssued)},{"oldAfterReap",trace(terminal)},{"oldResult",frozenOld},{"newAcceptedWhileOldOutstanding",true},{"newEpoch",trace(nextSummary)},{"newResult",frozenNew},{"oldNewCallerResultsIsolated",true},{"sameNameCreateWhileOldLeaseHeldSucceeded",false},{"sameNameHeldError",sameNameHeldError},{"sameNameAcceptedAfterReapWithOldClientStillOpen",sameNameAcceptedWithOldClient},{"sameNameAfterReapError",sameNameAfterReapError},{"sameNameAcceptedAfterAllOldLeasesClosed",true}});
}
int main(){try{DWORD before=0,after=0;check(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"initial process handle count");inlineAndPartial();actualPending();coexistAndDisconnect(false);coexistAndDisconnect(true);check(r43ProcessBudget->count==0&&r43ProcessBudget->bytes==0&&handles->live==0&&handles->created==handles->closed&&handles->closeFailures==0,"resource accounting retained");check(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&after==before,"process handle leak");std::cout<<json({{"pass",true},{"checks",checks},{"cases",cases},{"privatePipePairs",pairs},{"adapterHandlesCreated",handles->created},{"adapterHandlesClosed",handles->closed},{"processHandlesBefore",before},{"processHandlesAfter",after},{"logicalCountAfter",r43ProcessBudget->count},{"logicalBytesAfter",r43ProcessBudget->bytes},{"realKernelFaultInduced",false},{"sameProcessPrivatePipesOnly",true},{"productionIntegrated",false},{"noCPUOrCtrlCauseClaim",true}}).dump(2)<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
