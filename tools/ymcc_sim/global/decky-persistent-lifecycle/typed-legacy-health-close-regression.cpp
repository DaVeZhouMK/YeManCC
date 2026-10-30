// Bounded complete health-close consumer. All product bodies are byte-extracted.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <mutex>
namespace measured {
inline SRWLOCK createLock=SRWLOCK_INIT,jobLock=SRWLOCK_INIT;
struct Job {HANDLE handle=nullptr;DWORD root=0;bool killOnClose=false;};
inline Job jobs[128]{};inline std::atomic<unsigned> creations{0},cancels{0};inline std::atomic<bool> overflow{false};
inline BOOL WINAPI create(LPCWSTR a,LPWSTR b,LPSECURITY_ATTRIBUTES c,LPSECURITY_ATTRIBUTES d,BOOL e,DWORD f,LPVOID g,LPCWSTR h,LPSTARTUPINFOW i,LPPROCESS_INFORMATION j){AcquireSRWLockExclusive(&createLock);const auto result=::CreateProcessW(a,b,c,d,e,f,g,h,i,j);const auto error=GetLastError();ReleaseSRWLockExclusive(&createLock);if(result)++creations;SetLastError(error);return result;}
inline BOOL WINAPI createToken(HANDLE a,DWORD b,LPCWSTR c,LPWSTR d,DWORD e,LPVOID f,LPCWSTR g,LPSTARTUPINFOW h,LPPROCESS_INFORMATION i){AcquireSRWLockExclusive(&createLock);const auto result=::CreateProcessWithTokenW(a,b,c,d,e,f,g,h,i);const auto error=GetLastError();ReleaseSRWLockExclusive(&createLock);if(result)++creations;SetLastError(error);return result;}
inline void add(HANDLE handle,DWORD root=0,bool kill=false){AcquireSRWLockExclusive(&jobLock);bool added=false;for(auto& row:jobs)if(!row.handle){row={handle,root,kill};added=true;break;}ReleaseSRWLockExclusive(&jobLock);if(!added)overflow=true;}
inline Job find(HANDLE handle){AcquireSRWLockShared(&jobLock);Job result{};for(const auto& row:jobs)if(row.handle==handle){result=row;break;}ReleaseSRWLockShared(&jobLock);return result;}
inline HANDLE WINAPI job(LPSECURITY_ATTRIBUTES a,LPCWSTR b){const auto value=::CreateJobObjectW(a,b);if(value)add(value);return value;}
inline BOOL WINAPI configure(HANDLE handle,JOBOBJECTINFOCLASS kind,LPVOID value,DWORD bytes){const auto result=::SetInformationJobObject(handle,kind,value,bytes);const auto error=GetLastError();if(result&&kind==JobObjectExtendedLimitInformation&&bytes>=sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)){AcquireSRWLockExclusive(&jobLock);for(auto& row:jobs)if(row.handle==handle)row.killOnClose=(static_cast<JOBOBJECT_EXTENDED_LIMIT_INFORMATION*>(value)->BasicLimitInformation.LimitFlags&JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)!=0;ReleaseSRWLockExclusive(&jobLock);}SetLastError(error);return result;}
inline BOOL WINAPI assign(HANDLE handle,HANDLE process){const auto result=::AssignProcessToJobObject(handle,process);const auto error=GetLastError();if(result){AcquireSRWLockExclusive(&jobLock);for(auto& row:jobs)if(row.handle==handle)row.root=GetProcessId(process);ReleaseSRWLockExclusive(&jobLock);}SetLastError(error);return result;}
inline BOOL WINAPI duplicate(HANDLE a,HANDLE b,HANDLE c,LPHANDLE d,DWORD e,BOOL f,DWORD g){const auto source=GetProcessId(a)==GetCurrentProcessId()?find(b):Job{};const auto result=::DuplicateHandle(a,b,c,d,e,f,g);const auto error=GetLastError();if(result&&source.handle&&d&&GetProcessId(c)==GetCurrentProcessId())add(*d,source.root,source.killOnClose);SetLastError(error);return result;}
inline BOOL WINAPI close(HANDLE handle){const auto result=::CloseHandle(handle);const auto error=GetLastError();if(result){AcquireSRWLockExclusive(&jobLock);for(auto& row:jobs)if(row.handle==handle)row={};ReleaseSRWLockExclusive(&jobLock);}SetLastError(error);return result;}
inline BOOL WINAPI cancel(HANDLE handle,LPOVERLAPPED overlapped){++cancels;return ::CancelIoEx(handle,overlapped);}
}
#define CreateProcessW measured::create
#define CreateProcessWithTokenW measured::createToken
#define CreateJobObjectW measured::job
#define SetInformationJobObject measured::configure
#define AssignProcessToJobObject measured::assign
#define DuplicateHandle measured::duplicate
#define CloseHandle measured::close
#define CancelIoEx measured::cancel
#include "typed-legacy-health-close-helpers.inc"
#include "decky_sidebar_broker.h"
#include "decky_update_recovery.h"
#undef CreateProcessW
#undef CreateProcessWithTokenW
#undef CreateJobObjectW
#undef SetInformationJobObject
#undef AssignProcessToJobObject
#undef DuplicateHandle
#undef CloseHandle
#undef CancelIoEx
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
#error Current typed health-close forbids legacy Runtime callbacks.
#endif
#include <deque>
#include "owned-json-channel.h"
namespace hc {
using namespace rtfixture;using Json=nlohmann::json;using namespace ymcc::deckysidebar;
inline std::filesystem::path scope;inline Json assertions=Json::array(),cases=Json::array(),diagnostics=Json::array();
inline std::mutex ioMutex;inline Json observation=Json::object();
inline void save(const std::filesystem::path& p,const Json& value){owned_json_candidate::FileLease fileLease(p,1000);const auto temporary=p.wstring()+L".tmp-"+std::to_wstring(GetCurrentProcessId());std::ofstream out(temporary,std::ios::binary|std::ios::trunc);need(bool(out),"owned-json-open");out<<value.dump(2);out.close();need(bool(out),"owned-json-write");need(MoveFileExW(temporary.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"owned-json-commit");}
// Fixture observation must not deny atomic replacement of the state it reads.
inline HANDLE openOwnedJsonRead(const std::filesystem::path& p){const auto handle=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);need(handle!=INVALID_HANDLE_VALUE,"owned-json-read-open");return handle;}
inline Json readOwnedJson(HANDLE handle){LARGE_INTEGER bytes{};need(GetFileSizeEx(handle,&bytes)!=FALSE&&bytes.QuadPart>=0&&bytes.QuadPart<=8*1024*1024,"owned-json-read-size");std::string text(static_cast<std::size_t>(bytes.QuadPart),'\0');std::size_t offset=0;while(offset<text.size()){DWORD done=0;need(ReadFile(handle,text.data()+offset,static_cast<DWORD>(text.size()-offset),&done,nullptr)!=FALSE&&done>0,"owned-json-read-bytes");offset+=done;}return Json::parse(text);}
inline Json load(const std::filesystem::path& p){owned_json_candidate::FileLease fileLease(p,1000);detail::Handle handle{openOwnedJsonRead(p)};return readOwnedJson(handle.value);}
inline Json ownedJsonObservationControls(){
    const auto p=scope/L"owned-json-sharing-control.json";
    save(p,{{"revision",1}});Json result=Json::array();
    // Intentional nonparticipant: a raw held ifstream still blocks the original
    // writer. This negative is retained; the mutex is not a filesystem bypass.
    {std::ifstream original(p);need(bool(original),"owned-json-original-reader-open");
     bool refused=false;try{save(p,{{"revision",2}});}catch(const std::exception& error){refused=std::string(error.what())=="owned-json-commit";}
     result.push_back({{"id","original-ifstream-reader-denies-atomic-commit"},{"passed",refused}});
     need(refused,"owned-json-original-race-not-reproduced");}
    save(p,{{"revision",3}});
    // load completes and closes its reader before releasing the per-file mutex.
    // Its returned JSON is the exact committed snapshot, not a retained handle.
    const Json captured=load(p);save(p,{{"revision",4}});
    const bool current=load(p).at("revision")==4,previous=captured.at("revision")==3;
    result.push_back({{"id","completed-observation-allows-original-atomic-commit"},{"passed",current}});
    result.push_back({{"id","captured-json-retains-exact-committed-snapshot"},{"passed",previous}});
    need(current&&previous,"owned-json-serialized-observer-contract");return result;
}
inline Json jobs(){Json result=Json::array();AcquireSRWLockShared(&measured::jobLock);for(const auto& row:measured::jobs)if(row.handle){JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};const bool okay=QueryInformationJobObject(row.handle,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr)!=FALSE;result.push_back({{"handle",reinterpret_cast<std::uintptr_t>(row.handle)},{"root",row.root},{"killOnClose",row.killOnClose},{"querySucceeded",okay},{"activeProcesses",okay?Json(info.ActiveProcesses):Json(nullptr)}});}ReleaseSRWLockShared(&measured::jobLock);return result;}
inline void check(bool okay,const char* label){assertions.push_back({{"label",label},{"passed",okay},{"measurements",observation}});std::cout<<(okay?"PASS ":"FAIL ")<<label<<std::endl;if(!okay)throw std::runtime_error(label);}
inline void extra(const char* id,bool okay,const Json& measurements){cases.push_back({{"id",id},{"passed",okay},{"measurements",measurements}});std::cout<<(okay?"PASS ":"FAIL ")<<id<<std::endl;if(!okay)throw std::runtime_error(id);}
inline std::wstring unique(const wchar_t* suffix){return L"Local\\YMCC-Typed-HealthClose-"+std::to_wstring(GetCurrentProcessId())+suffix;}
}
// The exact production worker body is reused. Only its registry-backed
// SteamLease input boundary is replaced by a real read-only owned parent lease.
// This does not claim the independent installed Steam registry contract passed.
namespace ownedworker {
using namespace ymcc::deckybinding;using namespace ymcc::deckybinding::worker;
class SteamLease {
    detail::Handle parent;const Request& request;
public:
    // Accept the exact current worker port. The registry selector belongs to
    // the independent installed-identity gate; this fixture observes its own
    // real parent lease and reports the same named, value-free refusals.
    explicit SteamLease(const Request& value,const std::wstring& registryPath=L"Software\\Valve\\Steam",std::string* rejectedCheck=nullptr):request(value){
        (void)registryPath;
        if(rejectedCheck)rejectedCheck->clear();
        ULONGLONG birth=0;parent.value=ymcc::steamsession::verifiedProcess(request.steam.pid,request.steam.root,&birth);
        const auto reject=[&](const char* check){if(rejectedCheck)*rejectedCheck=check;throw std::runtime_error("mirror-bootstrap-session-changed");};
        if(!parent.value||birth!=request.steam.created)reject("steam-process-or-birth");
        if(!ymcc::deckymirror::sameWindowsUserAndSession(request.steam.pid))reject("steam-user-or-session");
        require(current(rejectedCheck),"mirror-bootstrap-session-changed");
    }
    SteamLease(const SteamLease&)=delete;
    bool current(std::string* rejectedCheck=nullptr)const{
        const auto reject=[&](const char* check){if(rejectedCheck)*rejectedCheck=check;return false;};
        if(rejectedCheck)rejectedCheck->clear();
        if(!parent.value||WaitForSingleObject(parent.value,0)!=WAIT_TIMEOUT)return reject("steam-process-exited");
        DWORD session=MAXDWORD;if(!ProcessIdToSessionId(request.steam.pid,&session)||session!=request.windowsSession)return reject("steam-windows-session");
        if(!ymcc::deckymirror::sameWindowsUserAndSession(request.steam.pid))return reject("steam-user-or-session");
        return true;
    }
};
#include "typed-legacy-health-close-worker.inc"
inline bool child(int& code){int argc=0;auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return false;const auto mode=ChildChannel::classify(argc,argv);LocalFree(argv);if(mode==ChildMode::ordinary)return false;ChildChannel channel;ChildMode actual{};if(!channel.tryRunCommandLine(GetCommandLineW(),code,actual)||actual!=ChildMode::worker){code=87;return true;}const auto frame=channel.readFrame(7000);if(!frame){code=88;return true;}const std::string bytes(frame.payload.begin(),frame.payload.end());code=run(bytes,channel.cancelEvent(),[&](const Json& packet){const auto text=packet.dump();return bool(channel.writeFrame(std::vector<std::uint8_t>(text.begin(),text.end()),2500));});return true;}
}
namespace cdp {
using namespace hc;
struct PeerDisconnected:std::runtime_error { int code;explicit PeerDisconnected(int value):std::runtime_error("owned CDP peer disconnected"),code(value){} };
struct PeerSocket { SOCKET value=INVALID_SOCKET;~PeerSocket(){if(value!=INVALID_SOCKET)closesocket(value);} };
inline bool sendAll(SOCKET socket,const std::string& bytes){size_t at=0;while(at<bytes.size()){const auto sent=send(socket,bytes.data()+at,static_cast<int>(bytes.size()-at),0);if(sent<=0){const int error=sent==0?0:WSAGetLastError();if(error==0||error==WSAECONNRESET||error==WSAECONNABORTED||error==WSAESHUTDOWN||error==WSAENOTCONN)throw PeerDisconnected(error);return false;}at+=static_cast<size_t>(sent);}return true;}
inline bool text(SOCKET socket,const Json& value){return sendAll(socket,ymcc::deckymirror::encode(value.dump()));}
inline Json marker(const std::string& run,int context=101){return {{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",context},{"payload",Json{{"runId",run},{"code","decky-render-failed"}}.dump()}}}};}
inline Json context(){return {{"method","Runtime.executionContextCreated"},{"params",{{"context",{{"id",101},{"origin","https://steamloopback.host"},{"auxData",{{"isDefault",true}}}}}}}};}
inline int server(const std::filesystem::path& directory,const std::wstring& readyName,const std::wstring& stopName){
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 81;
    detail::Handle ready{OpenEventW(EVENT_MODIFY_STATE,FALSE,readyName.c_str())},stop{OpenEventW(SYNCHRONIZE,FALSE,stopName.c_str())};
    SOCKET listener=INVALID_SOCKET;Json log={{"connections",Json::array()},{"errors",Json::array()},{"steps",Json::array()},{"commands",Json::array()},{"barriers",0},{"readbacks",0},{"teardowns",0},{"bootstraps",0}};
    const auto stateFile=directory/L"cdp-state.json";unsigned serial=0;std::string run,step="idle";bool barrierPending=false;
    try{
        need(ready.value&&stop.value,"cdp-control-events");listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);need(listener!=INVALID_SOCKET,"cdp-socket");
        const BOOL exclusive=TRUE;need(setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))==0,"cdp-exclusive");
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);need(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0&&listen(listener,4)==0,"cdp-loopback-bind");int size=sizeof(address);need(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&size)==0,"cdp-port");const auto port=ntohs(address.sin_port);log["port"]=port;log["pid"]=GetCurrentProcessId();save(stateFile,log);SetEvent(ready.value);
        while(WaitForSingleObject(stop.value,0)==WAIT_TIMEOUT){
            fd_set readable;FD_ZERO(&readable);FD_SET(listener,&readable);timeval poll{0,20000};const auto selected=select(0,&readable,nullptr,nullptr,&poll);if(selected==0)continue;need(selected>0,"cdp-select");SOCKET peer=accept(listener,nullptr,nullptr);need(peer!=INVALID_SOCKET,"cdp-accept");PeerSocket peerOwner{peer};
            // Runtime OFF may cancel a session between command read and reply.
            // Retire only that socket; subsequent generations need this server.
            try {
            DWORD timeout=1000;setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));std::string header;char input[8192];
            while(header.find("\r\n\r\n")==std::string::npos){const auto count=recv(peer,input,sizeof(input),0);if(count<=0)break;header.append(input,count);need(header.size()<=8192,"cdp-header-bound");}
            if(header.rfind("GET /fixture/cancelled-peer ",0)==0){need(sendAll(peer,"reset-ready"),"cdp-reset-control-ready");char gone{};const auto received=recv(peer,&gone,1,0);const auto error=received<0?WSAGetLastError():0;need(received<=0&&(error==0||error==WSAECONNRESET||error==WSAECONNABORTED),"cdp-reset-control-client");need(sendAll(peer,"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"),"cdp-reset-control-reply");throw std::runtime_error("cdp-reset-control-did-not-disconnect");}
            if(header.rfind("GET /json/list ",0)==0){const Json targets=Json::array({{{"type","page"},{"title","SharedJSContext"},{"url","https://steamloopback.host/index.html"},{"webSocketDebuggerUrl","ws://127.0.0.1:"+std::to_string(port)+"/devtools/page/health-fixture"}}});const auto body=targets.dump();need(sendAll(peer,"HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body),"cdp-target-reply");continue;}
            need(header.rfind("GET /devtools/page/health-fixture ",0)==0,"cdp-path");const auto lower=ymcc::deckymirror::lower(header);const auto begin=lower.find("sec-websocket-key:");need(begin!=std::string::npos,"cdp-upgrade-key");const auto end=header.find("\r\n",begin);const auto key=ymcc::deckymirror::trim(header.substr(begin+18,end-begin-18));
            need(sendAll(peer,"HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Accept: "+ymcc::deckymirror::websocketAccept(key)+"\r\n\r\n"),"cdp-upgrade");
            std::vector<unsigned char> buffer;bool active=true;
            while(active&&WaitForSingleObject(stop.value,0)==WAIT_TIMEOUT){
                if(std::filesystem::exists(directory/L"control.json")){const auto control=load(directory/L"control.json");const auto next=control.value("serial",0u);if(next>serial){serial=next;step=control.at("step").get<std::string>();log["steps"].push_back({{"serial",serial},{"step",step},{"runId",run}});
                    if(step!="idle"){
                        auto event=marker(step=="foreign-run"?std::string(32,'f'):run,step=="foreign-context"?102:101);need(text(peer,event),"cdp-health-event");
                        if(step!="fatal"){barrierPending=true;need(text(peer,context()),"cdp-negative-barrier");}
                    }save(stateFile,log);
                }}
                fd_set reads;FD_ZERO(&reads);FD_SET(peer,&reads);timeval interval{0,10000};const auto available=select(0,&reads,nullptr,nullptr,&interval);if(available==0)continue;if(available<0)break;const auto count=recv(peer,input,sizeof(input),0);if(count<=0)break;buffer.insert(buffer.end(),input,input+count);
                for(;;){const auto frame=ymcc::deckymirror::decode(buffer);if(!frame.complete)break;buffer.erase(buffer.begin(),buffer.begin()+static_cast<std::ptrdiff_t>(frame.bytes));if(frame.opcode==8){active=false;break;}if(frame.opcode==9){need(sendAll(peer,ymcc::deckymirror::encode(frame.payload,10)),"cdp-pong");continue;}need(frame.opcode==1,"cdp-text-opcode");const auto command=Json::parse(frame.payload);const auto id=command.at("id");const auto method=command.at("method").get<std::string>();Json reply={{"id",id},{"result",Json::object()}};
                    std::string expression;if(method=="Runtime.evaluate")expression=command.at("params").at("expression").get<std::string>();log["commands"].push_back({{"id",id},{"method",method},{"expression",expression},{"step",step}});
                    if(method=="Runtime.evaluate"){
                        Json value;
                        const auto valueAt=expression.find("const value = ");
                        if(valueAt!=std::string::npos){const auto stopAt=expression.find("; Object.defineProperty",valueAt);need(stopAt!=std::string::npos,"cdp-bootstrap-parse");const auto bootstrap=Json::parse(expression.substr(valueAt+14,stopAt-valueAt-14));run=bootstrap.at("runId").get<std::string>();need(expression.find(ymcc::deckymirror::healthScript())!=std::string::npos,"production-health-expression-required");value=run;log["bootstraps"]=log["bootstraps"].get<unsigned>()+1;if(barrierPending){log["barriers"]=log["barriers"].get<unsigned>()+1;barrierPending=false;}}
                        else if(expression=="window.__YMCC_DECKY_HEALTH__?.read() || ''"){value=step=="marker-without-dom"?std::string{}:std::string("decky-render-failed");log["readbacks"]=log["readbacks"].get<unsigned>()+1;}
                        else if(expression.rfind("window.__YMCC_DECKY_MIRROR__?.runId === ",0)==0)value=step!="stale-document";
                        else if(expression.find("ymcc-decky-health-stop")!=std::string::npos){value=true;log["teardowns"]=log["teardowns"].get<unsigned>()+1;}
                        else if(expression=="!window.DeckyPluginLoader")value=false;
                        else throw std::runtime_error("cdp-unexpected-production-expression");
                        reply["result"]={{"result",{{"type",value.is_boolean()?"boolean":"string"},{"value",value}}}};
                    }else need(method=="Runtime.enable"||method=="Runtime.addBinding","cdp-unexpected-method");
                    need(text(peer,reply),"cdp-command-reply");if(method=="Runtime.enable")need(text(peer,context()),"cdp-initial-context");save(stateFile,log);
                }
            }
            }catch(const PeerDisconnected& disconnected){log["peerDisconnects"].push_back({{"code",disconnected.code},{"runId",run},{"serial",serial}});}
            log["connections"].push_back({{"runId",run},{"closed",true}});save(stateFile,log);
        }
    }catch(const std::exception& error){log["errors"].push_back(error.what());save(stateFile,log);if(listener!=INVALID_SOCKET)closesocket(listener);WSACleanup();return 82;}
    if(listener!=INVALID_SOCKET)closesocket(listener);log["naturalStop"]=true;save(stateFile,log);WSACleanup();return 0;
}
}
namespace consumer {
using namespace hc;using json=nlohmann::json;
inline Runtime g_deckySidebarRuntime;inline std::shared_ptr<Client> client=std::make_shared<Client>();
inline ymcc::deckymirror::Broker g_deckyMirrorBroker;inline ymcc::deckyupdate::Recovery g_deckyUpdateRecovery;
inline HWND g_hwnd=nullptr;inline std::mutex g_deckyEventMutex;inline std::deque<json> g_deckyEvents;inline bool g_deckyEventPosted=false;
constexpr UINT WM_DECKY_MIRROR_EVENT=WM_USER+30;
inline std::atomic<unsigned> launched{0},plans{0},ownerSafetyCalls{0},settingsWrites{0};
inline std::wstring POWER_CONTROL_DIR;inline std::filesystem::path settingsPath;
inline std::mutex changedMutex;inline std::condition_variable changed;
inline std::string W2U(const std::wstring& text){const auto size=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);need(size>=0,"owned-root-utf8");std::string result(static_cast<size_t>(size),'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr);return result;}
struct OwnedSessionInput {
    ymcc::steamsession::Session sessionSnapshot()const{ymcc::steamsession::Session result;result.root=ymcc::steamlive::normalizedPath(std::filesystem::path(detail::executable()).parent_path().wstring());result.pid=GetCurrentProcessId();detail::Handle observed{ymcc::steamsession::verifiedProcess(result.pid,result.root,&result.created)};need(observed.value!=nullptr,"owned-session-input");result.account=12345678;return result;}
    int steamPresence()const{return 1;} // Explicit owned-fixture observation; never starts the real observer.
};
inline OwnedSessionInput g_steamSettingsObserver;
enum class PowerLifecycle {Ready};inline std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
inline Json ymSettingsSection(const char* section){if(!std::filesystem::is_regular_file(settingsPath))return Json::object();const auto all=load(settingsPath);return all.value(section,Json::object());}
inline bool ymSettingsWriteSection(const char* section,const Json& value){++settingsWrites;try{Json all=Json::object();if(std::filesystem::is_regular_file(settingsPath))all=load(settingsPath);all[section]=value;save(settingsPath,all);return true;}catch(...){return false;}}
inline Json deckySidebarEnvironment(bool){const auto image=std::filesystem::path(detail::executable());return {{"ready",std::filesystem::is_regular_file(image)&&image.parent_path()==scope.parent_path()},{"scope","only source-built owned fixture resource admission"}};}
inline void deckySidebarDiagnostic(const char* name,const Json& detail,bool=false){std::lock_guard lock(ioMutex);diagnostics.push_back({{"event",name},{"detail",detail}});}
inline std::string deckySidebarStartMirrorBroker(){return g_deckyMirrorBroker.start({})?std::string{}:std::string("mirror-broker-not-active");}
#include "typed-legacy-health-close-main-consumers.inc"
inline Json resources(){const auto state=g_deckySidebarRuntime.snapshot();auto c=fixture::state(client->snapshot());const auto value=client->snapshot();c["enabled"]=value.enabled;c["canceled"]=value.canceled;c["processId"]=value.processId;return {{"runtime",deckySidebarStateJson(state)},{"client",c},{"jobs",jobs()},{"creations",measured::creations.load()},{"cancelApiCalls",measured::cancels.load()},{"broker",g_deckyMirrorBroker.status()}};}
inline void ipc_post_json(unsigned,const Json&){throw std::runtime_error("unexpected-non-health-ui-event");}
inline Json deckySidebarConnectionState(){return Json::object();}
inline LRESULT CALLBACK window(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam){switch(message){
#include "typed-legacy-health-close-ui-case.inc"
    default:return DefWindowProcW(hwnd,message,wParam,lParam);
}}
inline void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
template<class F>bool await(F condition,DWORD ms){const auto until=GetTickCount64()+ms;while(!condition()&&GetTickCount64()<until){pump();Sleep(2);}pump();return condition();}
inline void initialize(bool retained=false){settingsPath=scope/L"private-settings.json";POWER_CONTROL_DIR=scope.wstring();save(settingsPath,{{"extensions",{{"deckySidebar",{{"enabled",true}}}}}});
    WNDCLASSW clazz{};clazz.lpfnWndProc=window;clazz.hInstance=GetModuleHandleW(nullptr);clazz.lpszClassName=L"YMCC-Typed-HealthClose-Owner";need(RegisterClassW(&clazz)!=0,"owner-window-class");
    g_hwnd=CreateWindowExW(0,clazz.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,clazz.hInstance,nullptr);need(g_hwnd!=nullptr,"owned-hidden-owner-window");
    Config config;config.executable=detail::executable();config.home=(scope/L"home").wstring();config.preflight=[] {return std::string{};};
    config.changed=[](const State& state){if(state.phase!="starting"&&state.phase!="loader-started"&&state.phase!="reconnecting"&&state.phase!="frontend-attached")g_deckyMirrorBroker.stop();if(state.phase=="loader-started")++launched;changed.notify_all();};
    if(retained)config.launchAdmission=[] {return LaunchAdmission{true,"retained-exact-fixture",false,"",true};};
    config.ownedBinding=client;config.bindingPlan=[](BindingMode mode,std::uint64_t generation){++plans;return deckySidebarOwnedBindingPlan(mode,generation);};config.bindingDiagnostic=deckySidebarOwnedBindingDiagnostic;
    g_deckySidebarRuntime.initialize(std::move(config));
}
inline void finish(){g_deckySidebarRuntime.shutdown();g_deckyMirrorBroker.stop();pump();if(g_hwnd){DestroyWindow(g_hwnd);g_hwnd=nullptr;}UnregisterClassW(L"YMCC-Typed-HealthClose-Owner",GetModuleHandleW(nullptr));}
}
namespace hc {
using namespace consumer;
struct ContextServer {
    detail::Handle process,job,ready,stop;std::filesystem::path directory;std::wstring readyName,stopName;
    Json identity;unsigned serial=0;
    explicit ContextServer(const std::filesystem::path& output):directory(output){
        std::filesystem::create_directories(directory);readyName=unique(L"-ready");stopName=unique(L"-stop");
        ready.value=CreateEventW(nullptr,TRUE,FALSE,readyName.c_str());stop.value=CreateEventW(nullptr,TRUE,FALSE,stopName.c_str());need(ready.value&&stop.value,"context-server-events");
        save(directory/L"control.json",{{"serial",0},{"step","idle"}});
        job.value=CreateJobObjectW(nullptr,nullptr);need(job.value!=nullptr,"context-server-job");JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;need(SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&info,sizeof(info)),"context-server-job-policy");
        const auto image=(std::filesystem::path(detail::executable()).parent_path()/L"bin/cef/fixture/steamwebhelper.exe").wstring();
        auto command=L"\""+image+L"\" --typed-legacy-health-close-cdp \""+directory.wstring()+L"\" \""+readyName+L"\" \""+stopName+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};need(measured::create(image.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child),"context-server-create");process.value=child.hProcess;detail::Handle thread{child.hThread};need(AssignProcessToJobObject(job.value,process.value)&&ResumeThread(thread.value)!=static_cast<DWORD>(-1),"context-server-assign");
        need(WaitForSingleObject(ready.value,3000)==WAIT_OBJECT_0,"context-server-ready");identity=fixture::identity(process.value);
    }
    ~ContextServer(){if(stop.value)SetEvent(stop.value);if(process.value)WaitForSingleObject(process.value,3000);}
    void resetPeerControl(){
        struct SocketScope {WSADATA data{};SocketScope(){need(WSAStartup(MAKEWORD(2,2),&data)==0,"peer-reset-control-winsock");}~SocketScope(){WSACleanup();}} socketScope;
        const auto port=state().at("port").get<unsigned short>();SOCKET peer=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);need(peer!=INVALID_SOCKET,"peer-reset-control-socket");cdp::PeerSocket owner{peer};sockaddr_in target{};target.sin_family=AF_INET;target.sin_addr.s_addr=htonl(INADDR_LOOPBACK);target.sin_port=htons(port);need(connect(peer,reinterpret_cast<sockaddr*>(&target),sizeof(target))==0,"peer-reset-control-connect");need(cdp::sendAll(peer,"GET /fixture/cancelled-peer HTTP/1.1\r\nHost: localhost\r\n\r\n"),"peer-reset-control-request");DWORD timeout=1000;need(setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout))==0,"peer-reset-control-timeout");std::string ack;char received[11]{};while(ack.size()<11){const auto count=recv(peer,received,static_cast<int>(11-ack.size()),0);need(count>0,"peer-reset-control-ready");ack.append(received,count);}need(ack=="reset-ready","peer-reset-control-ready-bytes");linger reset{1,0};need(setsockopt(peer,SOL_SOCKET,SO_LINGER,reinterpret_cast<const char*>(&reset),sizeof(reset))==0,"peer-reset-control-linger");closesocket(peer);owner.value=INVALID_SOCKET;
        need(waitFor([&]{return state().contains("peerDisconnects");},1000),"peer-reset-control-observed");need(alive(),"peer-reset-control-server-alive");
    }
    bool alive()const{return WaitForSingleObject(process.value,0)==WAIT_TIMEOUT;}
    Json state()const{try{return load(directory/L"cdp-state.json");}catch(...){return Json::object();}}
    void setStep(const std::string& step){save(directory/L"control.json",{{"serial",++serial},{"step",step}});}
    Json stopAndMeasure(){SetEvent(stop.value);const auto waited=WaitForSingleObject(process.value,3000);DWORD code=STILL_ACTIVE;GetExitCodeProcess(process.value,&code);const auto data=state();return {{"pid",identity.at("pid")},{"exited",waited==WAIT_OBJECT_0},{"exitCode",code},{"server",data}};}
};
inline bool zero(const ClientSnapshot& s){return s.fullyDestroyed&&s.handles==0&&s.pendingIo==0&&s.activeCalls==0&&s.jobActiveProcesses==0&&s.childExited&&s.owner.fullyDestroyed&&s.readerFullyDestroyed;}
inline void run(ContextServer& context){
    wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);
    observation={{"scope",scope.string()},{"image",W2U(image)}};
    check(scope.filename()==L"health-close-fixture"&&scope.parent_path()==std::filesystem::path(image).parent_path(),"fixture is next to this test image in explicit task output");
    std::filesystem::create_directories(scope/L"home");initialize();g_deckySidebarRuntime.observeSteam(1,true);g_deckySidebarRuntime.setEnabled(true);
    bool started=false;{std::unique_lock lock(changedMutex);started=changed.wait_for(lock,std::chrono::seconds(3),[]{return g_deckySidebarRuntime.snapshot().phase=="loader-started";});}
    observation=resources();check(started,"production runtime starts only self-built inert child");
    const auto before=g_deckySidebarRuntime.snapshot();detail::Handle child{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,before.pid)};
    check(child.value!=nullptr,"owned child handle retained for termination proof");
    wchar_t childImage[32768]{};DWORD length=32768;const bool same=QueryFullProcessImageNameW(child.value,0,childImage,&length)&&_wcsicmp(image,childImage)==0;
    observation["ownedChildImage"]=W2U(childImage);check(same,"child image mismatch"); // Original unnamed ++checks is explicitly retained.
    need(await([&]{return context.state().value("bootstraps",0u)>=2;},1000),"production-watch-attached");
    const auto steady=resources();extra("current.real-bootstrap-watch-and-owned-io",client->snapshot().pendingIo>0&&client->snapshot().processId!=0&&context.alive()&&g_deckyMirrorBroker.status().value("active",false),steady);
    unsigned barriers=context.state().value("barriers",0u);
    for(const auto& step:{std::string("foreign-context"),std::string("foreign-run"),std::string("marker-without-dom"),std::string("stale-document")}){
        const auto generation=plans.load(),calls=settingsWrites.load();context.setStep(step);const bool consumed=await([&]{return context.state().value("barriers",0u)>barriers;},1000);barriers=context.state().value("barriers",0u);
        extra(("negative.cdp-"+step+"-cannot-close").c_str(),consumed&&g_deckySidebarRuntime.snapshot().enabled&&g_deckySidebarRuntime.snapshot().pid==before.pid&&settingsWrites==calls&&plans==generation,{{"barrierConsumed",consumed},{"beforePid",before.pid},{"observedPid",g_deckySidebarRuntime.snapshot().pid},{"plansBefore",generation},{"plansAfter",plans.load()},{"settingsWritesBefore",calls},{"settingsWritesAfter",settingsWrites.load()},{"serverAlive",context.alive()},{"server",context.state()},{"resources",resources()}});
    }
    const auto run=g_deckyMirrorBroker.bootstrap().at("runId").get<std::string>();Json event;
    context.setStep("fatal");const bool queued=waitFor([&]{std::lock_guard lock(g_deckyEventMutex);return !g_deckyEvents.empty();},1000);
    // Inspect the actual worker-produced
    // event before the original owner case consumes it; no UI pump occurs here.
    {std::lock_guard lock(g_deckyEventMutex);for(const auto& packet:g_deckyEvents)if(packet.value("event",std::string{})=="deckySidebar.safetyStop")event=packet.at("data");}
    const auto live=deckySidebarStateJson(g_deckySidebarRuntime.snapshot());
    observation={{"queued",queued},{"event",event},{"state",live},{"runId",run},{"server",context.state()}};
    check(!event.empty()&&ymcc::deckyhealth::matches(event,live,run),"scoped actual fatal evidence admitted");
    const auto stopAt=GetTickCount64();pump();const bool terminated=WaitForSingleObject(child.value,1000)==WAIT_OBJECT_0;const auto elapsed=GetTickCount64()-stopAt;
    const auto after=resources();observation={{"offMs",elapsed},{"resources",after},{"contextAlive",context.alive()},{"server",context.state()}};
    check(terminated&&context.alive()&&zero(client->snapshot()),"normal off kills and waits for only the owned Job child");
    check(!g_deckySidebarRuntime.snapshot().enabled&&g_deckySidebarRuntime.snapshot().phase=="disabled"&&g_deckySidebarRuntime.snapshot().pid==0,"off state and process ownership are consistent");
    check(g_deckySidebarRuntime.snapshot().reason=="decky-render-failed","close reason survives worker final disabled publication");
    extra("current.owned-watch-cancel-and-private-safety-persistence",measured::cancels>0&&ymSettingsSection("extensions").at("deckySidebar").at("disabledReason")=="decky-render-failed"&&!g_deckyMirrorBroker.status().value("active",true),after);
    const auto n=launched.load(),plan=plans.load(),create=measured::creations.load();g_deckySidebarRuntime.observeSteam(0);g_deckySidebarRuntime.observeSteam(1,true);g_deckySidebarRuntime.requestTransportRecovery();g_deckySidebarRuntime.requestBindingRefresh(1);g_deckySidebarRuntime.requestLaunchAdmissionRefresh();
    observation=resources();check(launched==n&&!g_deckySidebarRuntime.snapshot().enabled&&g_deckySidebarRuntime.snapshot().reason=="decky-render-failed"&&plans==plan&&measured::creations==create,"session and transport events cannot reopen safety close");
    context.setStep("idle");deckySidebarSetEnabled(true);
    bool reenabled=false;{std::unique_lock lock(changedMutex);reenabled=changed.wait_for(lock,std::chrono::seconds(3),[&]{return launched>n;});}
    observation=resources();check(reenabled,"explicit user re-enable can start a new owned child");
    check(g_deckySidebarRuntime.snapshot().reason!="decky-render-failed","explicit enable clears runtime fault");
    const auto originalFinal=g_deckySidebarRuntime.snapshot();detail::Handle originalFinalChild{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,originalFinal.pid)};
    const auto cancelBefore=measured::cancels.load();const auto shutdownAt=GetTickCount64();g_deckySidebarRuntime.shutdown();
    extra("current.original-final-shutdown-cancels-real-watch",originalFinalChild.value&&WaitForSingleObject(originalFinalChild.value,1000)==WAIT_OBJECT_0&&zero(client->snapshot())&&context.alive()&&measured::cancels>cancelBefore,{{"shutdownMs",GetTickCount64()-shutdownAt},{"resources",resources()}});
    // Supplemental negative sequence starts after the original complete test.
    deckySidebarSetEnabled(true);need(await([&]{return g_deckySidebarRuntime.snapshot().phase=="loader-started";},3000),"supplemental-owned-reenable");
    const auto second=g_deckySidebarRuntime.snapshot();detail::Handle secondChild{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,second.pid)};
    const auto writes=settingsWrites.load();deckySidebarAcceptSafetyStop(event);
    extra("negative.previous-run-queued-fatal-cannot-close-new-runtime",g_deckySidebarRuntime.snapshot().enabled&&g_deckySidebarRuntime.snapshot().pid==second.pid&&settingsWrites==writes,resources());
    // A real private-storage open error must not block the production safety OFF.
    const auto savedSettings=settingsPath;settingsPath=scope;context.setStep("fatal");
    const bool safetyClosed=await([]{return !g_deckySidebarRuntime.snapshot().enabled;},1000);
    extra("negative.private-settings-write-error-cannot-block-fatal-off",safetyClosed&&secondChild.value&&WaitForSingleObject(secondChild.value,1000)==WAIT_OBJECT_0&&g_deckySidebarRuntime.snapshot().reason=="decky-render-failed"&&zero(client->snapshot())&&context.alive(),{{"resources",resources()},{"server",context.state()}});settingsPath=savedSettings;
    const auto count=launched.load();deckySidebarSetEnabled(false);g_deckySidebarRuntime.requestTransportRecovery();g_deckySidebarRuntime.requestBindingRefresh(999);g_deckySidebarRuntime.requestLaunchAdmissionRefresh();pump();
    extra("negative.explicit-off-rejects-late-health-and-recovery",!g_deckySidebarRuntime.snapshot().enabled&&g_deckySidebarRuntime.snapshot().pid==0&&launched==count&&zero(client->snapshot()),resources());
    g_deckySidebarRuntime.shutdown();extra("current.shutdown-owned-resource-proof",zero(client->snapshot())&&context.alive(),resources());
}

