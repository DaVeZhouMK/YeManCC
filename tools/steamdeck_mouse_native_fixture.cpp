#include <iostream>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <codecvt>
#include <locale>
#include <cwctype>
#include <map>
#include <mutex>
#include <atomic>
#include <functional>
#include <vector>
#include <string>
#include <stdexcept>
#include "json.hpp"
#include "steamdeck_mouse_vdf.h"
using json = nlohmann::json;
namespace fspath = std::filesystem;
static std::wstring mockSteamRoot=L"S:\\Steam";
using DWORD = unsigned long;
constexpr int HKEY_CURRENT_USER = 1;
constexpr int FALSE = 0;
constexpr int STEAM_OVERLAY_FIX_TIMER_ID = 0xA221;
constexpr int STEAM_DECK_MOUSE_TIMER_ID = 0xA220;
constexpr int kSteamOverlayFixRetryMs = 30000;
static int g_hwnd = 1;
static struct { bool isolated = false; } g_aiFanMockSession;
static std::map<std::wstring,std::string> disk;
static json receipts, inputConfig;
static bool running = true, timer = false, copyOk = true, writeOk = true, settingsOk = true;
static bool registryAccount = true;
static DWORD activeAccount = 42, mockSteamPid = 1234;
static std::atomic<bool> g_sdmEventPending{false};
static struct {
 int windows = 0; std::function<void(bool)> changed; std::function<bool()> needsRetry;
 void beginWindow() { ++windows; }
 bool start(std::function<void(bool)> c, std::function<bool()> n) { changed=std::move(c); needsRetry=std::move(n); changed(true); return true; }
} g_steamSettingsObserver;
static int writes = 0, backups = 0, events = 0;
static std::string W2U(const std::wstring& s) { return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().to_bytes(s); }
static std::wstring U2W(const std::string& s) { return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().from_bytes(s); }
static std::wstring sofRegSZ(int, const wchar_t*, const wchar_t*) { return mockSteamRoot; }
static bool sofRegDWORD(int, const wchar_t*, const wchar_t* name, DWORD* out) {
 if(std::wstring(name)==L"pid"){*out=mockSteamPid;return true;}
 if(!registryAccount){*out=0;return false;}*out=activeAccount;return true;
}
static bool sofFindLocalConfig(std::wstring* path,std::string* account) { if(account)*account=std::to_string(activeAccount);if(path)*path=L"S:\\Steam\\localconfig.vdf";return activeAccount!=0; }
static bool sofSteamRunning() { return running; }
static int SetTimer(int,int,int,void*) { timer=true;return 1; }
static int KillTimer(int,int) { timer=false;return 1; }
static std::string sgReadFile(const std::wstring& path) { return disk[path]; }
static bool CopyFileW(const wchar_t* source,const wchar_t* target,int) { backups++;if(copyOk)disk[target]=disk[source];return copyOk; }
static bool sgWriteFileAtomic(const std::wstring& path,const std::string& text) { writes++;if(writeOk)disk[path]=text;return writeOk; }
static json ymSettingsSection(const char* section) { return std::string(section)=="input"?inputConfig:receipts; }
static bool ymSettingsWriteSection(const char*,const json& j) { if(!settingsOk)return false;receipts=j;return true; }
static void ipc_emit(const std::string&,const json&) { events++; }
static int gameInputOwnerSession() { return 0; }
namespace ymcc { static json effectiveGameInput(const json& j,int) { return j; } }
// Match the current effective-input accessor; this fixture models one desktop owner.
static json effectiveRuntimeInputSettings() { return ymcc::effectiveGameInput(ymSettingsSection("input"),gameInputOwnerSession()); }
static std::string liveMode = "live-debug-unavailable";
static int liveRequests = 0, liveMutations = 0, overlayRequests = 0, overlayRuntime = 0, overlayFile = 0;
static bool livePersist = true, poolOk = true, failFinish = false, poolHold = false;
static std::vector<std::function<void()>> heldJobs;
static std::function<void()> overlayHook, transportHook;
static void Sleep(unsigned) {}
static bool poolSubmit(std::function<void()> job) { if(!poolOk)return false;if(poolHold)heldJobs.push_back(std::move(job));else job();return true; }
static json mockSteamLiveTransport(const json& request) {
 const DWORD account=request.value("account",0u);
 ++liveRequests;
 if(request["operation"]=="overlay.set")++overlayRequests;
 if(liveMode=="setter-transport-uncertain"&&request["operation"]=="mouse.set")return {{"ok",false},{"reason","live-transport-uncertain"},{"mutated",true}};
 if(liveMode!="ok"&&liveMode!="setter-transport-uncertain")return {{"ok",false},{"reason",liveMode},{"mutated",liveMode=="live-transport-uncertain"}};
 if(account!=activeAccount)return {{"ok",false},{"reason","steam-account-changed"},{"mutated",false}};
 if(request["operation"]=="overlay.set") {
   if(overlayHook){auto hook=std::move(overlayHook);overlayHook={};hook();}
   overlayRuntime=request.value("enabled",true)?1:0; ++liveMutations;
   if(livePersist)overlayFile=overlayRuntime;
   return {{"ok",true},{"value",overlayRuntime}};
 }
 // Convert once: iterators from different temporary strings must not be mixed.
 if(request.value("controllerType",std::string())!="controller_neptune")throw std::runtime_error("production-must-target-neptune-only");
 const auto pathText=request["path"].get<std::string>();const std::wstring filePath=U2W(pathText);
 const int percent=ymcc::steamdeck::desktopLayout(disk[filePath]).sensitivity;
 if(request["operation"]=="mouse.get")return {{"ok",true},{"percent",percent}};
 ++liveMutations;
 if(failFinish)settingsOk=false;
 if(request.value("baseline",-1)!=percent)return {{"ok",false},{"reason","desktop-sensitivity-changed"},{"mutated",false}};
 if(livePersist)disk[filePath]=ymcc::steamdeck::replaceSensitivity(disk[filePath],request["percent"].get<int>());
 return {{"ok",true},{"percent",request["percent"]},{"saved",true}};
}
namespace ymcc::steamlive {
using Json = json;
//__PRODUCTION_DEFER_POLICY__
static json run(const std::wstring&,const json& request) {
 auto result=mockSteamLiveTransport(request);
 if(transportHook){auto hook=std::move(transportHook);transportHook={};hook();}
 return result;
}
}
//__PRODUCTION_LIVE_PREFLIGHT__
static std::mutex g_sofMx;
static std::atomic<bool> g_sofPending{false}, g_sofEnabled{true};
static std::atomic<int> g_sofDesired{-1};
static void sofLog(const char*,const char*,int,bool) {}
static int sofCurrentValue(const std::string&, bool* = nullptr) { return overlayFile; }
static bool sofApplyNowLocked(const char*,int desired) { overlayFile=desired;g_sofPending.store(false);return true; }
static void steamOverlayFixKick(const char*);
//__PRODUCTION_OVERLAY_RUNTIME__

