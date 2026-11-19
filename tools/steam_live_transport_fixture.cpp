#ifdef YMCC_BOUND_API_FIELD_ONLY
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <iostream>
#include "steam_live_cdp.h"
#include "fixtures/steam_existing_process_api_bound.inc"
int main(int argc,char** argv){
    const auto selected=existing_process_api_field::selected(argc,argv);
    return selected?*selected:2;
}
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <iostream>
#include <string>
#include <future>
#include <thread>
// Preinclude SDK declarations so only the product call, not its import, is wrapped.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <winhttp.h>
#include <iphlpapi.h>
#include <atomic>

#ifdef YMCC_SOCKET_OWNER_CONSTRUCTION_FIXTURE
#include <new>
#include <cstdlib>
#include <malloc.h>
// This entire allocator replacement exists only in the exact selected build.
static std::atomic<bool> socketOwnerArm{false};
static unsigned socketOwnerThrows=0,socketOwnerUpgradeSuccess=0;
static std::size_t socketOwnerAllocationBytes=0;
static void* socketOwnerFrames[24]{};static USHORT socketOwnerFrameCount=0;
static bool socketOwnerProbe=false;
static unsigned socketOwnerOpens[6]{},socketOwnerCloses[6]{};
static void* socketOwnerAllocate(std::size_t n,std::size_t alignment=0){
 if(socketOwnerArm.exchange(false,std::memory_order_relaxed)){
   ++socketOwnerThrows;socketOwnerAllocationBytes=n;
   socketOwnerFrameCount=CaptureStackBackTrace(0,24,socketOwnerFrames,nullptr);
   throw std::bad_alloc();
 }
 for(;;){void* p=alignment?_aligned_malloc(n?n:1,alignment):std::malloc(n?n:1);if(p)return p;auto h=std::get_new_handler();if(!h)throw std::bad_alloc();h();}
}
void* operator new(std::size_t n){return socketOwnerAllocate(n);}
void* operator new[](std::size_t n){return socketOwnerAllocate(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t a){return socketOwnerAllocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return socketOwnerAllocate(n,static_cast<std::size_t>(a));}
void operator delete(void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{_aligned_free(p);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new[](n,a);}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&)noexcept{_aligned_free(p);}
#endif

// Test-only kernel namespace; all SourceOwner read/drain bodies stay actual.
static HANDLE WINAPI fixtureOwnedSourceMutex(LPSECURITY_ATTRIBUTES sa,BOOL owner,LPCWSTR name){
 const std::wstring prefix=L"Local\\YeManCC.SteamSource.v1.";
 if(!name||std::wstring_view(name).find(prefix)!=0){SetLastError(ERROR_INVALID_NAME);return nullptr;}
 const auto scoped=L"Local\\YMCCFixture.SourceDrain."+std::to_wstring(GetCurrentProcessId())+L"."+std::wstring(name+prefix.size());
 return ::CreateMutexW(sa,owner,scoped.c_str());
}
#define CreateMutexW fixtureOwnedSourceMutex
#include "steam_source_owner.h"
#undef CreateMutexW
#include <cstring>
#include <limits>
#include <functional>
#include <map>
#include <set>
struct DocumentPopupLeaf {std::string href,origin="https://steamloopback.host",key="1700000000.125",finalHref,finalOrigin,finalKey;};
struct DocumentPopupSocket {std::string path;unsigned replyId=0;};
static bool documentPopupMapping=false;
static unsigned documentPopupNext=0,documentPopupUnexpected=0;
static std::map<std::string,DocumentPopupLeaf> documentPopupLeaves;
static std::map<HINTERNET,std::string> documentPopupRequests;
static std::map<HINTERNET,DocumentPopupSocket> documentPopupSockets;
static std::set<HINTERNET> documentPopupActive;
static std::function<void(const std::string&,unsigned)> documentPopupReceiveHook;
#ifdef YMCC_STEAM_DOCUMENT_READ_ONLY
static std::string documentPopupReply(HINTERNET);
#endif
static bool failureDiagnosticsProbe=false;
static unsigned failureExternalCalls=0,failureImageOpens=0,failureImageCloses=0,failureSnapshotOpens=0,failureFinalAdmissions=0;
static std::string failurePacket;
static std::string dispatchLastMessage;
static std::size_t failurePacketOffset=0;
static DWORD failureReceiveError=0,failureHttpStatus=0,failureHttpError=ERROR_WINHTTP_CANNOT_CONNECT;
static unsigned failureHttpSendAt=0,failureHttpReceiveAt=0;
static bool failureHttpQueryDenied=false,failureSocketClose=false;
static DWORD failureCloseQueryError=ERROR_INVALID_OPERATION;
static USHORT failureCloseStatus=1001;
static unsigned failureCloseQueryCalls=0;
static std::string failureCloseReason;
static bool documentFinalTargetsRead=false;
static std::string documentChangedTargets="[]";
static bool documentProbe=false,documentReplaced=false,documentTargetsChanged=false;
static unsigned documentReplyId=1,documentTargetReads=0;
static std::string documentTargets,documentExpression;
static std::string documentHref="https://steamloopback.host/index.html";
// Only the explicit listener inventory case enables these local API ports.
static bool listenerEndpointProbe=false;
static std::function<HANDLE(DWORD,BOOL,DWORD)> listenerEndpointOpen;
static std::function<BOOL(HANDLE,DWORD,LPWSTR,PDWORD)> listenerEndpointImage;
static std::function<BOOL(HANDLE)> listenerEndpointClose;
static std::function<NTSTATUS(SYSTEM_INFORMATION_CLASS,PVOID,ULONG,PULONG)> listenerEndpointQuery;
static std::function<DWORD(PVOID,PDWORD,BOOL,ULONG,TCP_TABLE_CLASS,ULONG)> listenerEndpointTcp;
static bool processIdentityProbe=false;
static bool dispatchProbe=false, dispatchExitOnUpgrade=false, dispatchOwner=true, dispatchSendFails=false;
static unsigned dispatchSends=0, dispatchHttpOpen=0, dispatchHttpClose=0, dispatchRead=0;
static std::function<void()> dispatchReplyHook;
static std::function<void()> modeDiscoveryHook;
static std::function<void()> dispatchUpgradeHook;
static std::string dispatchReplyValue=R"({"ok":true,"mutated":false})";
static HANDLE dispatchImageHandle(bool child){return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(child?0xC0D013:0xC0D012));}
static HINTERNET dispatchHttp(unsigned value){return reinterpret_cast<HINTERNET>(static_cast<std::uintptr_t>(0xC0E000+value));}
static bool dispatchHttpOwned(HINTERNET h){return (reinterpret_cast<std::uintptr_t>(h)>=0xC0E001&&reinterpret_cast<std::uintptr_t>(h)<=0xC0E005)||(documentPopupMapping&&reinterpret_cast<std::uintptr_t>(h)>=0xC0E100&&reinterpret_cast<std::uintptr_t>(h)<0xC0F000);}
static HINTERNET dispatchNewHttp(unsigned value){
#ifdef YMCC_SOCKET_OWNER_CONSTRUCTION_FIXTURE
 if(socketOwnerProbe&&value<6)++socketOwnerOpens[value];
#endif
++dispatchHttpOpen;return dispatchHttp(value);}
static HINTERNET WINAPI observedHttpOpen(LPCWSTR a,DWORD b,LPCWSTR c,LPCWSTR d,DWORD e){if(failureDiagnosticsProbe&&!dispatchProbe){++failureExternalCalls;return nullptr;}return dispatchProbe?dispatchNewHttp(1) : ::WinHttpOpen(a,b,c,d,e);}
static BOOL WINAPI observedHttpTimeouts(HINTERNET h,int a,int b,int c,int d){return dispatchHttpOwned(h)?TRUE : ::WinHttpSetTimeouts(h,a,b,c,d);}
static HINTERNET WINAPI observedHttpConnect(HINTERNET h,LPCWSTR a,INTERNET_PORT b,DWORD c){return dispatchHttpOwned(h)?dispatchNewHttp(2) : ::WinHttpConnect(h,a,b,c);}
static HINTERNET WINAPI observedHttpRequest(HINTERNET h,LPCWSTR a,LPCWSTR b,LPCWSTR c,LPCWSTR d,LPCWSTR* e,DWORD f){if(!dispatchHttpOwned(h))return ::WinHttpOpenRequest(h,a,b,c,d,e,f);if(documentProbe&&wcscmp(b,L"/json/list")==0)dispatchRead=0;if(documentPopupMapping&&wcscmp(b,L"/json/list")!=0){auto r=dispatchNewHttp(0x100+documentPopupNext++);std::string fixturePath;for(const wchar_t* ch=b;*ch;++ch)fixturePath.push_back(static_cast<char>(*ch));documentPopupRequests[r]=std::move(fixturePath);documentPopupActive.insert(r);return r;}return dispatchNewHttp(wcscmp(b,L"/json/list")==0?3:4);}
static BOOL WINAPI observedHttpOption(HINTERNET h,DWORD a,LPVOID b,DWORD c){return dispatchHttpOwned(h)?TRUE : ::WinHttpSetOption(h,a,b,c);}
static BOOL WINAPI observedHttpSend(HINTERNET h,LPCWSTR a,DWORD b,LPVOID c,DWORD d,DWORD e,DWORD_PTR f){if(dispatchHttpOwned(h)){if(failureDiagnosticsProbe&&h==dispatchHttp(failureHttpSendAt)){SetLastError(failureHttpError);return FALSE;}return TRUE;}if(failureDiagnosticsProbe){++failureExternalCalls;return FALSE;}return ::WinHttpSendRequest(h,a,b,c,d,e,f);}
static BOOL WINAPI observedHttpResponse(HINTERNET h,LPVOID a){if(dispatchHttpOwned(h)){if(failureDiagnosticsProbe&&h==dispatchHttp(failureHttpReceiveAt)){SetLastError(failureHttpError);return FALSE;}return TRUE;}if(failureDiagnosticsProbe){++failureExternalCalls;return FALSE;}return ::WinHttpReceiveResponse(h,a);}
static BOOL WINAPI observedHttpHeaders(HINTERNET h,DWORD a,LPCWSTR b,LPVOID c,LPDWORD d,LPDWORD e){if(!dispatchHttpOwned(h))return ::WinHttpQueryHeaders(h,a,b,c,d,e);if(failureDiagnosticsProbe&&h==dispatchHttp(4)&&failureHttpQueryDenied){SetLastError(failureHttpError);return FALSE;}*static_cast<DWORD*>(c)=h==dispatchHttp(3)?200:failureDiagnosticsProbe&&failureHttpStatus?failureHttpStatus:101;*d=sizeof(DWORD);return TRUE;}
static BOOL WINAPI observedHttpRead(HINTERNET h,LPVOID b,DWORD cap,LPDWORD n){
 if(!dispatchHttpOwned(h))return ::WinHttpReadData(h,b,cap,n);
 if(modeDiscoveryHook){auto hook=std::move(modeDiscoveryHook);modeDiscoveryHook={};hook();}
 const std::string data=R"([{"type":"page","title":"SharedJSContext","url":"https://steamloopback.host/index.html","webSocketDebuggerUrl":"ws://127.0.0.1:8080/devtools/page/ABC-123"}])";
 if(documentProbe){const auto bytes=documentTargetsChanged&&documentTargetReads++?documentChangedTargets:documentTargets;*n=dispatchRead++?0:static_cast<DWORD>(bytes.size());if(*n>cap)return FALSE;if(*n)memcpy(b,bytes.data(),*n);if(dispatchSends==6&&!*n)documentFinalTargetsRead=true;return TRUE;}
 *n=dispatchRead++?0:static_cast<DWORD>(data.size());if(*n>cap)return FALSE;if(*n)memcpy(b,data.data(),*n);return TRUE;
}
static DWORD WINAPI observedSocketQueryCloseStatus(HINTERNET h,USHORT* status,PVOID reason,DWORD capacity,DWORD* consumed){
 if(dispatchHttpOwned(h)){
   ++failureCloseQueryCalls;
   if(capacity!=WINHTTP_WEB_SOCKET_MAX_CLOSE_REASON_LENGTH){++failureExternalCalls;return ERROR_INVALID_PARAMETER;}
   *status=failureCloseQueryError==NO_ERROR?failureCloseStatus:0;
   *consumed=static_cast<DWORD>((std::min)(std::size_t(capacity),failureCloseReason.size()));
   if(*consumed)memcpy(reason,failureCloseReason.data(),*consumed);
   return failureCloseQueryError;
 }
 if(failureDiagnosticsProbe||dispatchProbe){++failureExternalCalls;return ERROR_INVALID_HANDLE;}
 return ::WinHttpWebSocketQueryCloseStatus(h,status,reason,capacity,consumed);
}
static HINTERNET WINAPI observedHttpUpgrade(HINTERNET h,DWORD_PTR a);
static DWORD WINAPI observedSocketSend(HINTERNET h,WINHTTP_WEB_SOCKET_BUFFER_TYPE a,PVOID b,DWORD c){if(!dispatchHttpOwned(h))return ::WinHttpWebSocketSend(h,a,b,c);++dispatchSends;dispatchLastMessage.assign(static_cast<char*>(b),c);if(documentProbe){const std::string request(static_cast<char*>(b),c);documentReplyId=request.find("\"id\":2")!=std::string::npos?2:1;if(documentReplyId==1)documentExpression=request;if(documentPopupMapping){auto it=documentPopupSockets.find(h);if(it==documentPopupSockets.end())++documentPopupUnexpected;else it->second.replyId=documentReplyId;} }return dispatchSendFails?ERROR_CONNECTION_ABORTED:NO_ERROR;}
static DWORD WINAPI observedSocketReceive(HINTERNET h,PVOID b,DWORD cap,LPDWORD n,WINHTTP_WEB_SOCKET_BUFFER_TYPE* kind){
 if(!dispatchHttpOwned(h))return ::WinHttpWebSocketReceive(h,b,cap,n,kind);
 if(failureDiagnosticsProbe){
   if(failureReceiveError)return failureReceiveError;
   if(failureSocketClose){*n=0;*kind=WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE;return NO_ERROR;}
   if(!failurePacket.empty()){if(dispatchReplyHook){auto hook=std::move(dispatchReplyHook);dispatchReplyHook={};hook();}const auto count=(std::min)(std::size_t(cap),failurePacket.size()-failurePacketOffset);memcpy(b,failurePacket.data()+failurePacketOffset,count);failurePacketOffset+=count;*n=static_cast<DWORD>(count);*kind=failurePacketOffset<failurePacket.size()?WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE:WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;return NO_ERROR;}
 }
 if(dispatchReplyHook){auto hook=std::move(dispatchReplyHook);dispatchReplyHook={};hook();}
#ifdef YMCC_STEAM_DOCUMENT_READ_ONLY
 if(documentPopupMapping){const auto data=documentPopupReply(h);*n=static_cast<DWORD>(data.size());if(*n>cap)return ERROR_INSUFFICIENT_BUFFER;memcpy(b,data.data(),*n);*kind=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;return NO_ERROR;}
#endif
 const std::string data=documentProbe?std::string("{\"id\":")+std::to_string(documentReplyId)+",\"result\":{\"result\":{\"type\":\"object\",\"value\":{\"sourceOrigin\":\"https://steamloopback.host\",\"sourceHref\":\""+documentHref+"\",\"documentKey\":\""+(documentReplaced&&documentReplyId==2?"successor":"original")+"\",\"protocol\":4,\"ready\":true,\"styles\":1,\"values\":[\"1\"],\"fileCounts\":[1]}}}}":std::string(R"({"id":1,"result":{"result":{"value":)")+dispatchReplyValue+"}}}";
 *n=static_cast<DWORD>(data.size());if(*n>cap)return ERROR_INSUFFICIENT_BUFFER;memcpy(b,data.data(),*n);*kind=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;return NO_ERROR;
}
static BOOL WINAPI observedHttpClose(HINTERNET h){if(!dispatchHttpOwned(h))return ::WinHttpCloseHandle(h);
#ifdef YMCC_SOCKET_OWNER_CONSTRUCTION_FIXTURE
 if(socketOwnerProbe){const auto i=reinterpret_cast<std::uintptr_t>(h)-0xC0E000;if(i<6)++socketOwnerCloses[i];}
#endif
++dispatchHttpClose;if(documentPopupMapping&&reinterpret_cast<std::uintptr_t>(h)>=0xC0E100){if(documentPopupActive.erase(h)!=1)++documentPopupUnexpected;}return TRUE;}

