// The runner appends actual storage consumers extracted from native/main.cpp.
// Memory transactions and host/device admission are inert; a separate owned
// Win32 IO consumer tests real file, encoding and lock failures. Production
// JSON merge/guards/writer bodies and the actual settings.write handler run.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include <filesystem>
#include <thread>
#include <future>
#include <mutex>
#include <chrono>
#include "settings_baseline.h"
#include "sleep_power_revocation.h"
#include "steam_monitor_settings.h"
#include "settings_upgrade_recovery.h"
#include "controller_persona_settings.h"
#include "boot_mode_shortcuts.h"
#include "boot_mode_preferences.h"
#include "decky_sidebar_manual_repair.h"
#include "decky_sidebar_diagnostics.h"
#if defined(YMCC_SETTINGS_VIRTUAL_START_WRITE_FAILURE_SCOPE)
#include "input_shortcut_toggle.h"
#include "virtual_start_protection.h"
#endif
#include <iostream>
#include <map>
#include <functional>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <set>
#include <limits>
#include <stdexcept>
using json = nlohmann::json;

// Test release string only. Actual running literal/receipt authority is a declared
// fixture port below; this macro never asserts installed-image identity.
#ifndef APP_VER_STR
#define APP_VER_STR "0.0.40"
#endif

// SETTINGS_BASELINE_DECKY_UPGRADE_FIXTURE_PORT_BEGIN
// SETTINGS_BASELINE_UPGRADE_POLICY_HEADER
// ONLY protected installation IO/verified image digest are fixture ports.
// Actual settings pair reads, guards, CreateNew/flush/replace/readback remain
// the production Win32 bodies below. Do not include the real ACL store header.
namespace ymcc::deckyupgrade::runningstore {
struct FixturePort {
 bool enabled=false;
 std::wstring expectedExe;
 std::string normalizedExe="c:\\owned-fixture-install\\yemancc.exe";
 running::Build currentBuild;
 Json history=Json::object();
 std::string digestValue;
 unsigned calls=0,commits=0,digestReads=0;
};
inline FixturePort fixture;
struct FixtureLease {
 FixturePort saved;
 FixtureLease():saved(std::move(fixture)){fixture=FixturePort{};}
 ~FixtureLease(){fixture=std::move(saved);}
 FixtureLease(const FixtureLease&)=delete;
 FixtureLease& operator=(const FixtureLease&)=delete;
};
inline bool reserve(const std::wstring& exe,const std::string& version,bool reopen,const std::string& steamTicket,
                    const Json& priorLedger,const Json& reservedLedger,
                    const std::function<std::string(const std::wstring&)>& digest,
                    const std::function<bool()>& current,const char** stage=nullptr){
 if(stage)*stage="grant";
 if(!fixture.enabled)return false; // All original ordinary consumers keep this default.
 ++fixture.calls;
 if(!current||!current()||exe!=fixture.expectedExe||!digest||version!=fixture.currentBuild.version)return false;
 if(digest(exe)!=fixture.currentBuild.sha256||!current())return false;
 const auto observed=running::observe(fixture.history,fixture.currentBuild,fixture.normalizedExe);
 if(observed.is_null())return false;
 const auto next=running::reserve(observed,fixture.currentBuild,fixture.normalizedExe,
                                     reopen,steamTicket,priorLedger,reservedLedger);
 // Independent MEMORY installation history stands for parent's protected IO;
 // its persistence/ACL/anti-rollback implementation is NOT executed in this leaf.
 fixture.history=next.is_null()?observed:next;
 if(next.is_null())return false;
 ++fixture.commits;
 return current();
}
}
static std::string sha256FileCancellable(const std::wstring& path,const std::function<bool()>& cancelled){
 auto& port=ymcc::deckyupgrade::runningstore::fixture;
 if(!port.enabled||path!=port.expectedExe||(cancelled&&cancelled()))return {};
 ++port.digestReads;
 return port.digestValue; // Declared read-only image-digest port, not a file hash.
}
// SETTINGS_BASELINE_DECKY_UPGRADE_FIXTURE_PORT_END


