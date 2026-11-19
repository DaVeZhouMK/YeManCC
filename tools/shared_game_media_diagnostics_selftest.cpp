#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "shared_game_media_shortcut.h"
#include "shared_game_media_diagnostics.h"
#include "decky_sidebar_diagnostics.h"
#include <iostream>
using namespace ymcc::media;
static int checks=0;static void check(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
static void bytes(const fs::path&file,const std::string&value){fs::create_directories(file.parent_path());std::ofstream output(file,std::ios::binary);output.write(value.data(),value.size());}
static std::string content(const fs::path&file){std::ifstream input(file,std::ios::binary);return std::string((std::istreambuf_iterator<char>(input)),{});}
static std::string shortcuts(bool empty=false){std::string result;auto text=[&](const std::string&s){result+=s;result.push_back(0);};result.push_back(0);text("shortcuts");if(!empty){result.push_back(0);text("0");result.push_back(2);text("appid");for(int n=0;n<4;n++)result.push_back(static_cast<char>(0x80000001u>>(n*8)));result.push_back(1);text("appname");text("diagnostic fixture");result.push_back(1);text("exe");text("\"G:\\Fixture\\game.exe\"");result.push_back(8);}result.push_back(8);result.push_back(8);return result;}
namespace ymcc::media::diagnostics {
template<class Path> Export fixtureHostExport(const Path& host,const Path& output){
 if constexpr(requires{exportHostRecords(host,output);})return exportHostRecords(host,output);
 else return exportMetadata(output); // The pre-fix export had no child Host evidence.
}
}
static int hostRecords(const fs::path& root){
 const auto host=root/L"child",output=root/L"out";
 const auto unobserved=diagnostics::fixtureHostExport(fs::path{},root/L"unknown");check(unobserved.files.empty()&&!fs::exists(root/L"unknown"),"unknown child root is passive and creates nothing");
 const auto absent=diagnostics::fixtureHostExport(host,root/L"absent");check(absent.files.empty()&&!fs::exists(host)&&!fs::exists(root/L"absent"),"missing child logs are not created or searched elsewhere");
 for(const auto* kind:{L"crash",L"exit"})for(int n=0;n<4;++n){const auto file=host/L"logs"/(L"host-"+std::wstring(kind))/(std::wstring(kind)+L"-"+std::to_wstring(1000+n)+L"-7.json");bytes(file,Json{{"schemaVersion",1},{"kind",utf8(kind)},{"at",1000+n},{"detail","fixture"}}.dump());fs::last_write_time(file,fs::file_time_type{}+std::chrono::seconds(1000+n));}
 bytes(host/L"logs"/L"host-crash"/L"private.json","not a host receipt");bytes(host/L"auth.json","unrelated secret");fs::create_directory(host/L"logs"/L"host-crash"/L"crash-9000-7.json");
 const auto normal=diagnostics::fixtureHostExport(host,output);check(normal.files.size()==6,"normal export includes three newest crash and three newest exit records");
 check(normal.receipt.value("sourceReadOnly",false)&&!normal.receipt.value("workerStarted",true)&&!normal.receipt.value("networkStarted",true),"host export keeps passive scope explicit");
 for(const auto& file:normal.files){const auto name=file.filename().wstring();check(name.rfind(L"custom-library-",0)==0&&name.find(L"1000-")==std::wstring::npos,"record names are isolated and oldest records excluded");const auto sourceName=name.substr(std::wstring(L"custom-library-").size());const auto folder=sourceName.rfind(L"crash-",0)==0?L"host-crash":L"host-exit";check(content(file)==content(host/L"logs"/folder/sourceName),"host records preserve exact source bytes");}
 check(!fs::exists(output/L"auth.json")&&!fs::exists(output/L"private.json"),"host export excludes unrelated files");
 const auto lockedFile=host/L"logs"/L"host-crash"/L"crash-1003-7.json";const auto original=content(lockedFile);HANDLE held=CreateFileW(lockedFile.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held!=INVALID_HANDLE_VALUE,"fixture holds only its own crash file");
 const auto partial=diagnostics::fixtureHostExport(host,output);CloseHandle(held);check(partial.files.size()==5,"one locked host record does not lose other evidence");check(std::none_of(partial.files.begin(),partial.files.end(),[](const auto&p){return p.filename()==L"custom-library-crash-1003-7.json";}),"old destination is never returned for a locked current source");check(content(lockedFile)==original,"export never changes a source record");
 check(std::any_of(partial.receipt["groups"][0]["files"].begin(),partial.receipt["groups"][0]["files"].end(),[](const Json&j){return !j.value("copied",true)&&j.value("win32Error",DWORD{0})==ERROR_SHARING_VIOLATION;}),"locked record carries its actual sharing error");
 const auto large=host/L"logs"/L"host-exit"/L"exit-1003-7.json";{std::ofstream out(large,std::ios::binary);out.seekp(262144);out.put('x');}const auto capped=diagnostics::fixtureHostExport(host,root/L"capped");check(capped.files.size()==5&&fs::file_size(large)==262145,"host size cap omits only the oversized record and preserves source bytes");
 check(std::any_of(capped.receipt["groups"][1]["files"].begin(),capped.receipt["groups"][1]["files"].end(),[](const Json&j){return j.value("win32Error",DWORD{0})==ERROR_FILE_TOO_LARGE;}),"oversized host record has explicit error");
 const auto missingCategory=root/L"missing-category";bytes(missingCategory/L"logs"/L"host-exit"/L"exit-1000-7.json","{}");const auto one=diagnostics::fixtureHostExport(missingCategory,root/L"one");check(one.files.size()==1&&!fs::exists(missingCategory/L"logs"/L"host-crash"),"missing category leaves the other category exportable without source writes");
 const auto bounded=root/L"bounded";for(int n=0;n<130;++n)bytes(bounded/L"logs"/L"host-crash"/(L"crash-"+std::to_wstring(n)+L"-7.json"),"{}");const auto limited=diagnostics::fixtureHostExport(bounded,root/L"limited");check(limited.files.size()<=3&&limited.receipt["groups"][0].value("scanLimitReached",false),"large record folder has a bounded explicitly incomplete scan");
 Json result={{"ok",true},{"checks",checks},{"scope","original passive exporter, real owned files/share lock/size cap/retention and bounded scan; no installed application or Steam writes"},{"normal",normal.receipt},{"partial",partial.receipt},{"capped",capped.receipt},{"limited",limited.receipt}};atomicWrite(root/L"result.json",result);std::cout<<Json{{"ok",true},{"checks",checks}}.dump()<<std::endl;return 0;
}
static int uiErrors(const fs::path& root){
 diagnostics::rememberRoot({});const auto unknown=diagnostics::exportMetadata(root/L"unknown");check(unknown.files.empty()&&!fs::exists(root/L"unknown"),"unobserved root creates no UI evidence or directory");
 const auto data=root/L"media",source=data/L"state"/L"ui-errors.jsonl",output=root/L"out";diagnostics::rememberRoot(data);const auto missing=diagnostics::exportMetadata(root/L"missing");check(missing.files.empty()&&!fs::exists(data),"missing UI log is not created or discovered elsewhere");
 const std::string original="{\"kind\":\"window-error\",\"message\":\"fixture renderer error\"}\n{\"kind\":\"invalid-host-snapshot\"}\n";bytes(source,original);bytes(data/L"config"/L"ui-errors.jsonl","wrong category");bytes(data/L"state"/L"auth.json","unrelated credentials");
 const auto copied=diagnostics::exportMetadata(output);check(copied.files.size()==1&&copied.files.front().filename()==L"ui-errors.jsonl","ordinary passive export includes the existing UI error file");
 check(content(copied.files.front())==original,"UI errors preserve exact JSONL bytes");check(content(source)==original,"source UI log is never rewritten");
 check(copied.receipt.value("sourceReadOnly",false)&&!copied.receipt.value("workerStarted",true)&&!copied.receipt.value("networkStarted",true),"UI error export retains passive scope");
 check(!copied.receipt.value("coherentAcrossFiles",true),"UI log is a per-file snapshot rather than an atomic cross-file claim");
 const auto row=[&](const diagnostics::Export& value){for(const auto& item:value.receipt["files"])if(item.value("name",std::string{})=="ui-errors.jsonl")return item;return Json::object();};
 check(row(copied).value("copied",false)&&row(copied).value("copiedBytes",uint64_t{0})==original.size(),"ordinary availability receipt records the exact UI byte count");
 check(!fs::exists(output/L"auth.json")&&content(output/L"ui-errors.jsonl")!="wrong category","only the observed root state UI log is selected");
 HANDLE lock=CreateFileW(source.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(lock!=INVALID_HANDLE_VALUE,"fixture locks only its owned UI log");
 const auto locked=diagnostics::exportMetadata(output);CloseHandle(lock);check(locked.files.empty()&&!row(locked).value("copied",true)&&row(locked).value("win32Error",DWORD{0})==ERROR_SHARING_VIOLATION,"locked current UI log never substitutes an old destination and reports its sharing error");check(content(source)==original,"locked export leaves source bytes unchanged");
 {std::ofstream out(source,std::ios::binary);out.seekp(16*1024*1024);out.put('x');}const auto capped=diagnostics::exportMetadata(root/L"capped");check(capped.files.empty()&&row(capped).value("win32Error",DWORD{0})==ERROR_FILE_TOO_LARGE,"UI log obeys the existing open-handle16MiB cap");check(fs::file_size(source)==16*1024*1024+1&&!fs::exists(root/L"capped"/L"ui-errors.jsonl"),"UI cap never truncates source or publishes partial bytes");
 bytes(source,"{unfinished JSONL tail");const auto partial=diagnostics::exportMetadata(root/L"partial");check(partial.files.size()==1&&content(partial.files.front())=="{unfinished JSONL tail","partial JSONL tail is preserved as evidence without invented parsing or repair");
 fs::remove(source);fs::create_directory(source);const auto directory=diagnostics::exportMetadata(root/L"directory");check(directory.files.empty()&&row(directory).value("missingReason",std::string{})=="media-state-not-regular","directory disguised as UI log is refused");
 fs::remove(source);const auto gone=diagnostics::exportMetadata(output);check(gone.files.empty()&&!row(gone).value("copied",true),"removed current UI log cannot return an earlier staged copy");
 const auto report=Json{{"ok",true},{"checks",checks},{"scope","actual passive exporter and shared snapshot copier, owned JSONL/share lock/cap/partial bytes; no live UI or Steam"},{"copied",copied.receipt},{"locked",locked.receipt},{"capped",capped.receipt}};atomicWrite(root/L"result.json",report);std::cout<<Json{{"ok",true},{"checks",checks}}.dump()<<std::endl;return 0;
}
static int cachedLibraryState(const fs::path& root){
 diagnostics::rememberRoot({});const auto unknown=diagnostics::exportMetadata(root/L"unknown");check(unknown.files.empty()&&!fs::exists(root/L"unknown"),"unobserved library root remains passive");
 const auto data=root/L"media",state=data/L"state",output=root/L"out";diagnostics::rememberRoot(data);
 const auto absent=diagnostics::exportMetadata(root/L"absent");check(absent.files.empty()&&!fs::exists(data)&&!fs::exists(root/L"absent"),"missing cached library state creates no directories");
 const auto row=[](const diagnostics::Export& value,const char* name){for(const auto& item:value.receipt["files"])if(item.value("name",std::string{})==name)return item;return Json::object();};
 const auto names={L"library-scan.json",L"steam-add-plan.json",L"library-steam-add-plan.json"};
 for(const auto* name:names){bytes(state/name,Json{{"source",utf8(name)},{"games",Json::array({{{"name","owned missing game"},{"primaryExecutable","Z:\\NotPresent\\game.exe"}}})}}.dump());bytes(data/L"config"/name,"wrong category");}
 bytes(state/L"auth.json","unrelated credentials");bytes(state/L"steam-auth.json","unrelated account credentials");bytes(state/L"extra-plan.json","not selected");
 const auto normal=diagnostics::exportMetadata(output);check(normal.files.size()==3,"passive export includes all three existing cached library records");
 for(const auto* name:names){check(content(output/name)==content(state/name),"cached library bytes come from the observed root state directory");const auto item=row(normal,utf8(name).c_str());check(item.value("copied",false)&&item.value("copiedBytes",uint64_t{0})==content(state/name).size(),"cached library availability retains exact byte count");}
 check(!fs::exists(output/L"auth.json")&&!fs::exists(output/L"steam-auth.json")&&!fs::exists(output/L"extra-plan.json"),"cached export remains an exact file allowlist");
 check(normal.receipt.value("sourceReadOnly",false)&&!normal.receipt.value("networkStarted",true)&&!normal.receipt.value("workerStarted",true)&&!normal.receipt.value("coherentAcrossFiles",true),"cached records are passive per-file evidence rather than fresh scan or atomic truth");
 const auto scan=state/L"library-scan.json";const auto original=content(scan);HANDLE held=CreateFileW(scan.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held!=INVALID_HANDLE_VALUE,"fixture locks only its own cached scan");const auto locked=diagnostics::exportMetadata(output);CloseHandle(held);
 check(locked.files.size()==2&&!row(locked,"library-scan.json").value("copied",true)&&row(locked,"library-scan.json").value("win32Error",DWORD{0})==ERROR_SHARING_VIOLATION,"locked scan reports actual error while both plans remain exportable");
 check(std::none_of(locked.files.begin(),locked.files.end(),[](const auto& p){return p.filename()==L"library-scan.json";})&&content(output/L"library-scan.json")==original&&content(scan)==original,"locked current scan cannot substitute a prior staged copy or modify source");
 {std::ofstream large(scan,std::ios::binary);large.seekp(16*1024*1024);large.put('x');}const auto capped=diagnostics::exportMetadata(root/L"capped");check(capped.files.size()==2&&row(capped,"library-scan.json").value("win32Error",DWORD{0})==ERROR_FILE_TOO_LARGE,"cached scans use the original open-handle16MiB cap");check(fs::file_size(scan)==16*1024*1024+1&&!fs::exists(root/L"capped"/L"library-scan.json"),"oversized cached source is not truncated or partially published");
 bytes(scan,"{unfinished cached JSON");const auto malformed=diagnostics::exportMetadata(root/L"malformed");check(malformed.files.size()==3&&content(root/L"malformed"/L"library-scan.json")=="{unfinished cached JSON","cached evidence preserves malformed bytes without reparsing or repairing");
 fs::remove(scan);const auto missing=diagnostics::exportMetadata(output);check(missing.files.size()==2&&row(missing,"library-scan.json").value("missingReason",std::string{})=="not-created-or-not-retained","removed scan reports absence without dropping either plan");
 fs::create_directory(scan);const auto disguised=diagnostics::exportMetadata(root/L"directory");check(disguised.files.size()==2&&row(disguised,"library-scan.json").value("missingReason",std::string{})=="media-state-not-regular","directory disguised as cached scan is refused");
 Json accountRows=Json::array();bool accountSafe=true;unsigned index=0;
 for(const auto* name:{L"steam-accounts.json",L"last-steam-add.json",L"last-steam-add-all-accounts.json",L"last-steam-artwork-refresh-all-accounts.json",L"last-steam-delete-all-accounts.json"}){
  const auto own=root/std::to_wstring(index++),data=own/L"d",state=data/L"state",source=state/name,out=own/L"o";diagnostics::rememberRoot(data);
  const std::string raw=Json{{"file",utf8(name)},{"accountId","1001"},{"opaque","preserve original cached observation"}}.dump();bytes(source,raw);bytes(state/L"library-scan.json","owned sibling state");bytes(data/L"config"/name,"wrong category");bytes(state/L"steam-auth.json","unrelated account credentials");
  const auto copied=diagnostics::exportMetadata(out);const bool exact=copied.files.size()==2&&row(copied,utf8(name).c_str()).value("copied",false)&&content(out/name)==raw&&row(copied,utf8(name).c_str()).value("copiedBytes",uint64_t{0})==raw.size();
  bytes(out/name,"stale staged evidence");HANDLE held=CreateFileW(source.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held!=INVALID_HANDLE_VALUE,"fixture locks only its own Steam evidence");const auto locked=diagnostics::exportMetadata(out);CloseHandle(held);
  const bool lockSafe=locked.files.size()==1&&row(locked,utf8(name).c_str()).value("win32Error",DWORD{0})==ERROR_SHARING_VIOLATION&&content(source)==raw&&content(out/name)=="stale staged evidence"&&std::none_of(locked.files.begin(),locked.files.end(),[&](const auto& p){return p.filename()==name;});
  {std::ofstream large(source,std::ios::binary);large.seekp(16*1024*1024);large.put('x');}const auto capped=diagnostics::exportMetadata(own/L"c");const bool capSafe=capped.files.size()==1&&row(capped,utf8(name).c_str()).value("win32Error",DWORD{0})==ERROR_FILE_TOO_LARGE&&fs::file_size(source)==16*1024*1024+1&&!fs::exists(own/L"c"/name);
  bytes(source,"{unfinished outcome");const auto malformed=diagnostics::exportMetadata(own/L"m");const bool malformedKept=malformed.files.size()==2&&content(own/L"m"/name)=="{unfinished outcome";
  fs::remove(source);const auto gone=diagnostics::exportMetadata(out);const bool absent=gone.files.size()==1&&row(gone,utf8(name).c_str()).value("missingReason",std::string{})=="not-created-or-not-retained";
  fs::create_directory(source);const auto disguised=diagnostics::exportMetadata(own/L"x");const bool regular=disguised.files.size()==1&&row(disguised,utf8(name).c_str()).value("missingReason",std::string{})=="media-state-not-regular";
  const bool excluded=!fs::exists(out/L"steam-auth.json");const bool passed=exact&&lockSafe&&capSafe&&malformedKept&&absent&&regular&&excluded;
  accountRows.push_back({{"name",utf8(name)},{"passed",passed},{"exactBytes",exact},{"lockedNoStale",lockSafe},{"sizeCap",capSafe},{"malformedPreserved",malformedKept},{"absenceDeclared",absent},{"nonRegularRejected",regular},{"credentialsExcluded",excluded}});accountSafe=accountSafe&&passed;
 }
 atomicWrite(root/L"account-outcome-observed.json",{{"rows",accountRows}});check(accountSafe,"Account discovery or Steam completion evidence is missing from passive export");

 const auto own=root/L"manual-selection",manualData=own/L"media",manualSource=manualData/L"config"/L"manual-overrides.json",manualOutput=own/L"out";
 diagnostics::rememberRoot(manualData);
 const auto absentManual=diagnostics::exportMetadata(own/L"absent");
 const bool absentPassive=absentManual.files.empty()&&!fs::exists(manualData)&&!fs::exists(own/L"absent");
 const std::string manualRaw=R"({"schemaVersion":1,"items":{"g:\\fixture\\game.exe":{"steamId":4285170,"manualName":"owned choice","idCleared":false,"wallpaperPath":"G:\\Fixture\\manual.jpg"}}})";
 bytes(manualSource,manualRaw);bytes(manualData/L"state"/L"manual-overrides.json","wrong category");bytes(manualData/L"state"/L"library-scan.json","cached sibling");bytes(manualData/L"config"/L"auth.json","unrelated credentials");
 const auto copiedManual=diagnostics::exportMetadata(manualOutput);
 const auto manualRow=[&](const diagnostics::Export& value){return row(value,"manual-overrides.json");};
 const bool exactManual=copiedManual.files.size()==2&&manualRow(copiedManual).value("copied",false)&&manualRow(copiedManual).value("copiedBytes",uint64_t{0})==manualRaw.size()&&content(manualOutput/L"manual-overrides.json")==manualRaw&&content(manualSource)==manualRaw;
 const bool passiveManual=copiedManual.receipt.value("sourceReadOnly",false)&&!copiedManual.receipt.value("workerStarted",true)&&!copiedManual.receipt.value("networkStarted",true)&&!copiedManual.receipt.value("coherentAcrossFiles",true)&&!fs::exists(manualOutput/L"auth.json");
 bytes(manualOutput/L"manual-overrides.json","stale staged evidence");
 HANDLE manualHeld=CreateFileW(manualSource.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(manualHeld!=INVALID_HANDLE_VALUE,"fixture locks only its own manual selection config");
 const auto lockedManual=diagnostics::exportMetadata(manualOutput);CloseHandle(manualHeld);
 const bool lockedManualSafe=lockedManual.files.size()==1&&!manualRow(lockedManual).value("copied",true)&&manualRow(lockedManual).value("win32Error",DWORD{0})==ERROR_SHARING_VIOLATION&&content(manualOutput/L"manual-overrides.json")=="stale staged evidence"&&content(manualSource)==manualRaw&&std::none_of(lockedManual.files.begin(),lockedManual.files.end(),[](const auto& p){return p.filename()==L"manual-overrides.json";});
 {std::ofstream large(manualSource,std::ios::binary);large.seekp(16*1024*1024);large.put('x');}
 const auto cappedManual=diagnostics::exportMetadata(own/L"capped");
 const bool cappedManualSafe=cappedManual.files.size()==1&&manualRow(cappedManual).value("win32Error",DWORD{0})==ERROR_FILE_TOO_LARGE&&fs::file_size(manualSource)==16*1024*1024+1&&!fs::exists(own/L"capped"/L"manual-overrides.json");
 bytes(manualSource,"{unfinished manual choices");const auto malformedManual=diagnostics::exportMetadata(own/L"malformed");
 const bool malformedManualKept=malformedManual.files.size()==2&&content(own/L"malformed"/L"manual-overrides.json")=="{unfinished manual choices";
 fs::remove(manualSource);const auto removedManual=diagnostics::exportMetadata(manualOutput);
 const bool removedManualSafe=removedManual.files.size()==1&&manualRow(removedManual).value("missingReason",std::string{})=="not-created-or-not-retained"&&std::none_of(removedManual.files.begin(),removedManual.files.end(),[](const auto& p){return p.filename()==L"manual-overrides.json";});
 fs::create_directory(manualSource);const auto directoryManual=diagnostics::exportMetadata(own/L"directory");
 const bool directoryManualSafe=directoryManual.files.size()==1&&manualRow(directoryManual).value("missingReason",std::string{})=="media-state-not-regular";
 Json manualObservation={{"absentRootPassive",absentPassive},{"exactBytes",exactManual},{"passiveScope",passiveManual},{"lockedNoStale",lockedManualSafe},{"sizeCap",cappedManualSafe},{"malformedPreserved",malformedManualKept},{"absenceDeclared",removedManualSafe},{"nonRegularRejected",directoryManualSafe}};
 bool manualPassed=true;for(const auto& value:manualObservation)manualPassed=manualPassed&&value.get<bool>();manualObservation["passed"]=manualPassed;
 atomicWrite(root/L"manual-selection-observed.json",manualObservation);check(manualPassed,"Explicit manual identity/artwork selection is missing from passive diagnostics");
 const auto report=Json{{"ok",true},{"checks",checks},{"scope","actual passive exporter with owned cached library files, lock, cap, malformed bytes and absent records; no game path probes, scan, application or Steam actions"},{"normal",normal.receipt},{"locked",locked.receipt},{"capped",capped.receipt}};atomicWrite(root/L"result.json",report);std::cout<<Json{{"ok",true},{"checks",checks}}.dump()<<std::endl;return 0;
}
static int rootRecords(const fs::path& root){
 const auto row=[](const diagnostics::Export& value,const char* name){for(const auto& item:value.receipt["files"])if(item.value("name",std::string{})==name)return item;return Json::object();};
 diagnostics::rememberRoot({});const auto unknown=diagnostics::exportMetadata(root/L"unknown");check(unknown.files.empty()&&!fs::exists(root/L"unknown"),"unobserved root identity export creates nothing");
 for(const auto* name:{"data-root.json","data-migration.json"})check(row(unknown,name).value("missingReason",std::string{})=="media-root-not-observed","unknown root record keeps an explicit unobserved receipt");
 const auto data=root/L"media",manifest=data/L"data-root.json",debt=data/L"state"/L"data-migration.json",output=root/L"out";diagnostics::rememberRoot(data);
 const auto absent=diagnostics::exportMetadata(output);check(absent.files.empty()&&!fs::exists(data)&&!fs::exists(output),"missing root records do not initialize a library or export directory");
 for(const auto* name:{"data-root.json","data-migration.json"})check(row(absent,name).value("missingReason",std::string{})=="not-created-or-not-retained","absent root identity or debt is declared without repair");
 const std::string original="{\"schemaVersion\":99,\"dataRoot\":\"Z:\\\\Unknown\\\\data\",\"opaque\":\"preserve\"}\n",pending="{unfinished migration debt";
 bytes(manifest,original);bytes(debt,pending);bytes(data/L"config"/L"data-root.json","wrong root category");bytes(data/L"config"/L"data-migration.json","wrong state category");bytes(data/L"auth.json","unrelated credentials");
 const auto normal=diagnostics::exportMetadata(output);check(normal.files.size()==2,"passive export includes both existing root identity and migration debt records");
 check(content(output/L"data-root.json")==original&&content(output/L"data-migration.json")==pending,"root record export preserves future and malformed evidence from the exact locations");
 check(content(manifest)==original&&content(debt)==pending,"export never signs or repairs unknown records");
 for(const auto* name:{"data-root.json","data-migration.json"}){const auto item=row(normal,name);check(item.value("copied",false)&&item.value("copiedBytes",uint64_t{0})==content(output/fs::path(name)).size(),"root record availability retains actual copied byte count");}
 check(!fs::exists(output/L"auth.json")&&normal.receipt.value("sourceReadOnly",false)&&!normal.receipt.value("networkStarted",true)&&!normal.receipt.value("workerStarted",true)&&!normal.receipt.value("coherentAcrossFiles",true),"root identity diagnostics keep passive allowlist and per-file scope");
 HANDLE held=CreateFileW(manifest.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held!=INVALID_HANDLE_VALUE,"fixture exclusively holds its own root record");const auto locked=diagnostics::exportMetadata(output);CloseHandle(held);
 check(locked.files.size()==1&&!row(locked,"data-root.json").value("copied",true)&&row(locked,"data-root.json").value("win32Error",DWORD{0})==ERROR_SHARING_VIOLATION,"locked root identity reports its actual sharing error while debt survives");
 check(locked.files.front().filename()==L"data-migration.json"&&content(output/L"data-root.json")==original&&content(manifest)==original,"locked root record never substitutes a stale staged file or changes its source");
 {std::ofstream large(manifest,std::ios::binary);large.seekp(16*1024*1024);large.put('x');}const auto capped=diagnostics::exportMetadata(root/L"capped");check(capped.files.size()==1&&row(capped,"data-root.json").value("win32Error",DWORD{0})==ERROR_FILE_TOO_LARGE,"root records use the existing open-handle16MiB cap");
 check(fs::file_size(manifest)==16*1024*1024+1&&!fs::exists(root/L"capped"/L"data-root.json"),"root record cap does not truncate or partially publish evidence");
 bytes(manifest,original);fs::remove(debt);const auto gone=diagnostics::exportMetadata(output);check(gone.files.size()==1&&gone.files.front().filename()==L"data-root.json"&&row(gone,"data-migration.json").value("missingReason",std::string{})=="not-created-or-not-retained","removed migration debt cannot return an old staged copy");
 fs::create_directory(debt);const auto disguised=diagnostics::exportMetadata(root/L"directory");check(disguised.files.size()==1&&row(disguised,"data-migration.json").value("missingReason",std::string{})=="media-state-not-regular","directory disguised as migration record is refused without repair");
 const auto report=Json{{"ok",true},{"checks",checks},{"scope","original passive exporter and snapshot copier with existing root/migration files, absence, lock, cap and unknown bytes; no initialization, normalization, application or Steam actions"},{"normal",normal.receipt},{"locked",locked.receipt},{"capped",capped.receipt},{"gone",gone.receipt}};atomicWrite(root/L"result.json",report);std::cout<<Json{{"ok",true},{"checks",checks}}.dump()<<std::endl;return 0;
}
namespace ymcc::media::diagnostics {
template<class Path,class Hash> Export fixturePackageExport(const Path& host,const Path& output,Hash hash){
 if constexpr(requires{exportPackageIdentity(host,output,hash);})return exportPackageIdentity(host,output,hash);
 else return {{{"components",Json::array()}},{}};
}
}
static std::string packageTestHash(const std::wstring& path){
 const auto value=content(fs::path(path));BCRYPT_ALG_HANDLE provider=nullptr;unsigned char digest[32]{};
 if(BCryptOpenAlgorithmProvider(&provider,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
 const auto code=BCryptHash(provider,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())),static_cast<ULONG>(value.size()),digest,sizeof digest);BCryptCloseAlgorithmProvider(provider,0);
 if(code<0)return {};std::string result;for(const auto byte:digest){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;
}
static int packageIdentity(const fs::path& root){
 const auto host=root/L"child",output=root/L"out";
 const auto exportNow=[&](const fs::path& from,const fs::path& to){return diagnostics::fixturePackageExport(from,to,packageTestHash);};
 const auto unknown=exportNow({},root/L"unknown");check(unknown.files.empty()&&!fs::exists(root/L"unknown"),"unknown package root stays passive");
 const auto absent=exportNow(host,root/L"absent");check(absent.files.empty()&&!fs::exists(host)&&!fs::exists(root/L"absent"),"absent package is not created");
 bytes(host/L"CustomSteamLibrary.exe","abc");bytes(host/L"SteamArtworkLab.exe","worker");const std::string manifest="{\"sourceBuild\":{\"host\":\"declared-old-hash\"},\"future\":17}\r\n";bytes(host/L"package-manifest.json",manifest);bytes(host/L"auth.json","private");
 const auto normal=exportNow(host,output);atomicWrite(root/L"normal-observation.json",normal.receipt);
 check(normal.receipt["components"].size()==3,"PACKAGE_IDENTITIES_MISSING");
 const auto row=[](const diagnostics::Export& result,const std::string& name){for(const auto& item:result.receipt["components"])if(item.value("name",std::string{})==name)return item;return Json::object();};
 check(row(normal,"CustomSteamLibrary.exe").value("sha256",std::string{})=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","actual staged host SHA256 observed");
 check(row(normal,"SteamArtworkLab.exe").value("sha256",std::string{})==packageTestHash((host/L"SteamArtworkLab.exe").wstring()),"actual Worker SHA256 observed");
 check(normal.files.size()==1&&normal.files[0].filename()==L"custom-library-package-manifest.json"&&content(normal.files[0])==manifest,"manifest exact bytes exported with collision-free name");
 check(row(normal,"package-manifest.json").value("sha256",std::string{})==packageTestHash(normal.files[0].wstring()),"manifest digest belongs to copied bytes not declared identities");
 check(normal.receipt.value("sourceReadOnly",false)&&!normal.receipt.value("networkStarted",true)&&!normal.receipt.value("workerStarted",true)&&!normal.receipt.value("coherentAcrossFiles",true),"export scope is passive and per-file");
 check(!fs::exists(output/L"CustomSteamLibrary.exe")&&!fs::exists(output/L"SteamArtworkLab.exe")&&!fs::exists(output/L"auth.json")&&std::distance(fs::directory_iterator(output),fs::directory_iterator{})==1,"binary snapshots and unrelated secrets not retained");
 const auto originalHost=content(host/L"CustomSteamLibrary.exe");HANDLE held=CreateFileW((host/L"CustomSteamLibrary.exe").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held!=INVALID_HANDLE_VALUE,"fixture owns exclusive lock");const auto locked=exportNow(host,output);CloseHandle(held);
 check(!row(locked,"CustomSteamLibrary.exe").value("observed",true)&&row(locked,"CustomSteamLibrary.exe").value("win32Error",DWORD{})==ERROR_SHARING_VIOLATION&&row(locked,"SteamArtworkLab.exe").value("observed",false)&&locked.files.size()==1,"locked current host is unknown while other identities survive");
 check(content(host/L"CustomSteamLibrary.exe")==originalHost&&content(host/L"package-manifest.json")==manifest,"export preserves source bytes");
 {std::ofstream large(host/L"package-manifest.json",std::ios::binary);large.seekp(262144);large.put('x');}
 const auto capped=exportNow(host,root/L"capped");check(!row(capped,"package-manifest.json").value("observed",true)&&row(capped,"package-manifest.json").value("win32Error",DWORD{})==ERROR_FILE_TOO_LARGE&&row(capped,"SteamArtworkLab.exe").value("observed",false)&&capped.files.empty(),"oversized manifest is refused without blocking binaries");
 bytes(host/L"package-manifest.json",manifest);fs::remove(host/L"SteamArtworkLab.exe");fs::create_directory(host/L"SteamArtworkLab.exe");const auto directory=exportNow(host,root/L"directory");check(!row(directory,"SteamArtworkLab.exe").value("observed",true)&&row(directory,"SteamArtworkLab.exe").value("reason",std::string{})=="package-file-not-regular","directory disguised as binary is refused");
 const auto badHash=diagnostics::fixturePackageExport(host,root/L"bad-hash",[](const std::wstring&)->std::string{throw std::runtime_error("hash failed");});check(badHash.files.empty()&&std::none_of(badHash.receipt["components"].begin(),badHash.receipt["components"].end(),[](const Json& item){return item.value("observed",false);})&&fs::is_empty(root/L"bad-hash"),"hash failure keeps unknown status and removes snapshots");
 fs::remove(host/L"package-manifest.json");const auto missing=exportNow(host,output);check(missing.files.empty()&&!row(missing,"package-manifest.json").value("observed",true),"missing current manifest never returns old staged copy");
 bytes(host/L"package-manifest.json","{unfinished");const auto partial=exportNow(host,root/L"partial");check(partial.files.size()==1&&content(partial.files[0])=="{unfinished","malformed manifest bytes are evidence not repaired");
 const auto replaced=diagnostics::fixturePackageExport(host,root/L"replaced",[&](const std::wstring& staged){const auto hash=packageTestHash(staged);if(content(fs::path(staged))=="{unfinished")bytes(host/L"package-manifest.json","new source version");return hash;});check(replaced.files.size()==1&&content(replaced.files[0])=="{unfinished"&&content(host/L"package-manifest.json")=="new source version"&&row(replaced,"package-manifest.json").value("sha256",std::string{})==packageTestHash(replaced.files[0].wstring()),"manifest hash and exported bytes remain the same snapshot during source replacement");
 {std::ofstream large(host/L"CustomSteamLibrary.exe",std::ios::binary);large.seekp(64ull*1024*1024);large.put('x');}const auto binaryCapped=exportNow(host,root/L"binary-capped");check(!row(binaryCapped,"CustomSteamLibrary.exe").value("observed",true)&&row(binaryCapped,"CustomSteamLibrary.exe").value("win32Error",DWORD{})==ERROR_FILE_TOO_LARGE&&binaryCapped.files.size()==1,"binary snapshot is also bounded before copying");
 atomicWrite(root/L"result.json",Json{{"ok",true},{"checks",checks},{"normal",normal.receipt},{"locked",locked.receipt},{"capped",capped.receipt},{"missing",missing.receipt}});std::cout<<Json{{"ok",true},{"checks",checks}}.dump()<<std::endl;return 0;
}

static int retryQueue(const fs::path& root){
 const auto data=root/L"media",output=root/L"out";
 const auto path=data/L"state"/L"network-retry-queue.json",backup=fs::path(path.wstring()+L".bak");
 Json cases=Json::object();
 const auto observe=[&](const char* scenario,const char* status,size_t files=0){
    const auto exported=diagnostics::exportMetadata(output);
    check(exported.receipt.contains("networkRetryQueue"),"support receipt omits network retry queue observation");
    const auto& item=exported.receipt["networkRetryQueue"];
    check(item["status"]==status&&item["scope"]=="persisted-labels-not-worker-liveness","queue availability and evidence scope are exact");
    check(item.contains("counts")== (std::string_view(status)=="observed"),"only observed data has counts");
    check(exported.files.size()==files&&std::none_of(exported.files.begin(),exported.files.end(),[](const auto& f){return f.filename().wstring().starts_with(L"network-retry-queue.json");}),"neither source nor older staged queue is added to the archive file list");
    check(exported.receipt["sourceReadOnly"]==true&&exported.receipt["workerStarted"]==false&&exported.receipt["networkStarted"]==false,"queue summary stays passive");
    cases[scenario]=item;
    return item;
 };
 diagnostics::rememberRoot({});observe("unobserved","root-not-observed");
 diagnostics::rememberRoot(data);observe("root-absent","absent");
 check(!fs::exists(data)&&!fs::exists(output),"unknown or missing root creates neither source nor staging");
 const std::string empty=R"({"jobs":{}})";
 bytes(backup,empty);observe("backup-only","absent");
 check(!fs::exists(path)&&!fs::exists(output)&&content(backup)==empty,"backup is not substituted, staged or repaired");
 bytes(path,empty);
 const auto zeros=observe("empty","observed")["counts"];
 check(zeros.size()==9&&std::all_of(zeros.begin(),zeros.end(),[](const Json& n){return n==0;}),"only an actual empty compatible queue is zero");
 const std::string mixed=R"({"schemaVersion":1,"jobs":{"private-key-1":{"status":"waiting-network","executable":"C:\\private.exe","commandArgs":["secret-argument"],"lastFailure":{"detail":"private-provider"}},"two":{"status":"waiting-network"},"three":{"status":"running-event-wakeup","workerPid":99},"four":{"status":"running"},"five":{"status":"needs-manual-review-executable-changed"},"six":{"status":"needs-manual-review-interrupted-retry"},"seven":{"status":"private-future-status"},"eight":{"status":3},"nine":null},"history":[{"private-history":4}],"quarantinedJobs":{"private-quarantine":{}}})";
 bytes(path,mixed);const auto writeTime=fs::last_write_time(path);
 const auto summary=observe("mixed","observed");
 const Json expected={{"status","observed"},{"scope","persisted-labels-not-worker-liveness"},{"maxBytes",8u<<20},
    {"counts",{{"jobs",9},{"waitingNetwork",2},{"wakeClaimed",1},{"legacyRunning",1},{"reviewExecutableChanged",1},
    {"reviewInterrupted",1},{"unrecognized",3},{"history",1},{"quarantined",1}}}};
 check(summary==expected,"only fixed aggregate fields survive (no job key, path, arguments, provider details, PID or history)");
 check(!fs::exists(output)&&content(path)==mixed&&fs::last_write_time(path)==writeTime,"a queue alone never creates a staging directory or modifies source");
 bytes(data/L"config"/L"game-media.json","{}");
 const auto staged=output/path.filename();bytes(staged,"stale private queue");
 HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 check(lock!=INVALID_HANDLE_VALUE,"fixture locks its own queue");
 const auto locked=diagnostics::exportMetadata(output);CloseHandle(lock);
 const auto& failure=locked.receipt.at("networkRetryQueue");cases["locked"]=failure;
 check(failure["status"]=="unreadable"&&failure["win32Error"]==ERROR_SHARING_VIOLATION&&!failure.contains("counts"),"locked current queue preserves sharing error without inventing counts");
 check(locked.files.size()==1&&locked.files.front().filename()==L"game-media.json"&&content(path)==mixed&&content(staged)=="stale private queue","locked queue preserves other evidence without substituting backup or stale staged source");
 bytes(path,R"({"schemaVersion":2,"jobs":{}})");observe("version","unsupported-version",1);
 bytes(path,R"({"schemaVersion":"1","jobs":{}})");observe("version-type","unsupported-version",1);
 bytes(path,"{unfinished");observe("malformed","invalid",1);
 bytes(path,R"({"jobs":{"one":{"status":"waiting-network","\u0073tatus":"running"}}})");observe("duplicate-label","invalid",1);
 bytes(path,R"({"jobs":{},"history":{}})");observe("shape","invalid",1);
 bytes(path,std::string("{\"jobs\":{},\"deep\":")+std::string(65,'[')+"0"+std::string(65,']')+"}");observe("nesting","invalid",1);
 {std::ofstream large(path,std::ios::binary);large.seekp(8u<<20);large.put('x');}
 observe("oversized","size-limit",1);
 check(fs::file_size(path)==(8u<<20)+1,"oversized current queue is not truncated");
 fs::remove(path);fs::create_directory(path);observe("directory","not-regular",1);
 fs::remove(path);observe("removed","absent",1);
 check(content(backup)==empty&&content(staged)=="stale private queue","no repair, replacement or staged fallback on failure or removal");
 atomicWrite(root/L"result.json",Json{{"ok",true},{"checks",checks},{"selection","retry-queue"},{"cases",cases}});
 std::cout<<Json{{"ok",true},{"checks",checks}}.dump()<<std::endl;return 0;
}

int wmain(int argc,wchar_t**argv){try{
 if(argc==3){const auto mode=std::wstring_view(argv[2]);if(mode!=L"package-identity"&&mode!=L"host-records"&&mode!=L"ui-errors"&&mode!=L"cached-library-state"&&mode!=L"root-records"&&mode!=L"retry-queue")throw std::runtime_error("unknown explicit diagnostics selection");const auto root=fs::absolute(argv[1]);if(fs::exists(root))throw std::runtime_error("fresh fixture root required");fs::create_directories(root);return mode==L"retry-queue"?retryQueue(root):mode==L"package-identity"?packageIdentity(root):mode==L"root-records"?rootRecords(root):mode==L"cached-library-state"?cachedLibraryState(root):mode==L"ui-errors"?uiErrors(root):hostRecords(root);}
 if(argc!=2)throw std::runtime_error("fresh fixture root required");const auto root=fs::absolute(argv[1]);if(fs::exists(root))throw std::runtime_error("fresh fixture root required");fs::create_directories(root);
 auto missing=observeShortcutInventory(root/L"missing.vdf",17,[]{return 17u;});check(!missing.value("available",true)&&missing["reason"]=="shortcut-file-missing","missing VDF is explicit and never promoted to success");check(!fs::exists(root/L"missing.vdf"),"diagnostic does not create a missing Steam file");
 const auto valid=root/L"shortcuts.vdf";const auto raw=shortcuts();bytes(valid,raw);auto result=observeShortcutInventory(valid,17,[]{return 17u;});check(result.value("available",false)&&result["reason"]=="ready"&&result["rows"].size()==1,"actual valid VDF still returns verified-file candidates");check(result["rows"][0].value("steamAccount",std::string{})=="17","active account provenance remains on every row");check(content(valid)==raw,"valid VDF is byte-preserved");
 // Same common publication drives local media just as the Decky UI account.
 auto identity=std::make_shared<ymcc::steamsession::IdentitySnapshot>();
 identity->presence=1;identity->session={root.wstring(),77,17,88};identity->rootReadError=ERROR_SUCCESS;
 identity->accountReadError=identity->registeredPidReadError=ERROR_SUCCESS;identity->registeredPid=77;
 Json ui={{"known",true},{"running",true},{"pid",77u},{"birth","88"},{"mode","bigpicture"},{"observedAccount",18u},{"deckyAccountWatchAvailable",true}};
 const auto selected=shortcutSteamSession(identity,ui);
 check(selected.account==18&&selected.root==root.wstring(),"media uses common current UI account despite stale positive registry candidate");
 const auto selectedFile=fs::path(selected.root)/L"userdata"/std::to_wstring(selected.account)/L"config"/L"shortcuts.vdf";bytes(selectedFile,raw);
 const auto mapped=shortcutIdentity(selectedFile,"2147483649");check(mapped.value("executable",std::string{})=="G:\\Fixture\\game.exe","current shared UI account reaches its actual shortcuts VDF");
 auto offline=std::make_shared<ymcc::steamsession::IdentitySnapshot>(*identity);offline->session.account=0;
 check(shortcutSteamSession(offline,ui).account==18,"readable zero candidate retains verified offline UI media");
 check(shortcutSteamSession(identity,Json::object()).account==17,"legacy candidate fallback is retained without new UI gating");
 check(shortcutSteamSession({},ui).account==0,"missing common publication does not invent a media account");
 auto unavailable=std::make_shared<ymcc::steamsession::IdentitySnapshot>(*identity);unavailable->presence=-1;
 check(shortcutSteamSession(unavailable,ui).account==0,"unknown common process cannot supply a shortcuts root");
 const auto stable=[&]()->uint32_t{return shortcutSteamSession(identity,ui)==selected?selected.account:0;};
 auto projected=observeShortcutInventory(selectedFile,selected.account,stable);check(projected.value("available",false)&&projected["rows"][0]["steamAccount"]=="18","actual inventory carries the current shared account");
 ui["observedAccount"]=19u;auto switched=observeShortcutInventory(selectedFile,selected.account,stable);
 check(!switched.value("available",true)&&switched["rows"].empty(),"effective UI account changes cancel an old read-only media inventory");
 for(const auto* reason:{"shortcut-file-missing","shortcut-file-status-failed","shortcut-file-not-regular","shortcut-file-too-large","shortcut-file-read-failed","shortcut-file-changed","shortcut-vdf-invalid"}){
  const auto diagnostic=ymcc::deckydiag::record("wallpaper-inventory",{{"reason",reason},{"shortcutRead",{{"file","private path"},{"account","private account"}}}});
  check(diagnostic.value("reason",std::string{})==reason&&!diagnostic.contains("shortcutRead"),"bounded VDF failure reason survives without raw path or account");
 }
 const auto inventoryDiagnostic=[](const char* event,const Json& detail){return ymcc::deckydiag::record(event,detail);};
 const auto allOrigins=Json::array({"library-notice","page-hint","manual","enabled","power-recovery","steam-process","steam-account","steam-document","steam-mode","steam-revalidated"});
 for(const bool ok:{true,false}){
  const auto row=inventoryDiagnostic("wallpaper-inventory",{{"ok",ok},{"reason",ok?"ready":"inventory-read-failed"},{"originMask",1023u},{"requestId","private"},{"path","private"}});
  check(row["ok"]==ok&&row["reason"]==(ok?"ready":"inventory-read-failed")&&row["origins"]==allOrigins&&!row.contains("originMask")&&!row.contains("requestId")&&!row.contains("path"),"success and failure retain only bounded named origins and the original result");
 }
 check(inventoryDiagnostic("wallpaper-inventory",{{"originMask",1|4|128}})["origins"]==Json::array({"library-notice","manual","steam-document"}),"signed bounded masks retain only selected labels");
 for(const auto& value:{Json(0),Json(-1),Json(1025u),Json(18446744073709551615ull),Json(4.0),Json("4"),Json(true),Json(),Json::array({4}),Json{{"account",4}}}){
  check(!inventoryDiagnostic("wallpaper-inventory",{{"originMask",value}}).contains("origins"),"malformed and unknown masks are omitted in full");
 }
 check(!inventoryDiagnostic("wallpaper-inventory",{{"ok",true},{"origins",allOrigins}}).contains("origins"),"legacy or raw labeled input does not emit origins");
 check(!inventoryDiagnostic("wallpaper-prepare",{{"originMask",1}}).contains("origins"),"only inventory diagnostics accept origin masks");
 auto wrong=observeShortcutInventory(valid,17,[]{return 18u;});check(!wrong.value("available",true)&&wrong["reason"]=="account-changed"&&wrong["rows"].empty(),"wrong account cannot read or admit rows");check(!wrong["evidence"].contains("filePresent"),"wrong account stops before a file observation");
 int observed=0;auto changed=observeShortcutInventory(valid,17,[&]{return ++observed<4?17u:18u;});check(!changed.value("available",true)&&changed["rows"].empty()&&changed["reason"]=="account-changed","account change after read drops the entire shortcut snapshot");
 bytes(root/L"empty.vdf",shortcuts(true));auto empty=observeShortcutInventory(root/L"empty.vdf",17,[]{return 17u;});check(empty.value("available",false)&&empty["rows"].empty(),"valid empty VDF is distinct from a missing/unreadable file");
 bytes(root/L"bad.vdf",raw.substr(0,raw.size()-1));auto bad=observeShortcutInventory(root/L"bad.vdf",17,[]{return 17u;});check(!bad.value("available",true)&&bad["reason"]=="shortcut-vdf-invalid"&&bad["evidence"].contains("readErrorTag"),"malformed VDF retains exact bounded parser failure tag");check(bad["rows"].empty(),"parser failure never creates a guessed identity");
 HANDLE lock=CreateFileW(valid.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(lock!=INVALID_HANDLE_VALUE,"fixture holds only its own VDF exclusively");auto locked=observeShortcutInventory(valid,17,[]{return 17u;});CloseHandle(lock);const auto evidence=locked["evidence"];check(!locked.value("available",true),"locked VDF is not reported ready");check(evidence.value("afterFailureReadErrorCode",DWORD{0})==ERROR_SHARING_VIOLATION||evidence.value("fileStatusErrorCode",0)==ERROR_SHARING_VIOLATION||evidence.value("fileSizeErrorCode",0)==ERROR_SHARING_VIOLATION,"actual Windows sharing error is retained for support");check(content(valid)==raw,"failed observation does not alter Steam source");
 const auto large=root/L"large.vdf";{std::ofstream output(large,std::ios::binary);output.seekp(16*1024*1024);output.put('x');}auto oversized=observeShortcutInventory(large,17,[]{return 17u;});check(oversized["reason"]=="shortcut-file-too-large"&&oversized["rows"].empty(),"oversized VDF is bounded before parsing");
 const auto unknownOutput=root/L"unobserved-export";const auto unobserved=diagnostics::exportMetadata(unknownOutput);check(!unobserved.receipt.value("rootObserved",true)&&unobserved.files.empty(),"export does not discover or initialize an unused library root");check(!fs::exists(unknownOutput),"unobserved export creates no output or source directory");
 const auto data=root/L"media";const auto config=data/L"config";bytes(config/L"game-media-downloads.json","{\"enabled\":true,\"jobs\":{}}\n");bytes(config/L"game-media-download-progress.json","{\"bytes\":123,\"workerPid\":99}\n");bytes(config/L"game-media.json","{\"entries\":{},\"revision\":9}\n");bytes(data/L"artwork"/L"video"/L"do-not-copy.mp4","not exported");bytes(data/L"auth.json","not exported secret");
 diagnostics::rememberRoot(data);const auto normalOutput=root/L"normal-export";const auto normal=diagnostics::exportMetadata(normalOutput);check(normal.files.size()==3,"normal export copies all three actual media state files");check(normal.receipt.value("sourceReadOnly",false)&&!normal.receipt.value("workerStarted",true)&&!normal.receipt.value("networkStarted",true),"export metadata makes the passive scope explicit");check(!normal.receipt.value("coherentAcrossFiles",true),"per-file copies do not falsely claim one atomic cross-file snapshot");for(const auto&f:normal.files)check(content(f)==content(config/f.filename()),"export preserves exact source state bytes");check(!fs::exists(normalOutput/L"do-not-copy.mp4")&&!fs::exists(normalOutput/L"auth.json"),"video bodies and unrelated credentials stay out of export");
 lock=CreateFileW((config/L"game-media-download-progress.json").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(lock!=INVALID_HANDLE_VALUE,"fixture locks only its own progress state");auto partial=diagnostics::exportMetadata(normalOutput);CloseHandle(lock);check(partial.files.size()==2,"one unreadable media file does not discard other export evidence");check(std::none_of(partial.files.begin(),partial.files.end(),[](const auto&p){return p.filename()==L"game-media-download-progress.json";}),"old destination copy is not substituted when current source read fails");check(std::any_of(partial.receipt["files"].begin(),partial.receipt["files"].end(),[](const Json&j){return j["name"]=="game-media-download-progress.json"&&!j.value("copied",true)&&j.contains("win32Error");}),"partial export carries exact read error rather than silently omitting it");
 const auto largeState=config/L"game-media-downloads.json";{std::ofstream output(largeState,std::ios::binary);output.seekp(16*1024*1024);output.put('x');}auto capped=diagnostics::exportMetadata(root/L"capped-export");check(capped.files.size()==2,"oversized metadata cannot block other diagnostics");check(std::any_of(capped.receipt["files"].begin(),capped.receipt["files"].end(),[](const Json&j){return j["name"]=="game-media-downloads.json"&&j.value("win32Error",DWORD{0})==ERROR_FILE_TOO_LARGE;}),"actual open-file byte cap rejects before allocating or copying a huge state file");check(fs::file_size(largeState)==16*1024*1024+1,"byte cap does not modify or truncate source metadata");
 DWORD code=0;bytes(root/L"tiny.txt","abc");check(!ymcc::logexport::snapshotSharedLog(root/L"tiny.txt",root/L"bounded-copy.txt",code,2)&&code==ERROR_FILE_TOO_LARGE&&!fs::exists(root/L"bounded-copy.txt"),"bounded shared copier rejects from the opened file before destination creation");check(ymcc::logexport::snapshotSharedLog(root/L"tiny.txt",root/L"legacy-copy.txt",code)&&content(root/L"legacy-copy.txt")=="abc","existing three-argument log copier keeps prior behavior");
 Json report={{"ok",true},{"checks",checks},{"scope","Actual VDF reader, account fencing, Windows sharing errors, passive export root, bounded snapshot copier; fixture files only, no live Steam/YMCC, registry writes, controllers or downloads"},{"normalExport",normal.receipt},{"missingVdf",missing},{"lockedVdf",locked}};atomicWrite(root/L"result.json",report);std::cout<<Json({{"ok",true},{"checks",checks}}).dump()<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 2;}}