// Runtime is extracted verbatim from native/main.cpp by the test runner.
//__PRODUCTION_RUNTIME__
static const std::wstring path=L"S:\\Steam\\steamapps\\common\\Steam Controller Configs\\42\\config\\413080\\controller_neptune.vdf";
static const std::wstring selection=L"S:\\Steam\\steamapps\\common\\Steam Controller Configs\\42\\config\\configset_controller_neptune.vdf";
static std::string fixture=R"VDF("controller_mappings" {
 "controller_type" "controller_neptune"
 "group" { "id" "9" "mode" "absolute_mouse" "settings" { "sensitivity" "75" } }
 "group" { "id" "77" "mode" "joystick_mouse" "settings" { "sensitivity" "145" "unknown" "KEEP" } }
 "group" { "id" "23" "mode" "joystick_move" "settings" { "sensitivity" "33" } }
 "preset" { "id" "0" "name" "Default" "group_source_bindings" { "9" "right_trackpad active" "77" "right_joystick active" } }
 "preset" { "id" "1" "name" "Gamepad" "group_source_bindings" { "23" "right_joystick active" } }
})VDF";
static void expect(bool condition,const char* label) { if(!condition)throw std::runtime_error(label);std::cout<<"PASS "<<label<<"\n"; }
static void reset() {
 disk.clear();disk[path]=fixture;disk[selection]="\"controller_config\" { \"413080\" { \"autosave\" \"1\" } }";
 receipts=json::object();inputConfig={{"outputTarget",{{"buttonMappingEnabled",true},{"persona","steamdeck"}}}};
 activeAccount=42;mockSteamPid=1234;g_sdmEventPending.store(false);g_steamSettingsObserver.windows=0;g_steamSettingsObserver.changed={};g_steamSettingsObserver.needsRetry={};g_sofRequestSerial.store(0);g_sdmRequestSerial.store(0);g_sdmRefreshRequested.store(false);g_sdmRetryQueued.store(false);registryAccount=true;running=true;timer=false;copyOk=true;writeOk=true;settingsOk=true;g_aiFanMockSession.isolated=false;
 mockSteamRoot=L"S:\\Steam";
 writes=0;backups=0;events=0;liveRequests=0;liveMutations=0;overlayRequests=0;liveMode="live-debug-unavailable";livePersist=true;poolOk=true;overlayRuntime=0;overlayFile=0;g_sofPending.store(false);g_sofDesired.store(-1);g_sofEnabled.store(true);g_sofQueued.store(false);g_sofReceipt={{"via","none"}};g_sdmReceiptError.clear();failFinish=false;poolHold=false;heldJobs.clear();overlayHook={};transportHook={};
 disk[L"S:\\Steam\\localconfig.vdf"]="overlay";
}
static bool rejects(const std::string& text) { try { ymcc::steamdeck::desktopLayout(text);return false; } catch (...) { return true; } }
static std::string readEvidence(const char* path) {
 std::ifstream file(path,std::ios::binary);
 if(!file)throw std::runtime_error("evidence-file-not-found");
 return {std::istreambuf_iterator<char>(file),{}};
}
int main(int argc,char** argv) { try {
 if(argc!=4)throw std::runtime_error("expected index/100-percent/137-percent evidence files");
 const auto evidenceIndex=readEvidence(argv[1]),actual100=readEvidence(argv[2]),actual137=readEvidence(argv[3]);
 expect(ymcc::steamdeck::desktopAutosave(evidenceIndex),"actual ROG selection index uses desktop 413080 autosave");
 const auto layout100=ymcc::steamdeck::desktopLayout(actual100),layout137=ymcc::steamdeck::desktopLayout(actual137);
 expect(layout100.group=="25"&&layout137.group=="25"&&layout100.sensitivity==100&&layout137.sensitivity==137&&layout100.explicitValue&&layout137.explicitValue,"actual saved 100 and 137 percent map directly to right-stick scalar");
 expect(layout100.revision==41&&layout137.revision==42,"actual Steam sensitivity save increments layout revision");
 expect(ymcc::steamdeck::replaceSensitivity(actual100,137)==actual137,"production 100 to 137 transform exactly matches real Steam saved layout bytes");
 expect(ymcc::steamdeck::replaceSensitivity(actual100,100)==actual100,"unchanged value leaves entire actual layout byte-identical including revision");
 for(int value:{1,137,300}) {
   const auto changed=ymcc::steamdeck::replaceSensitivity(actual100,value);
   const auto parsed=ymcc::steamdeck::desktopLayout(changed);
   expect(parsed.sensitivity==value&&parsed.revision==42,"actual layout boundaries update scalar and increment revision once");
 }
 reset();disk[path]=actual100;disk[selection]=evidenceIndex;
 auto actualReceipt=steamDeckMouseSet({{"percent",137}});expect(actualReceipt["pending"]==true&&writes==0,"actual fixture set remains deferred while Steam running");
 running=false;steamDeckMouseRetry();expect(disk[path]==actual137&&disk[path+L".ymcc-bak"]==actual100&&steamDeckMouseGet()["percent"]==137,"production runtime output matches actual 137 archive with original backup/readback");
 auto revisionAtEnd=fixture;revisionAtEnd.insert(revisionAtEnd.rfind('}')," \"revision\" \"9\" ");
 const auto revisionTail=ymcc::steamdeck::desktopLayout(ymcc::steamdeck::replaceSensitivity(revisionAtEnd,200));
 expect(revisionTail.sensitivity==200&&revisionTail.revision==10,"revision edits work even when metadata appears after source groups");
 auto badRevision=actual100;const std::string revisionToken="\"revision\"\t\t\"41\"";const auto revisionPos=badRevision.find(revisionToken);
 badRevision.replace(revisionPos,revisionToken.size(),"\"revision\" \"2147483647\"");
 expect(ymcc::steamdeck::desktopLayout(badRevision).revision==2147483647LL,"maximum existing revision remains readable");
 bool overflowRejected=false;try{ymcc::steamdeck::replaceSensitivity(badRevision,137);}catch(...){overflowRejected=true;}
 expect(overflowRejected,"revision overflow rejected before any replacement");
 auto nearMax=actual100;nearMax.replace(nearMax.find(revisionToken),revisionToken.size(),"\"revision\" \"2147483646\"");
 expect(ymcc::steamdeck::desktopLayout(ymcc::steamdeck::replaceSensitivity(nearMax,137)).revision==2147483647LL,"last valid revision increment still parses after write");
 reset();expect(steamDeckMouseGet()["percent"]==145&&writes==0,"get is actual file value, read only");
 for(int value:{1,100,137,300}) {
   const auto changed=ymcc::steamdeck::replaceSensitivity(fixture,value);
   expect(ymcc::steamdeck::desktopLayout(changed).sensitivity==value,"all percent boundaries roundtrip");
   expect(changed.find("\"sensitivity\" \"75\"")!=std::string::npos&&changed.find("\"sensitivity\" \"33\"")!=std::string::npos&&changed.find("\"unknown\" \"KEEP\"")!=std::string::npos,"trackpad gamepad unknown bytes preserved");
 }
 auto missing=fixture;const auto at=missing.find("\"sensitivity\" \"145\"");missing.erase(at,19);
 expect(ymcc::steamdeck::desktopLayout(missing).sensitivity==100,"absent scalar displays requested default 100");
 expect(ymcc::steamdeck::desktopLayout(ymcc::steamdeck::replaceSensitivity(missing,300)).sensitivity==300,"missing scalar inserted only in matched group settings");
 expect(ymcc::steamdeck::desktopLayout("\xef\xbb\xbf// comment\n"+fixture).sensitivity==145,"BOM and comments supported");
 expect(rejects(fixture.substr(0,fixture.size()-1)),"truncated VDF rejected");
 auto ambiguous=fixture;auto bind=ambiguous.find("\"77\" \"right_joystick active\"");ambiguous.insert(bind,"\"78\" \"right_joystick active\" ");expect(rejects(ambiguous),"ambiguous right stick rejected");
 auto wrong=fixture;wrong.replace(wrong.find("joystick_mouse"),14,"joystick_move");expect(rejects(wrong),"non mouse source not rewritten");
 auto invalid=fixture;invalid.replace(invalid.find("\"145\""),5,"\"10001\"");expect(rejects(invalid),"out of range Steam scalar rejected");
 auto shared=fixture;shared.insert(shared.find("\"23\" \"right_joystick active\""),"\"77\" \"right_joystick active\" ");expect(rejects(shared),"shared action-set groups never rewritten");
 auto result=steamDeckMouseSet({{"percent",300}});expect(result["ok"]==true&&result["pending"]==true&&result["percent"]==145&&writes==0&&!timer&&g_sdmEventPending.load(),"Steam running queues durable request without writes or a recurring timer");
 expect(receipts["desired"]==300&&receipts["baseline"]==145,"pending stores desired and baseline in native section");
 running=false;steamDeckMouseRetry();expect(writes==1&&backups==1&&!timer&&!receipts["pending"].get<bool>()&&steamDeckMouseGet()["percent"]==300,"Steam exit applies backed up atomic write and actual readback");
 expect(disk[path+L".ymcc-bak"]==fixture,"backup is full original layout");steamDeckMouseRetry();expect(writes==1,"completed retry no longer writes");
 reset();steamDeckMouseSet({{"percent",200}});running=false;activeAccount=43;steamDeckMouseRetry();expect(writes==0&&receipts["pending"].get<bool>()&&!g_sdmEventPending.load()&&receipts["liveReason"]=="steam-account-waiting","different account preserves original mouse request without writing or timed retries");
 activeAccount=42;steamDeckMouseRetry();expect(writes==1&&steamDeckMouseGet()["percent"]==200&&!receipts["pending"].get<bool>(),"returning to original account heals pending mouse request");
 reset();steamDeckMouseSet({{"percent",200}});running=false;disk[path]=ymcc::steamdeck::replaceSensitivity(fixture,137);steamDeckMouseRetry();expect(writes==0&&receipts["error"]=="desktop-sensitivity-changed","Steam edits while waiting take precedence");
 reset();running=false;copyOk=false;result=steamDeckMouseSet({{"percent",200}});expect(result["ok"]==false&&writes==0&&disk[path]==fixture,"backup failure leaves original untouched and reports failure");
 reset();running=false;writeOk=false;result=steamDeckMouseSet({{"percent",200}});expect(result["ok"]==false&&disk[path]==fixture,"atomic write failure is visible");
 reset();settingsOk=false;result=steamDeckMouseSet({{"percent",200}});expect(result["ok"]==false&&writes==0&&backups==0,"receipt failure cannot modify Steam");
 reset();disk[selection]="\"controller_config\" { \"413080\" { \"autosave\" \"1\" \"template\" \"CLOUD_other/file\" } }";expect(steamDeckMouseGet()["available"]==false&&steamDeckMouseSet({{"percent",200}})["ok"]==false&&writes==0,"templates and other configs never guessed or modified");
 reset();inputConfig["outputTarget"]["persona"]="elite";expect(steamDeckMouseSet({{"percent",200}})["ok"]==false&&writes==0,"not SteamDeck cannot set");
 reset();g_aiFanMockSession.isolated=true;expect(steamDeckMouseGet()["available"]==false&&steamDeckMouseSet({{"percent",200}})["ok"]==false&&writes==0,"AI isolation prevents ambient reads and writes");
 reset();registryAccount=false;running=false;expect(steamDeckMouseSet({{"percent",1}})["ok"]==true&&steamDeckMouseGet()["percent"]==1,"last account fallback works when Steam has cleared ActiveUser");
 reset();expect(steamDeckMouseSet({{"percent",0}})["ok"]==false&&steamDeckMouseSet({{"percent",301}})["ok"]==false&&steamDeckMouseSet({{"percent",1.5}})["ok"]==false&&steamDeckMouseSet({{"percent",4294967297LL}})["ok"]==false&&writes==0,"invalid set values rejected before disk writes");
 reset();receipts={{"pending","corrupt"}};steamDeckMouseRetry();expect(writes==0&&!timer,"corrupt durable receipt cannot crash startup or timer");

 reset();liveMode="ok";result=steamDeckMouseGet();expect(result["liveAvailable"]==true&&liveMutations==0,"live get probes native runtime without writing");
 result=steamDeckMouseSet({{"percent",137}});expect(result["ok"]==true&&!result["pending"].get<bool>()&&result["appliedLive"]==true&&result["percent"]==137&&liveMutations==1&&writes==0&&backups==1,"live set uses Steam save, readback and backup without external write");
 reset();liveMode="live-editor-busy";result=steamDeckMouseSet({{"percent",137}});expect(result["ok"]==false&&!receipts["pending"].get<bool>()&&liveMutations==0&&writes==0,"dirty Steam editor is refused, never queued to overwrite later");
 reset();liveMode="setter-transport-uncertain";result=steamDeckMouseSet({{"percent",137}});expect(result["ok"]==false&&!receipts["pending"].get<bool>()&&writes==0,"uncertain live request never triggers file fallback");
 reset();liveMode="ok";livePersist=false;result=steamDeckMouseSet({{"percent",137}});expect(result["ok"]==false&&result["error"]=="live-file-not-confirmed"&&writes==0&&!receipts["pending"].get<bool>(),"runtime acceptance without disk confirmation is not reported as saved");
 reset();liveMode="ok";copyOk=false;result=steamDeckMouseSet({{"percent",137}});expect(result["ok"]==false&&liveMutations==0,"live mutation requires a successful original backup");
 reset();disk[path]=ymcc::steamdeck::replaceSensitivity(fixture,300);auto text323=disk[path];text323.replace(text323.find("\"300\""),5,"\"323\"");disk[path]=text323;result=steamDeckMouseGet();expect(result["percent"]==323&&liveMutations==0,"existing 323 percent is readable without clamping or default writes");
 reset();liveMode="ok";steamOverlayFixKick("test-on");expect(steamOverlayFixGet()["via"]=="live"&&overlayRuntime==0&&!g_sofPending.load(),"wake repair switch applies live overlay off");
 g_sofEnabled.store(false);steamOverlayFixKick("test-off");expect(overlayRuntime==1&&overlayFile==1&&steamOverlayFixGet()["persisted"]==true,"wake repair off restores overlay through Steam itself");
 reset();steamOverlayFixKick("test-disabled-debug");expect(steamOverlayFixGet()["via"]=="deferred"&&g_sofPending.load()&&liveMutations==0,"overlay debug unavailable explicitly defers without enabling debug");
 running=false;steamOverlayFixKick("setting");expect(steamOverlayFixGet()["via"]=="file"&&!g_sofPending.load(),"a new manual attempt can write the deferred setting only when Steam is stopped");
 reset();liveMode="live-transport-uncertain";steamOverlayFixKick("test-uncertain");expect(steamOverlayFixGet()["via"]=="error"&&!g_sofPending.load(),"overlay uncertain call never falls back to a file overwrite");
 reset();g_aiFanMockSession.isolated=true;steamOverlayFixKick("test-isolation");expect(liveRequests==0&&liveMutations==0,"overlay AI isolation forbids ambient Steam requests");
 reset();poolOk=false;steamOverlayFixKick("test-worker-full");expect(!g_sofQueued.load()&&g_sofPending.load()&&!timer,"overlay queue rejection retains manual retry intent without blocking UI or starting a timer");


 reset();liveMode="ok";failFinish=true;result=steamDeckMouseSet({{"percent",137}});expect(result["ok"]==false&&receipts["phase"]=="live-inflight"&&liveMutations==1,"live in-flight phase persists before mutation even when completion receipt fails");
 running=false;steamDeckMouseRetry();expect(writes==0&&liveMutations==1,"uncertain in-flight request is never replayed or written after Steam exit");
 settingsOk=true;steamDeckMouseRetry();expect(receipts["error"]==""&&receipts["reconciled"]==true&&!receipts["pending"].get<bool>()&&writes==0&&liveMutations==1,"matching offline readback heals uncertain receipt without replay or VDF write");
 reset();steamDeckMouseSet({{"percent",137}});inputConfig["outputTarget"]["persona"]="elite";running=false;steamDeckMouseRetry();expect(writes==0&&!receipts["pending"].get<bool>(),"turning off SteamDeck cancels a pending request before hardware/file changes");


 reset();liveMode="ok";poolHold=true;steamOverlayFixKick("queued-on");g_sofEnabled.store(false);steamOverlayFixKick("queued-off");expect(heldJobs.size()==1,"multiple toggles share one worker rather than blocking or flooding the queue");poolHold=false;auto held=std::move(heldJobs.front());heldJobs.clear();held();expect(overlayRuntime==1&&steamOverlayFixGet()["value"]==1&&!g_sofQueued.load(),"queued overlay worker applies latest toggle");
 reset();liveMode="ok";overlayHook=[] {g_sofEnabled.store(false);steamOverlayFixKick("toggle-during-native");};steamOverlayFixKick("initial-toggle");expect(overlayRuntime==1&&steamOverlayFixGet()["value"]==1&&liveMutations==2&&!g_sofQueued.load(),"toggle during native call follows up with latest target, never drops it");


 reset();steamDeckMouseSet({{"percent",200}});activeAccount=0;steamDeckMouseRetry();
 expect(receipts["pending"]==true&&receipts["phase"]=="queued"&&receipts["liveReason"]=="steam-session-starting"&&liveMutations==0&&writes==0,"ActiveUser zero is startup wait, not account cancellation");
 activeAccount=42;mockSteamPid=4321;liveMode="ok";steamDeckMouseRetry();
 expect(!receipts["pending"].get<bool>()&&receipts["livePid"]==4321&&steamDeckMouseGet()["percent"]==200&&writes==0,"same-account replacement PID resumes live mouse save");
 reset();liveMode="ok";activeAccount=0;steamOverlayFixKick("setting");
 expect(steamOverlayFixGet()["via"]=="deferred"&&g_sofPending.load()&&liveRequests==0,"overlay waits while login account is unknown without transport work");
 activeAccount=43;mockSteamPid=5678;g_sofEnabled.store(false);steamOverlayFixKick("setting");
 expect(overlayRuntime==1&&steamOverlayFixGet()["via"]=="live","overlay user intent follows a verified different account and PID");
 reset();liveMode="ok";transportHook=[] {mockSteamPid=4321;};
 auto changedRead=steamLiveRequest(42,{{"operation","mouse.get"},{"controllerType","controller_neptune"},{"path",W2U(path)}});
 expect(changedRead["reason"]=="steam-session-changed"&&!changedRead["mutated"].get<bool>()&&steamLiveCanDefer(changedRead),"PID change during a read is safe to defer");
 reset();liveMode="ok";transportHook=[] {activeAccount=0;};
 auto changedSet=steamLiveRequest(42,{{"operation","overlay.set"},{"enabled",true}});
 expect(changedSet["mutated"]==true&&!steamLiveCanDefer(changedSet),"session loss after a setter never authorizes replay");
 reset();liveMode="steam-session-changed";steamDeckMouseSet({{"percent",200}});
 expect(receipts["pending"]==true&&receipts["phase"]=="queued"&&liveMutations==0,"negative acknowledgement remains queued rather than uncertain-inflight");
 liveMode="ok";steamDeckMouseRetry();expect(steamDeckMouseGet()["percent"]==200&&liveMutations==1,"definite preflight failure heals once session is ready");
 reset();steamSettingsObserverStart();liveMode="ok";mockSteamPid=4321;g_steamSettingsObserver.changed(true);
 expect(steamOverlayFixGet()["via"]=="none"&&g_sofRequestSerial.load()==0&&overlayRequests==0,"startup and session changes never apply an enabled remembered overlay choice");
 g_sofPending.store(true);
 expect(!g_steamSettingsObserver.needsRetry(),"pending overlay does not request a shared detection or retry window");
 g_steamSettingsObserver.changed(false);
 expect(g_sofRequestSerial.load()==0&&overlayRequests==0,"shared retry event never resubmits overlay work");
 reset();g_sofEnabled.store(false);steamSettingsObserverStart();liveMode="ok";g_steamSettingsObserver.changed(true);
 expect(steamOverlayFixGet()["via"]=="none"&&g_sofRequestSerial.load()==0&&overlayRequests==0,"default disabled choice stays idle at startup and Steam session events");
 const auto observerWindows=g_steamSettingsObserver.windows;steamOverlayFixKick("setting");
 expect(overlayRequests==1&&overlayRuntime==1&&!g_sofPending.load()&&!timer&&g_steamSettingsObserver.windows==observerWindows,"one explicit user action applies once without opening an observer window or timer");
 g_steamSettingsObserver.changed(true);g_steamSettingsObserver.changed(false);
 expect(overlayRequests==1&&g_sofRequestSerial.load()==1,"later session and retry events cannot reapply the manual overlay setting");
 reset();steamSettingsObserverStart();steamOverlayFixKick("setting");liveMode="ok";mockSteamPid=4321;g_steamSettingsObserver.changed(true);
 expect(overlayRequests==1&&steamOverlayFixGet()["via"]=="deferred"&&g_sofRequestSerial.load()==1,"a failed explicit action keeps its receipt but never auto-retries on session change");
 reset();liveMode="ok";overlayHook=[] {mockSteamPid=4321;steamOverlayFixKick("setting");};steamOverlayFixKick("initial");
 expect(steamOverlayFixGet()["via"]=="live"&&!g_sofQueued.load(),"same-target user submission during IO gets a followup, not dropped");
 reset();receipts={{"account",42},{"path",W2U(path)},{"pending",false},{"error","steam-account-changed"}};
 expect(!steamDeckMouseGet().contains("error"),"legacy account-change error is not left stuck in UI");
 reset();steamDeckMouseSet({{"percent",200}});g_sdmEventPending.store(false);poolOk=false;steamSettingsObserverStart();
 expect(g_sdmEventPending.load()&&receipts["pending"]==true,"startup worker rejection retains durable mouse retry demand");
 poolOk=true;liveMode="ok";g_steamSettingsObserver.changed(false);
 expect(steamDeckMouseGet()["percent"]==200&&!receipts["pending"].get<bool>(),"bounded startup retry recovers mouse after initial queue rejection");
 reset();liveMode="live-transport-uncertain";steamDeckMouseSet({{"percent",200}});
 expect(receipts["pending"]==true&&receipts["liveReason"]=="live-read-unavailable"&&liveMutations==0,"interrupted GET preserves mouse request for safe reconnection");
 mockSteamPid=4321;liveMode="ok";steamDeckMouseRetry();
 expect(liveMutations==1&&steamDeckMouseGet()["percent"]==200,"replacement PID heals interrupted read then performs one explicit SET");
 reset();liveMode="setter-transport-uncertain";steamDeckMouseSet({{"percent",200}});running=false;steamDeckMouseRetry();
 expect(writes==0&&receipts["error"]=="live-transport-uncertain","uncertain SET with nonmatching readback never replays or writes offline");
 reset();liveMode="ok";livePersist=false;steamDeckMouseSet({{"percent",200}});disk[path]=ymcc::steamdeck::replaceSensitivity(fixture,200);steamDeckMouseRetry(true);
 expect(receipts["error"]==""&&receipts["reconciled"]==true&&liveMutations==1&&writes==0,"late Steam-owned file save heals old error by matching runtime/file reads");
 reset();liveMode="live-busy";steamDeckMouseSet({{"percent",200}});
 expect(receipts["pending"]==true&&receipts["phase"]=="queued"&&liveMutations==0,"another live transaction may defer without losing mouse intent");
 liveMode="ok";steamDeckMouseRetry();expect(liveMutations==1&&steamDeckMouseGet()["percent"]==200,"temporary transaction contention heals on retry");
 reset();g_aiFanMockSession.isolated=true;steamSettingsObserverStart();
 expect(!g_steamSettingsObserver.changed&&liveRequests==0,"isolated startup never starts ambient Steam observer");

 for (const auto& root:std::vector<std::wstring>{L"C:\\Program Files (x86)\\Steam",L"E:\\PortableSteam",L"F:\\Steam-\u4fbf\u643a"}) {
   for (const DWORD account:std::vector<DWORD>{17,89250578,1234567890}) {
     reset();mockSteamRoot=root;activeAccount=account;
     const auto directory=root+L"\\steamapps\\common\\Steam Controller Configs\\"+std::to_wstring(account)+L"\\config\\";
     disk[directory+L"configset_controller_neptune.vdf"]=disk[selection];
     auto alternate=fixture;size_t found=0;while((found=alternate.find("\"77\"",found))!=std::string::npos){alternate.replace(found,4,"\"901\"");found+=5;}
     const auto selected=directory+L"413080\\controller_neptune.vdf";disk[selected]=alternate;
     liveMode="ok";result=steamDeckMouseSet({{"percent",137}});
     expect(result["ok"]==true&&result["appliedLive"]==true&&ymcc::steamdeck::desktopLayout(disk[selected]).group=="901"&&disk[path]==fixture,"cross-machine Steam path/account/group are dynamic; no changes to another machine's file");
   }
 }
 reset();mockSteamRoot=L"C:\\Steam";activeAccount=98765;
 const std::wstring library=L"E:\\SteamLibrary",directory=library+L"\\steamapps\\common\\Steam Controller Configs\\98765\\config\\";
 disk[mockSteamRoot+L"\\steamapps\\libraryfolders.vdf"]="\"libraryfolders\" { \"0\" { \"path\" \"C:\\\\Steam\" } \"1\" { \"path\" \"E:\\\\SteamLibrary\" } }";
 disk[directory+L"configset_controller_neptune.vdf"]=disk[selection];disk[directory+L"413080\\controller_neptune.vdf"]=fixture;
 liveMode="ok";result=steamDeckMouseSet({{"percent",137}});expect(result["ok"]==true&&result["appliedLive"]==true&&writes==0,"SteamDeck autosave in a separate Steam-declared library uses the same live path");
 reset();disk[L"S:\\Steam\\steamapps\\libraryfolders.vdf"]="\"LibraryFolders\" { \"TimeNextStatsReport\" \"0\" \"1\" \"T:\\\\Library\" }";
 const std::wstring second=L"T:\\Library\\steamapps\\common\\Steam Controller Configs\\42\\config\\";disk[second+L"configset_controller_neptune.vdf"]=disk[selection];disk[second+L"413080\\controller_neptune.vdf"]=fixture;
 expect(steamDeckMouseGet()["reason"]=="desktop-autosave-ambiguous"&&steamDeckMouseSet({{"percent",137}})["ok"]==false&&liveMutations==0,"duplicate selected layouts across libraries are refused rather than guessing the current one");
 reset();disk.erase(path);disk.erase(selection);const std::wstring genericDir=L"S:\\Steam\\steamapps\\common\\Steam Controller Configs\\42\\config\\";disk[genericDir+L"configset_controller_generic.vdf"]="\"controller_config\" { \"413080\" { \"autosave\" \"1\" } }";disk[genericDir+L"413080\\controller_generic.vdf"]=fixture;
 expect(steamDeckMouseGet()["available"]==false&&steamDeckMouseSet({{"percent",137}})["ok"]==false&&liveMutations==0&&writes==0,"generic test controller can never substitute for an absent SteamDeck layout");

 std::cout<<"STEAMDECK_MOUSE_NATIVE_OK (production runtime mocked, hardware operations 0)\n";return 0;
 } catch(const std::exception& e) {std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}}

