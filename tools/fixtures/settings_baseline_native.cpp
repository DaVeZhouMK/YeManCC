// The runner appends actual storage consumers extracted from native/main.cpp.
// Memory transactions and host/device admission are inert; a separate owned
// Win32 IO consumer tests real file, encoding and lock failures. Production
// JSON merge/guards/writer bodies and the actual settings.write handler run.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <thread>
#include <future>
#include <mutex>
#include <chrono>
#include "settings_baseline.h"
#include "controller_persona_settings.h"
#include "boot_mode_shortcuts.h"
#include "decky_sidebar_manual_repair.h"
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
// SETTINGS_BASELINE_PRODUCTION_IO
#if !defined(YMCC_SETTINGS_SECTION_CACHE_SCOPE)
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
static int failWriteOrdinal=0,corruptWriteOrdinal=0,writeAttempts=0;
static int failArchiveAt=0,archiveAttempts=0;
static bool failMainRead=false,failBackupRead=false;
struct SettingsFileGuard {};
static std::string ymSettingsReadRaw(const std::wstring& file){if((file==YM_SETTINGS_FILE&&failMainRead)||(file==YM_SETTINGS_BACKUP_FILE&&failBackupRead))throw std::runtime_error("fixture-sharing-or-access-error");return files.contains(file)?files[file]:std::string{};}
static bool sgWriteFileAtomic(const std::wstring& file,const std::string& value){++writeAttempts;if(failWriteOrdinal&&writeAttempts==failWriteOrdinal)return false;if(corruptWriteOrdinal&&writeAttempts==corruptWriteOrdinal){files[file]="{partial-write";++writes;return true;}if(failMainWrite&&file==YM_SETTINGS_FILE)return false;if((file.find(L".before-auto-recovery-")!=std::wstring::npos||file.find(L".before-backup-replacement-")!=std::wstring::npos)&&++archiveAttempts==failArchiveAt)return false;files[file]=value;++writes;return true;}
static unsigned long long GetTickCount64(){return 1234;}
static unsigned long GetCurrentProcessId(){return 42;}
static std::string W2U(const std::wstring& value){return std::string(value.begin(),value.end());}
namespace fspath{static bool exists(const std::wstring& file){return files.contains(file);}}
struct InputHostTargetWriteGuard{bool admit(const json&,const json&){++admitted;return true;}void commit(){}};
static bool ymSettingsFileExceedsLimit(const std::wstring&){return false;}
static bool ymSettingsArchiveFile(const std::wstring&,const std::wstring&){return false;}
static bool ymSettingsWriteBoundedRecovery(json,const json*,json*,bool,long long,bool* handled,bool*){*handled=false;return false;}
// SETTINGS_BASELINE_PRODUCTION_BODIES
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
static json inputHostStartProtectionSnapshot(){return json::object();}
static json ymSettingsSection(const char*){return json::object();}
static void appendNativeLifecycleLog(const char*,const json&){effect("lifecycle-log");}
static bool sgStaticScanEnabled(const json&){return false;}
static void sgStaticScanPreference(bool){effect("sleep");}
// External wallpaper owner is an inert port in this settings-only consumer.
static void steamWallpaperForegroundPreference(const json&){effect("wallpaper-preference");}
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
 for(const auto* failed:{"sleep","fxsound","input-refresh","shortcut-snapshot","shortcut-event","gesture"}){
  seed(defaults);auto submitted=defaults;submitted["music"]["volume"]=0.41;submitted["ui"]["fixtureChanged"]=true;
  effectCalls.clear();failingEffect=failed;
  const auto response=writeHandler({{"path",W2U(YM_SETTINGS_FILE)},{"content",submitted.dump()},{"baseline",defaults}});
  failingEffect.clear();need(response.value("ok",false)&&json::parse(response.value("content",std::string{}))==disk(),"postcommit callback failure loses the durable save receipt");
  need(disk()["music"]["volume"]==0.41&&response.contains("refreshWarnings"),"postcommit failure is hidden as a failed or warning-free save");
  need(effectCalls.contains("sleep")&&effectCalls.contains("fxsound")&&effectCalls.contains("input-refresh")&&effectCalls.contains("gesture"),"one failed consumer suppresses unrelated postcommit refreshes");
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
int main(int argc,char** argv){
#if defined(YMCC_SETTINGS_SECTION_CACHE_SCOPE)
 if(argc>=3&&std::string(argv[2])=="--section-cache")return settingsIoFixture::runSectionCacheScope(argc,argv);
 return 2;
#else
 if(argc>=3&&std::string(argv[2])=="--cleanup-history")return settingsIoFixture::runCleanupHistoryScope(argc,argv);
 try{SettingsCleanupFrontendFixture frontend;const int code=memoryFixture::run(argc,argv);frontend.close();return code;}
 catch(const std::exception& error){std::cerr<<"FAIL cleanup frontend "<<error.what()<<'\n';return 1;}
#endif
}
