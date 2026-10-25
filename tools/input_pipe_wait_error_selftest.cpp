// R37 permitted-result model over actual pipe helper bodies. No real pipe/event APIs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
enum class Case {Sync,PendingSuccess,PendingFailure,StartFailure,EventFailure,TimeoutPending,TimeoutCompleted,FailedCancelNotFound,FailedSecondWait,FailedSuccessfulCleanupClearsError};
struct Model {Case which;bool pending=false;DWORD last=0;unsigned waits=0,cancels=0,closesPending=0;std::vector<std::string> actions;};
static Model model{};
static DWORD FakeGetLastError(){return model.last;}
static HANDLE FakeCreateEventW(LPSECURITY_ATTRIBUTES,BOOL,BOOL,LPCWSTR){model.actions.push_back("event-create");if(model.which==Case::EventFailure){model.last=ERROR_NOT_ENOUGH_MEMORY;return nullptr;}return reinterpret_cast<HANDLE>(uintptr_t(0x1001));}
static BOOL FakeStart(DWORD size,DWORD* bytes){
 if(model.which==Case::Sync){*bytes=size;return TRUE;}
 if(model.which==Case::StartFailure){model.last=ERROR_BROKEN_PIPE;return FALSE;}
 model.pending=true;model.last=ERROR_IO_PENDING;return FALSE;
}
static BOOL FakeWriteFile(HANDLE,LPCVOID,DWORD size,LPDWORD bytes,LPOVERLAPPED){model.actions.push_back("write");return FakeStart(size,bytes);}
static BOOL FakeReadFile(HANDLE,LPVOID,DWORD size,LPDWORD bytes,LPOVERLAPPED){model.actions.push_back("read");return FakeStart(size,bytes);}
static BOOL FakeConnectNamedPipe(HANDLE,LPOVERLAPPED){model.actions.push_back("connect");DWORD unused=0;return FakeStart(0,&unused);}
static DWORD FakeWaitForSingleObject(HANDLE,DWORD duration){
 model.actions.push_back("wait:"+std::to_string(duration));++model.waits;
 if(model.waits==1){
  if(model.which==Case::PendingSuccess||model.which==Case::PendingFailure){model.pending=false;return WAIT_OBJECT_0;}
  if(model.which==Case::FailedCancelNotFound||model.which==Case::FailedSecondWait||model.which==Case::FailedSuccessfulCleanupClearsError){model.last=ERROR_INVALID_HANDLE;return WAIT_FAILED;}
  return WAIT_TIMEOUT;
 }
 if(model.which==Case::TimeoutPending)return WAIT_TIMEOUT;
 if(model.which==Case::FailedSecondWait){model.last=ERROR_ACCESS_DENIED;return WAIT_FAILED;}
 model.pending=false;
 if(model.which==Case::FailedSuccessfulCleanupClearsError)model.last=ERROR_SUCCESS;
 return WAIT_OBJECT_0;
}
static BOOL FakeCancelIoEx(HANDLE,LPOVERLAPPED){
 model.actions.push_back("cancel");++model.cancels;
 if(model.which==Case::FailedCancelNotFound||model.which==Case::TimeoutCompleted){model.pending=false;model.last=ERROR_NOT_FOUND;return FALSE;}
 return TRUE;
}
static BOOL FakeGetOverlappedResult(HANDLE,LPOVERLAPPED,LPDWORD bytes,BOOL){model.actions.push_back("result");if(model.which==Case::PendingFailure){model.last=ERROR_BROKEN_PIPE;return FALSE;}*bytes=7;return TRUE;}
static BOOL FakeCloseHandle(HANDLE){model.actions.push_back("event-close");model.closesPending+=model.pending;return TRUE;}
#define GetLastError FakeGetLastError
#define CreateEventW FakeCreateEventW
#define WriteFile FakeWriteFile
#define ReadFile FakeReadFile
#define ConnectNamedPipe FakeConnectNamedPipe
#define WaitForSingleObject FakeWaitForSingleObject
#define CancelIoEx FakeCancelIoEx
#define GetOverlappedResult FakeGetOverlappedResult
#define CloseHandle FakeCloseHandle
#include "actual-helpers.hpp"
#undef GetLastError
#undef CreateEventW
#undef WriteFile
#undef ReadFile
#undef ConnectNamedPipe
#undef WaitForSingleObject
#undef CancelIoEx
#undef GetOverlappedResult
#undef CloseHandle
static unsigned checks=0;
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct Result{bool ok;DWORD error,bytes;bool pending;unsigned closesPending;std::vector<std::string> actions;};
static Result invoke(bool after,Case c,int op,bool errorOut,bool bytesOut){
 model={};model.which=c;char buffer[16]{};DWORD error=123456,bytes=123456;HANDLE pipe=reinterpret_cast<HANDLE>(uintptr_t(0x2001));bool ok=false;
 if(op==2)ok=after?candidate::inputHostWaitClientLocked(pipe,1000,errorOut?&error:nullptr):baseline::inputHostWaitClientLocked(pipe,1000,errorOut?&error:nullptr);
 else ok=after?candidate::inputHostPipeIo(pipe,op==0,buffer,sizeof(buffer),1000,bytesOut?&bytes:nullptr,errorOut?&error:nullptr):baseline::inputHostPipeIo(pipe,op==0,buffer,sizeof(buffer),1000,bytesOut?&bytes:nullptr,errorOut?&error:nullptr);
 return {ok,error,bytes,model.pending,model.closesPending,model.actions};
}
static const char* name(Case c){switch(c){case Case::Sync:return "sync";case Case::PendingSuccess:return "pending-success";case Case::PendingFailure:return "pending-terminal-error";case Case::StartFailure:return "start-failure";case Case::EventFailure:return "event-failure";case Case::TimeoutPending:return "timeout-cancel-still-pending";case Case::TimeoutCompleted:return "timeout-cancel-not-found-completed";case Case::FailedCancelNotFound:return "wait-failed-cancel-not-found";case Case::FailedSecondWait:return "wait-failed-second-wait-overwrites";default:return "unspecified-success-last-error-cleared-control";}}
int main(){try{
 json rows=json::array();unsigned differences=0,falseSuccessModels=0,unchangedPendingCases=0;
 for(auto c:{Case::Sync,Case::PendingSuccess,Case::PendingFailure,Case::StartFailure,Case::EventFailure,Case::TimeoutPending,Case::TimeoutCompleted,Case::FailedCancelNotFound,Case::FailedSecondWait,Case::FailedSuccessfulCleanupClearsError})for(int op=0;op<3;++op)for(bool errorOut:{false,true})for(bool bytesOut:{false,true}){
  const auto b=invoke(false,c,op,errorOut,bytesOut),a=invoke(true,c,op,errorOut,bytesOut);
  check(a.actions==b.actions,"candidate changed IO/cancel/wait/close sequence");check(a.pending==b.pending&&a.closesPending==b.closesPending,"candidate changed resource-lifetime behavior");check(a.bytes==b.bytes,"candidate changed byte output");
  const bool failed=c==Case::FailedCancelNotFound||c==Case::FailedSecondWait||c==Case::FailedSuccessfulCleanupClearsError;
  if(failed){check(!a.ok,"candidate lost original WAIT_FAILED");if(errorOut)check(a.error==ERROR_INVALID_HANDLE,"candidate did not preserve original failure error");if(a.error!=b.error||a.ok!=b.ok)++differences;falseSuccessModels+=b.ok&&!a.ok;}
  else {check(a.ok==b.ok&&a.error==b.error,"normal/timeout output changed");}
  if(c==Case::TimeoutPending){check(a.pending&&a.closesPending==1,"existing R32 pending-close limitation was hidden");++unchangedPendingCases;}
  if(errorOut&&bytesOut)rows.push_back({{"operation",op==0?"write":op==1?"read":"connect"},{"scenario",name(c)},{"baselineResult",b.ok},{"candidateResult",a.ok},{"baselineError",b.error},{"candidateError",a.error},{"stillPendingAtReturn",a.pending},{"actions",a.actions},{"specificWindowsCleanupClearsErrorProven",false}});
 }
 check(differences==24,"expected primary/unspecified-model differences");check(falseSuccessModels==12,"expected unspecified-clobber controls");check(unchangedPendingCases==12,"expected unchanged pending-close controls");
 std::cout<<json({{"pass",true},{"checks",checks},{"pairedCases",120},{"changedResultModels",differences},{"falseSuccessModelsUnderUnspecifiedSuccessClobber",falseSuccessModels},{"r32PendingCloseCasesRemain",unchangedPendingCases},{"rows",rows},{"productionChanged",false},{"realKernelFaultOrKeyboardCauseProven",false}}).dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