// Same real worker/CDP/owner consumer as the original sequence, with the
// production retained-frontend admission. No test-owned Loader may be created.
inline Json retainedFrontendHealth(ContextServer& context){
    Json rows=Json::array();const auto checkRetained=[&](const char* id,bool passed,Json detail){rows.push_back({{"id",id},{"passed",passed},{"detail",std::move(detail)}});return passed;};
    const auto returned=[&]{return Json{{"passed",std::all_of(rows.begin(),rows.end(),[](const Json& row){return row.at("passed")==true;})},{"cases",rows},{"ordinaryLoaderStarted",false},{"identityBoundary","same task-owned CDP and extracted production owner; no installed Steam action"}};};
    consumer::finish();context.setStep("idle");
    const auto frontend=scope/L"decky/plugins/ymcc-sidebar/dist/index.js";std::filesystem::create_directories(frontend.parent_path());{std::ofstream file(frontend);file<<"// YMCC_DECKY_FRONTEND_BUILD_ID="<<std::string(64,'d')<<'\n';need(bool(file),"retained-fixture-frontend-marker");}
    initialize(true);const auto loaderLaunches=launched.load();g_deckySidebarRuntime.observeSteam(1,true);g_deckySidebarRuntime.setEnabled(true);
    const bool attached=await([]{return g_deckySidebarRuntime.snapshot().phase=="frontend-attached";},3000);
    const auto before=g_deckySidebarRuntime.snapshot();const auto firstRun=g_deckyMirrorBroker.bootstrap().value("runId",std::string{});const auto active=client->snapshot();detail::Handle firstWorker{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,active.processId)};
    if(!checkRetained("retained.actual-binding-without-owned-loader",attached&&before.enabled&&before.pid==0&&launched==loaderLaunches&&firstWorker.value&&active.pendingIo>0&&!firstRun.empty()&&context.alive(),resources()))return returned();
    auto barriers=context.state().value("barriers",0u);const auto writes=settingsWrites.load();
    context.setStep("foreign-run");const bool foreignRun=await([&]{return context.state().value("barriers",0u)>barriers;},1000);barriers=context.state().value("barriers",0u);
    if(!checkRetained("retained.foreign-run-cannot-disable",foreignRun&&g_deckySidebarRuntime.snapshot().enabled&&settingsWrites==writes,resources()))return returned();
    context.setStep("foreign-context");const bool foreignContext=await([&]{return context.state().value("barriers",0u)>barriers;},1000);
    if(!checkRetained("retained.foreign-context-cannot-disable",foreignContext&&g_deckySidebarRuntime.snapshot().enabled&&settingsWrites==writes,resources()))return returned();
    context.setStep("fatal");const bool queued=waitFor([]{std::lock_guard lock(g_deckyEventMutex);return !g_deckyEvents.empty();},1000);Json event;
    {std::lock_guard lock(g_deckyEventMutex);for(const auto& packet:g_deckyEvents)if(packet.value("event",std::string{})=="deckySidebar.safetyStop")event=packet.at("data");}
    const auto holdStart=GetTickCount64();const auto plansBeforeHold=plans.load();
    const bool transportRejected=!g_deckySidebarRuntime.requestTransportRecovery();
    const bool resumeRejected=!g_deckySidebarRuntime.requestBindingRefresh(88000);
    const bool admissionRejected=!g_deckySidebarRuntime.requestLaunchAdmissionRefresh();
    const bool connectionCheckRejected=!g_deckySidebarRuntime.requestConnectionCheck();
    const bool preparationRejected=!g_deckySidebarRuntime.requestPreparationRefresh(88000);
    Sleep(1700); // Deliberately no UI pump; owner OFF's original1500ms budget starts after dispatch.
    const auto atDispatch=deckySidebarStateJson(g_deckySidebarRuntime.snapshot());
    const auto currentRunAtDispatch=g_deckyMirrorBroker.bootstrap().value("runId",std::string{});
    const Json held={{"queuedWithoutUiPumpMs",GetTickCount64()-holdStart},{"transportRejected",transportRejected},{"resumeRejected",resumeRejected},{"admissionRejected",admissionRejected},{"connectionCheckRejected",connectionCheckRejected},{"preparationRejected",preparationRejected},{"plansBefore",plansBeforeHold},{"plansAfter",plans.load()},{"runBefore",firstRun},{"runAfter",currentRunAtDispatch},{"oldWorkerExitedBeforeOwner",WaitForSingleObject(firstWorker.value,0)==WAIT_OBJECT_0},{"resources",resources()}};
    const bool heldCurrent=transportRejected&&resumeRejected&&admissionRejected&&connectionCheckRejected&&preparationRejected&&plans.load()==plansBeforeHold&&currentRunAtDispatch==firstRun&&WaitForSingleObject(firstWorker.value,0)==WAIT_OBJECT_0;

    if(!checkRetained("retained.actual-fatal-event-is-admitted",queued&&!event.empty()&&heldCurrent&&g_deckySidebarRuntime.acceptsSafetyStop(event,currentRunAtDispatch),{{"queued",queued},{"event",event},{"state",atDispatch},{"runId",firstRun},{"held",held}}))return returned();
    const auto start=GetTickCount64();deckySidebarAcceptSafetyStop(event);pump();const auto off=g_deckySidebarRuntime.snapshot();const auto elapsed=GetTickCount64()-start;
    if(!checkRetained("retained.fatal-owner-off-stops-only-own-worker",elapsed<1500&&!off.enabled&&off.pid==0&&off.reason=="decky-render-failed"&&zero(client->snapshot())&&WaitForSingleObject(firstWorker.value,1000)==WAIT_OBJECT_0&&context.alive()&&launched==loaderLaunches,{{"offMs",elapsed},{"resources",resources()}}))return returned();
    if(!checkRetained("retained.fatal-reason-is-persisted",ymSettingsSection("extensions").at("deckySidebar").at("disabledReason")=="decky-render-failed",resources()))return returned();
    context.setStep("idle");deckySidebarSetEnabled(true);const bool reattached=await([]{return g_deckySidebarRuntime.snapshot().phase=="frontend-attached";},3000);const auto secondRun=g_deckyMirrorBroker.bootstrap().value("runId",std::string{});const auto writesBeforeOld=settingsWrites.load();deckySidebarAcceptSafetyStop(event);pump();
    if(!checkRetained("retained.previous-run-cannot-close-new-connection",reattached&&!secondRun.empty()&&secondRun!=firstRun&&g_deckySidebarRuntime.snapshot().enabled&&settingsWrites==writesBeforeOld&&launched==loaderLaunches,resources()))return returned();
    deckySidebarSetEnabled(false);g_deckySidebarRuntime.requestTransportRecovery();g_deckySidebarRuntime.requestBindingRefresh(99000);g_deckySidebarRuntime.requestLaunchAdmissionRefresh();deckySidebarAcceptSafetyStop(event);pump();
    if(!checkRetained("retained.explicit-off-rejects-late-events",!g_deckySidebarRuntime.snapshot().enabled&&zero(client->snapshot())&&context.alive()&&launched==loaderLaunches,resources()))return returned();
    context.setStep("idle");deckySidebarSetEnabled(true);need(await([]{return g_deckySidebarRuntime.snapshot().phase=="frontend-attached";},3000),"retained-session-reset-start");
    context.setStep("fatal");need(waitFor([]{std::lock_guard lock(g_deckyEventMutex);return !g_deckyEvents.empty();},1000),"retained-session-reset-fatal-queued");Json beforeSessionEvent;
    {std::lock_guard lock(g_deckyEventMutex);for(const auto& packet:g_deckyEvents)if(packet.value("event",std::string{})=="deckySidebar.safetyStop")beforeSessionEvent=packet.at("data");}
    const auto beforeSessionRun=g_deckyMirrorBroker.bootstrap().value("runId",std::string{});need(g_deckySidebarRuntime.acceptsSafetyStop(beforeSessionEvent,beforeSessionRun),"retained-session-reset-current-fault");
    context.setStep("idle");g_deckySidebarRuntime.observeSteam(1,true);const auto beforeOldWrite=settingsWrites.load();deckySidebarAcceptSafetyStop(beforeSessionEvent);
    if(!checkRetained("retained.new-session-invalidates-queued-fatal",g_deckySidebarRuntime.snapshot().enabled&&!g_deckySidebarRuntime.acceptsSafetyStop(beforeSessionEvent,beforeSessionRun)&&settingsWrites==beforeOldWrite,resources()))return returned();
    deckySidebarSetEnabled(false);pump();
    // Restore the ordinary configuration before the original20-cycle stress.
    consumer::finish();initialize();return returned();
}

}

