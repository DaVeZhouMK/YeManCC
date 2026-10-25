// Private same-process message pipes; all I/O, waits, queries and disconnects
// go to Win32. Explicit API-boundary faults only deny cancel/close when enabled.
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
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
namespace realApi {
enum Role:unsigned{Original=1,Duplicate=2,Event=4};
static std::map<HANDLE,Role> live;
static unsigned failClose=0,closeAttempts=0,closeFailures=0,closes=0,waits=0,queries=0,disconnects=0,cancels=0,cancelDenials=0;
static bool denyCancel=false;
static HANDLE event(LPSECURITY_ATTRIBUTES a,BOOL b,BOOL c,LPCWSTR d){auto h=::CreateEventW(a,b,c,d);if(h)live.emplace(h,Event);return h;}
static BOOL duplicate(HANDLE a,HANDLE b,HANDLE c,HANDLE* d,DWORD e,BOOL f,DWORD g){auto ok=::DuplicateHandle(a,b,c,d,e,f,g);if(ok)live.emplace(*d,Duplicate);return ok;}
static BOOL close(HANDLE h){++closeAttempts;auto it=live.find(h);if(it==live.end())std::abort();if(failClose&it->second){++closeFailures;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}auto ok=::CloseHandle(h);if(ok){++closes;live.erase(it);}return ok;}
static DWORD wait(HANDLE h,DWORD ms){++waits;return ::WaitForSingleObject(h,ms);}
static BOOL query(HANDLE h,OVERLAPPED* ov,DWORD* n,BOOL b){++queries;return ::GetOverlappedResult(h,ov,n,b);}
static BOOL disconnect(HANDLE h){++disconnects;return ::DisconnectNamedPipe(h);}
static BOOL cancel(HANDLE h,OVERLAPPED* ov){++cancels;if(denyCancel){++cancelDenials;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::CancelIoEx(h,ov);}
}
#define CreateEventW realApi::event
#define DuplicateHandle realApi::duplicate
#define CloseHandle realApi::close
#define WaitForSingleObject realApi::wait
#define GetOverlappedResult realApi::query
#define DisconnectNamedPipe realApi::disconnect
#define CancelIoEx realApi::cancel
#include "input_host_pipe_transport.h"
#undef CreateEventW
#undef DuplicateHandle
#undef CloseHandle
#undef WaitForSingleObject
#undef GetOverlappedResult
#undef DisconnectNamedPipe
#undef CancelIoEx
using json=nlohmann::json;
namespace tr=ymcc::inputHost::pipeTransport;
static unsigned checks=0,pairs=0;
static json rows=json::array();
static void check(bool b,const char* text){++checks;if(!b)throw std::runtime_error(std::string(text)+" win32="+std::to_string(GetLastError()));}
static json state(tr::Runtime& r){auto s=r.snapshot();check(s.available,"snapshot");check(s.ownedOriginals+s.ownedDuplicates+s.ownedEvents==realApi::live.size(),"every live transport handle accounted");return {{"pending",s.pending},{"active",s.activeLeases},{"retainedLeases",s.retainedLeases},{"cached",s.cachedOperations},{"debt",s.failedCloseDebt},{"originals",s.ownedOriginals},{"duplicates",s.ownedDuplicates},{"events",s.ownedEvents},{"stopped",s.stopped},{"sealed",s.sealed},{"closeFailures",s.closeFailures},{"oracleLive",realApi::live.size()}};}
static void drain(tr::Runtime& r){for(unsigned i=0;i<100&&!r.stopped();++i){r.maintenance();if(!r.stopped())::Sleep(1);}check(r.stopped(),"resource return before exit");check(realApi::live.empty(),"oracle zero before exit");}
struct Pair {
 tr::Runtime* owner=new tr::Runtime();std::wstring name;HANDLE source=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 explicit Pair(std::wstring requested=L""){
  name=requested.empty()?L"\\\\.\\pipe\\YMCC-VC-PipeRetire23-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++pairs):std::move(requested);
  source=::CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,tr::kMessageBytes,tr::kMessageBytes,0,nullptr);
  check(source!=INVALID_HANDLE_VALUE,"create private pipe");realApi::live.emplace(source,realApi::Original);DWORD e=0;bool taken=false;check(owner->adopt(source,name,e,&taken)&&taken,"adopt private pipe");
 }
 void connect(){client=::CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);check(client!=INVALID_HANDLE_VALUE,"private peer");DWORD mode=PIPE_READMODE_MESSAGE;check(::SetNamedPipeHandleState(client,&mode,nullptr,nullptr)!=FALSE,"message peer");ULONG pid=0;check(::GetNamedPipeClientProcessId(source,&pid)&&pid==GetCurrentProcessId(),"client PID");check(::GetNamedPipeServerProcessId(client,&pid)&&pid==GetCurrentProcessId(),"server PID");check(owner->run(source,tr::Kind::Connect,nullptr,0,50,5).ok,"connect result");}
 void finish(){owner->close(source);drain(*owner);if(client!=INVALID_HANDLE_VALUE){check(::CloseHandle(client)!=FALSE,"close peer");client=INVALID_HANDLE_VALUE;}}
};
static DWORD peerIo(HANDLE h,bool write,void* data,DWORD size){auto event=::CreateEventW(nullptr,TRUE,FALSE,nullptr);check(event!=nullptr,"peer event");OVERLAPPED ov{};ov.hEvent=event;BOOL ok=write?::WriteFile(h,data,size,nullptr,&ov) : ::ReadFile(h,data,size,nullptr,&ov);DWORD e=ok?ERROR_SUCCESS:GetLastError(),n=0;
 if(!ok&&e==ERROR_IO_PENDING){auto w=::WaitForSingleObject(event,2000);if(w!=WAIT_OBJECT_0){check(::CancelIoEx(h,&ov)!=FALSE||GetLastError()==ERROR_NOT_FOUND,"peer cancel");check(::WaitForSingleObject(event,2000)==WAIT_OBJECT_0,"peer cancellation terminal");}}
 ok=::GetOverlappedResult(h,&ov,&n,FALSE);e=ok?0:GetLastError();check(::CloseHandle(event)!=FALSE,"close peer event");check(ok&&e==0,"peer terminal transfer");return n;
}
static void normalDebt(){Pair p;p.connect();std::array<unsigned char,64> data{},received{};data.fill(0x58);check(peerIo(p.client,true,data.data(),64)==64,"peer write");auto r=p.owner->run(p.source,tr::Kind::Read,received.data(),64,50,5);check(r.ok&&r.transferred==64&&received==data,"real owned read");auto w=p.owner->run(p.source,tr::Kind::Write,data.data(),64,50,5);check(w.ok&&peerIo(p.client,false,received.data(),64)==64&&received==data,"real owned write");auto active=state(*p.owner);check(active["pending"]==0&&active["active"]==1&&active["cached"]==1&&!p.owner->stopped(),"cache/active observable before close");
 realApi::failClose=7;p.owner->close(p.source);auto debt=state(*p.owner);check(debt["pending"]==0&&debt["debt"]==3&&!p.owner->stopped(),"real handles retained after denied close");DWORD e=0;check(!p.owner->canCreate(p.name,e)&&e==ERROR_PIPE_BUSY,"private same name debt gate");p.owner->sealAndReap();auto sealed=state(*p.owner);check(sealed["sealed"]==true&&!p.owner->stopped(),"sealed debt is not stopped");
 realApi::failClose=0;drain(*p.owner);auto end=state(*p.owner);check(end["closeFailures"].get<unsigned>()>0&&end["debt"]==0,"historical failures remain after successful return");p.finish();
 Pair replacement(p.name);replacement.finish();rows.push_back({{"case","real-transfer-disconnect-all-close-debts"},{"active",active},{"debt",debt},{"sealedDebt",sealed},{"afterRelease",end},{"samePrivateNameRecreated",true}});
}
static void pendingConnect(){Pair p;realApi::denyCancel=true;realApi::failClose=realApi::Original;auto r=p.owner->run(p.source,tr::Kind::Connect,nullptr,0,10,3);check(r.admitted&&r.retired&&!r.terminal&&r.error==WAIT_TIMEOUT&&r.cancelError==ERROR_ACCESS_DENIED,"real unconnected request remains pending after denied cancel");auto pending=state(*p.owner);p.owner->sealAndReap();auto debt=state(*p.owner);check(debt["sealed"]==true&&debt["debt"]==1&&debt["originals"]==1&&!p.owner->stopped(),"seal cannot pass retained original debt");if(debt["pending"]==1)check(debt["duplicates"]==1,"uncompleted connect keeps duplicate");realApi::denyCancel=false;realApi::failClose=0;drain(*p.owner);auto end=state(*p.owner);check(r.error==WAIT_TIMEOUT&&r.retired&&!r.terminal,"original result immutable after later terminal query");p.finish();rows.push_back({{"case","real-wait-query-failed-cancel-conditional-release"},{"pending",pending},{"sealedDebt",debt},{"afterRelease",end}});}
static void pendingRead(){Pair p;p.connect();std::array<unsigned char,64> data{};data.fill(0xA7);realApi::denyCancel=true;auto r=p.owner->run(p.source,tr::Kind::Read,data.data(),64,10,3);check(r.retired&&!r.terminal&&r.error==WAIT_TIMEOUT,"real read remains pending");check(data.front()==0xA7,"pending read caller unchanged");auto pending=state(*p.owner);p.owner->close(p.source);realApi::denyCancel=false;drain(*p.owner);auto end=state(*p.owner);check(data.front()==0xA7&&r.error==WAIT_TIMEOUT,"late disconnect cannot mutate expired caller/result");p.finish();rows.push_back({{"case","real-read-disconnect-query-before-free"},{"pending",pending},{"afterRelease",end}});}
int main(){try{DWORD before=0,after=0;check(::GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"before handles");normalDebt();pendingConnect();pendingRead();check(::GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE&&after==before,"before-exit handle count restored");std::cout<<json({{"pass",true},{"checks",checks},{"cases",rows},{"privatePipePairs",pairs},{"realWaitCalls",realApi::waits},{"realQueryCalls",realApi::queries},{"realDisconnectCalls",realApi::disconnects},{"cancelCalls",realApi::cancels},{"cancelFaultDenials",realApi::cancelDenials},{"closeAttempts",realApi::closeAttempts},{"closeFaultDenials",realApi::closeFailures},{"successfulCloses",realApi::closes},{"oracleLiveBeforeExit",realApi::live.size()},{"processHandlesBefore",before},{"processHandlesBeforeExit",after},{"productsOrDevicesUsed",false},{"arbitraryOsBlockingBound","UNKNOWN"}}).dump(2)<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
