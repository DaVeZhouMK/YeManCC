// R44 actual production Runtime API-boundary controls, never kernel pipe I/O.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
#include "input_pipe_test_allocation_fault.h"
namespace script {
struct Plan {
 BOOL issued=TRUE,query=TRUE;DWORD initial=0,queryError=0,n=32,eventState=WAIT_OBJECT_0,firstWait=WAIT_OBJECT_0,waitError=ERROR_INVALID_HANDLE;
 DWORD eventError=0,duplicateError=0;unsigned issuedCalls=0,queryCalls=0,cancels=0,disconnects=0,createdEvents=0;
};
static Plan* p=nullptr;
static BOOL issue(void* data,DWORD size){++p->issuedCalls;if(p->issued&&data)std::memset(data,0x69,size);SetLastError(p->initial);return p->issued;}
static BOOL read(HANDLE,void* data,DWORD n,DWORD*,OVERLAPPED*){return issue(data,n);}
static BOOL write(HANDLE,const void*,DWORD n,DWORD*,OVERLAPPED*){return issue(nullptr,n);}
static BOOL connect(HANDLE,OVERLAPPED*){return issue(nullptr,0);}
static BOOL query(HANDLE,OVERLAPPED*,DWORD* n,BOOL){++p->queryCalls;*n=p->n;SetLastError(p->queryError);return p->query;}
static DWORD wait(HANDLE,DWORD ms){SetLastError(ms==17?p->waitError:ERROR_INVALID_PARAMETER);return ms==0?p->eventState:ms==17?p->firstWait:WAIT_TIMEOUT;}
static BOOL cancel(HANDLE,OVERLAPPED*){++p->cancels;SetLastError(ERROR_NOT_FOUND);return FALSE;}
static BOOL disconnect(HANDLE){++p->disconnects;return TRUE;}
static HANDLE event(LPSECURITY_ATTRIBUTES a,BOOL b,BOOL c,LPCWSTR d){if(p->eventError){SetLastError(p->eventError);return nullptr;}++p->createdEvents;return CreateEventW(a,b,c,d);}
static BOOL duplicate(HANDLE a,HANDLE b,HANDLE c,HANDLE* d,DWORD e,BOOL f,DWORD g){if(p->duplicateError){SetLastError(p->duplicateError);return FALSE;}return DuplicateHandle(a,b,c,d,e,f,g);}
}
#define ReadFile script::read
#define WriteFile script::write
#define ConnectNamedPipe script::connect
#define GetOverlappedResult script::query
#define WaitForSingleObject script::wait
#define CancelIoEx script::cancel
#define DisconnectNamedPipe script::disconnect
#define CreateEventW script::event
#define DuplicateHandle script::duplicate
#include "input_host_pipe_transport.h"
#undef ReadFile
#undef WriteFile
#undef ConnectNamedPipe
#undef GetOverlappedResult
#undef WaitForSingleObject
#undef CancelIoEx
#undef DisconnectNamedPipe
#undef CreateEventW
#undef DuplicateHandle
using json=nlohmann::json;
namespace transport=ymcc::inputHost::pipeTransport;
static unsigned checks=0;
static json cases=json::array();
static void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
static HANDLE source(){HANDLE h=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(h!=nullptr,"control source creation");return h;}
static void drain(transport::Runtime& owner,script::Plan& plan){plan.query=TRUE;plan.queryError=0;plan.n=0;plan.eventState=WAIT_OBJECT_0;owner.maintenance();check(!owner.hasPending(),"control retired owner not drained");}
static json outcome(const transport::Result& r){return {{"ok",r.ok},{"error",r.error},{"known",r.byteCountKnown},{"bytes",r.transferred},{"terminal",r.terminal},{"retired",r.retired},{"admitted",r.admitted},{"initialError",r.initialError},{"queryError",r.queryError},{"firstWait",r.firstWait},{"cancelAttempted",r.cancelAttempted},{"cancelError",r.cancelError}};}
static void single(const char* name,transport::Kind kind,script::Plan plan,DWORD expected,bool known=false){script::p=&plan;auto* owner=new transport::Runtime();HANDLE h=source();DWORD e=99;check(owner->adopt(h,L"test-control",e)&&e==0,"control adopt");std::vector<unsigned char> caller(64,0xA7);auto r=owner->run(h,kind,kind==transport::Kind::Connect?nullptr:caller.data(),kind==transport::Kind::Connect?0:64,17,3);check(r.error==expected&&r.ok==(expected==0),"control business outcome mismatch");check(r.byteCountKnown==known,"control byte provenance mismatch");const auto frozen=outcome(r);
 if((r.error==WAIT_TIMEOUT||r.firstWait==WAIT_FAILED)||(!known&&kind==transport::Kind::Read))check(caller==std::vector<unsigned char>(64,0xA7),"unproven/expired control read copied bytes");
 if(plan.issued&&plan.query==FALSE){check(r.terminal&&!r.retired&&!r.cancelAttempted&&plan.cancels==0,"inline TRUE lost terminal fact");}
 if(r.retired)drain(*owner,plan);owner->close(h);owner->sealAndReap();check(outcome(r)==frozen,"cleanup changed returned control result");auto stats=owner->snapshot();check(stats.available&&stats.pending==0&&stats.payloadBytes==0&&stats.admitted==stats.reaped,"control stats leaked");cases.push_back({{"case",name},{"kind",static_cast<int>(kind)},{"APIIsSynthetic",true},{"result",frozen},{"issueCalls",plan.issuedCalls},{"cancelCalls",plan.cancels}});}
