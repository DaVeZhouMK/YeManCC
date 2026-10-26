#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
// Only the fixture's WinSock terminal selects an ephemeral loopback port.
// Keep production PortLease and all real ownership/busy checks unchanged.
static int WSAAPI resourceFixtureBind(SOCKET socket,const sockaddr* value,int length){
 if(value&&length==sizeof(sockaddr_in)&&value->sa_family==AF_INET){
  auto address=*reinterpret_cast<const sockaddr_in*>(value);
  if(ntohs(address.sin_port)==1337&&address.sin_addr.s_addr==htonl(INADDR_ANY)){
   address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
   return ::bind(socket,reinterpret_cast<const sockaddr*>(&address),sizeof(address));
  }
 }
 return ::bind(socket,value,length);
}
#define bind resourceFixtureBind
#include "../native/decky_binding_worker_entry.h"
#include "../native/decky_sidebar_runtime.h"
#undef bind
#include <condition_variable>
#include <iostream>
#include <thread>
using Json=nlohmann::json;
namespace fs=std::filesystem;
using namespace ymcc::deckyresources;
static void need(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
static void put(const fs::path& path,const std::string& bytes){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<bytes;need(out.good(),"fixture write failed");}
static std::wstring env(const wchar_t* name){wchar_t value[32768]{};const auto n=GetEnvironmentVariableW(name,value,32768);return n&&n<32768?std::wstring(value,n):std::wstring{};}
static bool childPeer(int& exit){
    int argc=0;auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);const auto mode=ymcc::deckybinding::ChildChannel::classify(argc,argv);if(mode==ymcc::deckybinding::ChildMode::ordinary){LocalFree(argv);return false;}
    const auto behavior=env(L"YMCC_RESOURCE_WORKER_FIXTURE");if(behavior.empty()){LocalFree(argv);return ymcc::deckybinding::entry::tryRunCommandLine(GetCommandLineW(),exit);}
    ymcc::deckybinding::ChildChannel channel;const auto opened=channel.openArguments(argc,argv);LocalFree(argv);if(opened!=ymcc::deckybinding::ChildMode::worker){exit=91;return true;}
    try{
        const auto frame=channel.readFrame(3000);need(static_cast<bool>(frame),"fixture request missing");const auto request=Json::parse(frame.payload);
        const auto nonce=ymcc::deckybinding::worker::wide(request.at("home"));put(fs::path(nonce)/L"fixture-child-started",std::to_string(GetCurrentProcessId()));
        if(behavior==L"eof"){exit=0;return true;} // Peer closes without a result; never retry/spin.
        if(behavior==L"bad-reply"){
            const Json reply={{"schema",worker::schema},{"id",std::string(32,'f')},{"generation",request.at("generation")},{"detail",{{"ready",true},{"reason","forged"}}}};
            const auto text=reply.dump();channel.writeFrame(std::vector<std::uint8_t>(text.begin(),text.end()),1000);exit=0;return true;
        }
        // Exact callback does not observe cancellation; only the owned Job can
        // revoke this wait. No fixture release occurs until AFTER measurement.
        std::mutex mutex;std::condition_variable condition;bool released=false;std::unique_lock lock(mutex);condition.wait(lock,[&]{return released;});exit=0;return true;
    }catch(...){exit=92;return true;}
}
static DWORD handles(){DWORD n=0;need(GetProcessHandleCount(GetCurrentProcess(),&n)!=FALSE,"handle snapshot failed");return n;}
static Json cases=Json::array();template<class F>static void check(const char* name,F&& work){try{work();cases.push_back({{"name",name},{"pass",true}});}catch(const std::exception& error){cases.push_back({{"name",name},{"pass",false},{"error",error.what()}});}}
int wmain(int argc,wchar_t** argv){int code=0;if(childPeer(code))return code;try{
    need(argc==4,"explicit fixture result and official data file required");const auto base=fs::absolute(argv[1]);need(base.filename()==L"worker-fixture","scope rejected");const auto steam=base/L"Steam",home=base/L"PowerControl"/L"decky",metadata=base/L"YeManCC"/L"decky-package.json";put(steam/L"steam.exe","inert");put(steam/L".cef-enable-remote-debugging","");
    for(const auto* name:ymcc::deckysetup::managedFiles)put(home/name,"fixture-"+ymcc::deckysetup::pathText(fs::path(name)));
    fs::copy_file(argv[3],home/ymcc::deckysetup::managedFiles[0],fs::copy_options::overwrite_existing);
    Json manifest={{"schemaVersion",1},{"owner","YMCC"},{"origin","official-release"},{"loaderVersion",ymcc::deckyidentity::version},{"loaderSha256",ymcc::deckyidentity::sha256},{"files",Json::array()}};
    for(const auto* name:ymcc::deckysetup::managedFiles)manifest["files"].push_back({{"path",ymcc::deckysetup::pathText(fs::path(name))},{"bytes",fs::file_size(home/name)},{"sha256",worker::digest((home/name).wstring(),[]{return true;})}});put(metadata,manifest.dump());
    const auto normal=[&]{return worker::prepare(steam,home,metadata,[]{return true;},true);};
    check("actual private early-entry child validates current official bytes and primes cache",[&]{const auto r=normal();need(r.value("ready",false)&&r.value("resourceCacheAvailable",false)&&r.value("resourceWorkerDestroyed",false),"real resource worker did not finish with zero resources");});
    check("actual same-image child repairs missing Loader and confirms final identity",[&]{fs::remove(home/ymcc::deckysetup::managedFiles[0]);const auto r=normal();need(r.value("ready",false)&&r.value("resourcesRepaired",0u)==1&&r.value("resourceWorkerDestroyed",false),"real worker did not repair official bytes");});
    const auto count=handles();
    check("forged or stale child response cannot claim ready",[&]{SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"bad-reply");const auto r=normal();SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);need(!r.value("ready",true)&&r.value("resourceWorkerDestroyed",false),"forged resource reply accepted");need(handles()==count,"invalid reply leaked handles");});
    check("child EOF before a result stops promptly without retry or leaked resources",[&]{SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"eof");const auto started=GetTickCount64();const auto r=normal();const auto elapsed=GetTickCount64()-started;SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);need(!r.value("ready",true)&&r.value("reason",std::string{})=="resource-worker-read-failed"&&elapsed<1000&&r.value("resourceWorkerDestroyed",false)&&handles()==count,"resource EOF spun or retained ownership");});
    check("noncooperative resource callback is revoked before the unchanged 3200ms deadline",[&]{SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"stall");const auto started=GetTickCount64();const auto r=normal();const auto elapsed=GetTickCount64()-started;SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);need(elapsed<=3200&&!r.value("ready",true)&&r.value("resourceWorkerDestroyed",false),"resource worker exceeded stop bound or retained Job/IO");need(handles()==count,"stalled resource child leaked handles");const auto pid=static_cast<DWORD>(std::stoul([&]{std::ifstream file(home/L"fixture-child-started");std::string s;file>>s;return s;}()));HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);need(!process||WaitForSingleObject(process,0)==WAIT_OBJECT_0,"stalled child survived");if(process)CloseHandle(process);cases.push_back({{"name","stalled callback actual resources at deadline"},{"pass",true},{"elapsedMs",elapsed},{"deadlineMs",3200},{"destroyed",r.at("resourceWorkerDestroyed")}});});
    check("OFF during a stuck child stops only its owned Job without waiting for callback release",[&]{SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"stall");const auto started=GetTickCount64();const auto r=worker::prepare(steam,home,metadata,[&]{return GetTickCount64()-started<150;},true);const auto elapsed=GetTickCount64()-started;SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);need(elapsed<=1000&&r.value("reason",std::string{})=="preparation-cancelled"&&r.value("resourceWorkerDestroyed",false),"OFF blocked on resource child");need(handles()==count,"OFF resource child leaked handles");});
    check("explicit OFF at entry starts no child and touches no resource",[&]{const auto before=handles();const auto r=worker::prepare(steam,home,metadata,[]{return false;},true);need(!r.value("ready",true)&&r.value("reason",std::string{})=="preparation-cancelled"&&handles()==before,"OFF starts resource worker");});
    check("parent setup consumes child verification without any synchronous resource digest",[&]{ymcc::deckyresources::Options options;options.enabled=false;options.isolatedPreparation=normal;const auto r=ymcc::deckysetup::prepare(steam,home,[](const std::wstring&)->std::string{throw std::runtime_error("parent digest must never run");},false,[]{return true;},metadata,options);need(r.value("ready",false)&&r.value("resourceWorkerDestroyed",false),"parent performed resource IO or lost child verification");});
    check("actual Runtime OFF drains a stuck resource preparation before its original deadline",[&]{
        fs::remove(home/L"fixture-child-started");SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",L"stall");
        ymcc::deckysidebar::Runtime runtime;ymcc::deckysidebar::Config config;std::atomic<bool> destroyed=false;
        config.prepareEnvironment=[&](const std::function<bool()>& current){const auto r=worker::prepare(steam,home,metadata,current,true);destroyed=r.value("resourceWorkerDestroyed",false);return r.value("ready",false)?std::string{}:r.value("reason",std::string{});};
        runtime.initialize(std::move(config));runtime.observeSteam(0);runtime.setEnabled(true);
        const auto entered=GetTickCount64()+1500;while(!fs::exists(home/L"fixture-child-started")&&GetTickCount64()<entered)Sleep(2);
        need(fs::exists(home/L"fixture-child-started"),"actual Runtime resource child did not enter");
        const auto started=GetTickCount64();runtime.shutdown();const auto elapsed=GetTickCount64()-started;SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);
        need(elapsed<=3200&&destroyed&&!runtime.snapshot().enabled&&handles()==count,"actual Runtime OFF retained resource Job or parent worker handles");
    });
    check("actual Runtime committed preparation repairs resource loss without granting Loader launch",[&]{
        ymcc::deckysidebar::Runtime runtime;ymcc::deckysidebar::Config config;std::atomic<unsigned> prepared=0;std::atomic<bool> recovered=false;
        config.prepareEnvironment=[&](const std::function<bool()>& current){const auto r=worker::prepare(steam,home,metadata,current,true);recovered=r.value("ready",false)&&r.value("resourcesRepaired",0u)>0&&r.value("resourceWorkerDestroyed",false);++prepared;return r.value("ready",false)?std::string{}:r.value("reason",std::string{});};
        runtime.initialize(std::move(config));runtime.observeSteam(0);runtime.setEnabled(true);
        auto deadline=GetTickCount64()+2500;while(prepared<1&&GetTickCount64()<deadline)Sleep(2);need(prepared==1,"Runtime resource preparation did not complete");
        fs::remove(home/ymcc::deckysetup::managedFiles[2]);need(runtime.requestPreparationRefresh(77),"committed preparation was not admitted");
        deadline=GetTickCount64()+2500;while(prepared<2&&GetTickCount64()<deadline)Sleep(2);
        need(prepared==2&&recovered&&runtime.snapshot().pid==0,"Ready did not recover resources or started a Loader");
        need(!runtime.requestPreparationRefresh(77),"duplicate Ready restarted preparation");runtime.shutdown();need(handles()==count,"committed resource recovery leaked resources");
    });
    const auto passed=std::count_if(cases.begin(),cases.end(),[](const auto& r){return r.at("pass")==true;});const Json result={{"pass",passed==static_cast<int>(cases.size())},{"count",cases.size()},{"passed",passed},{"cases",cases},{"realSteamActions",0},{"officialLoaderExecuted",false},{"childIsOwnTestImage",true},{"originalStopDeadlineMs",3200}};put(argv[2],result.dump(2));std::cout<<result.dump()<<std::endl;return result.at("pass")==true?0:2;
 }catch(const std::exception& error){SetEnvironmentVariableW(L"YMCC_RESOURCE_WORKER_FIXTURE",nullptr);std::cerr<<error.what()<<std::endl;return 3;}}
