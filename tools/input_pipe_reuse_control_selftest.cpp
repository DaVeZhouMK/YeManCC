// Actual Runtime reuse controls with synthetic pipe I/O and real events/handles.
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
namespace fake {
struct Entry {void* data=nullptr;DWORD size=0;HANDLE event=nullptr;bool pending=false,force=false;DWORD n=0;unsigned char value=0;};
static std::map<OVERLAPPED*,Entry> operations;
static bool nextPending=false;
static unsigned char nextValue=0x55;
static DWORD resetFailure=0;
static unsigned creates=0,resets=0,cancels=0,issues=0,unsafeReuse=0;
static OVERLAPPED* last=nullptr;
static BOOL issue(void* data,DWORD size,OVERLAPPED* ov){
 auto previous=operations.find(ov);if(previous!=operations.end()&&previous->second.pending){++unsafeReuse;SetLastError(ERROR_INVALID_DATA);return FALSE;}
 Entry entry{data,size,ov->hEvent,nextPending,false,size,nextValue};operations[ov]=entry;last=ov;++issues;
 if(nextPending){SetLastError(ERROR_IO_PENDING);return FALSE;}
 if(data&&size)std::memset(data,nextValue,size);SetEvent(ov->hEvent);return TRUE;
}
static BOOL read(HANDLE,void* data,DWORD size,DWORD*,OVERLAPPED* ov){return issue(data,size,ov);}
static BOOL write(HANDLE,const void*,DWORD size,DWORD*,OVERLAPPED* ov){return issue(nullptr,size,ov);}
static BOOL connect(HANDLE,OVERLAPPED* ov){return issue(nullptr,0,ov);}
static BOOL query(HANDLE,OVERLAPPED* ov,DWORD* n,BOOL){auto& entry=operations.at(ov);if(entry.pending&&!entry.force){*n=0;SetLastError(ERROR_INVALID_HANDLE);return FALSE;}if(entry.pending){entry.pending=false;if(entry.data&&entry.n)std::memset(entry.data,entry.value,entry.n);SetEvent(entry.event);}*n=entry.n;return TRUE;}
static DWORD wait(HANDLE event,DWORD ms){if(ms==0)return WaitForSingleObject(event,0);return WaitForSingleObject(event,0)==WAIT_OBJECT_0?WAIT_OBJECT_0:WAIT_TIMEOUT;}
static BOOL cancel(HANDLE,OVERLAPPED*){++cancels;SetLastError(ERROR_NOT_FOUND);return FALSE;}
static BOOL disconnect(HANDLE){return TRUE;}
static HANDLE event(LPSECURITY_ATTRIBUTES a,BOOL b,BOOL c,LPCWSTR d){++creates;return CreateEventW(a,b,c,d);}
[[maybe_unused]] static BOOL reset(HANDLE h){++resets;if(resetFailure){SetLastError(std::exchange(resetFailure,0));return FALSE;}for(const auto& [key,entry]:operations){(void)key;if(entry.event==h&&entry.pending){++unsafeReuse;SetLastError(ERROR_INVALID_DATA);return FALSE;}}return ResetEvent(h);}
}
#define ReadFile fake::read
#define WriteFile fake::write
#define ConnectNamedPipe fake::connect
#define GetOverlappedResult fake::query
#define WaitForSingleObject fake::wait
#define CancelIoEx fake::cancel
#define DisconnectNamedPipe fake::disconnect
#define CreateEventW fake::event
#define ResetEvent fake::reset
#include "input_host_pipe_transport.h"
#undef ReadFile
#undef WriteFile
#undef ConnectNamedPipe
#undef GetOverlappedResult
#undef WaitForSingleObject
#undef CancelIoEx
#undef DisconnectNamedPipe
#undef CreateEventW
#undef ResetEvent
using json=nlohmann::json;
namespace transport=ymcc::inputHost::pipeTransport;
static unsigned checks=0;
static void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
static HANDLE source(){HANDLE h=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(h!=nullptr,"control original handle");return h;}
static HANDLE adopt(transport::Runtime& owner,const wchar_t* name){HANDLE h=source();DWORD e=99;check(owner.adopt(h,name,e)&&e==0,"reuse control adopt");return h;}
static void repeatedReuse(){auto* owner=new transport::Runtime();const auto before=fake::creates;
 for(unsigned lease=0;lease<20;++lease){HANDLE h=adopt(*owner,L"same-name");std::uint64_t previous=0;for(unsigned n=0;n<100;++n){const auto kind=n%3==0?transport::Kind::Connect:n%3==1?transport::Kind::Read:transport::Kind::Write;const DWORD size=kind==transport::Kind::Connect?0:std::array<DWORD,4>{0,64,1024,16384}[n%4];std::vector<unsigned char> caller(size,0xA7);fake::nextValue=static_cast<unsigned char>((n+lease)%251);fake::nextPending=false;auto r=owner->run(h,kind,size?caller.data():nullptr,size,17,3);check(r.ok&&r.terminal&&!r.retired&&!r.cancelAttempted&&r.initialError==0&&r.queryError==0,"reuse retained completion/cancel/error state");check(r.operation>previous,"reused operation kept old request identity");previous=r.operation;check(r.byteCountKnown==(kind!=transport::Kind::Connect),"reuse retained byte applicability");check(r.transferred==(kind==transport::Kind::Connect?0:size),"reuse retained old byte count");if(kind==transport::Kind::Read)check(caller==std::vector<unsigned char>(size,fake::nextValue),"reuse read stale payload");}
 owner->close(h);DWORD e=99;check(owner->canCreate(L"SAME-NAME",e)&&e==0,"cached lease blocked name after close");auto s=owner->snapshot();check(s.pending==0&&s.payloadBytes==0&&s.admitted==s.reaped,"reuse accounting wrong");}
#ifdef R45_CANDIDATE
 check(fake::creates-before==20,"candidate did not limit cache to one event per active lease");
#else
 check(fake::creates-before==2000,"baseline event model unexpected");
#endif
 owner->sealAndReap();}
