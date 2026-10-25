#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string>
#include "battery_charge_client.h"
#include "secondary_display_client.h"
using Json=nlohmann::json;
static int checks=0;
static void check(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
static std::wstring wide(const char* input){int n=MultiByteToWideChar(CP_UTF8,0,input,-1,nullptr,0);if(n<=0)throw std::runtime_error("UTF8");std::wstring result(n,L'\0');MultiByteToWideChar(CP_UTF8,0,input,-1,result.data(),n);result.resize(n-1);return result;}
int wmain(int argc,wchar_t** argv){
 HANDLE child=nullptr,event=nullptr;bool sentStop=false;std::wstring emptyRoot,state;
 const auto transport=[&](const Json& input){return ymcc::batterycharge::request(emptyRoot,state,input);};
 try{
  if(argc!=5)throw std::runtime_error("dotnet, fixture DLL, output directory and model required");
  state=argv[3];emptyRoot=state+L"\\no-product-helper";std::filesystem::create_directories(emptyRoot);
  const std::wstring model=argv[4],board=model==L"AYANEOFlipDS"?L"FLIP DS":L"FLIP 1S DS";
  const auto stamp=ymcc::batterycharge::parentStamp();const auto readyName=L"Local\\YMCC.D15.Ready."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(stamp);
  event=CreateEventW(nullptr,TRUE,FALSE,readyName.c_str());check(event!=nullptr,"fixture event failed");
  const auto finalPath=state+L"\\server-result.json";
  std::wstring command=ymcc::batterycharge::quoted(argv[1])+L" "+ymcc::batterycharge::quoted(argv[2])+L" "+std::to_wstring(GetCurrentProcessId())+L" "+std::to_wstring(stamp)+L" "+ymcc::batterycharge::quoted(board)+L" "+ymcc::batterycharge::quoted(readyName)+L" "+ymcc::batterycharge::quoted(finalPath);
  STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
  check(CreateProcessW(argv[1],command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,state.c_str(),&si,&pi)!=0,"fixture process failed");child=pi.hProcess;CloseHandle(pi.hThread);
  check(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"fixture did not publish ready");
  const std::string deviceClass=model==L"AYANEOFlipDS"?"AYANEOFlipDS":"AYANEOFlip1SDS";
  const auto consume=[&](const std::string& op,const Json& args){return ymcc::secondarydisplay::consume(deviceClass,op,args,transport,[]{return true;});};
  auto read=consume("get",Json::object());check(read.value("ok",false)&&read["raw"]==200,"real-pipe initial get failed");
  const auto oldFence=read["fence"];auto result=consume("set",{{"target",50},{"fence",oldFence}});check(result.value("ok",false)&&result["raw"]==127,"real-pipe set readback failed");
  check(!consume("toggle",{{"fence",oldFence}}).value("ok",true),"replayed toggle admitted across pipe");
  read=consume("get",Json::object());transport({{"op","fixture.external"},{"raw",42}});
  check(!consume("set",{{"target",80},{"fence",read["fence"]}}).value("ok",true),"external change overwritten across pipe");
  auto status=transport({{"op","fixture.status"}});check(status["raw"]==42&&status["writes"]==2,"conflict produced writes");
  read=consume("get",Json::object());result=consume("set",{{"target",32767},{"fence",read["fence"]}});check(result.value("ok",false)&&result["raw"]==255,"signed short not clamped by actual managed provider");
  read=consume("get",Json::object());check(consume("toggle",{{"fence",read["fence"]}})["raw"]==0,"real-pipe off failed");read=consume("get",Json::object());check(consume("toggle",{{"fence",read["fence"]}})["raw"]==255,"real-pipe remembered brightness failed");
  read=consume("get",Json::object());transport({{"op","fixture.suspend"}});check(!consume("set",{{"target",25},{"fence",read["fence"]}}).value("ok",true),"suspended controller wrote");transport({{"op","fixture.resume"}});check(!consume("set",{{"target",25},{"fence",read["fence"]}}).value("ok",true),"resume replayed stale fence");
  read=consume("get",Json::object());check(consume("set",{{"target",25},{"fence",read["fence"]}}).value("ok",false),"fresh resumed operation failed");
  read=consume("get",Json::object());transport({{"op","fixture.fault"},{"failure","write-after"}});
  result=consume("set",{{"target",60},{"fence",read["fence"]}});check(!result.value("ok",true)&&result["providerResult"]["stage"]=="brightness-write","partial write became success or lost failure evidence");
  status=transport({{"op","fixture.status"}});check(status["raw"]==153&&status["writes"]==12,"partial write was retried or guessed rollback");transport({{"op","fixture.fault"},{"failure",""}});
  read=consume("get",Json::object());status=transport({{"op","fixture.status"}});const auto opens=status["opens"];
  check(consume("release",Json::object()).value("ok",false),"release failed");status=transport({{"op","fixture.status"}});check(status["raw"]==153&&status["opens"]==opens,"release restored persistent brightness or opened EC");
  check(status["batteries"]==0&&status["opens"]==status["disposals"],"display constructed charge domain or leaked EC session");
  check(transport({{"op","fixture.stop"}}).value("ok",false),"fixture stop failed");sentStop=true;
  check(WaitForSingleObject(child,10000)==WAIT_OBJECT_0,"fixture natural exit missing");DWORD exit=~0u;GetExitCodeProcess(child,&exit);check(exit==0,"fixture exit failed");
  std::ifstream finalFile(finalPath);Json server;finalFile>>server;check(server.value("status",std::string())=="PASS","fixture final status failed");
  std::cout<<Json({{"schemaVersion",1},{"status","PASS"},{"checks",checks},{"fixturePid",pi.dwProcessId},{"fixtureExitCode",exit},{"fixtureNaturalExit",true},{"realNativePipeClient",true},{"actualManagedRuntimeContractAndRouter",true},{"server",server},{"ecHardwareIo",false},{"productHostMainExecuted",false},{"physicalAcceptance",false}}).dump()<<'\n';
  CloseHandle(child);CloseHandle(event);return 0;
 }catch(const std::exception& e){
  if(child){if(!sentStop)try{transport({{"op","fixture.stop"}});}catch(...){}WaitForSingleObject(child,10000);CloseHandle(child);}if(event)CloseHandle(event);
  std::cout<<Json({{"status","FAIL"},{"checks",checks},{"error",e.what()}}).dump()<<'\n';return 1;
 }
}