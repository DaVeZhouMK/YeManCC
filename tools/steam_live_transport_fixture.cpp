#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <iostream>
#include <future>
#include <thread>
// Preinclude SDK declarations so only the product call, not its import, is wrapped.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <cstring>
#include <limits>
static unsigned listenerToolhelpCalls = 0;
static DWORD listenerToolhelpFlags = 0, listenerToolhelpPid = 0;
static int listenerFallbackFault = 0;
static unsigned listenerFixtureCloses = 0;
static HANDLE listenerFixtureSnapshot() { return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0xC0D0F0)); }
static HANDLE WINAPI observedListenerToolhelp(DWORD flags, DWORD pid) {
 ++listenerToolhelpCalls; listenerToolhelpFlags = flags; listenerToolhelpPid = pid;
 if (listenerFallbackFault == 1) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
 if (listenerFallbackFault) return listenerFixtureSnapshot();
 return ::CreateToolhelp32Snapshot(flags, pid);
}
static BOOL WINAPI observedListenerFirst(HANDLE h, PROCESSENTRY32W* row) {
 if (h != listenerFixtureSnapshot()) return ::Process32FirstW(h,row);
 if (listenerFallbackFault == 2) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
 if (listenerFallbackFault == 4) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
 row->th32ProcessID=424241;row->th32ParentProcessID=424200;
 wcscpy_s(row->szExeFile,L"steamwebhelper.exe");return TRUE;
}
static BOOL WINAPI observedListenerNext(HANDLE h, PROCESSENTRY32W* row) {
 if (h != listenerFixtureSnapshot()) return ::Process32NextW(h,row);
 SetLastError(ERROR_ACCESS_DENIED);return FALSE;
}
static BOOL WINAPI observedListenerClose(HANDLE h) {
 if (h != listenerFixtureSnapshot()) return ::CloseHandle(h);
 ++listenerFixtureCloses;return TRUE;
}
#define CreateToolhelp32Snapshot observedListenerToolhelp
#define Process32FirstW observedListenerFirst
#define Process32NextW observedListenerNext
#define CloseHandle observedListenerClose
#include "steam_live_cdp.h"
#undef CreateToolhelp32Snapshot
#undef Process32FirstW
#undef Process32NextW
#undef CloseHandle
using namespace ymcc::steamlive;
static unsigned listenerChecks = 0;
static void expect(bool ok,const char* message) {++listenerChecks;if(!ok)throw std::runtime_error(message);std::cout<<"PASS "<<message<<"\n";}
static Json target() {return {{"type","page"},{"title","SharedJSContext"},{"url","https://steamloopback.host/index.html"},{"webSocketDebuggerUrl","ws://127.0.0.1:8080/devtools/page/ABC-123"}};}
static constexpr unsigned short testPort=8080;
static const Endpoint testEndpoint{1,testPort,L"127.0.0.1"};
static bool rejected(const Json& list) {try {targetPath(list,testEndpoint);return false;}catch(...){return true;}}
// These NT rows are hints only. No process image, socket, Steam, or CDP port is mocked.
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
}
int main(int argc,char** argv) {try {
 if(argc==2 && std::string(argv[1])=="--listener-inventory-only") {
   listenerTopologyFixture::run();
   std::cout << "LISTENER_INVENTORY_SELECTED_PASS checks=" << listenerChecks << " steamCdpAccess=false" << "\n";
   return 0;
 }
 const auto fixtureStarted = std::chrono::steady_clock::now();
 for(const char* op:{"desktopMouse.get","desktopMouse.set"}){
   std::promise<void> locked,release;auto ready=locked.get_future();auto done=release.get_future();
   std::thread owner([&]{std::lock_guard guard(transportMutex);locked.set_value();done.wait_for(std::chrono::milliseconds(500));});ready.wait();
   const auto started=std::chrono::steady_clock::now();
   const auto receipt=run(L"S:\\YMCC-owned-offline-fixture",{{"operation",op}});
   const auto elapsed=std::chrono::steady_clock::now()-started;release.set_value();owner.join();
   expect(!receipt.value("ok",true)&&receipt.value("reason",std::string{})=="live-busy"&&!receipt.value("mutated",true)&&elapsed<std::chrono::milliseconds(200),"desktop mouse never queues behind another Steam transport owner");
   std::cout << "TRANSPORT_BUSY_OBSERVATION " << Json{{"operation",op},{"elapsedMs",std::chrono::duration<double,std::milli>(elapsed).count()},{"limitMs",200}}.dump() << "\n";
 }

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
}catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}}
