// R43 controls execute the ACTUAL Backend implementation with API-boundary
// stand-ins. No I/O is submitted to the kernel. Unnamed handle/event ownership
// is real, so destructor and count checks still apply. Not a Windows fault.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
namespace scripted {
struct Frame {
 BOOL issueOk=TRUE,queryOk=TRUE;DWORD issueError=ERROR_SUCCESS,queryError=ERROR_SUCCESS,queryBytes=32;
 DWORD eventState=WAIT_TIMEOUT,firstWait=WAIT_TIMEOUT;
 unsigned issueCalls=0,queryCalls=0,queryEventWaits=0,requestWaits=0,cancelCalls=0;
};
static Frame* frame=nullptr;
static BOOL issue(void* data,DWORD size){++frame->issueCalls;if(data&&frame->issueOk)std::memset(data,0x62,size);SetLastError(frame->issueError);return frame->issueOk;}
static BOOL read(HANDLE,void* data,DWORD size,DWORD*,OVERLAPPED*){return issue(data,size);}
static BOOL write(HANDLE,const void*,DWORD size,DWORD*,OVERLAPPED*){return issue(nullptr,size);}
static BOOL connect(HANDLE,OVERLAPPED*){return issue(nullptr,0);}
static BOOL query(HANDLE,OVERLAPPED*,DWORD* n,BOOL){++frame->queryCalls;*n=frame->queryBytes;SetLastError(frame->queryError);return frame->queryOk;}
static DWORD wait(HANDLE,DWORD ms){if(ms==0){++frame->queryEventWaits;return frame->eventState;}++frame->requestWaits;SetLastError(ERROR_INVALID_HANDLE);return frame->firstWait;}
static BOOL cancel(HANDLE,OVERLAPPED*){++frame->cancelCalls;SetLastError(ERROR_NOT_FOUND);return FALSE;}
}
#define ReadFile scripted::read
#define WriteFile scripted::write
#define ConnectNamedPipe scripted::connect
#define GetOverlappedResult scripted::query
#define WaitForSingleObject scripted::wait
#define CancelIoEx scripted::cancel
#include "input_pipe_owned_bridge.h"
#undef ReadFile
#undef WriteFile
#undef ConnectNamedPipe
#undef GetOverlappedResult
#undef WaitForSingleObject
#undef CancelIoEx
using json=nlohmann::json;
namespace own=ymcc::testing::pipeOwnership;
namespace win=ymcc::testing::pipeWin32;
namespace bridge=ymcc::testing::pipeBridge;
static unsigned checks=0,changedCases=0;
static json rows=json::array();
static auto handles=std::make_shared<win::HandleStats>();
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static json trace(const win::Summary& s){return {{"apiReturnedTrue",s.apiReturnedTrue},{"initialError",s.initialError},{"terminal",s.terminal},{"completionError",s.completionError},{"lastPollError",s.lastPollError},{"eventState",s.lastEventWait},{"byteCountKnown",s.byteCountKnown},{"bytes",s.byteCountKnown?json(s.bytes):json(nullptr)},{"byteQueryAttempted",s.byteQueryAttempted},{"byteQueryReturnedTrue",s.byteQueryReturnedTrue},{"byteQueryError",s.byteQueryError},{"polls",s.polls},{"cancels",s.cancels},{"destroyed",s.destroyed}
#ifdef R43_CANDIDATE
 ,{"lastQueryReturnedTrue",s.lastQueryReturnedTrue},{"terminalFromApiReturn",s.terminalFromApiReturn}
#endif
 };}