static bool documentChildOpen=true,documentChildTimes=true,documentChildImage=true;
static DWORD documentChildWait=WAIT_TIMEOUT,documentParentOverride=0;
static bool documentTcp6Denied=false;
static HANDLE documentChildHandle(){return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0xC0D014));}
static bool processIdentityOpen=true,processIdentityPath=true,processIdentityTimes=true,processIdentityAlive=true;
static DWORD processIdentityPid=424242;
static ULONGLONG processIdentityBirth=123456;
static unsigned processIdentityOpens=0,processIdentityCloses=0,processIdentityUnexpected=0;
static std::function<void()> processIdentityWaitHook;
static HANDLE processIdentityHandle(){return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0xC0D011));}
static HANDLE WINAPI observedIdentityOpen(DWORD access,BOOL inherit,DWORD pid){
 if(listenerEndpointProbe)return listenerEndpointOpen(access,inherit,pid);
 if(!processIdentityProbe)return ::OpenProcess(access,inherit,pid);
 if(dispatchProbe&&access==PROCESS_QUERY_LIMITED_INFORMATION&&!inherit&&(pid==processIdentityPid||pid==processIdentityPid+1)){if(failureDiagnosticsProbe)++failureImageOpens;return dispatchImageHandle(pid!=processIdentityPid);}
 if(dispatchProbe&&documentProbe&&access==(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION)&&!inherit&&pid==processIdentityPid+1){if(documentChildOpen){++processIdentityOpens;return documentChildHandle();}return nullptr;}
 ++processIdentityOpens;
 if(access!=(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION)||inherit||pid!=processIdentityPid){++processIdentityUnexpected;return nullptr;}
 return processIdentityOpen?processIdentityHandle():nullptr;
}
static BOOL WINAPI observedIdentityImage(HANDLE h,DWORD flags,LPWSTR output,PDWORD length){
 if(listenerEndpointProbe)return listenerEndpointImage(h,flags,output,length);
 if(h==documentChildHandle()){
   const std::wstring p=documentChildImage?L"P:\\YMCC-identity-fixture\\bin\\steamwebhelper.exe":L"P:\\Other\\steamwebhelper.exe";
   if(flags||!length||*length<p.size())return FALSE;std::copy(p.begin(),p.end(),output);*length=static_cast<DWORD>(p.size());return TRUE;
 }
 if(h==dispatchImageHandle(false)||h==dispatchImageHandle(true)){
   const std::wstring p=h==dispatchImageHandle(true)?L"P:\\YMCC-identity-fixture\\bin\\steamwebhelper.exe":L"P:\\YMCC-identity-fixture\\steam.exe";
   if(!length||*length<p.size())return FALSE;std::copy(p.begin(),p.end(),output);*length=static_cast<DWORD>(p.size());return TRUE;
 }
 if(h!=processIdentityHandle())return ::QueryFullProcessImageNameW(h,flags,output,length);
 const std::wstring path=L"P:\\YMCC-identity-fixture\\steam.exe";
 if(!processIdentityPath||flags||!length||*length<path.size()){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 std::copy(path.begin(),path.end(),output);*length=static_cast<DWORD>(path.size());return TRUE;
}
static BOOL WINAPI observedIdentityTimes(HANDLE h,LPFILETIME b,LPFILETIME e,LPFILETIME k,LPFILETIME u){
 if(h==documentChildHandle()){if(!documentChildTimes)return FALSE;*b={123457,0};*e={};*k={};*u={};return TRUE;}
 if(h!=processIdentityHandle())return ::GetProcessTimes(h,b,e,k,u);
 if(!processIdentityTimes)return FALSE;
 *b={static_cast<DWORD>(processIdentityBirth),static_cast<DWORD>(processIdentityBirth>>32)};*e={};*k={};*u={};return TRUE;
}
static HINTERNET WINAPI observedHttpUpgrade(HINTERNET h,DWORD_PTR a){if(!dispatchHttpOwned(h))return ::WinHttpWebSocketCompleteUpgrade(h,a);
#ifdef YMCC_SOCKET_OWNER_CONSTRUCTION_FIXTURE
 if(socketOwnerProbe){auto socket=dispatchNewHttp(5);++socketOwnerUpgradeSuccess;if(dispatchUpgradeHook)dispatchUpgradeHook();return socket;}
#endif
if(dispatchExitOnUpgrade)processIdentityAlive=false;if(dispatchUpgradeHook)dispatchUpgradeHook();if(documentPopupMapping){auto it=documentPopupRequests.find(h);if(it==documentPopupRequests.end()){++documentPopupUnexpected;return nullptr;}auto socket=dispatchNewHttp(0x100+documentPopupNext++);documentPopupSockets[socket]={it->second,0};documentPopupActive.insert(socket);return socket;}return dispatchNewHttp(5);}
static DWORD WINAPI observedIdentityWait(HANDLE h,DWORD ms){
 if(h==documentChildHandle()){if(ms){++processIdentityUnexpected;return WAIT_FAILED;}return documentChildWait;}
 if(h!=processIdentityHandle())return ::WaitForSingleObject(h,ms);
 if(ms){++processIdentityUnexpected;return WAIT_FAILED;}if(processIdentityWaitHook)processIdentityWaitHook();return processIdentityAlive?WAIT_TIMEOUT:WAIT_OBJECT_0;
}
static NTSTATUS NTAPI observedIdentityQuery(SYSTEM_INFORMATION_CLASS kind,PVOID data,ULONG bytes,PULONG returned){
 if(listenerEndpointProbe)return listenerEndpointQuery(kind,data,bytes,returned);
 if(processIdentityProbe){if(returned)*returned=0;return static_cast<NTSTATUS>(0xC0000003L);}
 using Q=NTSTATUS(NTAPI*)(SYSTEM_INFORMATION_CLASS,PVOID,ULONG,PULONG);
 static const auto real=reinterpret_cast<Q>(::GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQuerySystemInformation"));
 return real?real(kind,data,bytes,returned):static_cast<NTSTATUS>(0xC0000003L);
}
static FARPROC WINAPI observedIdentityProc(HMODULE module,LPCSTR name){
 if(reinterpret_cast<ULONG_PTR>(name)>0xffff&&std::strcmp(name,"NtQuerySystemInformation")==0&&module==GetModuleHandleW(L"ntdll.dll"))return reinterpret_cast<FARPROC>(observedIdentityQuery);
 return ::GetProcAddress(module,name);
}
static unsigned listenerToolhelpCalls = 0;
static DWORD listenerToolhelpFlags = 0, listenerToolhelpPid = 0;
static int listenerFallbackFault = 0;
static unsigned listenerFixtureCloses = 0;
static HANDLE listenerFixtureSnapshot() { return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0xC0D0F0)); }
static HANDLE WINAPI observedListenerToolhelp(DWORD flags, DWORD pid) {
 ++listenerToolhelpCalls; listenerToolhelpFlags = flags; listenerToolhelpPid = pid;
 if(processIdentityProbe){if(failureDiagnosticsProbe)++failureSnapshotOpens;return listenerFixtureSnapshot();}
 if (listenerFallbackFault == 1) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
 if (listenerFallbackFault) return listenerFixtureSnapshot();
 return ::CreateToolhelp32Snapshot(flags, pid);
}
static BOOL WINAPI observedListenerFirst(HANDLE h, PROCESSENTRY32W* row) {
 if (h != listenerFixtureSnapshot()) return ::Process32FirstW(h,row);
 if(processIdentityProbe){if(dispatchProbe){row->th32ProcessID=processIdentityPid+1;row->th32ParentProcessID=documentParentOverride?documentParentOverride:processIdentityPid;wcscpy_s(row->szExeFile,L"steamwebhelper.exe");return TRUE;}SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
 if (listenerFallbackFault == 2) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
 if (listenerFallbackFault == 4) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
 row->th32ProcessID=424241;row->th32ParentProcessID=424200;
 wcscpy_s(row->szExeFile,L"steamwebhelper.exe");return TRUE;
}
static BOOL WINAPI observedListenerNext(HANDLE h, PROCESSENTRY32W* row) {
 if (h != listenerFixtureSnapshot()) return ::Process32NextW(h,row);
 SetLastError((processIdentityProbe&&dispatchProbe)||listenerFallbackFault==5?ERROR_NO_MORE_FILES:ERROR_ACCESS_DENIED);return FALSE;
}
static BOOL WINAPI observedListenerClose(HANDLE h) {
 if(listenerEndpointProbe)return listenerEndpointClose(h);
 if(h==dispatchImageHandle(false)||h==dispatchImageHandle(true)){if(failureDiagnosticsProbe)++failureImageCloses;return TRUE;}
 if(h==processIdentityHandle()||h==documentChildHandle()){++processIdentityCloses;return TRUE;}
 if (h != listenerFixtureSnapshot()) return ::CloseHandle(h);
 ++listenerFixtureCloses;return TRUE;
}
static std::atomic<bool> admissionProbe{false};
static std::atomic<unsigned> admissionPortReads{0};
static std::function<void()> sourceDrainTcpHook;
static std::function<void()> discoveryCancellationHook;
static DWORD WINAPI observedAdmissionTcpTable(PVOID table, PDWORD size, BOOL order, ULONG family, TCP_TABLE_CLASS kind, ULONG reserved) {
 if(listenerEndpointProbe)return listenerEndpointTcp(table,size,order,family,kind,reserved);
 if(dispatchProbe){
   ++admissionPortReads;
   if(sourceDrainTcpHook)sourceDrainTcpHook();
   if(discoveryCancellationHook)discoveryCancellationHook();
   if(family==23){if(documentTcp6Denied)return ERROR_ACCESS_DENIED;const auto needed=static_cast<DWORD>(FIELD_OFFSET(MIB_TCP6TABLE_OWNER_PID,table));
     if(!table||*size<needed){*size=needed;return ERROR_INSUFFICIENT_BUFFER;}
     std::memset(table,0,needed);*size=needed;return NO_ERROR;
   }
   if(family!=2){*size=0;return ERROR_NO_DATA;}
   const auto needed=static_cast<DWORD>(sizeof(MIB_TCPTABLE_OWNER_PID));
   if(!table||*size<needed){*size=needed;return ERROR_INSUFFICIENT_BUFFER;}
   auto* t=static_cast<MIB_TCPTABLE_OWNER_PID*>(table);*t={};t->dwNumEntries=1;
   auto& r=t->table[0];r.dwLocalAddr=0x0100007f;r.dwLocalPort=0x901f;r.dwOwningPid=processIdentityPid+1;*size=needed;return NO_ERROR;
 }
 if(admissionProbe.load()) { ++admissionPortReads; return ERROR_ACCESS_DENIED; }
 return ::GetExtendedTcpTable(table,size,order,family,kind,reserved);
}
#define OpenProcess observedIdentityOpen
#define QueryFullProcessImageNameW observedIdentityImage
#define GetProcessTimes observedIdentityTimes
#define WaitForSingleObject observedIdentityWait
#define GetProcAddress observedIdentityProc
#define CreateToolhelp32Snapshot observedListenerToolhelp
#define Process32FirstW observedListenerFirst
#define Process32NextW observedListenerNext
#define CloseHandle observedListenerClose
#define GetExtendedTcpTable observedAdmissionTcpTable
#define WinHttpOpen observedHttpOpen
#define WinHttpSetTimeouts observedHttpTimeouts
#define WinHttpConnect observedHttpConnect
#define WinHttpOpenRequest observedHttpRequest
#define WinHttpSetOption observedHttpOption
#define WinHttpSendRequest observedHttpSend
#define WinHttpReceiveResponse observedHttpResponse
#define WinHttpQueryHeaders observedHttpHeaders
#define WinHttpReadData observedHttpRead
#define WinHttpWebSocketCompleteUpgrade observedHttpUpgrade
#define WinHttpWebSocketSend observedSocketSend
#define WinHttpWebSocketReceive observedSocketReceive
#define WinHttpWebSocketQueryCloseStatus observedSocketQueryCloseStatus
#define WinHttpCloseHandle observedHttpClose
#include "steam_failure_diagnostics.h"
#if defined(YMCC_FAILURE_OLD_SOURCE) || defined(YMCC_WEBSOCKET_CLOSE_PUBLIC_CONTROL)
#include "../oldnative/steam_live_cdp.h"
#else
#include "steam_live_cdp.h"
#if __has_include("steam_command_coordinator.h")
#include "steam_command_coordinator.h"
#define YMCC_FORMAL_COMMAND_PROVIDER 1
#endif
#endif
#ifdef YMCC_STEAM_DOCUMENT_READ_ONLY
#include "steam_document_read.h"
#include <fstream>
#endif
#undef WinHttpOpen
#undef WinHttpSetTimeouts
#undef WinHttpConnect
#undef WinHttpOpenRequest
#undef WinHttpSetOption
#undef WinHttpSendRequest
#undef WinHttpReceiveResponse
#undef WinHttpQueryHeaders
#undef WinHttpReadData
#undef WinHttpWebSocketCompleteUpgrade
#undef WinHttpWebSocketSend
#undef WinHttpWebSocketReceive
#undef WinHttpWebSocketQueryCloseStatus
#undef WinHttpCloseHandle
#undef OpenProcess
#undef QueryFullProcessImageNameW
#undef GetProcessTimes
#undef WaitForSingleObject
#undef GetProcAddress
#undef CreateToolhelp32Snapshot
#undef Process32FirstW
#undef Process32NextW
#undef CloseHandle
#undef GetExtendedTcpTable
#include "decky_sidebar_manual_repair.h"
using namespace ymcc::steamlive;
static unsigned listenerChecks = 0;
static void expect(bool ok,const char* message) {++listenerChecks;if(!ok)throw std::runtime_error(message);std::cout<<"PASS "<<message<<"\n";}
static Json target() {return {{"type","page"},{"title","SharedJSContext"},{"url","https://steamloopback.host/index.html"},{"webSocketDebuggerUrl","ws://127.0.0.1:8080/devtools/page/ABC-123"}};}
static constexpr unsigned short testPort=8080;
static const Endpoint testEndpoint{1,testPort,L"127.0.0.1"};
static bool rejected(const Json& list) {try {targetPath(list,testEndpoint);return false;}catch(...){return true;}}
// These NT rows are hints only. No process image, socket, Steam, or CDP port is mocked.
#include "steam_endpoint_discovery_cases.inc"
namespace listenerTopologyFixture {
using Row = SYSTEM_BASICPROCESS_INFORMATION;
struct Input { std::uintptr_t pid, parent; ULONG64 sequence; std::wstring name; };
enum class Reply { valid, unsupported, denied, zeroLength, truncated, badOffset, badName, oddName, oversizedPid, oversizedParent, resizeOnce };
static std::vector<Input> inputs;
static Reply reply = Reply::valid;
static unsigned queryCalls = 0;
static bool queryClassValid = true;
static std::size_t aligned(std::size_t value) { return (value + alignof(Row) - 1) & ~(alignof(Row) - 1); }
static NTSTATUS NTAPI query(SYSTEM_INFORMATION_CLASS info, PVOID buffer, ULONG capacity, PULONG returned) {
 ++queryCalls; queryClassValid = queryClassValid && static_cast<unsigned>(info) == 252;
 *returned = 0;
 if (reply == Reply::unsupported) return static_cast<NTSTATUS>(0xC0000003u);
 if (reply == Reply::denied) return static_cast<NTSTATUS>(0xC0000022u);
 if (reply == Reply::zeroLength) return 0;
 if (reply == Reply::resizeOnce && queryCalls == 1) { *returned = capacity + 1; return static_cast<NTSTATUS>(0xC0000004u); }
 std::size_t bytes = 0;
 for (const auto& input : inputs) bytes += aligned(sizeof(Row) + input.name.size() * sizeof(wchar_t));
 *returned = static_cast<ULONG>(bytes);
 if (capacity < bytes) return static_cast<NTSTATUS>(0xC0000004u);
 std::memset(buffer, 0, bytes);
 std::size_t offset = 0;
 for (std::size_t i = 0; i < inputs.size(); ++i) {
   const auto& input = inputs[i]; const auto extent = aligned(sizeof(Row) + input.name.size() * sizeof(wchar_t));
   Row row{};
   row.NextEntryOffset = i + 1 == inputs.size() ? 0 : static_cast<ULONG>(extent);
   row.UniqueProcessId = reinterpret_cast<HANDLE>(input.pid);
   row.InheritedFromUniqueProcessId = reinterpret_cast<HANDLE>(input.parent);
   row.SequenceNumber = input.sequence;
   row.ImageName.Length = row.ImageName.MaximumLength = static_cast<USHORT>(input.name.size() * sizeof(wchar_t));
   row.ImageName.Buffer = reinterpret_cast<wchar_t*>(static_cast<unsigned char*>(buffer) + offset + sizeof(Row));
   std::memcpy(row.ImageName.Buffer, input.name.data(), row.ImageName.Length);
   if (i + 1 == inputs.size()) {
     if (reply == Reply::badOffset) row.NextEntryOffset = 1;
     if (reply == Reply::badName) row.ImageName.Buffer = reinterpret_cast<wchar_t*>(static_cast<unsigned char*>(buffer) + bytes + sizeof(wchar_t));
     if (reply == Reply::oddName) row.ImageName.Length = 1;
     if (reply == Reply::oversizedPid) row.UniqueProcessId = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>((std::numeric_limits<DWORD>::max)()) + 1);
     if (reply == Reply::oversizedParent) row.InheritedFromUniqueProcessId = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>((std::numeric_limits<DWORD>::max)()) + 1);
   }
   std::memcpy(static_cast<unsigned char*>(buffer) + offset, &row, sizeof(row));
   offset += extent;
 }
 if (reply == Reply::truncated) *returned = sizeof(Row) - 1;
 return 0;
}
static void reset(Reply value = Reply::valid) {
 reply = value; queryCalls = 0; queryClassValid = true;
 listenerToolhelpCalls = 0; listenerToolhelpFlags = listenerToolhelpPid = 0;
}
static void run() {
 const auto started = std::chrono::steady_clock::now();
 // Independent real Toolhelp reference only enumerates processes; it never contacts Steam.
 std::map<DWORD,DWORD> reference;
 const HANDLE control = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
 expect(control != INVALID_HANDLE_VALUE, "listener topology read-only Toolhelp control opens");
 PROCESSENTRY32W process{sizeof(process)};
 const bool first = Process32FirstW(control, &process) != FALSE;
 if (first) do { reference[process.th32ProcessID] = process.th32ParentProcessID; } while (Process32NextW(control, &process));
 const bool closed = CloseHandle(control) != FALSE;
 expect(first && closed && reference.count(GetCurrentProcessId()) == 1, "listener topology control enumerates self and closes its handle");
 const auto self = GetCurrentProcessId(), selfParent = reference.at(self);
 inputs = {{4000,100,11,L"steam.exe"},{5000,200,12,L"steam.exe"},{8001,4000,13,L"steamwebhelper.exe"},{8002,5000,14,L"steamwebhelper.exe"}};
 const std::map<DWORD,DWORD> expected{{4000,100},{5000,200},{8001,4000},{8002,5000}};
 reset(); const auto basic = listenerParents(query);
 expect(basic == expected, "listener Basic snapshot preserves distinct parent PIDs for identical executable names");
 expect(queryCalls == 1 && queryClassValid && listenerToolhelpCalls == 0, "valid Basic query uses class 252 and never calls Toolhelp");
 expect(listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\steamwebhelper.exe",L"S:\\Steam\\steam.exe",basic.at(8001),4000), "fresh Basic topology retains valid expected Steam parent admission");
 expect(!listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\steamwebhelper.exe",L"S:\\Steam\\steam.exe",basic.at(8002),4000), "Basic topology cannot admit same-name helper from a different Steam PID");
 expect(!listenerProcessBound(L"S:\\Steam",L"S:\\Other\\steamwebhelper.exe",L"S:\\Steam\\steam.exe",basic.at(8001),4000), "Basic parent hint cannot admit same-name executable outside Steam root");
 expect(!listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\steamwebhelper.exe",L"S:\\Other\\steam.exe",basic.at(8001),4000), "Basic parent hint cannot substitute for current parent image validation");
 expect(!listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\fake.exe",L"S:\\Steam\\steam.exe",basic.at(8001),4000), "Basic topology cannot admit a renamed helper image");
 // Every call gets a new table: order, same PID/new instance, parent and PID changes.
 std::reverse(inputs.begin(), inputs.end()); reset();
 expect(listenerParents(query) == expected && queryCalls == 1 && listenerToolhelpCalls == 0, "Basic topology is independent of row ordering");
 inputs = {{8001,5000,99,L"steamwebhelper.exe"},{9001,4000,100,L"steamwebhelper.exe"},{5000,300,101,L"steam.exe"}};
 reset(); const auto changed = listenerParents(query);
 expect(changed == std::map<DWORD,DWORD>{{8001,5000},{9001,4000},{5000,300}} && queryCalls == 1 && listenerToolhelpCalls == 0, "each Basic lookup sees replaced PIDs and changed parents without cached old rows");
 expect(!listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\steamwebhelper.exe",L"S:\\Steam\\steam.exe",changed.at(8001),4000), "reused helper PID with a different parent loses old expected-PID admission");
 expect(listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\steamwebhelper.exe",L"S:\\Steam\\steam.exe",changed.at(9001),4000), "new helper PID still requires and accepts its verified current parent");
 inputs = {{0,0,0,L""},{(std::numeric_limits<DWORD>::max)(),(std::numeric_limits<DWORD>::max)(),1,L"edge.exe"}};
 reset(); const auto edges = listenerParents(query);
 expect(edges == std::map<DWORD,DWORD>{{0,0},{(std::numeric_limits<DWORD>::max)(),(std::numeric_limits<DWORD>::max)()}} && listenerToolhelpCalls == 0, "Basic parent and child DWORD boundary values are preserved without narrowing");
 // Corrupt the second row, after a valid row, so partial Basic tables cannot leak.
 inputs = {{8001,4000,11,L"steamwebhelper.exe"},{8002,5000,12,L"steamwebhelper.exe"}};
 for (const auto [failure,label] : std::vector<std::pair<Reply,const char*>>{
      {Reply::unsupported,"unknown Basic class falls back to actual Toolhelp"},
      {Reply::denied,"failed Basic query falls back to actual Toolhelp"},
      {Reply::zeroLength,"empty Basic reply falls back to actual Toolhelp"},
      {Reply::truncated,"truncated Basic reply falls back to actual Toolhelp"},
      {Reply::badOffset,"corrupt Basic row offset falls back to actual Toolhelp"},
      {Reply::badName,"out-of-buffer Basic image pointer falls back to actual Toolhelp"},
      {Reply::oddName,"malformed Basic image length falls back to actual Toolhelp"},
      {Reply::oversizedPid,"out-of-range Basic PID falls back without truncation"},
      {Reply::oversizedParent,"out-of-range Basic parent PID falls back without truncation"}}) {
   reset(failure); const auto fallback = listenerParents(query);
   expect(queryCalls == 1 && queryClassValid && listenerToolhelpCalls == 1 && listenerToolhelpFlags == TH32CS_SNAPPROCESS && listenerToolhelpPid == 0 && fallback.count(self) == 1 && fallback.at(self) == selfParent, label);
   reset();
   expect(listenerParents(query) == std::map<DWORD,DWORD>{{8001,4000},{8002,5000}} && queryCalls == 1 && listenerToolhelpCalls == 0, "valid next Basic call recovers fresh topology after query or parse failure");
 }
 reset(); const auto legacy = listenerParents(nullptr);
 expect(queryCalls == 0 && listenerToolhelpCalls == 1 && listenerToolhelpFlags == TH32CS_SNAPPROCESS && listenerToolhelpPid == 0 && legacy.count(self) && legacy.at(self) == selfParent, "null Basic query retains the actual original Toolhelp parent table");
 reset(Reply::resizeOnce);
 expect(listenerParents(query) == std::map<DWORD,DWORD>{{8001,4000},{8002,5000}} && queryCalls == 2 && queryClassValid && listenerToolhelpCalls == 0, "Basic buffer resize retry recovers without Toolhelp");
 reset();
 { ymcc::gameInventory::BasicSnapshot held(query);
   expect(held.valid(), "outer Basic snapshot is valid before reentrant topology lookup");
   const auto nested = listenerParents(query);
   expect(queryCalls == 1 && listenerToolhelpCalls == 1 && nested.count(self) && nested.at(self) == selfParent, "reentrant Basic storage falls back to actual Toolhelp without overwriting outer rows");
   expect(held.entries().size() == 2 && held.entries()[0].pid == 8001 && held.entries()[0].parentPid == 4000, "reentrant fallback preserves outer Basic snapshot parent hints");
 }
 reset();
 expect(listenerParents(query) == std::map<DWORD,DWORD>{{8001,4000},{8002,5000}} && listenerToolhelpCalls == 0, "Basic lookup recovers after outer snapshot releases storage");
 // Read-only live comparison binds only this fixture process across fresh snapshots.
 auto realQuery = ymcc::gameInventory::resolveQuery(); bool supported = false; std::size_t liveRows = 0;
 { ymcc::gameInventory::BasicSnapshot actual(realQuery); supported = actual.valid(); if(supported) liveRows = actual.entries().size(); }
 reset(); const auto live = listenerParents();
 expect(live.count(self) == 1 && live.at(self) == selfParent, "default live listener topology agrees with independent Toolhelp self-parent control");
 expect(listenerToolhelpCalls == (supported ? 0u : 1u), "default live Basic support selects zero Toolhelp calls or the original fallback");
 // Every production Basic call must publish through the one shared owner.
 reset();
 const auto ownerBefore=ymcc::processInventory::read(query,ymcc::processInventory::currentTime,0,false);
 const auto heldGeneration=ownerBefore->generation;
 const auto ownerParents=listenerParents(query);
 const auto ownerAfter=ymcc::processInventory::read(query,ymcc::processInventory::currentTime,250,false);
 expect(ownerParents==std::map<DWORD,DWORD>{{8001,4000},{8002,5000}} && ownerAfter->generation==heldGeneration+1 && queryCalls==2,
     "listener Basic discovery advances the single owner and subsequent consumers reuse it");
 expect(ownerBefore->generation==heldGeneration && ownerBefore->rows.size()==2 && ownerBefore->rows[0].parentPid==4000,
     "listener refresh leaves an already-held owner receipt immutable");
 for(int fault: {1,2,3}) {
   reset(Reply::unsupported);listenerFallbackFault=fault;const auto closedBefore=listenerFixtureCloses;bool refused=false;
   try { (void)listenerParents(query); } catch(const std::runtime_error& error) { refused=std::string(error.what())=="live-debug-unavailable"; }
   listenerFallbackFault=0;
   expect(refused,"incomplete fallback never supplies partial listener parent authority");
   expect(queryCalls==1&&listenerToolhelpCalls==1&&listenerFixtureCloses==closedBefore+(fault==1?0:1),
       "failed fallback has one producer and closes each owned snapshot exactly once");
 }
 reset(Reply::unsupported);listenerFallbackFault=4;const auto closedBefore=listenerFixtureCloses;
 const auto empty=listenerParents(query);listenerFallbackFault=0;
 expect(empty.empty()&&listenerToolhelpCalls==1&&listenerFixtureCloses==closedBefore+1,
     "complete empty owner fallback remains a valid empty parent table");
 reset();
 std::cout << "LISTENER_TOPOLOGY_OBSERVATION " << Json{{"basicSupported",supported},{"basicRows",liveRows},{"listenerRows",live.size()},{"toolhelpCalls",listenerToolhelpCalls},{"pid",self},{"parentPid",selfParent},{"readOnly",true},{"steamCdpAccess",false},{"elapsedMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()}}.dump() << "\n";
}
// Additional boundaries of the original listener inventory consumer.
static void endpointParentPrefilter(bool costOnly) {
 const auto checksBefore=listenerChecks;
 std::map<DWORD,std::wstring> images;
 std::map<HANDLE,std::wstring> held;
 std::vector<MIB_TCPROW_OWNER_PID> v4;
 std::vector<MIB_TCP6ROW_OWNER_PID> v6;
 unsigned opens=0,imageReads=0,closes=0,tcpReads=0,deniedPid=0;
 bool realImages=false;
 listenerEndpointOpen=[&](DWORD access,BOOL inherit,DWORD pid)->HANDLE{
   ++opens;expect(access==PROCESS_QUERY_LIMITED_INFORMATION&&!inherit,"listener image access remains read-only");
   if(pid==deniedPid){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
   auto h=::OpenProcess(access,inherit,GetCurrentProcessId());
   if(h)held.emplace(h,images.count(pid)?images.at(pid):L"Q:\\Other\\service.exe");
   return h;
 };
 listenerEndpointImage=[&](HANDLE h,DWORD flags,LPWSTR out,PDWORD length)->BOOL{
   ++imageReads;
   if(realImages)return ::QueryFullProcessImageNameW(h,flags,out,length);
   const auto& image=held.at(h);
   if(flags||!length||*length<image.size()){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
   std::copy(image.begin(),image.end(),out);*length=static_cast<DWORD>(image.size());return TRUE;
 };
 listenerEndpointClose=[&](HANDLE h)->BOOL{
   if(held.erase(h))++closes;
   return ::CloseHandle(h);
 };
 listenerEndpointQuery=[](SYSTEM_INFORMATION_CLASS kind,PVOID data,ULONG bytes,PULONG returned){return query(kind,data,bytes,returned);};
 listenerEndpointTcp=[&](PVOID out,PDWORD size,BOOL order,ULONG family,TCP_TABLE_CLASS kind,ULONG reserved)->DWORD{
   ++tcpReads;
   if(order||kind!=TCP_TABLE_OWNER_PID_LISTENER||reserved)return ERROR_INVALID_PARAMETER;
   const auto copy=[&](const auto& rows,auto* table)->DWORD{
     using Table=std::remove_pointer_t<decltype(table)>;
     using TableRow=typename std::decay_t<decltype(rows)>::value_type;
     const auto required=static_cast<DWORD>(offsetof(Table,table)+rows.size()*sizeof(TableRow));
     if(!out||*size<required){*size=required;return ERROR_INSUFFICIENT_BUFFER;}
     auto* result=static_cast<Table*>(out);result->dwNumEntries=static_cast<DWORD>(rows.size());
     if(!rows.empty())std::memcpy(result->table,rows.data(),rows.size()*sizeof(TableRow));
     *size=required;return NO_ERROR;
   };
   if(family==2)return copy(v4,static_cast<MIB_TCPTABLE_OWNER_PID*>(nullptr));
   if(family==23)return copy(v6,static_cast<MIB_TCP6TABLE_OWNER_PID*>(nullptr));
   return ERROR_INVALID_PARAMETER;
 };
 struct PortsGuard {~PortsGuard(){listenerEndpointProbe=false;listenerEndpointOpen={};listenerEndpointImage={};listenerEndpointClose={};listenerEndpointQuery={};listenerEndpointTcp={};ymcc::processInventory::invalidate();}} guard;
 listenerEndpointProbe=true;
 const auto port=[](unsigned short p){return static_cast<DWORD>(((p&0xff)<<8)|(p>>8));};
 const auto ipv4=[&](DWORD pid,unsigned short p,bool exposed=false){MIB_TCPROW_OWNER_PID r{};r.dwOwningPid=pid;r.dwLocalAddr=exposed?0:0x0100007f;r.dwLocalPort=port(p);v4.push_back(r);};
 const auto ipv6=[&](DWORD pid,unsigned short p,bool exposed=false){MIB_TCP6ROW_OWNER_PID r{};r.dwOwningPid=pid;r.ucLocalAddr[15]=exposed?0:1;r.dwLocalPort=port(p);v6.push_back(r);};
 const std::wstring root=L"P:\\YMCC-identity-fixture";
 Json observations=Json::array();
 const auto resetCase=[&]{expect(held.empty(),"previous listener image handles were returned");opens=imageReads=closes=tcpReads=0;reset();};
 const auto checkCase=[&](const char* name,DWORD expected,const std::vector<Endpoint>& want,const char* reason=nullptr){
   resetCase();std::vector<Endpoint> got;std::string error;
   try{got=listeners(root,expected);}catch(const std::runtime_error& e){error=e.what();}
   expect(got==want&&error==(reason?reason:""),name);
   expect(opens==closes&&held.empty(),"listener closes all successfully opened image handles");
   expect(queryCalls==1&&listenerToolhelpCalls==0&&tcpReads==4,"listener retains one fresh topology and both address-family reads");
   observations.push_back(Json{{"case",name},{"opens",opens},{"imageReads",imageReads},{"closes",closes},{"tcpReads",tcpReads},{"basicQueries",queryCalls},{"resultCount",got.size()},{"reason",error}});
 };
 if(costOnly){
   const auto self=GetCurrentProcessId();inputs={{self,self,1,L"fixture.exe"}};ipv4(self,8080);realImages=true;
   // Only this fixture process is opened/read; the port table is an inert local port.
   opens=imageReads=closes=tcpReads=0;reset();
   // Per-call assertions/printing would dominate the benchmark; validate flags once above.
   listenerEndpointOpen=[&](DWORD access,BOOL inherit,DWORD pid)->HANDLE{
     if(access!=PROCESS_QUERY_LIMITED_INFORMATION||inherit||pid!=self)throw std::runtime_error("cost fixture attempted a foreign process");
     ++opens;auto h=::OpenProcess(access,inherit,pid);if(h)held.emplace(h,L"");return h;
   };
   constexpr unsigned count=2000;
   FILETIME b{},e{},k0{},u0{},k1{},u1{};
   const auto ticks=[](FILETIME f){return (ULONGLONG(f.dwHighDateTime)<<32)|f.dwLowDateTime;};
   expect(GetThreadTimes(GetCurrentThread(),&b,&e,&k0,&u0)!=FALSE,"cost thread times open");
   const auto started=std::chrono::steady_clock::now();
   for(unsigned i=0;i<count;++i){bool rejected=false;try{(void)listeners(root,self+1);}catch(const std::runtime_error& error){rejected=std::string(error.what())=="live-debug-unavailable";}if(!rejected)throw std::runtime_error("wrong parent cost control admitted a port");}
   const auto wallMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
   expect(GetThreadTimes(GetCurrentThread(),&b,&e,&k1,&u1)!=FALSE,"cost thread times close");
   expect(opens==closes&&held.empty()&&tcpReads==count*4&&queryCalls==count,"cost keeps each final-read boundary and returns all image handles");
   std::cout<<"ENDPOINT_PARENT_COST "<<Json{{"calls",count},{"opens",opens},{"imageReads",imageReads},{"closes",closes},{"topologyQueries",queryCalls},{"tcpReads",tcpReads},{"wallMs",wallMs},{"cpuMs",(ticks(k1)+ticks(u1)-ticks(k0)-ticks(u0))/10000.0},{"realProcessQueried","self only"},{"realSteamOperations",0},{"wholeProcessCpuMeasured",false}}.dump()<<'\n';return;
 }
 inputs={{4000,1,1,L"steam.exe"},{8001,4000,2,L"steamwebhelper.exe"}};
 images[4000]=root+L"\\steam.exe";images[8001]=root+L"\\bin\\steamwebhelper.exe";ipv4(8001,8080);
 for(DWORD i=0;i<64;++i){inputs.push_back({9000+i,5000+i,10+i,L"steamwebhelper.exe"});images[9000+i]=images[8001];images[5000+i]=images[4000];ipv4(9000+i,9000+static_cast<unsigned short>(i));}
 checkCase("mixed-unrelated-parents",4000,{{8001,8080,L"127.0.0.1"}});
 reset();
 const auto allParents=listenerParents(query);
 const auto scopedParents=listenerParents(query,4000);
 expect(allParents.size()==66 && scopedParents==std::map<DWORD,DWORD>{{8001,4000}} && queryCalls==2 && listenerToolhelpCalls==0,
     "parent projection drops unrelated rows without changing fresh acquisition");
 v4.clear();v6.clear();inputs={{8001,4000,2,L"steamwebhelper.exe"},{8002,5000,3,L"steamwebhelper.exe"}};
 images[8002]=images[8001];images[5000]=images[4000];ipv4(8001,8080);ipv6(8001,8080);
 checkCase("same-helper-dual-family",4000,{{8001,8080,L"127.0.0.1"},{8001,8080,L"::1"}});
 inputs[0].parent=5000;
 checkCase("same-pid-new-parent-next-read",4000,{},"live-debug-unavailable");
 inputs[0].parent=4000;images[8001]=L"Q:\\Other\\steamwebhelper.exe";
 checkCase("expected-parent-foreign-child-image",4000,{},"live-debug-unavailable");
 images[8001]=root+L"\\bin\\steamwebhelper.exe";images[4000]=L"Q:\\Other\\steam.exe";
 checkCase("expected-parent-foreign-parent-image",4000,{},"live-debug-unavailable");
 images[4000]=root+L"\\steam.exe";deniedPid=8001;
 // An image open can legitimately fail, so close count equals successful opens.
 resetCase();bool refused=false;try{(void)listeners(root,4000);}catch(const std::runtime_error& e){refused=std::string(e.what())=="live-debug-unavailable";}
 expect(refused&&held.empty()&&opens==closes+1,"denied child image still refuses and closes successful parent image handles");deniedPid=0;
 observations.push_back(Json{{"case","denied-child-image"},{"opens",opens},{"closes",closes},{"tcpReads",tcpReads},{"basicQueries",queryCalls},{"resultCount",0},{"reason","live-debug-unavailable"}});
 inputs={{7777,5000,4,L"unrelated.exe"}};checkCase("missing-parent-row",4000,{},"live-debug-unavailable");
 inputs={{8001,4000,2,L"steamwebhelper.exe"},{8002,5000,3,L"steamwebhelper.exe"}};
 v6.clear();ipv4(8002,8081);
 checkCase("legacy-unspecified-steam-pid",0,{{8001,8080,L"127.0.0.1"},{8002,8081,L"127.0.0.1"}});
 ipv4(8001,8080,true);
 checkCase("ipv4-exposure-revokes-port",4000,{},"live-debug-not-loopback");
 v4.clear();v6.clear();ipv4(8001,8080);ipv6(8001,8080,true);
 checkCase("ipv6-exposure-revokes-ipv4",4000,{},"live-debug-not-loopback");
 v4.clear();v6.clear();ipv6(8001,8080);ipv4(8001,8080,true);
 checkCase("ipv4-exposure-revokes-ipv6",4000,{},"live-debug-not-loopback");
 v4.clear();v6.clear();inputs={{8003,4000,99,L"steamwebhelper.exe"}};images[8003]=images[8001];ipv4(8003,8080);
 checkCase("successor-helper-new-pid-same-port",4000,{{8003,8080,L"127.0.0.1"}});
 inputs.push_back({8003,5000,100,L"steamwebhelper.exe"});
 checkCase("duplicate-pid-last-row-foreign",4000,{},"live-debug-unavailable");
 std::reverse(inputs.begin(),inputs.end());
 checkCase("duplicate-pid-last-row-expected",4000,{{8003,8080,L"127.0.0.1"}});
 listenerEndpointProbe=false; // Use the inventory fixture's synthetic Toolhelp handle and close counter.
 const auto fallbackProjection=[&](DWORD expected){
   reset(Reply::unsupported);listenerFallbackFault=5;
   const auto before=listenerFixtureCloses;
   const auto result=listenerParents(query,expected);
   listenerFallbackFault=0;
   expect(queryCalls==1&&listenerToolhelpCalls==1&&listenerFixtureCloses==before+1,"scoped fallback stays fresh and returns its snapshot handle");
   return result;
 };
 expect(fallbackProjection(424200)==std::map<DWORD,DWORD>{{424241,424200}} && fallbackProjection(4000).empty(),"fallback projection only keeps matching parent rows");
 std::cout<<"ENDPOINT_PARENT_OBSERVATION "<<Json{{"checks",listenerChecks-checksBefore},{"cases",observations},{"unscopedParentRows",allParents.size()},{"scopedParentRows",scopedParents.size()},{"fallbackProjectionVerified",true},{"realSteamOperations",0},{"wholeProcessCpuMeasured",false}}.dump()<<'\n';
}

}
#include "fixtures/steam_listener_complete_tables.inc"
#ifndef YMCC_STEAM_DOCUMENT_READ_ONLY
// Only the source-extracted main dispatch's owner-observation ports are inert.
// Real ManualRepair, real Recovery and real transport implementations stay linked.
enum class PowerLifecycle { Ready };
static std::atomic<bool> g_exitRequested{false};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static unsigned long long selectedPowerGeneration=1;
static unsigned long long currentPowerGeneration(){return selectedPowerGeneration;}
struct DeckySelectedRuntimePort {
 struct State { bool enabled=true;unsigned long long revision=1; };
 State snapshot()const{return {};}
 bool exitFenced()const{return false;}
} g_deckySidebarRuntime;
static std::function<void()> busyReadReleaseHook;
static unsigned busyReadAcquisitions=0,busyReadReleases=0,busyReleaseHooks=0,busyTcpReads=0;
struct DeckySelectedSessionPort {
 ymcc::steamsession::SourceOwner sourceOwner;
 bool realSourceReads=false;
 struct ReadViewPort {
   bool actual=false;ymcc::steamsession::SourceOwner::View value;
   bool current()const noexcept{return !actual||value.current();}
 };
 struct ReadPort {
   bool actual=false;std::optional<ymcc::steamsession::SourceOwner::Read> value;
   ~ReadPort(){if(actual&&value&&static_cast<bool>(*value)){value.reset();++busyReadReleases;if(busyReadReleaseHook){auto hook=std::move(busyReadReleaseHook);busyReadReleaseHook={};++busyReleaseHooks;hook();}}}
   explicit operator bool()const noexcept{return !actual||(value&&static_cast<bool>(*value));}
   ReadViewPort view()const noexcept{return {actual,value?value->view():ymcc::steamsession::SourceOwner::View{}};}
 };
 ReadPort sourceRead(){
   if(realSourceReads){++busyReadAcquisitions;return {true,std::optional<ymcc::steamsession::SourceOwner::Read>(sourceOwner.read())};}
   return {};
 }
 ymcc::steamsession::Session value;
 mutable std::mutex mx;
 std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> facts;
 unsigned refreshes=0;std::function<void()> onRead;
 auto sessionSnapshot()const{return value;}
 auto identitySnapshot(){std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> result;std::function<void()> notice;{std::lock_guard lock(mx);result=facts;notice=onRead;}if(notice)notice();return result;}
 bool sameIdentityEpoch(const std::shared_ptr<const ymcc::steamsession::IdentitySnapshot>& expected){
   const auto current=identitySnapshot();return expected&&current&&expected->processEpoch==current->processEpoch&&expected->accountEpoch==current->accountEpoch&&expected->session==current->session&&expected->presence==current->presence;
 }
 bool requestIdentityRefresh(){++refreshes;return true;}
 void publish(ymcc::steamsession::IdentitySnapshot next){std::lock_guard lock(mx);facts=std::make_shared<const ymcc::steamsession::IdentitySnapshot>(std::move(next));}
} g_steamSettingsObserver;
#include "steam_native_return_evidence.h"
using json=nlohmann::json;
static struct {bool isolated=false;} g_aiFanMockSession;
static std::wstring U2W(const std::string& input){const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),nullptr,0);if(!n&&!input.empty())throw std::runtime_error("fixture-utf8");std::wstring out(n,0);if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),out.data(),n);return out;}
#include "main_steam_live_request.inc"
static auto actualRepairDispatchForDrain(){return [](const auto& session,const auto& action,const auto& admit){
            const auto power=currentPowerGeneration();
            const auto revision=g_deckySidebarRuntime.snapshot().revision;
            // Only this repeatable observation may run before discovery/after a wait.
            // admit also checks resources and grants RAM recovery consent; keep it
            // at the transport's single final pre-dispatch gate.
            const auto current=[&]{
                const auto state=g_deckySidebarRuntime.snapshot();
                return !g_exitRequested.load()&&!g_deckySidebarRuntime.exitFenced()&&state.enabled&&state.revision==revision&&
                    g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready&&currentPowerGeneration()==power&&
                    session==g_steamSettingsObserver.sessionSnapshot();
            };
            return ymcc::steamlive::runOwned(g_steamSettingsObserver,session.root,action,admit,0,current,&session);
        };}
#ifdef YMCC_UI_RECEIPT_FIXTURE
#include "steam_ui_read_budget.h"
#include "decky_sidebar_session.h"
#include "decky_sidebar_diagnostics.h"
namespace ymcc::steamui {
#include "ui_receipt_read.inc"
}
#endif
static void mainSharedIdentityFixture(bool nativeOwnerOnly=false,const std::string& startAt={}){
 processIdentityProbe=dispatchProbe=true;admissionProbe=true;processIdentityOpens=processIdentityCloses=processIdentityUnexpected=0;
 unsigned admits=0;unsigned checksBefore=listenerChecks;
 auto fresh=[&]{ymcc::steamsession::IdentitySnapshot f;f.session={L"P:\\YMCC-identity-fixture",processIdentityPid,42,processIdentityBirth};f.presence=1;f.registeredPid=processIdentityPid;f.rootReadError=f.registeredPidReadError=f.accountReadError=ERROR_SUCCESS;f.processEpoch=1;f.accountEpoch=1;f.revision=1;return f;};
 auto setup=[&]{processIdentityOpen=processIdentityPath=processIdentityTimes=processIdentityAlive=true;dispatchRead=0;dispatchOwner=true;dispatchExitOnUpgrade=dispatchSendFails=false;dispatchReplyHook={};dispatchReplyValue=R"({"ok":true,"mutated":false})";g_steamSettingsObserver.onRead={};g_steamSettingsObserver.publish(fresh());g_aiFanMockSession.isolated=false;ymcc::processInventory::invalidate();admits=0;};
 const auto witness=[&]{return Json{{"pid",processIdentityPid},{"created",std::to_string(processIdentityBirth)},{"root","P:\\YMCC-identity-fixture"}};};
 const auto invoke=[&](DWORD account,Json request){return steamLiveRequest(account,std::move(request),[&]{++admits;return true;});};
 const auto denied=[](const Json& r){return !r.value("ok",true)&&!r.value("mutated",true);};
#ifdef YMCC_SOCKET_OWNER_CONSTRUCTION_FIXTURE
 if(nativeOwnerOnly&&startAt=="socket-owner-construction"){
   Json rows=Json::array();bool all=true;
#ifdef YMCC_SOCKET_OWNER_OLD_ONLY
   const unsigned count=1;
#else
   const unsigned count=3;
#endif
   for(unsigned which=0;which<count;++which){
     setup();g_exitRequested=false;dispatchUpgradeHook={};socketOwnerArm=false;socketOwnerThrows=socketOwnerUpgradeSuccess=0;socketOwnerAllocationBytes=0;socketOwnerFrameCount=0;
     for(unsigned i=0;i<6;++i)socketOwnerOpens[i]=socketOwnerCloses[i]=0;
     socketOwnerProbe=true;
     if(which==0)dispatchUpgradeHook=[]{socketOwnerArm.store(true,std::memory_order_relaxed);};
     if(which==2)dispatchUpgradeHook=[]{dispatchOwner=false;};
     const auto sendsBefore=dispatchSends,opensBefore=dispatchHttpOpen,closesBefore=dispatchHttpClose;
     const auto session=fresh().session;
     const auto result=ymcc::steamlive::run(session.root,{{"operation","settings.set"}},[&]{++admits;return true;},0,[]{return dispatchOwner;},&session);
     const bool armedAfter=socketOwnerArm.exchange(false);socketOwnerProbe=false;dispatchUpgradeHook={};
     Json opens=Json::array(),closes=Json::array(),frames=Json::array();bool parents=true;
     for(unsigned i=1;i<6;++i){opens.push_back(socketOwnerOpens[i]);closes.push_back(socketOwnerCloses[i]);if(i<5)parents=parents&&socketOwnerOpens[i]>0&&socketOwnerOpens[i]==socketOwnerCloses[i];}
     const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
     for(USHORT i=0;i<socketOwnerFrameCount;++i)frames.push_back(reinterpret_cast<std::uintptr_t>(socketOwnerFrames[i])-base);
     const bool fault=which==0,late=which==2;
     const bool outcome=fault?(!result.value("ok",true)&&result.value("reason",std::string{})==std::bad_alloc{}.what()&&!result.value("requestDispatched",true)&&!result.value("mutated",true)&&socketOwnerThrows==1&&socketOwnerFrameCount>3):late?(!result.value("ok",true)&&result.value("reason",std::string{})=="live-action-cancelled"&&!result.value("requestDispatched",true)&&!result.value("mutated",true)&&socketOwnerThrows==0):(result.value("ok",false)&&!result.value("mutated",true)&&socketOwnerThrows==0);
     const bool exactDispatch=dispatchSends-sendsBefore==(which==1?1u:0u)&&admits==(which==1?1u:0u);
     const bool passed=outcome&&exactDispatch&&parents&&!armedAfter&&socketOwnerUpgradeSuccess==1&&socketOwnerOpens[5]==1&&socketOwnerCloses[5]==1&&dispatchHttpOpen-opensBefore==dispatchHttpClose-closesBefore&&processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0;
     rows.push_back({{"case",fault?"constructor-bad-alloc":late?"late-cancellation":"healthy"},{"receipt",result},{"passed",passed},{"outcomeMatched",outcome},{"parentDirectHandlesReturned",parents},{"directOpensByHandle",opens},{"directClosesByHandle",closes},{"socketDirectCloses",socketOwnerCloses[5]},{"upgradeSuccesses",socketOwnerUpgradeSuccess},{"allocationThrows",socketOwnerThrows},{"allocationBytes",socketOwnerAllocationBytes},{"allocationStackRvas",frames},{"armedAfter",armedAfter},{"admits",admits},{"sends",dispatchSends-sendsBefore},{"processOpens",processIdentityOpens},{"processCloses",processIdentityCloses}});all=all&&passed;
   }
   std::cout<<"SOCKET_OWNER_CONSTRUCTION_RESULT "<<Json{{"ok",all},{"rows",rows},{"caseCount",rows.size()},{"defaultGroupExecuted",false},{"realSteamOperations",0},{"actualStdThreadConstructor",true},{"memoryWinHttpLeaves",true}}.dump()<<"\n";
   expect(all,"actual run socket constructor exception direct ownership and healthy/cancel controls");return;
 }
#endif
 if(nativeOwnerOnly&&startAt=="command-busy-return"){
   setup();g_exitRequested=false;g_steamSettingsObserver.realSourceReads=true;
   auto& source=g_steamSettingsObserver.sourceOwner;expect(source.claim(),"busy return actual private SourceOwner claim");source.resumeReads();
   // Use the original fixture's normal Provider, created by main; no replacement Provider.
   auto* coordinator=ymcc::steamcommand::provider.load();expect(coordinator!=nullptr,"busy return uses original ordinary Provider");
   const ymcc::steamcommand::Target target{processIdentityPid,processIdentityBirth,fresh().session.root};
   Json rows=Json::array();bool all=true;
   listenerEndpointTcp=[](PVOID,PDWORD,BOOL,ULONG,TCP_TABLE_CLASS,ULONG){++busyTcpReads;return ERROR_ACCESS_DENIED;};
   const auto snapshot=[&]{return Json{{"acquisitions",busyReadAcquisitions},{"releases",busyReadReleases},{"hooks",busyReleaseHooks},{"process",processIdentityOpens},{"send",dispatchSends},{"http",dispatchHttpOpen},{"tcp",busyTcpReads}};};
   const std::vector<std::string> withdrawals={"owner","power","process","account"};
   for(unsigned branch=0;branch<2;++branch)for(unsigned fault=0;fault<withdrawals.size();++fault){
     setup();g_exitRequested=false;source.resumeReads();bool ownerAlive=true;const auto power=currentPowerGeneration();
     const auto operation=branch?(fault%2?"sleep.input-resume":"sleep.reconcile"):"settings.set";
     Json request={{"operation",operation}};if(branch)request["sleepSessionProcess"]=witness();
     auto occupied=coordinator->reserve(target,ymcc::steamcommand::Plan::Single);expect(bool(occupied),"busy return holds actual conflicting ordinary reservation");
     const auto before=snapshot();const auto handlesBefore=[] {DWORD n=0;GetProcessHandleCount(GetCurrentProcess(),&n);return n;}();
     busyReadReleaseHook=[&]{if(fault==0)ownerAlive=false;else if(fault==1)++selectedPowerGeneration;else {auto next=fresh();if(fault==2){++next.processEpoch;++next.session.created;}else{++next.accountEpoch;++next.session.account;}g_steamSettingsObserver.publish(next);}};
     const auto result=steamLiveRequest(42,request,[&]{++admits;return true;},[&]{return ownerAlive&&power==currentPowerGeneration();});
     const auto after=snapshot();const auto handlesAfter=[] {DWORD n=0;GetProcessHandleCount(GetCurrentProcess(),&n);return n;}();
     const Json expected={{"ok",false},{"reason","steam-command-busy"},{"mutated",false},{"requestDispatched",false}};
     const bool local=result==expected&&!ymcc::steamlive::canDefer(result)&&admits==0&&after["acquisitions"].get<unsigned>()==before["acquisitions"].get<unsigned>()+1&&after["releases"].get<unsigned>()==before["releases"].get<unsigned>()+1&&after["hooks"].get<unsigned>()==before["hooks"].get<unsigned>()+1&&after["process"]==before["process"]&&after["send"]==before["send"]&&after["http"]==before["http"]&&after["tcp"]==before["tcp"]&&handlesAfter==handlesBefore&&!busyReadReleaseHook;
     rows.push_back({{"operation",operation},{"withdrawal",withdrawals[fault]},{"receipt",result},{"canDefer",ymcc::steamlive::canDefer(result)},{"before",before},{"after",after},{"admits",admits},{"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter},{"passed",local}});all=all&&local;occupied.parentReturned();
#ifdef YMCC_BUSY_OLD_ONLY
     std::cout<<"COMMAND_BUSY_RETURN_RESULT "<<Json{{"ok",all},{"rows",rows},{"caseCount",rows.size()},{"oldNecessaryFailureOnly",true},{"defaultGroupExecuted",false},{"realSteamOperations",0}}.dump()<<"\n";
     source.drain();expect(source.close(),"busy return actual source close");g_steamSettingsObserver.realSourceReads=false;listenerEndpointTcp={};expect(all,"actual main preserves local Busy after source release withdrawal");return;
#endif
   }
   listenerEndpointTcp={};
   // A forwarded remote JSON shape is not native typed coordination provenance.
   for(bool remoteLike:{true,false}){
     setup();g_exitRequested=false;source.resumeReads();
     dispatchReplyValue=remoteLike?R"({"ok":false,"reason":"steam-command-busy","mutated":false,"requestDispatched":false})":R"({"ok":true,"mutated":true})";
     busyReadReleaseHook=[&]{auto next=fresh();++next.processEpoch;++next.session.created;g_steamSettingsObserver.publish(next);};
     const auto sends=dispatchSends,reads=busyReadAcquisitions,releases=busyReadReleases,hooks=busyReleaseHooks;
     const auto result=steamLiveRequest(42,{{"operation","settings.set"}},[&]{++admits;return true;});
     const bool passed=!result.value("ok",true)&&result.value("reason",std::string{})=="steam-session-changed"&&result.value("mutated",false)==!remoteLike&&dispatchSends==sends+1&&admits==1&&busyReadAcquisitions==reads+1&&busyReadReleases==releases+1&&busyReleaseHooks==hooks+1&&!busyReadReleaseHook;
     rows.push_back({{"control",remoteLike?"forwarded-remote-busy-lookalike":"late-sent-mutation"},{"receipt",result},{"sendDelta",dispatchSends-sends},{"admits",admits},{"passed",passed}});all=all&&passed;
   }
   expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0&&dispatchHttpOpen==dispatchHttpClose,"busy return control process and network resources returned");
   source.drain();expect(source.close(),"busy return actual source close");g_steamSettingsObserver.realSourceReads=false;
   std::cout<<"COMMAND_BUSY_RETURN_RESULT "<<Json{{"ok",all},{"rows",rows},{"caseCount",rows.size()},{"busyCases",8},{"controls",2},{"defaultGroupExecuted",false},{"realSteamOperations",0},{"sourceReadBalanced",busyReadAcquisitions==busyReadReleases}}.dump()<<"\n";
   expect(all,"actual main preserves only locally typed Busy and retains late sent revocation without replay");return;
 }
 if(nativeOwnerOnly&&startAt=="message-assembly"){
   setup();g_exitRequested=false;g_steamSettingsObserver.value=fresh().session;
   g_steamSettingsObserver.realSourceReads=true;auto& source=g_steamSettingsObserver.sourceOwner;
   expect(source.claim(),"message fixture claims its actual private source owner");source.resumeReads();
   Json rows=Json::array();bool allEqual=true;
   for(const std::string operation:{"settings.get","ui.mode.get","input.gamepadLease"})for(unsigned variant=0;variant<5;++variant){
     setup();g_exitRequested=false;dispatchReplyValue=R"({"ok":true,"mutated":false,"observedAccount":42,"mode":"bigpicture"})";
     Json request={{"operation",operation},{"fixtureData",Json::array({nullptr,true,-7,1.25,"owned"})}};
     if(variant==1)request["escaped"]="quote\" slash\\ newline\n tab\t";
     if(variant==2)request["unicode"]="游戏 🐈 字段";
     if(variant==3)request["bounds"]=Json::array({uint64_t(0xffffffffffffffffULL),int64_t(-9223372036854775807LL),0.0});
     if(variant==4)request["nested"]={{"array",Json::array({Json::object(),Json::array(),nullptr})},{"text",std::string(4096,'x')}};
     if(operation=="input.gamepadLease")request["lightSessionProcess"]=witness();
     const auto sends=dispatchSends;const auto reply=invoke(42,request);
     expect(reply.value("ok",false)&&admits==1&&dispatchSends==sends+1,"message case executes exactly one original whole main dispatch");
     const auto sent=Json::parse(dispatchLastMessage);const auto& params=sent.at("params");
     expect(sent.at("id")==Json(1)&&sent.at("method")==Json("Runtime.evaluate")&&params.at("timeout").is_number_unsigned(),"original CDP id method and serialized timeout type retained");
     // This oracle reuses the actual clock scalar; independent deadline behavior
     // remains covered by the original transport consumer, not this byte check.
     const DWORD timeout=params.at("timeout").get<DWORD>();expect(timeout>0&&timeout<=14000,"original dynamic timeout remains bounded");
     request["account"]=DWORD(42);
     const char* script=operation=="input.gamepadLease"?kLightGamepadScript:operation=="ui.mode.get"?kUiModeScript:kScript;
     const std::string expression=std::string(script)+"("+request.dump()+")";
     const std::string expected=Json{{"id",1},{"method","Runtime.evaluate"},{"params",{{"expression",expression},{"returnByValue",true},{"awaitPromise",true},{"timeout",timeout}}}}.dump();
     const bool same=dispatchLastMessage==expected;allEqual=allEqual&&same;
     expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0&&dispatchHttpOpen==dispatchHttpClose,"message cases return all original process and HTTP socket handles");
     rows.push_back({{"operation",operation},{"variant",variant},{"messageBytes",dispatchLastMessage.size()},{"expressionBytes",expression.size()},{"timeout",timeout},{"exactMessageBytes",same}});
   }
   source.drain();expect(source.close(),"message fixture closes its actual source mutex after all reads");
   g_steamSettingsObserver.realSourceReads=false;setup();dispatchProbe=processIdentityProbe=false;admissionProbe=false;
   std::cout<<"MAIN_MESSAGE_ASSEMBLY_SELECTED_RESULT "<<Json{{"ok",allEqual},{"selection",startAt},{"cases",rows},{"caseCount",rows.size()},{"checks",listenerChecks-checksBefore},{"dynamicClockScalarUsedFromActualMessage",true},{"originalDefaultGroupExecuted",false},{"realSteamOperations",0}}.dump()<<"\n";
   expect(allEqual,"exact-runtime-evaluate-message");return;
 }
 if(nativeOwnerOnly&&startAt=="discovery-cancel"){
   const auto previous=ymcc::steamlive::listenersource::operationCurrent;
   struct Case {const char* name;unsigned leaf;unsigned revoke;bool inherited;};
   const std::vector<Case> cases={{"owner-after-size",1,1,true},{"owner-after-fill",2,1,true},{"exit-after-size",1,2,true},{"throw-after-size",1,3,true},{"inherited-revoked",0,0,false},{"healthy-inherited",0,0,true}};
   Json observations=Json::array();
   for(const auto& selected:cases){
     setup();g_exitRequested=false;bool ownerValid=true,ownerThrows=false;
     const auto reads=admissionPortReads.load(),sends=dispatchSends,http=dispatchHttpOpen;
     discoveryCancellationHook=[&]{if(selected.leaf&&admissionPortReads.load()-reads==selected.leaf){if(selected.revoke==1)ownerValid=false;else if(selected.revoke==2)g_exitRequested=true;else ownerThrows=true;}};
     const auto current=[&]{if(ownerThrows)throw std::runtime_error("fixture-owner-current");return ownerValid;};
     Json value;
     {
       ymcc::steamlive::listenersource::Current inherited([&]{return selected.inherited;});
       const auto inheritedPointer=ymcc::steamlive::listenersource::operationCurrent;
       value=steamLiveRequest(42,{{"operation","settings.get"}},[&]{++admits;return true;},current);
       expect(ymcc::steamlive::listenersource::operationCurrent==inheritedPointer,"existing inherited current restored after transport return");
     }
     expect(ymcc::steamlive::listenersource::operationCurrent==previous,"outer current restored without dangling request stack");
     const auto executed=admissionPortReads.load()-reads;
     if(selected.leaf||!selected.inherited){
       expect(!value.value("ok",true)&&!value.value("mutated",true),"cancelled discovery never publishes success or mutation");
       expect(executed==selected.leaf,"caller revocation stops at the first completed OS leaf");
       expect(dispatchHttpOpen==http&&dispatchSends==sends&&admits==0,"cancelled discovery issues no HTTP websocket or action admission");
     }else{
       expect(value.value("ok",false)&&dispatchSends==sends+1&&admits==1,"healthy inherited context preserves one original dispatch");
       expect(executed==12,"healthy operation retains all three original fresh dual-family ownership checks");
     }
     expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0&&dispatchHttpOpen==dispatchHttpClose,"actual original transport leaves release all owned memory handles");
     observations.push_back({{"name",selected.name},{"tcpLeaves",executed},{"httpOpened",dispatchHttpOpen-http},{"sends",dispatchSends-sends},{"admissions",admits},{"result",value}});
     discoveryCancellationHook={};g_exitRequested=false;
   }
   setup();dispatchProbe=processIdentityProbe=false;admissionProbe=false;
   std::cout<<"MAIN_DISCOVERY_CANCEL_RESULT "<<Json{{"selection","discovery-cancel"},{"caseCount",cases.size()},{"checks",listenerChecks-checksBefore},{"cases",observations},{"originalDefaultGroupExecuted",false},{"realSteamOperations",0},{"wholeProcessCpuMeasured",false}}.dump()<<"\n";return;
 }
 if(nativeOwnerOnly&&startAt.starts_with("source-drain")){
   setup();g_exitRequested=false;
   g_steamSettingsObserver.realSourceReads=true;
   auto& source=g_steamSettingsObserver.sourceOwner;
   if(startAt=="source-drain-missing"){
     const auto reads=admissionPortReads.load(),http=dispatchHttpOpen,sends=dispatchSends;
     const auto result=steamLiveRequest(42,{{"operation","settings.get"}});
     expect(!result.value("ok",true)&&!result.value("mutated",true)&&admissionPortReads.load()==reads&&dispatchHttpOpen==http&&dispatchSends==sends,"missing actual source owner cannot use cached identity for discovery");
     g_steamSettingsObserver.realSourceReads=false;setup();dispatchProbe=processIdentityProbe=false;admissionProbe=false;
     std::cout<<"MAIN_SOURCE_DRAIN_ADDITIONAL "<<Json{{"selection",startAt},{"passed",true},{"realSourceOwner",true},{"http",0},{"sends",0}}.dump()<<"\n";return;
   }
   expect(source.claim(),"actual source owner claimed in fixture-only named namespace");source.resumeReads();
   if(startAt=="source-drain-write"){
     unsigned admissions=0;const auto sends=dispatchSends;
     dispatchReplyHook=[&]{source.suspendReads();};
     const auto result=steamLiveRequest(42,{{"operation","settings.set"}},[&]{++admissions;return true;});dispatchReplyHook={};
     expect(!result.value("ok",true)&&result.value("mutated",false)&&dispatchSends==sends+1&&admissions==1,"source revocation after send keeps uncertain write and single admission");
     source.drain();expect(source.close(),"source reader returned after late sent-write rejection");
     g_steamSettingsObserver.realSourceReads=false;setup();dispatchProbe=processIdentityProbe=false;admissionProbe=false;
     std::cout<<"MAIN_SOURCE_DRAIN_ADDITIONAL "<<Json{{"selection",startAt},{"passed",true},{"realSourceOwner",true},{"result",result},{"sends",1},{"admissions",admissions}}.dump()<<"\n";return;
   }
   std::promise<void> leafEntered,leafRelease;auto entered=leafEntered.get_future();auto release=leafRelease.get_future().share();std::atomic<bool> once{false};
   sourceDrainTcpHook=[&]{if(!once.exchange(true)){leafEntered.set_value();release.wait_for(std::chrono::seconds(5));}};
   const auto http=dispatchHttpOpen,sends=dispatchSends;
   g_steamSettingsObserver.value=fresh().session;
   auto request=std::async(std::launch::async,[&]()->Json{
     if(startAt=="source-drain-bound")return steamLiveRequest(0,{{"operation","sleep.session.inspect"},{"sleepSessionProcess",witness()}});
     if(startAt=="source-drain-repair"){auto dispatch=actualRepairDispatchForDrain();return dispatch(fresh().session,Json{{"operation","decky.recoverConnection"}},[]{return true;});}
#ifdef YMCC_UI_RECEIPT_FIXTURE
     if(startAt=="source-drain-ui"){
       const auto state=ymcc::steamui::read(g_steamSettingsObserver,true,[]{return !g_exitRequested.load();});
       return Json{{"ok",state.value("known",false)&&state.value("mode",std::string{})!="unknown"},{"mutated",false},{"modeResult",state}};
     }
#endif
     return steamLiveRequest(42,{{"operation","settings.get"}});
   });
   const bool actualEntered=entered.wait_for(std::chrono::seconds(2))==std::future_status::ready;
   g_exitRequested=true;source.suspendReads();
   auto drained=std::async(std::launch::async,[&]{source.drain();return true;});
   const bool returnedWhileBlocked=drained.wait_for(std::chrono::milliseconds(60))==std::future_status::ready;
   bool ownerClosedBeforeTask=false,successorClaimedBeforeTask=false;
   if(returnedWhileBlocked){ownerClosedBeforeTask=source.close();ymcc::steamsession::SourceOwner successor;successorClaimedBeforeTask=successor.claim();successor.drain();successor.close();}
   leafRelease.set_value();const auto result=request.get();const bool actualDrained=drained.get();sourceDrainTcpHook={};source.close();
   expect(actualEntered,"actual original main request entered existing TCP discovery leaf");
   expect(!result.value("ok",true)&&!result.value("mutated",true)&&dispatchHttpOpen==http&&dispatchSends==sends,"original cancellation still prevents HTTP and dispatch after the blocked leaf returns");
   expect(actualDrained&&processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0,"controlled request thread and original process observations return");
   std::cout<<"MAIN_SOURCE_DRAIN_OBSERVATION "<<Json{{"selection",startAt},{"actualSourceOwner",true},{"actualMainFunction",true},{"originalObserverWorkerRun",false},{"actualTcpLeafControlled",true},{"drainReturnedBeforeInFlightRequest",returnedWhileBlocked},{"ownerClosedBeforeTask",ownerClosedBeforeTask},{"successorClaimedBeforeTask",successorClaimedBeforeTask},{"checks",listenerChecks-checksBefore},{"httpAfterCancellation",dispatchHttpOpen-http},{"sends",dispatchSends-sends},{"realSteamOperations",0},{"installedProcessSingletonExercised",false}}.dump()<<"\n";
   g_exitRequested=false;setup();dispatchProbe=processIdentityProbe=false;admissionProbe=false;
   g_steamSettingsObserver.realSourceReads=false;
   expect(!returnedWhileBlocked,"original public source drainage must include in-flight main transport discovery");return;
 }
 if(nativeOwnerOnly){
   if(startAt=="process-pid-fields"){
     struct Input {const char* name;Json value;};
     const std::vector<Input> malformedWitness={{"null",nullptr},{"bool",true},{"number",1},{"string","process"},{"array",Json::array()}};
     const std::vector<Input> malformedPid={{"null",nullptr},{"bool",true},{"negative",-1},{"fraction",1.5},{"integral-float",1.0},{"string","42"},{"object",Json::object()},{"array",Json::array()},{"overflow",uint64_t(0x100000000ULL)},{"overflow-wrap-current",uint64_t(0x100000000ULL)+processIdentityPid}};
     std::vector<std::string> names;
     const ymcc::steamsession::Session expected{L"P:\\YMCC-identity-fixture",processIdentityPid,42,processIdentityBirth};
     const auto prepare=[&]{setup();g_exitRequested=false;dispatchUpgradeHook={};};
     const auto checkRequest=[&](const std::string& name,Json request,bool valid,bool cancel=false){
       prepare();const auto sends=dispatchSends,opens=processIdentityOpens,ports=admissionPortReads.load(),http=dispatchHttpOpen;
       unsigned currentCalls=0;Json receipt;bool threw=false;
       try{receipt=run(expected.root,request,[&]{++admits;return true;},400,[&]{++currentCalls;return !cancel;},&expected);}
       catch(...){threw=true;}
       std::cout<<"PROCESS_PID_FIELD_CASE "<<Json{{"case",name},{"request",request},{"threw",threw},{"receipt",receipt},{"sends",dispatchSends-sends},{"opens",processIdentityOpens-opens},{"ports",admissionPortReads.load()-ports},{"admissions",admits},{"currentCalls",currentCalls}}.dump()<<"\n";
       expect(!threw,"consumed optional process PID fields return fixed refusal instead of throwing");
       if(cancel){
         expect(receipt.value("reason",std::string{})=="live-action-cancelled"&&!receipt.value("mutated",true)&&!receipt.value("requestDispatched",true),"original owner cancellation still precedes process-field validation");
         expect(currentCalls==1&&dispatchSends==sends&&processIdentityOpens==opens&&admissionPortReads.load()==ports&&admits==0,"cancelled original owner performs no process discovery or final admission");
       }else if(!valid){
         expect(receipt==Json{{"ok",false},{"reason","steam-session-changed"},{"mutated",false},{"requestDispatched",false}},"invalid consumed process PID returns fixed typed unwritten refusal");
         expect(dispatchSends==sends&&processIdentityOpens==opens&&admissionPortReads.load()==ports&&dispatchHttpOpen==http&&admits==0,"invalid consumed process PID cannot open process HTTP or listener discovery");
       }else{
         expect(receipt==Json{{"ok",true},{"mutated",false}}&&dispatchSends==sends+1&&admits==1,"legacy valid optional hints retain the original single dispatch and reply");
         expect(processIdentityOpens>opens&&processIdentityOpens==processIdentityCloses&&dispatchHttpOpen==dispatchHttpClose,"valid process hint retains original held identity and resource return");
       }
       expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0&&dispatchHttpOpen==dispatchHttpClose,"all process PID field paths return original owned resources");
       names.push_back(name);
     };
     for(const auto* operation:{"sleep.inspect","input.gamepadLease"}){
       const char* field=std::string(operation)=="input.gamepadLease"?"lightSessionProcess":"sleepSessionProcess";
       const std::string prefix=std::string(operation)+"/";
       for(const auto& item:malformedWitness)checkRequest(prefix+"witness-"+item.name,{{"operation",operation},{field,item.value}},false);
       for(const auto& item:malformedPid)checkRequest(prefix+"pid-"+item.name,{{"operation",operation},{field,{{"pid",item.value}}}},false);
       checkRequest(prefix+"absent",{{"operation",operation}},true);
       checkRequest(prefix+"empty",{{"operation",operation},{field,Json::object()}},true);
       checkRequest(prefix+"zero",{{"operation",operation},{field,{{"pid",uint32_t(0)}}}},true);
       checkRequest(prefix+"current-unsigned",{{"operation",operation},{field,{{"pid",processIdentityPid}}}},true);
       checkRequest(prefix+"current-signed",{{"operation",operation},{field,{{"pid",int64_t(processIdentityPid)}}}},true);
       checkRequest(prefix+"owner-cancel-malformed",{{"operation",operation},{field,nullptr}},false,true);
     }
     checkRequest("unconsumed-fields/ordinary-read",{{"operation","settings.get"},{"sleepSessionProcess",nullptr},{"lightSessionProcess",Json::array()}},true);
     prepare();dispatchProbe=processIdentityProbe=false;admissionProbe=false;
     std::cout<<"PROCESS_PID_FIELDS_SELECTED_RESULT "<<Json{{"ok",true},{"cases",names},{"caseCount",names.size()},{"checks",listenerChecks-checksBefore},{"originalDefaultGroupExecuted",false},{"originalExpectedIdentityGroupExecuted",false},{"realSteamOperations",0},{"realRecoveryOperations",0}}.dump()<<"\n";return;
   }
   if(startAt=="menu-mutation-fields"){
     // Invoke the exact extracted main wrapper, including its post-run classification.
     // Null is first: old main throws after a healthy transport return. No direct
     // menuGameMutation call is needed, so this fixture also compiles with before.
     struct Flag {const char* name;Json value;bool present;bool writes;};
     const std::vector<Flag> flags={{"null",nullptr,true,false},{"missing",nullptr,false,false},
       {"true",true,true,true},{"false",false,true,false},{"zero",0,true,false},{"one",1,true,false},
       {"string","true",true,false},{"object",Json{{"value",true}},true,false},{"array",Json::array({true}),true,false}};
     enum class Outcome { healthy, sendFailure, identityAfter };
     struct MenuCase {std::string name;Json request;Outcome outcome;bool writes;};
     std::vector<MenuCase> cases;
     const auto disposeRequest=[](const Flag& flag){Json request={{"operation","menu.qam.dispose"}};if(flag.present)request["menuGameDisposeOnly"]=flag.value;return request;};
     for(const auto& flag:flags)cases.push_back({std::string("healthy/dispose-")+flag.name,disposeRequest(flag),Outcome::healthy,flag.writes});
     for(const auto& flag:flags)cases.push_back({std::string("send-failure/dispose-")+flag.name,disposeRequest(flag),Outcome::sendFailure,flag.writes});
     for(const auto index:{0u,2u,3u}){const auto& flag=flags[index];cases.push_back({std::string("identity-after/dispose-")+flag.name,disposeRequest(flag),Outcome::identityAfter,flag.writes});}
     // Presence is deliberately conservative even when JS would see null. The
     // disposal flag is scoped to dispose; observe remains the original read.
     const Json menuGameNull={{"operation","menu.qam.observe"},{"menuGame",nullptr},{"menuGameDisposeOnly",false}};
     cases.push_back({"healthy/menuGame-null",menuGameNull,Outcome::healthy,true});
     cases.push_back({"send-failure/menuGame-null",menuGameNull,Outcome::sendFailure,true});
     cases.push_back({"identity-after/menuGame-null",menuGameNull,Outcome::identityAfter,true});
     cases.push_back({"send-failure/observe-dispose-true",{{"operation","menu.qam.observe"},{"menuGameDisposeOnly",true}},Outcome::sendFailure,false});
     const Json healthy={{"ok",true},{"mutated",false},{"requestDispatched",true},{"observedAccount",42},
       {"payload",{{"owner","unchanged"},{"values",Json::array({nullptr,false,1,"true",Json{{"nested",true}}})}}}};
     std::vector<std::string> names;
     const auto prepare=[&]{setup();g_exitRequested=false;dispatchUpgradeHook={};g_powerLifecycle.store(PowerLifecycle::Ready);};
     for(const auto& item:cases){
       prepare();dispatchReplyValue=healthy.dump();dispatchSendFails=item.outcome==Outcome::sendFailure;
       unsigned replyHooks=0,afterReads=0;
       if(item.outcome==Outcome::identityAfter)dispatchReplyHook=[&]{
         ++replyHooks;
         g_steamSettingsObserver.onRead=[&]{
           // identitySnapshot captures the old pointer before this callback.
           // CDP's final current() sees that pointer and returns healthy; main's
           // subsequent afterIdentity read alone sees the newly published epoch.
           if(++afterReads==1){auto next=fresh();++next.processEpoch;++next.revision;g_steamSettingsObserver.publish(next);}
         };
       };
       const auto sends=dispatchSends,httpOpen=dispatchHttpOpen,httpClose=dispatchHttpClose;
       const auto processOpen=processIdentityOpens,processClose=processIdentityCloses;
       Json receipt;bool threw=false;std::string exception;
       try{receipt=invoke(0,item.request);}catch(const std::exception& error){threw=true;exception=error.what();}catch(...){threw=true;exception="non-std-exception";}
       g_steamSettingsObserver.onRead={};
       const auto mode=item.outcome==Outcome::healthy?"healthy":item.outcome==Outcome::sendFailure?"send-failure":"identity-after";
       std::cout<<"MAIN_MENU_MUTATION_CASE "<<Json{{"case",item.name},{"mode",mode},{"request",item.request},{"expectedPossibleWrite",item.writes},
         {"threw",threw},{"exception",exception},{"receipt",receipt},{"sends",dispatchSends-sends},{"admissions",admits},
         {"processOpens",processIdentityOpens-processOpen},{"processCloses",processIdentityCloses-processClose},
         {"httpOpens",dispatchHttpOpen-httpOpen},{"httpCloses",dispatchHttpClose-httpClose},{"replyHooks",replyHooks},{"afterReads",afterReads}}.dump()<<"\n";
       expect(processIdentityOpens>processOpen&&processIdentityOpens-processOpen==processIdentityCloses-processClose&&processIdentityUnexpected==0,"menu field wrapper returns its original held process handles");
       expect(dispatchHttpOpen>httpOpen&&dispatchHttpOpen-httpOpen==dispatchHttpClose-httpClose,"menu field wrapper returns its original HTTP and socket handles");
       expect(dispatchSends==sends+1&&admits==1,"menu field wrapper sends once and never repeats final admission");
       expect(!threw,"menu dispose field must not throw after original dispatch");
       const auto packet=Json::parse(dispatchLastMessage);
       expect(packet.at("params").at("expression").get_ref<const std::string&>().ends_with("("+item.request.dump()+")"),"menu field shapes reach the original script without truthy coercion or added operations");
       if(item.outcome==Outcome::healthy){
         expect(receipt==healthy,"healthy menu receipt and unknown nested payload remain exactly unchanged");
       }else if(item.outcome==Outcome::sendFailure){
         expect(receipt.at("ok")==Json(false)&&receipt.at("mutated")==Json(item.writes)&&receipt.at("requestDispatched")==Json(true)&&
           receipt.at("reason")==Json(item.writes?"live-transport-uncertain":"live-read-unavailable"),"actual sent failure keeps strict bool writes uncertain and original reads read-only");
         const auto& failure=receipt.at("steamFailure");
         expect(failure.at("schema")==Json(1)&&failure.at("domain")==Json("winhttp")&&failure.at("sourceStage")==Json("send")&&
           failure.at("codeKnown")==Json(true)&&failure.at("code")==Json(ERROR_CONNECTION_ABORTED),"send failure retains the original WinHTTP send-stage diagnostic");
         expect(canDefer(receipt)==!item.writes,"sent possible menu writes cannot defer or gain replay authority");
       }else{
         expect(replyHooks==1&&afterReads==2,"identity revocation occurs between CDP's captured final current and main's afterIdentity observation");
         expect(receipt==Json{{"ok",false},{"mutated",item.writes},{"reason","steam-session-changed"}},"main post-return identity refusal retains possible writes without transport-failure remapping or payload leakage");
         expect(canDefer(receipt)==!item.writes,"post-return identity refusal cannot replay a possible menu write");
       }
       names.push_back(item.name);
     }
     prepare();dispatchProbe=processIdentityProbe=false;admissionProbe=false;
     std::cout<<"MAIN_MENU_MUTATION_FIELDS_SELECTED_RESULT "<<Json{{"ok",true},{"selection","menu-mutation-fields"},{"cases",names},{"caseCount",names.size()},
       {"checks",listenerChecks-checksBefore},{"originalDefaultGroupExecuted",false},{"originalMutationPolicyExecuted",false},{"realSteamOperations",0},{"realRecoveryOperations",0}}.dump()<<"\n";return;
   }
   if(startAt=="reply-envelope"){
     struct Bad {std::string name;Json value;};std::vector<Bad> bad;
     const Json base={{"ok",false},{"mutated",false},{"reason","live-busy"}};
     auto value=base;value["reason"]=Json::object();bad.push_back({"reason-object",value});
     value=base;value.erase("ok");bad.push_back({"missing-ok",value});
     for(const auto& malformed:Json::array({nullptr,0,1,"false",Json::object(),Json::array()})){
       for(const auto* field:{"ok","mutated","requestDispatched"}){value=base;value[field]=malformed;bad.push_back({std::string(field)+"/"+std::to_string(bad.size()),value});}
     }
     for(const auto& malformed:Json::array({nullptr,false,1,Json::array()})){value=base;value["reason"]=malformed;bad.push_back({"reason/"+std::to_string(bad.size()),value});}
     for(const auto& malformed:Json::array({nullptr,false,-1,0,1.0,42.5,"42",uint64_t(0x100000000ULL),Json::object(),Json::array()})){value=base;value["observedAccount"]=malformed;bad.push_back({"observedAccount/"+std::to_string(bad.size()),value});}
     struct Mode{const char* name;const char* operation;bool bound;bool discovery;bool background;bool writes;};
     const std::vector<Mode> modes={{"write","settings.set",false,false,false,true},{"read","settings.get",false,false,false,false},{"discovery","sleep.session.inspect",true,true,false,false},{"background","sleep.inspect",true,false,true,false}};
     std::vector<std::string> names;
     const auto prepare=[&]{setup();g_exitRequested=false;g_aiFanMockSession.isolated=false;dispatchUpgradeHook={};g_powerLifecycle.store(PowerLifecycle::Ready);};
     const auto request=[&](const Mode& mode){Json r={{"operation",mode.operation}};if(mode.bound)r["sleepSessionProcess"]=witness();if(mode.background)r["runningLinkRead"]=true;return r;};
     for(const auto& item:bad)for(const auto& mode:modes){
       prepare();dispatchReplyValue=item.value.dump();const auto sends=dispatchSends;Json receipt;bool threw=false;
       try{receipt=invoke(mode.discovery?0:42,request(mode));}catch(...){threw=true;}
       expect(!threw&&receipt.is_object()&&receipt.contains("ok")&&receipt["ok"].is_boolean()&&!receipt["ok"].get<bool>(),"malformed common reply must return a typed failure before wrapper conversions");
       expect(receipt.contains("mutated")&&receipt["mutated"].is_boolean()&&receipt["mutated"].get<bool>()==mode.writes,"malformed sent write stays uncertain and actual read remains non-mutating");
       expect(receipt.contains("reason")&&receipt["reason"].is_string(),"malformed envelope never exports an invalid common reason");
       if(mode.writes)expect(!canDefer(receipt),"malformed sent write cannot be replayed as deferred");
       expect(dispatchSends==sends+1&&admits==1,"common reply validation never repeats dispatch or final admission");
       expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0&&dispatchHttpOpen==dispatchHttpClose,"invalid common envelope returns original process and transport handles");names.push_back(item.name+"/"+mode.name);
     }
     const std::vector<Json> healthy={Json{{"ok",true}},Json{{"ok",false},{"reason","live-api-unavailable"},{"mutated",false}},Json{{"ok",true},{"mutated",false},{"requestDispatched",false},{"observedAccount",42},{"payload",{{"owner","unchanged"}}}},Json{{"ok",false},{"mutated",true},{"reason","live-timeout"}}};
     for(std::size_t i=0;i<healthy.size();++i){prepare();dispatchReplyValue=healthy[i].dump();const auto receipt=invoke(42,{{"operation","settings.set"}});expect(receipt==healthy[i],"valid optional reply fields and original unknown payload are returned unchanged");expect(dispatchSends>0&&admits==1,"healthy receipt has original single admission");names.push_back("healthy-unchanged/"+std::to_string(i));}
     for(const auto account:{uint64_t(1),uint64_t(0xffffffffULL)}){prepare();Json receipt;dispatchReplyValue=Json{{"ok",true},{"observedAccount",account}}.dump();const auto identity=fresh();auto foreign=identity;foreign.registeredPid=17;g_steamSettingsObserver.publish(foreign);receipt=invoke(0,{{"operation","sleep.session.inspect"},{"sleepSessionProcess",witness()}});expect(receipt.value("ok",false)&&receipt["observedAccount"]==account,"strict positive uint32 account endpoints retain discovery semantics");names.push_back("valid-account/"+std::to_string(account));}
     prepare();dispatchReplyValue=R"({"ok":false,"reason":{},"mutated":true,"steamFailure":{"schema":1,"domain":"winhttp","sourceStage":"receive","codeKnown":true,"code":12030,"private":"secret"}})";dispatchReplyHook=[]{g_exitRequested=true;};const auto late=invoke(42,{{"operation","settings.set"}});
     expect(!late.value("ok",true)&&late.value("mutated",false)&&late.contains("steamFailure")&&late["steamFailure"].value("code",0)==12030&&!late["steamFailure"].contains("private"),"late revoked malformed response keeps bounded error facts without acknowledgment");names.push_back("late-revoked/bounded-diagnostic");
     prepare();dispatchProbe=processIdentityProbe=false;admissionProbe=false;
     std::cout<<"MAIN_REPLY_ENVELOPE_SELECTED_RESULT "<<Json{{"ok",true},{"cases",names},{"caseCount",names.size()},{"checks",listenerChecks-checksBefore},{"originalDefaultGroupExecuted",false},{"realSteamOperations",0},{"realRecoveryOperations",0}}.dump()<<"\n";return;
   }
   if(startAt=="defer-receipt"){
     struct DeferredCase {std::string name;Json value;bool expected;};
     std::vector<DeferredCase> cases;
     const auto base=Json{{"ok",false},{"mutated",false},{"reason","live-busy"}};
     auto missing=base;missing.erase("mutated");cases.push_back({"missing-mutated-not-unwritten",missing,false});
     missing=base;missing.erase("ok");cases.push_back({"missing-ok-not-failure",missing,false});
     missing=base;missing.erase("reason");cases.push_back({"missing-reason-not-transient",missing,false});
     auto success=base;success["ok"]=true;cases.push_back({"success-is-not-deferrable-failure",success,false});
     auto written=base;written["mutated"]=true;cases.push_back({"possible-write-is-not-deferrable",written,false});
     for(const auto& value:Json::array({nullptr,0,1,"false",Json::object(),Json::array()})){
       auto bad=base;bad["mutated"]=value;cases.push_back({"malformed-mutated/"+std::to_string(cases.size()),bad,false});
       bad=base;bad["ok"]=value;cases.push_back({"malformed-ok/"+std::to_string(cases.size()),bad,false});
       bad=base;bad["reason"]=value;cases.push_back({"malformed-reason/"+std::to_string(cases.size()),bad,false});
     }
     for(const auto& value:Json::array({nullptr,false,0,"receipt",Json::array(),Json::object()}))cases.push_back({"nonreceipt/"+std::to_string(cases.size()),value,false});
     for(const auto* reason:{"live-read-unavailable","live-busy","steam-session-starting","steam-session-changed","steam-account-changed","live-debug-unavailable","live-context-unavailable","live-api-unavailable","live-controller-not-connected"}){
       auto allowed=base;allowed["reason"]=reason;cases.push_back({std::string("original-transient/")+reason,allowed,true});
       allowed["requestDispatched"]=true;cases.push_back({std::string("explicit-unwritten-sent/")+reason,allowed,true});
     }
     for(const auto* reason:{"live-transport-uncertain","live-editor-busy","live-timeout","live-busy.extra",""}){auto rejectedReceipt=base;rejectedReceipt["reason"]=reason;cases.push_back({std::string("original-nontransient/")+reason,rejectedReceipt,false});}
     std::vector<std::string> names;
     for(const auto& item:cases){
       bool threw=false,value=false;try{value=canDefer(item.value);}catch(...){threw=true;}
       expect(!threw&&value==item.expected,"only explicit negative unwritten transient receipts may defer without throwing");names.push_back(item.name);
     }
     const auto prepare=[&]{setup();g_exitRequested=false;g_aiFanMockSession.isolated=false;dispatchUpgradeHook={};g_powerLifecycle.store(PowerLifecycle::Ready);};
     for(unsigned variant=0;variant<3;++variant){
       prepare();auto reply=base;if(variant==0)reply.erase("mutated");if(variant==2)reply["requestDispatched"]=true;
       dispatchReplyValue=reply.dump();const auto sends=dispatchSends;const auto actual=invoke(42,{{"operation","settings.set"}});
       expect(!actual.value("ok",true)&&canDefer(actual)==(variant!=0)&&dispatchSends==sends+1&&admits==1,"actual shared settings consumer preserves explicit JS busy while refusing missing write evidence");
       expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0&&dispatchHttpOpen==dispatchHttpClose,"deferral receipt consumer returns original process and transport handles");names.push_back("actual-settings-busy/"+std::to_string(variant));
     }
     prepare();dispatchSendFails=true;auto actual=invoke(42,{{"operation","settings.set"}});
     expect(!canDefer(actual)&&actual.value("mutated",false),"actual uncertain sent write never becomes deferred");names.push_back("actual-settings-uncertain-write/refused");
     prepare();dispatchSendFails=true;actual=invoke(42,{{"operation","settings.get"}});
     expect(canDefer(actual)&&!actual.value("mutated",true)&&actual.value("reason",std::string{})=="live-read-unavailable","actual original failed read retains safe deferral");names.push_back("actual-settings-failed-read/allowed");
     prepare();dispatchProbe=processIdentityProbe=false;admissionProbe=false;
     std::cout<<"MAIN_DEFER_RECEIPT_SELECTED_RESULT "<<Json{{"ok",true},{"cases",names},{"caseCount",names.size()},{"checks",listenerChecks-checksBefore},{"originalDefaultGroupExecuted",false},{"realSteamOperations",0},{"realRecoveryOperations",0}}.dump()<<"\n";return;
   }
   if(startAt=="mutation-policy"){
     std::vector<std::string> names;
     const auto prepare=[&]{setup();g_exitRequested=false;g_aiFanMockSession.isolated=false;dispatchUpgradeHook={};g_powerLifecycle.store(PowerLifecycle::Ready);};
     for(const auto* operation:{"decky.repairConnection","decky.recoverConnection","decky.cleanupConnection"})for(unsigned fault=0;fault<4;++fault){
       prepare();const auto sends=dispatchSends;
       if(fault==0)dispatchSendFails=true;
       else if(fault==1)dispatchReplyValue=R"({"ok":false,"reason":"live-timeout","mutated":true})";
       else if(fault==2)dispatchReplyHook=[&]{auto next=fresh();++next.processEpoch;g_steamSettingsObserver.publish(next);};
       else dispatchUpgradeHook=[]{g_exitRequested=true;};
       const auto result=invoke(42,{{"operation",operation}});
       if(fault<3){
         expect(!result.value("ok",true)&&result.value("mutated",false)&&dispatchSends==sends+1&&admits==1,"known Decky operation must retain uncertain write after actual shared dispatch");
         expect(result.value("reason",std::string{})!="live-read-unavailable","known Decky mutation cannot become a retryable read");
       }else expect(!result.value("ok",true)&&!result.value("mutated",true)&&dispatchSends==sends&&admits==0,"Decky operation cancelled before dispatch remains positively unwritten");
       const auto typed=ymcc::steamrequest::classify(operation);
       expect(typed.known&&typed.setter&&!typed.runningLinkRead&&!typed.shortRead,"known Decky operation has mutation uncertainty policy only");
       expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0&&dispatchHttpOpen==dispatchHttpClose,"mutation policy returns original process and transport handles");
       names.push_back(std::string(operation)+"/"+std::to_string(fault));
     }
     prepare();dispatchSendFails=true;auto result=invoke(42,{{"operation","settings.get"}});
     expect(!result.value("ok",true)&&!result.value("mutated",true)&&result.value("reason",std::string{})=="live-read-unavailable","ordinary original scalar read keeps its read-only uncertainty mapping");names.push_back("settings.get/read-only-control");
     prepare();const auto sends=dispatchSends;result=invoke(42,{{"operation","decky.cleanupConnection.extra"}});
     expect(denied(result)&&dispatchSends==sends&&admits==0,"typed Decky prefixes do not add operations or permission");names.push_back("unknown-operation/refused");
     prepare();dispatchProbe=processIdentityProbe=false;admissionProbe=false;
     std::cout<<"MAIN_MUTATION_POLICY_SELECTED_RESULT "<<Json{{"ok",true},{"cases",names},{"caseCount",names.size()},{"checks",listenerChecks-checksBefore},{"originalDefaultGroupExecuted",false},{"realSteamOperations",0},{"realRecoveryOperations",0}}.dump()<<"\n";return;
   }
   if(startAt=="native-return"){
     struct ReturnCase{const char* name;std::string kind;Json code,booleanValue;unsigned revoke=0;bool valid=true;bool diagnostic=false;bool otherOperation=false;};
     const std::vector<ReturnCase> returnCases={
       {"exit-uint32","uint32",0,nullptr,0},
       {"isolated-uint32","uint32",7,nullptr,1},
       {"epoch-uint32","uint32",7,nullptr,2},
       {"process-exit-uint32","uint32",7,nullptr,3},
       {"maximum-uint32","uint32",uint64_t(0xffffffffULL),nullptr,0},
       {"boolean-true","boolean",nullptr,true,0},
       {"boolean-false","boolean",nullptr,false,0},
       {"undefined","undefined",nullptr,nullptr,0},
       {"unsupported","unsupported",nullptr,nullptr,0},
       {"overflow-discarded","uint32",uint64_t(0x100000000ULL),nullptr,0,false},
       {"verified-flag-discarded","uint32",7,nullptr,4,false},
       {"failure-and-return","uint32",7,nullptr,0,true,true},
       {"other-operation-discards","uint32",7,nullptr,0,true,false,true}
     };
     std::vector<std::string> names;
     for(const auto& value:returnCases){
       setup();g_exitRequested=false;g_aiFanMockSession.isolated=false;dispatchUpgradeHook={};
       Json proof={{"source","SteamClient.Apps.RaiseWindowForGame"},{"kind",value.kind},{"observed",value.kind=="uint32"||value.kind=="boolean"},
         {"code",value.code},{"booleanValue",value.booleanValue},{"authority","diagnostic-only-no-admission"},
         {"nativeMeaningVerified",value.revoke==4},{"gameInputVerified",false},{"continueVerified",false},{"private","secret"}};
       Json reply={{"ok",!value.diagnostic},{"mutated",true},{"nativeReturn",proof}};
       if(value.diagnostic)reply["steamFailure"]={{"schema",1},{"domain","winhttp"},{"sourceStage","receive"},{"codeKnown",true},{"code",12030},{"private","secret"}};
       dispatchReplyValue=reply.dump();dispatchReplyHook=[&]{if(value.revoke==1)g_aiFanMockSession.isolated=true;
         else if(value.revoke==2){auto f=fresh();++f.processEpoch;g_steamSettingsObserver.publish(f);}
         else if(value.revoke==3)processIdentityAlive=false;else g_exitRequested=true;};
       const auto sends=dispatchSends;
       const auto result=value.otherOperation?invoke(42,{{"operation","settings.set"}}):invoke(42,{{"operation","sleep.input-resume"},{"sleepSessionProcess",witness()}});
       expect(!result.value("ok",true)&&result.value("mutated",false)&&dispatchSends==sends+1&&admits==1,"revoked settled native result stays uncertain with one send and one admission");
       if(value.valid&&!value.otherOperation){
         expect(result.contains("nativeReturn")&&result["nativeReturn"]==ymcc::steamreturnevidence::bounded(reply)&&result.value("nativeReturnEvidenceVersion",std::string{})=="steam-native-return-evidence-v1","late revoked input resume retains existing bounded native return evidence");
         expect(!result["nativeReturn"].contains("private")&&!result["nativeReturn"].value("nativeMeaningVerified",true)&&!result["nativeReturn"].value("gameInputVerified",true)&&!result["nativeReturn"].value("continueVerified",true),"retained native return never gains meaning input continue or private fields");
       }else expect(!result.contains("nativeReturn"),"malformed evidence and unrelated operation do not gain native return");
       if(value.diagnostic)expect(result.contains("steamFailure")&&result["steamFailure"].value("code",0)==12030&&!result["steamFailure"].contains("private"),"native return preservation also keeps the original bounded error");
       expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0&&dispatchHttpOpen==dispatchHttpClose,"native return paths close original process HTTP and socket handles");names.push_back(value.name);
     }
     setup();g_exitRequested=false;g_aiFanMockSession.isolated=false;dispatchUpgradeHook={};dispatchProbe=processIdentityProbe=false;admissionProbe=false;
     std::cout<<"MAIN_NATIVE_RETURN_SELECTED_RESULT "<<Json{{"ok",true},{"cases",names},{"caseCount",names.size()},{"checks",listenerChecks-checksBefore},{"originalDefaultGroupExecuted",false},{"realSteamOperations",0}}.dump()<<"\n";return;
   }
   struct Case{const char* name;std::function<void()> run;};
   const auto resetOwner=[&]{setup();g_exitRequested=false;dispatchUpgradeHook={};g_powerLifecycle.store(PowerLifecycle::Ready);};
   const auto noDispatchedClaim=[](const Json& value){const auto field=value.find("requestDispatched");return field==value.end()||(field->is_boolean()&&!field->get<bool>());};
   const auto notSent=[&](const Json& value,unsigned sends,unsigned http,unsigned expectedAdmits=0){
     expect(denied(value)&&value.value("reason",std::string{})=="live-action-cancelled"&&noDispatchedClaim(value)&&
       dispatchSends==sends&&dispatchHttpOpen==http&&admits==expectedAdmits,"native cancellation refuses before discovery or final admission");
   };
   const auto queueCancel=[&](bool isolated){
     std::unique_lock serial(transportMutex);std::promise<void> entered;auto ready=entered.get_future();std::atomic<unsigned> reads{0};
     g_steamSettingsObserver.onRead=[&]{if(++reads==2)entered.set_value();};const auto sends=dispatchSends,http=dispatchHttpOpen;
     auto pending=std::async(std::launch::async,[&]{return invoke(42,{{"operation","settings.set"}});});
     const bool waiting=ready.wait_for(std::chrono::seconds(2))==std::future_status::ready;
     if(isolated)g_aiFanMockSession.isolated=true;else g_exitRequested=true;
     serial.unlock();const auto value=pending.get();g_steamSettingsObserver.onRead={};
     expect(waiting,"actual main request reaches original shared transport queue");notSent(value,sends,http);
   };
   std::vector<std::string> names;
   const std::vector<Case> cases={
     {"entry-exit",[&]{const auto sends=dispatchSends,http=dispatchHttpOpen;g_exitRequested=true;notSent(invoke(42,{{"operation","settings.set"}}),sends,http);}},
     {"snapshot-exit",[&]{const auto sends=dispatchSends,http=dispatchHttpOpen;g_steamSettingsObserver.onRead=[]{g_exitRequested=true;};notSent(invoke(42,{{"operation","settings.set"}}),sends,http);g_steamSettingsObserver.onRead={};}},
     {"queue-exit",[&]{queueCancel(false);}},
     {"queue-isolated",[&]{queueCancel(true);}},
     {"upgrade-exit",[&]{const auto sends=dispatchSends;dispatchUpgradeHook=[]{g_exitRequested=true;};const auto value=invoke(42,{{"operation","settings.set"}});expect(denied(value)&&noDispatchedClaim(value)&&dispatchSends==sends&&admits==0,"exit during socket upgrade cannot send or spend final admission");}},
     {"upgrade-isolated",[&]{const auto sends=dispatchSends;dispatchUpgradeHook=[]{g_aiFanMockSession.isolated=true;};const auto value=invoke(42,{{"operation","settings.set"}});expect(denied(value)&&noDispatchedClaim(value)&&dispatchSends==sends&&admits==0,"isolation during socket upgrade cannot send or spend final admission");}},
     {"admit-exit",[&]{const auto sends=dispatchSends;const auto value=steamLiveRequest(42,{{"operation","settings.set"}},[&]{++admits;g_exitRequested=true;return true;});expect(denied(value)&&noDispatchedClaim(value)&&dispatchSends==sends&&admits==1,"native revoke during final consent never sends or repeats consent");}},
     {"admit-isolated",[&]{const auto sends=dispatchSends;const auto value=steamLiveRequest(42,{{"operation","settings.set"}},[&]{++admits;g_aiFanMockSession.isolated=true;return true;});expect(denied(value)&&noDispatchedClaim(value)&&dispatchSends==sends&&admits==1,"isolation during final consent never sends or repeats consent");}},
     {"reply-exit-write",[&]{const auto sends=dispatchSends;dispatchReplyHook=[]{g_exitRequested=true;};const auto value=invoke(42,{{"operation","settings.set"}});expect(!value.value("ok",true)&&value.value("reason",std::string{})=="live-action-cancelled"&&value.value("mutated",false)&&dispatchSends==sends+1&&admits==1,"late native exit rejects a sent write and preserves uncertainty without replay");}},
     {"reply-isolated-read",[&]{const auto sends=dispatchSends;dispatchReplyHook=[]{g_aiFanMockSession.isolated=true;};const auto value=steamLiveRequest(42,{{"operation","settings.get"}});expect(!value.value("ok",true)&&!value.value("mutated",true)&&value.value("reason",std::string{})=="live-action-cancelled"&&dispatchSends==sends+1,"late isolation cannot publish a former read result as current");}},
     {"bound-discovery-exit",[&]{const auto sends=dispatchSends;dispatchUpgradeHook=[]{g_exitRequested=true;};const auto value=steamLiveRequest(0,{{"operation","sleep.session.inspect"},{"sleepSessionProcess",witness()}});expect(denied(value)&&noDispatchedClaim(value)&&dispatchSends==sends&&admits==0,"bound discovery pure branch also cancels late native exit");}},
     {"bound-reply-exit",[&]{const auto sends=dispatchSends;dispatchReplyHook=[]{g_exitRequested=true;};const auto value=invoke(42,{{"operation","sleep.input-resume"},{"sleepSessionProcess",witness()}});expect(!value.value("ok",true)&&value.value("mutated",false)&&value.value("reason",std::string{})=="live-action-cancelled"&&dispatchSends==sends+1&&admits==1,"bound sent mutation cannot acknowledge a cancelled native owner");}},
     {"sleep-read-keeps-original-phase-contract",[&]{g_powerLifecycle.store(static_cast<PowerLifecycle>(3));dispatchReplyValue=R"({"ok":true,"observedAccount":42,"mutated":false})";const auto value=steamLiveRequest(0,{{"operation","sleep.session.inspect"},{"sleepSessionProcess",witness()}});expect(value.value("ok",false)&&admits==0,"native gate does not invent a global Ready requirement for original sleep reads");}},
     {"normal-failed-reply-trace",[&]{dispatchReplyValue=R"({"ok":false,"reason":"live-caught","mutated":false,"steamFailure":{"schema":1,"domain":"javascript","sourceStage":"evaluate","exceptionType":"TypeError","private":"secret"}})";dispatchReplyHook=[]{g_exitRequested=true;};const auto value=invoke(42,{{"operation","settings.set"}});expect(!value.value("ok",true)&&value.contains("steamFailure")&&value["steamFailure"].value("domain",std::string{})=="javascript"&&!value["steamFailure"].contains("private"),"normal late native refusal preserves normalized existing failure only");}},
     {"bound-failed-reply-trace",[&]{dispatchReplyValue=R"({"ok":false,"reason":"live-caught","mutated":false,"steamFailure":{"schema":1,"domain":"winhttp","sourceStage":"receive","codeKnown":true,"code":12030,"private":"secret"}})";dispatchReplyHook=[]{g_exitRequested=true;};const auto value=invoke(42,{{"operation","sleep.input-resume"},{"sleepSessionProcess",witness()}});expect(!value.value("ok",true)&&value.contains("steamFailure")&&value["steamFailure"].value("code",0)==12030&&!value["steamFailure"].contains("private"),"bound late native refusal preserves normalized existing failure only");}},
     {"success-no-fabricated-failure",[&]{dispatchReplyValue=R"({"ok":true,"mutated":true,"steamFailure":{"schema":1,"domain":"javascript","sourceStage":"evaluate"}})";dispatchReplyHook=[]{g_exitRequested=true;};const auto value=invoke(42,{{"operation","settings.set"}});expect(!value.value("ok",true)&&!value.contains("steamFailure"),"late success never manufactures caught failure facts");}}
   };
   bool reached=startAt.empty();
   for(const auto& value:cases){if(!reached){if(startAt!=value.name)continue;reached=true;}resetOwner();value.run();names.push_back(value.name);expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0,"selected native owner process handles return");expect(dispatchHttpOpen==dispatchHttpClose,"selected native owner HTTP/socket handles return");}
   resetOwner();dispatchProbe=processIdentityProbe=false;admissionProbe=false;
   std::cout<<"MAIN_NATIVE_OWNER_SELECTED_RESULT "<<Json{{"ok",true},{"cases",names},{"caseCount",names.size()},{"checks",listenerChecks-checksBefore},{"originalDefaultGroupExecuted",false},{"selectedFrom",startAt},{"wholeNativeOwnerParentExecuted",startAt.empty()},{"realSteamOperations",0}}.dump()<<"\n";return;
 }

 setup();auto before=dispatchSends;auto r=invoke(42,{{"operation","settings.set"}});
 expect(r.value("ok",false)&&admits==1&&dispatchSends==before+1,"actual main settings request consumes shared identity and dispatches once");
 for(const auto& bad:Json::array({nullptr,1,Json::array(),Json::object(),Json{{"operation",false}},Json{{"operation","invalid"}}})){
   setup();before=dispatchSends;r=invoke(42,bad);expect(denied(r)&&r.value("reason",std::string{})=="invalid-live-operation"&&dispatchSends==before&&admits==0,"actual main rejects malformed operations before dispatch");
 }
 setup();g_aiFanMockSession.isolated=true;before=dispatchSends;r=invoke(42,{{"operation","settings.set"}});expect(denied(r)&&dispatchSends==before&&admits==0,"isolated main owner never starts a Steam request");
 setup();{std::lock_guard lock(g_steamSettingsObserver.mx);g_steamSettingsObserver.facts.reset();}const auto refreshes=g_steamSettingsObserver.refreshes;before=dispatchSends;r=invoke(42,{{"operation","settings.set"}});expect(denied(r)&&g_steamSettingsObserver.refreshes==refreshes+1&&dispatchSends==before&&admits==0,"missing shared receipt requests only original producer refresh");
 for(const auto status:{ERROR_SUCCESS,ERROR_ACCESS_DENIED}){
   setup();auto f=fresh();f.session.account=0;f.accountReadError=status;g_steamSettingsObserver.publish(f);before=dispatchSends;r=invoke(0,{{"operation","menu.button"},{"account",123}});expect(r.value("ok",false)&&admits==1&&dispatchSends==before+1,"explicit menu preserves Unknown account without a global account gate");
 }
 setup();auto f=fresh();f.session.account=0;g_steamSettingsObserver.publish(f);before=dispatchSends;r=invoke(42,{{"operation","settings.set"}});expect(denied(r)&&dispatchSends==before&&admits==0,"ordinary account-backed mutation keeps zero-account refusal");
 setup();f=fresh();f.session.account=43;g_steamSettingsObserver.publish(f);before=dispatchSends;r=invoke(42,{{"operation","settings.set"}});expect(denied(r)&&r.value("reason",std::string{})=="steam-account-changed"&&dispatchSends==before&&admits==0,"ordinary account mismatch rejects before final consent");
 setup();f=fresh();f.registeredPid=17;f.session.account=99;g_steamSettingsObserver.publish(f);before=dispatchSends;r=invoke(42,{{"operation","sleep.input-resume"},{"sleepSessionProcess",witness()}});expect(r.value("ok",false)&&admits==1&&dispatchSends==before+1,"sleep UI-pinned account survives a foreign registry PID candidate");
 setup();f=fresh();f.registeredPid=17;f.session.account=99;g_steamSettingsObserver.publish(f);dispatchReplyHook=[&]{auto next=f;next.session.account=101;++next.accountEpoch;g_steamSettingsObserver.publish(next);};before=dispatchSends;r=invoke(42,{{"operation","sleep.input-resume"},{"sleepSessionProcess",witness()}});expect(r.value("ok",false)&&admits==1&&dispatchSends==before+1,"foreign registry account-only noise cannot erase the same pinned Steam UI account");
 for(unsigned fault=0;fault<4;++fault){setup();auto w=witness();if(fault==0)w["pid"]=processIdentityPid+1;if(fault==1)w["created"]=std::to_string(processIdentityBirth+1);if(fault==2)w["root"]="P:\\OtherSteam";if(fault==3)w["created"]="+"+std::to_string(processIdentityBirth);before=dispatchSends;r=invoke(42,{{"operation","sleep.input-resume"},{"sleepSessionProcess",w}});expect(denied(r)&&dispatchSends==before&&admits==0,"original sleep witness cannot replace shared PID birth or root");}
 setup();before=dispatchSends;r=invoke(0,{{"operation","sleep.session.inspect"}});expect(denied(r)&&r.value("reason",std::string{})=="steam-session-process-witness-required"&&dispatchSends==before,"sleep discovery still requires an original process witness");
 for(const auto account:{42u,43u}){setup();dispatchReplyValue=std::string(R"({"ok":true,"mutated":false,"observedAccount":)")+std::to_string(account)+"}";r=invoke(0,{{"operation","sleep.session.inspect"},{"sleepSessionProcess",witness()}});expect(account==42?r.value("ok",false):denied(r),"sleep discovery binds only returned UI account compatible with the same registered Steam process");}
 setup();f=fresh();dispatchReplyHook=[&]{auto next=f;next.session.account=43;++next.accountEpoch;g_steamSettingsObserver.publish(next);};before=dispatchSends;r=invoke(42,{{"operation","settings.set"}});expect(!r.value("ok",true)&&r.value("mutated",false)&&admits==1&&dispatchSends==before+1,"account switch after send cannot acknowledge or replay an uncertain settings write");
 setup();dispatchReplyHook=[]{processIdentityAlive=false;};before=dispatchSends;r=invoke(42,{{"operation","settings.set"}});expect(!r.value("ok",true)&&r.value("mutated",false)&&admits==1&&dispatchSends==before+1,"Steam exit during reply cannot publish old-instance success");
 setup();dispatchSendFails=true;r=invoke(0,{{"operation","menu.inspect"},{"menuGame",Json::object()}});expect(!r.value("ok",true)&&r.value("mutated",false)&&admits==1,"menu inspect carrying game mutation keeps uncertain no-replay semantics");
 setup();dispatchSendFails=true;r=invoke(0,{{"operation","menu.qam.dispose"},{"menuGameDisposeOnly",true}});expect(!r.value("ok",true)&&r.value("mutated",false)&&admits==1,"QAM disposal-only write keeps uncertain no-replay semantics");
 setup();dispatchSendFails=true;r=invoke(42,{{"operation","settings.get"}});expect(!r.value("ok",true)&&!r.value("mutated",true)&&r.value("reason",std::string{})=="live-read-unavailable","ordinary fixed getter preserves bounded read-only failure mapping");
 setup();std::unique_lock serial(transportMutex);std::promise<void> entered;auto ready=entered.get_future();std::atomic<unsigned> reads{0};g_steamSettingsObserver.onRead=[&]{if(++reads==2)entered.set_value();};before=dispatchSends;
 auto pending=std::async(std::launch::async,[&]{return invoke(42,{{"operation","settings.set"}});});const auto waiting=ready.wait_for(std::chrono::seconds(2))==std::future_status::ready;f=fresh();++f.processEpoch;g_steamSettingsObserver.publish(f);serial.unlock();r=pending.get();g_steamSettingsObserver.onRead={};expect(waiting&&denied(r)&&admits==0&&dispatchSends==before,"same PID birth after producer process-epoch change cancels queued main request");
 expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0,"all actual main request process leases return their handles");
 expect(dispatchHttpOpen==dispatchHttpClose,"all actual main request HTTP and socket handles return");
 dispatchProbe=processIdentityProbe=false;admissionProbe=false;std::cout<<"MAIN_SHARED_IDENTITY_SELECTED_PASS checks="<<listenerChecks-checksBefore<<" actualSteamOperations=0\n";
}

