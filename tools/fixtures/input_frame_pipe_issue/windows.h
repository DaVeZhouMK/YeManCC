#pragma once
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using DWORD=std::uint32_t;using BOOL=int;using ULONGLONG=unsigned long long;using BYTE=unsigned char;
struct FakeHandle{bool event=false,closed=false,signaled=false;unsigned endpoint=0;};using HANDLE=FakeHandle*;
inline HANDLE const INVALID_HANDLE_VALUE=reinterpret_cast<HANDLE>(~std::uintptr_t(0));
struct OVERLAPPED{std::uintptr_t Internal=0,InternalHigh=0;DWORD Offset=0,OffsetHigh=0;HANDLE hEvent=nullptr;};
constexpr BOOL TRUE=1,FALSE=0;constexpr DWORD ERROR_SUCCESS=0,ERROR_ACCESS_DENIED=5,ERROR_SEM_TIMEOUT=121,ERROR_TIMEOUT=1460,ERROR_INVALID_HANDLE=6,ERROR_NOT_ENOUGH_MEMORY=8,ERROR_INVALID_DATA=13,ERROR_GEN_FAILURE=31,ERROR_INVALID_PARAMETER=87,ERROR_BROKEN_PIPE=109,ERROR_BUSY=170,ERROR_PIPE_BUSY=231,ERROR_NO_DATA=232,ERROR_PIPE_NOT_CONNECTED=233,ERROR_MORE_DATA=234,ERROR_PIPE_CONNECTED=535,ERROR_OPERATION_ABORTED=995,ERROR_IO_PENDING=997,ERROR_IO_INCOMPLETE=996,ERROR_SHUTDOWN_IN_PROGRESS=1115,ERROR_ARITHMETIC_OVERFLOW=534;
constexpr DWORD WAIT_OBJECT_0=0,WAIT_TIMEOUT=258,WAIT_FAILED=0xffffffffu,INFINITE=0xffffffffu,DUPLICATE_SAME_ACCESS=2;constexpr int CSTR_EQUAL=2;
inline ULONGLONG tick=1000;inline ULONGLONG GetTickCount64(){return tick;}
namespace fake {
inline DWORD error=0;inline unsigned nextEndpoint=0,creates=0,closes=0,writes=0,reads=0,connects=0,cancels=0,resets=0,duplicateClosesWhilePending=0;
inline std::vector<std::unique_ptr<FakeHandle>> handles;
struct Operation{HANDLE pipe=nullptr,event=nullptr;DWORD bytes=0;bool pending=false,connect=false;DWORD completion=ERROR_SUCCESS;};inline std::map<OVERLAPPED*,Operation> operations;
inline DWORD connectFailure=ERROR_SUCCESS;
inline bool pendingConnect=false,completeConnect=false,pendingWrites=false,pendingReads=false,cancelCompletes=false;
inline std::function<void()> connectHook,resetHook,createHook,writeHook,readHook;
inline std::map<HANDLE,unsigned> closeFailureCounts;
inline HANDLE failedProbe=nullptr,lastIssuedEvent=nullptr;
struct WaitStep{unsigned count=0;DWORD milliseconds=0,advance=0;std::function<void()> action;DWORD failure=ERROR_SUCCESS;};
struct WaitObservation{unsigned count=0;DWORD milliseconds=0,result=WAIT_TIMEOUT;ULONGLONG begin=0,end=0;};
inline std::deque<WaitStep> waitSteps;inline std::vector<WaitObservation> waits;inline bool waitMismatch=false;
inline void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
inline HANDLE allocate(bool event=false,unsigned endpoint=0){handles.push_back(std::make_unique<FakeHandle>());auto h=handles.back().get();h->event=event;h->endpoint=endpoint;return h;}
inline bool livePending(HANDLE h){for(auto& [ov,o]:operations)if(o.pending&&(o.pipe==h||o.event==h))return true;return false;}
inline void once(std::function<void()>& hook){if(hook){auto f=std::move(hook);hook={};f();}}
inline void finish(Operation& o,DWORD completion=ERROR_SUCCESS){o.pending=false;o.completion=completion;o.event->signaled=true;}
inline void completePending(DWORD completion=ERROR_SUCCESS){for(auto&[ov,o]:operations)if(o.pending)finish(o,completion);}
inline void queueWait(unsigned count,DWORD milliseconds,DWORD advance,const std::function<void()>& action={},DWORD failure=ERROR_SUCCESS){waitSteps.push_back({count,milliseconds,advance,action,failure});}
inline DWORD ready(unsigned count,const HANDLE* values){for(unsigned i=0;i<count;++i){auto h=values[i];require(h&&!h->closed,"wait on closed");if(h->event){if(h->signaled)return WAIT_OBJECT_0+i;}else if(!livePending(h))return WAIT_OBJECT_0+i;}return WAIT_TIMEOUT;}
inline DWORD wait(unsigned count,const HANDLE* values,DWORD milliseconds){
 const ULONGLONG begin=tick;DWORD result=ready(count,values);
 // Zero-time observations never consume a scheduled blocking interleave.
 if(count==1&&milliseconds==0&&values[0]==failedProbe){error=ERROR_ACCESS_DENIED;result=WAIT_FAILED;}
 else if(result==WAIT_TIMEOUT&&milliseconds!=0){
  if(!waitSteps.empty()){
   auto step=std::move(waitSteps.front());waitSteps.pop_front();
   waitMismatch=waitMismatch||step.count!=count||step.milliseconds!=milliseconds;tick+=step.advance;
   if(step.action)step.action();
   if(step.failure){error=step.failure;result=WAIT_FAILED;}else result=ready(count,values);
  }else{require(milliseconds!=INFINITE,"unbounded unscripted fixture wait");tick+=milliseconds;}
 }
 waits.push_back({count,milliseconds,result,begin,tick});return result;
}
inline unsigned blockingWaitCount(unsigned count){unsigned result=0;for(const auto& w:waits)result+=w.count==count&&w.milliseconds!=0;return result;}
inline std::vector<DWORD> blockingBudgets(){std::vector<DWORD> result;for(const auto& w:waits)if(w.milliseconds!=0)result.push_back(w.milliseconds);return result;}
inline void clear(){for(auto&[ov,o]:operations)require(!o.pending,"reset loses pending resources");operations.clear();connectFailure=ERROR_SUCCESS;pendingConnect=completeConnect=pendingWrites=pendingReads=cancelCompletes=false;connectHook=resetHook=createHook=writeHook=readHook={};writes=reads=connects=cancels=resets=0;closeFailureCounts.clear();failedProbe=lastIssuedEvent=nullptr;waitSteps.clear();waits.clear();waitMismatch=false;}
}
inline DWORD GetLastError(){return fake::error;}inline void SetLastError(DWORD e){fake::error=e;}
inline HANDLE GetCurrentProcess(){return nullptr;}
inline BOOL CloseHandle(HANDLE h){fake::require(h&&h!=INVALID_HANDLE_VALUE&&!h->closed,"double/invalid close");if(auto it=fake::closeFailureCounts.find(h);it!=fake::closeFailureCounts.end()&&it->second){--it->second;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}if(fake::livePending(h))++fake::duplicateClosesWhilePending;h->closed=true;++fake::closes;return TRUE;}
inline BOOL DuplicateHandle(HANDLE,HANDLE source,HANDLE,HANDLE* target,DWORD,BOOL,DWORD){*target=fake::allocate(false,source->endpoint);return TRUE;}
inline int CompareStringOrdinal(const wchar_t* a,int,const wchar_t* b,int,BOOL){std::wstring x=a,y=b;for(auto&c:x)c=std::towupper(c);for(auto&c:y)c=std::towupper(c);return x==y?CSTR_EQUAL:1;}
inline HANDLE CreateEventW(void*,BOOL,BOOL initial,const wchar_t*){++fake::creates;auto h=fake::allocate(true);h->signaled=initial!=FALSE;fake::once(fake::createHook);return h;}
inline BOOL SetEvent(HANDLE h){fake::require(h&&h->event&&!h->closed,"signal closed/non-event");h->signaled=true;return TRUE;}
inline BOOL ResetEvent(HANDLE h){fake::require(h&&h->event&&!h->closed&&!fake::livePending(h),"reset active/closed event");h->signaled=false;++fake::resets;fake::once(fake::resetHook);return TRUE;}
inline BOOL ConnectNamedPipe(HANDLE h,OVERLAPPED* ov){++fake::connects;fake::lastIssuedEvent=ov->hEvent;fake::operations[ov]={h,ov->hEvent,0,fake::pendingConnect,true};fake::once(fake::connectHook);if(fake::connectFailure){SetLastError(fake::connectFailure);return FALSE;}if(fake::pendingConnect){SetLastError(ERROR_IO_PENDING);return FALSE;}ov->hEvent->signaled=true;SetLastError(ERROR_PIPE_CONNECTED);return FALSE;}
inline BOOL WriteFile(HANDLE h,const void*,DWORD n,DWORD* count,OVERLAPPED* ov){fake::require(h&&!h->closed&&!count,"bad overlapped write");++fake::writes;fake::lastIssuedEvent=ov->hEvent;fake::operations[ov]={h,ov->hEvent,n,fake::pendingWrites,false};ov->hEvent->signaled=!fake::pendingWrites;fake::once(fake::writeHook);if(fake::pendingWrites){SetLastError(ERROR_IO_PENDING);return FALSE;}return TRUE;}
inline BOOL ReadFile(HANDLE h,void* data,DWORD,DWORD* count,OVERLAPPED* ov){fake::require(h&&!h->closed&&!count,"bad overlapped read");++fake::reads;static constexpr char r[]="{}";std::memcpy(data,r,2);fake::lastIssuedEvent=ov->hEvent;fake::operations[ov]={h,ov->hEvent,2,fake::pendingReads,false};ov->hEvent->signaled=!fake::pendingReads;fake::once(fake::readHook);if(fake::pendingReads){SetLastError(ERROR_IO_PENDING);return FALSE;}return TRUE;}
inline BOOL GetOverlappedResult(HANDLE h,OVERLAPPED* ov,DWORD* n,BOOL wait){fake::require(!wait&&h&&!h->closed,"blocking/closed query");auto it=fake::operations.find(ov);fake::require(it!=fake::operations.end()&&it->second.pipe==h,"query wrong operation owner");auto&o=it->second;if(o.pending&&o.connect&&fake::completeConnect)fake::finish(o);if(o.pending){SetLastError(ERROR_IO_INCOMPLETE);return FALSE;}*n=o.completion==ERROR_SUCCESS?o.bytes:0;if(o.completion){SetLastError(o.completion);return FALSE;}return TRUE;}
inline DWORD WaitForSingleObject(HANDLE h,DWORD milliseconds){return fake::wait(1,&h,milliseconds);}
inline DWORD WaitForMultipleObjects(DWORD count,const HANDLE* values,BOOL all,DWORD milliseconds){fake::require(count==2&&!all,"expected IO plus public cancellation wait-any");return fake::wait(count,values,milliseconds);}
inline BOOL CancelIoEx(HANDLE h,OVERLAPPED* ov){auto it=fake::operations.find(ov);fake::require(it!=fake::operations.end()&&it->second.pipe==h&&!h->closed,"cancel wrong owner");++fake::cancels;if(fake::cancelCompletes)fake::finish(it->second,ERROR_OPERATION_ABORTED);return TRUE;}
inline BOOL DisconnectNamedPipe(HANDLE h){fake::require(h&&!h->closed,"disconnect closed");return TRUE;}