static void runCase(const char* name,own::Kind kind,scripted::Frame plan,bool trueIssueWithFailedQuery){
 scripted::frame=&plan;win::Handle source(CreateEventW(nullptr,TRUE,FALSE,nullptr));check(source.valid(),"control source event failed");
 auto budget=std::make_shared<own::Budget>(4,256);auto hub=std::make_shared<bridge::RetirementHub<win::Backend>>();auto backend=std::make_shared<win::Backend>(source.get(),handles);
 std::vector<unsigned char> caller(64,0xA7);bridge::Result result;
 {bridge::Endpoint<win::Backend> endpoint(backend,budget,hub);result=endpoint.run(kind,kind==own::Kind::Connect?nullptr:caller.data(),kind==own::Kind::Connect?0:64,7,3);}
 const auto beforeCleanup=backend->summary(result.operationId);const auto requestWaits=plan.requestWaits,cancels=plan.cancelCalls,queries=plan.queryCalls;const bool bytesKnown=result.byteCountKnown;const auto frozenError=result.error;const auto frozenOk=result.ok;
 if(trueIssueWithFailedQuery){
  ++changedCases;
#ifdef R43_CANDIDATE
  check(result.ok&&result.error==0&&result.terminalAtReturn&&!result.retired,"inline TRUE lost its completion or success proof");
  check(requestWaits==0&&cancels==0&&budget->count==0,"inline completion entered request wait/cancel/retirement");
  check(beforeCleanup.terminalFromApiReturn&&!beforeCleanup.lastQueryReturnedTrue,"inline/query provenance missing");
  check(!bytesKnown&&result.transferred==0,"failed query fabricated a successful byte count");
  check(caller==std::vector<unsigned char>(64,0xA7),"failed byte query copied ungrounded inline read data");
#else
  check(!result.ok,"baseline did not reproduce A5 for contradictory query");
  // Depending on the query mapping, old code either retires as a timeout or
  // publishes the query error, overriding the original TRUE. Record both.
  check(result.error==WAIT_TIMEOUT||result.error==plan.queryError,"baseline A5 outcome unexpected");
#endif
 }else{
  if(plan.issueOk){check(result.ok&&result.error==0&&!result.retired,"normal inline result regressed");if(kind!=own::Kind::Connect)check(bytesKnown==(plan.queryBytes<=64),"normal count validity regressed");}
  else if(plan.issueError==ERROR_PIPE_CONNECTED)check(result.ok&&result.error==0&&queries==0,"already-connected normalization regressed");
  else if(plan.issueError==ERROR_MORE_DATA)check(!result.ok&&result.error==ERROR_MORE_DATA&&!result.retired,"immediate partial message lost original error");
  else if(plan.issueError==ERROR_IO_PENDING){
   if(plan.queryOk)check(result.ok&&result.error==0&&!result.retired,"pending completion regressed");
   else check(!result.ok&&result.retired&&budget->count==1,"unproven pending query released ownership");
  }
 }
 if(kind==own::Kind::Connect)check(!bytesKnown&&result.transferred==0,"Connect exposed undefined bytes");
 const bool retainedAtReturn=result.retired;
 if(retainedAtReturn){plan.queryOk=TRUE;plan.queryError=ERROR_SUCCESS;plan.queryBytes=32;plan.eventState=WAIT_OBJECT_0;const auto swept=hub->sweep();check(swept.terminal==1,"scripted retired state did not reap");}
 check(result.error==frozenError&&result.ok==frozenOk,"maintenance changed old request result");check(budget->count==0&&budget->bytes==0&&hub->tracked()==0,"control budget/retirement leaked");check(backend->summary(result.operationId).destroyed,"terminal operation not destroyed");
 rows.push_back({{"case",name},{"kind",static_cast<int>(kind)},{"APIIsSynthetic",true},{"originalIssueTrue",plan.issueOk!=FALSE},{"queryAtRequest",trace(beforeCleanup)},{"requestResult",{{"ok",frozenOk},{"error",frozenError},{"bytesKnown",bytesKnown},{"transferred",result.transferred},{"retired",retainedAtReturn}}},{"requestWaits",requestWaits},{"cancelCalls",cancels},{"queryCalls",queries},{"A5Control",trueIssueWithFailedQuery}});
}
int main(){try{DWORD before=0,after=0;check(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"initial handles failed");
 for(auto kind:{own::Kind::Read,own::Kind::Write,own::Kind::Connect}){
  for(auto error:{ERROR_IO_INCOMPLETE,ERROR_INVALID_HANDLE,ERROR_GEN_FAILURE,ERROR_OPERATION_ABORTED,ERROR_BROKEN_PIPE,ERROR_MORE_DATA}){
   scripted::Frame plan;plan.queryOk=FALSE;plan.queryError=error;plan.eventState=(error==ERROR_BROKEN_PIPE||error==ERROR_MORE_DATA)?WAIT_OBJECT_0:WAIT_TIMEOUT;
   runCase("inline-true-query-failed",kind,plan,true);
  }
  scripted::Frame ok;runCase("inline-success-query-success",kind,ok,false);
  if(kind!=own::Kind::Connect){scripted::Frame over;over.queryBytes=65;runCase("inline-success-invalid-count",kind,over,false);}
  scripted::Frame pending;pending.issueOk=FALSE;pending.issueError=ERROR_IO_PENDING;pending.firstWait=WAIT_OBJECT_0;pending.eventState=WAIT_OBJECT_0;runCase("pending-query-success",kind,pending,false);
  pending.queryOk=FALSE;pending.queryError=ERROR_INVALID_HANDLE;runCase("pending-unknown-still-owned",kind,pending,false);
 }
 scripted::Frame partial;partial.issueOk=FALSE;partial.issueError=ERROR_MORE_DATA;partial.queryOk=FALSE;partial.queryError=ERROR_MORE_DATA;partial.eventState=WAIT_OBJECT_0;runCase("immediate-partial-read",own::Kind::Read,partial,false);
 partial.queryError=ERROR_INVALID_HANDLE;runCase("immediate-partial-read-count-unavailable",own::Kind::Read,partial,false);
 scripted::Frame connected;connected.issueOk=FALSE;connected.issueError=ERROR_PIPE_CONNECTED;runCase("already-connected",own::Kind::Connect,connected,false);
 check(handles->live==0&&handles->created==handles->closed&&handles->closeFailures==0,"adapter controls leaked handles");check(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&after==before,"process control handles leaked");
 std::cout<<json({{"pass",true},{"checks",checks},{"cases",rows},{"A5Controls",changedCases},{"apiCallsAreSynthetic",true},{"realKernelIOIssued",false},{"realKernelFaultInduced",false},{"adapterHandlesCreated",handles->created},{"adapterHandlesClosed",handles->closed},{"processHandlesBefore",before},{"processHandlesAfter",after},
#ifdef R43_CANDIDATE
 {"variant","candidate"},{"baselineDefectExpected",false}
#else
 {"variant","baseline"},{"baselineDefectExpected",true}
#endif
 }).dump(2)<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
