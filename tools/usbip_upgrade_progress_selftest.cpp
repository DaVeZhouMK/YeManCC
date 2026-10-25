#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include "gamepad_driver_setup.h"
#include <iostream>
#include <thread>
using json=nlohmann::json;
static std::string utf8(const std::wstring& s) {auto text=std::filesystem::path(s).u8string();return {text.begin(),text.end()};}
static void write(const std::filesystem::path& p,const json& v){std::ofstream out(p,std::ios::binary);out<<v.dump();}
int wmain(int argc,wchar_t** argv) {
    if(argc>1 && std::wstring(argv[1])==L"--prepare") {
        std::wstring root,report,session;unsigned owner=0;
        for(int i=2;i<argc;++i){std::wstring a=argv[i];if(a==L"--owner-pid")owner=std::stoul(argv[++i]);else if(a==L"--install-root")root=argv[++i];else if(a==L"--report")report=argv[++i];else if(a==L"--setup-session")session=argv[++i];}
        auto dir=std::filesystem::path(root);json cfg;std::ifstream(dir/L"case.json")>>cfg;
        auto stage=[&](std::string phase,unsigned sequence,bool stale=false){write(std::filesystem::path(report+L".usbip.json"),{{"batch","GP-USBIP-UPGRADE-1"},{"ownerPid",owner},{"installRoot",utf8(root)},{"session",utf8(session)+(stale?"old":"")},{"usbipUpgrade",{{"schemaVersion",1},{"phase",phase},{"installedVersion","0.9.7.7"},{"targetVersion","0.9.8.1"},{"detail","memory child; no driver IO"},{"sequence",sequence}}}});};
        stage("removing",2);write(dir/L"ready.json",true);
        auto until=GetTickCount64()+5000;while(!std::filesystem::exists(dir/L"next.json")&&GetTickCount64()<until)Sleep(5);
        stage("installing",1); // out-of-order same session
        write(dir/L"old-ready.json",true);until=GetTickCount64()+5000;while(!std::filesystem::exists(dir/L"finish.json")&&GetTickCount64()<until)Sleep(5);
        stage("complete",3,cfg.value("staleFinal",false));
        write(std::filesystem::path(report),{{"batch","GP-PREREQ-2"},{"ownerPid",owner},{"installRoot",utf8(root)},{"finishedAt","2026-10-23T10:00:00+08:00"},{"exitCode",0},{"hidhide","READY"}});return 0;
    }
    if(argc!=2)return 2;const auto out=std::filesystem::path(argv[1]);std::filesystem::create_directories(out);json checks=json::array();
    auto need=[&](const char* name,bool value){checks.push_back({{"name",name},{"pass",value}});if(!value)throw std::runtime_error(name);};
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    for(bool stale:{false,true}) {
        auto dir=out/(stale?L"stale":L"valid");std::filesystem::create_directories(dir/L"PowerControl"/L"redist");std::filesystem::copy_file(executable,dir/L"PowerControl"/L"redist"/L"HIDMaestroSetup.exe",std::filesystem::copy_options::overwrite_existing);write(dir/L"case.json",{{"staleFinal",stale}});
        ymcc::GamepadDriverSetup setup;setup.start(dir.wstring(),(dir/L"report.json").wstring());
        auto wait=[&](const wchar_t* marker){auto end=GetTickCount64()+6000;while(!std::filesystem::exists(dir/marker)&&GetTickCount64()<end)Sleep(5);need("owned child reached marker",std::filesystem::exists(dir/marker));};
        wait(L"ready.json");need("pending report publication edge",setup.poll());need("actual current-session removing stage",setup.usbipUpgradeSnapshot().value("phase","")=="removing");need("state read does not complete an active installer",setup.active());
        write(dir/L"next.json",true);wait(L"old-ready.json");setup.poll();need("lower sequence cannot replace removing",setup.usbipUpgradeSnapshot().value("phase","")=="removing");
        write(dir/L"finish.json",true);auto end=GetTickCount64()+6000;while(setup.active()&&GetTickCount64()<end){setup.poll();Sleep(5);}need("owned child exit observed",!setup.active());need("valid complete or rejected stale final",setup.usbipUpgradeSnapshot().value("phase","")==(stale?"unverified":"complete"));
        auto saved=setup.usbipUpgradeSnapshot();need("completed polling does no work",!setup.poll());need("late poll cannot revive a completed stage",saved==setup.usbipUpgradeSnapshot());
    }
    write(out/L"progress-result.json",{{"status","PASS"},{"checks",checks},{"actualConsumer","GamepadDriverSetup.start/poll/usbipUpgradeSnapshot"},{"hardwareIo",false}});std::cout<<"USBIP_PROGRESS_PASS checks="<<checks.size()<<" hardwareIo=false\n";
}
