// Narrow mode of the existing native mouse consumer: all transport/storage is memory-only.
#include <iostream>
#include <filesystem>
#include <map>
#include <mutex>
#include <atomic>
#include <functional>
#include <algorithm>
#include <vector>
#include <string>
#include <cwctype>
#include <stdexcept>
#include "json.hpp"
#include "steamdeck_mouse_vdf.h"
using json=nlohmann::json;namespace fspath=std::filesystem;using DWORD=unsigned long;
constexpr int HKEY_CURRENT_USER=1,FALSE=0;static int g_hwnd=1;
static struct{bool isolated=false;}g_aiFanMockSession;static std::map<std::wstring,std::string>disk;static json receipts,inputConfig;
static unsigned writes=0,liveMutations=0;static std::string liveMode="ok";
static std::atomic<bool>g_sdmEventPending{false};
static void Sleep(unsigned){}
static struct{void beginWindow(){}}g_steamSettingsObserver;
static std::string W2U(const std::wstring& value){return {value.begin(),value.end()};}static std::wstring U2W(const std::string& value){return {value.begin(),value.end()};}
static std::wstring sofRegSZ(int,const wchar_t*,const wchar_t*){return L"S:\\Steam";}
static bool sofRegDWORD(int,const wchar_t*,const wchar_t* name,DWORD* out){*out=std::wstring(name)==L"pid"?1234u:42u;return true;}
static bool sofFindLocalConfig(std::wstring* path,std::string* account){*path=L"S:\\Steam\\localconfig.vdf";*account="42";return true;}
static bool sofSteamRunning(){return true;}
static std::string sgReadFile(const std::wstring& path){return disk[path];}
static bool CopyFileW(const wchar_t* from,const wchar_t* to,int){disk[to]=disk[from];return true;}
static bool sgWriteFileAtomic(const std::wstring& path,const std::string& value){++writes;disk[path]=value;return true;}
static json ymSettingsSection(const char*){return receipts;}static bool ymSettingsWriteSection(const char*,const json& value){receipts=value;return true;}
static json effectiveRuntimeInputSettings(){return inputConfig;}
static void ipc_emit(const char*,const json&){}static void appendNativeLifecycleLog(const char*,const json&){}
static bool steamLiveCanDefer(const json&){return false;}
static void steamSettingsQueue(const char*){}
static json steamLiveRequest(DWORD account,json request){
 if(account!=42)return {{"ok",false},{"reason","steam-account-changed"}};
 const auto file=U2W(request.value("path",std::string{}));const auto current=ymcc::steamdeck::desktopLayout(disk[file]).sensitivity;
 if(request.value("operation",std::string{})=="mouse.get")return {{"ok",true},{"percent",current}};
 if(request.value("baseline",-1)!=current)return {{"ok",false},{"reason","desktop-sensitivity-changed"}};
 ++liveMutations;disk[file]=ymcc::steamdeck::replaceSensitivity(disk[file],request.at("percent").get<int>());return {{"ok",true},{"percent",request.at("percent")},{"saved",true}};
}
//__PRODUCTION_RUNTIME__
static const std::wstring path=L"S:\\Steam\\steamapps\\common\\Steam Controller Configs\\42\\config\\413080\\controller_neptune.vdf";
static const std::wstring selection=L"S:\\Steam\\steamapps\\common\\Steam Controller Configs\\42\\config\\configset_controller_neptune.vdf";
static const std::string fixture=R"VDF("controller_mappings" { "controller_type" "controller_neptune" "group" { "id" "77" "mode" "joystick_mouse" "settings" { "sensitivity" "145" } } "preset" { "id" "0" "name" "Default" "group_source_bindings" { "77" "right_joystick active" } } })VDF";
static void reset(){disk.clear();disk[path]=fixture;disk[selection]="\"controller_config\" { \"413080\" { \"autosave\" \"1\" } }";receipts=json::object();inputConfig={{"outputTarget",{{"buttonMappingEnabled",true},{"persona","steamdeck"}}}};writes=liveMutations=0;g_sdmEventPending=false;g_sdmReceiptError.clear();}
static void expect(bool ok,const char* label){if(!ok)throw std::runtime_error(label);std::cout<<"PASS "<<label<<'\n';}
int main(){try{
//__RECEIPT_STORM__
std::cout<<"STEAMDECK_MOUSE_RECEIPT_ONLY_OK hardware=0 services=0 disk=memory\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