#if defined(YMCC_SETTINGS_SERIAL_CONFIG_SCOPE)
#if defined(YMCC_SETTINGS_SECTION_CACHE_SCOPE)
#error serial-config and section-cache scopes are exclusive
#endif
namespace serialConfigFixture {
constexpr const char* kSerialConfigCaseNames[] = {
 "memory/document/unrelated",
 "memory/section/unrelated",
 "memory/patch/unrelated",
 "memory/document/cas-rejected",
 "memory/document/atomic-denied",
 "memory/section/atomic-denied",
 "memory/patch/atomic-denied",
 "win32/document/unrelated",
 "win32/section/unrelated",
 "win32/patch/unrelated",
 "win32/document/cas-rejected",
 "win32/document/atomic-denied",
 "win32/section/atomic-denied",
 "win32/patch/atomic-denied",
 "source-read-primary",
 "source-read-backup",
 "source-read-failed",
 "memory/document/rules-committed",
 "memory/section/rules-committed",
 "memory/patch/rules-committed",
 "win32/document/rules-committed",
 "win32/section/rules-committed",
 "win32/patch/rules-committed",
};
constexpr std::size_t kSerialConfigCaseCount=sizeof(kSerialConfigCaseNames)/sizeof(kSerialConfigCaseNames[0]);
// The parent injects the actual event type and rule helpers immediately after
// the json alias, before this observer. This port never publishes or writes.
struct AtomicObservation {std::wstring mainPath;json samples=json::array();bool lost=false;};
static AtomicObservation* activeObservation=nullptr;
static void observeAtomicWrite(const std::wstring& path,const std::string& content) noexcept {
 if(!activeObservation||path!=activeObservation->mainPath)return;
 const DWORD savedError=::GetLastError();
 try{activeObservation->samples.push_back({
  {"writers",GamepadSerialEvent::ConfigurationWriters.load(std::memory_order_acquire)},
  {"required",GamepadSerialEvent::RulesRequired.load(std::memory_order_acquire)},
  {"rules",gamepadSerialActionRuleSnapshot(json::parse(content))}});
 }catch(...){activeObservation->lost=true;}
 ::SetLastError(savedError);
}
struct ObservationLease {
 explicit ObservationLease(AtomicObservation& value){if(activeObservation)throw std::runtime_error("nested serial-config observation");activeObservation=&value;}
 ~ObservationLease(){activeObservation=nullptr;}
 ObservationLease(const ObservationLease&)=delete;
 ObservationLease& operator=(const ObservationLease&)=delete;
};
}
#endif
#if !defined(YMCC_SETTINGS_SECTION_CACHE_SCOPE)
#include "decky_loader_identity.h"
namespace ymcc::deckymirror {
// SETTINGS_BASELINE_FRONTEND_IDENTITY_READER
}
static std::wstring POWER_CONTROL_DIR;
class SettingsCleanupFrontendFixture {
 std::filesystem::path root_;bool owned_=false;
public:
 SettingsCleanupFrontendFixture(){
  const DWORD required=GetEnvironmentVariableW(L"YMCC_BASELINE_EVIDENCE",nullptr,0);
  if(!required||required>32768)throw std::runtime_error("cleanup frontend evidence root unavailable");
  std::wstring base(required,L'\0');const DWORD copied=GetEnvironmentVariableW(L"YMCC_BASELINE_EVIDENCE",base.data(),required);
  if(!copied||copied>=required)throw std::runtime_error("cleanup frontend evidence root changed");base.resize(copied);
  const std::filesystem::path parent(base);
  if(!parent.is_absolute()||!std::filesystem::is_directory(parent))throw std::runtime_error("cleanup frontend evidence root must exist and be absolute");
  root_=parent/(L"cleanup-frontend-"+std::to_wstring(::GetCurrentProcessId()));
  if(!std::filesystem::create_directory(root_))throw std::runtime_error("cleanup frontend owned root must be fresh");owned_=true;
  try{
   const auto dist=root_/L"decky"/L"plugins"/L"ymcc-sidebar"/L"dist";std::filesystem::create_directories(dist);
   const auto file=dist/L"index.js";std::ofstream output(file,std::ios::binary|std::ios::trunc);
   output<<"// YMCC_DECKY_FRONTEND_BUILD_ID="<<std::string(64,'a')<<'\n';output.close();
   if(output.fail()||ymcc::deckymirror::frontendBuildId(file.wstring())!=std::string(64,'a'))throw std::runtime_error("actual frontend identity reader rejected owned marker");
   POWER_CONTROL_DIR=root_.wstring();
  }catch(...){std::error_code ignored;std::filesystem::remove_all(root_,ignored);owned_=false;throw;}
 }
 SettingsCleanupFrontendFixture(const SettingsCleanupFrontendFixture&)=delete;
 SettingsCleanupFrontendFixture& operator=(const SettingsCleanupFrontendFixture&)=delete;
 void close(){
  if(!owned_)return;std::error_code error;std::filesystem::remove_all(root_,error);
  if(error||std::filesystem::exists(root_))throw std::runtime_error("cleanup frontend owned resources not returned");
  owned_=false;POWER_CONTROL_DIR.clear();
 }
 ~SettingsCleanupFrontendFixture(){if(owned_){std::error_code ignored;std::filesystem::remove_all(root_,ignored);POWER_CONTROL_DIR.clear();}}
};
#endif
#if !defined(YMCC_SETTINGS_SECTION_CACHE_SCOPE)
// Only the external device/component call is a memory port. The original
// registration, validation and unified-settings transaction execute below.
namespace ymcc::sleeppower {struct SharedPowerV1;}
namespace inputRuntimePowerFixture {
static std::mutex gate;
static ymcc::sleeppower::SharedPowerV1 value;
static bool known=false;
static ymcc::sleeppower::RevocationMirrors mirrors;
static int read(ymcc::sleeppower::SharedPowerV1* output,std::uint32_t bytes) noexcept {
 std::lock_guard<std::mutex> lock(gate);if(!known||!output||bytes!=sizeof(value))return 0;*output=value;return 1;
}
static void ready(){std::lock_guard<std::mutex> lock(gate);value={};value.generation=7;value.publicationRevision=11;value.observedNotificationSequence=19;value.suspendSerial=3;value.flags=3;known=true;}
static void revoke(bool aba=false){mirrors.revoke();std::lock_guard<std::mutex> lock(gate);if(!aba){value.flags=1;value.phase=1;++value.observedNotificationSequence;++value.publicationRevision;}}
}
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* value,std::uint32_t bytes) noexcept {return inputRuntimePowerFixture::read(value,bytes);}
static bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& value) noexcept {return ymcc::sleeppower::capturePower(&readSharedSystemPower,value);}
static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& value) noexcept {return ymcc::sleeppower::powerCurrent(&readSharedSystemPower,value);}
static ymcc::sleeppower::RevocationMirrors::Registration observeSharedPowerRevocation(const ymcc::sleeppower::SharedPowerV1& value) noexcept {return inputRuntimePowerFixture::mirrors.observe(&readSharedSystemPower,value);}
namespace ymcc::batterycharge {
using Json=nlohmann::json;
using SystemPowerReader=int(__cdecl*)(ymcc::sleeppower::SharedPowerV1*,std::uint32_t);
static bool bindSystemPowerReader(SystemPowerReader reader) noexcept {return reader!=nullptr;}
static bool hasBattery(){return true;}
// SETTINGS_BASELINE_CHARGE_VALIDATION
static unsigned fixtureRequests=0,fixtureRestores=0;
static Json fixtureResponse,fixtureLastRequest;
static std::function<void()> fixtureBeforeReturn;
static Json request(const std::wstring&,const std::wstring&,const Json& input,const std::function<bool()>& = {}){
 ++fixtureRequests;fixtureLastRequest=input;if(fixtureBeforeReturn)fixtureBeforeReturn();return fixtureResponse;
}
static void restoreAtStartup(const std::wstring&,const std::wstring&){++fixtureRestores;}
}
static std::wstring yemancc_data_dir(){return L"owned-charge-component-memory-port";}
// Explicit inert boot/device ownership port, consistent with this memory-only
// external component. Actual boot admission has its own original consumer.
static std::function<bool()> monitorHudBootOwnerCheck(const json&){return {};}
static struct {bool isolated=true;} g_aiFanMockSession;
namespace ymcc::powerSource {
static bool fixtureReadable=true;static BYTE fixtureAc=1;
static bool read(SYSTEM_POWER_STATUS& value){value={};value.ACLineStatus=fixtureAc;return fixtureReadable;}
}
#endif
namespace settingsIoFixture {
static std::timed_mutex g_settingsFileMtx;
static bool simulateArchiveTimeout=false;static unsigned long long timeAdvance=0;
static ULONGLONG GetTickCount64(){if(simulateArchiveTimeout)timeAdvance+=2100;return ::GetTickCount64()+timeAdvance;}
static std::wstring YM_SETTINGS_FILE,YM_SETTINGS_BACKUP_FILE;
static std::wstring isolatedMutexName(){return L"Local\\YMCC-ConfigStorm-"+std::to_wstring(GetCurrentProcessId());}
namespace fspath=std::filesystem;
struct InputHostTargetWriteGuard {bool admit(const json&,const json&){return true;}void commit(){}};
static bool sgWriteFileAtomic(const std::wstring&,const std::string&);
static HANDLE WINAPI fixtureMutex(LPSECURITY_ATTRIBUTES security,BOOL owner,LPCWSTR){return CreateMutexW(security,owner,isolatedMutexName().c_str());}
#define CreateMutexW fixtureMutex
static std::function<void(HANDLE)> inputFlush;
static std::function<bool(const wchar_t*)> inputMove;
static std::function<void(const wchar_t*)> inputReplace;
static BOOL WINAPI inputFixtureFlush(HANDLE h){const auto result=::FlushFileBuffers(h);const auto saved=::GetLastError();if(inputFlush)inputFlush(h);::SetLastError(saved);return result;}
static BOOL WINAPI inputFixtureMove(LPCWSTR from,LPCWSTR to,DWORD flags){if(inputMove&&inputMove(to)){::SetLastError(ERROR_SHARING_VIOLATION);return FALSE;}return ::MoveFileExW(from,to,flags);}
static BOOL WINAPI inputFixtureReplace(LPCWSTR to,LPCWSTR from,LPCWSTR backup,DWORD flags,LPVOID exclude,LPVOID reserved){if(inputReplace)inputReplace(to);return ::ReplaceFileW(to,from,backup,flags,exclude,reserved);}
#define FlushFileBuffers inputFixtureFlush
#define MoveFileExW inputFixtureMove
#define ReplaceFileW inputFixtureReplace
// SETTINGS_BASELINE_PRODUCTION_IO
#undef ReplaceFileW
#undef MoveFileExW
#undef FlushFileBuffers
#if !defined(YMCC_SETTINGS_SECTION_CACHE_SCOPE)
static std::map<std::string,std::function<json(const json&)>> chargeHandlers;
static void ipc_on(const std::string& name,std::function<json(const json&)> handler){chargeHandlers[name]=std::move(handler);}
// SETTINGS_BASELINE_CHARGE_REGISTRATION
// SETTINGS_BASELINE_DISPLAY_MEMBERS
static DEVMODEW fixtureDisplayCurrent{};
static std::vector<DisplayModeInfo> fixtureDisplayModes;
static LONG fixtureDisplayTestCode=DISP_CHANGE_SUCCESSFUL,fixtureDisplayApplyCode=DISP_CHANGE_SUCCESSFUL;
static unsigned fixtureDisplayTests=0,fixtureDisplayApplies=0;
static bool fixtureDisplayReadback=true,fixtureDisplayMismatch=false;
static std::function<void()> fixtureDisplayBeforeReturn;
static std::function<void(const char*)> fixtureDisplayBoundary;
static std::vector<DisplayModeInfo> enumerateCurrentDisplayModes(std::wstring* device=nullptr,DEVMODEW* current=nullptr){
 if(device)*device=L"Owned-Fake-Monitor";if(current)*current=fixtureDisplayCurrent;if(fixtureDisplayBoundary)fixtureDisplayBoundary("enumerate");return fixtureDisplayModes;
}
static LONG fixtureChangeDisplaySettingsExW(LPCWSTR device,DEVMODEW* value,HWND,DWORD flags,LPVOID){
 if(!device||std::wstring(device)!=L"Owned-Fake-Monitor")throw std::runtime_error("display fixture escaped its owned adapter");
 if(flags==CDS_TEST){++fixtureDisplayTests;if(fixtureDisplayBoundary)fixtureDisplayBoundary("test");return fixtureDisplayTestCode;}
 ++fixtureDisplayApplies;if(fixtureDisplayApplyCode==DISP_CHANGE_SUCCESSFUL)fixtureDisplayCurrent=*value;
 if(fixtureDisplayBeforeReturn)fixtureDisplayBeforeReturn();if(fixtureDisplayBoundary)fixtureDisplayBoundary("apply");return fixtureDisplayApplyCode;
}
static BOOL fixtureEnumDisplaySettingsExW(LPCWSTR device,DWORD mode,DEVMODEW* value,DWORD){
 if(!device||std::wstring(device)!=L"Owned-Fake-Monitor"||mode!=ENUM_CURRENT_SETTINGS)throw std::runtime_error("display readback did not use the original adapter");
 if(!fixtureDisplayReadback)return FALSE;*value=fixtureDisplayCurrent;if(fixtureDisplayMismatch)value->dmPelsWidth+=16;if(fixtureDisplayBoundary)fixtureDisplayBoundary("readback");return TRUE;
}
// SETTINGS_BASELINE_DISPLAY_REMEMBER
#define ChangeDisplaySettingsExW fixtureChangeDisplaySettingsExW
#define EnumDisplaySettingsExW fixtureEnumDisplaySettingsExW
// SETTINGS_BASELINE_DISPLAY_REGISTRATION
#undef EnumDisplaySettingsExW
#undef ChangeDisplaySettingsExW
// SETTINGS_BASELINE_BRIGHTNESS_GUIDS
static HWND g_hwnd=reinterpret_cast<HWND>(1);
static bool fixtureHardwareAllowed=true,fixtureSchemeActive=true,fixtureLeaseAllowed=true;
static DWORD fixtureBrightnessValue=50,fixtureBrightnessReadCode=ERROR_SUCCESS,fixtureBrightnessWriteCode=ERROR_SUCCESS,fixtureBrightnessApplyCode=ERROR_SUCCESS;
static unsigned fixtureBrightnessReads=0,fixtureBrightnessWrites=0,fixtureBrightnessApplies=0;
static bool fixtureBrightnessMismatch=false;
static json fixtureBrightnessEvents=json::array();
static bool hardwareWriteAllowed(){return fixtureHardwareAllowed;}
struct HardwareWriteLease{explicit HardwareWriteLease(const char*){if(!fixtureLeaseAllowed)throw std::runtime_error("fixture owner retiring");}};
static DWORD PowerGetActiveScheme(HKEY,GUID** value){*value=static_cast<GUID*>(::LocalAlloc(LMEM_FIXED,sizeof(GUID)));if(!*value)return ERROR_OUTOFMEMORY;**value=fixtureSchemeActive?YM_GUID_YEMAN_SCHEME:GUID{};return ERROR_SUCCESS;}
static DWORD PowerReadACValueIndex(HKEY,const GUID*,const GUID*,const GUID*,DWORD* value){++fixtureBrightnessReads;*value=fixtureBrightnessValue+(fixtureBrightnessMismatch&&fixtureBrightnessReads>1?1:0);return fixtureBrightnessReadCode;}
static DWORD PowerReadDCValueIndex(HKEY a,const GUID* b,const GUID* c,const GUID* d,DWORD* value){return PowerReadACValueIndex(a,b,c,d,value);}
static DWORD PowerWriteACValueIndex(HKEY,const GUID*,const GUID*,const GUID*,DWORD value){++fixtureBrightnessWrites;if(fixtureBrightnessWriteCode==ERROR_SUCCESS)fixtureBrightnessValue=value;return fixtureBrightnessWriteCode;}
static DWORD PowerWriteDCValueIndex(HKEY a,const GUID* b,const GUID* c,const GUID* d,DWORD value){return PowerWriteACValueIndex(a,b,c,d,value);}
static DWORD PowerSetActiveScheme(HKEY,const GUID*){++fixtureBrightnessApplies;return fixtureBrightnessApplyCode;}
static void gamepadSerialPostEvent(const char* event,const json& value){fixtureBrightnessEvents.push_back({{"event",event},{"value",value}});}
// SETTINGS_BASELINE_BRIGHTNESS_BODY
static json fixtureShortcutEvents=json::array();
static unsigned long long currentPowerGeneration(){return 0;}
static void ipc_emit(const char* event,const json& value){fixtureShortcutEvents.push_back({{"event",event},{"value",value}});}
#include "shortcut_feedback_native.h"
// The original cases retain their two-argument port. The production reserve
// still performs the real four-argument document/host admission and pair commit.
static bool deckySidebarReserveContextCleanup(const ymcc::steamsession::Session& session,const std::function<bool()>& current){
 return deckySidebarReserveContextCleanup(session,"settings-baseline-cleanup","42:123456",current);
}
#endif
#undef CreateMutexW

#if defined(YMCC_SETTINGS_SECTION_CACHE_SCOPE)
// Explicit-only extension of this original Win32 consumer; no memory stub is
// called here. The prepare step inserts the real helper/getter in this namespace.
struct SectionCacheHandle {
 HANDLE value=INVALID_HANDLE_VALUE;
 explicit SectionCacheHandle(HANDLE handle):value(handle){}
 ~SectionCacheHandle(){close();}
 void close(){if(value!=INVALID_HANDLE_VALUE){CloseHandle(value);value=INVALID_HANDLE_VALUE;}}
 SectionCacheHandle(const SectionCacheHandle&)=delete;
 SectionCacheHandle& operator=(const SectionCacheHandle&)=delete;
};
static int runSectionCacheScope(int argc,char** argv) {
 try {
  if(argc!=4)throw std::runtime_error("section-cache requires defaults, scope and a fresh owned root");
  std::ifstream defaultsInput(argv[1]);if(!defaultsInput)throw std::runtime_error("original defaults input unavailable");
  const fspath::path owned=argv[3];
  if(fspath::exists(owned)||!fspath::create_directory(owned))throw std::runtime_error("section-cache root must be fresh");
  YM_SETTINGS_FILE=(owned/L"yeman-settings.json").wstring();YM_SETTINGS_BACKUP_FILE=YM_SETTINGS_FILE+L".bak";
  int assertions=0;json records=json::array();
  const auto need=[&](bool condition,const char* message){++assertions;if(!condition)throw std::runtime_error(message);};
  const auto put=[&](const std::wstring& file,const std::string& raw){
   std::ofstream output(file,std::ios::binary|std::ios::trunc);output.write(raw.data(),static_cast<std::streamsize>(raw.size()));output.close();
   need(!output.fail(),"section-cache owned file write failed");
  };
  const auto doc=[](const char* theme){return json{{"schemaVersion",1},{"ui",{{"theme",theme},{"nested",{{"keep",7}}}}},{"music",{{"volume",0.25}}}};};
  const auto seed=[&](const json& main,const json& backup){put(YM_SETTINGS_FILE,main.dump());put(YM_SETTINGS_BACKUP_FILE,backup.dump());};
  const auto check=[&](const char* name,const std::function<void()>& body){
   const int before=assertions;const auto parsed=sectionCacheParserCalls;const auto started=::GetTickCount64();body();
   records.push_back({{"name",name},{"assertions",assertions-before},{"parserCalls",sectionCacheParserCalls-parsed},{"elapsedMs",::GetTickCount64()-started},{"pass",true}});
  };
  check("exact raw hit reuses the real parser result",[&]{
   const auto main=doc("exact-raw");seed(main,doc("unused-backup"));
   const auto parsed=sectionCacheParserCalls;
   auto expected=ymcc::settingsBaseline::parseDocument(main.dump());ymcc::controllerPersona::migrateSavedDocument(expected);
   need(ymSettingsSection("ui")==expected["ui"],"first section differs from actual parse/support/migration result");
   need(sectionCacheParserCalls==parsed+1,"initial main must call the actual parser once");
   for(unsigned repeat=0;repeat<4;++repeat){
    need(ymSettingsSection("ui")==expected["ui"],"identical raw changes the section");
    need(ymSettingsSection("music")==expected["music"],"another section cannot share the parsed document");
   }
   need(sectionCacheParserCalls==parsed+1,"raw hit reparses instead of reusing the real parser result");
  });
  check("same size and file times still detect different raw",[&]{
   const auto first=doc("alpha"),next=doc("bravo");const auto firstRaw=first.dump(),nextRaw=next.dump();
   need(firstRaw.size()==nextRaw.size(),"same-size setup differs");seed(first,doc("metadata-backup"));
   need(ymSettingsSection("ui")==first["ui"],"metadata warmup failed");
   WIN32_FILE_ATTRIBUTE_DATA before{},after{};
   need(GetFileAttributesExW(YM_SETTINGS_FILE.c_str(),GetFileExInfoStandard,&before)!=FALSE,"original file metadata unavailable");
   put(YM_SETTINGS_FILE,nextRaw);
   {
    SectionCacheHandle handle(CreateFileW(YM_SETTINGS_FILE.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr));
    need(handle.value!=INVALID_HANDLE_VALUE,"timestamp restore handle unavailable");
    need(SetFileTime(handle.value,&before.ftCreationTime,nullptr,&before.ftLastWriteTime)!=FALSE,"cannot restore file times");
   }
   need(GetFileAttributesExW(YM_SETTINGS_FILE.c_str(),GetFileExInfoStandard,&after)!=FALSE,"rewritten file metadata unavailable");
   need(before.nFileSizeHigh==after.nFileSizeHigh&&before.nFileSizeLow==after.nFileSizeLow&&CompareFileTime(&before.ftCreationTime,&after.ftCreationTime)==0&&CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)==0,"external rewrite did not preserve size/creation/mtime");
   const auto parsed=sectionCacheParserCalls;
   need(ymSettingsSection("ui")==next["ui"],"same-metadata different raw returned stale UI");
   need(sectionCacheParserCalls==parsed+1,"different raw did not reach the actual parser");
  });
  check("source path main corruption backup changes and main recovery",[&]{
   const auto same=doc("source-switch");seed(same,same);need(ymSettingsSection("ui")==same["ui"],"source warmup failed");
   need(fspath::remove(YM_SETTINGS_FILE),"cannot remove owned main");auto parsed=sectionCacheParserCalls;
   need(ymSettingsSection("ui")==same["ui"],"missing main did not use identical-byte backup");
   need(sectionCacheParserCalls==parsed+1,"cache key omitted source path");
   put(YM_SETTINGS_FILE,"{corrupt-primary");const auto backupOne=doc("backup-one");put(YM_SETTINGS_BACKUP_FILE,backupOne.dump());
   need(ymSettingsSection("ui")==backupOne["ui"],"corrupt main did not select current backup");
   const auto backupTwo=doc("backup-two");put(YM_SETTINGS_BACKUP_FILE,backupTwo.dump());
   need(ymSettingsSection("ui")==backupTwo["ui"],"backup change reused older backup");
   put(YM_SETTINGS_FILE,backupTwo.dump());parsed=sectionCacheParserCalls;
   need(ymSettingsSection("ui")==backupTwo["ui"],"repaired main did not take priority");
   need(sectionCacheParserCalls==parsed+1,"backup-to-main same-byte recovery omitted source path");
  });
  check("both bad unsupported or absent never continue a healthy value",[&]{
   const auto main=doc("both-bad-warm");seed(main,doc("old-backup"));need(ymSettingsSection("ui")==main["ui"],"bad-source warmup failed");
   put(YM_SETTINGS_FILE,"{bad-main");put(YM_SETTINGS_BACKUP_FILE,"{bad-backup");
   need(ymSettingsSection("ui")==json::object(),"both corrupt sources continued old healthy UI");
   need(ymSettingsReadRaw(YM_SETTINGS_FILE)=="{bad-main"&&ymSettingsReadRaw(YM_SETTINGS_BACKUP_FILE)=="{bad-backup","read fallback mutated bad originals");
   auto future=doc("future");future["schemaVersion"]=99;put(YM_SETTINGS_FILE,future.dump());put(YM_SETTINGS_BACKUP_FILE,"[]");
   need(ymSettingsSection("ui")==json::object(),"unsupported main or nonobject backup became an active cached document");
   fspath::remove(YM_SETTINGS_FILE);fspath::remove(YM_SETTINGS_BACKUP_FILE);
   need(ymSettingsSection("ui")==json::object(),"two missing sources continued old healthy UI");
   need(!fspath::exists(YM_SETTINGS_FILE)&&!fspath::exists(YM_SETTINGS_BACKUP_FILE),"default read created persistent files");
  });
  check("real sharing read faults fall back and heal",[&]{
   const auto main=doc("sharing-main"),backup=doc("sharing-backup");seed(main,backup);
   need(ymSettingsSection("ui")==main["ui"],"sharing warmup failed");
   SectionCacheHandle heldMain(CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr));
   need(heldMain.value!=INVALID_HANDLE_VALUE,"main exclusive read setup failed");
   need(ymSettingsSection("ui")==backup["ui"],"main sharing failure bypassed current backup");
   SectionCacheHandle heldBackup(CreateFileW(YM_SETTINGS_BACKUP_FILE.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr));
   need(heldBackup.value!=INVALID_HANDLE_VALUE,"backup exclusive read setup failed");
   need(ymSettingsSection("ui")==json::object(),"two sharing faults continued cached backup");
   heldBackup.close();heldMain.close();const auto recovered=doc("sharing-healed");put(YM_SETTINGS_FILE,recovered.dump());
   need(ymSettingsSection("ui")==recovered["ui"],"next explicit read did not heal after sharing faults");
  });
  check("failed actual atomic commit cannot publish draft UI",[&]{
   const auto main=doc("commit-before");seed(main,doc("commit-backup"));const auto raw=ymSettingsReadRaw(YM_SETTINGS_FILE);
   need(ymSettingsSection("ui")==main["ui"],"commit warmup failed");const json draft={{"theme","uncommitted"}};
   SectionCacheHandle heldMain(CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
   need(heldMain.value!=INVALID_HANDLE_VALUE,"atomic replacement denial setup failed");
   need(ymSettingsReadRaw(YM_SETTINGS_FILE)==raw,"replacement denial accidentally blocked the strict raw read");
   need(!ymSettingsWriteSection("ui",draft),"denied real replacement reported success");
   need(ymSettingsReadRaw(YM_SETTINGS_FILE)==raw,"failed atomic replacement changed the main bytes");
   need(ymSettingsSection("ui")==main["ui"],"failed commit poisoned section cache with draft");
   heldMain.close();need(ymSettingsWriteSection("ui",draft),"replacement did not heal after handle release");
   need(ymSettingsSection("ui")==draft,"successful later commit left the old cached section");
  });
  check("returned objects are detached and section shape stays local",[&]{
   const auto main=doc("detached");seed(main,doc("shape-backup"));auto returned=ymSettingsSection("ui");
   need(returned==main["ui"],"detach warmup failed");const auto parsed=sectionCacheParserCalls;
   returned["theme"]="caller-edit";returned["nested"]["keep"]=99;
   need(ymSettingsSection("ui")==main["ui"],"returned object mutation escaped into cached DOM");
   need(ymSettingsSection("missing")==json::object(),"absent section is not empty");
   need(sectionCacheParserCalls==parsed,"detached or missing-section read reparsed unchanged raw");
   auto scalar=doc("unused");scalar["ui"]=17;put(YM_SETTINGS_FILE,scalar.dump());
   need(ymSettingsSection("ui")==json::object(),"scalar section incorrectly fell back to backup UI");
   need(ymSettingsSection("music")==scalar["music"],"scalar UI invalidated unrelated valid section");
  });
  check("original local and named lock failures remain bounded",[&]{
   const auto main=doc("lock-bounds");seed(main,doc("lock-backup"));need(ymSettingsSection("ui")==main["ui"],"lock warmup failed");
   for(const bool named:{false,true}){
    std::promise<bool> ready;std::promise<void> release;auto released=release.get_future();
    std::thread holder([&]{
     if(named){
      SectionCacheHandle mutex(::CreateMutexW(nullptr,FALSE,isolatedMutexName().c_str()));
      const DWORD wait=mutex.value!=INVALID_HANDLE_VALUE?WaitForSingleObject(mutex.value,2000):WAIT_FAILED;
      const bool held=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;ready.set_value(held);
      released.wait_for(std::chrono::seconds(8));if(held)ReleaseMutex(mutex.value);
     }else{std::lock_guard<std::timed_mutex> held(g_settingsFileMtx);ready.set_value(true);released.wait_for(std::chrono::seconds(8));}
    });
    const bool held=ready.get_future().get();const auto parsed=sectionCacheParserCalls;json value;bool threw=false;
    const auto started=::GetTickCount64();try{value=ymSettingsSection("ui");}catch(...){threw=true;}
    const auto elapsed=::GetTickCount64()-started;release.set_value();holder.join();
    need(held,"original settings lock holder failed");need(!threw&&value==json::object(),"lock failure threw or continued old cached UI");
    need(elapsed>=1500&&elapsed<3500,"section bypassed the original bounded two-second lock");
    need(sectionCacheParserCalls==parsed,"failed lock reached the cache/parser");
    need(ymSettingsSection("ui")==main["ui"],"section did not recover after original lock release");
   }
  });
  need(records.size()==8,"section-cache scope must stay at eight bounded scenarios");
  for(const auto& entry:fspath::directory_iterator(owned)){
   need(entry.is_regular_file(),"unexpected child in section-cache owned root");
   need(fspath::remove(entry.path()),"owned section-cache file cleanup failed");
  }
  need(fspath::remove(owned),"owned section-cache directory cleanup failed");
  std::cout<<json{{"status","PASS"},{"scope","section-cache-real-win32-io"},{"consumerNamespace","settingsIoFixture"},{"actualProductionSection",true},{"memorySectionStubUsed",false},{"cases",records.size()},{"caseRecords",records},{"assertions",assertions},{"parserCalls",sectionCacheParserCalls},{"ownedIoRemoved",!fspath::exists(owned)},{"installedSettingsWritten",false},{"hardwareValidated",false}}.dump()<<'\n';
  return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL section-cache "<<error.what()<<'\n';return 1;}
}
#endif
#if !defined(YMCC_SETTINGS_SECTION_CACHE_SCOPE)
// One original consumer block, shared by the default IO route and exact scope.
static json runCleanupHistory(const json& defaults, const fspath::path& root, const std::string& selected = {}) {
 int assertions=0;json cases=json::array();
 const auto need=[&](bool value,const char* why){++assertions;if(!value)throw std::runtime_error(why);};
 const auto put=[&](const std::wstring& path,const std::string& bytes){std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(bytes.data(),bytes.size());need(output.good(),"owned cleanup-history seed write failed");};
 const auto write=[&](const std::string& bytes){put(YM_SETTINGS_FILE,bytes);};
 const auto backupWrite=[&](const std::string& bytes){put(YM_SETTINGS_BACKUP_FILE,bytes);};
 const auto rawFile=[](const std::wstring& path){std::ifstream input(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});};
 const auto seed=[&]{write(defaults.dump());backupWrite(defaults.dump());};
 const auto check=[&](const std::string& name,const std::function<void()>& body){if(!selected.empty()&&selected!=name)return;const auto start=assertions;body();cases.push_back({{"name",name},{"assertions",assertions-start},{"pass",true}});};
 check("cleanup pair-commit fallback and duplicate",[&]{
 {
  ymcc::steamsession::Session session;session.root=L"C:\\FixtureSteam";session.pid=41;session.created=123456;session.account=9;
  write(defaults.dump());backupWrite(defaults.dump());
  need(deckySidebarReserveContextCleanup(session,[]{return true;}),"actual Win32 pair-commit rejected a fresh current cleanup");
  const auto firstMain=rawFile(YM_SETTINGS_FILE),firstBackup=rawFile(YM_SETTINGS_BACKUP_FILE);
  write("{broken-after-final-cleanup");
  auto recovered=ymSettingsReadUnlocked();
  need(!ymcc::deckyrepair::cleanupAvailable(recovered.value("deckyContextCleanup",json()),session),"actual Win32 main corruption and fallback refilled final-cleanup fuse");
  auto business=recovered;business.erase("deckyContextCleanup");need(business==defaults,"actual Win32 pair commit modified backed-up business settings");
  write(firstMain);need(!deckySidebarReserveContextCleanup(session,[]{return true;})&&rawFile(YM_SETTINGS_FILE)==firstMain&&rawFile(YM_SETTINGS_BACKUP_FILE)==firstBackup,"actual Win32 duplicate reservation changed durable receipts");
  seed();
  need(deckySidebarReserveContextCleanup(session,"settings-baseline-cleanup","42:123456",[]{return true;},true),"actual Win32 Big Picture reservation refused");
  const auto bigPictureMain=ymSettingsReadUnlocked();
  need(bigPictureMain.contains("deckyBigPictureRecovery")&&!bigPictureMain.contains("deckyContextCleanup"),"Big Picture reservation touched lower-tier budget");
  auto stale=defaults;stale["music"]["volume"]=0.37;
  need(ymSettingsWriteDocument(stale.dump(),nullptr,&defaults),"stale ordinary save refused");
  need(ymSettingsReadUnlocked()["deckyBigPictureRecovery"]==bigPictureMain["deckyBigPictureRecovery"],"stale save erased Big Picture reservation");
  write("{broken-after-bigpicture");
  recovered=ymSettingsReadUnlocked();
  need(recovered["deckyBigPictureRecovery"]==bigPictureMain["deckyBigPictureRecovery"],"backup recovery refunded Big Picture reservation");
  need(!deckySidebarReserveContextCleanup(session,"settings-baseline-cleanup","42:123456",[]{return true;},true),"Big Picture operation replayed after corruption");
 }
 {
  // Extension of this ONE original case: original tests above remain unchanged.
  // The real Win32 settings pair calls the exact extracted production reserve.
  // Only installation authority/storage and digest are declared memory ports.
  namespace upgrade=ymcc::deckyupgrade;
  upgrade::runningstore::FixtureLease installPortScope;
  auto& installPort=upgrade::runningstore::fixture;
  need(!installPort.enabled,"upgrade fixture port must default to refusing");
  wchar_t image[32768]{};const DWORD imageLength=GetModuleFileNameW(nullptr,image,32768);
  need(imageLength>0&&imageLength<32768,"owned fixture executable identity unavailable");
  installPort.expectedExe.assign(image,imageLength);
  installPort.enabled=true;
  // First verified marked running B initializes the migration boundary.
  // These are declared authority inputs, not claims about this fixture PE.
  installPort.currentBuild={std::string(64,'b'),std::string(64,'2'),APP_VER_STR};
  installPort.digestValue=installPort.currentBuild.sha256;
  ymcc::steamsession::Session same;same.root=L"C:\\FixtureSteam";same.pid=41;same.created=123456;same.account=9;
  const auto steam=ymcc::deckyupdate::ticket(same);
  const auto oldPage=ymcc::deckyupdate::managed::record(same,"older-owned-document","55:7654321",
      std::string(64,'a'),ymcc::deckyidentity::version,"reserved");
  const json oldBpm={{"schemaVersion",1},{"steamSession",steam}};
  need(!oldPage.is_null(),"original managed v2 fixture receipt invalid");
  auto spentSettings=defaults;spentSettings["deckyContextCleanup"]=oldPage;spentSettings["deckyBigPictureRecovery"]=oldBpm;
  const auto seedSpent=[&]{write(spentSettings.dump());backupWrite(spentSettings.dump());};
  seedSpent();
  const auto oldMain=rawFile(YM_SETTINGS_FILE),oldBackup=rawFile(YM_SETTINGS_BACKUP_FILE);
  need(!ymcc::deckyrepair::cleanupDocumentAvailable(spentSettings,same,"settings-baseline-cleanup")&&
       !ymcc::deckyrepair::cleanupDocumentAvailable(spentSettings,same,{},"deckyBigPictureRecovery"),
       "new-build control did not start with BOTH ordinary quotas spent");
  need(deckySidebarReserveContextCleanup(same,[]{return true;}),
       "new B could not supplement spent page quota through actual settings pair");
  auto pageMain=ymSettingsReadUnlocked();
  auto pageBackup=ymcc::settingsBaseline::parseDocument(rawFile(YM_SETTINGS_BACKUP_FILE));
  need(pageMain.at("deckyContextCleanup")==pageBackup.at("deckyContextCleanup"),
       "extra page receipt was not read back from both real settings copies");
  const auto pageReceipt=pageMain.at("deckyContextCleanup");
  auto expectedPage=spentSettings;expectedPage["deckyContextCleanup"]=pageReceipt;
  need(pageMain==expectedPage&&pageBackup==expectedPage,"extra page changed unrelated real settings");
  need(upgrade::running::valid(installPort.history)&&installPort.commits==1&&installPort.digestReads==1,
       "new B did not consume one independent pure-policy page slot");
  const auto& bGrant=installPort.history.at("builds").at(installPort.currentBuild.id);
  need(bGrant.at("page").at("priorLedger")==oldPage&&
       bGrant.at("page").at("reservedLedger")==pageReceipt,
       "extra page lost the two exact original v2 ledger images");
  need(deckySidebarReserveContextCleanup(same,"settings-baseline-cleanup","42:123456",[]{return true;},true),
       "new B could not supplement independently spent BPM quota");
  auto bothMain=ymSettingsReadUnlocked();
  auto bothBackup=ymcc::settingsBaseline::parseDocument(rawFile(YM_SETTINGS_BACKUP_FILE));
  need(bothMain==bothBackup,"extra page/BPM commit left real settings copies unequal");
  const auto savedBHistory=installPort.history;
  const auto& bothGrant=savedBHistory.at("builds").at(installPort.currentBuild.id);
  need(bothGrant.at("bpm").at("priorLedger")==oldBpm&&
       bothGrant.at("bpm").at("reservedLedger")==bothMain.at("deckyBigPictureRecovery")&&
       bothGrant.at("bpm").at("reservedLedger").size()==2&&
       bothGrant.at("page").at("reservedLedger").size()==7,
       "extra BPM archive changed the original two/seven-field receipt contracts");
  need(installPort.commits==2,"new B consumed other than one page and one BPM extra slot");
  const auto beforeRepeatMain=rawFile(YM_SETTINGS_FILE),beforeRepeatBackup=rawFile(YM_SETTINGS_BACKUP_FILE);
  need(!deckySidebarReserveContextCleanup(same,[]{return true;})&&
       !deckySidebarReserveContextCleanup(same,"settings-baseline-cleanup","42:123456",[]{return true;},true),
       "same B replayed extra page/BPM");
  need(installPort.history==savedBHistory&&installPort.commits==2&&
       rawFile(YM_SETTINGS_FILE)==beforeRepeatMain&&rawFile(YM_SETTINGS_BACKUP_FILE)==beforeRepeatBackup,
       "duplicate refusal changed independent history or real settings");

  // Coherently restore BOTH old real settings files; the declared installation
  // history is deliberately independent and must retain B's consumed slots.
  write(oldMain);backupWrite(oldBackup);
  need(!deckySidebarReserveContextCleanup(same,[]{return true;})&&
       !deckySidebarReserveContextCleanup(same,"settings-baseline-cleanup","42:123456",[]{return true;},true),
       "rolling back both settings files refunded the independent extra slots");
  need(installPort.history==savedBHistory&&installPort.commits==2&&
       rawFile(YM_SETTINGS_FILE)==oldMain&&rawFile(YM_SETTINGS_BACKUP_FILE)==oldBackup,
       "settings rollback refusal mutated pure history or old settings");

  // A distinct verified marked running C has fresh quota at the same test release.
  // Fail the ACTUAL primary
  // atomic replacement after the independent pure-policy reservation commits.
  installPort.currentBuild={std::string(64,'c'),std::string(64,'3'),APP_VER_STR};
  installPort.digestValue=installPort.currentBuild.sha256;
  seedSpent();
  const auto lockedMain=rawFile(YM_SETTINGS_FILE);
  {
   struct NoDeleteSharingLease {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~NoDeleteSharingLease(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
   } blocked;
   blocked.value=CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
                             nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
   need(blocked.value!=INVALID_HANDLE_VALUE,"could not hold primary without FILE_SHARE_DELETE");
   need(!deckySidebarReserveContextCleanup(same,[]{return true;}),
        "real primary replacement succeeded despite deny-delete sharing");
   need(rawFile(YM_SETTINGS_FILE)==lockedMain,"sharing failure replaced primary bytes");
  }
  need(installPort.commits==3&&upgrade::running::valid(installPort.history),
       "failed settings replacement did not retain the already reserved extra C page");
  const auto failedSettingsHistory=installPort.history;
  need(!failedSettingsHistory.at("builds").at(installPort.currentBuild.id).at("page").is_null(),
       "real settings commit failure refunded the memory installation slot");
  seedSpent();
  need(!deckySidebarReserveContextCleanup(same,[]{return true;})&&installPort.history==failedSettingsHistory&&
       installPort.commits==3,"retry after releasing primary handle refunded an uncertain extra reservation");

  // Observe the next verified marked running build before any failure, as the
  // parent startup does independently of Steam/fault. D slots remain unused.
  installPort.currentBuild={std::string(64,'d'),std::string(64,'4'),APP_VER_STR};
  installPort.digestValue=installPort.currentBuild.sha256;
  installPort.history=upgrade::running::observe(installPort.history,installPort.currentBuild,installPort.normalizedExe);
  need(upgrade::running::valid(installPort.history),"healthy marked running D was not observed before failure");
  const auto knownDHistory=installPort.history;
  for(bool reopen:{false,true}){
   const char* key=reopen?"deckyBigPictureRecovery":"deckyContextCleanup";
   auto unknown=spentSettings;unknown[key]=ymcc::settingsBaseline::cleanupHistoryUnconfirmed();
   write(unknown.dump());backupWrite(unknown.dump());
   const auto unknownMain=rawFile(YM_SETTINGS_FILE),unknownBackup=rawFile(YM_SETTINGS_BACKUP_FILE);
   const auto beforeUnknownCommits=installPort.commits;
   // Unknown history alone never supplies the separate verified-native grant.
   // The acknowledged new-build positive is exercised below with authority ON.
   installPort.enabled=false;
   need(!deckySidebarReserveContextCleanup(same,"settings-baseline-cleanup","42:123456",[]{return true;},reopen),
        "unknown equal primary/backup ledgers acquired extra recovery");
   need(installPort.history==knownDHistory&&installPort.commits==beforeUnknownCommits&&
        rawFile(YM_SETTINGS_FILE)==unknownMain&&rawFile(YM_SETTINGS_BACKUP_FILE)==unknownBackup,
        "unknown-history refusal changed original settings or consumed pure quota");
   installPort.enabled=true;

   auto left=spentSettings,right=spentSettings;
   if(reopen)right[key]["steamSession"]="99:999999";
   else right[key]["document"]="conflicting-owned-document";
   write(left.dump());backupWrite(right.dump());
   const auto conflictMain=rawFile(YM_SETTINGS_FILE),conflictBackup=rawFile(YM_SETTINGS_BACKUP_FILE);
   const auto beforeConflictCalls=installPort.calls;
   need(!deckySidebarReserveContextCleanup(same,"settings-baseline-cleanup","42:123456",[]{return true;},reopen),
        "conflicting old settings copies acquired extra recovery");
   need(installPort.calls==beforeConflictCalls&&installPort.history==knownDHistory&&
        rawFile(YM_SETTINGS_FILE)==conflictMain&&rawFile(YM_SETTINGS_BACKUP_FILE)==conflictBackup,
        "old-ledger mismatch crossed the actual pre-store equality gate");
  }
  // Pure reserve takes history by value: probe both unspent slots without consuming.
  need(!upgrade::running::reserve(installPort.history,installPort.currentBuild,installPort.normalizedExe,false,steam,oldPage,pageReceipt).is_null()&&
       !upgrade::running::reserve(installPort.history,installPort.currentBuild,installPort.normalizedExe,true,steam,oldBpm,oldBpm).is_null(),
       "unknown/mismatched settings poisoned unrelated unspent D quota");
  // Exact field sentinel: a positively verified new native build has an
  // independent, already authorized slot. Archive the unknown bytes rather than
  // pretending its old operation completed or refunding ordinary cleanup.
  const auto sentinel=ymcc::settingsBaseline::cleanupHistoryUnconfirmed();
  auto unknownPair=spentSettings;unknownPair["deckyContextCleanup"]=sentinel;unknownPair["deckyBigPictureRecovery"]=sentinel;
  const auto seedUnknown=[&]{write(unknownPair.dump());backupWrite(unknownPair.dump());};
  for(bool reopen:{false,true})for(unsigned bad=0;bad<7;++bad){
   const char* key=reopen?"deckyBigPictureRecovery":"deckyContextCleanup";auto invalid=sentinel;
   if(bad==0)invalid["extra"]=true;if(bad==1)invalid["schemaVersion"]=1.0;if(bad==2)invalid["schemaVersion"]=2;
   if(bad==3)invalid["history"]="other";if(bad==4)invalid["history"]=nullptr;if(bad==5)invalid=nullptr;if(bad==6)invalid.erase("schemaVersion");
   auto malformed=unknownPair;malformed[key]=invalid;write(malformed.dump());backupWrite(malformed.dump());
   const auto originalMain=rawFile(YM_SETTINGS_FILE),originalBackup=rawFile(YM_SETTINGS_BACKUP_FILE);const auto commits=installPort.commits;
   need(!deckySidebarReserveContextCleanup(same,"settings-baseline-cleanup","42:123456",[]{return true;},reopen),"malformed unknown predecessor acquired a verified-build slot");
   need(installPort.commits==commits&&installPort.history==knownDHistory&&rawFile(YM_SETTINGS_FILE)==originalMain&&rawFile(YM_SETTINGS_BACKUP_FILE)==originalBackup,"malformed refusal consumed the independent native quota or wrote settings");
  }
  seedUnknown();installPort.digestValue=std::string(64,'9');
  const auto wrongImageHistory=installPort.history;
  need(!deckySidebarReserveContextCleanup(same,[]{return true;})&&installPort.history==wrongImageHistory,"unknown history bypassed actual image-digest authority");
  installPort.digestValue=installPort.currentBuild.sha256;
  const auto commitsBeforeUnknown=installPort.commits;
  need(deckySidebarReserveContextCleanup(same,[]{return true;}),"verified new native build could not reserve page recovery after the exact unknown sentinel");
  auto unknownPageMain=ymSettingsReadUnlocked(),unknownPageBackup=ymcc::settingsBaseline::parseDocument(rawFile(YM_SETTINGS_BACKUP_FILE));
  auto unknownExpected=unknownPair;unknownExpected["deckyContextCleanup"]=unknownPageMain.at("deckyContextCleanup");
  need(unknownPageMain==unknownExpected&&unknownPageBackup==unknownExpected,"unknown page replacement changed business data or missed exact backup");
  need(deckySidebarReserveContextCleanup(same,"settings-baseline-cleanup","42:123456",[]{return true;},true),"verified new native build could not reserve BPM recovery after the exact unknown sentinel");
  const auto unknownReadyMain=ymSettingsReadUnlocked(),unknownReadyBackup=ymcc::settingsBaseline::parseDocument(rawFile(YM_SETTINGS_BACKUP_FILE));
  unknownExpected["deckyBigPictureRecovery"]=unknownReadyMain.at("deckyBigPictureRecovery");
  need(unknownReadyMain==unknownExpected&&unknownReadyBackup==unknownExpected,"unknown page/BPM pair altered unrelated settings");
  const auto consumedD=installPort.history;const auto& unknownD=consumedD.at("builds").at(installPort.currentBuild.id);
  need(upgrade::running::valid(consumedD)&&installPort.commits==commitsBeforeUnknown+2&&unknownD.at("steamTicket")==steam,"unknown predecessor did not consume exactly one native page and BPM slot");
  need(unknownD.at("page").at("priorLedger")==sentinel&&unknownD.at("bpm").at("priorLedger")==sentinel&&unknownD.at("page").at("reservedLedger")==unknownReadyMain.at("deckyContextCleanup")&&unknownD.at("bpm").at("reservedLedger")==unknownReadyMain.at("deckyBigPictureRecovery"),"independent build archive lost the exact unknown preimages or reservation pair");
  seedUnknown();
  need(!deckySidebarReserveContextCleanup(same,[]{return true;})&&!deckySidebarReserveContextCleanup(same,"settings-baseline-cleanup","42:123456",[]{return true;},true)&&installPort.history==consumedD,"restoring unknown settings refunded consumed native slots");
  // Same-build observation/reinstallation never refills either consumed slot.
  installPort.history=upgrade::running::observe(installPort.history,installPort.currentBuild,installPort.normalizedExe);
  need(installPort.history==consumedD&&!deckySidebarReserveContextCleanup(same,[]{return true;}),"same-build observation refilled unknown migration recovery");
  const auto buildD=installPort.currentBuild;
  installPort.currentBuild={std::string(64,'e'),std::string(64,'5'),APP_VER_STR};installPort.digestValue=installPort.currentBuild.sha256;
  installPort.history=upgrade::running::observe(installPort.history,installPort.currentBuild,installPort.normalizedExe);
  seedUnknown();const auto uncertainUnknownMain=rawFile(YM_SETTINGS_FILE);
  {
   struct UnknownNoDeleteLease{HANDLE value=INVALID_HANDLE_VALUE;~UnknownNoDeleteLease(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}} held;
   held.value=CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
   need(held.value!=INVALID_HANDLE_VALUE,"unknown primary replacement denial setup failed");
   need(!deckySidebarReserveContextCleanup(same,[]{return true;})&&rawFile(YM_SETTINGS_FILE)==uncertainUnknownMain,"unknown primary sharing failure reported a successful replacement");
  }
  const auto uncertainE=installPort.history;
  need(upgrade::running::valid(uncertainE)&&uncertainE.at("builds").at(installPort.currentBuild.id).at("page").at("priorLedger")==sentinel,"uncertain unknown replacement did not retain its consumed protected slot");
  seedUnknown();need(!deckySidebarReserveContextCleanup(same,[]{return true;})&&installPort.history==uncertainE,"release after unknown sharing failure replayed a consumed action");
  installPort.currentBuild=buildD;installPort.digestValue=buildD.sha256;
  installPort.history=upgrade::running::observe(installPort.history,buildD,installPort.normalizedExe);
  need(installPort.history.at("builds").at(buildD.id).at("retired")==true&&!deckySidebarReserveContextCleanup(same,[]{return true;})&&!deckySidebarReserveContextCleanup(same,"settings-baseline-cleanup","42:123456",[]{return true;},true),"E-to-D rollback revived unknown-migration recovery");
  seedSpent(); // Leave the real pair valid; scope restores default-refusing port.
 }
 });
 ymcc::steamsession::Session session;session.root=L"C:\FixtureSteam";session.pid=41;session.created=123456;session.account=9;
 const auto save=[&](unsigned route){if(route==1)return ymSettingsWriteSection("music",{{"volume",0.37}});if(route==2)return ymSettingsPatchSection("music",{{"volume",0.37}});if(route==3)return ymSettingsResetApplication(defaults).value("ok",false);const auto before=ymSettingsReadUnlocked();auto next=before;next["music"]["volume"]=0.37;return ymSettingsWriteDocument(next.dump(),nullptr,&before);};
 for(unsigned mode=0;mode<2;++mode)check(std::string("cleanup unknown recovery ")+(mode?"overlimit":"malformed"),[&]{
  seed();need(deckySidebarReserveContextCleanup(session,[]{return true;}),"original pair reservation failed");const std::string damaged=mode?std::string(4*1024*1024+1,'x'):"{broken-history";write(damaged);backupWrite(damaged);const json empty=json::object();json committed;
  need(ymSettingsWriteDocument(defaults.dump(),nullptr,&empty,&committed,true),"both-untrusted initialization refused");
  need(committed.contains("deckyContextCleanup")&&committed["deckyContextCleanup"].value("history",std::string{})=="unconfirmed","untrusted reconstruction lost explicit unknown history");
  need(committed.contains("deckyBigPictureRecovery")&&committed["deckyBigPictureRecovery"].value("history",std::string{})=="unconfirmed","untrusted reconstruction refilled Big Picture recovery");
  need(save(0),"unknown history blocked unrelated business save");need(!deckySidebarReserveContextCleanup(session,[]{return true;}),"untrusted reconstruction or ordinary save refunded cleanup");
  bool mainArchived=false,backupArchived=false;for(const auto& item:std::filesystem::directory_iterator(root)){const auto path=item.path().wstring();if(path.starts_with(YM_SETTINGS_FILE+L".before-")&&rawFile(path)==damaged)mainArchived=true;if(path.starts_with(YM_SETTINGS_BACKUP_FILE+L".before-")&&rawFile(path)==damaged)backupArchived=true;}
  need(mainArchived&&backupArchived,"unknown-history reconstruction did not preserve exact damaged originals");
 });
 for(unsigned route=0;route<4;++route)for(unsigned damage=0;damage<3;++damage)check(std::string("cleanup ordinary writer=")+std::to_string(route)+" damage="+std::to_string(damage),[&]{
  seed();need(deckySidebarReserveContextCleanup(session,[]{return true;}),"original pair reservation failed");auto primary=ymSettingsReadUnlocked();
  if(damage==0)primary.erase("deckyContextCleanup");else if(damage==1)primary["deckyContextCleanup"]=nullptr;else primary["deckyContextCleanup"]={{"schemaVersion",1},{"steamSession","99:999999"}};
  primary["cleanupHistorySibling"]={{"preserved",true}};write(primary.dump());need(save(route)&&save(0),"native history repair blocked an ordinary save");const auto after=ymSettingsReadUnlocked();need(after["cleanupHistorySibling"]["preserved"]==true,"history repair lost independent business settings");need(!deckySidebarReserveContextCleanup(session,[]{return true;}),"ordinary save or reset refunded spent cleanup");
 });
 check("cleanup absent first initialization remains fresh",[&]{
  std::filesystem::remove(YM_SETTINGS_FILE);std::filesystem::remove(YM_SETTINGS_BACKUP_FILE);const json empty=json::object();json committed;need(ymSettingsWriteDocument(defaults.dump(),nullptr,&empty,&committed,true),"absent first initialization refused");need(!committed.contains("deckyContextCleanup"),"genuine fresh initialization invented unknown history");need(deckySidebarReserveContextCleanup(session,[]{return true;}),"genuine fresh first reservation refused");
 });
 check("cleanup next verified Steam process remains available",[&]{seed();need(deckySidebarReserveContextCleanup(session,[]{return true;}),"first reservation refused");auto next=session;++next.created;need(deckySidebarReserveContextCleanup(next,[]{return true;}),"next verified Steam process inherited old spent allowance");});
 need(!cases.empty()&&(selected.empty()||cases.size()==1),"invalid cleanup-history exact selection");return {{"cases",cases},{"assertions",assertions}};
}
static int runCleanupHistoryScope(int argc,char** argv) {
 try{
  if(argc<4||argc>5)throw std::runtime_error("cleanup-history defaults, scope, owned root and optional exact case required");std::ifstream input(argv[1]);json defaults;input>>defaults;const fspath::path owned=argv[3];if(std::filesystem::exists(owned)||!std::filesystem::create_directory(owned))throw std::runtime_error("cleanup-history scope root must be fresh");
  SettingsCleanupFrontendFixture frontend;
  YM_SETTINGS_FILE=(owned/L"yeman-settings.json").wstring();YM_SETTINGS_BACKUP_FILE=YM_SETTINGS_FILE+L".bak";auto result=runCleanupHistory(defaults,owned,argc==5?argv[4]:"");frontend.close();
  for(const auto& entry:std::filesystem::directory_iterator(owned)){if(!entry.is_regular_file())throw std::runtime_error("unexpected cleanup-history owned child");std::filesystem::remove(entry.path());}std::filesystem::remove(owned);
  result["status"]="PASS";result["caseRecords"]=result["cases"];result["selection"]=argc==5?argv[4]:"all-cleanup-history";result["cases"]=result["caseRecords"].size();result["scope"]="original-cleanup-history-real-win32-io";result["installedSettingsWritten"]=false;result["hardwareValidated"]=false;result["ownedIoRemoved"]=!std::filesystem::exists(owned);std::cout<<result.dump()<<'\n';return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL cleanup-history "<<error.what()<<'\n';return 1;}
}
static int runUpgradeBoundaries(int argc,char** argv){
 json cases=json::array();int assertions=0;std::filesystem::path owned;
 const auto need=[&](bool ok,const char* message){++assertions;if(!ok)throw std::runtime_error(message);};
 try{
  need(argc==4||argc==5,"upgrade-boundaries requires defaults, exact scope, fresh root and optional case");
  const std::string selected=argc==5?argv[4]:"";
  const std::vector<std::string> names={"deep-extension","limit-extension","legacy-schema-key","near-size-limit","over-depth","defaults-invalid","archive-failure","busy-release","updater-state-guard"};
  need(selected.empty()||std::find(names.begin(),names.end(),selected)!=names.end(),"unknown upgrade-boundaries exact case");
  std::ifstream input(argv[1]);json defaults;input>>defaults;need(!input.fail(),"defaults fixture input");
  owned=std::filesystem::path(argv[3]);need(owned.is_absolute()&&!std::filesystem::exists(owned),"owned root must be fresh");
  need(std::filesystem::create_directory(owned),"owned root creation");
  const auto primary=(owned/L"settings.json").wstring(),backup=primary+L".bak",shipped=(owned/L"defaults.json").wstring();
  const auto put=[&](const std::wstring& file,const std::string& bytes){std::ofstream stream(file,std::ios::binary|std::ios::trunc);stream.write(bytes.data(),bytes.size());need(stream.good(),"owned fixture write");};
  const auto raw=[&](const std::wstring& file){std::ifstream stream(file,std::ios::binary);return std::string(std::istreambuf_iterator<char>(stream),{});};
  const auto go=[&]{return ymcc::settingsUpgrade::prepare(true,primary,shipped,sgWriteFileAtomic);};
  for(const auto& name:names){if(!selected.empty()&&selected!=name)continue;const int before=assertions;
   put(shipped,defaults.dump());auto saved=defaults;saved["music"]["volume"]=0.29;
   if(name=="deep-extension"||name=="limit-extension"||name=="over-depth"){
    json leaf="preserved";const int depth=name=="deep-extension"?40:name=="limit-extension"?62:70;
    for(int i=0;i<depth;++i)leaf=json{{"next",leaf}};saved["future"]=leaf;
   }
   if(name=="legacy-schema-key")saved["schema"]={{"legacyModule",99}};
   if(name=="near-size-limit"){
    saved["future"]=std::string("x");saved["future"]=std::string(ymcc::settingsUpgrade::kMaximum-saved.dump().size()+1,'x');
    need(saved.dump().size()==ymcc::settingsUpgrade::kMaximum,"exact settings size boundary");
   }
   put(primary,saved.dump());put(backup,"{bad-backup");
   if(name=="updater-state-guard"){
    put(primary,R"({"schemaVersion":99})");need(ymcc::updateconfig::read(primary,65536).kind==ymcc::updateconfig::Kind::unsupported,"generic updater schema protection relaxed");
    put(primary,std::string("{\"future\":")+std::string(40,'[')+"0"+std::string(40,']')+"}");
    need(ymcc::updateconfig::read(primary,65536).kind==ymcc::updateconfig::Kind::damaged,"generic updater depth limit relaxed");
    const auto policy=ymcc::updateconfig::policyDefaults(false);ymcc::updateconfig::save(primary,policy,65536,ymcc::updateconfig::policyValid,sgWriteFileAtomic);
    need(ymcc::updateconfig::read(primary,65536).kind==ymcc::updateconfig::Kind::ready&&raw(primary)==policy.dump(),"default updater reader/writer broken");
    auto invalid=policy;invalid["enabled"]="old";bool refused=false;try{ymcc::updateconfig::save(primary,invalid,65536,ymcc::updateconfig::policyValid,sgWriteFileAtomic);}catch(...){refused=true;}
    need(refused&&raw(primary)==policy.dump(),"invalid new updater configuration accepted");
   }else if(name=="defaults-invalid"){
    put(shipped,"{}");bool refused=false;try{go();}catch(...){refused=true;}
    need(refused&&raw(primary)==saved.dump()&&raw(backup)=="{bad-backup","bad shipped defaults bypassed");
   }else if(name=="busy-release"){
    HANDLE h=CreateFileW(primary.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);need(h!=INVALID_HANDLE_VALUE,"busy setup");
    bool refused=false;const auto start=GetTickCount64();try{go();}catch(...){refused=true;}CloseHandle(h);
    need(refused&&GetTickCount64()-start<3000&&raw(primary)==saved.dump(),"busy source false success or unbounded wait");
    need(go()["status"]=="repaired","unlocked source cannot recover");
   }else{
    auto result=name=="archive-failure"?ymcc::settingsUpgrade::prepare(true,primary,shipped,sgWriteFileAtomic,+[](const std::wstring&,const std::wstring&)->bool{throw std::runtime_error("diagnostic archive denied");}):go();
    need(result["status"]=="repaired","old data prevents repair");const auto bytes=raw(primary);const auto repaired=ymcc::settingsBaseline::parseDocument(bytes);
    need(repaired.is_object()&&ymcc::settingsBaseline::supported(repaired)&&bytes.size()<=ymcc::settingsUpgrade::kMaximum&&bytes==raw(backup),"published settings pair unreadable or oversized");
    need(repaired["deckyContextCleanup"]["history"]=="unconfirmed","recovery grants spent cleanup budget");
    if(name=="deep-extension"||name=="limit-extension")need(repaired["future"]==saved["future"]&&repaired["music"]["volume"]==0.29,"compatible extension or preference discarded");
    if(name=="over-depth"||name=="near-size-limit"){
     need(!repaired.contains("future"),"unusable or overflowing legacy content retained");
     need(repaired["input"]["applyStatus"]=="safe-stop"&&repaired["startupDesired"]["fanControl"]==false&&!repaired["extensions"].contains("bootModes"),"discarded configuration activates old startup hardware");
    }
    need(go()["status"]=="compatible"&&raw(primary)==bytes,"recovery repeats on subsequent startup");
    if(name=="legacy-schema-key")need(repaired["schema"]==saved["schema"],"unrelated legacy schema extension removed");
   }
   cases.push_back({{"name",name},{"pass",true},{"assertions",assertions-before}});
  }
  need(cases.size()==(selected.empty()?names.size():1),"exact selection mismatch");
  for(const auto& entry:std::filesystem::directory_iterator(owned)){need(entry.is_regular_file()&&!entry.is_symlink(),"unowned cleanup child");need(std::filesystem::remove(entry.path()),"owned file removal");}
  need(std::filesystem::remove(owned),"owned root removal");
  std::cout<<json{{"status","PASS"},{"scope","upgrade-boundaries-real-win32-io"},{"caseRecords",cases},{"cases",cases.size()},{"assertions",assertions},{"installedSettingsWritten",false},{"hardwareValidated",false},{"ownedIoRemoved",true}}.dump()<<'\n';return 0;
 }catch(const std::exception& error){std::cout<<json{{"status","FAIL"},{"scope","upgrade-boundaries-real-win32-io"},{"caseRecords",cases},{"assertions",assertions},{"error",error.what()}}.dump()<<'\n';return 1;}
}
static int run(const json& defaults){
 int checks=0;const auto need=[&](bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);};
 wchar_t temporary[MAX_PATH]{};need(GetTempPathW(MAX_PATH,temporary)>0,"isolated IO temporary parent unavailable");
 const auto root=std::filesystem::path(temporary)/(L"YMCC-ConfigStorm-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 need(!std::filesystem::exists(root),"isolated IO root must be fresh");std::filesystem::create_directories(root);
 YM_SETTINGS_FILE=(root/L"yeman-settings.json").wstring();YM_SETTINGS_BACKUP_FILE=YM_SETTINGS_FILE+L".bak";
 const auto write=[&](const std::string& bytes){std::ofstream f(YM_SETTINGS_FILE,std::ios::binary|std::ios::trunc);f.write(bytes.data(),bytes.size());need(f.good(),"isolated input write failed");};
 const auto valid=std::string("{\"schemaVersion\":1,\"ui\":{\"theme\":\"blue-black\"}}");write(valid);
 need(ymSettingsReadRaw(YM_SETTINGS_FILE)==valid,"raw native reader altered a valid document");
 auto first=ymSettingsReadSnapshot("");need(first.value("content",std::string{})==valid,"actual settings snapshot changed valid bytes");
 need(ymSettingsReadSnapshot(first.value("stamp",std::string{})).value("unchanged",false),"unchanged settings do not retain metadata fast path");
 reg_battery_charge();
 need(chargeHandlers.size()==4&&ymcc::batterycharge::fixtureRestores==0,"isolated registration loaded or restored the real charge component");
 for(const char* mode:{"global","xbox","steam","xbox-steam"}){
  auto saved=defaults;
  auto& boot=saved["extensions"]["bootModes"];
  boot={{"schema",1},{"revision",3},{"current",mode},{"global",{{"charge",{{"enabled",false},{"limit",80},{"keep",17}}}}},
   {"scenes",{{"steam",{{"overrides",{{"charge",{{"enabled",true},{"limit",70}}}}}}}}}};
  saved["extensions"]["peerChargePreference"]={{"keep",true}};
  const auto scenes=boot["scenes"];
  const json chargeArgs={{"fence",std::string(64,'a')},{"target",60}};
  auto& response=ymcc::batterycharge::fixtureResponse;
  response={{"schema",1},{"ok",true},{"supported",true},{"managed",true},{"requestedLimit",60},{"currentLimit",60},{"verification","readback"}};
  ymcc::batterycharge::fixtureRequests=0;
  write(saved.dump());const auto result=chargeHandlers.at("battery.charge.set")(chargeArgs);
  auto committed=ymSettingsReadUnlocked();
  need(committed["extensions"]["bootModes"]["global"]["charge"]["enabled"]==true&&committed["extensions"]["bootModes"]["global"]["charge"]["limit"]==60,
   "ordinary charge success did not update global startup preference");
  need(result.value("ok",false)&&result.value("preferenceSaved",false)&&ymcc::batterycharge::fixtureRequests==1,"ordinary charge operation replayed or lost its split save receipt");
  need(ymcc::batterycharge::fixtureLastRequest==json{{"op","set"},{"target",60},{"fence",std::string(64,'a')}},"charge origin handling changed the original device fence/target request");
  need(committed["extensions"]["bootModes"]["revision"]==4&&committed["extensions"]["bootModes"]["scenes"]==scenes&&committed["extensions"]["bootModes"]["global"]["charge"]["keep"]==17,
   "ordinary charge preference erased a scene, peer key or revision");
  auto runtimeArgs=chargeArgs;runtimeArgs["origin"]="boot-mode";
  write(saved.dump());const auto runtimeBefore=ymSettingsReadRaw(YM_SETTINGS_FILE);
  const auto runtime=chargeHandlers.at("battery.charge.set")(runtimeArgs);
  need(runtime.value("ok",false)&&ymSettingsReadRaw(YM_SETTINGS_FILE)==runtimeBefore,"boot charge restoration overwrote ordinary global preference");
  response["requestedLimit"]=100;response["currentLimit"]=100;
  auto full=chargeArgs;full["target"]=100;
  write(saved.dump());chargeHandlers.at("battery.charge.set")(full);committed=ymSettingsReadUnlocked();
  need(committed["extensions"]["bootModes"]["global"]["charge"]["enabled"]==false&&committed["extensions"]["bootModes"]["global"]["charge"]["limit"]==100,
   "firmware's managed 100 percent was recorded as an active charge cap");
  response["managed"]=false;response["requestedLimit"]=100;response["currentLimit"]=100;response["verification"]="unmanaged";
  write(saved.dump());const auto released=chargeHandlers.at("battery.charge.release")(json::object());committed=ymSettingsReadUnlocked();
  need(released.value("ok",false)&&released.value("preferenceSaved",false)&&committed["extensions"]["bootModes"]["global"]["charge"]["enabled"]==false&&committed["extensions"]["bootModes"]["global"]["charge"]["limit"]==100,
   "ordinary charge release did not remember unmanaged global intent");
  response["managed"]=true;response["requestedLimit"]=60;response["currentLimit"]=60;response["verification"]="readback";
  write(saved.dump());const auto deniedBefore=ymSettingsReadRaw(YM_SETTINGS_FILE);const auto requestsBefore=ymcc::batterycharge::fixtureRequests;
  HANDLE denied=CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  need(denied!=INVALID_HANDLE_VALUE,"charge preference replacement denial setup failed");
  const auto partial=chargeHandlers.at("battery.charge.set")(chargeArgs);CloseHandle(denied);
  need(partial.value("ok",false)&&partial.contains("preferenceSaved")&&!partial["preferenceSaved"].get<bool>()&&ymSettingsReadRaw(YM_SETTINGS_FILE)==deniedBefore,
   "charge preference IO failure masked accepted device state or reported a future save");
  need(ymcc::batterycharge::fixtureRequests==requestsBefore+1,"preference failure retried or rolled back the device operation");
  write(saved.dump());response["ok"]=false;const auto refusedBefore=ymSettingsReadRaw(YM_SETTINGS_FILE);
  chargeHandlers.at("battery.charge.set")(chargeArgs);
  need(ymSettingsReadRaw(YM_SETTINGS_FILE)==refusedBefore,"failed charge operation changed future global intent");response["ok"]=true;
  auto invalid=chargeArgs;invalid["origin"]="invalid-origin";const auto invalidCalls=ymcc::batterycharge::fixtureRequests;bool rejected=false;
  try{chargeHandlers.at("battery.charge.set")(invalid);}catch(const std::invalid_argument&){rejected=true;}
  need(rejected&&ymcc::batterycharge::fixtureRequests==invalidCalls,"invalid charge origin reached the device owner");
  response["managed"]=true;response["requestedLimit"]=60;response["currentLimit"]=60;response["verification"]="readback";
  saved["extensions"]["bootModes"]["global"]["charge"]["enabled"]=true;saved["extensions"]["bootModes"]["global"]["charge"]["limit"]=60;
  write(saved.dump());const auto unchangedBefore=ymSettingsReadRaw(YM_SETTINGS_FILE);
  const auto unchanged=chargeHandlers.at("battery.charge.set")(chargeArgs);
  need(unchanged.value("preferenceSaved",false)&&ymSettingsReadRaw(YM_SETTINGS_FILE)==unchangedBefore,"matching global charge preference was not an honest no-op");
  response["requestedLimit"]=70;response["currentLimit"]=70;
  const auto mismatch=chargeHandlers.at("battery.charge.set")(chargeArgs);
  need(mismatch.value("ok",false)&&mismatch.contains("preferenceSaved")&&!mismatch["preferenceSaved"].get<bool>()&&ymSettingsReadRaw(YM_SETTINGS_FILE)==unchangedBefore,
   "mismatched device acknowledgement changed the ordinary boot preference");
  response["requestedLimit"]=60;response["currentLimit"]=60;
  response["managed"]=false;
  const auto unmanaged=chargeHandlers.at("battery.charge.set")(chargeArgs);
  need(unmanaged.value("ok",false)&&unmanaged.contains("preferenceSaved")&&!unmanaged["preferenceSaved"].get<bool>()&&ymSettingsReadRaw(YM_SETTINGS_FILE)==unchangedBefore,
   "an unowned sub-100 readback became a saved ordinary charge preference");
  response["managed"]=true;
  saved["extensions"]["bootModes"]["global"]["charge"]["limit"]=80;write(saved.dump());
  ymcc::batterycharge::fixtureBeforeReturn=[&]{auto peer=ymSettingsReadUnlocked();peer["extensions"]["peerDuringDeviceOperation"]={{"keep",99}};write(peer.dump());};
  const auto merged=chargeHandlers.at("battery.charge.set")(chargeArgs);ymcc::batterycharge::fixtureBeforeReturn=nullptr;
  committed=ymSettingsReadUnlocked();
  need(merged.value("preferenceSaved",false)&&committed["extensions"]["peerDuringDeviceOperation"]["keep"]==99&&committed["extensions"]["bootModes"]["global"]["charge"]["limit"]==60,
   "charge projection replaced a concurrent peer extension with a stale snapshot");
 }
 for(const auto schema:{0,1,2}){
  auto saved=defaults;
  if(schema!=0)saved["extensions"]["bootModes"]={{"schema",schema},{"revision",9007199254740991LL},{"global",{{"charge",{{"enabled",false},{"limit",80}}}}}};
  write(saved.dump());const auto before=ymSettingsReadRaw(YM_SETTINGS_FILE);
  const auto result=chargeHandlers.at("battery.charge.set")({{"fence",std::string(64,'a')},{"target",60}});
  need(result.value("ok",false)&&result.value("preferenceSaved",false)==(schema==0)&&ymSettingsReadRaw(YM_SETTINGS_FILE)==before,
   "missing/future/exhausted boot document produced a false charge preference save or changed retained bytes");
 }
 reg_display();
 const auto resetDisplay=[&]{
  fixtureDisplayCurrent={};fixtureDisplayCurrent.dmSize=sizeof(DEVMODEW);fixtureDisplayCurrent.dmPelsWidth=1920;fixtureDisplayCurrent.dmPelsHeight=1080;fixtureDisplayCurrent.dmDisplayFrequency=120;
  fixtureDisplayModes={{displayModeId(1920,1080,120,0),1920,1080,120,0},{displayModeId(1920,1080,60,0),1920,1080,60,0},{displayModeId(1280,720,60,0),1280,720,60,0}};
  fixtureDisplayTestCode=fixtureDisplayApplyCode=DISP_CHANGE_SUCCESSFUL;fixtureDisplayTests=fixtureDisplayApplies=0;
  fixtureDisplayReadback=true;fixtureDisplayMismatch=false;fixtureDisplayBeforeReturn=nullptr;
 };
 const json displayArgs={{"id",displayModeId(1280,720,60,0)},{"width",1280},{"height",720},{"refresh",60},{"orientation",0}};
 for(const char* mode:{"global","xbox","steam","xbox-steam"}){
  auto saved=defaults;auto& boot=saved["extensions"]["bootModes"];
  boot={{"schema",1},{"revision",3},{"current",mode},{"global",{{"display",{{"brightness",72},{"resolution","2560x1600"},{"hz","144"}}}}},
   {"scenes",{{"steam",{{"overrides",{{"display",{{"brightness",25},{"resolution","1280x800"},{"hz","40"}}}}}}}}}};
  const auto independent=boot["scenes"];
  resetDisplay();write(saved.dump());const auto applied=chargeHandlers.at("display.setMode")(displayArgs);auto committed=ymSettingsReadUnlocked();
  need(committed["extensions"]["bootModes"]["global"]["display"]["resolution"]=="1280x720"&&committed["extensions"]["bootModes"]["global"]["display"]["hz"]=="60",
   "ordinary display success did not update global startup preference");
  need(applied.value("ok",false)&&applied.value("preferenceSaved",false)&&fixtureDisplayApplies==1&&fixtureDisplayTests==1,"display mode did not return an honest actual owner/save receipt");
  need(committed["extensions"]["bootModes"]["global"]["display"]["brightness"]==72&&committed["extensions"]["bootModes"]["scenes"]==independent&&committed["extensions"]["bootModes"]["revision"]==4,
   "ordinary display projection recaptured brightness or changed an own scene");
  for(const char* field:{"resolution","hz"}){
   resetDisplay();write(saved.dump());auto args=displayArgs;args["preferenceFields"]=json::array({field});
   chargeHandlers.at("display.setMode")(args);committed=ymSettingsReadUnlocked();
   need(committed["extensions"]["bootModes"]["global"]["display"][field]==(std::string(field)=="resolution"?"1280x720":"60"),"display explicit field was not saved");
   need(committed["extensions"]["bootModes"]["global"]["display"][std::string(field)=="resolution"?"hz":"resolution"]==(std::string(field)=="resolution"?"144":"2560x1600"),"one ordinary display selector captured the other effective mode field");
  }
  resetDisplay();write(saved.dump());auto bootArgs=displayArgs;bootArgs["origin"]="boot-mode";const auto bootBefore=ymSettingsReadRaw(YM_SETTINGS_FILE);
  need(chargeHandlers.at("display.setMode")(bootArgs).value("ok",false)&&ymSettingsReadRaw(YM_SETTINGS_FILE)==bootBefore,"boot display restoration overwrote ordinary global preference");
  for(const bool readable:{false,true}){
   resetDisplay();write(saved.dump());fixtureDisplayReadback=readable;fixtureDisplayMismatch=readable;const auto before=ymSettingsReadRaw(YM_SETTINGS_FILE);
   const auto result=chargeHandlers.at("display.setMode")(displayArgs);
   need(result.contains("ok")&&!result["ok"].get<bool>()&&ymSettingsReadRaw(YM_SETTINGS_FILE)==before,"display API success without matching actual readback was reported applied or saved");
  }
  resetDisplay();write(saved.dump());const auto deniedBefore=ymSettingsReadRaw(YM_SETTINGS_FILE);
  HANDLE denied=CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);need(denied!=INVALID_HANDLE_VALUE,"display preference denial setup failed");
  const auto partial=chargeHandlers.at("display.setMode")(displayArgs);CloseHandle(denied);
  need(partial.value("ok",false)&&partial.contains("preferenceSaved")&&!partial["preferenceSaved"].get<bool>()&&fixtureDisplayApplies==1&&ymSettingsReadRaw(YM_SETTINGS_FILE)==deniedBefore,
   "display save failure rolled back/replayed the owner or misreported future intent");
  resetDisplay();write(saved.dump());auto current=displayArgs;current["width"]=1920;current["height"]=1080;current["refresh"]=120;
  const auto same=chargeHandlers.at("display.setMode")(current);
  need(same.value("ok",false)&&same.value("preferenceSaved",false)&&fixtureDisplayTests==0&&fixtureDisplayApplies==0,"same display selection replayed a device write instead of repairing only stale global intent");
  resetDisplay();write(saved.dump());fixtureDisplayTestCode=DISP_CHANGE_BADMODE;const auto invalidBefore=ymSettingsReadRaw(YM_SETTINGS_FILE);bool refused=false;
  try{chargeHandlers.at("display.setMode")(displayArgs);}catch(...){refused=true;}
  need(refused&&fixtureDisplayApplies==0&&ymSettingsReadRaw(YM_SETTINGS_FILE)==invalidBefore,"failed display test committed a mode or future preference");
  resetDisplay();auto invalid=displayArgs;invalid["origin"]="unknown";refused=false;
  try{chargeHandlers.at("display.setMode")(invalid);}catch(const std::invalid_argument&){refused=true;}
  need(refused&&fixtureDisplayTests==0&&fixtureDisplayApplies==0,"invalid display origin reached the device port");
  for(const char* field:{"unknown","brightness"}){
   resetDisplay();invalid=displayArgs;invalid["preferenceFields"]=json::array({field});refused=false;
   try{chargeHandlers.at("display.setMode")(invalid);}catch(const std::invalid_argument&){refused=true;}
   need(refused&&fixtureDisplayApplies==0,"invalid display preference field reached the device port");
  }
  resetDisplay();write(saved.dump());fixtureDisplayBeforeReturn=[&]{auto peer=ymSettingsReadUnlocked();peer["extensions"]["peerDuringDisplayChange"]=99;write(peer.dump());};
  const auto merged=chargeHandlers.at("display.setMode")(displayArgs);fixtureDisplayBeforeReturn=nullptr;
  need(merged.value("preferenceSaved",false)&&ymSettingsReadUnlocked()["extensions"]["peerDuringDisplayChange"]==99,"ordinary display save erased a peer change made during the OS operation");
  resetDisplay();fixtureDisplayCurrent.dmDisplayFrequency=1;const auto unknown=chargeHandlers.at("display.getModes")(json::object());
  need(unknown.contains("known")&&!unknown["known"].get<bool>()&&unknown["current"]=="","unknown active refresh became a fabricated current display mode");
 }
 const auto resetBrightness=[&]{fixtureBrightnessValue=50;fixtureBrightnessReads=fixtureBrightnessWrites=fixtureBrightnessApplies=0;fixtureBrightnessMismatch=false;fixtureBrightnessReadCode=fixtureBrightnessWriteCode=fixtureBrightnessApplyCode=ERROR_SUCCESS;fixtureHardwareAllowed=fixtureLeaseAllowed=fixtureSchemeActive=true;fixtureBrightnessEvents=json::array();ymcc::powerSource::fixtureReadable=true;ymcc::powerSource::fixtureAc=1;};
 for(const char* mode:{"global","xbox","steam","xbox-steam"})for(const BYTE ac:{BYTE(0),BYTE(1)}){
  auto saved=defaults;saved["extensions"]["bootModes"]={{"schema",1},{"revision",3},{"current",mode},{"global",{{"display",{{"brightness",72},{"resolution","1920x1080"},{"hz","120"}}}}}};
  resetBrightness();ymcc::powerSource::fixtureAc=ac;write(saved.dump());nativeApplyBrightness(1,true,5);
  auto committed=ymSettingsReadUnlocked();
  need(committed["extensions"]["bootModes"]["global"]["display"]["brightness"]==55,"ordinary brightness success did not update global startup preference");
  need(fixtureBrightnessWrites==1&&fixtureBrightnessApplies==1&&fixtureBrightnessEvents.size()==1&&fixtureBrightnessEvents[0]["value"].value("ok",false)&&fixtureBrightnessEvents[0]["value"].value("preferenceSaved",false),"brightness owner/global save receipt was not confirmed");
  need(committed["extensions"]["bootModes"]["global"]["display"]["resolution"]=="1920x1080"&&committed["extensions"]["bootModes"]["global"]["display"]["hz"]=="120","brightness shortcut recaptured effective display mode");
  resetBrightness();ymcc::powerSource::fixtureReadable=false;write(saved.dump());nativeApplyBrightness(1,true,5);
  need(fixtureBrightnessWrites==0&&ymSettingsReadUnlocked()==saved,"unknown power source was silently treated as AC for a brightness write");
  resetBrightness();fixtureBrightnessMismatch=true;write(saved.dump());nativeApplyBrightness(1,true,5);
  need(fixtureBrightnessEvents.size()==1&&!fixtureBrightnessEvents[0]["value"].value("ok",true)&&ymSettingsReadUnlocked()==saved,"brightness accepted a write acknowledgement without matching plan readback");
  resetBrightness();fixtureBrightnessApplyCode=ERROR_ACCESS_DENIED;write(saved.dump());nativeApplyBrightness(1,true,5);
  need(fixtureBrightnessEvents.size()==1&&!fixtureBrightnessEvents[0]["value"].value("ok",true)&&fixtureBrightnessEvents[0]["value"].value("planValueSaved",false)&&fixtureBrightnessEvents[0]["value"].value("preferenceSaved",false)&&ymSettingsReadUnlocked()["extensions"]["bootModes"]["global"]["display"]["brightness"]==55,
   "brightness durable plan save was confused with current application");
  fixtureShortcutEvents=json::array();nativeShortcutFeedbackFromResult("gamepad.brightness",fixtureBrightnessEvents[0]["value"]);
  need(fixtureShortcutEvents.size()==1&&fixtureShortcutEvents[0]["value"]["state"]=="pending","plan-only brightness receipt became an applied monitor notification");
  resetBrightness();write(saved.dump());const auto bytes=ymSettingsReadRaw(YM_SETTINGS_FILE);
  HANDLE denied=CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);need(denied!=INVALID_HANDLE_VALUE,"brightness preference denial setup failed");
  nativeApplyBrightness(1,true,5);CloseHandle(denied);
  need(fixtureBrightnessEvents.size()==1&&fixtureBrightnessEvents[0]["value"].value("ok",false)&&!fixtureBrightnessEvents[0]["value"].value("preferenceSaved",true)&&fixtureBrightnessWrites==1&&ymSettingsReadRaw(YM_SETTINGS_FILE)==bytes,
   "brightness preference failure changed native application truth, replayed power write or overwrote settings");
  fixtureShortcutEvents=json::array();nativeShortcutFeedbackFromResult("gamepad.brightness",fixtureBrightnessEvents[0]["value"]);
  need(fixtureShortcutEvents.size()==1&&fixtureShortcutEvents[0]["value"]["state"]=="pending","missing brightness future preference became a fully applied notice");
  for(const char* deniedAt:{"hardware","lease","source","read"}){
   resetBrightness();write(saved.dump());
   if(std::string(deniedAt)=="hardware")fixtureHardwareAllowed=false;
   else if(std::string(deniedAt)=="lease")fixtureLeaseAllowed=false;
   else if(std::string(deniedAt)=="source")ymcc::powerSource::fixtureAc=255;
   else fixtureBrightnessReadCode=ERROR_ACCESS_DENIED;
   nativeApplyBrightness(1,true,5);need(fixtureBrightnessWrites==0&&ymSettingsReadUnlocked()==saved,"brightness refused owner/source/read admission still wrote state");
   const auto expected=std::string(deniedAt)=="source"?"power-source-unknown":std::string(deniedAt)=="read"?"unsupported":"power-transition";
   need(fixtureBrightnessEvents.size()==1&&fixtureBrightnessEvents[0]["value"]["reason"]==expected,"brightness admission case stopped at the wrong owner boundary");
  }
 }
 for(const char* mode:{"global","xbox","steam","xbox-steam"}){
  auto saved=defaults;saved["sleep"]["pauseGameOnSleep"]=false;
  auto& boot=saved["extensions"]["bootModes"];
  boot={{"schema",1},{"revision",3},{"current",mode}};
  boot["global"]["sleep"]["config"]={{"pauseGameOnSleep",false},{"retryOnSleepTimeout",true}};
  boot["scenes"]["steam"]["overrides"]["sleep"]["config"]={{"pauseGameOnSleep",false}};
  write(saved.dump());
  auto sleep=saved["sleep"];sleep["pauseGameOnSleep"]=true;
  const auto remember=[&](json& section,json& document){return ymcc::bootPreferences::rememberSection(document,"sleep",section,{{"pauseGameOnSleep",true}});};
  need(ymSettingsWriteSection("sleep",sleep,remember),"ordinary native preference failed its original atomic writer");
  auto committed=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
  need(committed["sleep"]["pauseGameOnSleep"]==true&&committed["extensions"]["bootModes"]["global"]["sleep"]["config"]["pauseGameOnSleep"]==true&&committed["extensions"]["bootModes"]["revision"]==4,"ordinary native save did not commit preference and global intent together");
  need(committed["extensions"]["bootModes"]["scenes"]==boot["scenes"],"ordinary native preference overwrote an independent scene");
  sleep["pauseGameOnSleep"]=false;
  need(ymSettingsWriteSection("sleep",sleep),"runtime save failed original section writer");
  committed=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
  need(committed["sleep"]["pauseGameOnSleep"]==false&&committed["extensions"]["bootModes"]["global"]["sleep"]["config"]["pauseGameOnSleep"]==true&&committed["extensions"]["bootModes"]["revision"]==4,"runtime save contaminated global startup intent");
  const auto beforeFailure=ymSettingsReadRaw(YM_SETTINGS_FILE);
  HANDLE held=CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  need(held!=INVALID_HANDLE_VALUE,"atomic preference failure setup failed");
  sleep["pauseGameOnSleep"]=true;const bool failed=ymSettingsWriteSection("sleep",sleep,remember);CloseHandle(held);
  need(!failed&&ymSettingsReadRaw(YM_SETTINGS_FILE)==beforeFailure,"failed preference commit partly saved startup intent");
  const auto patchPreference=[&](const char* section,const json& patch,ymcc::bootPreferences::WriteOrigin origin){
   return ymSettingsWriteSection(section,json::object(),[&](json& prepared,json& document){
    return ymcc::bootPreferences::prepareSection(prepared,document,section,patch,origin);
   });
  };
  need(patchPreference("sleep",{{"wakePasswordEnabled",true}},ymcc::bootPreferences::WriteOrigin::Ordinary),"ordinary wake password preference failed");
  committed=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
  need(committed["sleep"]["wakePasswordEnabled"]==true&&committed["extensions"]["bootModes"]["global"]["sleep"]["config"]["wakePasswordEnabled"]==true&&committed["extensions"]["bootModes"]["revision"]==5,"ordinary wake password did not atomically update global intent");
  need(patchPreference("sleep",{{"wakePasswordEnabled",false}},ymcc::bootPreferences::WriteOrigin::BootMode),"boot wake password preference failed");
  committed=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
  need(committed["sleep"]["wakePasswordEnabled"]==false&&committed["extensions"]["bootModes"]["global"]["sleep"]["config"]["wakePasswordEnabled"]==true&&committed["extensions"]["bootModes"]["revision"]==5,"boot wake password contaminated global intent");
  need(patchPreference("extensions",{{"deckySidebar",{{"enabled",false}}}},ymcc::bootPreferences::WriteOrigin::Ordinary),"ordinary Decky preference failed");
  committed=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
  need(committed["extensions"]["deckySidebar"]["enabled"]==false&&committed["extensions"]["bootModes"]["global"]["steamDecky"]["enabled"]==false&&committed["extensions"]["bootModes"]["revision"]==6,"extensions section replaced its own projected boot intent");
  need(committed["extensions"]["bootModes"]["scenes"]==boot["scenes"],"Decky preference changed independent scenes");
  need(patchPreference("extensions",{{"deckySidebar",{{"enabled",true}}}},ymcc::bootPreferences::WriteOrigin::BootMode),"boot Decky preference failed");
  committed=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
  need(committed["extensions"]["deckySidebar"]["enabled"]==true&&committed["extensions"]["bootModes"]["global"]["steamDecky"]["enabled"]==false&&committed["extensions"]["bootModes"]["revision"]==6,"boot Decky contaminated global intent");
  need(patchPreference("extensions",{{"unrelatedNewExtension",42}},ymcc::bootPreferences::WriteOrigin::Ordinary),"ordinary unrelated extension save failed");
  committed=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
  need(committed["extensions"]["unrelatedNewExtension"]==42&&committed["extensions"]["bootModes"]["global"]["steamDecky"]["enabled"]==false&&committed["extensions"]["bootModes"]["revision"]==6,"unrelated extension edit captured the runtime Decky value");
  const auto beforeDeckyFailure=ymSettingsReadRaw(YM_SETTINGS_FILE);
  held=CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  need(held!=INVALID_HANDLE_VALUE,"Decky atomic failure setup failed");
  const bool deckyFailed=patchPreference("extensions",{{"deckySidebar",{{"enabled",false}}}},ymcc::bootPreferences::WriteOrigin::Ordinary);CloseHandle(held);
  need(!deckyFailed&&ymSettingsReadRaw(YM_SETTINGS_FILE)==beforeDeckyFailure,"failed Decky commit lost either extension or startup intent");
  need(patchPreference("sleep",{{"autoWakeRecovery",true}},ymcc::bootPreferences::WriteOrigin::Ordinary),"ordinary automatic sleep recovery preference failed");
  committed=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
  need(committed["sleep"]["autoWakeRecovery"]==true&&committed["extensions"]["bootModes"]["global"]["sleep"]["steamAutoRecovery"]==true&&committed["extensions"]["bootModes"]["revision"]==7,"ordinary automatic recovery preference was not committed with global intent");
  need(patchPreference("sleep",{{"autoWakeRecovery",false}},ymcc::bootPreferences::WriteOrigin::BootMode),"boot automatic recovery preference failed");
  committed=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
  need(committed["sleep"]["autoWakeRecovery"]==false&&committed["extensions"]["bootModes"]["global"]["sleep"]["steamAutoRecovery"]==true&&committed["extensions"]["bootModes"]["revision"]==7,"boot automatic recovery preference contaminated global intent");
 }
 for(const auto& bad:std::vector<std::string>{std::string("\xff\xfe{\0",4),std::string("{\"x\":\"\xd6\xd0\"}",10)}){
  write(bad);const auto fault=ymSettingsReadSnapshot("");need(fault.value("readFault",std::string{})=="invalid-text-encoding"&&fault.value("content",std::string{}).empty(),"invalid file encoding crosses the asynchronous JSON response");
  need(!fault.dump().empty()&&ymSettingsReadRaw(YM_SETTINGS_FILE)==bad,"encoding fallback is not transport safe or mutates original");
 }
 write(std::string(4*1024*1024+1,'x'));auto large=ymSettingsReadSnapshot("");need(large.value("readFault",std::string{})=="content-read-failed"||large.value("readFault",std::string{})=="content-too-large","oversized configuration is not a bounded fault");
 write(valid);HANDLE exclusive=CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);need(exclusive!=INVALID_HANDLE_VALUE,"exclusive-read fixture handle unavailable");
 const auto locked=ymSettingsReadSnapshot("");CloseHandle(exclusive);need(locked.value("readFault",std::string{})=="content-read-failed","sharing failure is not an inspectable defaultable read result");
 need(ymSettingsReadSnapshot("").value("content",std::string{})==valid,"removing sharing fault does not heal the next explicit read");
 auto contention=[&](bool named){
  std::promise<bool> ready;std::promise<void> release;auto free=release.get_future();
  std::thread holder([&]{if(named){HANDLE mutex=CreateMutexW(nullptr,FALSE,isolatedMutexName().c_str());const bool held=mutex&&WaitForSingleObject(mutex,2000)==WAIT_OBJECT_0;ready.set_value(held);free.wait();if(held)ReleaseMutex(mutex);if(mutex)CloseHandle(mutex);}else{std::lock_guard<std::timed_mutex> held(g_settingsFileMtx);ready.set_value(true);free.wait();}});
  const bool held=ready.get_future().get();if(!held){release.set_value();holder.join();need(false,"fixture lock not acquired");}
  const auto begin=GetTickCount64();bool refused=false;try{SettingsFileGuard guard;}catch(const std::exception&){refused=true;}
  const auto elapsed=GetTickCount64()-begin;
  json fallback;bool readFailed=false;const auto readBegin=GetTickCount64();try{fallback=ymSettingsRead();}catch(...){readFailed=true;}
  const auto readElapsed=GetTickCount64()-readBegin;release.set_value();holder.join();
  need(!readFailed&&fallback==json::object()&&readElapsed<3500,"runtime native read contention throws instead of usable defaults");
  need(refused&&elapsed>=1500&&elapsed<3500,"actual native configuration lock has no bounded shared wait budget");
  {SettingsFileGuard guard;}need(true,"released configuration lock does not heal");
 };
 contention(false);contention(true);
 std::filesystem::remove(YM_SETTINGS_FILE);need(ymSettingsReadSnapshot("").value("stamp",std::string{})=="missing","genuine missing file is confused with inaccessible or invalid");
 // Run the production transaction and atomic writer against owned real files.
 const json emptyBaseline=json::object();write("{broken-main");
 {std::ofstream backup(YM_SETTINGS_BACKUP_FILE,std::ios::binary);backup<<"{broken-backup";need(backup.good(),"real backup setup failed");}
 json committed;need(ymSettingsWriteDocument(defaults.dump(),nullptr,&emptyBaseline,&committed,true),"real both-corrupt transaction cannot initialize");
 need(committed==json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE))&&committed["input"]["applyStatus"]=="safe-stop","real commit receipt or safe defaults are invalid");
 bool archivedMain=false,archivedBackup=false;
 for(const auto& entry:std::filesystem::directory_iterator(root)){
  const auto file=entry.path().wstring();if(file.starts_with(YM_SETTINGS_FILE+L".before-auto-recovery-"))archivedMain=ymSettingsReadRaw(file)=="{broken-main";
  if(file.starts_with(YM_SETTINGS_BACKUP_FILE+L".before-auto-recovery-"))archivedBackup=ymSettingsReadRaw(file)=="{broken-backup";
 }
 need(archivedMain&&archivedBackup,"real recovery does not archive both exact preimages");
 auto changed=committed;changed["music"]["volume"]=0.33;need(ymSettingsWriteDocument(changed.dump(),nullptr,&committed,&changed),"recovered disk document cannot save ordinary edit");
 const auto beforeMain=ymSettingsReadRaw(YM_SETTINGS_FILE);auto next=changed;next["music"]["volume"]=0.34;
 HANDLE keepMain=CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);need(keepMain!=INVALID_HANDLE_VALUE,"real replacement-sharing fixture unavailable");
 const bool replaced=ymSettingsWriteDocument(next.dump(),nullptr,&changed);CloseHandle(keepMain);
 need(!replaced&&ymSettingsReadRaw(YM_SETTINGS_FILE)==beforeMain,"real atomic replacement failure changes original or reports success");
 need(ymSettingsWriteDocument(next.dump(),nullptr,&changed,&committed)&&committed["music"]["volume"]==0.34,"real replacement failure does not heal after unlock");
 HANDLE keepBackup=CreateFileW(YM_SETTINGS_BACKUP_FILE.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);need(keepBackup!=INVALID_HANDLE_VALUE,"real backup-exclusive fixture unavailable");
 bool writeRejected=false;try{ymSettingsWriteDocument(next.dump(),nullptr,&committed);}catch(...){writeRejected=true;}CloseHandle(keepBackup);
 need(writeRejected&&json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE))==committed,"real unreadable backup is overwritten by a parse-error fallback");
 auto future=defaults;future["schemaVersion"]=99;const auto futureBytes=future.dump();
 {std::ofstream backup(YM_SETTINGS_BACKUP_FILE,std::ios::binary|std::ios::trunc);backup<<futureBytes;need(backup.good(),"real future backup setup failed");}
 auto final=committed;final["music"]["volume"]=0.35;need(ymSettingsWriteDocument(final.dump(),nullptr,&committed,&final),"real healthy-main save cannot preserve future backup");
 bool futureArchived=false;for(const auto& entry:std::filesystem::directory_iterator(root)){
  const auto file=entry.path().wstring();if(file.starts_with(YM_SETTINGS_BACKUP_FILE+L".before-backup-replacement-"))futureArchived=ymSettingsReadRaw(file)==futureBytes;
  need(!entry.path().filename().wstring().ends_with(L".tmp"),"failed atomic write leaves a temporary behind");
 }
 need(futureArchived,"real rotation silently drops future backup");
 // Over-limit originals must self-heal through streaming preservation, not remain unsaveable.
 const std::string oversized(4*1024*1024+1,'x');
 const auto rawFile=[](const std::wstring& path){std::ifstream input(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});};
 const auto backupWrite=[&](const std::string& bytes){std::ofstream output(YM_SETTINGS_BACKUP_FILE,std::ios::binary|std::ios::trunc);output<<bytes;need(output.good(),"over-limit backup setup failed");};
 write(oversized);backupWrite(defaults.dump());json recovered;
 need(ymSettingsWriteDocument(defaults.dump(),nullptr,&defaults,&recovered),"oversized main cannot recover from a compatible backup");
 need(recovered==json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE)),"oversized recovery lacks actual committed content");
 bool hugeArchived=false;for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.path().filename().wstring().find(L".before-bounded-recovery-")!=std::wstring::npos&&rawFile(entry.path().wstring())==oversized)hugeArchived=true;
 need(hugeArchived&&rawFile(YM_SETTINGS_BACKUP_FILE)==defaults.dump(),"oversized main preimage or compatible backup was lost");
 write(oversized);backupWrite(oversized);need(ymSettingsWriteDocument(defaults.dump(),nullptr,&emptyBaseline,&recovered,true),"both oversized originals cannot initialize after preserving bytes");
 need(recovered["input"]["applyStatus"]=="safe-stop"&&recovered["performanceSchedule"]["enabled"]==false,"both oversized recovery invents active state");
 bool hugeBackupArchived=false;for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.path().wstring().starts_with(YM_SETTINGS_BACKUP_FILE+L".before-bounded-recovery-")&&rawFile(entry.path().wstring())==oversized)hugeBackupArchived=true;
 need(hugeBackupArchived,"oversized backup not preserved exactly");
 write("{bad-small");backupWrite(oversized);need(ymSettingsWriteDocument(defaults.dump(),nullptr,&emptyBaseline,&recovered,true),"small corrupt main plus oversized backup cannot initialize");
 write(defaults.dump());backupWrite(oversized);auto edit=defaults;edit["music"]["volume"]=0.39;
 need(ymSettingsWriteDocument(edit.dump(),nullptr,&defaults,&recovered)&&recovered["music"]["volume"]==0.39,"healthy main cannot save while oversized backup is preserved");
 write(oversized);backupWrite("{bad-backup");HANDLE heldOversized=CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);need(heldOversized!=INVALID_HANDLE_VALUE,"oversized sharing fixture unavailable");
 bool oversizedSaved=false;try{oversizedSaved=ymSettingsWriteDocument(defaults.dump(),nullptr,&emptyBaseline,&recovered,true);}catch(...){}CloseHandle(heldOversized);
 need(!oversizedSaved&&rawFile(YM_SETTINGS_FILE)==oversized,"inaccessible over-limit original is replaced or reported saved");
 need(ymSettingsWriteDocument(defaults.dump(),nullptr,&emptyBaseline,&recovered,true),"oversized recovery does not heal after sharing fault removed");
 // Recovery must retain native receipts and input revision protection too.
 auto trustedBackup=defaults;trustedBackup["input"]["revision"]=17;trustedBackup["domainLogs"]={{"virtual",false}};trustedBackup["steamDeckMouse"]={{"pending",true},{"receipt","native-backup"}};trustedBackup["deckyContextCleanup"]={{"schemaVersion",1},{"steamSession","41:123456"}};
 write(oversized);backupWrite(trustedBackup.dump());auto stale=trustedBackup;stale["input"]["revision"]=0;stale.erase("domainLogs");stale.erase("steamDeckMouse");stale.erase("deckyContextCleanup");stale["music"]["volume"]=0.4;
 need(ymSettingsWriteDocument(stale.dump(),nullptr,&trustedBackup,&recovered),"bounded recovery rejects a valid unrelated edit");
 need(recovered["input"]["revision"]==17&&recovered["domainLogs"]==trustedBackup["domainLogs"]&&recovered["steamDeckMouse"]==trustedBackup["steamDeckMouse"],"bounded recovery drops native receipts or regresses input revision");
 need(recovered["deckyContextCleanup"]==trustedBackup["deckyContextCleanup"],"bounded backup recovery erased Decky final cleanup fuse");
 checks += runCleanupHistory(defaults,root).at("assertions").get<int>();
 auto futureOriginal=defaults;futureOriginal["schemaVersion"]=99;write(oversized);backupWrite(futureOriginal.dump());bool futureReplaced=false;try{futureReplaced=ymSettingsWriteDocument(defaults.dump(),nullptr,&emptyBaseline,&recovered,true);}catch(...){}
 need(!futureReplaced&&rawFile(YM_SETTINGS_FILE)==oversized&&rawFile(YM_SETTINGS_BACKUP_FILE)==futureOriginal.dump(),"oversized recovery consumes a future backup as defaults");
 write(futureOriginal.dump());backupWrite(oversized);futureReplaced=false;try{futureReplaced=ymSettingsWriteDocument(defaults.dump(),nullptr,&emptyBaseline,&recovered,true);}catch(...){}
 need(!futureReplaced&&rawFile(YM_SETTINGS_FILE)==futureOriginal.dump()&&rawFile(YM_SETTINGS_BACKUP_FILE)==oversized,"oversized backup authorizes downgrading a future main");
 write(oversized);backupWrite(defaults.dump());need(ymSettingsReadSnapshot("",YM_SETTINGS_BACKUP_FILE).value("content",std::string{})==defaults.dump(),"backup snapshot reads the wrong file");
 need(ymSettingsReadSnapshot("").value("readFault",std::string{})=="content-too-large","oversized source lacks a recoverable typed fault");
 const auto archiveDestination=(root/L"already-preserved.json").wstring();{std::ofstream output(archiveDestination,std::ios::binary);output<<"existing-archive";}
 write(oversized);need(!ymSettingsArchiveFile(YM_SETTINGS_FILE,archiveDestination)&&rawFile(archiveDestination)=="existing-archive"&&rawFile(YM_SETTINGS_FILE)==oversized,"archive collision overwrites a prior archive or original");
 need(!std::filesystem::exists(archiveDestination+L".tmp"),"failed archive activation leaks its owned temporary");
 const auto timeoutDestination=(root/L"timeout-archive.json").wstring();simulateArchiveTimeout=true;const bool copiedAtTimeout=ymSettingsArchiveFile(YM_SETTINGS_FILE,timeoutDestination);simulateArchiveTimeout=false;
 need(!copiedAtTimeout&&rawFile(YM_SETTINGS_FILE)==oversized&&!std::filesystem::exists(timeoutDestination),"archive copy budget cannot cancel without publishing a partial archive");
 need(!std::filesystem::exists(timeoutDestination+L".tmp"),"timed out archive keeps its own temporary");
 need(ymSettingsArchiveFile(YM_SETTINGS_FILE,timeoutDestination)&&rawFile(timeoutDestination)==oversized,"archive timeout cannot self-heal after transient budget fault clears");

 // The same production startup transaction used after an old installer starts
 // the incoming executable. All paths are inside this existing owned IO root.
 const auto upgradeDefaults=(root/L"upgrade-defaults.json").wstring();
 {std::ofstream output(upgradeDefaults,std::ios::binary);output<<defaults.dump();need(output.good(),"upgrade defaults setup");}
 const auto upgrade=[&](bool active=true){return ymcc::settingsUpgrade::prepare(active,YM_SETTINGS_FILE,upgradeDefaults,sgWriteFileAtomic);};
 const auto pair=[&](const std::string& main,const std::string& backup){write(main);backupWrite(backup);};
 const auto safePair=[&]{
  const auto a=json::parse(rawFile(YM_SETTINGS_FILE)),b=json::parse(rawFile(YM_SETTINGS_BACKUP_FILE));
  need(a==b&&ymcc::settingsBaseline::supported(a),"upgrade repaired pair mismatch");
  need(a["configurationBaseline"]["legacyImported"]==true,"upgrade can reimport bad legacy mirrors");
  need(a["deckyContextCleanup"]["history"]=="unconfirmed"&&a["deckyBigPictureRecovery"]["history"]=="unconfirmed","upgrade refunded native recovery budget");
  need(a["tdp"]["autoApply"]["boot"]==false&&a["performanceSchedule"]["enabled"]==false&&a["input"]["applyStatus"]=="safe-stop","upgrade activates hardware from damaged data");
 };
 const std::vector<std::string> incompatible={"", "{broken", "[]", "null", "true", "17",
  R"({"schemaVersion":99})",R"({"schemaVersion":"old"})",R"({"configurationBaseline":null})",
  R"({"configurationBaseline":{"id":"other","revision":1,"legacyImported":true}})",
  R"({"configurationBaseline":{"id":"ymcc-settings","revision":99,"legacyImported":true}})",
  R"({"configurationBaseline":{"id":"ymcc-settings","revision":1,"legacyImported":false}})",
  R"({"schemaVersion":1,"tdp":null})",R"({"input":{"outputTarget":[]}})",
  R"({"startupDesired":{"fanControl":"true"}})",R"({"sleep":true})",
  R"({"performanceSchedule":{"enabled":"obsolete"}})",
  R"({"schemaVersion":99,"schemaVersion":1})",
  R"({"schemaVersion":1,"tdp":{"autoApply":{"boot":false,"b\u006fot":true}}})",
  std::string(4*1024*1024+1,'x'),std::string(70,'[')+"0"+std::string(70,']'),std::string("\xff\xfe\x00",3)};
 for(const auto& old:incompatible){pair(old,"{broken-backup");need(upgrade()["status"]=="repaired","incompatible old data blocked upgrade");safePair();
  const auto stable=rawFile(YM_SETTINGS_FILE);need(upgrade()["status"]=="compatible"&&rawFile(YM_SETTINGS_FILE)==stable,"upgrade recovery not idempotent");}
 pair("{broken","{broken-backup");const auto dormant=rawFile(YM_SETTINGS_FILE);need(upgrade(false)["status"]=="not-installing"&&rawFile(YM_SETTINGS_FILE)==dormant,"predownload mutates settings");
 pair("{broken",defaults.dump());need(upgrade()["source"]=="backup","compatible backup not used");safePair();
 const auto utf16=[](const std::string& utf8,bool be){
  const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),static_cast<int>(utf8.size()),nullptr,0);
  if(count<=0)throw std::runtime_error("encoding fixture UTF8 invalid");
  std::wstring wide(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),static_cast<int>(utf8.size()),wide.data(),count);
  std::string bytes=be?std::string("\xfe\xff",2):std::string("\xff\xfe",2);
  for(wchar_t c:wide){bytes+=static_cast<char>(be?(c>>8):(c&255));bytes+=static_cast<char>(be?(c&255):(c>>8));}return bytes;
 };
 for(bool be:{false,true}){
  auto saved=defaults;saved["music"]["folder"]="\xe6\xb5\x8b\xe8\xaf\x95";saved["future"]={{"keep",true}};
  const auto encoded=utf16(saved.dump(),be);pair(encoded,encoded);
  need(upgrade()["status"]=="repaired","UTF16 deemed ready while real settings reader cannot parse it");
  const auto normalized=ymSettingsReadUnlocked();need(normalized["music"]["folder"]==saved["music"]["folder"]&&normalized["future"]["keep"]==true,"UTF16 normalization lost compatible values");
  need(ymcc::settingsBaseline::supported(json::parse(rawFile(YM_SETTINGS_BACKUP_FILE))),"encoded backup left unreadable");
  const auto stable=rawFile(YM_SETTINGS_FILE);need(upgrade()["status"]=="compatible"&&rawFile(YM_SETTINGS_FILE)==stable,"encoding recovery keeps writing every startup");
 }
 auto armedBackup=defaults;armedBackup["extensions"]["bootModes"]={{"enabled",true},{"activeMode","performance"}};
 armedBackup["extensions"]["bootModesAppliedBoot"]="old-boot";armedBackup["extensions"]["unrelated"]={{"keep",true}};
 pair("{broken",armedBackup.dump());need(upgrade()["status"]=="repaired","armed compatible backup cannot recover");safePair();
 need(!ymSettingsReadUnlocked()["extensions"].contains("bootModes")&&ymSettingsReadUnlocked()["extensions"]["unrelated"]["keep"]==true,"recovered backup can rearm old startup scenario or loses unrelated fields");
 pair("{broken","{backup-broken");const auto archiveThrow=[](const std::wstring&,const std::wstring&)->bool{throw std::runtime_error("archive IO fault");};
 need(ymcc::settingsUpgrade::prepare(true,YM_SETTINGS_FILE,upgradeDefaults,sgWriteFileAtomic,archiveThrow)["status"]=="repaired","archive exception vetoes upgrade");safePair();
 auto healthyLegacy=defaults;healthyLegacy["schemaVersion"]=0;healthyLegacy.erase("configurationBaseline");healthyLegacy["future"]={{"keep",true}};
 pair(healthyLegacy.dump(),healthyLegacy.dump());const auto preserved=rawFile(YM_SETTINGS_FILE);need(upgrade()["status"]=="compatible"&&rawFile(YM_SETTINGS_FILE)==preserved,"compatible legacy config changed");
 pair("{broken","{broken-backup");need(SetFileAttributesW(YM_SETTINGS_FILE.c_str(),FILE_ATTRIBUTE_READONLY)!=FALSE,"readonly setup");need(upgrade()["status"]=="repaired","readonly legacy blocks upgrade");safePair();
 pair("{broken","{broken-backup");const auto noArchive=[](const std::wstring&,const std::wstring&){return false;};
 need(ymcc::settingsUpgrade::prepare(true,YM_SETTINGS_FILE,upgradeDefaults,sgWriteFileAtomic,noArchive)["archived"]==0,"best-effort archive failure blocked upgrade");safePair();
 pair("{broken","{broken-backup");int writes=0;bool partialRejected=false;
 try{ymcc::settingsUpgrade::prepare(true,YM_SETTINGS_FILE,upgradeDefaults,[&](const std::wstring& path,const std::string& text){return ++writes==2?false:sgWriteFileAtomic(path,text);});}catch(...){partialRejected=true;}
 need(partialRejected&&ymcc::settingsBaseline::supported(json::parse(rawFile(YM_SETTINGS_FILE))),"interrupted pair lost healthy new main");need(upgrade()["status"]=="repaired","partial pair cannot resume");safePair();
 pair("{broken","{broken-backup");const auto original=rawFile(YM_SETTINGS_FILE);
 {std::ofstream output(upgradeDefaults,std::ios::binary|std::ios::trunc);output<<"{}";}
 bool invalidDefaultsRefused=false;try{upgrade();}catch(...){invalidDefaultsRefused=true;}
 need(invalidDefaultsRefused&&rawFile(YM_SETTINGS_FILE)==original,"old-data tolerance bypassed shipped defaults validation");
 {std::ofstream output(upgradeDefaults,std::ios::binary|std::ios::trunc);output<<defaults.dump();}
 HANDLE oldLock=CreateFileW(YM_SETTINGS_FILE.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);need(oldLock!=INVALID_HANDLE_VALUE,"upgrade lock setup");
 const auto lockedStart=GetTickCount64();bool lockRefused=false;try{upgrade();}catch(...){lockRefused=true;}const auto lockedMs=GetTickCount64()-lockedStart;CloseHandle(oldLock);
 need(lockRefused&&lockedMs<3000&&rawFile(YM_SETTINGS_FILE)==original,"locked old file hangs or reports false success");need(upgrade()["status"]=="repaired","released old file cannot self-heal");safePair();
 // A stale directory in a file slot is renamed as one literal owned entry.
 std::filesystem::remove(YM_SETTINGS_FILE);std::filesystem::create_directory(YM_SETTINGS_FILE);
 need(upgrade()["status"]=="repaired","directory in old settings slot blocks upgrade");safePair();
 // Reparse points are isolated as entries, never traversed for recovery writes.
 const auto junction=[](const std::filesystem::path& link,const std::filesystem::path& target){
  std::filesystem::create_directory(link);const auto print=std::filesystem::absolute(target).wstring(),sub=std::wstring(L"\\??\\")+print;
  std::vector<unsigned char> data(16+(sub.size()+1+print.size()+1)*sizeof(wchar_t),0);
  const DWORD tag=IO_REPARSE_TAG_MOUNT_POINT;memcpy(data.data(),&tag,4);
  const USHORT bytes=static_cast<USHORT>(data.size()-8),subLength=static_cast<USHORT>(sub.size()*2),printOffset=static_cast<USHORT>((sub.size()+1)*2),printLength=static_cast<USHORT>(print.size()*2);
  memcpy(data.data()+4,&bytes,2);memcpy(data.data()+10,&subLength,2);memcpy(data.data()+12,&printOffset,2);memcpy(data.data()+14,&printLength,2);
  memcpy(data.data()+16,sub.data(),subLength);memcpy(data.data()+16+printOffset,print.data(),printLength);
  HANDLE h=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(h==INVALID_HANDLE_VALUE)return false;
  DWORD used=0;const bool ok=DeviceIoControl(h,FSCTL_SET_REPARSE_POINT,data.data(),static_cast<DWORD>(data.size()),nullptr,0,&used,nullptr)!=FALSE;CloseHandle(h);return ok;
 };
 const auto external=root/L"owned-external-target";std::filesystem::create_directory(external);
 const auto externalFile=(external/L"yeman-settings.json").wstring();{std::ofstream output(externalFile,std::ios::binary);output<<"external-do-not-touch";}
 std::filesystem::remove(YM_SETTINGS_FILE);need(junction(YM_SETTINGS_FILE,external),"owned leaf junction setup");
 need(upgrade()["status"]=="repaired"&&rawFile(externalFile)=="external-do-not-touch","leaf junction affects external target");safePair();
 const auto parentLink=root/L"owned-parent-link";need(junction(parentLink,external),"owned parent junction setup");bool parentRefused=false;
 try{ymcc::settingsUpgrade::prepare(true,(parentLink/L"yeman-settings.json").wstring(),upgradeDefaults,sgWriteFileAtomic);}catch(...){parentRefused=true;}
 need(parentRefused&&rawFile(externalFile)=="external-do-not-touch","parent redirection allowed external configuration write");
 need(RemoveDirectoryW(parentLink.c_str())!=FALSE,"owned parent junction cleanup");
 for(const auto& entry:std::filesystem::directory_iterator(root))if((GetFileAttributesW(entry.path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT)&&entry.path().filename().wstring().starts_with(L"yeman-settings.json.ymcc-retired-"))need(RemoveDirectoryW(entry.path().c_str())!=FALSE,"owned retired junction cleanup");
 std::filesystem::remove(externalFile);std::filesystem::remove(external);
 const auto legacy=root/L"legacy-migration",canonical=root/L"new-canonical";
 std::filesystem::create_directory(legacy);std::filesystem::create_directory(canonical);
 const auto putRaw=[](const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;if(!out.good())throw std::runtime_error("migration fixture write failed");};
 for(const auto* name:{L"settings-defaults-v1.json",L"runtime.dll",L"yeman-settings.json"}){
  putRaw(legacy/name,"obsolete-old-bytes");putRaw(canonical/name,"new-canonical-bytes");
 }
 putRaw(legacy/L"ui-settings.json","missing-user-preference");
 putRaw(legacy/L"removed-runtime.dll","obsolete-removed-runtime");putRaw(legacy/L"old-install.ps1","obsolete-installer");
 putRaw(legacy/L"unmanaged-manifest.json","obsolete-manifest");
 migrate_legacy_power_control_dir(canonical,legacy);
 for(const auto* name:{L"settings-defaults-v1.json",L"runtime.dll",L"yeman-settings.json"})
  need(rawFile((canonical/name).wstring())=="new-canonical-bytes","legacy migration overwrote incoming runtime/defaults/user configuration");
 need(rawFile((canonical/L"ui-settings.json").wstring())=="missing-user-preference"&&!std::filesystem::exists(legacy),"missing-only migration or bounded cleanup failed");
 need(!std::filesystem::exists(canonical/L"removed-runtime.dll")&&!std::filesystem::exists(canonical/L"old-install.ps1")&&!std::filesystem::exists(canonical/L"unmanaged-manifest.json"),"legacy migration resurrects removed binary/script/manifest");
 bool retiredLegacy=false;
 for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.path().filename().wstring().starts_with(L"legacy-migration.ymcc-retired-")){
  need(rawFile((entry.path()/L"removed-runtime.dll").wstring())=="obsolete-removed-runtime","legacy original not isolated");
  for(const auto& file:std::filesystem::directory_iterator(entry.path())){need(file.is_regular_file(),"unexpected legacy fixture child");std::filesystem::remove(file.path());}
  std::filesystem::remove(entry.path());retiredLegacy=true;
 }
 need(retiredLegacy,"legacy directory not retired as one owned entry");
 for(const auto& file:std::filesystem::directory_iterator(canonical))std::filesystem::remove(file.path());std::filesystem::remove(canonical);
 for(const auto& entry:std::filesystem::directory_iterator(root)){
  if(entry.is_directory()&&entry.path().filename().wstring().starts_with(L"yeman-settings.json.ymcc-retired-"))std::filesystem::remove(entry.path());
  else if(entry.is_regular_file())SetFileAttributesW(entry.path().c_str(),FILE_ATTRIBUTE_NORMAL);
 }
 // Every child is owned by this fresh directory; remove files without recursion.
 for(const auto& entry:std::filesystem::directory_iterator(root)){need(entry.is_regular_file(),"unexpected non-owned child in IO fixture");std::filesystem::remove(entry.path());}
 std::filesystem::remove(root);return checks;
}
#endif
}
#if !defined(YMCC_SETTINGS_SECTION_CACHE_SCOPE)
namespace ymcc::gesturesummon { inline constexpr UINT kRefreshMessage=WM_APP+7; }
namespace memoryFixture {
static const std::wstring YM_SETTINGS_FILE=L"fixture\\yeman-settings.json";
static const std::wstring YM_SETTINGS_BACKUP_FILE=YM_SETTINGS_FILE+L".bak";
static std::map<std::wstring,std::string> files;
static int writes=0, admitted=0;
static bool failMainWrite=false;
#if defined(YMCC_SETTINGS_VIRTUAL_START_WRITE_FAILURE_SCOPE)
static std::uint64_t virtualStartClock=1234;
static unsigned virtualStartMainAttempts=0;
#endif
static int failWriteOrdinal=0,corruptWriteOrdinal=0,writeAttempts=0;
static int failArchiveAt=0,archiveAttempts=0;
static bool failMainRead=false,failBackupRead=false;
struct SettingsFileGuard {};
static std::string ymSettingsReadRaw(const std::wstring& file){if((file==YM_SETTINGS_FILE&&failMainRead)||(file==YM_SETTINGS_BACKUP_FILE&&failBackupRead))throw std::runtime_error("fixture-sharing-or-access-error");return files.contains(file)?files[file]:std::string{};}
static bool sgWriteFileAtomic(const std::wstring& file,const std::string& value){
#if defined(YMCC_SETTINGS_VIRTUAL_START_WRITE_FAILURE_SCOPE)
 if(file==YM_SETTINGS_FILE){++virtualStartMainAttempts;if(failMainWrite)virtualStartClock+=37;}
#endif
#if defined(YMCC_SETTINGS_SERIAL_CONFIG_SCOPE)
 serialConfigFixture::observeAtomicWrite(file,value);
#endif
++writeAttempts;if(failWriteOrdinal&&writeAttempts==failWriteOrdinal)return false;if(corruptWriteOrdinal&&writeAttempts==corruptWriteOrdinal){files[file]="{partial-write";++writes;return true;}if(failMainWrite&&file==YM_SETTINGS_FILE)return false;if((file.find(L".before-auto-recovery-")!=std::wstring::npos||file.find(L".before-backup-replacement-")!=std::wstring::npos)&&++archiveAttempts==failArchiveAt)return false;files[file]=value;++writes;return true;}
static bool sgWriteFileAtomicFlags(const std::wstring& file,const std::string& value,bool durable,const std::function<void()>& prepare=nullptr,const std::function<bool()>& current={}){
 if(!durable)throw std::runtime_error("runtime input settings lost durable write");if(current&&!current())return false;if(prepare)prepare();if(current&&!current())return false;return sgWriteFileAtomic(file,value);
}
static unsigned long long GetTickCount64(){
#if defined(YMCC_SETTINGS_VIRTUAL_START_WRITE_FAILURE_SCOPE)
 return virtualStartClock;
#else
 return 1234;
#endif
}
static unsigned long GetCurrentProcessId(){return 42;}
static std::string W2U(const std::wstring& value){return std::string(value.begin(),value.end());}
namespace fspath{static bool exists(const std::wstring& file){return files.contains(file);}}
static std::function<void()> beforeTargetAdmit;
#if defined(YMCC_SETTINGS_VIRTUAL_START_WRITE_FAILURE_SCOPE)
static std::mutex g_inputShortcutRuntimeMx;
static ymcc::InputShortcutRuntime g_inputShortcutRuntime;
static std::uint64_t observedTimeoutTicket=0;
static json virtualStartEvents=json::array();
static std::string gameInputOwnerSession(){return "owned-settings-fixture";}
static bool inputHostRogOffExecutionDefersSuccess(std::uint64_t,const char*){throw std::logic_error("unexpected ROG OFF port");}
static void inputHostRecordSwitchTimeout(const ymcc::VirtualStartProtection::View& view) noexcept{observedTimeoutTicket=view.requestId;}
static void appendNativeLifecycleLog(const char*,const json&);
// SETTINGS_BASELINE_VIRTUAL_START_GUARD
#else
struct InputHostTargetWriteGuard{bool admit(const json&,const json&){if(beforeTargetAdmit)beforeTargetAdmit();++admitted;return true;}void commit(){}};
#endif
static bool boundedMonitorLarge=false,boundedMonitorArchive=false;
static bool ymSettingsFileExceedsLimit(const std::wstring& p){return boundedMonitorLarge&&p==YM_SETTINGS_FILE;}
static bool ymSettingsArchiveFile(const std::wstring& a,const std::wstring& b){if(!boundedMonitorArchive)return false;files[b]=files[a];return true;}
#ifdef YMCC_SETTINGS_MONITOR_ROOT_WRITE_SCOPE
static bool ymSettingsWriteBoundedRecovery(json,const json*,json*,bool,long long,bool*,bool*);
#else
static bool ymSettingsWriteBoundedRecovery(json,const json*,json*,bool,long long,bool* handled,bool*){*handled=false;return false;}
#endif
// SETTINGS_BASELINE_PRODUCTION_BODIES
// SETTINGS_BASELINE_MONITOR_BOUNDED_CONSUMER
// The original cases retain their two-argument port. The production reserve
// still performs the real four-argument document/host admission and pair commit.
static bool deckySidebarReserveContextCleanup(const ymcc::steamsession::Session& session,const std::function<bool()>& current){
 return deckySidebarReserveContextCleanup(session,"settings-baseline-cleanup","42:123456",current);
}

static std::string failingEffect;static std::set<std::string> effectCalls;
static void effect(const char* name){effectCalls.insert(name);if(failingEffect==name)throw std::runtime_error("fixture postcommit failure");}
static std::wstring U2W(const std::string& value){return {value.begin(),value.end()};}
static bool sameWindowsPath(const std::wstring& a,const std::wstring& b){return a==b;}
static void msiTargetIntentDocumentWrite(const json&,const json&,bool,bool,long long,long long)noexcept{}
#if !defined(YMCC_SETTINGS_VIRTUAL_START_WRITE_FAILURE_SCOPE)
static json inputHostStartProtectionSnapshot(){return json::object();}
#endif
static json ymSettingsSection(const char*){return json::object();}
static void appendNativeLifecycleLog(const char* name,const json& row){
#if defined(YMCC_SETTINGS_VIRTUAL_START_WRITE_FAILURE_SCOPE)
 if(std::strcmp(name,"virtual-start-protection")==0){virtualStartEvents.push_back(row);return;}
#else
 (void)name;(void)row;
#endif
 effect("lifecycle-log");
}
static bool sgStaticScanEnabled(const json&){return false;}
static void sgStaticScanPreference(bool){effect("sleep");}
// External wallpaper owner is an inert port in this settings-only consumer.
static void steamWallpaperForegroundPreference(const json&){effect("wallpaper-preference");}
static void steamControllerCursorConfigure(const json&){effect("steam-controller-cursor");}
static void steamMenuGameConfigure(const json&){effect("steam-menu-game");}
static bool fxSoundRecoveryEnabled(){return false;}
struct FixtureFx{void configure(bool){effect("fxsound");}};static FixtureFx g_fxSoundRecovery;
static void inputCaptureRefreshRealStickSettings(bool){effect("input-refresh");}
static json inputShortcutRuntimeSnapshot(){effect("shortcut-snapshot");return json::object();}
static void ipc_emit(const char*,const json&){effect("shortcut-event");}
static int g_hwnd=1;
static BOOL PostMessageW(int,UINT,WPARAM,LPARAM){effect("gesture");return TRUE;}
static std::function<json(const json&)> writeHandler,resetHandler;
static void ipc_on(const char* name,std::function<json(const json&)> handler){if(std::string(name)=="settings.reset")resetHandler=std::move(handler);else writeHandler=std::move(handler);}
static void registerWriteHandler(){
// SETTINGS_BASELINE_PRODUCTION_HANDLER
}

static void need(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static void seed(const json& current){files.clear();files[YM_SETTINGS_FILE]=current.dump();writes=0;admitted=0;failMainWrite=false;failWriteOrdinal=corruptWriteOrdinal=writeAttempts=0;}
static json disk(){return json::parse(files.at(YM_SETTINGS_FILE));}

#if defined(YMCC_SETTINGS_VIRTUAL_START_WRITE_FAILURE_SCOPE)
static int runVirtualStartWriteFailureScope(int argc,char** argv){
 if(argc!=3||std::string(argv[2])!="--virtual-start-write-failure")return 2;
 constexpr auto name="admitted-write-failure-and-explicit-retry";
 const auto cap=ymcc::VirtualStartProtection::maxMs;
 json observations=json::object();int assertions=0;
 const auto check=[&](bool value,const char* reason){++assertions;need(value,reason);};
 const auto result=[&](bool pass){return json{{"status",pass?"PASS":"FAIL"},{"scope","virtual-start-write-failure"},{"cases",1},
  {"caseRecords",json::array({{{"name",name},{"pass",pass},{"assertions",assertions}}})},{"observations",observations},
  {"protectionEvents",virtualStartEvents},{"installedSettingsWritten",false},{"hardwareValidated",false}};};
 try{
  std::ifstream in(argv[1]);json before;in>>before;check(!in.fail(),"owned defaults unavailable");
  before["input"]["revision"]=30;before["input"]["outputTarget"]={{"persona","disabled"},{"buttonMappingEnabled",false},{"gyroEnabled",false}};
  auto next=before;next["input"]["revision"]=31;next["input"]["outputTarget"]={{"persona","elite"},{"buttonMappingEnabled",true},{"gyroEnabled",false}};
  seed(before);registerWriteHandler();
  const auto args=json{{"path",W2U(YM_SETTINGS_FILE)},{"content",next.dump()},{"baseline",before},{"expectedInputRevision",30}};
  const auto start=virtualStartClock;const auto oldBytes=files.at(YM_SETTINGS_FILE);
  check(g_virtualStartProtection.view(start).phase=="idle","fixture must start with no ticket");
  failMainWrite=true;const auto failed=writeHandler(args);failMainWrite=false;
  const auto view=g_virtualStartProtection.view(virtualStartClock);
  observations["failedReceipt"]=failed;
  observations["failedWrite"]={{"mainAttempts",virtualStartMainAttempts},{"oldBytesPreserved",files.at(YM_SETTINGS_FILE)==oldBytes},
   {"phase",view.phase},{"candidate",view.candidateOutcome},{"ticket",view.requestId},{"remainingMs",view.remainingMs},{"effects",effectCalls}};
  check(failed==json({{"ok",false},{"content",""}}),"failed main write must preserve the original ACK");
  check(virtualStartMainAttempts==1&&files.at(YM_SETTINGS_FILE)==oldBytes,"failure must reach the main write without changing the old document");
  check(effectCalls.empty(),"failed write must not refresh runtime or publish shortcut effects");
  check(view.requestId!=0&&view.locked&&view.phase=="verifying"&&view.verificationPending&&view.candidateOutcome=="failure"&&view.persona=="elite","admitted ticket did not enter failure verification");
  check(g_inputHostSwitchVerificationPending.load()==view.requestId&&view.fallbackAttempts==0,"failure changed ownership or spent fallback");
  check(view.remainingMs==cap-37&&view.inspectionStartedElapsedMs==37,"failure moved the original admission deadline");
  check(virtualStartEvents.size()==2,"expected one admission and one verification event");
  check(virtualStartEvents[0].value("action","")=="accepted"&&virtualStartEvents[0].value("origin","")=="settings-commit"&&
   virtualStartEvents[0].value("requestId",0ULL)==view.requestId,"failure cannot mint a different ticket");
  check(virtualStartEvents[1].value("action","")=="verification-pending"&&virtualStartEvents[1].value("origin","")=="settings-write-failed"&&
   virtualStartEvents[1].value("requestId",0ULL)==view.requestId,"failure must use the production verification origin");
  virtualStartClock=start+cap-1;
  const auto retryEarly=writeHandler(args);
  observations["earlyRetryReceipt"]=retryEarly;
  check(!retryEarly.value("ok",true)&&retryEarly.value("reason","")=="virtual-start-protection-active","early retry must honor the old ticket");
  check(virtualStartMainAttempts==1&&files.at(YM_SETTINGS_FILE)==oldBytes&&effectCalls.empty()&&
   g_virtualStartProtection.view(virtualStartClock).requestId==view.requestId,"early retry must not write, refresh or replace the ticket");
  virtualStartClock=start+cap;
  const auto accepted=writeHandler(args);const auto successor=g_virtualStartProtection.view(virtualStartClock);
  observations["explicitRetry"]={{"ok",accepted.value("ok",false)},{"target",disk()["input"]["outputTarget"]},{"mainAttempts",virtualStartMainAttempts},
   {"phase",successor.phase},{"ticket",successor.requestId},{"expiredTicket",observedTimeoutTicket},{"effects",effectCalls}};
  check(accepted.value("ok",false)&&disk()["input"]["revision"]==31&&inputHostTargetKey(disk()["input"])=="elite","explicit retry at the original deadline did not commit");
  check(virtualStartMainAttempts==2&&effectCalls.contains("input-refresh"),"successful control must reach the post-commit input consumer");
  check(successor.requestId!=0&&successor.requestId!=view.requestId&&successor.phase=="pending"&&observedTimeoutTicket==view.requestId,
   "success must use a fresh pending ticket after the original deadline");
  std::cout<<result(true).dump()<<'\n';return 0;
 }catch(const std::exception& error){auto report=result(false);report["error"]=error.what();std::cout<<report.dump()<<'\n';return 1;}
}
#endif

// SETTINGS_BASELINE_MONITOR_RECEIPT_VALIDATION
// One optional scope in the existing memory consumer; production implementations stay extracted.
static int runMonitorRootWriteScope(int argc,char** argv){
 if(argc!=3&&argc!=4)return 2;
 const std::string only=argc==4?argv[3]:"";
 const std::vector<std::string> names={"stale-handler-no-baseline","opaque-current-document","absent-handler-forgery","baseline-delta-handler","initialization-authority","backup-recovery-consumers","ordinary-native-section","damaged-init-no-invention","bounded-monitor-recovery"};
 if(!only.empty()&&std::find(names.begin(),names.end(),only)==names.end())return 2;
 json records=json::array();int checks=0;std::string active;
 const auto check=[&](bool yes,const char* message){++checks;if(!yes)throw std::runtime_error(message);};
 const json base={{"schemaVersion",1},{"music",{{"volume",0.2}}},{"future",{{"keep",true}}}};
 const json pending={{"pending",true},{"restartRequired",true},{"restart","unknown-sentinel"},{"via","deferred"},{"reason","steam-session-starting"},{"desired",{{"position",4}}},{"account","42"},{"path","C:/FixtureSteam/userdata/42/config/localconfig.vdf"},{"livePid",41},{"liveValues",{{"position",1},{"detail",2},{"scale",1.0},{"opacity",0.5},{"saturation",0.5}}},{"opaque",{{"token",17}}}};
 auto consumed=pending;consumed["pending"]=false;consumed["restart"]=false;consumed["reason"]="consumed";consumed["opaque"]["token"]=28;
 const auto call=[&](const json& next,const json* baseline=nullptr,bool initialize=false){json a={{"path",W2U(YM_SETTINGS_FILE)},{"content",next.dump()}};if(baseline)a["baseline"]=*baseline;if(initialize)a["initialize"]=true;return writeHandler(a);};
 try{registerWriteHandler();for(const auto& name:names){if(!only.empty()&&name!=only)continue;active=name;const auto started=checks;
  if(name=="stale-handler-no-baseline"){
   auto current=base;current["steamMonitor"]=consumed;seed(current);auto stale=base;stale["steamMonitor"]=pending;stale["music"]["volume"]=0.4;
   check(call(stale)==true,"actual settings.write rejected unrelated save");
   check(disk()["steamMonitor"]==consumed,"stale snapshot revived consumed native monitor");
   check(disk()["music"]["volume"]==0.4&&disk()["future"]==base["future"],"unrelated modules changed");
  }else if(name=="opaque-current-document"){
   for(const auto& value:std::vector<json>{consumed,nullptr,false,17,"opaque",json::array({1,"keep"})}){
    auto current=base;current["steamMonitor"]=value;seed(current);auto next=base;next["music"]["volume"]=0.5;
    check(ymSettingsWriteDocumentUnlocked(next.dump()),"direct writer failed");check(disk().contains("steamMonitor")&&disk()["steamMonitor"]==value,"native opaque current value lost");
   }
  }else if(name=="absent-handler-forgery"){
   for(int kind:{0,1,2}){seed(base);auto next=base;next["steamMonitor"]=kind==0?pending:kind==1?json(nullptr):json("forged");check(call(next)==true,"absence save failed");check(!disk().contains("steamMonitor"),"snapshot invented absent native monitor");}
  }else if(name=="baseline-delta-handler"){
   for(int kind:{0,1,2}){auto current=base;current["steamMonitor"]=consumed;current["future"]["concurrent"]=true;seed(current);auto baseline=base;baseline["steamMonitor"]=pending;auto next=baseline;next["music"]["volume"]=0.6;if(kind==1)next.erase("steamMonitor");if(kind==2)next["steamMonitor"]=nullptr;
    const auto response=call(next,&baseline);check(response.value("ok",false),"baseline handler rejected");check(disk()["steamMonitor"]==consumed,"baseline delta reset native monitor");check(json::parse(response.at("content").get<std::string>())==disk(),"receipt does not match committed document");check(disk()["future"]["concurrent"]==true&&disk()["music"]["volume"]==0.6,"baseline delta damaged siblings");}
   seed(base);auto next=base;next["steamMonitor"]=pending;check(call(next,&base).value("ok",false)&&!disk().contains("steamMonitor"),"baseline delta invented absent monitor");
  }else if(name=="initialization-authority"){
   const json empty=json::object();for(int kind:{0,1,2}){auto current=base;if(kind==0)current["steamMonitor"]=consumed;if(kind==2){files.clear();writes=0;}else seed(current);auto next=base;next["steamMonitor"]=pending;next["newSibling"]=27;
    const auto response=call(next,&empty,true);check(response.value("ok",false),"initialization handler failed");check(kind==0?disk()["steamMonitor"]==consumed:!disk().contains("steamMonitor"),"initialization created or reset native monitor");check(disk()["newSibling"]==27,"initialization lost unrelated defaults");}
  }else if(name=="backup-recovery-consumers"){
   check(steamMonitorSavedValid(pending),"formal pending receipt valid before recovery");auto backup=base;backup["steamMonitor"]=pending;auto expected=pending;expected["pending"]=false;expected["restartRequired"]=false;expected["via"]="unknown";expected["reason"]="recovered-state-unconfirmed";expected.erase("livePid");expected.erase("liveValues");
   for(int route:{0,1,2,3}){
    seed(backup);check(ymSettingsWriteSection("steamMonitor",consumed),"native preconsume section write failed");
    const auto bytes=files[YM_SETTINGS_BACKUP_FILE];check(json::parse(bytes)["steamMonitor"]==pending,"preconsume did not rotate old pending into backup");
    files[YM_SETTINGS_FILE]="{broken-main";const auto beforeRead=files;const auto recovered=ymSettingsReadUnlocked();
    check(!recovered["steamMonitor"].value("restartRequired",true)&&recovered["steamMonitor"].at("restart")=="unknown-sentinel","recovery must clear formal restartRequired and preserve unknown restart");
    check(steamMonitorSavedValid(recovered["steamMonitor"]),"actual saved consumer accepts recovered receipt");
    check(recovered["steamMonitor"]==expected,"real backup read revived pending or retained live proof");
    check(files==beforeRead,"recovery read mutated disk");check(recovered["music"]==backup["music"]&&recovered["future"]==backup["future"],"monitor recovery changed other modules");
    auto next=recovered;next["steamMonitor"]=pending;next["music"]["volume"]=0.7;
    if(route==0)check(ymSettingsWriteSection("music",{{"volume",0.7}}),"backup section writer failed");
    else if(route==1)check(ymSettingsWriteDocumentUnlocked(next.dump()),"direct backup writer failed");
    else{const auto response=call(next,&recovered,route==3);check(response.value("ok",false),"backup handler failed");check(json::parse(response.at("content").get<std::string>())==disk(),"backup handler receipt mismatch");}
    check(disk()["steamMonitor"]==expected,"recovered backup revived pending or retained live proof");check(files[YM_SETTINGS_BACKUP_FILE]==bytes,"backup original bytes changed");check(disk()["future"]==backup["future"],"recovery changed unrelated module");
   }
   for(const auto& value:std::vector<json>{nullptr,"opaque",json::array({1})}){auto next=base;next["steamMonitor"]=value;ymcc::settingsBaseline::deactivateRecoveredState(next);check(next["steamMonitor"]==value,"recovery altered non-object monitor");}
  }else if(name=="bounded-monitor-recovery"){
   boundedMonitorLarge=boundedMonitorArchive=true;
   for(int kind:{0,1,2})for(bool initialize:{false,true}){
    auto backup=base;if(kind==0)backup["steamMonitor"]=pending;if(kind==1)backup["steamMonitor"]=nullptr;
    files.clear();files[YM_SETTINGS_FILE]="oversize-controlled-leaf";files[YM_SETTINGS_BACKUP_FILE]=backup.dump();writes=writeAttempts=0;
    auto next=backup;next["steamMonitor"]=pending;next["music"]["volume"]=0.73;const auto oldBackup=files[YM_SETTINGS_BACKUP_FILE];
    const auto response=call(next,&backup,initialize);check(response.value("ok",false),"actual bounded handler refused supported recovery");
    if(kind==0){check(!disk()["steamMonitor"].value("pending",true),"bounded recovery must not revive executable pending");check(!disk()["steamMonitor"].value("restartRequired",true)&&steamMonitorSavedValid(disk()["steamMonitor"]),"bounded recovery receipt accepted by actual saved consumer");check(disk()["steamMonitor"]["restart"]=="unknown-sentinel"&&!disk()["steamMonitor"].contains("liveValues"),"bounded recovery preserved unknowns without live proof");}
    else if(kind==1)check(disk()["steamMonitor"].is_null(),"bounded recovery must preserve opaque source");
    else check(!disk().contains("steamMonitor"),"bounded backup absence cannot create native intent");
    check(files[YM_SETTINGS_BACKUP_FILE]==oldBackup&&disk()["future"]==base["future"],"bounded recovery changed backup bytes or sibling");
   }
   files.clear();files[YM_SETTINGS_FILE]="oversize-controlled-leaf";files[YM_SETTINGS_BACKUP_FILE]="{bad";auto next=base;next["steamMonitor"]=pending;const json empty=json::object();
   check(call(next,&empty,true).value("ok",false),"bounded damaged initialization failed");check(!disk().contains("steamMonitor"),"bounded initialization invented monitor authority");boundedMonitorLarge=boundedMonitorArchive=false;
  }else if(name=="ordinary-native-section"){
   seed(base);check(ymSettingsWriteSection("steamMonitor",pending)&&disk()["steamMonitor"]==pending,"native monitor section write blocked");check(ymSettingsWriteSection("steamMonitor",consumed)&&disk()["steamMonitor"]==consumed,"native monitor consumption blocked");check(ymSettingsPatchSection("steamMonitor",{{"pending",true},{"nativeOpaque",7}}),"native monitor patch blocked");auto native=disk()["steamMonitor"];check(native["pending"]==true&&native["nativeOpaque"]==7,"native patch not applied");check(ymSettingsWriteSection("music",{{"volume",0.8}})&&disk()["steamMonitor"]==native,"unrelated section reset monitor");
  }else if(name=="damaged-init-no-invention"){
   files.clear();files[YM_SETTINGS_FILE]="{bad-main";files[YM_SETTINGS_BACKUP_FILE]="{bad-backup";auto next=base;next["steamMonitor"]=pending;const json empty=json::object();check(call(next,&empty,true).value("ok",false),"damaged initialization failed");check(!disk().contains("steamMonitor"),"damaged initialization invented monitor");auto absent=base;ymcc::settingsBaseline::deactivateRecoveredState(absent);check(!absent.contains("steamMonitor"),"recovery helper created absent monitor");
  }
  records.push_back({{"name",name},{"pass",true},{"checks",checks-started}});
 }
 std::cout<<json({{"status","PASS"},{"scope","monitor-root-write"},{"cases",records.size()},{"checks",checks},{"caseRecords",records},{"actualSettingsWriteHandler",true},{"actualDocumentWriter",true},{"storage","existing-memoryFixture"},{"installedSettingsWritten",false},{"hardwareValidated",false}}).dump()<<'\n';return 0;
 }catch(const std::exception& e){records.push_back({{"name",active},{"pass",false},{"error",e.what()}});std::cout<<json({{"status","FAIL"},{"scope","monitor-root-write"},{"checks",checks},{"caseRecords",records},{"error",e.what()}}).dump()<<'\n';return 1;}
}

int run(int argc,char** argv){try{
 need(argc==2,"default snapshot input missing");std::ifstream input(argv[1]);json defaults;input>>defaults;int passed=0;
 auto pass=[&](const char* name){++passed;std::cout<<"PASS "<<name<<"\n";};
 json legacy={{"schemaVersion",1},{"ui",{{"theme","user"}}},{"input",{{"revision",17}}},{"future",{{"keep",true}}}};
 seed(legacy);auto next=defaults;next["future"]=legacy["future"];next["ui"]["theme"]="user";next["input"]["revision"]=17;
 need(ymSettingsWriteDocumentUnlocked(next.dump(),nullptr,&legacy),"legacy delta failed");need(disk()["future"]["keep"]==true,"lost unknown");need(disk()["configurationBaseline"]["revision"]==1,"no durable receipt");need(files[YM_SETTINGS_BACKUP_FILE]==legacy.dump(),"no exact before image");pass("legacy delta persists baseline, settings and exact backup");
 auto baseline=defaults;auto concurrentlyChanged=defaults;concurrentlyChanged["ui"]["theme"]="concurrent-user";concurrentlyChanged["input"]["revision"]=22;concurrentlyChanged["domainLogs"]={{"virtual",true}};
 seed(concurrentlyChanged);next=baseline;next["music"]["volume"]=0.41;
 need(ymSettingsWriteDocumentUnlocked(next.dump(),nullptr,&baseline),"concurrent delta failed");need(disk()["ui"]["theme"]=="concurrent-user","overwrote sibling");need(disk()["input"]["revision"]==22,"rolled back input");need(disk()["domainLogs"]["virtual"]==true,"lost native receipt");pass("migration delta preserves concurrent user/module and native-owned changes");
 for(int kind=0;kind<3;++kind){
  auto nativeState=defaults;const json fuse={{"schemaVersion",1},{"steamSession","41:123456"}};nativeState["deckyContextCleanup"]=fuse;seed(nativeState);
  next=defaults;next["music"]["volume"]=0.37;
  if(kind==1)next["deckyContextCleanup"]={{"schemaVersion",1},{"steamSession","99:999999"}};
  if(kind==2)next["deckyContextCleanup"]=nullptr;
  need(ymSettingsWriteDocumentUnlocked(next.dump()),"legacy unrelated settings write refused");
  need(disk()["deckyContextCleanup"]==fuse&&disk()["music"]["volume"]==0.37,"stale settings snapshot erased or rewrote Decky final cleanup fuse");
 }pass("legacy whole-document writes preserve Decky fuse against missing forged and null snapshots");
 {auto trusted=defaults;trusted["deckyContextCleanup"]={{"schemaVersion",1},{"steamSession","41:123456"}};seed(trusted);files[YM_SETTINGS_FILE]="{broken-main";files[YM_SETTINGS_BACKUP_FILE]=trusted.dump();
  next=defaults;next["music"]["volume"]=0.36;need(ymSettingsWriteDocumentUnlocked(next.dump()),"compatible backup cannot save unrelated settings");
  need(disk()["deckyContextCleanup"]==trusted["deckyContextCleanup"],"ordinary compatible-backup recovery erased Decky final cleanup fuse");
 }pass("ordinary compatible-backup recovery preserves Decky fuse without a renderer baseline");
 {
  ymcc::steamsession::Session session;session.root=L"C:\\FixtureSteam";session.pid=41;session.created=123456;session.account=9;
  seed(defaults);const auto before=defaults;need(deckySidebarReserveContextCleanup(session,[]{return true;}),"final cleanup reservation rejected before corruption");
  const auto written=disk();need(!ymcc::deckyrepair::cleanupAvailable(written.value("deckyContextCleanup",json()),session),"primary reservation not persisted");
  files[YM_SETTINGS_FILE]="{broken-after-reservation";
  const auto recovered=ymSettingsReadUnlocked();
  need(!ymcc::deckyrepair::cleanupAvailable(recovered.value("deckyContextCleanup",json()),session),"main corruption and old backup refilled already-spent final cleanup budget");
  auto stripped=recovered;stripped.erase("deckyContextCleanup");need(stripped==before,"cleanup backup synchronization changed user business configuration");
 }pass("actual cleanup reservation remains spent after main corruption and compatible-backup recovery");
 {
  ymcc::steamsession::Session session;session.root=L"C:\\FixtureSteam";session.pid=41;session.created=123456;session.account=9;
  for(int ordinal=1;ordinal<=3;++ordinal){seed(defaults);failWriteOrdinal=ordinal;need(!deckySidebarReserveContextCleanup(session,[]{return true;}),"partial cleanup receipt write authorized final dispatch");}
  seed(defaults);corruptWriteOrdinal=3;need(!deckySidebarReserveContextCleanup(session,[]{return true;}),"unverified backup receipt authorized final dispatch");
  seed(defaults);unsigned checks=0;need(!deckySidebarReserveContextCleanup(session,[&]{return ++checks<4;}),"OFF during receipt synchronization authorized final dispatch");
  seed(defaults);need(deckySidebarReserveContextCleanup(session,[]{return true;}),"receipt write failure permanently poisoned unrelated new attempt");
  const auto after=files;need(!deckySidebarReserveContextCleanup(session,[]{return true;})&&files==after,"same Steam process consumed another cleanup after receipt commit");
  auto nextSession=session;++nextSession.created;need(deckySidebarReserveContextCleanup(nextSession,[]{return true;}),"new Steam process incorrectly inherited previous process cleanup fuse");
  auto backed=json::parse(files.at(YM_SETTINGS_BACKUP_FILE));auto live=disk();backed.erase("deckyContextCleanup");live.erase("deckyContextCleanup");need(backed==defaults&&live==defaults,"cleanup metadata commit changed business settings in main or backup");
 }pass("actual cleanup pair-commit rejects each failed write corrupt readback and cancellation while preserving all business settings");
 seed(defaults);next=defaults;next.erase("configurationBaseline");need(ymSettingsWriteDocumentUnlocked(next.dump()),"old module write failed");need(disk().contains("configurationBaseline"),"old module erased baseline");pass("old full-document module writes cannot erase completed migration");
 seed(defaults);need(ymSettingsWriteSection("music",{{"folder","custom"}}),"module section failed");need(disk()["configurationBaseline"]==defaults["configurationBaseline"],"section erased receipt");need(ymSettingsPatchSection("ui",{{"custom","keep"}}),"module patch failed");pass("autonomous module section and patch writes preserve baseline");
 auto jsonWhole=defaults;jsonWhole["schemaVersion"]=1.0;jsonWhole["configurationBaseline"]["revision"]=1.0;seed(jsonWhole);need(ymSettingsWriteSection("music",{{"volume",0.31}}),"whole JSON numeric versions rejected");pass("whole JSON numeric versions match frontend integer semantics");
 int negatives=0;for(const auto& stamp:json::array({nullptr,json::array(),"future",json{{"id","other"},{"revision",1},{"legacyImported",true}},json{{"id","ymcc-settings"},{"revision",2},{"legacyImported",true}},json{{"id","ymcc-settings"},{"revision","1"},{"legacyImported",true}},json{{"id","ymcc-settings"},{"revision",1},{"legacyImported",false}}})){
 auto future=defaults;future["configurationBaseline"]=stamp;seed(future);auto before=files;
 need(!ymSettingsWriteDocumentUnlocked(defaults.dump()),"future document accepted");need(!ymSettingsWriteSection("ui",json::object()),"future section accepted");need(!ymSettingsPatchSection("music",json::object()),"future patch accepted");need(files==before&&writes==0&&admitted==0,"future caused writes");negatives+=3;}
 pass("21 future/malformed-baseline native writes reject before backup or admission");
 for(const auto& schema:json::array({99,-1,1.5,"future"})){auto future=defaults;future["schemaVersion"]=schema;seed(future);auto before=files;need(!ymSettingsWriteDocumentUnlocked(defaults.dump()),"future schema doc accepted");need(!ymSettingsWriteSection("ui",json::object()),"future schema section accepted");need(!ymSettingsPatchSection("music",json::object()),"future schema patch accepted");need(files==before&&writes==0,"future schema changed bytes");}pass("12 unsupported-document-schema writes reject without changes");
 seed(defaults);auto future=defaults;future["configurationBaseline"]["revision"]=2;files[YM_SETTINGS_FILE]="{broken";files[YM_SETTINGS_BACKUP_FILE]=future.dump();writes=0;auto before=files;json empty=json::object();need(!ymSettingsWriteDocumentUnlocked(defaults.dump(),nullptr,&empty),"future backup overwritten");need(files==before&&writes==0,"future backup caused corrupt archive");pass("unsupported backup blocks recovery before corrupt-file archiving");
 auto malformed=defaults;malformed["rtss"]=nullptr;seed(malformed);need(ymSettingsWriteDocumentUnlocked(defaults.dump(),nullptr,&malformed),"repair delta failed");need(disk()["rtss"].is_object(),"shape repair not durable");pass("malformed RTSS shape repairs in the actual native delta writer");
 auto brokenBackup=defaults;brokenBackup["rtss"]=nullptr;brokenBackup["future"]={{"preserve",true}};files.clear();files[YM_SETTINGS_BACKUP_FILE]=brokenBackup.dump();writes=0;auto recovered=brokenBackup;recovered["rtss"]=defaults["rtss"];need(ymSettingsWriteDocumentUnlocked(recovered.dump(),nullptr,&brokenBackup),"backup delta repair failed");need(disk()["rtss"].is_object()&&disk()["future"]["preserve"]==true,"backup delta not durable");need(files[YM_SETTINGS_BACKUP_FILE]==brokenBackup.dump(),"backup preimage changed");pass("actual native recovered-backup delta persists repairs without replacing its preimage");
 seed(defaults);failMainWrite=true;next=defaults;next["music"]["volume"]=0.22;need(!ymSettingsWriteDocumentUnlocked(next.dump(),nullptr,&defaults),"interrupted write accepted");need(disk()==defaults,"failed write changed main");failMainWrite=false;need(ymSettingsWriteDocumentUnlocked(next.dump(),nullptr,&defaults),"retry failed");need(disk()["music"]["volume"]==0.22,"retry did not commit");pass("interrupted atomic commit retains main and later retry succeeds");

 for(const auto& broken:std::vector<std::string>{"{broken","","[]"}){files.clear();files[YM_SETTINGS_FILE]=broken;files[YM_SETTINGS_BACKUP_FILE]="{broken-backup";writes=0;admitted=0;auto original=files;need(!ymSettingsWriteDocumentUnlocked(defaults.dump()),"corrupt native document overwritten");need(!ymSettingsWriteSection("ui",json::object()),"corrupt native section overwritten");need(!ymSettingsPatchSection("music",json::object()),"corrupt native patch overwritten");need(files==original&&writes==0,"corrupt originals changed");}pass("all native writers preserve existing corrupt/empty originals without a usable backup");
 files.clear();files[YM_SETTINGS_FILE]="{broken";files[YM_SETTINGS_BACKUP_FILE]=defaults.dump();writes=0;need(ymSettingsWriteSection("music",{{"volume",0.29}}),"native compatible-backup recovery failed");need(disk()["configurationBaseline"]==defaults["configurationBaseline"],"native backup lost baseline");pass("ordinary native module save can recover a compatible backup");
 files.clear();writes=0;need(ymSettingsWriteSection("music",{{"volume",0.2}}),"first-run native write blocked");pass("genuine absent first-run files still allow native defaults");
 auto protectedConfig=defaults;protectedConfig["input"]["revision"]=27;protectedConfig["fan"]["deviceIdentity"]="owned-module";protectedConfig["future"]={{"keep",true}};protectedConfig["rtss"]["frameRates"]["ac"]["fps"]=37;seed(protectedConfig);auto recovery=ymSettingsResetApplication(defaults);need(recovery["ok"]==true,"explicit recovery failed");need(disk()["input"]["revision"]==27&&disk()["fan"]["deviceIdentity"]=="owned-module"&&disk()["future"]["keep"]==true,"reset lost protected data");need(disk()["rtss"]==defaults["rtss"],"reset ignored RTSS");need(disk()["configurationBaseline"]==defaults["configurationBaseline"],"reset omitted baseline");need(disk()["tdp"]["autoApply"]["boot"]==false,"reset activates defaults");pass("explicit archived reset establishes baseline and retains protected modules");
 // Configuration-storm cases use the actual production writer/read bodies.
 for(const auto& badRevision:std::vector<json>{"bad",true,nullptr,json::array(),json::object(),18446744073709551615ULL}){
  auto bad=defaults;bad["input"]["revision"]=badRevision;bad["future"]={{"preserve",true}};seed(bad);
  auto repaired=defaults;repaired["future"]=bad["future"];need(ymSettingsWriteDocumentUnlocked(repaired.dump(),nullptr,&bad),"non-CAS normalized repair cannot persist a malformed input revision");
  need(disk()["input"]["revision"].is_number_integer()&&disk()["future"]["preserve"]==true,"repair lost valid siblings or retained malformed revision");
 }pass("malformed input revision can be repaired without inventing an expected CAS revision");
 seed(defaults);files[YM_SETTINGS_BACKUP_FILE]=defaults.dump();failMainRead=true;
 need(ymSettingsReadUnlocked()==defaults,"unreadable main prevents a healthy backup from being used");failMainRead=false;
 pass("sharing/access failure on main falls back to a readable compatible backup");
 failMainRead=true;failBackupRead=true;need(ymSettingsReadUnlocked().is_object(),"both unreadable settings abort native defaults");failMainRead=failBackupRead=false;
 pass("both unreadable sources return an empty default object without I/O mutation");
 auto unsupported=defaults;unsupported["schemaVersion"]=99;seed(unsupported);files[YM_SETTINGS_BACKUP_FILE]=defaults.dump();
 need(ymSettingsReadUnlocked()==defaults,"native reader consumes an unsupported main schema instead of a compatible backup");
 pass("unsupported native schema never becomes an active document");
 // Automatic recovery is an initialization transaction, with exact-byte archives.
 const json emptyBaseline=json::object();
 for(const auto& broken:std::vector<std::string>{"{broken","","[]","null"}){
  files.clear();files[YM_SETTINGS_FILE]=broken;files[YM_SETTINGS_BACKUP_FILE]="{broken-backup";archiveAttempts=0;
  auto requested=defaults;requested["startupDesired"]["virtualGamepadPersona"]="xbox";requested["startupDesired"]["fanControl"]=true;
  requested["performanceSchedule"]["enabled"]=true;requested["input"]["appliedRevision"]=23;requested["input"]["applyStatus"]="active";
  need(ymSettingsWriteDocumentUnlocked(requested.dump(),nullptr,&emptyBaseline,nullptr,true),"both-corrupt initialization does not self-heal");
  need(disk()["startupDesired"]["virtualGamepadPersona"]=="disabled"&&disk()["performanceSchedule"]["enabled"]==false,"recovery activates remembered hardware intent");
  need(disk()["input"]["appliedRevision"].is_null()&&disk()["input"]["applyStatus"]=="safe-stop","recovery fabricates an applied input receipt");
  bool savedMain=false,savedBackup=false;for(const auto& [file,value]:files){if(file.starts_with(YM_SETTINGS_FILE+L".before-auto-recovery-"))savedMain=value==broken;if(file.starts_with(YM_SETTINGS_BACKUP_FILE+L".before-auto-recovery-"))savedBackup=value=="{broken-backup";}
  need(savedMain&&savedBackup,"automatic recovery did not archive both exact originals including empty file");
  need(ymSettingsPatchSection("music",{{"volume",0.31}})&&disk()["music"]["volume"]==0.31,"recovered document cannot save the next ordinary edit");
 }pass("both-corrupt initialization archives exact main/backup and heals to inactive defaults");
 for(int fault:{1,2}){
  files.clear();files[YM_SETTINGS_FILE]="{bad-main";files[YM_SETTINGS_BACKUP_FILE]="{bad-backup";archiveAttempts=0;failArchiveAt=fault;
  need(!ymSettingsWriteDocumentUnlocked(defaults.dump(),nullptr,&emptyBaseline,nullptr,true),"archive refusal reported a recovered save");
  need(files[YM_SETTINGS_FILE]=="{bad-main"&&files[YM_SETTINGS_BACKUP_FILE]=="{bad-backup","failed archive changed source originals");
  failArchiveAt=0;archiveAttempts=0;need(ymSettingsWriteDocumentUnlocked(defaults.dump(),nullptr,&emptyBaseline,nullptr,true),"archive fault removal does not heal initialization");
 }pass("first and second archive failures preserve sources and explicit retry heals");
 files.clear();files[YM_SETTINGS_FILE]="{bad-main";files[YM_SETTINGS_BACKUP_FILE]="{bad-backup";archiveAttempts=0;failMainWrite=true;
 need(!ymSettingsWriteDocumentUnlocked(defaults.dump(),nullptr,&emptyBaseline,nullptr,true),"failed recovery commit reports success");
 need(files[YM_SETTINGS_FILE]=="{bad-main"&&files[YM_SETTINGS_BACKUP_FILE]=="{bad-backup","failed recovery commit changes source originals");
 failMainWrite=false;need(ymSettingsWriteDocumentUnlocked(defaults.dump(),nullptr,&emptyBaseline,nullptr,true),"recovery commit cannot retry after fault removal");pass("interrupted recovered commit preserves originals and can retry");
 for(bool futureMain:{false,true}){
  auto future=defaults;future["schemaVersion"]=99;files.clear();files[YM_SETTINGS_FILE]=futureMain?future.dump():"{bad-main";files[YM_SETTINGS_BACKUP_FILE]=futureMain?"{bad-backup":future.dump();writes=0;auto before=files;
  need(!ymSettingsWriteDocumentUnlocked(defaults.dump(),nullptr,&emptyBaseline,nullptr,true),"automatic initialization overwrites future schema");need(files==before&&writes==0,"future schema recovery mutated originals");
 }pass("automatic recovery cannot archive or replace a future main or backup");
 files.clear();files[YM_SETTINGS_FILE]="{bad-main";files[YM_SETTINGS_BACKUP_FILE]="{bad-backup";writes=0;auto corruptBefore=files;
 need(!ymSettingsWriteDocumentUnlocked(defaults.dump(),nullptr,&emptyBaseline,nullptr,true,0),"recovery invents an expected input revision");need(files==corruptBefore&&writes==0,"CAS recovery rejection still wrote files");pass("automatic recovery retains the input CAS proof requirement");
 failBackupRead=true;bool rejected=false;try{ymSettingsWriteDocumentUnlocked(defaults.dump(),nullptr,&emptyBaseline,nullptr,true);}catch(...){rejected=true;}failBackupRead=false;
 need(rejected&&files==corruptBefore,"unreadable backup becomes permission to reset");pass("unreadable original is not mistaken for corrupt reset permission");
 auto raced=defaults;raced["music"]["volume"]=0.73;seed(raced);need(ymSettingsWriteDocumentUnlocked(defaults.dump(),nullptr,&emptyBaseline,nullptr,true),"concurrent healthy initialization refused");need(disk()["music"]["volume"]==0.73,"late initialization overwrites a concurrent healthy document");pass("late initialization fills missing values without rolling back a repaired document");
 for(int writer:{0,1,2}){
  auto future=defaults;future["schemaVersion"]=99;
  seed(defaults);files[YM_SETTINGS_BACKUP_FILE]=future.dump();archiveAttempts=0;
  const auto save=[&](){if(writer==0){auto changed=defaults;changed["music"]["volume"]=0.47;return ymSettingsWriteDocumentUnlocked(changed.dump(),nullptr,&defaults);}if(writer==1)return ymSettingsWriteSection("music",{{"volume",0.47}});return ymSettingsPatchSection("music",{{"volume",0.47}});};
  need(save(),"healthy main cannot save beside a retained future backup");bool archived=false;
  for(const auto& [file,value]:files)if(file.starts_with(YM_SETTINGS_BACKUP_FILE+L".before-backup-replacement-"))archived=value==future.dump();
  need(archived&&disk()["music"]["volume"]==0.47,"ordinary writer silently replaces future backup bytes");
  seed(defaults);files[YM_SETTINGS_BACKUP_FILE]=future.dump();archiveAttempts=0;failArchiveAt=1;auto before=files;
  need(!save()&&files==before,"backup archive failure still changes the main or backup");failArchiveAt=0;
  failBackupRead=true;bool blocked=false;try{save();}catch(...){blocked=true;}failBackupRead=false;
  need(blocked&&files==before,"backup read failure falls through a parse catch and commits anyway");
 }pass("all three native writers preserve future backup and reject archive/access failures");
 std::string deep="{\"schemaVersion\":1,\"deep\":";for(int i=0;i<2000;++i)deep+="{\"next\":";deep+='0';deep.append(2000,'}');deep+='}';
 files.clear();files[YM_SETTINGS_FILE]=deep;need(ymSettingsReadUnlocked()==json::object(),"deep source becomes an active document instead of bounded defaults");
 need(ymSettingsWriteDocumentUnlocked(defaults.dump(),nullptr,&emptyBaseline,nullptr,true)&&disk()["input"]["applyStatus"]=="safe-stop","deep source cannot self-heal through an archived initialization");
 bool deepArchived=false;for(const auto& [file,bytes]:files)if(file.starts_with(YM_SETTINGS_FILE+L".before-auto-recovery-"))deepArchived=bytes==deep;
 need(deepArchived,"deep source is truncated or deleted instead of preserved");pass("deep documents have bounded parse/default/reconstruction and exact raw archives");
 registerWriteHandler();
 for(const auto* failed:{"wallpaper-preference","steam-controller-cursor","steam-menu-game","sleep","fxsound","input-refresh","shortcut-snapshot","shortcut-event","gesture"}){
  seed(defaults);auto submitted=defaults;submitted["music"]["volume"]=0.41;submitted["ui"]["fixtureChanged"]=true;
  effectCalls.clear();failingEffect=failed;
  const auto response=writeHandler({{"path",W2U(YM_SETTINGS_FILE)},{"content",submitted.dump()},{"baseline",defaults}});
  failingEffect.clear();need(response.value("ok",false)&&json::parse(response.value("content",std::string{}))==disk(),"postcommit callback failure loses the durable save receipt");
  need(disk()["music"]["volume"]==0.41&&response.contains("refreshWarnings"),"postcommit failure is hidden as a failed or warning-free save");
  need(effectCalls.contains("wallpaper-preference")&&effectCalls.contains("steam-controller-cursor")&&effectCalls.contains("steam-menu-game")&&effectCalls.contains("sleep")&&effectCalls.contains("fxsound")&&effectCalls.contains("input-refresh")&&effectCalls.contains("gesture"),"one failed consumer suppresses unrelated postcommit refreshes");
 }pass("actual settings.write handler returns durable receipts and isolated warnings for failed consumers");
 seed(defaults);auto staleInput=defaults;staleInput["input"]["revision"]=-1;staleInput["music"]["volume"]=0.43;effectCalls.clear();failingEffect="lifecycle-log";
 const auto logged=writeHandler({{"path",W2U(YM_SETTINGS_FILE)},{"content",staleInput.dump()},{"baseline",defaults}});failingEffect.clear();
 need(logged.value("ok",false)&&disk()["music"]["volume"]==0.43,"stale diagnostic logging failure hides committed save");pass("postcommit stale-guard diagnostics cannot invalidate the committed transaction");
 seed(defaults);failMainWrite=true;effectCalls.clear();const auto refused=writeHandler({{"path",W2U(YM_SETTINGS_FILE)},{"content",defaults.dump()},{"baseline",defaults}});failMainWrite=false;
 need(!refused.value("ok",true)&&effectCalls.empty(),"failed disk commit runs runtime consumers or reports success");pass("failed transaction emits neither successful receipt nor runtime refresh");
 for(const auto* failed:{"fxsound","gesture"}){
  seed(defaults);effectCalls.clear();failingEffect=failed;
  const auto response=resetHandler({{"path",W2U(YM_SETTINGS_FILE)},{"confirm","reset-application-settings"},{"defaults",defaults}});failingEffect.clear();
  need(response.value("ok",false)&&response.contains("refreshWarnings")&&response["backups"].size()==1,"explicit reset loses successful commit receipt on refresh exception");
  need(effectCalls.contains("fxsound")&&effectCalls.contains("gesture")&&disk()["tdp"]["autoApply"]["boot"]==false,"explicit reset refresh failures block another consumer or activate hardware");
 }pass("actual settings.reset preserves successful durable receipt across runtime refresh failures");
 const auto ioChecks=settingsIoFixture::run(defaults);pass("actual Win32 settings IO encoding/size/sharing and timed-lock recovery");
 std::cout<<"{\"status\":\"PASS\",\"cases\":"<<passed<<",\"ioChecks\":"<<ioChecks<<",\"negativeWrites\":"<<negatives+12<<",\"storage\":\"memory-and-isolated-win32-io\",\"installedSettingsWritten\":false,\"hardwareValidated\":false}\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}

}
#endif

#if defined(YMCC_SETTINGS_SERIAL_CONFIG_SCOPE)
namespace serialConfigFixture {
struct Fence {
 unsigned long long revision=GamepadSerialEvent::captureRevision();
 unsigned long long required=GamepadSerialEvent::RulesRequired.load(std::memory_order_acquire);
 unsigned long long applied=GamepadSerialEvent::RulesApplied.load(std::memory_order_acquire);
 unsigned writers=GamepadSerialEvent::ConfigurationWriters.load(std::memory_order_acquire);
};
struct Result {
 json caseRecords=json::array();unsigned assertions=0;std::size_t next=0;
 void need(bool value,const char* why){++assertions;if(!value)throw std::runtime_error(why);}
 template<class Body> bool check(Body body){
  if(next>=kSerialConfigCaseCount)throw std::runtime_error("serial-config case array exhausted");
  json record={{"name",kSerialConfigCaseNames[next++]},{"pass",false},{"assertions",0}};const auto start=assertions;
  try{body();record["pass"]=true;}catch(const std::exception& error){record["error"]=error.what();}
  record["assertions"]=assertions-start;const bool passed=record["pass"].get<bool>();caseRecords.push_back(std::move(record));return passed;
 }
};
struct ReadHandle {
 HANDLE value=INVALID_HANDLE_VALUE;
 explicit ReadHandle(const std::wstring& path,DWORD sharing):value(::CreateFileW(path.c_str(),GENERIC_READ,sharing,nullptr,OPEN_EXISTING,0,nullptr)){
  if(value==INVALID_HANDLE_VALUE)throw std::runtime_error("owned sharing-denial handle unavailable");
 }
 ~ReadHandle(){if(value!=INVALID_HANDLE_VALUE)::CloseHandle(value);}
 ReadHandle(const ReadHandle&)=delete;ReadHandle& operator=(const ReadHandle&)=delete;
};
static json document(const json& defaults){
 auto value=defaults;
 value["input"]["revision"]=17;
 value["input"]["buttonMapping"]={{"rules",{{"shortcutRulesInitialized",true},{"shortcutRules",json::array({
  {{"id","serial-config-owned"},{"origin","custom"},{"enabled",true},{"actionId","window.summon"},
   {"inputs",json::array({{{"source","controller"},{"code","a"}}})},{"trigger","press"},{"params",json::object()}}
 })}}}};
 return value;
}
static json changedRules(const json& before){auto next=before;next["input"]["revision"]=18;next["input"]["buttonMapping"]["rules"]["shortcutRules"][0]["inputs"][0]["code"]="b";return next;}
struct MemoryStore {
 const std::wstring& mainPath()const{return memoryFixture::YM_SETTINGS_FILE;}
 void seed(const json& value)const{memoryFixture::seed(value);}
 std::string mainRaw()const{return memoryFixture::ymSettingsReadRaw(memoryFixture::YM_SETTINGS_FILE);}
 std::string backupRaw()const{return memoryFixture::ymSettingsReadRaw(memoryFixture::YM_SETTINGS_BACKUP_FILE);}
 bool save(unsigned route,const json& before,const json& next,const char* section,long long expected=-1,bool* conflict=nullptr)const{
  if(route==0)return memoryFixture::ymSettingsWriteDocumentUnlocked(next.dump(),nullptr,&before,nullptr,false,expected,conflict);
  if(route==1)return memoryFixture::ymSettingsWriteSection(section,next.at(section));
  return memoryFixture::ymSettingsPatchSection(section,next.at(section));
 }
 bool denied(unsigned route,const json& before,const json& next)const{
  struct Restore{bool previous=memoryFixture::failMainWrite;~Restore(){memoryFixture::failMainWrite=previous;}} restore;
  memoryFixture::failMainWrite=true;return save(route,before,next,"input");
 }
};
struct Win32Store {
 const std::wstring& mainPath()const{return settingsIoFixture::YM_SETTINGS_FILE;}
 void put(const std::wstring& path,const json& value)const{
  const auto bytes=value.dump();std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(bytes.data(),bytes.size());output.close();
  if(output.fail())throw std::runtime_error("serial-config owned file seed failed");
 }
 void seed(const json& value)const{put(mainPath(),value);put(settingsIoFixture::YM_SETTINGS_BACKUP_FILE,value);}
 std::string mainRaw()const{return settingsIoFixture::ymSettingsReadRaw(mainPath());}
 std::string backupRaw()const{return settingsIoFixture::ymSettingsReadRaw(settingsIoFixture::YM_SETTINGS_BACKUP_FILE);}
 bool save(unsigned route,const json& before,const json& next,const char* section,long long expected=-1,bool* conflict=nullptr)const{
  if(route==0)return settingsIoFixture::ymSettingsWriteDocument(next.dump(),nullptr,&before,nullptr,false,expected,conflict);
  if(route==1)return settingsIoFixture::ymSettingsWriteSection(section,next.at(section));
  return settingsIoFixture::ymSettingsPatchSection(section,next.at(section));
 }
 bool denied(unsigned route,const json& before,const json& next)const{
  ReadHandle held(mainPath(),FILE_SHARE_READ);return save(route,before,next,"input");
 }
};
static void observed(Result& result,const AtomicObservation& observation,unsigned writers,unsigned long long required,const json& rules){
 result.need(!observation.lost,"atomic observation lost a sample");
 result.need(observation.samples.size()==1,"exactly one main atomic entry must be observed");
 for(const auto& sample:observation.samples){
  result.need(sample.at("writers").get<unsigned>()==writers,"actual writer active/inactive state differs");
  result.need(sample.at("required").get<unsigned long long>()==required,"publication occurred before main atomic entry");
  result.need(sample.at("rules")==rules,"actual atomic payload rule snapshot differs");
 }
}
template<class Store> static bool withoutPublication(Result& result,const json& defaults,const Store& store){
 for(unsigned route=0;route<3;++route)if(!result.check([&]{
  const auto before=document(defaults);auto next=before;next["music"]["volume"]=0.371;
  store.seed(before);const Fence start;const auto rules=gamepadSerialActionRuleSnapshot(before);
  result.need(rules.is_array()&&!rules.empty(),"seed must expose actual effective rules");
  result.need(GamepadSerialEvent::currentRevision(start.revision),"nonpublication cases require initially admitted revision");
  AtomicObservation observation{store.mainPath()};bool saved=false;
  {ObservationLease lease(observation);saved=store.save(route,before,next,"music");}
  result.need(saved,"unrelated actual writer refused");observed(result,observation,0,start.required,rules);
  const Fence finish;
  result.need(finish.revision==start.revision&&finish.required==start.required&&finish.applied==start.applied&&finish.writers==0,"unrelated section revoked or published");
  result.need(GamepadSerialEvent::currentRevision(start.revision),"unrelated section retired an admitted revision");
  const auto committed=json::parse(store.mainRaw());result.need(committed.at("music").at("volume")==next.at("music").at("volume"),"unrelated commit missing");
  result.need(gamepadSerialActionRuleSnapshot(committed)==rules,"unrelated commit changed rules");
 }))return false;
 if(!result.check([&]{
  const auto before=document(defaults),next=changedRules(before);store.seed(before);const Fence start;
  const auto mainRaw=store.mainRaw(),backupRaw=store.backupRaw();bool conflict=false,saved=false;AtomicObservation observation{store.mainPath()};
  {ObservationLease lease(observation);saved=store.save(0,before,next,"input",16,&conflict);}
  result.need(!saved&&conflict,"actual document CAS mismatch was not rejected");
  result.need(store.mainRaw()==mainRaw&&store.backupRaw()==backupRaw,"CAS rejection changed main/backup bytes");
  result.need(!observation.lost&&observation.samples.empty(),"CAS rejection entered main atomic writer");
  const Fence finish;
  result.need(finish.revision==start.revision&&finish.required==start.required&&finish.applied==start.applied&&finish.writers==0,"CAS rejection changed action fence");
  result.need(GamepadSerialEvent::currentRevision(start.revision),"CAS rejection revoked current action");
 }))return false;
 for(unsigned route=0;route<3;++route)if(!result.check([&]{
  const auto before=document(defaults),next=changedRules(before);store.seed(before);const auto rawBefore=store.mainRaw();const Fence start;
  result.need(GamepadSerialEvent::currentRevision(start.revision),"failure cases require an initially admitted revision");
  AtomicObservation observation{store.mainPath()};bool saved=false;
  {ObservationLease lease(observation);saved=store.denied(route,before,next);}
  result.need(!saved,"denied atomic replacement reported success");
  observed(result,observation,1,start.required,gamepadSerialActionRuleSnapshot(next));
  const Fence finish;
  result.need(store.mainRaw()==rawBefore,"failed atomic replacement changed main bytes");
  result.need(finish.revision==start.revision+2&&finish.required==start.required&&finish.applied==start.applied&&finish.writers==0,"failed active writer did not unwind without publication");
  result.need(!GamepadSerialEvent::currentRevision(start.revision),"failed active writer left old revision live");
  result.need(GamepadSerialEvent::currentRevision(finish.revision),"failed commit invented missing-Applied debt");
  result.need(gamepadSerialActionRuleSnapshot(json::parse(store.mainRaw()))==gamepadSerialActionRuleSnapshot(before),"failed write changed durable effective rules");
 }))return false;
 return true;
}
static bool sourceReceipts(Result& result,const json& defaults,const Win32Store& store){
 auto primary=document(defaults),backup=changedRules(primary);backup["input"]["revision"]=9;
 backup["input"]["buttonMapping"]["rules"]["shortcutRules"][0]["id"]="serial-config-backup-source";
 const auto read=[&](bool& primaryRead){return settingsIoFixture::ymSettingsFields("input",{"revision","buttonMapping"},&primaryRead);};
 if(!result.check([&]{
  store.seed(primary);store.put(settingsIoFixture::YM_SETTINGS_BACKUP_FILE,backup);const Fence start;bool primaryRead=false;
  const auto fields=read(primaryRead);result.need(primaryRead,"healthy primary did not issue primary receipt");
  result.need(fields.at("revision")==17&&fields.at("buttonMapping")==primary.at("input").at("buttonMapping"),"primary read returned another source");
  const Fence finish;result.need(finish.revision==start.revision&&finish.required==start.required&&finish.applied==start.applied&&finish.writers==0,"primary read changed action fence");
 }))return false;
 if(!result.check([&]{
  const Fence start;ReadHandle held(store.mainPath(),0);bool primaryRead=true;const auto fields=read(primaryRead);
  result.need(!primaryRead,"backup fallback retained a primary receipt");
  result.need(fields.at("revision")==9&&fields.at("buttonMapping")==backup.at("input").at("buttonMapping"),"primary sharing denial did not select actual backup");
  const Fence finish;result.need(finish.revision==start.revision&&finish.required==start.required&&finish.applied==start.applied&&finish.writers==0,"backup read changed action fence");
 }))return false;
 if(!result.check([&]{
  const Fence start;ReadHandle primaryHeld(store.mainPath(),0),backupHeld(settingsIoFixture::YM_SETTINGS_BACKUP_FILE,0);
  bool primaryRead=true;const auto fields=read(primaryRead);
  result.need(!primaryRead&&fields==json::object(),"both-source sharing denial returned stale fields/primary receipt");
  const Fence finish;result.need(finish.revision==start.revision&&finish.required==start.required&&finish.applied==start.applied&&finish.writers==0,"failed read acknowledged/published rules");
 }))return false;
 return true;
}
template<class Store> static bool withPublication(Result& result,const json& defaults,const Store& store){
 for(unsigned route=0;route<3;++route)if(!result.check([&]{
  const auto before=document(defaults),next=changedRules(before);store.seed(before);const Fence start;
  const auto nextRules=gamepadSerialActionRuleSnapshot(next);
  result.need(gamepadSerialActionRulesDiffer(gamepadSerialActionRuleSnapshot(before),next),"requested rules must actually differ");
  AtomicObservation observation{store.mainPath()};bool saved=false;
  {ObservationLease lease(observation);saved=store.save(route,before,next,"input");}
  result.need(saved,"actual rules writer failed");observed(result,observation,1,start.required,nextRules);
  const Fence finish;
  result.need(gamepadSerialActionRuleSnapshot(json::parse(store.mainRaw()))==nextRules,"committed rule snapshot differs");
  result.need(finish.revision==start.revision+3&&finish.required==start.required+1&&finish.applied==start.applied&&finish.writers==0,"actual commit did not revoke/publish/unwind exactly once");
  result.need(!GamepadSerialEvent::currentRevision(start.revision),"successful rules commit retained old revision");
  result.need(finish.required!=finish.applied,"commit unexpectedly claimed parser Applied");
  result.need(GamepadSerialEvent::BoundRevision==0&&GamepadSerialEvent::captureRevision()==finish.revision,"fresh admission captured inherited/stale revision");
  result.need(!GamepadSerialEvent::currentRevision(finish.revision),"actual admission predicate accepted fresh revision without Applied");
  result.need(GamepadSerialEvent::currentRevision(0),"ordinary state notification lost zero-revision exemption");
 }))return false;
 return true;
}
static int runScope(int argc,char** argv){
 Result result;std::filesystem::path owned;bool created=false,removed=false,ok=false;std::string failure,cleanupFailure;
 try{
  result.need(argc==4,"serial-config requires defaults, exact scope and fresh owned root");
  result.need(std::string(argv[2])=="--serial-config","serial-config native mode mismatch");
  std::ifstream input(argv[1]);json defaults;input>>defaults;
  result.need(!input.fail()&&ymcc::settingsBaseline::supported(defaults),"serial-config defaults missing/unsupported");
  owned=std::filesystem::path(argv[3]);result.need(owned.is_absolute()&&!std::filesystem::exists(owned),"serial-config root must be fresh and absolute");
  result.need(std::filesystem::create_directory(owned),"serial-config owned root creation failed");created=true;
  settingsIoFixture::YM_SETTINGS_FILE=(owned/L"yeman-settings.json").wstring();settingsIoFixture::YM_SETTINGS_BACKUP_FILE=settingsIoFixture::YM_SETTINGS_FILE+L".bak";
  result.need(GamepadSerialEvent::BoundRevision==0&&GamepadSerialEvent::ConfigurationWriters.load()==0,"scope did not start outside an action/write");
  MemoryStore memory;Win32Store win32;
  ok=withoutPublication(result,defaults,memory)&&withoutPublication(result,defaults,win32)&&sourceReceipts(result,defaults,win32)&&withPublication(result,defaults,memory)&&withPublication(result,defaults,win32);
  if(ok){result.need(result.next==kSerialConfigCaseCount&&result.caseRecords.size()==kSerialConfigCaseCount,"serial-config selection incomplete");
   for(std::size_t i=0;i<kSerialConfigCaseCount;++i){result.need(result.caseRecords[i].at("name")==kSerialConfigCaseNames[i],"serial-config case order drift");for(std::size_t j=0;j<i;++j)result.need(std::string(kSerialConfigCaseNames[i])!=kSerialConfigCaseNames[j],"duplicate serial-config case name");}
  }
 }catch(const std::exception& error){ok=false;failure=error.what();}
 if(created)try{
  for(const auto& entry:std::filesystem::directory_iterator(owned)){
   if(!entry.is_regular_file()||entry.is_symlink())throw std::runtime_error("unexpected serial-config owned child; cleanup stopped");
   if(!std::filesystem::remove(entry.path()))throw std::runtime_error("serial-config owned file cleanup failed");
  }
  removed=std::filesystem::remove(owned)&&!std::filesystem::exists(owned);
  if(!removed)throw std::runtime_error("serial-config owned directory cleanup failed");
 }catch(const std::exception& error){ok=false;cleanupFailure=error.what();}
 if(!created)removed=true;
 json summary={{"status",ok&&removed?"PASS":"FAIL"},{"scope","serial-config-storage"},{"caseRecords",result.caseRecords},{"cases",result.caseRecords.size()},{"assertions",result.assertions},
  {"ownedIoCreated",created},{"ownedIoRemoved",removed},{"installedSettingsWritten",false},{"hardwareValidated",false},
  {"publicationWhileWriterActiveObserved",false},{"actualQueuedWorkerExecuted",false},{"actualParserExecuted",false}};
 if(!failure.empty())summary["error"]=failure;if(!cleanupFailure.empty())summary["cleanupError"]=cleanupFailure;
 std::cout<<summary.dump()<<'\n';return ok&&removed?0:1;
}
}
#endif

#if !defined(YMCC_SETTINGS_SECTION_CACHE_SCOPE)
namespace memoryFixture {
static int runInputRuntimePowerScope(int argc,char** argv){
 int checks=0;json records=json::array();std::string active,error;std::filesystem::path owned;bool created=false,removed=true;
 const auto check=[&](bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);};
 try{
  check(argc==3||argc==4,"input-runtime-power requires exact original scope");
  const std::string only=argc==4?argv[3]:"all";
  const std::vector<std::string> names={"held-before-commit","sticky-aba","current-runtime","stale-handler","same-generation-sequence","invalid-runtime-fields","ordinary-durable-suspended","revision-conflict","win32-flush-revoked","win32-retry-revoked","win32-fallback-revoked"};
  check(only=="all"||std::find(names.begin(),names.end(),only)!=names.end(),"unknown original input-runtime-power case");
  std::ifstream in(argv[1]);json defaults;in>>defaults;check(!in.fail(),"input power defaults unavailable");registerWriteHandler();
  const auto request=[&]{auto before=defaults;before["input"]["revision"]=30;seed(before);auto next=before;next["input"]["revision"]=31;next["input"]["gameOverride"]={{"identity","owned-input-runtime"}};return json{{"path",W2U(YM_SETTINGS_FILE)},{"content",next.dump()},{"baseline",before},{"expectedInputRevision",30},{"inputRuntimePower",{{"generation",7},{"observedNotificationSequence",19}}}};};
  for(const auto& name:names){if(only!="all"&&only!=name)continue;active=name;const int start=checks;inputRuntimePowerFixture::ready();auto args=request();const auto before=disk();effectCalls.clear();
   if(name=="held-before-commit"||name=="sticky-aba"){
    std::promise<void> entered,released;auto release=released.get_future().share();beforeTargetAdmit=[&]{entered.set_value();release.wait();};
    auto work=std::async(std::launch::async,[&]{return writeHandler(args);});const bool held=entered.get_future().wait_for(std::chrono::seconds(5))==std::future_status::ready;
    inputRuntimePowerFixture::revoke(name=="sticky-aba");released.set_value();const auto response=work.get();beforeTargetAdmit={};check(held,"actual settings handler did not reach held target admission");
    check(!response.value("ok",false),"revoked runtime overlay committed after Native admission");check(response.value("reason","")=="input-runtime-power-superseded","runtime cancellation lost typed result");check(disk()==before,"revoked runtime overlay changed actual Native document");check(writes==0&&effectCalls.empty(),"cancelled precommit had write/refresh effects");
   }else if(name=="current-runtime"){
    auto response=writeHandler(args);check(response.value("ok",false),"current runtime input refused");check(disk()["input"]["revision"]==31,"runtime revision not committed");check(disk()["input"]["gameOverride"]["identity"]=="owned-input-runtime","runtime payload lost");check(files.at(YM_SETTINGS_BACKUP_FILE)==before.dump(),"runtime backup differs from exact preimage");
   }else if(name=="stale-handler"||name=="same-generation-sequence"){
    if(name=="stale-handler")inputRuntimePowerFixture::revoke();else {std::lock_guard<std::mutex> lock(inputRuntimePowerFixture::gate);++inputRuntimePowerFixture::value.observedNotificationSequence;}
    auto response=writeHandler(args);check(!response.value("ok",false)&&response.value("reason","")=="input-runtime-power-superseded","stale runtime scope accepted");check(writes==0&&admitted==0&&disk()==before&&effectCalls.empty(),"stale request acquired device/write/refresh");
   }else if(name=="invalid-runtime-fields"){
    const json bad=json::array({nullptr,json::array(),json::object(),{{"generation",0},{"observedNotificationSequence",19}},{{"generation",7},{"observedNotificationSequence",19.5}},{{"generation","7"},{"observedNotificationSequence",19}},{{"generation",7},{"observedNotificationSequence",-1}},{{"generation",9007199254740992ULL},{"observedNotificationSequence",19}}});
    for(const auto& value:bad){args["inputRuntimePower"]=value;auto response=writeHandler(args);check(!response.value("ok",false)&&response.value("reason","")=="invalid-input-runtime-power","bad runtime field granted power");check(writes==0&&admitted==0&&disk()==before,"bad runtime field wrote");}
    args=request();args.erase("baseline");auto response=writeHandler(args);check(!response.value("ok",false)&&disk()==before,"runtime without baseline wrote");args=request();args["initialize"]=true;response=writeHandler(args);check(!response.value("ok",false)&&disk()==before,"runtime initialization accepted");
   }else if(name=="ordinary-durable-suspended"){
    inputRuntimePowerFixture::revoke();args.erase("inputRuntimePower");auto response=writeHandler(args);check(response.value("ok",false),"ordinary durable settings improperly power-gated");check(disk()["input"]["revision"]==31,"ordinary durable revision missing");
   }else if(name=="revision-conflict"){
    args["expectedInputRevision"]=29;auto response=writeHandler(args);check(!response.value("ok",false)&&response.value("reason","")=="revision-conflict","runtime bypassed original revision guard");check(writes==0&&admitted==0&&disk()==before,"conflicting runtime wrote");
   }else{
    if(!created){const DWORD n=GetEnvironmentVariableW(L"YMCC_BASELINE_EVIDENCE",nullptr,0);check(n>1&&n<32768,"owned native input evidence unavailable");std::wstring base(n,L'\0');const auto got=GetEnvironmentVariableW(L"YMCC_BASELINE_EVIDENCE",base.data(),n);check(got>0&&got<n,"owned evidence changed");base.resize(got);owned=std::filesystem::path(base)/L"input-runtime-owned";check(owned.is_absolute()&&!std::filesystem::exists(owned),"owned input root not fresh");check(std::filesystem::create_directory(owned),"owned input root creation failed");created=true;removed=false;}
    settingsIoFixture::YM_SETTINGS_FILE=(owned/L"yeman-settings.json").wstring();settingsIoFixture::YM_SETTINGS_BACKUP_FILE=settingsIoFixture::YM_SETTINGS_FILE+L".bak";
    const auto put=[&](const std::wstring& file,const std::string& raw){std::ofstream out(file,std::ios::binary|std::ios::trunc);out<<raw;out.close();check(!out.fail(),"owned settings seed failed");};put(settingsIoFixture::YM_SETTINGS_FILE,before.dump());put(settingsIoFixture::YM_SETTINGS_BACKUP_FILE,before.dump());
    const auto get=[&]{std::ifstream input(settingsIoFixture::YM_SETTINGS_FILE,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});};
    ymcc::sleeppower::SharedPowerV1 captured;check(captureSharedPowerLifecycle(captured),"actual public power capture failed");auto cancellation=observeSharedPowerRevocation(captured);check(static_cast<bool>(cancellation),"actual revocation registration failed");
    const auto current=[&]{return !cancellation.cancelled()&&sharedPowerLifecycleCurrent(captured);};unsigned mainMoves=0,replaces=0,mainFlushes=0;
    settingsIoFixture::inputFlush=[&](HANDLE h){wchar_t buffer[32768];const auto got=GetFinalPathNameByHandleW(h,buffer,32768,FILE_NAME_NORMALIZED);if(got&&got<32768&&std::wstring(buffer,got).find(L"yeman-settings.json.ymcc.")!=std::wstring::npos){++mainFlushes;if(name=="win32-flush-revoked")inputRuntimePowerFixture::revoke(true);}};
    settingsIoFixture::inputMove=[&](const wchar_t* to){if(std::wstring(to)!=settingsIoFixture::YM_SETTINGS_FILE)return false;++mainMoves;if(name=="win32-retry-revoked"){inputRuntimePowerFixture::revoke(true);return true;}if(name=="win32-fallback-revoked"){if(mainMoves==6)inputRuntimePowerFixture::revoke(true);return true;}return false;};
    settingsIoFixture::inputReplace=[&](const wchar_t* to){if(std::wstring(to)==settingsIoFixture::YM_SETTINGS_FILE)++replaces;};
    json committed;const bool saved=settingsIoFixture::ymSettingsWriteDocument(args["content"].get<std::string>(),nullptr,&before,&committed,false,30,nullptr,nullptr,false,current);
    settingsIoFixture::inputFlush={};settingsIoFixture::inputMove={};settingsIoFixture::inputReplace={};
    check(!saved,"revoked runtime reached actual Win32 replace");check(get()==before.dump(),"Win32 runtime cancellation changed original main");check(mainFlushes==1,"actual main temporary flush not reached exactly once");check(replaces==0,"revoked runtime entered ReplaceFile fallback");
    check(mainMoves==(name=="win32-flush-revoked"?0:name=="win32-retry-revoked"?1:6),"runtime replayed old atomic move after revocation");
    for(const auto& item:std::filesystem::directory_iterator(owned))check(item.path().extension()!=L".tmp","cancelled atomic transaction left its temporary");
   }
   records.push_back({{"name",name},{"pass",true},{"assertions",checks-start}});
  }
 }catch(const std::exception& e){error=e.what();}
 beforeTargetAdmit={};settingsIoFixture::inputFlush={};settingsIoFixture::inputMove={};settingsIoFixture::inputReplace={};
 if(created)try{for(const auto& item:std::filesystem::directory_iterator(owned)){if(!item.is_regular_file()||item.is_symlink())throw std::runtime_error("unexpected input runtime cleanup child");std::filesystem::remove(item.path());}removed=std::filesystem::remove(owned);}catch(const std::exception& e){error+="; cleanup:"+std::string(e.what());}
 json result={{"status",error.empty()&&removed?"PASS":"FAIL"},{"scope","input-runtime-power"},{"caseRecords",records},{"cases",records.size()},{"assertions",checks},{"ownedIoCreated",created},{"ownedIoRemoved",removed},{"installedSettingsWritten",false},{"hardwareValidated",false}};
 if(!error.empty())result["error"]=error;result["activeCase"]=active;std::cout<<result.dump()<<'\n';return error.empty()&&removed?0:1;
}
}
#endif

int main(int argc,char** argv){
#if defined(YMCC_SETTINGS_VIRTUAL_START_WRITE_FAILURE_SCOPE)
 return memoryFixture::runVirtualStartWriteFailureScope(argc,argv);
#endif
#if !defined(YMCC_SETTINGS_SECTION_CACHE_SCOPE)
 if(argc>=3&&std::string(argv[2])=="--input-runtime-power")return memoryFixture::runInputRuntimePowerScope(argc,argv);
#endif
#if defined(YMCC_SETTINGS_MONITOR_ROOT_WRITE_SCOPE)
#if defined(YMCC_SETTINGS_SERIAL_CONFIG_SCOPE) || defined(YMCC_SETTINGS_SECTION_CACHE_SCOPE)
#error monitor-root-write requires an exclusive scope
#endif
 if(argc>=3&&std::string(argv[2])=="--monitor-root-write")return memoryFixture::runMonitorRootWriteScope(argc,argv);
 return 2;
#else
#if defined(YMCC_SETTINGS_SERIAL_CONFIG_SCOPE)
 if(argc>=3&&std::string(argv[2])=="--serial-config")return serialConfigFixture::runScope(argc,argv);
 std::cerr<<"FAIL serial-config exact scope required\n";return 2;
#else
#if defined(YMCC_SETTINGS_SECTION_CACHE_SCOPE)
 if(argc>=3&&std::string(argv[2])=="--section-cache")return settingsIoFixture::runSectionCacheScope(argc,argv);
 return 2;
#else
 if(argc>=3&&std::string(argv[2])=="--cleanup-history")return settingsIoFixture::runCleanupHistoryScope(argc,argv);
 if(argc>=3&&std::string(argv[2])=="--upgrade-boundaries")return settingsIoFixture::runUpgradeBoundaries(argc,argv);
 try{SettingsCleanupFrontendFixture frontend;const int code=memoryFixture::run(argc,argv);frontend.close();return code;}
 catch(const std::exception& error){std::cerr<<"FAIL cleanup frontend "<<error.what()<<'\n';return 1;}
#endif
#endif
#endif // YMCC_SETTINGS_MONITOR_ROOT_WRITE_SCOPE
}
