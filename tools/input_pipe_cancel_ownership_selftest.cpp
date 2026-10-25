// R32 permitted-API-result model, not real kernel fault reproduction.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
enum class Scenario { Sync,PendingSuccess,PendingTerminalError,CancelCompleted,CancelStillPending,WaitFailedCancelStillPending,StartFailure,EventFailure,AlreadyConnected };
struct Model {Scenario scenario=Scenario::Sync;bool pending=false,cancelRequested=false,ovObserved=false,bufferObserved=false;unsigned creates=0,closes=0,waits=0,cancels=0,queries=0,closedWhilePending=0,readStarts=0,writeStarts=0,connectStarts=0;};
static Model model;
static HANDLE FakeCreateEventW(LPSECURITY_ATTRIBUTES,BOOL,BOOL,LPCWSTR){++model.creates;if(model.scenario==Scenario::EventFailure){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}return reinterpret_cast<HANDLE>(uintptr_t(0x1001));}
static BOOL FakeStart(void* buffer,DWORD bytes,DWORD* transferred,OVERLAPPED* ov){model.ovObserved=ov!=nullptr;model.bufferObserved=buffer!=nullptr;
 if(model.scenario==Scenario::Sync){*transferred=bytes;return TRUE;}
 if(model.scenario==Scenario::StartFailure){SetLastError(ERROR_BROKEN_PIPE);return FALSE;}
 model.pending=true;SetLastError(ERROR_IO_PENDING);return FALSE;
}
static BOOL FakeWriteFile(HANDLE,LPCVOID buffer,DWORD bytes,LPDWORD transferred,LPOVERLAPPED ov){++model.writeStarts;return FakeStart(const_cast<void*>(buffer),bytes,transferred,ov);}
static BOOL FakeReadFile(HANDLE,LPVOID buffer,DWORD bytes,LPDWORD transferred,LPOVERLAPPED ov){++model.readStarts;return FakeStart(buffer,bytes,transferred,ov);}
static BOOL FakeConnectNamedPipe(HANDLE,LPOVERLAPPED ov){++model.connectStarts;model.ovObserved=ov!=nullptr;if(model.scenario==Scenario::AlreadyConnected){SetLastError(ERROR_PIPE_CONNECTED);return FALSE;}DWORD unused=0;return FakeStart(nullptr,0,&unused,ov);}
static DWORD FakeWaitForSingleObject(HANDLE,DWORD timeout){++model.waits;
 if(model.waits==1&&(model.scenario==Scenario::PendingSuccess||model.scenario==Scenario::PendingTerminalError)){model.pending=false;return WAIT_OBJECT_0;}
 if(model.waits==1&&model.scenario==Scenario::WaitFailedCancelStillPending){SetLastError(ERROR_INVALID_HANDLE);return WAIT_FAILED;}
 if(model.waits==2&&model.scenario==Scenario::CancelCompleted){model.pending=false;return WAIT_OBJECT_0;}
 // The cancellation request does not itself complete the I/O. This is the
 // documented permitted branch under test, not a simulated timing measurement.
 return WAIT_TIMEOUT;
}
static BOOL FakeCancelIoEx(HANDLE,LPOVERLAPPED){++model.cancels;model.cancelRequested=true;return TRUE;}
static BOOL FakeGetOverlappedResult(HANDLE,LPOVERLAPPED,LPDWORD transferred,BOOL){++model.queries;
 if(model.pending){SetLastError(ERROR_IO_INCOMPLETE);return FALSE;}
 if(model.scenario==Scenario::PendingTerminalError||model.cancelRequested){SetLastError(ERROR_OPERATION_ABORTED);return FALSE;}
 *transferred=7;return TRUE;
}
static BOOL FakeCloseHandle(HANDLE){++model.closes;if(model.pending)++model.closedWhilePending;return TRUE;}
#define CreateEventW FakeCreateEventW
#define WriteFile FakeWriteFile
#define ReadFile FakeReadFile
#define ConnectNamedPipe FakeConnectNamedPipe
#define WaitForSingleObject FakeWaitForSingleObject
#define CancelIoEx FakeCancelIoEx
#define GetOverlappedResult FakeGetOverlappedResult
#define CloseHandle FakeCloseHandle
#include "actual-helpers.hpp"
#undef CreateEventW
#undef WriteFile
#undef ReadFile
#undef ConnectNamedPipe
#undef WaitForSingleObject
#undef CancelIoEx
#undef GetOverlappedResult
#undef CloseHandle
static unsigned checks=0;
static void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
static const char* scenarioName(Scenario s){switch(s){case Scenario::Sync:return "sync";case Scenario::PendingSuccess:return "pending-terminal-success";case Scenario::PendingTerminalError:return "pending-terminal-error";case Scenario::CancelCompleted:return "cancel-completed-within-wait";case Scenario::CancelStillPending:return "cancel-accepted-still-pending-after-wait";case Scenario::WaitFailedCancelStillPending:return "wait-failed-cancel-accepted-still-pending";case Scenario::StartFailure:return "start-error";case Scenario::EventFailure:return "event-create-error";default:return "already-connected";}}
int main(){try{
 json rows=json::array();unsigned hazardousRows=0;const HANDLE pipe=reinterpret_cast<HANDLE>(uintptr_t(0x2001));std::array<char,64> storage{};
 for(int operation=0;operation<3;++operation)for(auto s:{Scenario::Sync,Scenario::PendingSuccess,Scenario::PendingTerminalError,Scenario::CancelCompleted,Scenario::CancelStillPending,Scenario::WaitFailedCancelStillPending,Scenario::StartFailure,Scenario::EventFailure}){
  model={};model.scenario=s;DWORD count=999,error=999;bool ok=operation==2?inputHostWaitClientLocked(pipe,1000,&error):inputHostPipeIo(pipe,operation==0,storage.data(),DWORD(storage.size()),1000,&count,&error);
  bool hazard=s==Scenario::CancelStillPending||s==Scenario::WaitFailedCancelStillPending;
  check(model.creates==1,"one event creation expected");check(model.closes==(s==Scenario::EventFailure?0u:1u),"event closure count changed");
  check(model.pending==hazard,"model pending state inconsistent");check(model.closedWhilePending==(hazard?1u:0u),"pending close observation inconsistent");
  if(hazard){++hazardousRows;check(!ok,"timed-out operation falsely returned success");check(model.cancelRequested&&model.cancels==1&&model.waits==2,"cancel/wait sequence differs");check(model.queries==0,"source unexpectedly proves terminal state after timeout");check(model.ovObserved,"actual helper did not issue with OVERLAPPED");}
  if(s==Scenario::Sync||s==Scenario::PendingSuccess)check(ok,"success control failed");else check(!ok,"failure control failed");
  rows.push_back({{"operation",operation==0?"write":operation==1?"read":"connect"},{"scenario",scenarioName(s)},{"helperReturned",ok},{"reportedError",error},{"modelOperationStillPendingAtReturn",model.pending},{"eventCloseCallsWhilePending",model.closedWhilePending},{"cancelCalls",model.cancels},{"waitCalls",model.waits},{"terminalResultQueries",model.queries},{"scope","Permitted-result model over actual production helper; no real pipe/event or device operation"}});
 }
 model={};model.scenario=Scenario::AlreadyConnected;DWORD error=999;check(inputHostWaitClientLocked(pipe,1000,&error),"already-connected control failed");check(error==ERROR_SUCCESS&&!model.pending&&model.creates==1&&model.closes==1,"already-connected resource control");
 model={};DWORD bytes=0;check(inputHostPipeIo(pipe,true,storage.data(),DWORD(storage.size()),1000,&bytes,&error),"ordinary write");check(inputHostPipeIo(pipe,false,storage.data(),DWORD(storage.size()),1000,&bytes,&error),"ordinary read");check(model.creates==2&&model.closes==2&&!model.pending,"ordinary source census changed");
 check(hazardousRows==6,"expected six conditional lifetime observations");
 std::cout<<json({{"pass",true},{"checks",checks},{"modeledCases",rows.size()+2},{"conditionalLifetimeGapRows",hazardousRows},{"rows",rows},{"realKernelDelayOrFaultReproduced",false},{"productionChanged",false},{"actualHelpersExtractedVerbatim",true}}).dump(2)<<'\n';return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