#ifdef YMCC_UI_RECEIPT_FIXTURE
static std::vector<Json> modeReadDiagnosticRows;
static bool modeReadTraceThrows=false;
static void deckySidebarDiagnostic(const char* event,const Json& detail,bool,
    const ymcc::steamfailure::FailureContext* context=nullptr){
    modeReadDiagnosticRows.push_back(context?ymcc::deckydiag::failureRecord(detail,*context):ymcc::deckydiag::record(event,detail));
    if(context&&modeReadTraceThrows)throw std::runtime_error("inert-log-write-fault");
}
#if defined(YMCC_MODE_READ_CURRENT_FIXTURE) || defined(YMCC_UI_RECEIPT_OWNED_FIXTURE)
#include "steam_ui_mode_observer.h"
#include "sleep_power_state.h"
static std::mutex modePowerMutex;
static ymcc::sleeppower::SharedPowerV1 modePowerValue;
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* out,std::uint32_t size) noexcept {
    std::lock_guard lock(modePowerMutex);
    if(!out||size!=sizeof(*out))return 0;
#if defined(YMCC_MODE_READ_CURRENT_FIXTURE) || defined(YMCC_UI_PUBLIC_READ_OWNER_FIXTURE)
    *out=modePowerValue;
#else
    // Original receipt fixture's power observation port. The real captured
    // 48-byte public predicate and source-extracted main helpers remain active.
    *out={};out->generation=selectedPowerGeneration;
    out->flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
#endif
    return 1;
}
#include "mode_power_helpers.inc"
#endif
#ifdef YMCC_UI_RECEIPT_OWNED_FIXTURE
#define productionUiModeRead productionUiModeReadOwned
#endif
#include "main_ui_mode_read.inc"
#ifdef YMCC_UI_RECEIPT_OWNED_FIXTURE
#undef productionUiModeRead
static json productionUiModeRead(const ymcc::steamsession::Session& session) {
    // Existing one-argument receipt tests have no resident demand owner. Their
    // original session/exit scope supplies a real predicate to the owned API.
    return productionUiModeReadOwned(session,[session] {
        return !g_exitRequested.load() && session==g_steamSettingsObserver.sessionSnapshot();
    });
}
#endif
#ifdef YMCC_UI_PUBLIC_READ_OWNER_FIXTURE
#include "fixtures/steam_ui_public_read_owner.inc"
#endif
#ifdef YMCC_MODE_READ_CURRENT_FIXTURE
#include "mode_read_current_cases.inc"
#else
static void uiReceiptFixture(bool lateCaughtOnly=false,bool clientStateOnly=false){
 processIdentityProbe=dispatchProbe=true;admissionProbe=true;processIdentityOpens=processIdentityCloses=processIdentityUnexpected=0;
 unsigned countBefore=listenerChecks;unsigned httpBefore=dispatchHttpOpen,closedBefore=dispatchHttpClose;
 auto fresh=[] {ymcc::steamsession::IdentitySnapshot f;f.session={L"P:\\YMCC-identity-fixture",processIdentityPid,0,processIdentityBirth};f.presence=1;f.registeredPid=processIdentityPid;f.processEpoch=1;f.accountEpoch=1;f.rootReadError=f.registeredPidReadError=f.accountReadError=ERROR_SUCCESS;return f;};
 auto setup=[&]{failureDiagnosticsProbe=false;failurePacket.clear();failurePacketOffset=0;failureReceiveError=failureHttpSendAt=failureHttpReceiveAt=failureHttpStatus=0;failureHttpQueryDenied=failureSocketClose=false;modeReadTraceThrows=false;modeReadDiagnosticRows.clear();selectedPowerGeneration=17;g_exitRequested=false;g_steamSettingsObserver.value=fresh().session;processIdentityOpen=processIdentityPath=processIdentityTimes=processIdentityAlive=true;dispatchRead=0;dispatchSendFails=dispatchExitOnUpgrade=false;dispatchReplyHook={};dispatchReplyValue=R"({"ok":true,"mode":"bigpicture","observedAccount":42,"deckyAccountWatchAvailable":true,"modeEpoch":"renderer:7","mutated":false})";g_steamSettingsObserver.onRead={};g_steamSettingsObserver.publish(fresh());ymcc::processInventory::invalidate();};
 if(clientStateOnly){
   const std::vector<std::string> labels={"disposed","native-run-missing","binding-run-missing","run-mismatch","socket-missing","socket-connecting","socket-closing","socket-closed","socket-unavailable","transport-unconfirmed","opening-reply-missing","connected","module-dismounted","frontend-absent","frontend-inactive","read-failed"};
   for(const auto& label:labels){
     setup();const auto requested=g_steamSettingsObserver.value;const auto sends=dispatchSends;
     auto value=Json::parse(dispatchReplyValue);value["deckyBackendConnected"]=true;value["deckyHooksInstalled"]=true;value["deckyClientConnected"]=false;value["deckyClientState"]=label;dispatchReplyValue=value.dump();
     const auto result=productionUiModeRead(requested);
     expect(result.value("deckyClientState",std::string{})==label&&result.value("known",false)&&result.value("mode",std::string{})=="bigpicture"&&result.value("observedAccount",0)==42,"actual shared mode reader keeps finite frontend state and original UI proof");
     expect(result.value("deckyBackendConnected",false)&&result.value("deckyHooksInstalled",false)&&!result.value("deckyClientConnected",true)&&dispatchSends==sends+1,"finite state adds no transport request and cannot replace original booleans");
     expect(modeReadDiagnosticRows.size()==1&&modeReadDiagnosticRows[0].value("deckyClientState",std::string{})==label&&!modeReadDiagnosticRows[0].value("deckyClientConnected",true),"actual main callback relays finite client state to original Decky mode log");
   }
   for(const auto& label:std::vector<std::string>{"socket-missing","socket-connecting","socket-open","socket-closing","socket-closed","socket-unavailable"}){
     setup();const auto requested=g_steamSettingsObserver.value;const auto sends=dispatchSends;
     auto value=Json::parse(dispatchReplyValue);value["deckyBackendConnected"]=false;value["deckyBackendState"]=label;value["deckyHooksInstalled"]=true;value["deckyClientConnected"]=true;dispatchReplyValue=value.dump();
     const auto result=productionUiModeRead(requested);
     expect(result.value("deckyBackendState",std::string{})==label&&result.value("known",false)&&result.value("mode",std::string{})=="bigpicture","actual shared mode reader retains finite official backend state");
     expect(!result.value("deckyBackendConnected",true)&&result.value("deckyHooksInstalled",false)&&result.value("deckyClientConnected",false)&&dispatchSends==sends+1,"backend explanation never changes original booleans or adds a transport request");
     expect(modeReadDiagnosticRows.size()==1&&modeReadDiagnosticRows[0].value("deckyBackendState",std::string{})==label&&!modeReadDiagnosticRows[0].value("deckyBackendConnected",true),"actual main callback relays official backend state through the original mode diagnostic filter");
   }
   for(const auto& invalid:std::vector<Json>{nullptr,17,true,Json::object(),Json::array(),"private-backend-token",std::string(10000,'x')}){
     setup();const auto requested=g_steamSettingsObserver.value;auto value=Json::parse(dispatchReplyValue);value["deckyBackendState"]=invalid;dispatchReplyValue=value.dump();const auto result=productionUiModeRead(requested);
     expect(result.value("known",false)&&!result.contains("deckyBackendState")&&modeReadDiagnosticRows.size()==1&&!modeReadDiagnosticRows[0].contains("deckyBackendState"),"actual shared reader and mode logger discard malformed or private official backend state");
   }
   for(const auto& invalid:std::vector<Json>{nullptr,17,true,Json::object(),Json::array(),"private-client-token",std::string(10000,'x')}){
     setup();const auto requested=g_steamSettingsObserver.value;auto value=Json::parse(dispatchReplyValue);value["deckyClientState"]=invalid;dispatchReplyValue=value.dump();const auto result=productionUiModeRead(requested);
     expect(result.value("known",false)&&!result.contains("deckyClientState")&&modeReadDiagnosticRows.size()==1&&!modeReadDiagnosticRows[0].contains("deckyClientState"),"actual shared reader and mode logger discard malformed or private client states");
   }
   for(unsigned revoked=0;revoked<2;++revoked){
     setup();const auto requested=g_steamSettingsObserver.value;auto value=Json::parse(dispatchReplyValue);value["deckyClientState"]="socket-closed";dispatchReplyValue=value.dump();
     dispatchReplyHook=[&,revoked]{if(revoked){selectedPowerGeneration=99;}else{auto next=fresh();++next.accountEpoch;g_steamSettingsObserver.publish(next);}};
     const auto result=productionUiModeRead(requested);
     // Owned mode reads now reject a revoked Native generation as well as
     // account changes; an optional label cannot publish an obsolete result.
     expect(revoked?(!result.value("known",true)&&result.value("reason",std::string{})=="shared-mode-read-cancelled") : (!result.value("known",true)&&!result.contains("deckyClientState")),"optional client label preserves original power/account read-result contract");
   }
   setup();const auto result=productionUiModeRead(g_steamSettingsObserver.value);expect(result.value("known",false)&&!result.contains("deckyClientState"),"older frontend without optional state remains fully compatible");
   expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0,"finite frontend diagnostic returns all actual process leases");
   expect(dispatchHttpOpen-httpBefore==dispatchHttpClose-closedBefore,"finite frontend diagnostic returns all actual HTTP/socket handles");
   dispatchProbe=processIdentityProbe=false;admissionProbe=false;
   std::cout<<"UI_RECEIPT_CLIENT_STATE_SELECTED_PASS checks="<<listenerChecks-countBefore<<" actualSteamOperations=0\n";return;
 }
 if(lateCaughtOnly){
   const auto traces=[] {std::vector<Json> rows;for(const auto& row:modeReadDiagnosticRows)if(row.value("event",std::string{})=="steam-recovery-trace")rows.push_back(row);return rows;};
   const auto failed=[] {return Json{{"ok",false},{"reason","live-api-error"},{"mutated",false},{"steamFailure",{{"schema",1},{"domain","javascript"},{"sourceStage","evaluate"},{"codeKnown",false},{"exceptionClassKnown",true},{"exceptionClass","TypeError"},{"lineKnown",true},{"line",31},{"signatures",Json::array({"context-destroyed","private-token"})},{"message",std::string(4096,'x')+"private-token"},{"stack","private-token"},{"account","private-token"}}}};};
   const auto revoke=[&] {auto next=fresh();++next.accountEpoch;g_steamSettingsObserver.publish(next);};
   setup();const auto requested=g_steamSettingsObserver.value;
   dispatchReplyValue=failed().dump();dispatchReplyHook=[&]{auto next=fresh();++next.processEpoch;++next.session.pid;++next.session.created;g_steamSettingsObserver.publish(next);g_steamSettingsObserver.value=next.session;selectedPowerGeneration=99;};
   auto sent=dispatchSends;auto r=productionUiModeRead(requested);auto rows=traces();
   expect(rows.size()==1&&rows[0]["steamFailure"].value("exceptionClass",std::string{})=="TypeError","late caught source failure must reach the existing ModeRead trace");
   expect(!r.value("known",true)&&r.value("reason",std::string{})=="steam-session-changed"&&!r.contains("observedAccount"),"late failed value cannot publish old mode or account proof");
   expect(rows[0].at("steamPid")==requested.pid&&rows[0].at("steamBirth")==std::to_string(requested.created)&&rows[0].at("nativePowerGeneration")==17&&rows[0].at("actionId")=="0","late caught trace keeps starting identity and power");
   expect(rows[0]["steamFailure"].value("line",0)==31&&rows[0]["steamFailure"]["signatures"]==Json::array({"context-destroyed"})&&!rows[0]["steamFailure"].value("codeKnown",true)&&rows[0].dump().size()<2304&&rows[0].dump().find("private-token")==std::string::npos,"late caught diagnostic is bounded normalized facts only");
   expect(dispatchSends==sent+1&&modeReadDiagnosticRows.size()==2,"late caught trace retains original mode log and adds no request");
   setup();dispatchReplyValue=failed().dump();dispatchReplyHook=revoke;r=productionUiModeRead(requested);rows=traces();
   expect(!r.value("known",true)&&rows.size()==1&&rows[0]["steamFailure"].value("exceptionClass",std::string{})=="TypeError","account revocation retains diagnostic without authorizing stale result");
   setup();dispatchReplyValue=failed().dump();dispatchReplyHook=[]{processIdentityAlive=false;};r=productionUiModeRead(requested);rows=traces();
   expect(r.value("mode",std::string{})=="unknown"&&r.value("reason",std::string{})=="steam-session-changed"&&!r.contains("observedAccount")&&rows.size()==1,"original process exit retains caught source facts without UI proof");
   setup();auto value=failed();value["ok"]=true;value["mode"]="bigpicture";dispatchReplyValue=value.dump();dispatchReplyHook=revoke;r=productionUiModeRead(requested);
   expect(!r.value("known",true)&&!r.contains("steamFailure")&&traces().empty(),"late successful value cannot invent failure from unsolicited fields");
   for(const auto& bad:std::vector<Json>{Json(),Json("false"),Json(0)}){setup();value=failed();if(bad.is_null())value.erase("ok");else value["ok"]=bad;dispatchReplyValue=value.dump();dispatchReplyHook=revoke;r=productionUiModeRead(requested);expect(r.value("reason",std::string{})=="steam-session-changed"&&!r.contains("steamFailure")&&traces().empty(),"malformed late ok is not false failure authority");}
   for(const auto& bad:std::vector<Json>{Json(),Json("private-token"),Json::array()}){setup();value=failed();value["steamFailure"]=bad;dispatchReplyValue=value.dump();dispatchReplyHook=revoke;r=productionUiModeRead(requested);expect(r.value("reason",std::string{})=="steam-session-changed"&&!r.contains("steamFailure")&&traces().empty(),"nonobject late failure is ignored without changing rejection");}
   setup();dispatchReplyValue=failed().dump();dispatchReplyHook=revoke;modeReadTraceThrows=true;r=productionUiModeRead(requested);
   expect(!r.value("known",true)&&r.value("reason",std::string{})=="steam-session-changed","late trace writer exception cannot alter original rejection");
   setup();dispatchReplyValue=failed().dump();bool current=true;dispatchReplyHook=[&]{current=false;};unsigned admits=0;sent=dispatchSends;
   r=run(requested.root,{{"operation","settings.set"}},[&]{++admits;return true;},0,[&]{return current;},&requested);
   expect(!r.value("ok",true)&&r.value("mutated",false)&&r.value("requestDispatched",false)&&r.value("reason",std::string{})=="live-transport-uncertain"&&!canDefer(r)&&admits==1&&dispatchSends==sent+1,"late caught mutation preserves uncertain no-replay and single admission");
   expect(r["steamFailure"].value("exceptionClass",std::string{})=="TypeError"&&r.dump().find("private-token")==std::string::npos,"uncertain mutation forwards only normalized caught facts");
   setup();r=productionUiModeRead(requested);expect(r.value("mode",std::string{})=="bigpicture"&&!r.contains("steamFailure")&&traces().empty(),"later healthy observation does not inherit previous failure");
   expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0,"late caught original process leases returned");
   expect(dispatchHttpOpen-httpBefore==dispatchHttpClose-closedBefore,"late caught HTTP and socket owners returned");
   dispatchProbe=processIdentityProbe=false;admissionProbe=false;
   std::cout<<"UI_RECEIPT_LATE_CAUGHT_SELECTED_PASS checks="<<listenerChecks-countBefore<<" actualSteamOperations=0\n";return;
 }
 setup();{std::lock_guard lock(g_steamSettingsObserver.mx);g_steamSettingsObserver.facts.reset();}const auto opens=processIdentityOpens;auto r=ymcc::steamui::read(g_steamSettingsObserver,true);expect(!r.value("known",true)&&processIdentityOpens==opens,"UI mode before shared identity is Unknown with no duplicate process query");
 setup();auto f=fresh();f.presence=0;f.session={};g_steamSettingsObserver.publish(f);r=ymcc::steamui::read(g_steamSettingsObserver,true);expect(r.value("known",false)&&!r.value("running",true)&&r.value("mode",std::string{})=="stopped"&&processIdentityOpens==opens,"UI stopped state comes from the same coherent shared receipt");
 setup();auto before=processIdentityOpens;r=ymcc::steamui::read(g_steamSettingsObserver,true);expect(r.value("known",false)&&r.value("mode",std::string{})=="bigpicture"&&r.value("observedAccount",0)==42&&processIdentityOpens==before+1&&processIdentityCloses==processIdentityOpens,"running UI account-zero read delegates exactly one process lease to the common transport");
 const auto sends=dispatchSends;before=processIdentityOpens;for(unsigned i=0;i<1001;++i){const auto cached=ymcc::steamui::read(g_steamSettingsObserver);if(cached.value("mode",std::string{})!="bigpicture")throw std::runtime_error("healthy cached UI lost");}expect(processIdentityOpens==before&&dispatchSends==sends,"1001 cached UI consumers add no process acquisition or request");
 f=fresh();++f.accountEpoch;g_steamSettingsObserver.publish(f);r=ymcc::steamui::read(g_steamSettingsObserver);expect(r.value("mode",std::string{})=="unknown"&&!r.contains("observedAccount")&&dispatchSends==sends,"account epoch revokes cached UI proof without refunding the automatic read budget");
 setup();dispatchReplyHook=[&]{auto next=fresh();++next.accountEpoch;g_steamSettingsObserver.publish(next);};r=ymcc::steamui::read(g_steamSettingsObserver,true);expect(!r.value("known",true)&&r.value("mode",std::string{})=="unknown"&&!r.contains("observedAccount"),"account epoch change during IO never publishes old UI proof");
 setup();dispatchReplyHook=[&]{auto next=fresh();++next.processEpoch;g_steamSettingsObserver.publish(next);};r=ymcc::steamui::read(g_steamSettingsObserver,true);expect(!r.value("known",true)&&r.value("mode",std::string{})=="unknown","producer process-epoch change during UI read revokes the old result");
 setup();dispatchReplyHook=[] {processIdentityAlive=false;};r=ymcc::steamui::read(g_steamSettingsObserver,true);expect(r.value("mode",std::string{})=="unknown"&&!r.contains("observedAccount"),"actual process exit in UI reply cannot return cached Big Picture success");
 setup();f=fresh();++f.session.created;g_steamSettingsObserver.publish(f);before=dispatchSends;r=ymcc::steamui::read(g_steamSettingsObserver,true);expect(r.value("mode",std::string{})=="unknown"&&dispatchSends==before,"shared UI receipt with another birth cannot select an existing endpoint");
 setup();dispatchReplyValue=R"({"ok":true,"mode":"desktop","observedAccount":42,"deckyAccountWatchAvailable":true,"modeEpoch":"renderer:8","mutated":false})";r=ymcc::steamui::read(g_steamSettingsObserver,true);expect(r.value("mode",std::string{})=="desktop"&&r.value("modeEpoch",std::string{})=="renderer:8","valid explicit UI refresh still publishes actual desktop observation");
 // Same original UI-receipt consumer: real common run/lease plus native
 // mode-read body and the exact main ModeObserver callback extracted by runner.
 const auto traceRows=[] {std::vector<Json> rows;for(const auto& row:modeReadDiagnosticRows)if(row.value("event",std::string{})=="steam-recovery-trace")rows.push_back(row);return rows;};
 const auto protocol=[] {return Json{{"id",1},{"error",{{"code",-32000},{"message","Cannot find context private-mode-token"}}}}.dump();};
 setup();failureDiagnosticsProbe=true;failurePacket=protocol();r=ymcc::steamui::read(g_steamSettingsObserver,true);
 expect(r.contains("steamFailure")&&r["steamFailure"].value("domain",std::string{})=="cdp-protocol"&&r["steamFailure"].value("code",0)==-32000&&r.value("reason",std::string{})=="live-api-error"&&r.value("mode",std::string{})=="unknown","UI failed read keeps real protocol facts without changing reason or mode");
 setup();failureDiagnosticsProbe=true;failurePacket=protocol();const auto requested=g_steamSettingsObserver.value;auto sentBefore=dispatchSends;r=productionUiModeRead(requested);auto traces=traceRows();
 expect(traces.size()==1&&traces[0].value("actionOperation",std::string{})=="mode-read"&&traces[0].value("actionPhase",std::string{})=="observation"&&traces[0].value("actionId",std::string{})=="0"&&dispatchSends==sentBefore+1,"actual ModeObserver callback records one existing read observation with no extra request");
 expect(modeReadDiagnosticRows.size()==2&&modeReadDiagnosticRows.front().value("event",std::string{})=="steam-mode-read"&&traces[0].at("steamPid")==requested.pid&&traces[0].at("steamBirth")==std::to_string(requested.created)&&traces[0].at("nativePowerGeneration")==17,"legacy mode log remains and failure keeps the starting Steam and power identity");
 setup();failureDiagnosticsProbe=true;failurePacket=Json{{"id",1},{"result",{{"exceptionDetails",{{"text","private-mode-token undefined-property"},{"lineNumber",31},{"columnNumber",4},{"exception",{{"className","TypeError"},{"description","cannot read properties of undefined secret-stack-account"}}}}}}}}.dump();r=productionUiModeRead(requested);traces=traceRows();
 expect(traces.size()==1&&traces[0]["steamFailure"].value("domain",std::string{})=="javascript"&&traces[0]["steamFailure"].value("exceptionClass",std::string{})=="TypeError"&&traces[0]["steamFailure"].value("line",0)==31,"actual JavaScript source class and line reach the mode observation");
 expect(!traces[0]["steamFailure"].value("codeKnown",true)&&!traces[0]["steamFailure"].contains("code")&&traces[0].dump().size()<2304&&traces[0].dump().find("secret")==std::string::npos&&traces[0].dump().find("private-mode-token")==std::string::npos,"unknown JS code remains unknown and remote text cannot enter the native log");
 setup();failureDiagnosticsProbe=true;failureReceiveError=ERROR_WINHTTP_TIMEOUT;r=productionUiModeRead(requested);traces=traceRows();
 expect(traces.size()==1&&traces[0]["steamFailure"].value("domain",std::string{})=="winhttp"&&traces[0]["steamFailure"].value("code",0)==ERROR_WINHTTP_TIMEOUT&&r.value("mode",std::string{})=="unknown","actual WinHTTP receive failure keeps its original numeric source fact");
 setup();{std::unique_lock serial(transportMutex);r=productionUiModeRead(requested);}
 expect(r.value("reason",std::string{})=="live-busy"&&traceRows().empty()&&modeReadDiagnosticRows.size()==1,"busy without an original failure creates only the unchanged mode log");
 setup();dispatchReplyValue=R"({"ok":true,"mode":"bigpicture","steamFailure":{"schema":1,"domain":"winhttp","codeKnown":true,"code":999},"mutated":false})";r=productionUiModeRead(requested);
 expect(r.value("mode",std::string{})=="bigpicture"&&!r.contains("steamFailure")&&traceRows().empty(),"successful mode reply cannot invent a failure from unsolicited fields");
 setup();failureDiagnosticsProbe=true;failurePacket=protocol();dispatchReplyHook=[&]{auto changed=fresh();++changed.processEpoch;++changed.session.created;++changed.session.pid;g_steamSettingsObserver.publish(changed);g_steamSettingsObserver.value=changed.session;selectedPowerGeneration=99;};r=productionUiModeRead(requested);traces=traceRows();
 expect(!r.value("known",true)&&r.value("reason",std::string{})=="steam-session-changed"&&!r.contains("observedAccount")&&traces.size()==1,"late failed original read is diagnostic only and cannot publish old UI proof");
 expect(traces[0].at("steamPid")==requested.pid&&traces[0].at("steamBirth")==std::to_string(requested.created)&&traces[0].at("nativePowerGeneration")==17&&traces[0]["steamFailure"].value("code",0)==-32000,"late source error retains original identity instead of replacement Steam or power");
 setup();failureDiagnosticsProbe=true;failurePacket=protocol();modeReadTraceThrows=true;r=productionUiModeRead(requested);
 expect(r.value("reason",std::string{})=="live-api-error"&&r.value("mode",std::string{})=="unknown","diagnostic callback failure cannot change the original read or recovery decision");
 setup();dispatchReplyValue=R"({"ok":false,"reason":"live-read-unavailable","steamFailure":{"schema":1,"domain":"winhttp","sourceStage":"receive","codeKnown":true,"code":"private-mode-token","secret":"secret-untrusted-value"},"mutated":false})";r=productionUiModeRead(requested);traces=traceRows();
 expect(traces.size()==1&&!r["steamFailure"].value("codeKnown",true)&&!r["steamFailure"].contains("secret")&&r.dump().find("private-mode-token")==std::string::npos,"mode state revalidates child facts before any status consumer can see them");
 setup();
 expect(processIdentityOpens==processIdentityCloses&&processIdentityUnexpected==0,"all UI read process leases return with no duplicate or unexpected acquisition");
 expect(dispatchHttpOpen-httpBefore==dispatchHttpClose-closedBefore,"all UI HTTP and socket owners return their handles");
 dispatchProbe=processIdentityProbe=false;admissionProbe=false;std::cout<<"UI_RECEIPT_SELECTED_PASS checks="<<listenerChecks-countBefore<<" actualSteamOperations=0\n";
}
#endif

