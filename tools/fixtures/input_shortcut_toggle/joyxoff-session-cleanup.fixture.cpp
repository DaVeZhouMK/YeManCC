#include <atomic>
#include <cstdint>
#include <cwchar>
#include <functional>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
using DWORD=std::uint32_t;using HANDLE=std::uintptr_t;using HWND=std::uintptr_t;using LPARAM=std::intptr_t;using BOOL=int;
constexpr BOOL FALSE=0,TRUE=1;
constexpr DWORD WAIT_OBJECT_0=0,WAIT_TIMEOUT=258,WAIT_FAILED=0xffffffff,WAIT_ABANDONED=128;
constexpr DWORD TH32CS_SNAPPROCESS=2,PROCESS_QUERY_LIMITED_INFORMATION=4096,PROCESS_TERMINATE=1,SYNCHRONIZE=0x100000;
constexpr unsigned MAX_PATH=260,WM_SG_JOYXOFF_RESULT=1234;
constexpr HANDLE INVALID_HANDLE_VALUE=~HANDLE(0);
struct PROCESSENTRY32W {DWORD dwSize=0;DWORD th32ProcessID=0;wchar_t szExeFile[260]{};};
static const std::wstring JOYXOFF_EXE=L"C:\\SOFT\\Joyxoff\\Joyxoff.exe";
static std::atomic<bool> g_joyXoffCloseInFlight{false},g_exitRequested{false};
static HWND g_hwnd=1;
struct Peer {DWORD hint=71,held=71,session=7,wait=WAIT_TIMEOUT;bool sessionOk=true,killed=false,pathOk=true,terminateOk=true;std::wstring path=JOYXOFF_EXE;};
struct Fixture {std::vector<Peer> peers;std::set<HANDLE> handles;std::vector<std::function<void()>> workers;bool selfOk=true,threadThrows=false,window=true,postOk=true;unsigned enumeration=0,threads=0,detaches=0,terminations=0,posts=0,selfQueries=0,heldQueries=0;std::size_t cursor=0;json receipt;std::string status;};
static Fixture f;
static void need(bool v,const char* label){if(!v)throw std::runtime_error(label);}
static Peer& peer(HANDLE h){need(h>=100&&h<100+f.peers.size(),"valid-fake-process-handle");need(f.handles.count(h)==1,"fake-process-handle-held");return f.peers.at(h-100);}
static DWORD GetCurrentProcessId(){return 900001;}
static BOOL ProcessIdToSessionId(DWORD pid,DWORD* value){if(pid==900001){++f.selfQueries;if(!f.selfOk)return FALSE;*value=7;return TRUE;}++f.heldQueries;for(auto& p:f.peers)if(p.held==pid){if(!p.sessionOk)return FALSE;*value=p.session;return TRUE;}return FALSE;}
static HANDLE CreateToolhelp32Snapshot(DWORD flags,DWORD){need(flags==TH32CS_SNAPPROCESS,"fake-snapshot-flags");++f.enumeration;f.handles.insert(1);return 1;}
static BOOL row(PROCESSENTRY32W* out){if(f.cursor>=f.peers.size())return FALSE;out->th32ProcessID=f.peers[f.cursor].hint;std::wcscpy(out->szExeFile,L"Joyxoff.exe");return TRUE;}
static BOOL Process32FirstW(HANDLE h,PROCESSENTRY32W* out){need(h==1&&f.handles.count(1),"snapshot-held-first");f.cursor=0;return row(out);}
static BOOL Process32NextW(HANDLE h,PROCESSENTRY32W* out){need(h==1&&f.handles.count(1),"snapshot-held-next");++f.cursor;return row(out);}
static HANDLE OpenProcess(DWORD rights,BOOL,DWORD pid){need((rights&PROCESS_TERMINATE)!=0,"fake-open-exact-rights");for(std::size_t i=0;i<f.peers.size();++i)if(f.peers[i].hint==pid){HANDLE h=100+i;f.handles.insert(h);return h;}return 0;}
static DWORD GetProcessId(HANDLE h){return peer(h).held;}
static BOOL CloseHandle(HANDLE h){need(f.handles.erase(h)==1,"fake-handle-returned-once");return TRUE;}
static BOOL QueryFullProcessImageNameW(HANDLE h,DWORD,wchar_t* out,DWORD* size){auto& p=peer(h);if(!p.pathOk)return FALSE;need(p.path.size()<*size,"fake-path-buffer");std::wcscpy(out,p.path.c_str());*size=static_cast<DWORD>(p.path.size());return TRUE;}
static BOOL TerminateProcess(HANDLE h,unsigned){auto& p=peer(h);++f.terminations;if(!p.terminateOk)return FALSE;p.killed=true;return TRUE;}
static DWORD WaitForSingleObject(HANDLE h,DWORD timeout){auto& p=peer(h);if(timeout==1500)return p.killed?WAIT_OBJECT_0:p.wait;need(timeout==0,"only-zero-added-live-probe");return p.killed?WAIT_OBJECT_0:p.wait;}
static BOOL IsWindow(HWND h){need(h==1,"fake-window-only");return f.window;}
static BOOL PostMessageW(HWND h,unsigned message,unsigned,LPARAM l){need(h==1&&message==WM_SG_JOYXOFF_RESULT,"fake-window-message");++f.posts;if(!f.postOk)return FALSE;auto* payload=reinterpret_cast<json*>(l);f.receipt=*payload;delete payload;return TRUE;}
static void traceLog(const char*,const char*,const char* status,int,int){f.status=status;}
namespace fixture_std {
using std::string;using std::vector;using std::size;using std::move;
using std::memory_order_acq_rel;using std::memory_order_acquire;using std::memory_order_release;
class thread {public:template<class Fn> explicit thread(Fn fn){++f.threads;if(f.threadThrows)throw std::runtime_error("fake-thread-construction-failure");f.workers.emplace_back(std::move(fn));}void detach(){++f.detaches;}};
}
// These includes contain the actual extracted function. Only external OS/thread
// boundaries and the C++ namespace alias are substituted; no product algorithm copy.
#define std fixture_std
#define closeJoyXoffAsync originalCloseJoyXoffAsync
#include "joyxoff-session-cleanup.before.inc"
#undef closeJoyXoffAsync
#include "joyxoff-session-cleanup.current.inc"
#undef std
static json results=json::array();
static void reset(){need(f.workers.empty()&&f.handles.empty(),"previous-fake-resources-returned");f=Fixture{};f.peers.push_back(Peer{});g_joyXoffCloseInFlight=false;g_exitRequested=false;g_hwnd=1;}
static void drain(){need(f.workers.size()==1,"one-fake-worker-queued");auto work=std::move(f.workers.front());f.workers.clear();work();need(!g_joyXoffCloseInFlight.load(),"inflight-returned-on-completion");need(f.handles.empty(),"all-fake-handles-returned");}
static void run(const char* name,std::function<void()> setup,unsigned term,const char* status){reset();setup();closeJoyXoffAsync(name);need(g_joyXoffCloseInFlight.load()&&f.workers.size()==1,"nonblocking-worker-not-run-inline");drain();need(f.terminations==term,"expected-fake-terminate-count");need(f.status==status,"expected-cleanup-status");results.push_back({{"case",name},{"pass",true},{"terminateCalls",f.terminations},{"status",f.status},{"inFlightReturned",true},{"outstandingHandles",f.handles.size()}});}
int main(){try{
 reset();f.peers[0].session=9;originalCloseJoyXoffAsync("original-cross-session");drain();need(f.terminations==1&&f.peers[0].killed,"original-body-demonstrates-cross-session-bug");json baseline={{"status","FAIL_EXPECTED"},{"assertion","foreign-session-must-not-close"},{"terminateCalls",f.terminations},{"rawProductionBodyExecuted",true}};
 run("same-path-current-session",[]{},1,"closed");
 run("same-path-foreign-session",[]{f.peers[0].session=9;},0,"already_off");
 run("held-session-differs-from-current-hint",[]{f.peers[0].session=12;},0,"already_off");
 run("held-pid-differs-from-hint",[]{f.peers[0].held=72;},0,"unknown");
 run("held-pid-unavailable",[]{f.peers[0].held=0;},0,"unknown");
 run("current-session-query-fails",[]{f.selfOk=false;},0,"unknown");need(f.enumeration==0,"self-session-failure-skips-enumeration");
 run("held-session-query-fails",[]{f.peers[0].sessionOk=false;},0,"unknown");
 run("already-exited-not-alive",[]{f.peers[0].wait=WAIT_OBJECT_0;},0,"already_off");
 run("wait-failed-not-timeout",[]{f.peers[0].wait=WAIT_FAILED;},0,"unknown");
 run("other-nontimeout-rejected",[]{f.peers[0].wait=WAIT_ABANDONED;},0,"unknown");
 run("same-name-different-path",[]{f.peers[0].path=L"D:\\Other\\Joyxoff.exe";},0,"already_off");
 run("mixed-local-and-foreign",[]{auto p=Peer{};p.hint=p.held=72;p.session=9;f.peers.push_back(p);},1,"closed");need(!f.peers[1].killed,"foreign-peer-survives-local-close");
 run("mixed-closed-and-session-unknown",[]{auto p=Peer{};p.hint=p.held=72;p.sessionOk=false;f.peers.push_back(p);},1,"failed");
 run("terminate-failure-still-releases",[]{f.peers[0].terminateOk=false;},1,"failed");
 run("exit-requested-worker-returns",[]{g_exitRequested=true;},1,"closed");need(f.posts==0,"exit-path-no-window-message");
 run("missing-window-worker-returns",[]{f.window=false;},1,"closed");need(f.posts==0,"no-invalid-window-post");
 run("post-message-failure-worker-returns",[]{f.postOk=false;},1,"closed");
 reset();closeJoyXoffAsync("first");closeJoyXoffAsync("second");need(f.threads==1&&f.detaches==1&&f.workers.size()==1,"singleflight-rejects-overlap");drain();closeJoyXoffAsync("after-completion");need(f.threads==2&&f.workers.size()==1,"singleflight-reusable-after-return");drain();results.push_back({{"case","singleflight-and-reuse"},{"pass",true}});
 reset();f.threadThrows=true;closeJoyXoffAsync("construction-failure");need(!g_joyXoffCloseInFlight.load()&&f.workers.empty()&&f.handles.empty(),"thread-throw-returns-inflight");results.push_back({{"case","thread-construction-failure"},{"pass",true}});
 std::cout<<json{{"status","PASS"},{"scope","original-input-shortcut-consumer-sidecar;actual-extracted-JoyXoff-body;fake-Win32-and-worker-only"},{"candidateCases",results.size()},{"baseline",baseline},{"cases",results},{"realProcessOperations",false},{"realWindowOperations",false},{"realDevices",false},{"outstandingHandles",f.handles.size()}}.dump()<<std::endl;return 0;
}catch(const std::exception& e){std::cout<<json{{"status","FAIL"},{"error",e.what()},{"completedCases",results},{"outstandingHandles",f.handles.size()}}.dump()<<std::endl;return 1;}}