static void matrix(){for(auto kind:{transport::Kind::Read,transport::Kind::Write,transport::Kind::Connect}){
 script::Plan ok;single("inline-known",kind,ok,0,kind!=transport::Kind::Connect);
 for(auto error:{ERROR_IO_INCOMPLETE,ERROR_INVALID_HANDLE,ERROR_GEN_FAILURE,ERROR_OPERATION_ABORTED,ERROR_MORE_DATA}){auto failed=ok;failed.query=FALSE;failed.queryError=error;single("inline-failed-query",kind,failed,kind==transport::Kind::Read?ERROR_INVALID_DATA:0,false);}
 auto pending=ok;pending.issued=FALSE;pending.initial=ERROR_IO_PENDING;single("pending-success",kind,pending,0,kind!=transport::Kind::Connect);
 auto timeout=pending;timeout.firstWait=WAIT_TIMEOUT;single("timeout-late-success",kind,timeout,WAIT_TIMEOUT,false);
 auto waitError=timeout;waitError.firstWait=WAIT_FAILED;single("first-wait-error-retained",kind,waitError,ERROR_INVALID_HANDLE,false);
 auto retained=timeout;retained.query=FALSE;retained.queryError=ERROR_INVALID_HANDLE;retained.eventState=WAIT_TIMEOUT;single("unknown-pending-retained",kind,retained,WAIT_TIMEOUT,false);
 auto createFailed=ok;createFailed.eventError=ERROR_ACCESS_DENIED;single("event-create-failure",kind,createFailed,ERROR_ACCESS_DENIED,false);
 }
 script::Plan partial;partial.issued=FALSE;partial.initial=ERROR_MORE_DATA;partial.query=FALSE;partial.queryError=ERROR_MORE_DATA;single("partial-read-known",transport::Kind::Read,partial,ERROR_MORE_DATA,true);
 script::Plan connected;connected.issued=FALSE;connected.initial=ERROR_PIPE_CONNECTED;single("already-connected",transport::Kind::Connect,connected,0,false);
}
static void factories(){script::Plan plan;script::p=&plan;auto* owner=new transport::Runtime();HANDLE h=source();plan.duplicateError=ERROR_ACCESS_DENIED;DWORD error=99;check(!owner->adopt(h,L"factory",error)&&error==ERROR_ACCESS_DENIED,"duplicate original error lost");DWORD flags=0;check(!GetHandleInformation(h,&flags)&&GetLastError()==ERROR_INVALID_HANDLE,"failed adopt did not close transferred source");plan.duplicateError=0;auto sealedSource=source();owner->sealAndReap();check(!owner->adopt(sealedSource,L"factory",error)&&error==ERROR_SHUTDOWN_IN_PROGRESS,"sealed adopt allowed");check(!GetHandleInformation(sealedSource,&flags),"sealed adopt did not close source");cases.push_back({{"case","factory-duplicate-error-and-sealed-adopt"},{"APIIsSynthetic",true},{"sourceOwnershipUniqueOnFailure",true}});}
static void allocationFailures(){
 script::Plan plan;script::p=&plan;auto* owner=new transport::Runtime();DWORD error=99;HANDLE h=source();
 const std::wstring factoryName=L"allocation-factory";ymcc::testing::allocationFault::failAfter(0);const bool adopted=owner->adopt(h,factoryName,error);
 check(!adopted&&error==ERROR_NOT_ENOUGH_MEMORY,"lease allocation error not mapped");DWORD flags=0;check(!GetHandleInformation(h,&flags),"lease allocation failure leaked source");
 h=source();check(owner->adopt(h,L"allocation-run",error),"allocation test adopt");std::vector<unsigned char> data(64,0xA7);
 for(int skip:{0,1}){const auto issued=plan.issuedCalls;ymcc::testing::allocationFault::failAfter(skip);auto r=owner->run(h,transport::Kind::Read,data.data(),64,17,3);check(!r.admitted&&r.error==ERROR_NOT_ENOUGH_MEMORY&&issued==plan.issuedCalls,"operation allocation failure admitted I/O");const auto stats=owner->snapshot();check(stats.pending==0&&stats.payloadBytes==0,"operation allocation failure retained capacity");}
 owner->close(h);owner->sealAndReap();cases.push_back({{"case","production-preissue-allocation-faults"},{"APIIsSynthetic",true},{"leaseAndOperationAllocationsFailedBeforeIssue",true},{"sourceAndDuplicateClosedAfterFactoryFailure",true}});
}
static void capsAndNames(){script::Plan plan;plan.issued=FALSE;plan.initial=ERROR_IO_PENDING;plan.query=FALSE;plan.queryError=ERROR_INVALID_HANDLE;plan.eventState=WAIT_TIMEOUT;plan.firstWait=WAIT_TIMEOUT;script::p=&plan;auto* owner=new transport::Runtime();DWORD error=99;HANDLE h=source();check(owner->adopt(h,L"same-name",error),"caps adopt");std::vector<unsigned char> caller(transport::kMessageBytes,0xA7);for(unsigned i=0;i<4;++i){auto r=owner->run(h,transport::Kind::Read,caller.data(),transport::kMessageBytes,17,3);check(r.retired&&r.error==WAIT_TIMEOUT,"caps pending setup");}const auto before=plan.issuedCalls;auto full=owner->run(h,transport::Kind::Read,caller.data(),transport::kMessageBytes,17,3);check(!full.admitted&&full.error==ERROR_NOT_ENOUGH_MEMORY&&plan.issuedCalls==before,"full cap admitted kernel operation");auto stats=owner->snapshot();check(stats.pending==4&&stats.payloadBytes==65536,"logical cap accounting wrong");owner->close(h);check(!owner->canCreate(L"SAME-NAME",error)&&error==ERROR_PIPE_BUSY,"same-name case-insensitive lease gate failed");check(owner->canCreate(L"new-name",error),"different-name gate blocked without active original");HANDLE next=source();check(owner->adopt(next,L"new-name",error),"different-name lease adopt");auto blocked=owner->run(next,transport::Kind::Write,caller.data(),1,17,3);check(!blocked.admitted&&plan.issuedCalls==before,"new lease bypassed shared cap");drain(*owner,plan);plan.issued=TRUE;plan.initial=0;plan.n=1;auto recovered=owner->run(next,transport::Kind::Write,caller.data(),1,17,3);check(recovered.ok,"admission did not recover after terminal reap");auto tooLarge=owner->run(next,transport::Kind::Read,caller.data(),transport::kMessageBytes+1,17,3);check(!tooLarge.admitted&&tooLarge.error==ERROR_INVALID_PARAMETER,"oversize not rejected before access/issue");owner->close(next);owner->sealAndReap();cases.push_back({{"case","production-capacity-shared-across-leases"},{"APIIsSynthetic",true},{"retainedCountAtCap",4},{"logicalBytesAtCap",65536},{"noEvictionOrNewIssueOnFull",true},{"caseInsensitiveSameNameGate",true},{"oversizeRejectedBeforeIssue",true}});}
static void sealBeforePublication(){
 script::Plan plan;script::p=&plan;check(transport::existingProcessRuntime()==nullptr,"unexpected process owner before first publication");
 transport::sealProcess();check(transport::existingProcessRuntime()==nullptr,"sealing allocated an owner");
 auto& process=transport::processRuntime();DWORD error=99;check(!process.canCreate(L"after-seal",error)&&error==ERROR_SHUTDOWN_IN_PROGRESS,"late first publication bypassed process seal");
 HANDLE h=source();check(!process.adopt(h,L"after-seal",error)&&error==ERROR_SHUTDOWN_IN_PROGRESS,"late-publication owner admitted a lease");DWORD flags=0;check(!GetHandleInformation(h,&flags),"sealed late-publication adopt leaked source");
 cases.push_back({{"case","process-seal-before-first-runtime-publication"},{"APIIsSynthetic",true},{"sealerDoesNotAllocate",true},{"lateFirstOwnerStartsSealed",true}});
}
int main(){try{DWORD before=0,after=0;check(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"initial process count");matrix();factories();allocationFailures();capsAndNames();sealBeforePublication();check(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&after==before,"control handles leaked");std::cout<<json({{"pass",true},{"checks",checks},{"cases",cases},{"processHandlesBefore",before},{"processHandlesAfter",after},{"APIResultsSynthetic",true},{"kernelPipeIOIssued",false},{"productionHeaderExecuted",true},{"noCPUOrCtrlCauseClaim",true}}).dump(2)<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