#endif // original ui receipt cases or directed current cases
static void deckyFinalAdmissionFixture() {
 admissionProbe=true;admissionPortReads=0;processIdentityProbe=true;dispatchProbe=false;
 processIdentityOpen=processIdentityPath=processIdentityTimes=processIdentityAlive=true;
 processIdentityOpens=processIdentityCloses=processIdentityUnexpected=0;
 ymcc::processInventory::invalidate();
 ymcc::steamsession::Session session;session.root=L"P:\\YMCC-identity-fixture";
 session.pid=processIdentityPid;session.created=processIdentityBirth;session.account=99;
 const std::string build(64,'a');
 Json runtime={{"enabled",true},{"pid",0u},{"revision",1u},{"phase","frontend-attached"},{"reason","retained-frontend-connected"}};
 Json mode={{"known",true},{"running",true},{"pid",session.pid},{"birth",std::to_string(session.created)},
   {"mode","bigpicture"},{"modeEpoch","doc:1"},{"deckyFrontendLoaded",true},
   {"deckyEmptyFrontend",{{"protocol",1},{"frontendBuildId",build},{"loaderVersion","v3.3.1"}}}};
 Json args={{"userInitiated",true},{"pid",session.pid},{"birth",std::to_string(session.created)},{"revision",1u}};
 ymcc::deckyupdate::Recovery recovery;
 unsigned consentCalls=0,preflightCalls=0,dispatchCalls=0;
 ymcc::deckyrepair::Dependencies deps;
 deps.session=[&]{return session;};deps.runtime=[&]{return runtime;};
 deps.mode=deps.freshMode=[&](const auto&){return mode;};deps.installed=[&]{return build;};deps.loader="v3.3.1";
 deps.now=[]{return 100ull;};deps.exiting=[]{return false;};
 deps.preflight=[&]{++preflightCalls;return std::string{};};
 // Exact main::deckySidebarRepairDependencies consent binding, with its real
 // Recovery implementation. Only resource/identity/transport IO boundaries are inert.
 deps.consent=[&](const auto& s){++consentCalls;return recovery.allowManualRepair(s);};
 g_steamSettingsObserver.value=session;
 const auto mainDispatch=actualRepairDispatchForDrain();
 deps.dispatch=[&](const auto& s,const auto& action,const auto& admit){++dispatchCalls;return mainDispatch(s,action,admit);};
 ymcc::deckyrepair::ManualRepair repair;
 const bool before=recovery.allows(session);
 const auto result=repair.request(args,deps);
 const bool after=recovery.allows(session);
 std::cout<<"DECKY_FINAL_ADMIT_ACTUAL "<<Json{{"beforeAllowed",before},{"afterAllowed",after},{"consentCalls",consentCalls},
   {"preflightCalls",preflightCalls},{"dispatchCalls",dispatchCalls},{"portReads",admissionPortReads.load()},{"processOpens",processIdentityOpens},{"processCloses",processIdentityCloses},{"unexpectedProcessCalls",processIdentityUnexpected},{"receipt",result}}.dump()<<"\n";
 expect(!before&&dispatchCalls==1&&admissionPortReads>0,"selected actual ManualRepair reaches only denied offline port discovery");
 expect(!result.value("ok",true)&&!result.value("mutated",true)&&!result.value("requestDispatched",false),"denied discovery never submits a Steam action");
 expect(consentCalls==0&&!after&&preflightCalls==1,"failed discovery must not invoke final consent or publish RAM recovery authority");
 processIdentityProbe=false;admissionProbe=false;ymcc::processInventory::invalidate();
}

