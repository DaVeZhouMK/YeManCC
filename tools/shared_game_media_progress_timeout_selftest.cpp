#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "shared_game_media_download_supervisor.h"
#include <iostream>
using namespace ymcc::media;using namespace ymcc::media::downloads;
static int checks=0;
static void check(bool ok,const char*label){++checks;if(!ok)throw std::runtime_error(label);}
static bool until(const std::function<bool()>&fn,DWORD timeout=3000){auto end=GetTickCount64()+timeout;do{if(fn())return true;Sleep(10);}while(GetTickCount64()<end);return false;}
static int child(wchar_t**argv){const auto root=custom_steam_library::configuredDataRoot();Queue q(root);const auto token=utf8(argv[4]);wchar_t modeText[80]{};GetEnvironmentVariableW(L"YMCC_MEDIA_PROGRESS_TEST_MODE",modeText,80);const std::wstring mode=modeText;q.update(token,[](Json&j){j["workerPid"]=GetCurrentProcessId();j["workerBirth"]=processBirthToken(GetCurrentProcessId());});auto item=q.claim(token);const auto key=stringField(item,"key");if(key.empty())return 3;
 if(mode==L"parallel-stall")q.claim(token);
 if(mode==L"stalled"){q.progress(token,key,"downloading",1024,100000);Sleep(10000);return 4;}
 const auto started=GetTickCount64();uint64_t bytes=0;
 for(;;){const auto elapsed=GetTickCount64()-started;if(elapsed>=1500)break;
  const bool publishing=mode==L"publishing"&&elapsed>=350;
  if(mode!=L"heartbeat"&&!publishing)bytes+=1024;
  const auto phase=mode==L"resolving"?"resolving":publishing?"publishing":"downloading";
  if(!q.progress(token,key,phase,bytes,100000))return 5;
  Sleep(40);
 }
 q.result(token,key,true);q.end(token,"completed");return 0;}
int wmain(int argc,wchar_t**argv){try{if(argc==5&&std::wstring_view(argv[1])==L"--media-background")return child(argv);if(argc!=2&&argc!=3)throw std::runtime_error("fresh isolated root [case] required");const auto base=fs::absolute(argv[1]);if(fs::exists(base))throw std::runtime_error("fresh root required");fs::create_directories(base);wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);Json reports=Json::array();
 for(const auto*mode:{L"progress",L"stalled",L"heartbeat",L"resolving",L"session",L"parallel-stall",L"publishing"}){
  if(argc==3&&std::wstring_view(argv[2])!=mode)continue;const auto root=base/mode;fs::create_directories(root);fs::copy_file(self,root/L"CustomSteamLibrary.exe");atomicWrite(root/L"package-manifest.json",{{"backgroundMediaProtocol",1},{"backgroundCatalogProtocol",1},{"backgroundIdentityProtocol",1},{"backgroundVideoQualityPolicy",2}});SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str());SetEnvironmentVariableW(L"YMCC_MEDIA_PROGRESS_TEST_MODE",mode);
  Queue q(root);q.enqueue(Json::array({{{"appId",570},{"verified",true}}}));if(std::wstring_view(mode)==L"parallel-stall")q.enqueue(Json::array({{{"appId",730},{"verified",true}}}));const bool success=std::wstring_view(mode)==L"progress";const std::string expected=std::wstring_view(mode)==L"stalled"||std::wstring_view(mode)==L"parallel-stall"?"worker-stalled":std::wstring_view(mode)==L"session"?"session-timeout":"task-timeout";
  const auto started=GetTickCount64();Json state;
  {Supervisor owner(root,root/L"CustomSteamLibrary.exe",Limits{280,600,std::wstring_view(mode)==L"session"?1000ull:4000ull,20,0,60000},ResourcePorts{[]{return 1;},[](uint64_t){return std::optional<double>{0};}});q.configure(true,"");owner.wake(true);
   if(success)check(until([&]{state=q.snapshot();return state.value("completed",0)==1&&state.value("workerPid",DWORD{0})==0;},2500),"continuous real byte progress must survive the shorter per-task ceiling");
   else check(until([&]{state=q.snapshot();return state.value("reason",std::string{})==expected&&state.value("workerPid",DWORD{0})==0;},2500),"unproductive or over-budget child is still stopped with the specific reason");
   check(state.value("activeCount",0)==0,"completed or stopped child releases all active lanes");if(!success){check(state.value("remaining",0)>0,"interrupted work remains recoverable");check(state.value("nextDueAt",int64_t{0})>now()+50000,"failure retains bounded retry rather than spinning");}else check(GetTickCount64()-started>1400,"passing result really crossed the original task wall time");
  }
  reports.push_back({{"case",utf8(mode)},{"elapsedMs",GetTickCount64()-started},{"ok",true},{"reason",state.value("reason",std::string{})}});
 }
 Json result={{"ok",true},{"checks",checks},{"cases",reports},{"scope","Actual production supervisor, own child process and queue progress; accelerated timeouts and explicit AC/CPU fixture, no live application or video/cache writes"}};atomicWrite(base/L"result.json",result);std::cout<<result.dump()<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 2;}}
