// Production header, deterministic memory API faults; real tracked Win32 handles.
// The model never turns an unsuccessful cancel/unknown query into completion.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "json.hpp"
#include "input_pipe_test_allocation_fault.h"
namespace memoryApi {
enum Role : unsigned {Original=1,Duplicate=2,Event=4};
struct Io {HANDLE issue,event;void* data;DWORD size;bool complete=false;};
static std::map<HANDLE,Role> live;
static std::map<OVERLAPPED*,Io> ios;
static unsigned failures=0,closed=0,closeAttempts=0,cancels=0,queries=0,issues=0,disconnects=0;
static unsigned failRoles=0;
static DWORD duplicateFailure=0,resetFailure=0,queryFailure=ERROR_IO_INCOMPLETE;
static bool startPending=false;
static DWORD firstWait=WAIT_TIMEOUT;
static HANDLE entered=nullptr,release=nullptr;
static HANDLE source(){HANDLE h=::CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!h)throw std::runtime_error("source event");live.emplace(h,Original);return h;}
static HANDLE event(LPSECURITY_ATTRIBUTES a,BOOL b,BOOL c,LPCWSTR d){auto h=::CreateEventW(a,b,c,d);if(h)live.emplace(h,Event);return h;}
static BOOL duplicate(HANDLE a,HANDLE b,HANDLE c,HANDLE* d,DWORD e,BOOL f,DWORD g){
 if(duplicateFailure){SetLastError(duplicateFailure);return FALSE;}
 auto ok=::DuplicateHandle(a,b,c,d,e,f,g);if(ok)live.emplace(*d,Duplicate);return ok;
}
static BOOL close(HANDLE h){
 ++closeAttempts;auto it=live.find(h);if(it==live.end())throw std::runtime_error("unregistered or double close");
 if(failRoles&it->second){++failures;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 for(const auto& [ov,io]:ios){(void)ov;if(!io.complete&&(io.issue==h||io.event==h))throw std::runtime_error("closed live IO storage");}
 auto ok=::CloseHandle(h);if(ok){live.erase(it);++closed;}return ok;
}
static BOOL reset(HANDLE h){if(resetFailure){SetLastError(resetFailure);resetFailure=0;return FALSE;}return ::ResetEvent(h);}
static BOOL start(HANDLE h,void* data,DWORD n,OVERLAPPED* ov){
 ++issues;ios[ov]={h,ov->hEvent,data,n,!startPending};
 if(entered){::SetEvent(entered);if(::WaitForSingleObject(release,3000)!=WAIT_OBJECT_0)std::abort();}
 if(startPending){SetLastError(ERROR_IO_PENDING);return FALSE;}
 if(data&&n)std::memset(data,0x6B,n);::SetEvent(ov->hEvent);return TRUE;
}
static BOOL read(HANDLE h,void* p,DWORD n,DWORD*,OVERLAPPED* ov){return start(h,p,n,ov);}
static BOOL write(HANDLE h,const void*,DWORD n,DWORD*,OVERLAPPED* ov){return start(h,nullptr,n,ov);}
static BOOL connect(HANDLE h,OVERLAPPED* ov){return start(h,nullptr,0,ov);}
static BOOL query(HANDLE h,OVERLAPPED* ov,DWORD* n,BOOL){
 ++queries;auto& io=ios.at(ov);if(io.issue!=h)throw std::runtime_error("wrong issuing lease");
 if(!io.complete){SetLastError(queryFailure);return FALSE;}
 *n=io.size;if(io.data&&io.size)std::memset(io.data,0x6B,io.size);return TRUE;
}
static DWORD wait(HANDLE h,DWORD ms){if(ms==0)return ::WaitForSingleObject(h,0);if(firstWait==WAIT_FAILED)SetLastError(ERROR_INVALID_HANDLE);return firstWait;}
static BOOL cancel(HANDLE,OVERLAPPED*){++cancels;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL disconnect(HANDLE){++disconnects;return TRUE;}
static void complete(){for(auto& [ov,io]:ios){(void)ov;io.complete=true;::SetEvent(io.event);}}
static void clear(){ios.clear();failRoles=0;duplicateFailure=resetFailure=0;queryFailure=ERROR_IO_INCOMPLETE;startPending=false;firstWait=WAIT_TIMEOUT;}
}
#define CreateEventW memoryApi::event
#define DuplicateHandle memoryApi::duplicate
#define CloseHandle memoryApi::close
#define ResetEvent memoryApi::reset
#define ReadFile memoryApi::read
#define WriteFile memoryApi::write
#define ConnectNamedPipe memoryApi::connect
#define GetOverlappedResult memoryApi::query
#define WaitForSingleObject memoryApi::wait
#define CancelIoEx memoryApi::cancel
#define DisconnectNamedPipe memoryApi::disconnect
#include "input_host_pipe_transport.h"
#undef CreateEventW
#undef DuplicateHandle
#undef CloseHandle
#undef ResetEvent
#undef ReadFile
#undef WriteFile
#undef ConnectNamedPipe
#undef GetOverlappedResult
#undef WaitForSingleObject
#undef CancelIoEx
#undef DisconnectNamedPipe
using json=nlohmann::json;
namespace tr=ymcc::inputHost::pipeTransport;
static unsigned checks=0;
static json rows=json::array();
static void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
static bool open(HANDLE h){DWORD f=0;return ::GetHandleInformation(h,&f)!=FALSE;}
static json state(tr::Runtime& r){auto s=r.snapshot();check(s.available,"snapshot available");
#ifndef PIPE_RETIRE_LEGACY
 check(s.ownedOriginals+s.ownedDuplicates+s.ownedEvents==memoryApi::live.size(),"receipt covers every live handle");
 check(s.activeLeases+s.retainedLeases<=tr::kLeaseSlots,"bounded leases");
 check(s.operationSlots<=tr::kPendingSlots&&s.residentPayloadBytes<=tr::kPayloadBudget,"bounded operation storage/payload");
 return {{"pending",s.pending},{"active",s.activeLeases},{"retainedLeases",s.retainedLeases},{"cached",s.cachedOperations},{"retainedOperations",s.retainedOperations},{"ownedOriginals",s.ownedOriginals},{"ownedDuplicates",s.ownedDuplicates},{"ownedEvents",s.ownedEvents},{"liveDebt",s.failedCloseDebt},{"closeFailures",s.closeFailures},{"stopped",s.stopped},{"sealed",s.sealed},{"slots",s.operationSlots},{"residentPayloadBytes",s.residentPayloadBytes},{"oracleLive",memoryApi::live.size()}};
#else
 return {{"pending",s.pending},{"closeFailures",s.closeFailures},{"sealed",s.sealed},{"oracleLive",memoryApi::live.size()}};
#endif
}
static HANDLE adopt(tr::Runtime& r,const std::wstring& name){auto h=memoryApi::source();DWORD e=99;check(r.adopt(h,name,e)&&e==0,"adopt");return h;}
static void clean(tr::Runtime& r){memoryApi::complete();memoryApi::failRoles=0;r.maintenance();r.sealAndReap();r.maintenance();
#ifndef PIPE_RETIRE_LEGACY
 check(r.stopped(),"runtime stopped before process exit");
#endif
 check(memoryApi::live.empty(),"oracle zero before process exit");memoryApi::clear();}
static tr::Result read(tr::Runtime& r,HANDLE h,DWORD n=64){std::vector<unsigned char> data(n,0xA7);auto result=r.run(h,tr::Kind::Read,data.data(),n,17,1);if(result.retired)check(data==std::vector<unsigned char>(n,0xA7),"pending read did not copy");return result;}
static void debtCase(unsigned mask){auto* r=new tr::Runtime();auto h=adopt(*r,L"close-debt");check(read(*r,h).ok,"cache setup");auto active=state(*r);
 memoryApi::failRoles=mask;r->close(h);auto debt=state(*r);const auto count=memoryApi::live.size();
 for(unsigned i=0;i<12;++i)r->maintenance();auto stable=state(*r);
#ifndef PIPE_RETIRE_LEGACY
 check(debt["pending"]==0&&debt["active"]==0&&!debt["stopped"].get<bool>(),"pending zero is not retirement");
 check(debt["liveDebt"]==count&&stable["liveDebt"]==count,"current debt tracks handles, not attempts");
 check(stable["closeFailures"].get<unsigned>()>debt["closeFailures"].get<unsigned>(),"close retry recorded");
 DWORD e=0;check(!r->canCreate(L"CLOSE-DEBT",e)&&e==ERROR_PIPE_BUSY,"same-name debt blocks admission");
 check(r->canCreate(L"different",e),"different-name R3 admission unchanged");
 if(open(h)){bool taken=true;check(!r->adopt(h,L"stolen",e,&taken)&&!taken&&open(h),"borrowed original not readopted or closed");}
 memoryApi::failRoles=0;r->maintenance();auto end=state(*r);check(end["stopped"]==true&&end["liveDebt"]==0&&end["closeFailures"].get<unsigned>()>0,"history does not block current retirement");
 rows.push_back({{"case","close-fault-role"},{"roleMask",mask},{"active",active},{"debt",debt},{"afterRetries",stable},{"afterRelease",end}});clean(*r);
#else
 memoryApi::failRoles=0;r->maintenance();r->sealAndReap();auto end=state(*r);
 const bool lost=!memoryApi::live.empty();rows.push_back({{"case","old-close-loses-last-owner"},{"roleMask",mask},{"before",active},{"debt",debt},{"afterMaintenance",end},{"failed",lost}});
 // Oracle cleanup occurs only AFTER preserving the FAIL. It is not Runtime
 // retirement, and makes the negative fixture's own handles zero before exit.
 for(const auto& [handle,role]:memoryApi::live){(void)role;::CloseHandle(handle);}memoryApi::live.clear();memoryApi::clear();check(lost,"legacy must lose ownership here");
#endif
}
#ifndef PIPE_RETIRE_LEGACY
static void pendingCase(){auto* r=new tr::Runtime();auto h=adopt(*r,L"old");memoryApi::startPending=true;
 std::vector<tr::Result> returned;for(unsigned i=0;i<tr::kPendingSlots;++i)returned.push_back(read(*r,h,tr::kMessageBytes));
 auto frozen=returned.front();check(frozen.retired&&frozen.error==WAIT_TIMEOUT&&frozen.cancelError==ERROR_ACCESS_DENIED,"cancel failure remains pending");
 memoryApi::failRoles=memoryApi::Original;r->close(h);auto debt=state(*r);check(debt["pending"]==4&&debt["ownedDuplicates"]==1&&debt["liveDebt"]==1,"pending duplicate survives original failure");
 for(auto& [ov,io]:memoryApi::ios){check(open(io.issue)&&open(io.event)&&ov->hEvent==io.event,"original OVERLAPPED/handles still owned");if(io.size)check(static_cast<unsigned char*>(io.data)[0]==0,"owned buffer remains valid");}
 memoryApi::queryFailure=ERROR_INVALID_HANDLE;for(unsigned i=0;i<3;++i)r->maintenance();check(r->snapshot().pending==4,"unknown query did not become terminal");
 DWORD e=0;check(!r->canCreate(L"OLD",e)&&e==ERROR_PIPE_BUSY,"old pending name blocked");check(r->canCreate(L"new",e),"different old pending lease allowed");
 auto next=adopt(*r,L"new");auto denied=read(*r,next);check(!denied.admitted&&denied.error==ERROR_NOT_ENOUGH_MEMORY,"four slot/payload budget retained");
 memoryApi::complete();memoryApi::failRoles=0;r->maintenance();check(r->snapshot().pending==0,"conditional completion drained IO");
 check(returned.front().error==frozen.error&&returned.front().retired==frozen.retired,"returned timeout remains immutable");r->close(next);auto end=state(*r);rows.push_back({{"case","failed-cancel-unknown-query-four-pending"},{"debt",debt},{"afterRelease",end}});clean(*r);
}
static void slotDebt(){auto* r=new tr::Runtime();auto h=adopt(*r,L"slot-debt");memoryApi::startPending=true;
 for(unsigned i=0;i<4;++i)check(read(*r,h,tr::kMessageBytes).retired,"pending slot setup");
 memoryApi::complete();memoryApi::failRoles=memoryApi::Event;r->maintenance();auto debt=state(*r);
 check(debt["pending"]==0&&debt["retainedOperations"]==3&&debt["cached"]==1&&debt["liveDebt"]==3,"terminal event debts retain fixed slots");
 auto next=read(*r,h,tr::kMessageBytes);check(next.retired,"one cache reusable while other events in debt");
 auto denied=read(*r,h);check(!denied.admitted&&denied.error==ERROR_NOT_ENOUGH_MEMORY,"event debt cannot be overwritten");
 memoryApi::failRoles=0;r->maintenance();check(r->snapshot().pending==1,"closing debt does not complete live IO");
 memoryApi::complete();r->maintenance();r->close(h);rows.push_back({{"case","terminal-event-debt-reserves-slots"},{"debt",debt},{"afterRelease",state(*r)}});clean(*r);
}
static bool reject(void*)noexcept{return false;}
static void preparationFailures(){
 for(unsigned mode=0;mode<3;++mode){auto* r=new tr::Runtime();auto h=adopt(*r,L"preparation");check(read(*r,h,1).ok,"preparation cache");memoryApi::failRoles=memoryApi::Event;
  tr::Result failed;std::array<unsigned char,64> data{};
  if(mode==0){memoryApi::resetFailure=ERROR_ACCESS_DENIED;failed=read(*r,h);}
  if(mode==1){failed=r->run(h,tr::Kind::Write,data.data(),64,17,1,reject);}
  // Inject only after all caller/Checked iterator allocations are complete.
  if(mode==2){ymcc::testing::allocationFault::failAfter(0);failed=r->run(h,tr::Kind::Read,data.data(),64,17,1);}
  check(!failed.admitted,"preparation never issued");auto debt=state(*r);check(debt["liveDebt"]==1&&debt["retainedOperations"]==1,"unissued event retained after failed close");
  memoryApi::failRoles=0;r->maintenance();r->close(h);rows.push_back({{"case","unissued-event-close-debt"},{"mode",mode},{"debt",debt},{"afterRelease",state(*r)}});clean(*r);
 }
}
static void adoptFailure(){for(unsigned mode=0;mode<2;++mode){auto* r=new tr::Runtime();auto h=memoryApi::source();DWORD e=0;bool taken=false;
 const std::wstring name=L"long-allocation-failure-name";memoryApi::failRoles=memoryApi::Original;
 if(mode==0)memoryApi::duplicateFailure=ERROR_ACCESS_DENIED;else ymcc::testing::allocationFault::failAfter(0);
 check(!r->adopt(h,name,e,&taken)&&taken,"failed adopt keeps ownership receipt");check(e==(mode==0?ERROR_ACCESS_DENIED:ERROR_NOT_ENOUGH_MEMORY),"adopt original error preserved");
 auto debt=state(*r);check(debt["liveDebt"]==1&&open(h),"failed adoption original retained");
 if(mode==1)check(!r->canCreate(L"other",e)&&e==ERROR_PIPE_BUSY,"unknown name conservatively blocks reuse");
 memoryApi::duplicateFailure=0;memoryApi::failRoles=0;r->maintenance();rows.push_back({{"case","failed-adopt-close-debt"},{"mode",mode},{"debt",debt},{"afterRelease",state(*r)}});clean(*r);
}}
static void leaseBudget(){auto* r=new tr::Runtime();memoryApi::failRoles=memoryApi::Original;
 for(unsigned i=0;i<tr::kLeaseSlots;++i){auto h=adopt(*r,L"debt-"+std::to_wstring(i));r->close(h);}
 auto full=state(*r);check(full["retainedLeases"]==tr::kLeaseSlots&&full["ownedOriginals"]==tr::kLeaseSlots,"fixed lease capacity");DWORD e=0;
 check(!r->canCreate(L"another",e)&&e==ERROR_NOT_ENOUGH_MEMORY,"canCreate rejects exhausted lease budget");
 auto extra=memoryApi::source();bool taken=true;check(!r->adopt(extra,L"another",e,&taken)&&!taken&&open(extra),"capacity rejection leaves explicit caller owner");
 // This handle was never transferred. Caller returns it, then the Runtime
 // alone returns every accepted lease after the failure condition is lifted.
 check(::CloseHandle(extra)!=FALSE,"caller closes untransferred source");memoryApi::live.erase(extra);
 memoryApi::failRoles=0;r->maintenance();rows.push_back({{"case","bounded-lease-admission"},{"full",full},{"afterRelease",state(*r)}});clean(*r);
}
static void busySeal(){auto* r=new tr::Runtime();auto h=adopt(*r,L"busy");memoryApi::entered=::CreateEventW(nullptr,TRUE,FALSE,nullptr);memoryApi::release=::CreateEventW(nullptr,TRUE,FALSE,nullptr);
 tr::Result result;std::array<unsigned char,64> data{};std::thread worker([&]{result=r->run(h,tr::Kind::Read,data.data(),64,17,1);});
 check(::WaitForSingleObject(memoryApi::entered,2000)==WAIT_OBJECT_0,"worker entered issue");auto snap=r->snapshot();check(!snap.available&&!snap.stopped&&!r->stopped(),"busy query never claims stopped");
 auto start=std::chrono::steady_clock::now();r->sealAndReap();auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
 auto sealed=r->snapshot();check(sealed.sealed&&!sealed.available&&!sealed.stopped,"seal is not retirement receipt");::SetEvent(memoryApi::release);worker.join();
 ::CloseHandle(memoryApi::entered);::CloseHandle(memoryApi::release);memoryApi::entered=memoryApi::release=nullptr;
 r->maintenance();check(result.ok&&r->stopped(),"same Runtime continues busy seal retirement");rows.push_back({{"case","try-lock-busy-seal"},{"observedSealMs",ms},{"afterRelease",state(*r)}});clean(*r);
}
#endif
int main(){try{DWORD before=0,after=0;check(::GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"initial process handles");
 for(unsigned mask:{1u,2u,4u,7u}){std::cerr<<"case close mask "<<mask<<std::endl;debtCase(mask);}
#ifndef PIPE_RETIRE_LEGACY
 std::cerr<<"case pending"<<std::endl;pendingCase();std::cerr<<"case slots"<<std::endl;slotDebt();std::cerr<<"case preparation"<<std::endl;preparationFailures();std::cerr<<"case adopt"<<std::endl;adoptFailure();std::cerr<<"case budget"<<std::endl;leaseBudget();std::cerr<<"case busy"<<std::endl;busySeal();
#endif
 check(::GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&after==before,"before-exit handle count exactly restored");
#ifdef PIPE_RETIRE_LEGACY
 const bool pass=false;
#else
 const bool pass=true;
#endif
 std::cout<<json({{"pass",pass},{"checks",checks},{"cases",rows},{"memoryApiFailureInjection",true},{"kernelIoIsSynthetic",true},{"processHandlesBefore",before},{"processHandlesBeforeExit",after},{"oracleLiveBeforeExit",memoryApi::live.size()},{"closeAttempts",memoryApi::closeAttempts},{"closeFailures",memoryApi::failures},{"successfulCloses",memoryApi::closed},{"cancelCalls",memoryApi::cancels},{"queryCalls",memoryApi::queries},{"issueCalls",memoryApi::issues},{"disconnectCalls",memoryApi::disconnects},{"noProcessExitResourceReleaseClaim",true}}).dump(2)<<'\n';return pass?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
