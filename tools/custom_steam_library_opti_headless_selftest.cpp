#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "custom_steam_library_storage_isolation.h"
#define wWinMain unusedHostMain
#include "../native/custom-steam-library/workspace_host.cpp"
#undef wWinMain
#include <iostream>
namespace startup=ymcc::optiStartup;
static unsigned passed=0;
static json cases=json::array();
static void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
template<class F>static void test(const char* name,F f){const auto tick=GetTickCount64();f();++passed;cases.push_back({{"name",name},{"passed",true},{"elapsedMs",GetTickCount64()-tick}});std::cout<<"PASS "<<name<<std::endl;}
static bool until(const std::function<bool()>& f,DWORD limit=5000){const auto end=GetTickCount64()+limit;do{if(f())return true;Sleep(10);}while(GetTickCount64()<end);return f();}
static fs::path self(){return startup::image(GetCurrentProcess());}
static unsigned windows(){unsigned n=0;EnumWindows([](HWND h,LPARAM p)->BOOL{DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==GetCurrentProcessId())++*reinterpret_cast<unsigned*>(p);return TRUE;},reinterpret_cast<LPARAM>(&n));return n;}
struct Fixture {
    fs::path root,child;
    startup::Owner owner;
    startup::Handle observed;
    std::string original="[{\"Name\":\"OPT only\",\"IsFavorite\":true}]";
    Fixture(const fs::path& base,const std::string& name,std::string mode="normal"){
        root=base/toWide(name);child=self().parent_path()/L"CustomSteamLibrary"/L"CustomSteamLibrary.exe";
        fs::create_directories(root/L"NativeGame");
        writeJsonWithLocalBackup(root/L"fixture.json",{{"mode",mode}},false);
        writeJsonWithLocalBackup(root/L"state"/L"library-scan.json",{{"games",json::array({{{"steamNative",true},{"nativeSteamAppId",123},
            {"gameDirectory",toUtf8((root/L"NativeGame").wstring())},{"directoryName","Native fixture"},{"primaryExecutable",nullptr}}})}},false);
        // Explicit OFF is persisted and never changed by this startup task.
        writeJsonWithLocalBackup(root/L"config"/L"game-media-downloads.json",{{"enabled",false},{"session","wallpaper-off"}},false);
        writeHostBytesAtomic(root/L"OptiscalerClient"/L"games.json",original);
        SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str());
    }
    void start(DWORD delay=50,DWORD budget=3000){check(owner.start(child,GetTickCount64(),{delay,budget}),"Initial schedule not accepted");}
    void finished(DWORD timeout=5000){check(until([&]{return owner.finished();},timeout),"Startup task failed to exit within bound");owner.stop();}
    void observe(DWORD limit=5000){check(until([&]{return fs::exists(root/L"child-started.json");},limit),"No owned startup child");
        const auto pid=readJson(root/L"child-started.json",json::object()).value("pid",DWORD{0});
        observed.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);check(observed.value,"Cannot retain exact child handle");
        writeHostBytesAtomic(root/L"child-observed","yes");}
    void unchanged(){check(readText(root/L"OptiscalerClient"/L"games.json")==original,"Failed/cancelled run changed OPT catalog");}
};
int wmain(int argc,wchar_t** argv){try{
    if(argc>1&&std::wstring_view(argv[1])==L"--opti-startup"){
        const auto root=custom_steam_library::configuredDataRoot();
        const auto config=readJson(root/L"fixture.json",json::object());const auto mode=config.value("mode",std::string{});
        const auto appRoot=self().parent_path();
        writeJsonWithLocalBackup(root/L"child-started.json",{{"pid",GetCurrentProcessId()}},false);
        until([&]{return fs::exists(root/L"child-observed");},2000);
        std::optional<int> result;
        if(mode=="hang"||mode=="wait-cancel") {
            result=startup::entrypoint(appRoot,[&](const std::function<bool()>& cancelled){
                if(mode=="hang")Sleep(INFINITE);
                while(!cancelled())Sleep(10);
            });
        } else {
            result=runLibraryOptiStartupEntrypoint(appRoot,[&]{return root/L"OptiscalerClient";},[&]{
                if(mode=="throw")throw std::runtime_error("fixture process probe error");
                return mode=="client-running";
            });
            const auto receipt=fs::exists(root/L"state"/L"last-opti-client-supplement.json")
                ? readJson(root/L"state"/L"last-opti-client-supplement.json",json::object())
                : json{{"status",result.value_or(99)==15?"error":"cancelled"}};
            writeJsonWithLocalBackup(root/L"child-finished.json",{{"result",receipt},{"topLevelWindows",windows()},
                {"hostWindow",g_window!=nullptr},{"webview",g_webview.Get()!=nullptr},{"backgroundThreads",g_hostThreads.size()}},false);
        }
        return result.value_or(99);
    }
    check(argc==2,"Pass a fresh Build fixture root");const auto base=fs::absolute(argv[1]).lexically_normal();
    const auto workspace=fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path();
    check((normalizedPathKey(base).starts_with(normalizedPathKey(workspace/L"Build")+L"\\")||normalizedPathKey(base).starts_with(normalizedPathKey(workspace/L"Testing"/L"YMCC"/L"Evidence"/L"Build")+L"\\"))&&!fs::exists(base),"Fixture escaped fresh test Build directory");
    fs::create_directories(base);check(self().filename()==L"YeManCC.exe","Use isolated expected parent image name");
    fs::create_directories(self().parent_path()/L"CustomSteamLibrary");
    check(CopyFileW(self().c_str(),(self().parent_path()/L"CustomSteamLibrary"/L"CustomSteamLibrary.exe").c_str(),TRUE),"Could not prepare owned fixture child");
    test("YMCC startup schedules exactly once after real 30 seconds even with wallpaper OFF",[&]{
        Fixture f(base,"default-30s");const auto off=readText(f.root/L"config"/L"game-media-downloads.json");
        const auto tick=GetTickCount64();f.start(startup::delayMs,5000);
        check(!f.owner.start(f.child,tick),"Repeated startup scheduled twice");Sleep(150);check(f.owner.launches()==0,"Launched before delay");f.unchanged();
        f.observe(35000);check(GetTickCount64()-tick>=30000,"Production startup delay bypassed");f.finished(5000);
        const auto done=readJson(f.root/L"child-finished.json",json::object());
        check(done["result"]["status"]=="synced"&&done["topLevelWindows"]==0&&done["hostWindow"]==false&&done["webview"]==false&&done["backgroundThreads"]==0,"Startup supplement did not stay headless");
        const auto games=json::parse(readText(f.root/L"OptiscalerClient"/L"games.json"));check(games.size()==2&&games[0]["Name"]=="OPT only"&&games[1]["AppId"]=="123","Startup catalog metadata wrong");
        check(readText(f.root/L"config"/L"game-media-downloads.json")==off,"Startup enabled/changed animation tasks");
        check(!f.owner.start(f.child,GetTickCount64())&&f.owner.launches()==1,"Completed pass could be restarted");
    });
    test("running OPT consumes the sole attempt without retries",[&]{
        Fixture f(base,"running","client-running");f.start();f.observe();f.finished();f.unchanged();
        check(f.owner.launches()==1&&!f.owner.start(f.child,GetTickCount64()),"Running client was retried");
        check(readJson(f.root/L"child-finished.json",json::object())["result"]["reason"]=="client-running","Running client not skipped");
    });
    test("first probe error exits immediately, silently, without later work or retries",[&]{
        Fixture f(base,"error","throw");f.start();f.observe();f.finished();f.unchanged();
        check(f.owner.launches()==1&&!f.owner.start(f.child,GetTickCount64()),"Error was retried");
        check(!fs::exists(f.root/L"state"/L"last-opti-client-supplement.json")&&!fs::exists(f.root/L"state"/L"ui-errors.jsonl"),"Error emitted a receipt/UI-error event");
        const auto done=readJson(f.root/L"child-finished.json",json::object());check(done["result"]["status"]=="error"&&done["topLevelWindows"]==0,"Error did not terminate silently");
    });
    test("invalid catalog aborts this one attempt without replacing or displaying it",[&]{
        Fixture f(base,"corrupt");writeHostBytesAtomic(f.root/L"OptiscalerClient"/L"games.json","{");f.start();f.observe();f.finished();
        check(readText(f.root/L"OptiscalerClient"/L"games.json")=="{"&&!fs::exists(f.root/L"state"/L"last-opti-client-supplement.json"),"Corrupt catalog not preserved silently");
        check(f.owner.launches()==1&&!f.owner.start(f.child,GetTickCount64()),"Corrupt catalog scheduled retry");
    });
    test("missing child consumes only one launch attempt and finishes silently",[&]{
        Fixture f(base,"missing");check(f.owner.start(f.root/L"Missing.exe",GetTickCount64(),{0,100}),"Missing child not scheduled");f.finished();f.unchanged();
        check(f.owner.launches()==1&&!f.owner.start(f.child,GetTickCount64()),"Missing child was retried");
    });
    test("YMCC exit before delay cancels with zero launches",[&]{
        Fixture f(base,"cancel-before");f.start(30000);const auto tick=GetTickCount64();f.owner.stop();
        check(GetTickCount64()-tick<500&&f.owner.launches()==0&&f.owner.finished(),"Stop waited for startup deadline");f.unchanged();
        f.owner.stop();check(!f.owner.start(f.child,GetTickCount64()),"Late callback resurrected stopped owner");
    });
    test("YMCC exit cancels active owned task and destroys actual child",[&]{
        Fixture f(base,"cancel-active","wait-cancel");f.start();f.observe();const auto tick=GetTickCount64();f.owner.stop();
        check(GetTickCount64()-tick<2500&&WaitForSingleObject(f.observed.value,0)==WAIT_OBJECT_0,"Active child survived owner stop");f.unchanged();
    });
    test("hung startup callback is forcibly ended by its own Job within deadline",[&]{
        Fixture f(base,"hung","hang");f.start();f.observe();const auto tick=GetTickCount64();f.owner.stop();
        check(GetTickCount64()-tick<2500&&WaitForSingleObject(f.observed.value,0)==WAIT_OBJECT_0,"Hung child survived owner fallback");f.unchanged();
    });
    test("worker time budget kills a stuck attempt without rescheduling",[&]{
        Fixture f(base,"timeout","hang");f.start(0,400);f.observe();f.finished(3000);
        check(WaitForSingleObject(f.observed.value,0)==WAIT_OBJECT_0&&f.owner.launches()==1,"Expired child still running/retried");f.unchanged();
    });
    test("normal visible/media command lines do not admit the startup consumer",[&]{
        unsigned calls=0;check(!startup::entrypoint(self().parent_path()/L"CustomSteamLibrary",[&](const auto&){++calls;}),"Ordinary command admitted startup");
        check(!runLibraryOptiStartupEntrypoint(self().parent_path()/L"CustomSteamLibrary")&&calls==0,"Visible command admitted OPT startup");
    });
    test("fresh commit cancellation rejects writes",[&]{
        const auto root=base/L"commit-cancel";writeHostBytesAtomic(root/L"games.json","[]");bool rejected=false;
        const std::vector<custom_steam_library::opti_catalog::Candidate> games{{"D:\\Games\\Native","","Native","",123}};
        try{supplementOptiCatalog(root,games,[]{return false;},[]{return false;},[]{return false;});}catch(...){rejected=true;}
        check(rejected&&readText(root/L"games.json")=="[]","Revoked commit wrote catalog");
    });
    writeJsonWithLocalBackup(base/L"summary.json",{{"status","PASS"},{"caseCount",passed},{"cases",cases},{"actualDelayMs",30000},
        {"realStartupOwner",true},{"realInheritedParentIdentity",true},{"realJobKill",true},{"wallpaperEnabled",false},
        {"physicalAcceptance",false},{"installedApplicationChanged",false}},false);
    std::cout<<"PASS "<<passed<<" once-per-YMCC startup/lifecycle cases"<<std::endl;return 0;
}catch(const std::exception& error){std::cerr<<"FAIL after "<<passed<<" cases: "<<error.what()<<std::endl;return 1;}}