static void retiredGenerationIsolation(){auto* owner=new transport::Runtime();HANDLE old=adopt(*owner,L"old");std::vector<unsigned char> oldCaller(64,0xA7),newCaller(64,0xA7);fake::nextPending=true;auto a=owner->run(old,transport::Kind::Read,oldCaller.data(),64,17,3);auto* oldOv=fake::last;check(a.retired&&a.error==WAIT_TIMEOUT,"old pending setup");owner->close(old);DWORD e=99;check(!owner->canCreate(L"OLD",e)&&e==ERROR_PIPE_BUSY,"old name no longer gated");HANDLE next=adopt(*owner,L"new");auto b=owner->run(next,transport::Kind::Read,newCaller.data(),64,17,3);auto* pendingNext=fake::last;check(b.retired&&b.lease!=a.lease&&oldOv!=pendingNext,"new pending reused old storage");fake::operations.at(oldOv).force=true;owner->maintenance();const auto createdBefore=fake::creates;fake::nextPending=false;fake::nextValue=0x73;auto current=owner->run(next,transport::Kind::Read,newCaller.data(),64,17,3);check(current.ok&&fake::creates==createdBefore+1,"late old generation supplied new lease cache");check(oldCaller==std::vector<unsigned char>(64,0xA7)&&newCaller==std::vector<unsigned char>(64,0x73),"late generation payload escaped");fake::operations.at(pendingNext).force=true;owner->maintenance();auto c=owner->run(next,transport::Kind::Read,newCaller.data(),64,17,3);check(c.ok&&!c.cancelAttempted&&!c.retired&&c.error==0,"cancel state leaked through reap/cache");check(a.error==WAIT_TIMEOUT&&b.error==WAIT_TIMEOUT,"returned late outcomes rewritten");owner->close(next);owner->sealAndReap();check(!owner->hasPending(),"generation test retained pending");}
static void resetFailureControl(){auto* owner=new transport::Runtime();HANDLE h=adopt(*owner,L"reset");std::vector<unsigned char> caller(64,0xA7);fake::nextPending=false;auto first=owner->run(h,transport::Kind::Read,caller.data(),64,17,3);check(first.ok,"reset failure setup");const auto before=fake::issues;
#ifdef R45_CANDIDATE
 fake::resetFailure=ERROR_ACCESS_DENIED;auto failed=owner->run(h,transport::Kind::Read,caller.data(),64,17,3);check(!failed.admitted&&failed.error==ERROR_ACCESS_DENIED&&fake::issues==before,"ResetEvent failure ignored or issue started");check(!owner->hasPending(),"reset failure retained slot");
#else
 (void)before;
#endif
 const auto freshBefore=fake::creates;auto recovered=owner->run(h,transport::Kind::Read,caller.data(),64,17,3);check(recovered.ok&&fake::creates==freshBefore+1,"reset failure discarded cache but recovery did not allocate new event");owner->close(h);owner->sealAndReap();}
int main(){try{DWORD before=0,after=0;check(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"initial handles");repeatedReuse();retiredGenerationIsolation();resetFailureControl();check(fake::unsafeReuse==0,"kernel model observed pending reset/reuse");check(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&after==before,"reuse control leaked owned handles");std::cout<<json({{"pass",true},{"checks",checks},{"repeatedCompletedOperations",2000},{"repeatedLeaseLifetimes",20},{"eventCreates",fake::creates},{"eventResets",fake::resets},{"issueCalls",fake::issues},{"unsafePendingReuse",fake::unsafeReuse},{"processHandlesBefore",before},{"processHandlesAfter",after},{"allPipeIOIsSynthetic",true},{"resetFailureInjectedOnlyForCandidate",true},{"productionEdited",false}}).dump(2)<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
