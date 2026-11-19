#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <set>
#include <iostream>
#include <cstring>
#include <algorithm>
#include "json.hpp"
namespace launchInventoryFixture {
using Json=nlohmann::json;
struct Row{DWORD pid;std::wstring name;};
static std::vector<Row> rows;
static std::size_t cursor=0;
static unsigned queries=0,opens=0,closes=0,unexpectedCalls=0,foreignProcessOpens=0;
static bool hardFailure=false,createFailure=false,firstFailure=false,tailFailure=false;
static HANDLE snapshot(){return reinterpret_cast<HANDLE>(std::uintptr_t{0xB01100});}
static NTSTATUS NTAPI query(SYSTEM_INFORMATION_CLASS kind,PVOID,ULONG,PULONG returned){
 ++queries;if(returned)*returned=0;
 if(static_cast<int>(kind)!=252){++unexpectedCalls;return static_cast<NTSTATUS>(0xC0000003L);}
 return static_cast<NTSTATUS>(hardFailure?0xC0000022L:0xC0000003L);
}
static FARPROC WINAPI procedure(HMODULE module,LPCSTR name){
 if(reinterpret_cast<ULONG_PTR>(name)>0xffff&&std::strcmp(name,"NtQuerySystemInformation")==0)return reinterpret_cast<FARPROC>(query);
 return ::GetProcAddress(module,name);
}
static ULONGLONG WINAPI tick(){return 500000;}
static HANDLE WINAPI create(DWORD flags,DWORD pid){
 if(flags!=TH32CS_SNAPPROCESS||pid){++unexpectedCalls;SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
 if(createFailure){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
 ++opens;cursor=0;return snapshot();
}
static BOOL emit(PROCESSENTRY32W* row){
 if(!row||row->dwSize!=sizeof(*row)){++unexpectedCalls;SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 if(cursor>=rows.size()){SetLastError(tailFailure?ERROR_ACCESS_DENIED:ERROR_NO_MORE_FILES);return FALSE;}
 const auto& value=rows[cursor++];row->th32ProcessID=value.pid;row->th32ParentProcessID=0;
 if(value.name.size()>=_countof(row->szExeFile)){++unexpectedCalls;return FALSE;}
 std::wmemcpy(row->szExeFile,value.name.c_str(),value.name.size()+1);return TRUE;
}
static BOOL WINAPI first(HANDLE h,PROCESSENTRY32W* row){
 if(h!=snapshot()){++unexpectedCalls;return FALSE;}cursor=0;
 if(firstFailure){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return emit(row);
}
static BOOL WINAPI next(HANDLE h,PROCESSENTRY32W* row){if(h!=snapshot()){++unexpectedCalls;return FALSE;}return emit(row);}
static BOOL WINAPI close(HANDLE h){if(h==snapshot()){++closes;return TRUE;}++unexpectedCalls;SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
static HRESULT WINAPI folder(HWND,int,HANDLE,DWORD,LPWSTR result){const wchar_t value[]=L"X:\\YMCC-owned-memory-profile";std::copy(std::begin(value),std::end(value),result);return S_OK;}
static HANDLE WINAPI process(DWORD,BOOL,DWORD){++foreignProcessOpens;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}

struct PathRead { HKEY hive; std::wstring name; DWORD view; bool operator==(const PathRead&)const=default; };
static std::vector<PathRead> pathReads;
static std::array<std::wstring,4> launchPaths;
static std::array<std::wstring,2> updateRoots;
static std::array<DWORD,2> updateTypes;
static std::vector<std::size_t> updateReads;
static unsigned updateOpens=0,updateCloses=0;
static std::vector<std::wstring> fixtureImages;
static DWORD WINAPI imageAttributes(LPCWSTR name){
 return std::find(fixtureImages.begin(),fixtureImages.end(),name)!=fixtureImages.end()?FILE_ATTRIBUTE_NORMAL:INVALID_FILE_ATTRIBUTES;
}
static LSTATUS WINAPI launchRegistry(HKEY hive,LPCWSTR key,LPCWSTR name,DWORD flags,LPDWORD,void* data,LPDWORD size){
 if(_wcsicmp(key,L"Software\\Valve\\Steam"))return ERROR_FILE_NOT_FOUND;
 const DWORD view=flags&~RRF_RT_REG_SZ;
 pathReads.push_back({hive,name,view});
 const std::wstring* value=nullptr;
 if(hive==HKEY_CURRENT_USER&&!view){
     if(!wcscmp(name,L"SteamExe"))value=&launchPaths[0];
     else if(!wcscmp(name,L"SteamPath"))value=&launchPaths[1];
 }else if(hive==HKEY_LOCAL_MACHINE&&!wcscmp(name,L"InstallPath")){
     if(view==RRF_SUBKEY_WOW6432KEY)value=&launchPaths[2];
     else if(view==RRF_SUBKEY_WOW6464KEY)value=&launchPaths[3];
 }
 if(!value)return ERROR_FILE_NOT_FOUND;
 const auto bytes=static_cast<DWORD>((value->size()+1)*sizeof(wchar_t));
 if(*size<bytes)return ERROR_MORE_DATA;
 memcpy(data,value->c_str(),bytes);*size=bytes;return ERROR_SUCCESS;
}
static HKEY updateKey(std::size_t index){return reinterpret_cast<HKEY>(std::uintptr_t{0x7333}+index);}
static LSTATUS WINAPI updateOpen(HKEY hive,LPCWSTR key,DWORD,REGSAM access,PHKEY opened){
 if(_wcsicmp(key,L"Software\\Valve\\Steam")||access!=KEY_READ)return ERROR_ACCESS_DENIED;
 if(hive!=HKEY_CURRENT_USER&&hive!=HKEY_LOCAL_MACHINE)return ERROR_FILE_NOT_FOUND;
 *opened=updateKey(hive==HKEY_CURRENT_USER?0:1);++updateOpens;return ERROR_SUCCESS;
}
static LSTATUS WINAPI updateQuery(HKEY key,LPCWSTR name,LPDWORD,LPDWORD type,LPBYTE data,LPDWORD size){
 if((key!=updateKey(0)&&key!=updateKey(1))||wcscmp(name,L"SteamPath"))return ERROR_FILE_NOT_FOUND;
 const auto index=key==updateKey(0)?0u:1u;updateReads.push_back(index);
 const auto& value=updateRoots[index];
 const auto bytes=static_cast<DWORD>((value.size()+1)*sizeof(wchar_t));
 if(*size<bytes)return ERROR_MORE_DATA;
 memcpy(data,value.c_str(),bytes);*size=bytes;*type=updateTypes[index];return ERROR_SUCCESS;
}
static LSTATUS WINAPI updateClose(HKEY key){
 if(key!=updateKey(0)&&key!=updateKey(1))return ERROR_INVALID_HANDLE;
 ++updateCloses;return ERROR_SUCCESS;
}
}
#define GetFileAttributesW launchInventoryFixture::imageAttributes
#include "steam_launch_path.h"
#undef GetFileAttributesW
#define GetProcAddress launchInventoryFixture::procedure
#define GetTickCount64 launchInventoryFixture::tick
#define CreateToolhelp32Snapshot launchInventoryFixture::create
#define Process32FirstW launchInventoryFixture::first
#define Process32NextW launchInventoryFixture::next
#define CloseHandle launchInventoryFixture::close
#define SHGetFolderPathW launchInventoryFixture::folder
#define OpenProcess launchInventoryFixture::process
#include "steam_launch_owner_observation.h"
#include "steam_native_launch_windows.h"
#undef GetProcAddress
#undef GetTickCount64
#undef CreateToolhelp32Snapshot
#undef Process32FirstW
#undef Process32NextW
#undef CloseHandle
#undef SHGetFolderPathW
#undef OpenProcess
#define RegGetValueW launchInventoryFixture::launchRegistry
#define RegOpenKeyExW launchInventoryFixture::updateOpen
#define RegQueryValueExW launchInventoryFixture::updateQuery
#define RegCloseKey launchInventoryFixture::updateClose
#include "steam_information_producer.h"
#undef RegGetValueW
#undef RegOpenKeyExW
#undef RegQueryValueExW
#undef RegCloseKey
int main(int argc,char** argv){
 using namespace launchInventoryFixture;using ymcc::steamlaunch::WindowsPorts;using ymcc::processInventory::Status;
 const std::set<std::string> names{"shared-owner-consumed","final-absence-fresh","final-presence-fresh","empty-complete","partial-prefix-unknown","first-error-unknown","create-error-unknown","native-error-no-private-fallback","own-child-only-excluded","case-insensitive-name","failed-fresh-not-old-success","cached-followers-zero-probes","source-missing-or-revoked","one-table-linked-projection","launch-path-stop-after-first-valid-source","update-hook-root-owner-gate-and-legacy-order"};
 std::string selected;
 if(argc==2&&std::string(argv[1])=="--inventory-consumption"){}
 else if(argc==4&&std::string(argv[1])=="--inventory-consumption"&&std::string(argv[2])=="--case"&&names.contains(argv[3]))selected=argv[3];
 else return 2;
 Json cases=Json::array();const auto need=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
 const auto reset=[] {rows.clear();cursor=queries=opens=closes=unexpectedCalls=foreignProcessOpens=0;hardFailure=createFailure=firstFailure=tailFailure=false;ymcc::processInventory::invalidate();};
 const auto fresh=[] {return ymcc::processInventory::read(ymcc::processInventory::resolveQuery(),ymcc::processInventory::currentTime,0);};
 const auto passive=[] {return ymcc::processInventory::read(ymcc::processInventory::resolveQuery());};
 const auto facts=[&](const auto& current){return ymcc::steamlaunch::LaunchFacts{
     [&,current](DWORD child,bool addons){return ymcc::steaminfo::launchDetail::observe(child,addons,current,fresh);},[]{return L"X:\\steam.exe";}};};
 const auto check=[&](const char* name,const auto& body){if(!selected.empty()&&selected!=name)return;reset();try{WindowsPorts io(L"X:\\YMCC-owned-memory",L"X:\\YMCC-owned-memory",{},{},facts([]{return true;}));body(io);need(opens==closes,"all-census-handles-returned");need(!unexpectedCalls&&!foreignProcessOpens,"no-foreign-process-or-unexpectedCalls-terminal");cases.push_back({{"name",name},{"passed",true},{"queries",queries},{"snapshotOpens",opens},{"snapshotCloses",closes}});}catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"assertion",e.what()},{"queries",queries},{"snapshotOpens",opens},{"snapshotCloses",closes}});}};
 check("shared-owner-consumed",[&](auto& io){auto seed=fresh();rows={{42,L"steam.exe"}};const auto value=io.snapshot({});need(value.steam==1,"positive-name-presence-preserved");need(queries==2,"launch-consumer-must-use-unique-shared-owner");auto now=passive();need(now->status==Status::Complete&&now->generation>seed->generation&&now->rows.size()==1&&queries==2,"launcher-publishes-same-canonical-table");});
 check("final-absence-fresh",[&](auto& io){need(fresh()->status==Status::Complete,"seed-complete-empty");rows={{42,L"steam.exe"}};need(!io.absentExcept(0)&&queries==2,"final-absence-cannot-use-cached-empty");});
 check("final-presence-fresh",[&](auto& io){rows={{42,L"steam.exe"}};fresh();rows.clear();need(io.snapshot({}).steam==0&&queries==2,"launch-presence-must-be-fresh");});
 check("empty-complete",[&](auto& io){need(io.snapshot({}).steam==0&&io.absentExcept(0)&&queries==2,"complete-empty-allows-only-original-absence-proof");});
 check("partial-prefix-unknown",[&](auto& io){rows={{77,L"other.exe"}};tailFailure=true;need(io.snapshot({}).steam==-1&&!io.absentExcept(0),"partial-prefix-never-establishes-absence");});
 check("first-error-unknown",[&](auto& io){firstFailure=true;need(io.snapshot({}).steam==-1&&!io.absentExcept(0),"first-error-never-establishes-absence");});
 check("create-error-unknown",[&](auto& io){createFailure=true;need(io.snapshot({}).steam==-1&&!io.absentExcept(0)&&opens==0,"failed-census-never-establishes-absence");});
 check("native-error-no-private-fallback",[&](auto& io){hardFailure=true;need(io.snapshot({}).steam==-1&&!io.absentExcept(0)&&opens==0&&queries==2,"native-failure-cannot-gain-private-fallback");});
 check("own-child-only-excluded",[&](auto& io){rows={{42,L"steam.exe"},{77,L"other.exe"}};need(io.absentExcept(42),"only-owned-suspended-child-can-be-excluded");rows.push_back({43,L"steam.exe"});need(!io.absentExcept(42),"second-steam-is-not-owned-child");});
 check("case-insensitive-name",[&](auto& io){rows={{42,L"StEaM.ExE"},{77,L"steamwebhelper.exe"}};need(io.snapshot({}).steam==1&&!io.absentExcept(0),"original-steam-name-and-case-matching-kept");});
 check("failed-fresh-not-old-success",[&](auto& io){need(fresh()->status==Status::Complete,"empty-seed-is-complete");hardFailure=true;need(io.snapshot({}).steam==-1&&!io.absentExcept(0)&&queries==3,"fresh-failure-does-not-resurrect-old-absence");});
 check("cached-followers-zero-probes",[&](auto& io){rows={{42,L"steam.exe"}};need(io.snapshot({}).steam==1,"launcher-positive");const auto initial=queries;for(unsigned n=0;n<1001;++n){const auto observed=passive();need(observed->status==Status::Complete&&observed->rows.size()==1,"same-public-follower-receipt");}need(queries==initial&&opens==closes,"followers-do-not-repeat-launch-census");});
 check("source-missing-or-revoked",[&](auto&){
     WindowsPorts missing(L"X:\\no-io",L"X:\\no-io");
     need(missing.snapshot({}).steam==-1&&!missing.absentExcept(0)&&missing.steamExecutable().empty()&&queries==0,"missing source must not scan or resolve an executable");
     unsigned checks=0;
     WindowsPorts revoked(L"X:\\no-io",L"X:\\no-io",{},{},facts([&]{return ++checks==1;}));
     need(revoked.snapshot({}).steam==-1&&checks==2&&queries==1,"revocation during the shared read suppresses the result");
 });
 check("one-table-linked-projection",[&](auto& io){
     rows={{42,L"steam.exe"},{77,L"unrelated.exe"}};
     const auto res=io.snapshot({L"x:\\linked.exe"});
     need(res.steam==1&&res.active.empty()&&res.uncertain.empty()&&queries==1,"Steam presence and linked rows use one owner table");
 });
 check("launch-path-stop-after-first-valid-source",[&](auto&){
     using namespace ymcc::steaminfo::launchDetail;
     launchPaths={L"X:/direct/steam.exe",L"X:\\user",L"X:\\machine32",L"X:\\machine64"};
     fixtureImages={L"X:\\direct\\steam.exe",L"X:\\user\\steam.exe",L"X:\\machine32\\steam.exe",L"X:\\machine64\\steam.exe"};
     const std::vector<PathRead> expected{{HKEY_CURRENT_USER,L"SteamExe",0},{HKEY_CURRENT_USER,L"SteamPath",0},
         {HKEY_LOCAL_MACHINE,L"InstallPath",RRF_SUBKEY_WOW6432KEY},{HKEY_LOCAL_MACHINE,L"InstallPath",RRF_SUBKEY_WOW6464KEY}};
     for(std::size_t count=1;count<=expected.size();++count){
         pathReads.clear();
         need(steamExecutable(registryText)==fixtureImages.front(),"launch-order-or-path-changed");
         need(pathReads==std::vector<PathRead>(expected.begin(),expected.begin()+count),"unneeded-registry-fallback-read");
         fixtureImages.erase(fixtureImages.begin());
     }
     pathReads.clear();need(steamExecutable(registryText).empty()&&pathReads==expected,"no-path-remains-empty");
     need(queries==0,"path-resolution-used-process-topology");
 });
 check("update-hook-root-owner-gate-and-legacy-order",[&](auto&){
     namespace root=ymcc::steaminfo::updateHookDetail;
     ymcc::steamsession::Observer stopped;
     updateReads.clear();updateOpens=updateCloses=0;
     need(ymcc::steaminfo::readUpdateHookInstallRoot(stopped).empty()&&updateOpens==0,"unowned-updater-read-registry");
     const auto read=[](HKEY hive){return root::registryRoot(hive);};
     const auto current=[](){return true;};
     updateRoots={L"X:/User Steam/",L"Y:\\Machine Steam"};updateTypes={REG_SZ,REG_SZ};
     need(root::selectRoot(current,read)==updateRoots[0]&&updateReads==std::vector<std::size_t>{0},"user-raw-root-should-win");
     updateReads.clear();updateTypes[0]=REG_EXPAND_SZ;
     need(root::selectRoot(current,read)==updateRoots[1]&&updateReads==std::vector<std::size_t>{0,1},"wrong-type-must-use-original-machine-fallback");
     updateReads.clear();updateTypes[0]=REG_SZ;updateRoots[0].assign(519,L'x');
     need(root::selectRoot(current,read)==updateRoots[1]&&updateReads==std::vector<std::size_t>{0,1},"original-updater-buffer-bound-changed");
     updateReads.clear();updateRoots[0].clear();
     unsigned checks=0;
     need(root::selectRoot([&]{return ++checks==1;},read).empty()&&updateReads==std::vector<std::size_t>{0},"revocation-must-block-machine-read");
     updateReads.clear();checks=0;
     need(root::selectRoot([&]{return ++checks<3;},read).empty()&&updateReads==std::vector<std::size_t>{0,1},"revocation-must-discard-root");
     need(updateOpens==updateCloses&&queries==0,"root-only-owner-route-no-process-queries-or-retained-key");
 });
 unsigned failed=0;for(const auto& c:cases)if(!c.value("passed",false))++failed;
 std::cout<<Json{{"selection","native-launch-inventory-consumption"},{"cases",cases},{"caseCount",cases.size()},{"failedCases",failed},{"realSteamActions",0},{"realForeignProcessQueries",foreignProcessOpens},{"realTokenOrRegistryReads",0},{"actualProductWindowsPortsCompiled",true},{"newCanonicalIds",0},{"defaultNativeLaunchGroupExecuted",false}}.dump(2)<<'\n';return cases.empty()?2:failed?1:0;
}
