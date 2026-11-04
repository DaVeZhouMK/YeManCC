#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "deps/json/json.hpp"
#include "tools/fixtures/process_inventory_ports.h"
#include "native/monitor_identity.h"
#include <set>
#include <functional>
namespace fspath=std::filesystem;
namespace windowfacts {
inline std::map<HWND,DWORD> windows;
inline std::set<DWORD> missing;
inline unsigned finds=0;
inline std::function<void(DWORD)> beforeOpen;
inline HANDLE WINAPI open(DWORD access,BOOL inherit,DWORD pid){
 if(access!=PROCESS_QUERY_LIMITED_INFORMATION){++osfacts::s.violations;return nullptr;}
 if(beforeOpen)beforeOpen(pid);
 return osfacts::open(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,inherit,pid);
}
inline BOOL WINAPI isWindow(HWND handle){return windows.contains(handle);}
inline DWORD WINAPI thread(HWND handle,LPDWORD pid){if(pid)*pid=windows.contains(handle)?windows.at(handle):0;return windows.contains(handle)?1:0;}
inline HWND find(DWORD pid){++finds;if(missing.contains(pid))return nullptr;for(const auto& [handle,current]:windows)if(current==pid)return handle;return nullptr;}
}
#undef OpenProcess
#define OpenProcess windowfacts::open
#define IsWindow windowfacts::isWindow
#define GetWindowThreadProcessId windowfacts::thread
#include "focus-slices.inc"
static unsigned checks=0;
static void require(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
static void returned(){require(osfacts::s.handles.empty(),"owned fake handles leaked");require(osfacts::s.violations==0,"fake API contract violated");}
static HWND hwnd(DWORD pid){return reinterpret_cast<HWND>(UINT_PTR(pid)+1000);}
static void reset(std::vector<osfacts::Row> rows={{100,L"Game.exe"}}){returned();osfacts::s=osfacts::State{};osfacts::s.rows=std::move(rows);for(auto& row:osfacts::s.rows)if(row.path==osfacts::expectedPath)row.path=std::wstring(LR"(Q:\Games\)")+row.name;windowfacts::windows.clear();for(const auto& row:osfacts::s.rows)windowfacts::windows.emplace(hwnd(row.pid),row.pid);windowfacts::missing.clear();windowfacts::finds=0;windowfacts::beforeOpen={};ymcc::processInventory::invalidate();}
static void before(){
 reset({{100,L"Unrelated.exe"},{101,L"Game.exe"}});
 require(focus_before::focusFindProcessByPath(LR"(Q:\Games\Game.exe)")==101,"original known path matching control");returned();
 std::cout<<"BEFORE_REDUNDANT_PATH_QUERIES opens="<<osfacts::s.opens<<" expected=1\n";
 require(osfacts::s.opens==1,"BEFORE_REDUNDANT_PATH_QUERIES: original opens unrelated processes before path match");
}
static void after(){
 reset({{100,L"HWiNFO64.exe"}});require(focus_after::focusIsBackgroundMonitorWindow(hwnd(100)),"live monitor path denies window");require(osfacts::s.queryCalls==0&&osfacts::s.snapshots==0,"readable monitor path never reads topology");returned();
 reset({{100,L"Game.exe"}});require(!focus_after::focusIsBackgroundMonitorWindow(hwnd(100)),"live game path not denied");require(osfacts::s.queryCalls==0&&osfacts::s.snapshots==0,"readable nonmonitor path never reads topology");
 reset({{100,L"HWiNFO64.exe"}});osfacts::s.rows[0].openFail=true;require(focus_after::focusIsBackgroundMonitorWindow(hwnd(100))&&osfacts::s.queryCalls==1,"denied monitor path uses owner name only for rejection");returned();
 reset({{100,L"HWiNFO64.exe"}});osfacts::s.rows[0].pathFail=true;require(focus_after::focusIsBackgroundMonitorWindow(hwnd(100))&&osfacts::s.queryCalls==1,"actual focus helper success-with-cleared-path still enters rejection fallback");returned();
 reset({{100,L"Game.exe"}});osfacts::s.rows[0].pathFail=true;require(!focus_after::focusIsBackgroundMonitorWindow(hwnd(100)),"unverifiable game not relabeled as background monitor");
 reset({{100,L"HWiNFO64.exe"}});osfacts::s.rows[0].openFail=true;osfacts::s.mode=osfacts::QueryMode::failed;require(!focus_after::focusIsBackgroundMonitorWindow(hwnd(100)),"failed topology cannot manufacture monitor classification");
 reset({{100,L"HWiNFO64.exe"}});osfacts::s.rows[0].openFail=true;osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.partial=true;require(focus_after::focusIsBackgroundMonitorWindow(hwnd(100)),"partial valid monitor prefix preserves original rejection-only fact");returned();
 reset({{100,L"Game.exe"},{101,L"HWiNFO64.exe"}});osfacts::s.rows[1].openFail=true;osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.partial=true;require(!focus_after::focusIsBackgroundMonitorWindow(hwnd(101)),"unseen PID in failed prefix is not manufactured as monitor");returned();
 reset({{100,L"HWiNFO64.exe"}});osfacts::s.rows[0].openFail=true;osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.partial=true;require(focus_after::focusIsBackgroundMonitorWindow(hwnd(100)),"partial positive hint retained only within its receipt");auto failed=ymcc::processInventory::read(ymcc::processInventory::resolveQuery(),ymcc::processInventory::currentTime,250);require(failed->status==ymcc::processInventory::Status::Failed&&failed->rows.empty()&&failed->rejectionHints.size()==1,"failed table never publishes rejection hints as complete rows");osfacts::s.mode=osfacts::QueryMode::failed;auto next=ymcc::processInventory::read(ymcc::processInventory::resolveQuery(),ymcc::processInventory::currentTime,0);require(next->rows.empty()&&next->rejectionHints.empty(),"later unrelated failure does not borrow old denial-only hints");returned();
 reset({{100,L"FxSound.exe"}});osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.partial=true;require(fxguard::processState()==ymcc::fxsound::ProcessState::unknown,"actual existing FxSound action-status consumer ignores denial-only hints and remains Unknown");returned();
 reset({{100,L"HWiNFO64.exe"}});osfacts::s.rows[0].openFail=true;osfacts::s.mode=osfacts::QueryMode::unsupported;require(focus_after::focusIsBackgroundMonitorWindow(hwnd(100))&&osfacts::s.snapshots==1,"complete centralized old-system fallback retains monitor rejection");returned();
 reset({{100,L"HWiNFO64.exe"}});require(!focus_after::focusIsBackgroundMonitorWindow(nullptr)&&!focus_after::focusIsBackgroundMonitorWindow(hwnd(101))&&osfacts::s.opens==0&&osfacts::s.queryCalls==0,"invalid windows never discover processes");
 reset({{100,L"Unrelated.exe"},{101,L"Game.exe"}});require(focus_after::focusFindProcessByPath(LR"(Q:\Games\Game.exe)")==101,"path finder returns exact live game");require(osfacts::s.opens==1&&windowfacts::finds==1,"basename shortlist avoids unrelated path handles");returned();
 osfacts::s.rows[1].path=LR"(Q:\Else\Game.exe)";require(focus_after::focusFindProcessByPath(LR"(Q:\Games\Game.exe)")==0&&osfacts::s.queryCalls==2,"fresh0 discovery does not reuse old path success");returned();
 reset({{100,L"Game.exe"},{101,L"GAME.EXE"}});osfacts::s.rows[0].path=LR"(Q:\Games\Game.exe)";osfacts::s.rows[1].path=LR"(Q:\GAMES\GAME.EXE)";require(focus_after::focusFindProcessByPath(LR"(q:\games\game.exe)")==100,"case-insensitive original first exact matching row preserved");
 windowfacts::missing.insert(100);require(focus_after::focusFindProcessByPath(LR"(Q:\Games\Game.exe)")==101,"same path without eligible window skips to next exact instance");
 reset({{100,L"Game.exe"},{101,L"Game.exe"}});osfacts::s.rows[0].session=8;require(focus_after::focusFindProcessByPath(LR"(Q:\Games\Game.exe)")==101,"foreign session skipped before exact path admission");
 reset({{100,L"Game.exe"},{101,L"Game.exe"}});osfacts::s.rows[0].sessionFail=true;require(focus_after::focusFindProcessByPath(LR"(Q:\Games\Game.exe)")==101,"unknown candidate session skipped, no stale session assumed");
 for(int guard=0;guard<3;++guard){reset();if(guard==0)osfacts::s.rows[0].openFail=true;if(guard==1)osfacts::s.rows[0].pathFail=true;if(guard==2)windowfacts::missing.insert(100);require(focus_after::focusFindProcessByPath(LR"(Q:\Games\Game.exe)")==0,"unverifiable path or missing window never finds game");returned();}
 reset();osfacts::s.ownSessionFail=true;require(focus_after::focusFindProcessByPath(LR"(Q:\Games\Game.exe)")==0&&osfacts::s.queryCalls==0,"own session unknown prevents discovery");
 reset();osfacts::s.mode=osfacts::QueryMode::failed;require(focus_after::focusFindProcessByPath(LR"(Q:\Games\Game.exe)")==0,"failed owner path discovery returns no target");
 reset();osfacts::s.mode=osfacts::QueryMode::unsupported;osfacts::s.partial=true;require(focus_after::focusFindProcessByPath(LR"(Q:\Games\Game.exe)")==0&&osfacts::s.opens==0,"partial fallback does not open or admit any early match");returned();
 reset();osfacts::s.mode=osfacts::QueryMode::unsupported;require(focus_after::focusFindProcessByPath(LR"(Q:\Games\Game.exe)")==100&&osfacts::s.snapshots==1,"central complete Toolhelp fallback still finds exact target");returned();
 reset();require(focus_after::focusFindProcessByPath(L"")==0&&osfacts::s.queryCalls==0,"empty requested path does no discovery");
 reset();osfacts::s.rows[0].path=LR"(\\server\share\Game.exe)";require(focus_after::focusFindProcessByPath(LR"(\\server\share\Game.exe)")==100,"UNC filename and exact path semantics preserved");
 reset();osfacts::s.rows[0].path=LR"(\\?\Q:\Games\Game.exe)";require(focus_after::focusFindProcessByPath(LR"(\\?\Q:\Games\Game.exe)")==100,"extended Windows path basename semantics retained");
 reset();windowfacts::beforeOpen=[](DWORD){osfacts::s.rows[0].path=LR"(Q:\Other\Other.exe)";};require(focus_after::focusFindProcessByPath(LR"(Q:\Games\Game.exe)")==0,"post-topology image replacement still fails exact live path gate");returned();
 std::cout<<nlohmann::json{{"passed",true},{"checks",checks},{"actualOriginalFunctions",true},{"actualOwner",true},{"realWindowsOrProcesses",false}}.dump()<<"\n";
}
int main(int argc,char**argv){try{if(argc!=2)throw std::runtime_error("selection required");if(!strcmp(argv[1],"--before"))before();else if(!strcmp(argv[1],"--after"))after();else throw std::runtime_error("unknown selection");return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
