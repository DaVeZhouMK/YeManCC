#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
static ULONGLONG g_setupTestClockOffset=0;
static ULONGLONG setupTestClock() { return ::GetTickCount64()+g_setupTestClockOffset; }
#define GetTickCount64 setupTestClock
#include "gamepad_driver_setup.h"
#undef GetTickCount64
#include <iostream>
#include <functional>
#include <tuple>

using N=ymcc::HidHideSetupNotice;
using json=nlohmann::json;
static ymcc::GamepadDriverSetup* g_fixtureSetup=nullptr;
static json g_setupEmissions=json::array(),g_setupLogs=json::array(),g_completedSnapshots=json::array();
static std::string W2U(const std::wstring& value) {
    if(value.empty())return {};
    const auto n=WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),out.data(),n,nullptr,nullptr);return out;
}
static void appendNativeLifecycleLog(const std::string& event,const json& data) {g_setupLogs.push_back({{"event",event},{"data",data}});}
static void ipc_emit(const std::string& event,const json& data) {g_setupEmissions.push_back({{"event",event},{"data",data}});}
// These two status-only functions are extracted verbatim from the current production main.cpp.
#define g_gamepadDriverSetup (*g_fixtureSetup)
#include "hidhide_setup_publication_fixture.h"
#undef g_gamepadDriverSetup
int wmain(int argc,wchar_t** argv) {
    if(argc==4 && std::wstring(argv[1])==L"--resolve-root") {
        const auto selected=ymcc::GamepadDriverSetup::ResolveRoot(argv[2],argv[3]);
        std::cout<<json{{"resolvedRoot",W2U(selected)},{"deviceOperations",0}}.dump()<<"\n";
        return 0;
    }
    // Test-owned substitute for the setup child: only reads/writes isolated JSON files.
    if (argc>1 && std::wstring(argv[1])==L"--prepare") {
        std::wstring root,report;unsigned owner=0;
        for(int i=2;i<argc;++i) {
            const std::wstring arg=argv[i];
            if(arg==L"--owner-pid" && i+1<argc)owner=std::stoul(argv[++i]);
            else if(arg==L"--install-root" && i+1<argc)root=argv[++i];
            else if(arg==L"--report" && i+1<argc)report=argv[++i];
        }
        const auto tree=std::filesystem::path(root);
        json spec;std::ifstream(tree/L"case.json")>>spec;
        const auto rootUtf8=tree.u8string();
        const unsigned code=spec.value("code",11u);
        json result={{"batch","GP-PREREQ-2"},{"ownerPid",owner},{"installRoot",std::string(rootUtf8.begin(),rootUtf8.end())},
            {"finishedAt","2026-10-13T12:00:00+08:00"},{"exitCode",code},{"hidhide",spec.value("hidhide",std::string{})}};
        if(spec.contains("status"))result["status"]=spec["status"];
        if(spec.contains("prepare"))result["prepare"]=spec["prepare"];
        if(spec.contains("hidmaestro"))result["hidmaestro"]=spec["hidmaestro"];
        if(spec.value("stale",false))result["ownerPid"]=owner+1;
        if(spec.value("hold",false)) {
            // Deliberately seed a complete-looking report while the child is STILL running.
            result["hidhide"]="READY";std::ofstream{std::filesystem::path(report)}<<result.dump();
            std::ofstream(tree/L"child-ready.txt")<<"ready";
            const auto deadline=GetTickCount64()+10000;
            while(!std::filesystem::exists(tree/L"release-child.txt")&&GetTickCount64()<deadline)Sleep(5);
            result["hidhide"]=spec.value("hidhide",std::string{});
        }
        if(!spec.value("noReport",false)) {
            std::ofstream out{std::filesystem::path(report),std::ios::binary};
            if(spec.value("oversized",false))out<<std::string(131073,'x');
            else if(spec.value("invalid",false))out<<"{broken";
            else out<<result.dump();
        }
        std::ofstream(tree/L"launch-count.txt",std::ios::app)<<"1\n";
        return code;
    }
    if(argc!=2)return 2;
    const auto output=std::filesystem::path(argv[1]);
    std::filesystem::create_directories(output);
    json checks=json::array();
    auto check=[&](const char* label,bool ok) {checks.push_back({{"name",label},{"passed",ok}});if(!ok)throw std::runtime_error(label);};
    using S=ymcc::GamepadDriverSetup;
    check("state values retain wire compatibility",S::NotStarted==0&&S::Pending==1&&S::Ready==2&&S::Failed==3&&S::RebootRequired==4&&S::Slow==5&&S::InstallerMissing==6&&S::InstallerInvalid==7&&S::InventoryUnverified==8);
    for (const auto& [code,expected]:std::vector<std::pair<DWORD,S::State>>{{0,S::Ready},{11,S::Failed},{12,S::InstallerMissing},{13,S::InstallerInvalid},{18,S::InventoryUnverified},{19,S::MaintenanceDeferred},{20,S::Failed},{1603,S::Failed},{1641,S::RebootRequired},{3010,S::RebootRequired},{0xffffffffu,S::Failed}})
        check(("exit mapping "+std::to_string(code)).c_str(),S::ResultState(code)==expected);
    check("deferred maintenance stays diagnostic",S::HasWarning(S::MaintenanceDeferred)&&S::MaintenanceDeferred!=S::Ready);
    check("unrecognized inventory stays diagnostic",S::HasWarning(S::InventoryUnverified)&&S::InventoryUnverified!=S::Ready);
    const std::string root="C:\\SOFT\\YeMan";const unsigned owner=100;
    auto report=[&](std::string hh,unsigned code=11) {return json{{"batch","GP-PREREQ-2"},{"ownerPid",owner},{"installRoot",root},
        {"finishedAt","2026-10-13T12:00:00+08:00"},{"exitCode",code},{"hidhide",hh}};};
    auto decide=[&](unsigned code,const json& r){return ymcc::hidHideCompletedSetupNotice(code,r,root,owner);};
    check("HidMaestro failure with healthy HidHide does not prompt",decide(11,report("READY"))==N::None);
    check("admin failure distinct",decide(11,report("ADMIN_REQUIRED"))==N::AdminRequired);
    check("active clients distinct",decide(11,report("CLOSE_CONTROLLER_APPS_FIRST"))==N::CloseControllerApps);
    check("existing partial driver distinct",decide(11,report("PRESENT_NOT_READY_NO_REINSTALL"))==N::ExistingNeedsRepair);
    check("installer process failure",decide(11,report("INSTALL_POSTCONDITION_UNPROVEN"))==N::Incomplete);
    check("missing installer code alone is unverified",decide(12,json{})==N::Unverified);
    check("invalid installer code alone is unverified",decide(13,json{})==N::Unverified);
    for(const auto& [code,status,expected]:std::vector<std::tuple<unsigned,std::string,N>>{
        {12,"HIDHIDE_INSTALLER_MISSING",N::InstallerMissing},{13,"HIDHIDE_INSTALLER_HASH_MISMATCH",N::InstallerInvalid}}) {
        auto installer=report("INSTALL_FRESH",code);installer["status"]=status;
        check(("confirmed installer failure: "+status).c_str(),decide(code,installer)==expected);
        installer["ownerPid"]=owner+1;
        check(("stale installer failure rejected: "+status).c_str(),decide(code,installer)==N::Unverified);
        installer=report("INSTALL_FRESH",code);installer["status"]="OTHER_DEPENDENCY_FAILURE";
        check(("other dependency installer failure: "+status).c_str(),decide(code,installer)==N::Unverified);
        check(("missing installer status rejected: "+status).c_str(),decide(code,report("INSTALL_FRESH",code))==N::Unverified);
        check(("healthy HidHide takes precedence: "+status).c_str(),decide(code,report("READY",code))==N::None);
    }
    check("missing HidHide report retains manual guidance",decide(11,report("MISSING"))==N::Incomplete);
    check("generic process failure is unverified",decide(2,json{})==N::Unverified);
    check("successful setup no report fallback",decide(0,json{})==N::None);
    check("reboot 3010 is not failed install",decide(3010,json{})==N::RebootRequired);
    check("reboot 1641 is not failed install",decide(1641,json{})==N::RebootRequired);
    check("report preserves HidHide reboot",decide(11,report("REBOOT_REQUIRED"))==N::RebootRequired);
    for(const auto& [field,value]:std::vector<std::pair<std::string,json>>{
        {"ownerPid",owner+1},{"installRoot","C:\\other"},{"batch","GP-PREREQ-1"},{"exitCode",0},{"finishedAt",""},
        {"ownerPid","wrong-type"},{"hidhide",42}}) {
        auto stale=report("READY");stale[field]=value;
        check(("invalid report rejected: "+field+"="+value.dump()).c_str(),decide(11,stale)==N::Unverified);
    }
    auto normalized=report("READY");normalized["installRoot"]="c:/soft/yeman/";
    check("root case and slashes normalized",decide(11,normalized)==N::None);
    check("discarded parse is fallback",decide(11,json::parse("{broken",nullptr,false))==N::Unverified);
    const auto paths=output/L"path selection 中文 with spaces";
    const auto packaged=paths/L"installed",app=packaged/L"app",configured=paths/L"legacy test";
    std::filesystem::create_directories(app);
    std::filesystem::create_directories(configured/L"PowerControl");
    auto seedEngine=[](const std::filesystem::path& target) {
        std::filesystem::create_directories(target/L"PowerControl"/L"redist");
        std::ofstream(target/L"PowerControl"/L"redist"/L"HIDMaestroSetup.exe")<<"fixture path only";
    };
    auto select=[&](const std::filesystem::path& exeDir,const std::filesystem::path& powerDir) {
        return ymcc::GamepadDriverSetup::ResolveRoot(exeDir.wstring(),powerDir.wstring());
    };
    seedEngine(packaged);
    check("package wins over stale override missing setup tool",select(app,configured/L"PowerControl")==packaged.wstring());
    seedEngine(configured);
    check("package wins even when old override has setup tool",select(app,configured/L"PowerControl")==packaged.wstring());
    check("flat exe and PowerControl package layout",select(packaged,configured/L"PowerControl")==packaged.wstring());
    check("trailing backslashes normalized",ymcc::GamepadDriverSetup::ResolveRoot(app.wstring()+L"\\",(configured/L"PowerControl").wstring()+L"\\")==packaged.wstring());
    auto forwardApp=app.wstring()+L"/",forwardPower=(configured/L"PowerControl").wstring()+L"/";
    std::replace(forwardApp.begin(),forwardApp.end(),L'\\',L'/');
    std::replace(forwardPower.begin(),forwardPower.end(),L'\\',L'/');
    check("forward slashes normalized",ymcc::GamepadDriverSetup::ResolveRoot(forwardApp,forwardPower)==packaged.wstring());
    const auto developmentApp=paths/L"development"/L"bin";
    std::filesystem::create_directories(developmentApp);
    check("development fallback uses complete configured package",select(developmentApp,configured/L"PowerControl")==configured.wstring());
    const auto absent=paths/L"absent tools";
    std::filesystem::create_directories(absent/L"PowerControl"/L"redist");
    check("all tools absent preserves configured diagnostic root",select(developmentApp,absent/L"PowerControl")==absent.wstring());
    wchar_t executable[MAX_PATH];GetModuleFileNameW(nullptr,executable,MAX_PATH);
    unsigned index=0;
    auto processCase=[&](const char* label,json spec,N expected,S::State expectedState=S::Failed) {
        const auto tree=output/(L"fake root 中文 & 100% with spaces "+std::to_wstring(++index));
        const auto redist=tree/L"PowerControl"/L"redist";
        std::filesystem::create_directories(redist);
        std::filesystem::copy_file(executable,redist/L"HIDMaestroSetup.exe",std::filesystem::copy_options::overwrite_existing);
        std::ofstream(tree/L"case.json")<<spec.dump();
        if(spec.value("oldSamePid",false)) {
            std::ofstream(tree/L"report.json")<<json{{"batch","GP-PREREQ-2"},{"ownerPid",GetCurrentProcessId()},
                {"installRoot",W2U(tree.wstring())},{"finishedAt","2020-01-01T00:00:00+08:00"},{"exitCode",11},{"hidhide","READY"}}.dump();
        }
        ymcc::GamepadDriverSetup setup;g_fixtureSetup=&setup;g_setupEmissions=json::array();g_setupLogs=json::array();
        check((std::string(label)+": not started").c_str(),setup.state()==ymcc::GamepadDriverSetup::NotStarted&&setup.hidHideNotice()==N::None);
        setup.start(tree.wstring(),(tree/L"report.json").wstring());
        setup.start((tree/L"wrong second root").wstring(),(tree/L"wrong-report.json").wstring());
        check((std::string(label)+": pending").c_str(),setup.state()==ymcc::GamepadDriverSetup::Pending&&setup.hidHideNotice()==N::None);
        if(spec.value("hold",false)) {
            const auto readyDeadline=GetTickCount64()+5000;
            while(!std::filesystem::exists(tree/L"child-ready.txt")&&GetTickCount64()<readyDeadline)Sleep(5);
            check("slow child reached controlled barrier",std::filesystem::exists(tree/L"child-ready.txt"));
            check("live child report never consumed",!gamepadDriverSetupPollAndPublish()&&setup.hidHideNotice()==N::None);
            g_setupTestClockOffset=180001;
            check("180s timeout is Slow, not terminal failure",gamepadDriverSetupPollAndPublish()&&setup.state()==ymcc::GamepadDriverSetup::Slow&&setup.hidHideNotice()==N::None);
            check("Slow transition emitted once",g_setupEmissions.size()==1&&g_setupEmissions.back()["data"]["driverSetup"]==5);
            bool quiet=true;for(int poll=0;poll<10000;++poll)quiet=!gamepadDriverSetupPollAndPublish()&&quiet;
            check("10000 slow polls emit no duplicates/retry",quiet);
            check("Slow has no failure guidance",g_setupEmissions.size()==1&&g_setupEmissions.back()["data"]["hidHideSetupNotice"]==0);
            std::ofstream(tree/L"release-child.txt")<<"release";
            g_setupTestClockOffset=0;
        }
        const auto deadline=GetTickCount64()+5000;
        while(setup.active()&&GetTickCount64()<deadline){gamepadDriverSetupPollAndPublish();Sleep(5);}
        check((std::string(label)+": finished result").c_str(),!setup.active()&&setup.result()==spec.value("code",11u)&&setup.hidHideNotice()==expected);
        check((std::string(label)+": exact terminal state").c_str(),setup.state()==expectedState);
        check((std::string(label)+": diagnostic warning retained").c_str(),setup.warnsTargets()==(expectedState!=S::Ready));
        check((std::string(label)+": actual redist path").c_str(),setup.redistDirectory()==redist.wstring());
        const json snapshot=gamepadDriverSetupSnapshot();
        check((std::string(label)+": exact production snapshot").c_str(),snapshot["driverSetup"]==(int)setup.state()&&snapshot["hidHideSetupNotice"]==(int)expected&&snapshot["driverSetupRedistDir"]==W2U(redist.wstring())&&snapshot["driverSetupResult"]==setup.result()&&!snapshot["driverSetupSession"].get<std::string>().empty());
        check((std::string(label)+": dedicated completion event").c_str(),g_setupEmissions.size()==(spec.value("hold",false)?2u:1u)&&g_setupEmissions.back()["event"]=="gamepad.driverSetup"&&g_setupEmissions.back()["data"]==snapshot);
        check((std::string(label)+": unchanged status emits nothing").c_str(),!gamepadDriverSetupPollAndPublish()&&g_setupLogs.size()==g_setupEmissions.size());
        g_completedSnapshots.push_back(snapshot);
        std::string line,extra;std::ifstream count(tree/L"launch-count.txt");std::getline(count,line);std::getline(count,extra);
        check((std::string(label)+": no second child").c_str(),line=="1"&&extra.empty()&&!setup.poll());
    };
    processCase("unrecognized inventory",{{"code",18},{"status","DRIVER_INVENTORY_UNRECOGNIZED"}},N::Unverified,S::InventoryUnverified);
    processCase("unrecognized inventory no report",{{"code",18},{"noReport",true}},N::Unverified,S::InventoryUnverified);
    processCase("unrecognized inventory healthy dependency",{{"code",18},{"hidhide","READY"}},N::None,S::InventoryUnverified);
    const json deferred={{"code",19},{"hidhide","READY"},{"prepare",true},
        {"status","DRIVER_MAINTENANCE_DEFERRED"},{"hidmaestro","DRIVER_MAINTENANCE_DEFERRED"}};
    processCase("verified installed packages maintenance deferred",deferred,N::None,S::MaintenanceDeferred);
    auto missingField=deferred;missingField.erase("hidmaestro");
    processCase("deferred missing dependency result",missingField,N::None,S::Failed);
    auto staleDeferred=deferred;staleDeferred["stale"]=true;
    processCase("deferred stale owner report",staleDeferred,N::Unverified,S::Failed);
    auto checkDeferred=deferred;checkDeferred["prepare"]=false;
    processCase("deferred check-only report",checkDeferred,N::None,S::Failed);
    auto wrongStatus=deferred;wrongStatus["status"]="DEPENDENCY_NOT_READY";
    processCase("deferred mismatched summary",wrongStatus,N::None,S::Failed);
    auto missingReport=deferred;missingReport["noReport"]=true;
    processCase("deferred missing report",missingReport,N::Unverified,S::Failed);
    processCase("future unknown exit healthy dependency",{{"code",20},{"hidhide","READY"}},N::None,S::Failed);
    processCase("future unknown exit no report",{{"code",0xffffffffu},{"noReport",true}},N::Unverified,S::Failed);
    processCase("ready HidHide other dependency failed",{{"hidhide","READY"}},N::None);
    processCase("admin required",{{"hidhide","ADMIN_REQUIRED"}},N::AdminRequired);
    processCase("stale old report",{{"hidhide","READY"},{"stale",true}},N::Unverified);
    processCase("report missing",{{"noReport",true}},N::Unverified);
    processCase("unchanged old report with reused PID",{{"noReport",true},{"oldSamePid",true}},N::Unverified);
    processCase("malformed report",{{"invalid",true}},N::Unverified);
    processCase("bounded report",{{"oversized",true}},N::Unverified);
    processCase("missing locked installer",{{"code",12},{"hidhide","INSTALL_FRESH"},{"status","HIDHIDE_INSTALLER_MISSING"}},N::InstallerMissing,S::InstallerMissing);
    processCase("invalid locked installer",{{"code",13},{"hidhide","INSTALL_FRESH"},{"status","HIDHIDE_INSTALLER_HASH_MISMATCH"}},N::InstallerInvalid,S::InstallerInvalid);
    processCase("unknown code 12 dependency",{{"code",12}},N::Unverified,S::InstallerMissing);
    processCase("unknown code 13 dependency",{{"code",13}},N::Unverified,S::InstallerInvalid);
    processCase("healthy HidHide despite code 12",{{"code",12},{"hidhide","READY"}},N::None,S::InstallerMissing);
    processCase("healthy HidHide despite code 13",{{"code",13},{"hidhide","READY"}},N::None,S::InstallerInvalid);
    processCase("confirmed missing HidHide",{{"hidhide","MISSING"}},N::Incomplete);
    processCase("unproven HidHide installation",{{"hidhide","INSTALL_POSTCONDITION_UNPROVEN"}},N::Incomplete);
    processCase("reboot successful",{{"code",3010},{"hidhide","REBOOT_REQUIRED"}},N::RebootRequired,S::RebootRequired);
    processCase("slow child eventually fails",{{"hold",true},{"hidhide","ADMIN_REQUIRED"}},N::AdminRequired);
    processCase("slow child eventually succeeds",{{"hold",true},{"code",0},{"hidhide","READY"}},N::None,S::Ready);
    ymcc::GamepadDriverSetup missing;g_fixtureSetup=&missing;
    missing.start(select(developmentApp,absent/L"PowerControl"),(output/L"unused.json").wstring());
    check("missing setup tool preserves Win32 file-not-found result",missing.result()==ERROR_FILE_NOT_FOUND);
    check("setup tool launch failure cannot prove HidHide failure",missing.state()==ymcc::GamepadDriverSetup::Failed&&missing.hidHideNotice()==N::Unverified);
    check("failed launch no retry",!missing.poll());
    g_completedSnapshots.push_back(gamepadDriverSetupSnapshot());
    std::ofstream(output/L"native-results.json")<<json{{"passed",true},{"checks",checks.size()},{"cases",checks},{"completedSnapshots",g_completedSnapshots},{"deviceOperations",0}}.dump(2);
    std::cout<<"HIDHIDE_SETUP_NOTICE_NATIVE_PASS checks="<<checks.size()<<" deviceOperations=0\n";
}
