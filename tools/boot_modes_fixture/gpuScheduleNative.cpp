#include <windows.h>
#include "json.hpp"
#include <string>
#include <vector>
#include <system_error>
#include <iostream>
#include <fstream>
#include <stdexcept>
using json=nlohmann::json;
namespace test {
 bool main=false,backup=false,locked=false,mismatch=false;int pathError=0,identityReads=0,gets=0,creates=0,sets=0,closes=0;
 LONG initialResult=ERROR_SUCCESS,createResult=ERROR_SUCCESS,writeResult=ERROR_SUCCESS,readbackResult=ERROR_SUCCESS;
 DWORD value=2;std::vector<json> logs;
 void need(bool c,const char* why){if(!c)throw std::runtime_error(why);}
}
struct {bool isolated=false;}g_aiFanMockSession;
struct {std::string processor;}g_machineIdentity;
static const std::wstring YM_SETTINGS_FILE=L"main",YM_SETTINGS_BACKUP_FILE=L"backup";
struct SettingsFileGuard{SettingsFileGuard(){test::need(!test::locked,"recursive settings lock");test::locked=true;}~SettingsFileGuard(){test::locked=false;}};
namespace fspath {bool exists(const std::wstring& path,std::error_code& e){test::need(test::locked,"unlocked configuration probe");const int which=path==YM_SETTINGS_FILE?1:2;if(test::pathError==which)e=std::error_code(5,std::generic_category());else e.clear();return which==1?test::main:test::backup;}}
static void ymccResolveMachineIdentityOnce(){++test::identityReads;}
static void appendNativeLifecycleLog(const char* event,const json& data){test::logs.push_back({{"event",event},{"details",data}});}
static void checkKey(LPCWSTR key,LPCWSTR name){test::need(test::locked,"registry outside settings lock");test::need(std::wstring(key)==L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers"&&std::wstring(name)==L"HwSchMode","wrong registry target");}
static LSTATUS fixtureGet(HKEY root,LPCWSTR key,LPCWSTR name,DWORD flags,LPDWORD,void* data,LPDWORD bytes){test::need(root==HKEY_LOCAL_MACHINE&&flags==RRF_RT_REG_DWORD,"wrong read registry options");checkKey(key,name);const LONG result=++test::gets==1?test::initialResult:test::readbackResult;if(result==ERROR_SUCCESS){*static_cast<DWORD*>(data)=test::value;*bytes=sizeof(DWORD);}return result;}
static LSTATUS fixtureCreate(HKEY root,LPCWSTR key,DWORD,LPWSTR,DWORD,REGSAM access,const LPSECURITY_ATTRIBUTES,PHKEY handle,LPDWORD){test::need(root==HKEY_LOCAL_MACHINE&&access==(KEY_SET_VALUE|KEY_QUERY_VALUE),"wrong create options");checkKey(key,L"HwSchMode");++test::creates;if(test::createResult==ERROR_SUCCESS)*handle=reinterpret_cast<HKEY>(1);return test::createResult;}
static LSTATUS fixtureSet(HKEY,LPCWSTR name,DWORD,DWORD type,const BYTE* data,DWORD bytes){test::need(std::wstring(name)==L"HwSchMode"&&type==REG_DWORD&&bytes==sizeof(DWORD),"wrong write shape");const auto value=*reinterpret_cast<const DWORD*>(data);test::need(value==1,"disable must write 1, not 0 or 2");++test::sets;if(test::writeResult==ERROR_SUCCESS&&!test::mismatch)test::value=value;return test::writeResult;}
static LSTATUS fixtureClose(HKEY){++test::closes;return ERROR_SUCCESS;}
#define RegGetValueW fixtureGet
#define RegCreateKeyExW fixtureCreate
#define RegSetValueExW fixtureSet
#define RegCloseKey fixtureClose
#include "gpu_schedule_first_run_native.inc"
#undef RegGetValueW
#undef RegCreateKeyExW
#undef RegSetValueExW
#undef RegCloseKey
static void reset(){test::main=test::backup=test::locked=test::mismatch=false;test::pathError=test::identityReads=test::gets=test::creates=test::sets=test::closes=0;test::initialResult=test::createResult=test::writeResult=test::readbackResult=ERROR_SUCCESS;test::value=2;test::logs.clear();g_aiFanMockSession.isolated=false;g_machineIdentity.processor="AMD RYZEN AI MAX+ 395 w/ Radeon 8060S";}
int main(int argc,char** argv){
 json cases=json::array();const std::string selected=argc>1?argv[1]:"";
 const auto check=[&](const std::string& name,const auto& run){if(!selected.empty()&&selected!=name)return;try{run();cases.push_back({{"name",name},{"ok",true}});}catch(const std::exception& e){cases.push_back({{"name",name},{"ok",false},{"error",e.what()}});}};
 const std::vector<std::pair<std::string,bool>> names={{"AMD RYZEN AI MAX+ 395 w/ Radeon 8060S",true},{"AMD Ryzen AI Max+ PRO 395",true},{"amd ryzen ai max 395",true},{"AMD Ryzen AI Max+ 388 w/ Radeon 8060S",true},{"AMD Ryzen AI Max+ PRO 388",true},{"amd ryzen ai max+ 388",true},{"AMD Ryzen AI Max 385 w/ Radeon 8050S",true},{"AMD Ryzen AI Max PRO 385",true},{"amd ryzen ai max 385",true},{"AMD Ryzen AI Max 390",false},{"AMD Ryzen AI Max+ 392",false},{"AMD Ryzen AI Max+ 3950",false},{"AMD Ryzen AI Max 3850",false},{"AMD Ryzen AI Max+ 3880",false},{"AMD Ryzen 9 3950X",false},{"Intel Core Ultra 9 395",false},{"AMD Ryzen AI Z2 Extreme",false},{"AMD Radeon 395",false},{"Unknown CPU",false},{"AMD Ryzen AI 9 HX 385",false},{"AMD Ryzen AI Max 390 w/ Radeon 395",false},{"AMD Ryzen AI Max 392 w/ Radeon 388",false},{"AMD Ryzen AI Max 380 w/ Radeon 385",false},{"AMD Ryzen(TM) AI Max+ PRO 388",true},{"AMD Ryzen AI Max+",false},{"AMD Ryzen AI Max+ PRO",false}};
 for(const auto& [name,expected]:names)check("CPU identity: "+name,[&]{reset();g_machineIdentity.processor=name;initializeGpuScheduleFirstRunDefault();test::need(test::identityReads==1,"CPU identity not queried once");if(expected)test::need(test::gets==2&&test::sets==1&&test::closes==1&&test::value==1&&test::logs.back()["details"]["ok"]==true,"target SKU did not disable and verify HAGS");else test::need(test::gets==0&&test::creates==0&&test::sets==0&&test::value==2,"other CPU altered system HAGS");});
 for(const auto& sku:std::vector<std::string>{"385","388","395"}){
  const auto processor=std::string("AMD Ryzen AI Max")+(sku=="385"?" ":"+ ")+sku;
  check("fresh "+sku+" writes OFF and next configured launch preserves user ON",[&]{reset();g_machineIdentity.processor=processor;initializeGpuScheduleFirstRunDefault();test::need(test::sets==1&&test::value==1&&test::gets==2,"first disable not read back");test::main=true;test::value=2;initializeGpuScheduleFirstRunDefault();test::need(test::sets==1&&test::identityReads==1&&test::value==2,"existing user ON overwritten");});
  check("fresh "+sku+" already disabled performs no write",[&]{reset();g_machineIdentity.processor=processor;test::value=1;initializeGpuScheduleFirstRunDefault();test::need(test::sets==0&&test::creates==0&&test::gets==2&&test::logs.back()["details"]["ok"]==true,"already disabled wrote or lacked confirmation");});
  check("fresh "+sku+" missing DWORD writes explicit OFF",[&]{reset();g_machineIdentity.processor=processor;test::initialResult=ERROR_FILE_NOT_FOUND;initializeGpuScheduleFirstRunDefault();test::need(test::sets==1&&test::value==1&&test::logs.back()["details"]["ok"]==true,"missing preference did not become OFF for exception");});
  for(int which=0;which<5;++which)check(sku+" existing backup error isolation guard "+std::to_string(which),[&]{reset();g_machineIdentity.processor=processor;if(which==0)test::main=true;else if(which==1)test::backup=true;else if(which==2)test::pathError=1;else if(which==3)test::pathError=2;else g_aiFanMockSession.isolated=true;initializeGpuScheduleFirstRunDefault();test::need(test::identityReads==0&&test::gets==0&&test::sets==0,"existing/unknown config touched HAGS");});
 }
 for(const DWORD system:{0u,1u,2u})check("other model system value "+std::to_string(system)+" remains untouched",[&]{reset();test::value=system;g_machineIdentity.processor="AMD Ryzen AI Max 390";initializeGpuScheduleFirstRunDefault();test::need(test::gets==0&&test::creates==0&&test::sets==0&&test::value==system,"ordinary clean first run overrode system");});
 check("other model missing system DWORD is not created",[&]{reset();test::initialResult=ERROR_FILE_NOT_FOUND;g_machineIdentity.processor="Intel Core Ultra 7 258V";initializeGpuScheduleFirstRunDefault();test::need(test::gets==0&&test::creates==0&&test::sets==0,"ordinary clean first run created HAGS value");});
 for(int failure=0;failure<5;++failure)check("registry failure/readback mismatch "+std::to_string(failure),[&]{reset();if(failure==0)test::initialResult=ERROR_ACCESS_DENIED;else if(failure==1)test::createResult=ERROR_ACCESS_DENIED;else if(failure==2)test::writeResult=ERROR_ACCESS_DENIED;else if(failure==3)test::readbackResult=ERROR_ACCESS_DENIED;else test::mismatch=true;initializeGpuScheduleFirstRunDefault();const auto& d=test::logs.back()["details"];test::need(d["ok"]==false&&d["error"].get<LONG>()!=0&&test::sets<=1,"failed write/readback reported success or retried");});
 unsigned failed=0;for(const auto& c:cases)if(!c["ok"].get<bool>())++failed;const json result={{"status",failed?"FAIL":"PASS"},{"cases",cases},{"passed",cases.size()-failed},{"failed",failed},{"actualProductionRuntimeCompiled",true},{"realRegistryWrites",0},{"realDeviceOperations",0}};std::cout<<result.dump(2)<<'\n';return failed?1:0;
}