static void expectedSteamIdentityFixture(const std::string& startAt={}){
 processIdentityProbe=true;admissionProbe=true;admissionPortReads=0;
 processIdentityOpens=processIdentityCloses=processIdentityUnexpected=0;
 const ymcc::steamsession::Session expected{L"P:\\YMCC-identity-fixture",processIdentityPid,42,processIdentityBirth};
 auto reset=[&]{processIdentityOpen=processIdentityPath=processIdentityTimes=processIdentityAlive=true;ymcc::processInventory::invalidate();};
 unsigned finalCalls=0;
 auto attempt=[&](ymcc::steamsession::Session identity,const Json& request=Json{{"operation","settings.set"}}){return run(expected.root,request,[&]{++finalCalls;return true;},0,[]{return true;},&identity);};
 auto refused=[&](const Json& r,const char* label){expect(!r.value("ok",true)&&r.value("reason",std::string{})=="steam-session-changed"&&!r.value("mutated",true)&&!r.value("requestDispatched",true)&&finalCalls==0&&admissionPortReads==0,label);};
 if(startAt.empty()){
 reset();auto invalid=expected;invalid.created=0;refused(attempt(invalid),"expected session without birth cannot start discovery");
 reset();invalid=expected;invalid.root=L"P:\\OtherSteam";refused(attempt(invalid),"expected session from a different root cannot start discovery");
 reset();invalid=expected;++invalid.created;refused(attempt(invalid),"same PID with another birth is refused before discovery");
 reset();processIdentityOpen=false;refused(attempt(expected),"process query denial cannot fall back to an unbound endpoint");
 reset();processIdentityPath=false;refused(attempt(expected),"process path read failure cannot select another Steam instance");
 reset();processIdentityTimes=false;refused(attempt(expected),"process birth read failure cannot select another Steam instance");
 reset();processIdentityAlive=false;refused(attempt(expected),"an exited expected process is refused before discovery");
 reset();refused(attempt(expected,{{"operation","sleep.inspect"},{"sleepSessionProcess",{{"pid",processIdentityPid+1}}}}),"operation PID cannot override the shared expected Steam process");
 reset();{
   const auto beforeClose=processIdentityCloses;
   {SteamProcessLease lease;expect(lease.bind(expected)&&lease.current(),"shared verified process lease binds the original birth and path");processIdentityAlive=false;expect(!lease.current(),"held process exit revokes the final current check");}
   expect(processIdentityCloses==beforeClose+1,"held process lease closes its one owned handle");
 }
 reset();{
   SteamProcessLease lease;auto stale=expected;++stale.created;
   expect(!lease.bind(stale)&&!lease.current(),"failed birth binding never leaves current process authority");
 }
 reset();{
   SteamProcessLease lease;if(!lease.bind(expected))throw std::runtime_error("initial lease unavailable");
   expect(!lease.bind(expected)&&!lease.current(),"a second bind cannot preserve authority from an earlier lease");
 }
 }
 if(startAt.empty()||startAt=="copied-identity"){
 reset();auto caller=expected;unsigned ownerChecks=0;
 const auto receipt=run(expected.root,{{"operation","settings.set"}},[&]{++finalCalls;return true;},0,[&]{++ownerChecks;caller.pid=99;caller.created=1;caller.root=L"P:\\OtherSteam";return true;},&caller);
 std::cout<<"EXPECTED_IDENTITY_COPY_OBSERVATION "<<Json{{"receipt",receipt},{"ownerChecks",ownerChecks},{"portReads",admissionPortReads.load()},{"finalCalls",finalCalls},{"continuedFrom",startAt}}.dump()<<"\n";
 expect(!receipt.value("ok",true)&&receipt.value("reason",std::string{})=="live-debug-unavailable"&&!receipt.value("mutated",true)&&!receipt.value("requestDispatched",true)&&admissionPortReads==1&&finalCalls==0&&ownerChecks==10,
    "queued callbacks cannot replace copied identity and denied discovery cannot consume final admission");
 expect(processIdentityCloses+(startAt.empty()?1u:0u)==processIdentityOpens&&processIdentityUnexpected==0,"every granted process handle returned and only reviewed process ports used");
 }
 dispatchProbe=true;
 const auto startSends=dispatchSends,startHttp=dispatchHttpOpen,startHttpClose=dispatchHttpClose;
 const auto acceptedOffset=startAt=="pre-admit"?0u:1u;
 auto setup=[&]{reset();dispatchRead=0;dispatchOwner=true;dispatchExitOnUpgrade=false;dispatchSendFails=false;finalCalls=0;};
 auto closed=[](const Json& r){return !r.value("ok",true)&&!r.value("mutated",true)&&!r.value("requestDispatched",true);};
 if(startAt!="pre-admit"){
 setup();{
   const auto r=attempt(expected);
   expect(r.value("ok",false)&&dispatchSends==startSends+acceptedOffset&&finalCalls==1,"actual transport positive control reaches final consent exactly once and sends once");
 }
 setup();dispatchExitOnUpgrade=true;{
   const auto r=attempt(expected);
   expect(closed(r)&&finalCalls==0&&dispatchSends==startSends+acceptedOffset,"Steam exit during socket upgrade cancels actual dispatch before final consent");
 }
 }
 setup();unsigned pureCalls=0,postUpgradeProcessChecks=0,finalProcessFaults=0;bool upgraded=false;{
   dispatchUpgradeHook=[&]{upgraded=true;};
   processIdentityWaitHook=[&]{if(upgraded&&++postUpgradeProcessChecks==2){++finalProcessFaults;processIdentityAlive=false;}};
   const auto r=run(expected.root,{{"operation","settings.set"}},[&]{++finalCalls;return true;},0,[&]{++pureCalls;return true;},&expected);
   std::cout<<"EXPECTED_FINAL_PROCESS_OBSERVATION "<<Json{{"receipt",r},{"upgraded",upgraded},{"pureCalls",pureCalls},{"postUpgradeProcessChecks",postUpgradeProcessChecks},{"finalProcessFaults",finalProcessFaults},{"finalCalls",finalCalls},{"sends",dispatchSends-startSends},{"acceptedOffset",acceptedOffset}}.dump()<<"\n";
   expect(closed(r)&&r.value("stage",std::string{})=="native-live-pre-dispatch"&&upgraded&&postUpgradeProcessChecks==2&&finalProcessFaults==1&&finalCalls==0&&dispatchSends==startSends+acceptedOffset,"exit observed during final held-process check cannot consume final consent or send");
   dispatchUpgradeHook={};processIdentityWaitHook={};
 }
 setup();{
   const auto r=run(expected.root,{{"operation","settings.set"}},[&]{++finalCalls;processIdentityAlive=false;return true;},0,[]{return true;},&expected);
   expect(closed(r)&&finalCalls==1&&dispatchSends==startSends+acceptedOffset,"process exit inside final admission is rechecked before actual WebSocket send");
 }
 setup();{
   const auto r=run(expected.root,{{"operation","settings.set"}},[&]{++finalCalls;dispatchOwner=false;return true;},0,[&]{return dispatchOwner;},&expected);
   expect(closed(r)&&finalCalls==1&&dispatchSends==startSends+acceptedOffset,"owner revoked inside final admission is rechecked without repeating consent");
 }
 setup();dispatchSendFails=true;{
   const auto r=attempt(expected);
   expect(!r.value("ok",true)&&r.value("mutated",false)&&r.value("requestDispatched",false)&&r.value("reason",std::string{})=="live-transport-uncertain"&&finalCalls==1&&dispatchSends==startSends+acceptedOffset+1,"attempted send failure preserves uncertainty and never retries the action");
 }
 expect(dispatchHttpOpen>startHttp&&dispatchHttpOpen-startHttp==dispatchHttpClose-startHttpClose,"all actual transport HTTP and socket owners return their handles");
 expect(processIdentityCloses+(startAt.empty()?1u:0u)==processIdentityOpens&&processIdentityUnexpected==0,"all dispatched and cancelled process leases return their handles");
 dispatchProbe=false;
 reset();processIdentityProbe=false;admissionProbe=false;
 std::cout<<"STEAM_EXPECTED_IDENTITY_SELECTED_PASS checks="<<listenerChecks<<" actualSteamOperations=0\n";
}
#include "failure_diagnostics_cases.inc"
#endif

