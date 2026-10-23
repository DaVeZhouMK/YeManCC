#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_cssloader_use.h"
#include <fstream>
#include <iostream>
#include <atomic>
using namespace ymcc;
static unsigned checks=0;
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);++checks;std::cout<<"PASS "<<text<<std::endl;}
static bool running(HANDLE p){return p&&WaitForSingleObject(p,0)==WAIT_TIMEOUT;}
static std::wstring self(){wchar_t s[32768]{};GetModuleFileNameW(nullptr,s,32768);return s;}
struct Child {HANDLE value=nullptr;~Child(){if(value){TerminateProcess(value,0);WaitForSingleObject(value,2000);CloseHandle(value);}}};
static HANDLE launch(const std::wstring& exe, const std::wstring& args,steamcssuse::Group* group=nullptr){
 auto command=L"\""+exe+L"\" "+args;STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
 if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&si,&pi))throw std::runtime_error("fixture launch");
 if(group&&!group->assign(pi.hProcess)){TerminateProcess(pi.hProcess,1);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);throw std::runtime_error("fixture job");}
 ResumeThread(pi.hThread);CloseHandle(pi.hThread);return pi.hProcess;
}
int wmain(int argc,wchar_t**argv){try{
 if(argc>1&&std::wstring(argv[1])==L"--dummy"){Sleep(INFINITE);return 0;}
 if(argc!=2)throw std::runtime_error("evidence root required");
 const auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
 const auto backend=root/L"CssLoader-Standalone-Headless.exe";std::filesystem::copy_file(self(),backend,std::filesystem::copy_options::overwrite_existing);
 std::filesystem::create_directories(root/L"steam-appearance");
 auto entry=root/L"steam-appearance.ps1",module=root/L"steam-appearance/resident.ps1",renderer=root/L"steam-appearance/idle.ps1";
 std::ofstream(renderer)<<"AppearanceRenderProtocol=4 function Get-AppearanceVariableDefaults";
 std::ofstream(entry)<<"ResidentBackendPolicy=3 AutomaticResultProtocol=2 ManualObservationProtocol=1";std::ofstream(module)<<"function Start-ResidentAppearanceBackend";
 steamcssuse::verifyResidentControl(entry.wstring());check(true,"matching resident script and module pass native preflight");
 std::ofstream(entry)<<"AutomaticResultProtocol=1";bool refused=false;try{steamcssuse::verifyResidentControl(entry.wstring());}catch(...){refused=true;}check(refused,"old script is refused before it can create job-owned CSS");
 std::ofstream(entry)<<"ResidentBackendPolicy=3 AutomaticResultProtocol=2 ManualObservationProtocol=1";std::filesystem::remove(module);refused=false;try{steamcssuse::verifyResidentControl(entry.wstring());}catch(...){refused=true;}check(refused,"missing resident module is refused before helper creation");
 std::ofstream(module)<<"function Start-ResidentAppearanceBackend";std::ofstream(renderer)<<"AppearanceRenderProtocol=2";
 refused=false;try{steamcssuse::verifyResidentControl(entry.wstring());}catch(...){refused=true;}check(refused,"old static-only render component is refused before helper creation");
 std::filesystem::remove(renderer);refused=false;try{steamcssuse::verifyResidentControl(entry.wstring());}catch(...){refused=true;}check(refused,"missing render component is refused before helper creation");
 std::ofstream(renderer)<<"AppearanceRenderProtocol=4 function Get-AppearanceVariableDefaults";
 Child css;css.value=launch(backend.wstring(),L"--dummy");
 DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
 for(unsigned i=0;i<10;++i){steamcssuse::Group group({},[]{return steamcsseditor::State::Absent;});group.open();group.adopt(backend.wstring());auto result=group.close();check(result.verified==1&&result.ok()&&running(css.value),"verified existing backend survives request release");}
 {steamcssuse::Group group({},[]{return steamcsseditor::State::Absent;});group.open();Child helper;helper.value=launch(self(),L"--dummy",&group);group.adopt(backend.wstring());group.close();check(WaitForSingleObject(helper.value,2000)==WAIT_OBJECT_0,"helper job still destroys its own pending work");check(running(css.value),"helper job never owns the adopted resident CSS");}
 for(auto state:{steamcsseditor::State::Active,steamcsseditor::State::Unknown}){steamcssuse::Group group({},[state]{return state;});group.open();Child helper;helper.value=launch(self(),L"--dummy",&group);auto result=group.close();check(result.editorProtected&&WaitForSingleObject(helper.value,2000)==WAIT_OBJECT_0&&running(css.value),"editor takeover retains CSS while bounding helper lifetime");}
 GetProcessHandleCount(GetCurrentProcess(),&after);check(after<=before+1,"repeated use releases all observation and job handles");
 for(const bool confirmed:{true,false}) {
  auto link=root/(confirmed?L"resident-ready.txt":L"resident-unconfirmed.txt");std::ofstream(link)<<backend.string();
  std::atomic<unsigned> probes{0},rebinds{0},uses{0};steamcss::Runtime observer;steamcss::Config config;
  config.linkFile=link.wstring();config.delays={1,2,3};config.modeDelays={1,2,3};
  auto existing=[&]{HANDLE copy=nullptr;DuplicateHandle(GetCurrentProcess(),css.value,GetCurrentProcess(),&copy,SYNCHRONIZE,FALSE,0);return steamcss::Result{copy,GetProcessId(css.value),"running",{},false};};
  config.ensure=[&](const auto&,const auto&,const auto&){++probes;return existing();};
  config.probeExisting=[&]{++rebinds;return existing();};
  config.confirmIdle=[&](const auto&,bool,bool){++uses;return CreateEventW(nullptr,TRUE,TRUE,nullptr);};
  config.isResidentConfirmed=[&](const auto&){return confirmed;};config.readMode=[](const auto&){return steamcss::UiMode::BigPicture;};
  check(observer.initialize(config),"shared CSS monitor initializes without a real Steam probe");
  observer.observeSteam({root.wstring(),42,0,100});auto deadline=GetTickCount64()+3000;
  while(observer.snapshot().reason!=(confirmed?"appearance-applied-resident":"appearance-unconfirmed")&&GetTickCount64()<deadline)Sleep(5);
  check(observer.snapshot().phase==(confirmed?"ready":"waiting-appearance"),"shared monitor publishes readiness only for an actually confirmed resident use");
  Sleep(100);check(rebinds==1&&probes==1&&uses==1,"helper completion rebinds the exit handle exactly once without another search loop");
  observer.shutdown();check(running(css.value),"shared monitor shutdown closes handles and retains the resident backend");
 }
 DWORD values[8]{0x594D4353,2,8,0,31,1,0,0};steamcssuse::Cleanup valid;check(steamcssuse::readAutomaticResult(values,valid),"resident result protocol accepted");valid.helperExitKnown=true;steamcssuse::requireAutomaticConfirmation(valid,true);check(!valid.error,"resident completion requires all identity and DOM flags");
 values[1]=1;steamcssuse::Cleanup old;check(!steamcssuse::readAutomaticResult(values,old),"old backend-exit protocol cannot masquerade as resident confirmation");
 values[1]=2;values[4]=3;steamcssuse::Cleanup partial;steamcssuse::readAutomaticResult(values,partial);partial.helperExitKnown=true;steamcssuse::requireAutomaticConfirmation(partial,true);check(partial.error&&partial.helperFailureCode==12,"partial confirmation is never reported as applied");
 std::ofstream(root/L"result.json")<<"{\"ok\":true,\"passed\":"<<checks<<",\"realSteamOperations\":0,\"realCssOperations\":0}";
 return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