int wmain(int argc,wchar_t** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc==5&&std::wstring_view(argv[1])==L"--typed-legacy-health-close-cdp")return cdp::server(argv[2],argv[3],argv[4]);
    int code=0;if(ownedworker::child(code)||ymcc::deckybinding::fixture::tryRunChild(code))return code;
    if(argc==1){Sleep(15000);return 0;} // Same image, same original inert-child condition.
    if(argc!=3)return 3;
    using namespace hc;scope=std::filesystem::absolute(argv[1]).lexically_normal();
    Json report={{"schema","ymcc.typed-legacy-health-close.v1"},{"productCallbackMode","typed-only; legacy macro absent"},{"fullPersistentAcceptance",false},
        {"officialLoaderExecuted",false},{"realSteamExecuted",false},{"physicalSleepPerformed",false},{"externalNetwork",false},
        {"identityBoundary","Production worker body and full injectBootstrap/BootstrapWatch are byte-extracted. Task-only real parent PID/birth/session/image lease replaces installed Steam registry input; registry acceptance is not claimed."},
        {"settingsBoundary","Original safety owner consumer uses actual private JSON file writes in this owned scope; no installed settings or registry touched."}};
    const auto start=GetTickCount64();bool okay=false;Json contextExit;
    try{
        std::filesystem::create_directories(scope);
        report["ownedJsonObservationControls"]=ownedJsonObservationControls();
        report["sharedCryptoCalibration"]=initializeSharedCryptoForMeasurement();
        // Original source-built Context and Broker cold initialization precedes
        // strict resource measurements. Both must close naturally and stabilize.
        for(unsigned i=0;i<2;++i){const auto before=handles();{Controls controls(L"success");Context owned;}{ymcc::deckymirror::Broker broker;need(broker.start({}),"cold-owned-broker");broker.stop();}const auto after=handles();report["coldCalibration"].push_back({{"index",i},{"beforeHandles",before},{"afterHandles",after},{"table",handleTable()}});if(i==1)need(before==after,"cold-calibration-not-stable");}
        // Independently prove the owner-window cold initialization before any
        // Runtime/Client invocation; do not subtract or close retained OS handles.
        for(unsigned i=0;i<2;++i){const auto before=handles();const auto tableBefore=handleTable();WNDCLASSW clazz{};clazz.lpfnWndProc=DefWindowProcW;clazz.hInstance=GetModuleHandleW(nullptr);clazz.lpszClassName=L"YMCC-HealthClose-ColdWindow";need(RegisterClassW(&clazz)!=0,"cold-window-class");HWND owned=CreateWindowExW(0,clazz.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,clazz.hInstance,nullptr);need(owned!=nullptr,"cold-window-create");need(DestroyWindow(owned)!=FALSE,"cold-window-destroy");need(UnregisterClassW(clazz.lpszClassName,clazz.hInstance)!=FALSE,"cold-window-unregister");consumer::pump();const auto after=handles();report["coldWindowCalibration"].push_back({{"index",i},{"beforeHandles",before},{"afterHandles",after},{"before",tableBefore},{"after",handleTable()}});if(i==1)need(before==after,"cold-window-not-stable");}
        const auto handlesBefore=handles();const auto threadsBefore=threadCount();report["parentHandlesBefore"]=handlesBefore;report["parentThreadsBefore"]=threadsBefore;report["parentHandleSnapshotBefore"]=handleTable();
        {
            ContextServer context(scope/L"cdp");context.resetPeerControl();
            try{run(context);report["retainedFrontendHealth"]=retainedFrontendHealth(context);if(report["retainedFrontendHealth"].at("passed")!=true)report["error"]="retained-frontend-health-failed";}catch(const std::exception& error){report["error"]=error.what();}
            // Repeated real Bootstrap/Watch/typed Client rebinds must retire the old
            // worker without restarting the owned Loader or retaining parent resources.
            // The independent direct AsyncRequest gate remains mandatory and separate.
            if(!report.contains("error")){
                const auto stressStart=GetTickCount64();const auto stressHandlesBefore=handles();const auto stressThreadsBefore=threadCount();Json stressRows=Json::array();bool stressOkay=true;
                context.setStep("idle");deckySidebarSetEnabled(true);need(await([&]{return g_deckySidebarRuntime.snapshot().phase=="loader-started";},3000),"stress-initial-owned-loader");
                for(unsigned iteration=0;iteration<20;++iteration){
                    const auto before=client->snapshot();const auto beforeLoader=g_deckySidebarRuntime.snapshot().pid;const auto readbacks=context.state().value("bootstraps",0u);detail::Handle previous{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,before.processId)};
                    const bool requested=g_deckySidebarRuntime.requestBindingRefresh(1000+iteration);
                    const bool readback=await([&]{return context.state().value("bootstraps",0u)>readbacks+1&&client->snapshot().processId!=before.processId&&client->snapshot().pendingIo>0;},1500);
                    const bool exited=previous.value&&WaitForSingleObject(previous.value,1000)==WAIT_OBJECT_0;const auto after=client->snapshot();
                    const bool passed=requested&&readback&&exited&&context.alive()&&g_deckySidebarRuntime.snapshot().enabled&&after.processId!=0&&beforeLoader!=0&&g_deckySidebarRuntime.snapshot().pid==beforeLoader;
                    stressRows.push_back({{"iteration",iteration},{"passed",passed},{"refreshRequested",requested},{"actualBootstrapReply",readback},{"oldOwnedWorkerExited",exited},{"cefUnaffected",context.alive()},{"beforeLoader",beforeLoader},{"afterLoader",g_deckySidebarRuntime.snapshot().pid},{"beforeWorker",before.processId},{"afterWorker",after.processId},{"resources",resources()}});
                    save(std::filesystem::path(argv[2]).parent_path()/L"stress-progress.json",stressRows);if(!passed){stressOkay=false;break;}
                }
                g_deckySidebarRuntime.shutdown();consumer::pump();const auto stressHandlesAfter=handles();const auto stressThreadsAfter=threadCount();stressOkay=stressOkay&&stressRows.size()==20&&zero(client->snapshot())&&stressHandlesAfter==stressHandlesBefore&&stressThreadsAfter==stressThreadsBefore;
                report["actualWorkerStress"]={{"passed",stressOkay},{"rows",stressRows},{"beforeHandles",stressHandlesBefore},{"afterHandles",stressHandlesAfter},{"beforeThreads",stressThreadsBefore},{"afterThreads",stressThreadsAfter},{"elapsedMs",GetTickCount64()-stressStart},{"originalNativeBudgetMs",15000},{"formalAsyncGateWaived",false},{"stressIsFormalSupplementalConsumer",true}};
                if(!stressOkay)report["error"]="actual-worker-rebind-stress-failed";
            }
            consumer::finish();contextExit=context.stopAndMeasure();
        }
        report["parentHandlesAfter"]=handles();report["parentThreadsAfter"]=threadCount();report["parentHandleSnapshotAfter"]=handleTable();
        okay=!report.contains("error")&&assertions.size()==11&&handles()==handlesBefore&&threadCount()==threadsBefore;
        for(const auto& row:assertions)if(row.at("passed")!=true)okay=false;for(const auto& row:cases)if(row.at("passed")!=true)okay=false;
        okay=okay&&contextExit.at("exited")==true&&contextExit.at("exitCode")==0&&!measured::overflow&&jobs().empty();
    }catch(const std::exception& error){report["error"]=error.what();consumer::finish();}
    report["passed"]=okay;report["assertions"]=assertions;report["cases"]=cases;report["diagnostics"]=diagnostics;report["contextExit"]=contextExit;report["jobsAtExit"]=jobs();report["jobLedgerOverflow"]=measured::overflow.load();report["atExit"]=consumer::resources();report["elapsedMs"]=GetTickCount64()-start;
    save(std::filesystem::path(argv[2]),report);std::cout<<report.dump()<<std::endl;return okay?0:1;
}