#ifdef YMCC_STEAM_DOCUMENT_READ_ONLY
#include "fixtures/steam_document_popup_cases.inc"
int main(int argc,char**argv){try{
#ifdef YMCC_FORMAL_COMMAND_PROVIDER
 ymcc::steamcommand::Coordinator commandCoordinator;
 ymcc::steamcommand::Provider commandProvider(commandCoordinator);
 if(!commandProvider)return 2;
#endif
 if(const auto selected=listenerCompleteSelection(argc,argv))return *selected;
 if(argc>1&&std::string(argv[1])=="--document-popup-only")return documentPopupFixture(argc,argv);
 if(const auto selected=endpointDiscoverySelection(argc,argv))return *selected;
 if(argc==4&&std::string(argv[1])=="--style-targets"){
  std::ifstream stream(argv[2]);Json raw;stream>>raw;ymcc::steamlive::Endpoint endpoint;endpoint.host=L"127.0.0.1";endpoint.port=raw.at("port").get<INTERNET_PORT>();
  Json rows=Json::array();for(const auto& target:ymcc::steamdocuments::targets(raw.at("targets"),endpoint))rows.push_back({{"kind",ymcc::steamdocuments::name(target.kind)},{"id",target.id},{"url",target.url}});
  if(std::none_of(rows.begin(),rows.end(),[](const auto& row){return row.at("kind")=="SP";}))throw std::runtime_error("document-main-unavailable");
  std::ofstream(argv[3])<<rows.dump();return 0;
 }
 if(argc==4&&std::string(argv[1])=="--style-expressions"){
  std::ifstream stream(argv[2]);Json raw;stream>>raw;const auto plan=ymcc::steamdocuments::parseStylePlan(raw);
  std::ofstream(argv[3])<<Json{{"SP",ymcc::steamdocuments::expression(plan.sp)},{"MainMenu",ymcc::steamdocuments::expression(plan.mainMenu)},{"QuickAccess",ymcc::steamdocuments::expression(plan.quickAccess)}}.dump();return 0;
 }
 if(argc!=3||std::string(argv[1])!="--document-read-only")throw std::runtime_error("Exact document selection required");
 using namespace ymcc::steamdocuments;using ymcc::steamsession::Session;
 processIdentityProbe=dispatchProbe=documentProbe=true;admissionProbe=true;
 Session expected{L"P:\\YMCC-identity-fixture",processIdentityPid,0,processIdentityBirth};
 StylePlan plan;plan.sp=plan.mainMenu=plan.quickAccess=Properties{{"--ymcc-file-0000000000000000"},{"--ymcc-file-0000000000000000"}};
 auto pages=[](bool menu=true,bool quick=true){Json tabs=Json::array();for(auto pair:{std::pair{"main","Steam Big Picture"},std::pair{"menu","MainMenu_uid1"},std::pair{"quick","QuickAccess_uid2"}}){if((std::string(pair.first)=="menu"&&!menu)||(std::string(pair.first)=="quick"&&!quick))continue;tabs.push_back({{"id",pair.first},{"type","page"},{"title",pair.second},{"url","https://steamloopback.host/index.html"},{"webSocketDebuggerUrl",std::string("ws://127.0.0.1:8080/devtools/page/")+pair.first}});}return tabs;};
 auto setup=[&]{processIdentityOpen=processIdentityPath=processIdentityTimes=processIdentityAlive=true;dispatchOwner=true;dispatchRead=documentTargetReads=0;dispatchExitOnUpgrade=dispatchSendFails=documentReplaced=documentTargetsChanged=false;dispatchReplyHook={};documentHref="https://steamloopback.host/index.html";documentTargets=pages().dump();ymcc::processInventory::invalidate();};
 auto attempt=[&]{return readStyles(expected,plan,[&]{return dispatchOwner;},4000);};
 const auto base=listenerChecks;
 setup();auto value=attempt();expect(value.value("ok",false)&&value.value("complete",false)&&value["documents"].size()==3,"public document reader observes actual three typed contexts in one serialized batch");
 expect(value["documents"][0].contains("sourceUrl")&&value["documents"][0]["value"].value("documentKey",std::string{})=="original","each document carries its own actual target and document source receipt");
 setup();documentTargets=pages(false,false).dump();value=attempt();expect(value.value("ok",false)&&!value.value("complete",true)&&value.value("sequential",false)&&value["documents"].size()==1,"uncreated auxiliary contexts are explicitly partial rather than fabricated by SharedJS");
 for(const auto* route:{"https://steamloopback.host/routes/library/home","https://steamloopback.host/routes/library/app/413080","https://steamloopback.host/routes/settings/controller?tab=layout#details"}){
  setup();auto routePages=pages(false,false);routePages[0]["url"]=route;documentHref=route;documentTargets=routePages.dump();value=attempt();
  expect(value.value("ok",false)&&!value.value("complete",true)&&value["documents"].size()==1&&value["documents"][0]["sourceUrl"]==route,"actual public document reader accepts current Steam SPA route without weakening partial context status");
 }
 setup();auto routePages=pages(false,false);routePages[0]["url"]="https://steamloopback.host/routes/library/home";documentTargets=routePages.dump();value=attempt();
 expect(!value.value("ok",true)&&value["documents"].empty(),"route acceptance still requires receipt href to equal the held target");
 setup();routePages=pages(false,false);routePages[0]["url"]="https://steamloopback.host/routes/library/home";documentHref=routePages[0]["url"].get<std::string>();documentTargets=routePages.dump();documentReplaced=true;value=attempt();
 expect(!value.value("ok",true)&&value["documents"].empty(),"Steam SPA route does not bypass final document-generation revalidation");
 setup();routePages=pages(false,false);routePages[0]["url"]="https://steamloopback.host/routes/library/home";documentHref=routePages[0]["url"].get<std::string>();documentTargets=routePages.dump();dispatchReplyHook=[] {dispatchOwner=false;};value=attempt();
 expect(!value.value("ok",true)&&value["documents"].empty(),"Steam SPA route preserves owner revocation during the real shared read");
 setup();routePages=pages(false,false);routePages[0]["url"]="https://steamloopback.host/routes/library/home";documentHref=routePages[0]["url"].get<std::string>();documentTargets=routePages.dump();documentTargetsChanged=true;value=attempt();
 expect(!value.value("ok",true)&&value["documents"].empty(),"Steam SPA route preserves the final target-set check");
 setup();documentReplaced=true;value=attempt();expect(!value.value("ok",true)&&value["documents"].empty(),"replacement of an earlier document revokes all sampled success");
 setup();documentTargetsChanged=true;value=attempt();expect(!value.value("ok",true)&&value["documents"].empty(),"a changed final target set cannot publish a stale batch");
 setup();dispatchOwner=false;const auto before=admissionPortReads.load();value=attempt();expect(!value.value("ok",true)&&admissionPortReads==before,"expired ownership stops before discovery");
 setup();dispatchReplyHook=[] {dispatchOwner=false;};value=attempt();expect(!value.value("ok",true)&&value["documents"].empty(),"owner revocation during a reply cannot escape as success");
 setup();dispatchExitOnUpgrade=true;value=attempt();expect(!value.value("ok",true),"held original Steam exit stops document sends");
 setup();{std::lock_guard lock(transportMutex);value=attempt();}expect(!value.value("ok",true)&&value.value("reason",std::string{})=="live-busy","theme read never queues behind a shared Steam mutation");
 setup();auto tabs=pages();tabs[0]["webSocketDebuggerUrl"]="ws://example.invalid:8080/devtools/page/main";documentTargets=tabs.dump();value=attempt();expect(!value.value("ok",true),"foreign websocket URLs are refused by the shared reader");
 setup();tabs=pages();tabs.push_back(tabs[0]);documentTargets=tabs.dump();value=attempt();expect(!value.value("ok",true),"duplicate document identities cannot be called independent contexts");
 setup();documentTargets=Json::array({{{"id","shared"},{"type","page"},{"title","SharedJSContext"},{"url","https://steamloopback.host/"},{"webSocketDebuggerUrl","ws://127.0.0.1:8080/devtools/page/shared"}}}).dump();value=attempt();expect(!value.value("ok",true),"SharedJS alone is never evidence that the Big Picture DOM has theme CSS");
 bool bad=false;auto invalid=plan;invalid.sp.values={"--x');fetch('unsafe')"};try{validate(invalid.sp);}catch(...){bad=true;}expect(bad,"property vocabulary cannot inject caller JavaScript into the fixed read");
 bad=false;try{parseStylePlan({{"SP",Json::object()}});}catch(...){bad=true;}expect(bad,"incomplete context plans are refused before shared transport");
 std::cout<<"DOCUMENT_HANDLE_OBSERVATION opens="<<processIdentityOpens<<" closes="<<processIdentityCloses<<" httpOpen="<<dispatchHttpOpen<<" httpClose="<<dispatchHttpClose<<" unexpected="<<processIdentityUnexpected<<'\n';

 const Target typed{Kind::BigPicture,"main","/devtools/page/main","https://steamloopback.host/index.html"};
 const Json healthy{{"documentKey","original"},{"sourceOrigin","https://steamloopback.host"},{"sourceHref",typed.url},{"protocol",4},{"ready",true},{"styles",1},{"values",Json::array({"1"})},{"fileCounts",Json::array({1})}};
 validateReceipt(healthy,plan.sp,typed);expect(true,"typed receipt validates protocol, ready, source and exact plan lengths");
 for(const auto& mutation:std::vector<std::pair<const char*,Json>>{{"protocol",true},{"protocol",3},{"ready",false},{"ready",1},{"styles",1.5},{"styles",-1},{"values",Json::array()},{"values",Json::array({true})},{"values",Json::array({std::string(97,'x')})},{"fileCounts",Json::array({1.5})},{"fileCounts",Json::array({-1})},{"fileCounts",Json::array({16385})},{"sourceOrigin","https://example.invalid"},{"sourceHref","https://steamloopback.host/foreign"}}){auto value=healthy;value[mutation.first]=mutation.second;bool rejected=false;try{validateReceipt(value,plan.sp,typed);}catch(...){rejected=true;}expect(rejected,"malformed or untrusted typed document receipt is refused");}
 for(const auto& url:{"https://example.invalid/?browserType=3","https://steamloopback.host.evil/index.html?browserType=3","https://steamloopback.host/foreign?browserType=3","about:blank?browserType=3",
  "https://steamloopback.host.evil/routes/library/home","https://steamloopback.host@evil.invalid/routes/library/home","http://steamloopback.host/routes/library/home","https://steamloopback.host:443/routes/library/home","https://steamloopback.host/routes.evil/library/home","https://steamloopback.host/foreign/routes/library/home","https://steamloopback.host/routes","https://steamloopback.host/index.html/routes/library/home"}){auto list=pages();list[0]["url"]=url;bool rejected=false;try{targets(list,testEndpoint);}catch(...){rejected=true;}expect(rejected,"external or unexpected-path Big Picture lookalikes are refused");}
 expect(processIdentityOpens==processIdentityCloses&&dispatchHttpOpen==dispatchHttpClose,"all temporary shared process and HTTP/socket handles are returned");
 std::ofstream(argv[2])<<Json{{"ok",true},{"passed",listenerChecks-base},{"selection","document-read-only"},{"expressionPacket",Json::parse(documentExpression)},{"realSteamOperations",0},{"realCssOperations",0}}.dump(2);std::cout<<"DOCUMENT_READ_SELECTED_PASS checks="<<listenerChecks-base<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
#else
#include "fixtures/steam_menu_pure_owner_cases.inc"
#include "fixtures/steam_transport_queue_cancel_cases.inc"
static void transportControlsFixture(bool selected=false) {
 const auto checksBefore=listenerChecks;
 const ymcc::steamsession::Session controlledExpected{L"S:\\YMCC-owned-offline-fixture",processIdentityPid,42,processIdentityBirth};
 // Exercise the real shared channel. Any accidentally reached discovery is
 // counted and denied by the OS port-table boundary, never a Steam connection.
 admissionProbe=true;admissionPortReads=0;
 for(const auto& malformed:Json::array({nullptr,1,"settings.set",Json::array(),Json::object(),Json{{"operation",1}},Json{{"operation","settings.set.extra"}},Json{{"operation","menu.qam.unknown"}}})) {
   unsigned admitted=0;
   const auto receipt=run(L"S:\\YMCC-owned-offline-fixture",malformed,[&]{++admitted;return true;});
   expect(receipt.value("reason",std::string{})=="invalid-live-operation"&&!receipt.value("mutated",true)&&
       !receipt.value("requestDispatched",true)&&admitted==0&&admissionPortReads==0,
       "invalid requests stop at the common gate before owner callbacks or port discovery");
 }
 for(const auto* op:{"settings.get","settings.set","menu.button","menu.qam.retry","sleep.session.inspect","sleep.inspect","sleep.reconcile","sleep.input-resume","input.gamepadLease","ui.mode.get","decky.recoverConnection"}) {
   unsigned finalAdmissions=0;
   const auto receipt=run(L"S:\\YMCC-owned-offline-fixture",{{"operation",op}},[&]{++finalAdmissions;return true;},400,[]{return false;});
   expect(receipt.value("reason",std::string{})=="live-action-cancelled"&&!receipt.value("mutated",true)&&
       !receipt.value("requestDispatched",true)&&finalAdmissions==0&&admissionPortReads==0,
       "all existing request families reject an expired owner before discovery or final admission");
 }
 {
   const auto receipt=run(L"S:\\YMCC-owned-offline-fixture",{{"operation","settings.set"}},{},0,[]()->bool{throw std::runtime_error("expired owner");});
   expect(receipt.value("reason",std::string{})=="live-action-cancelled"&&!receipt.value("mutated",true)&&admissionPortReads==0,
       "owner predicate failure closes the common gate without dispatch");
 }
 {
   std::unique_lock owner(transportMutex);std::atomic<bool> valid{true};std::atomic<unsigned> checks{0};
   std::promise<void> entered;auto ready=entered.get_future();
   auto pending=std::async(std::launch::async,[&]{return run(L"S:\\YMCC-owned-offline-fixture",{{"operation","settings.set"}},{},0,[&]{if(++checks==1)entered.set_value();return valid.load();},&controlledExpected);});
   const bool reached=ready.wait_for(std::chrono::seconds(2))==std::future_status::ready;
   valid=false;owner.unlock();const auto receipt=pending.get();
   expect(reached&&checks==2&&receipt.value("reason",std::string{})=="live-action-cancelled"&&
       !receipt.value("mutated",true)&&!receipt.value("requestDispatched",true)&&admissionPortReads==0,
       "an owner cancelled while queued is rechecked before any process or port discovery");
 }
 const auto setter=ymcc::steamrequest::classify("settings.set"),readOnly=ymcc::steamrequest::classify("sleep.inspect");
 expect(setter.known&&setter.setter&&!setter.shortRead&&!setter.runningLinkRead&&
     readOnly.known&&!readOnly.setter&&readOnly.runningLinkRead,
     "shared operation policy keeps settings writes separate from running-link reads");
 expect(ymcc::steamrequest::classify("menu.qam.retry").setter&&ymcc::steamrequest::classify("menu.qam.observe").qamFollowup&&
     !ymcc::steamrequest::classify("menu.qam.other").known,
     "QAM prefixes cannot invent an operation or bypass typed uncertainty classification");
 admissionProbe=false;
 for(const char* op:{"desktopMouse.get","desktopMouse.set"}){
   std::promise<void> locked,release;auto ready=locked.get_future();auto done=release.get_future();
   std::thread owner([&]{std::lock_guard guard(transportMutex);locked.set_value();done.wait_for(std::chrono::milliseconds(500));});ready.wait();
   const auto started=std::chrono::steady_clock::now();
   const auto receipt=run(controlledExpected.root,{{"operation",op}},{},0,{},&controlledExpected);
   const auto elapsed=std::chrono::steady_clock::now()-started;release.set_value();owner.join();
   expect(!receipt.value("ok",true)&&receipt.value("reason",std::string{})=="live-busy"&&!receipt.value("mutated",true)&&elapsed<std::chrono::milliseconds(200),"desktop mouse never queues behind another Steam transport owner");
   std::cout << "TRANSPORT_BUSY_OBSERVATION " << Json{{"operation",op},{"elapsedMs",std::chrono::duration<double,std::milli>(elapsed).count()},{"limitMs",200}}.dump() << "\n";
 }

 if(selected)std::cout<<"TRANSPORT_CONTROLS_SELECTED_RESULT "<<Json{{"ok",true},{"selection","transport-controls"},{"checks",listenerChecks-checksBefore},{"originalDefaultGroupExecuted",false},{"realSteamOperations",0}}.dump()<<"\n";
}
int main(int argc,char** argv) {try {
#ifdef YMCC_FORMAL_COMMAND_PROVIDER
 ymcc::steamcommand::Coordinator commandCoordinator;
 ymcc::steamcommand::Provider commandProvider(commandCoordinator);
 if(!commandProvider)return 2;
#endif
 if(argc==4&&std::string(argv[1])=="--main-shared-identity-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="menu-pure-owner")return menuPureOwnerFixture(false);
 if(argc==6&&std::string(argv[1])=="--main-shared-identity-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="menu-pure-owner"&&std::string(argv[4])=="--phase"&&std::string(argv[5])=="pre-dispatch")return menuPureOwnerFixture(false,"pre-dispatch");
 if(const auto selected=listenerCompleteSelection(argc,argv))return *selected;
#ifdef YMCC_MODE_READ_CURRENT_FIXTURE
 if(argc==4&&std::string(argv[1])=="--mode-read-current-only"&&std::string(argv[2])=="--case")return modeReadCurrentFixture(argv[3]);
 return 2;
#else
 if(const auto selected=endpointDiscoverySelection(argc,argv))return *selected;
#ifdef YMCC_FAILURE_OLD_SOURCE
 if(argc==2&&std::string(argv[1])=="--failure-diagnostics-old-source-negative-only")return failureDiagnosticsFixture(true);
#else
 if(argc==2&&std::string(argv[1])=="--failure-diagnostics-only")return failureDiagnosticsFixture(false);
 if(argc==4&&std::string(argv[1])=="--failure-diagnostics-only"&&std::string(argv[2])=="--case")return failureDiagnosticsFixture(false,argv[3]);
#endif
 if(argc>1&&std::string(argv[1]).find("failure-diagnostics")!=std::string::npos)return 2;
#ifdef YMCC_UI_RECEIPT_FIXTURE
#ifdef YMCC_UI_PUBLIC_READ_OWNER_FIXTURE
 if(argc==4&&std::string(argv[1])=="--ui-receipt-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="public-read-owner"){publicUiReadOwnerFixture(false);return 0;}
 if(argc==4&&std::string(argv[1])=="--ui-receipt-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="public-show-owner"){publicUiReadOwnerFixture(true);return 0;}
#endif
 if(argc==2&&std::string(argv[1])=="--ui-receipt-only"){uiReceiptFixture();uiReceiptFixture(false,true);return 0;}
 if(argc==4&&std::string(argv[1])=="--ui-receipt-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="late-caught-failure"){uiReceiptFixture(true);return 0;}
 if(argc==4&&std::string(argv[1])=="--ui-receipt-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="client-state"){uiReceiptFixture(false,true);return 0;}
#endif
 if(argc>1&&std::string(argv[1])=="--ui-receipt-only")return 2;
 if(argc==2&&std::string(argv[1])=="--main-shared-identity-only"){mainSharedIdentityFixture();return 0;}
 if(argc==4&&std::string(argv[1])=="--main-shared-identity-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="native-owner"){mainSharedIdentityFixture(true);return 0;}
 if(argc==4&&std::string(argv[1])=="--main-shared-identity-only"&&std::string(argv[2])=="--case"){
   const std::string selected=argv[3];
#ifdef YMCC_SOCKET_OWNER_CONSTRUCTION_FIXTURE
   if(selected=="socket-owner-construction"){mainSharedIdentityFixture(true,selected);return 0;}
#endif
   if(selected=="command-busy-return"){mainSharedIdentityFixture(true,selected);return 0;}
   if(selected=="message-assembly"){mainSharedIdentityFixture(true,selected);return 0;}
   if(selected=="source-drain"||selected=="source-drain-bound"||selected=="source-drain-repair"||selected=="source-drain-write"||selected=="source-drain-missing"){mainSharedIdentityFixture(true,selected);return 0;}
#ifdef YMCC_UI_RECEIPT_FIXTURE
   if(selected=="source-drain-ui"){mainSharedIdentityFixture(true,selected);return 0;}
#endif
 }
 if(argc==4&&std::string(argv[1])=="--main-shared-identity-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="discovery-cancel"){mainSharedIdentityFixture(true,"discovery-cancel");return 0;}
 if(argc==4&&std::string(argv[1])=="--main-shared-identity-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="native-return"){mainSharedIdentityFixture(true,"native-return");return 0;}
 if(argc==4&&std::string(argv[1])=="--main-shared-identity-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="mutation-policy"){mainSharedIdentityFixture(true,"mutation-policy");return 0;}
 if(argc==4&&std::string(argv[1])=="--main-shared-identity-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="menu-mutation-fields"){mainSharedIdentityFixture(true,"menu-mutation-fields");return 0;}
 if(argc==4&&std::string(argv[1])=="--main-shared-identity-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="process-pid-fields"){mainSharedIdentityFixture(true,"process-pid-fields");return 0;}
 if(argc==4&&std::string(argv[1])=="--main-shared-identity-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="defer-receipt"){mainSharedIdentityFixture(true,"defer-receipt");return 0;}
 if(argc==4&&std::string(argv[1])=="--main-shared-identity-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="reply-envelope"){mainSharedIdentityFixture(true,"reply-envelope");return 0;}
 if(argc==6&&std::string(argv[1])=="--main-shared-identity-only"&&std::string(argv[2])=="--case"&&std::string(argv[3])=="native-owner"&&std::string(argv[4])=="--from"&&std::string(argv[5])=="queue-exit"){mainSharedIdentityFixture(true,"queue-exit");return 0;}
 if(argc>1&&std::string(argv[1])=="--main-shared-identity-only")return 2;
 if(argc>1&&std::string(argv[1])=="--transport-controls-only"){if(argc==4&&std::string(argv[2])=="--case")return transportQueueCancelFixture(argv[3]);if(argc!=2)return 2;transportControlsFixture(true);return 0;}
 if(argc==4&&std::string(argv[1])=="--expected-steam-identity-only"&&std::string(argv[2])=="--from"&&(std::string(argv[3])=="copied-identity"||std::string(argv[3])=="dispatch"||std::string(argv[3])=="pre-admit")){expectedSteamIdentityFixture(argv[3]);return 0;}
 if(argc==2&&std::string(argv[1])=="--expected-steam-identity-only"){expectedSteamIdentityFixture();return 0;}
 if(argc>1&&std::string(argv[1])=="--expected-steam-identity-only")return 2;
 if(argc==2 && std::string(argv[1])=="--decky-final-admit-only") { deckyFinalAdmissionFixture();std::cout<<"DECKY_FINAL_ADMIT_SELECTED_PASS checks="<<listenerChecks<<"\n";return 0;}

 if(argc==4 && std::string(argv[1])=="--listener-inventory-only" && std::string(argv[2])=="--case" && std::string(argv[3])=="endpoint-parent-prefilter") {
   listenerTopologyFixture::endpointParentPrefilter(false); return 0;
 }
 if(argc==4 && std::string(argv[1])=="--listener-inventory-only" && std::string(argv[2])=="--case" && std::string(argv[3])=="endpoint-parent-prefilter-cost") {
   listenerTopologyFixture::endpointParentPrefilter(true); return 0;
 }
 if(argc==2 && std::string(argv[1])=="--listener-inventory-only") {
   listenerTopologyFixture::run();
   std::cout << "LISTENER_INVENTORY_SELECTED_PASS checks=" << listenerChecks << " steamCdpAccess=false" << "\n";
   return 0;
 }
 const auto fixtureStarted = std::chrono::steady_clock::now();
 transportControlsFixture();

 expect(isRunningLinkRead({{"operation","sleep.inspect"},{"runningLinkRead",true}}),"running link inspection has typed read-only admission");
 expect(isRunningLinkRead({{"operation","sleep.session.inspect"},{"runningLinkRead",true}}),"independent running link account bind is a read");
 for(const auto* op:{"sleep.reconcile","sleep.input-resume","settings.set","menu.button","overlay.set","unknown"})
   expect(!isRunningLinkRead({{"operation",op},{"runningLinkRead",true}}),"running-link flag cannot admit a mutator or unknown operation");
 for(const auto& value:Json::array({false,0,1,"true",nullptr,Json::object(),Json::array()}))
   expect(!isRunningLinkRead({{"operation","sleep.inspect"},{"runningLinkRead",value}}),"malformed or off running-link flags retain ordinary transport");
 expect(!isRunningLinkRead({{"operation","sleep.inspect"}}),"ordinary sleep inspection retains its original transport path");
 expect(!isRunningLinkRead({{"operation",1},{"runningLinkRead",true}}),"invalid operation cannot request background admission");
 auto t=target();expect(targetPath(Json::array({t}),testEndpoint)=="/devtools/page/ABC-123","only verified local SharedJSContext selected");
 for(const auto* ws:{"ws://evil.example/devtools/page/A","ws://127.0.0.1:9090/devtools/page/A","ws://127.0.0.1:8080/devtools/page/A?url=evil","ws://127.0.0.1:8080/devtools/page/../../json"}) {t=target();t["webSocketDebuggerUrl"]=ws;expect(rejected(Json::array({t})),"untrusted websocket URL refused");}
 t=target();expect(rejected(Json::array({t,t})),"ambiguous shared context refused");t["title"]="OtherWindow";expect(rejected(Json::array({t})),"other UI page never selected");
 expect(canDefer({{"ok",false},{"reason","live-debug-unavailable"},{"mutated",false}}),"disabled debugging may defer");
 expect(!canDefer({{"ok",false},{"reason","live-debug-unavailable"},{"mutated",true}}),"uncertain mutation cannot defer");
 expect(!canDefer({{"ok",false},{"reason","live-editor-busy"},{"mutated",false}}),"dirty editor cannot defer");


 for(const unsigned short port:std::vector<unsigned short>{7777,39217,65000}) {
   t=target();t["webSocketDebuggerUrl"]="ws://127.0.0.1:"+std::to_string(port)+"/devtools/page/ABC-123";
   expect(targetPath(Json::array({t}),Endpoint{987,port,L"127.0.0.1"})=="/devtools/page/ABC-123","dynamic Steam-owned port is accepted, not fixed to this PC");
 }
 t=target();t["webSocketDebuggerUrl"]="ws://[::1]:39217/devtools/page/ABC-123";
 expect(targetPath(Json::array({t}),Endpoint{987,39217,L"::1"})=="/devtools/page/ABC-123","IPv6 loopback target parsing supported");


 expect(listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\steamwebhelper.exe",L"S:\\Steam\\steam.exe",4000,4000),"sleep discovery binds a helper to the exact expected Steam PID");
 expect(!listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\steamwebhelper.exe",L"S:\\Steam\\steam.exe",5000,4000),"same-root helper from another Steam process cannot serve the captured sleep session");
 expect(listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\steamwebhelper.exe",L"S:\\Steam\\steam.exe",5000,0),"ordinary transport retains existing unbound PID behavior");
 expect(!listenerProcessBound(L"S:\\Steam",L"S:\\Steam-other\\steamwebhelper.exe",L"S:\\Steam\\steam.exe",4000,4000),"prefix lookalike is not a verified UI helper");
 expect(!listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\fake.exe",L"S:\\Steam\\steam.exe",4000,4000),"another executable cannot provide an account witness");
 expect(!listenerProcessBound(L"S:\\Steam",L"S:\\Steam\\bin\\steamwebhelper.exe",L"S:\\Steam\\game.exe",4000,4000),"foreign parent is rejected even with expected PID metadata");
 if(argc==4 && std::string(argv[1])=="--live-overlay-roundtrip") {
   const std::string root=argv[2];const std::wstring steam(root.begin(),root.end());const DWORD account=static_cast<DWORD>(std::stoul(argv[3]));
   const auto before=run(steam,{{"operation","overlay.get"},{"account",account}});
   if(!before.value("ok",false))throw std::runtime_error("original overlay read failed; no mutation");
   const bool enabled=before.value("value",1)!=0;
   const auto change=run(steam,{{"operation","overlay.set"},{"account",account},{"enabled",!enabled}});
   // Restore original even if change returned an uncertain acknowledgement.
   const auto restore=run(steam,{{"operation","overlay.set"},{"account",account},{"enabled",enabled}});
   const auto after=run(steam,{{"operation","overlay.get"},{"account",account}});
   std::cout<<Json{{"before",before},{"change",change},{"restore",restore},{"after",after}}.dump(2)<<"\n";
   if(!change.value("ok",false)||!restore.value("ok",false)||!after.value("ok",false)||after.value("value",-1)!=before.value("value",-2))return 3;
 }
 if(argc==6 && std::string(argv[1])=="--live-readonly") {
   const std::string root=argv[2],path=argv[4],type=argv[5];const DWORD account=static_cast<DWORD>(std::stoul(argv[3]));
   const std::wstring steam(root.begin(),root.end());
   const auto overlay=run(steam,{{"operation","overlay.get"},{"account",account}});
   const auto mouse=run(steam,{{"operation","mouse.get"},{"account",account},{"path",path},{"controllerType",type}});
   std::cout<<Json{{"overlay",overlay},{"mouse",mouse},{"hardwareWrites",0}}.dump(2)<<"\n";
   if(!overlay.value("ok",false)||!mouse.value("ok",false))return 2;
 }
 if(argc==4 && std::string(argv[1])=="--live-api-contract-read") {
   const std::string root=argv[2];const DWORD account=static_cast<DWORD>(std::stoul(argv[3]));
   const std::wstring steam(root.begin(),root.end());
   const auto result=run(steam,{{"operation","diagnostics.apps.contract.read"},{"account",account}});
   std::cout<<Json{{"apiContractRead",result},{"hardwareWrites",0}}.dump(2)<<"\n";
   if(!result.value("ok",false)||result.value("mutated",true)||!result.value("methodsNotCalled",false))return 2;
 }
 if(argc==4 && std::string(argv[1])=="--live-api-surface-read") {
   const std::string root=argv[2];const DWORD account=static_cast<DWORD>(std::stoul(argv[3]));
   const std::wstring steam(root.begin(),root.end());
   const auto result=run(steam,{{"operation","diagnostics.apps.api.read"},{"account",account}});
   std::cout<<Json{{"apiSurfaceRead",result},{"hardwareWrites",0}}.dump(2)<<"\n";
   if(!result.value("ok",false)||result.value("mutated",true)||!result.value("apiValuesNotRead",false))return 2;
 }
 if((argc==4||argc==5) && std::string(argv[1])=="--live-running-read") {
   const std::string root=argv[2];const DWORD account=static_cast<DWORD>(std::stoul(argv[3]));
   const std::wstring steam(root.begin(),root.end());
   Json ids=Json::array();if(argc==5){const auto appid=static_cast<std::uint32_t>(std::stoull(argv[4]));if(!appid)throw std::runtime_error("invalid appid");ids.push_back(appid);}
   const auto result=run(steam,{{"operation","sleep.inspect"},{"account",account},{"appids",ids},{"runningLinkRead",true},{"nativeFocusFeedbackRead",true}});
   std::cout<<Json{{"runningLinkRead",result},{"hardwareWrites",0}}.dump(2)<<"\n";
   if(!result.value("ok",false)||result.value("mutated",true))return 2;
 }
 if((argc==4||argc==5) && std::string(argv[1])=="--live-app-lifetime-read") {
   const std::string root=argv[2];const DWORD account=static_cast<DWORD>(std::stoul(argv[3]));
   const std::wstring steam(root.begin(),root.end());
   Json ids=Json::array();if(argc==5){const auto appid=static_cast<std::uint32_t>(std::stoull(argv[4]));if(!appid)throw std::runtime_error("invalid appid");ids.push_back(appid);}
   const auto result=run(steam,{{"operation","sleep.inspect"},{"account",account},{"appids",ids},{"runningLinkRead",true},{"nativeFocusFeedbackRead",true},{"nativeAppLifetimeFeedbackRead",true}});
   std::cout<<Json{{"appLifetimeRead",result},{"hardwareWrites",0}}.dump(2)<<"\n";
   if(!result.value("ok",false)||result.value("mutated",true))return 2;
 }
 listenerTopologyFixture::run();
 std::cout << "TRANSPORT_FIXTURE_OBSERVATION " << Json{{"elapsedMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-fixtureStarted).count()},{"liveModeRequested",argc>1}}.dump() << "\n";
 std::cout<<"STEAM_LIVE_TRANSPORT_OK\n";return 0;
#endif // directed selector
}catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}}

#endif

#endif // YMCC_BOUND_API_FIELD_ONLY
