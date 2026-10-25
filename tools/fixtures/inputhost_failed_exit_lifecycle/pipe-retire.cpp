#include "source/native/input_host_pipe_transport.h"
#include <iostream>
int main(){unsigned cases=0,blockedBefore=0,eofAfter=0,rejectedAfter=0,failures=0;for(unsigned i=0;i<16;i++){
 using namespace ymcc::inputHost::pipeTransport;static Runtime& owner=*new Runtime;
 const std::wstring name=L"\\\\.\\pipe\\YMCC-independent-retire-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(i);
 HANDLE server=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,kMessageBytes,kMessageBytes,0,nullptr);
 if(server==INVALID_HANDLE_VALUE)return 10;DWORD error=0;if(!owner.adopt(server,name,error))return 11;
 HANDLE client=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);if(client==INVALID_HANDLE_VALUE)return 12;
 const auto connected=owner.run(server,Kind::Connect,nullptr,0,1000);if(!connected.ok)return 13;
 OVERLAPPED read{};read.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);char c=0;DWORD count=0;const BOOL began=ReadFile(client,&c,1,&count,&read);const DWORD firstError=began?ERROR_SUCCESS:GetLastError();
 if(began||firstError!=ERROR_IO_PENDING)return 14;
 ++cases;if(WaitForSingleObject(read.hEvent,10)==WAIT_TIMEOUT)++blockedBefore;else ++failures;
 owner.close(server);
 const DWORD wait=WaitForSingleObject(read.hEvent,1000);const BOOL readOk=GetOverlappedResult(client,&read,&count,FALSE);const DWORD readError=readOk?ERROR_SUCCESS:GetLastError();
 const bool eof=wait==WAIT_OBJECT_0&&((readOk&&count==0)||(!readOk&&(readError==ERROR_BROKEN_PIPE||readError==ERROR_PIPE_NOT_CONNECTED||readError==ERROR_NO_DATA)));
 if(eof)++eofAfter;else ++failures;
 const auto rejected=owner.run(server,Kind::Read,&c,1,0);if(!rejected.admitted&&rejected.error==ERROR_INVALID_HANDLE)++rejectedAfter;else ++failures;
 owner.close(server);owner.maintenance();const auto snap=owner.snapshot();if(snap.closeFailures!=0||snap.pending!=0)++failures;
 CloseHandle(read.hEvent);CloseHandle(client);
 }
 std::cout<<"{\"cases\":"<<cases<<",\"blockedBeforeDisconnect\":"<<blockedBefore<<",\"eofOrDisconnectAfterOwnedClose\":"<<eofAfter<<",\"commandsRejectedAfterClose\":"<<rejectedAfter<<",\"failures\":"<<failures<<",\"realHostOrSdkExecuted\":false,\"isolatedWindowsPipeExecuted\":true}\n";return failures?1:0;
}
