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
template<class F>static void test(const char* name,F f){
    if(const auto* selected=std::getenv("YMCC_OPTI_HEADLESS_CASES")){
        const auto names=json::parse(selected);if(!names.is_array()||names.empty())throw std::runtime_error("Invalid OPT selection");
        if(std::find(names.begin(),names.end(),name)==names.end())return;
    }
    const auto tick=GetTickCount64();f();++passed;cases.push_back({{"name",name},{"passed",true},{"elapsedMs",GetTickCount64()-tick}});std::cout<<"PASS "<<name<<std::endl;}
static bool until(const std::function<bool()>& f,DWORD limit=5000){const auto end=GetTickCount64()+limit;do{if(f())return true;Sleep(10);}while(GetTickCount64()<end);return f();}
static fs::path self(){return startup::image(GetCurrentProcess());}
static void optiOwnedRootRecord(const fs::path& root){fs::create_directories(root);std::ofstream out(root/L"data-root.json",std::ios::binary);out<<json{{"schemaVersion",2},{"dataLayoutVersion",2},{"dataRoot",toUtf8(root.wstring())}}.dump();if(!out)throw std::runtime_error("Cannot seed owned root identity");}
static unsigned windows(){unsigned n=0;EnumWindows([](HWND h,LPARAM p)->BOOL{DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==GetCurrentProcessId())++*reinterpret_cast<unsigned*>(p);return TRUE;},reinterpret_cast<LPARAM>(&n));return n;}
struct Fixture {
    fs::path root,child;
    startup::Owner owner;
    startup::Handle observed;
    std::string original="[{\"Name\":\"OPT only\",\"IsFavorite\":true}]";
    Fixture(const fs::path& base,const std::string& name,std::string mode="normal"){
        root=base/toWide(name);child=self().parent_path()/L"CustomSteamLibrary"/L"CustomSteamLibrary.exe";
        fs::create_directories(root/L"NativeGame");optiOwnedRootRecord(root);
        writeJsonWithLocalBackup(root/L"fixture.json",{{"mode",mode}},false);
        writeJsonWithLocalBackup(root/L"state"/L"library-scan.json",{{"games",json::array({{{"steamNative",true},{"nativeSteamAppId",123},
            {"gameDirectory",toUtf8((root/L"NativeGame").wstring())},{"directoryName","Native fixture"},{"primaryExecutable",""}}})}},false);
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
    if(argc>1&&(std::wstring_view(argv[1])==L"--opti-startup"||std::wstring_view(argv[1])==L"--opti-catalog-prepare")){
        const auto root=custom_steam_library::configuredDataRoot();
        const auto config=readJson(root/L"fixture.json",json::object());const auto mode=config.value("mode",std::string{});
        const auto appRoot=self().parent_path();
        JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
        const bool known=QueryInformationJobObject(nullptr,JobObjectCpuRateControlInformation,&cpu,sizeof cpu,nullptr)!=FALSE;
        writeJsonWithLocalBackup(root/L"child-started.json",{{"pid",GetCurrentProcessId()},{"cpuLimitKnown",known},{"cpuRate",cpu.CpuRate},{"cpuFlags",cpu.ControlFlags}},false);
        until([&]{return fs::exists(root/L"child-observed");},2000);
        std::optional<int> result;
        if(mode=="hang"||mode=="wait-cancel") {
            result=startup::entrypoint(appRoot,[&](const std::function<bool()>& cancelled){
                if(mode=="hang"){writeHostBytesAtomic(root/L"hanging-entered","yes");Sleep(INFINITE);}
                while(!cancelled())Sleep(10);
            });
        } else {
            result=runLibraryOptiStartupEntrypoint(appRoot,[&]{return root/L"OptiscalerClient";},[&]{
                if(mode=="throw")throw std::runtime_error("fixture process probe error");
                return mode=="client-running";
            },[&]{return mode=="installed-first"||mode=="cold-native";},[&](uint32_t mask){
                check(mask==ymcc::steaminfo::Installations,"OPT requested unrelated Steam facts");
                ymcc::steaminfo::Facts facts;
                if(mode=="cold-native") {
                    writeHostBytesAtomic(root/L"cold-steam-facts-read","yes");
                    facts.available=true;
                    facts.installations.push_back({toUtf8((root/L"Steam").wstring()),"fixture",0});
                }
                return facts;
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
    test("explicit first opening seeds the new client catalog without configuration",[&]{
        Fixture f(base,"first-open");g_dataRoot=f.root;
        fs::remove(f.root/L"OptiscalerClient"/L"games.json");fs::remove(f.root/L"OptiscalerClient");
        const auto r=runOptiCatalogSupplement([]{return false;},[]{return true;},"ymcc-client-open",[&]{return f.root/L"OptiscalerClient";},[]{return false;});
        check(r.value("status",std::string{})=="synced","Explicit first opening skipped an unconfigured client");
        const auto games=json::parse(readText(f.root/L"OptiscalerClient"/L"games.json"));
        check(games.size()==1&&games[0]["AppId"]=="123"&&games[0]["Platform"]==0,"New catalog did not contain the known library game");
        check(!fs::exists(f.root/L"OptiscalerClient"/L"config.json")&&!games[0].contains("IsOptiscalerInstalled"),"Metadata fill changed client settings or injection state");
    });
    test("explicit first opening distinguishes a missing scan from a scanned empty library",[&]{
        Fixture f(base,"explicit-before-scan");
        const auto scan=f.root/L"state"/L"library-scan.json";
        const auto catalog=f.root/L"OptiscalerClient"/L"games.json";
        const auto receipt=f.root/L"state"/L"last-opti-client-supplement.json";
        const auto known=readText(scan);
        fs::remove(scan);fs::remove(catalog);fs::remove(catalog.parent_path());
        writeHostBytesAtomic(f.root/L"child-observed","yes");
        const auto cold=f.owner.prepareForOpen(f.child,{0,5000});
        const auto coldReceipt=readJson(receipt,json::object());
        std::cout<<"cold-open exit="<<cold<<" receipt="<<coldReceipt.dump()<<std::endl;
        check(fs::is_directory(catalog.parent_path())&&!fs::exists(catalog),"Cold open did not create only the client directory");
        check(!fs::exists(scan)&&!fs::exists(catalog.parent_path()/L"config.json"),"Cold open triggered an unrelated scan or rewrote settings");
        check(cold==22&&coldReceipt["status"]=="deferred"&&coldReceipt["reason"]=="library-scan-unavailable",
            "Missing YMCC scan was reported as prepared");
        writeHostBytesAtomic(scan,"{\"games\":{}}");
        check(f.owner.prepareForOpen(f.child,{0,5000})==22&&!fs::exists(catalog),"Malformed scan was accepted as an empty inventory");
        writeHostBytesAtomic(scan,"{\"games\":[]}");
        check(f.owner.prepareForOpen(f.child,{0,5000})==0&&!fs::exists(catalog),"A confirmed empty inventory should not be deferred or create an empty catalog");
        writeHostBytesAtomic(scan,known);
        check(f.owner.prepareForOpen(f.child,{0,5000})==0,"Explicit open did not recover after a successful scan");
        const auto games=json::parse(readText(catalog));
        check(games.size()==1&&games[0]["AppId"]=="123","Later open did not seed the actual known game");
        const auto bytes=readText(catalog);
        fs::remove(scan);
        check(f.owner.prepareForOpen(f.child,{0,5000})==22&&readText(catalog)==bytes&&!fs::exists(catalog.parent_path()/L"games.json.ymcc.bak"),
            "Missing scan should not affect the existing OPT catalog");
    });
    test("explicit cold open imports only verified local Steam installations",[&]{
        Fixture f(base,"cold-native","cold-native");
        const auto scan=f.root/L"state"/L"library-scan.json", catalog=f.root/L"OptiscalerClient"/L"games.json";
        const auto touch=f.root/L"cold-steam-facts-read",primary=f.root/L"Steam",other=f.root/L"SecondLibrary";
        fs::remove(scan);fs::remove(catalog);fs::remove(catalog.parent_path());
        writeHostBytesAtomic(f.root/L"child-observed","yes");
        writeHostBytesAtomic(primary/L"steam.exe","inert fixture Steam executable");
        const auto make=[&](const fs::path& root,int id,const std::string& dir,const std::string& flags="4",int innerId=0){
            fs::create_directories(root/L"steamapps"/L"common"/toWide(dir));
            auto text="\"AppState\" { \"appid\" \""+std::to_string(innerId?innerId:id)+"\" \"name\" \""+dir+"\" \"installdir\" \""+dir+"\" \"StateFlags\" \""+flags+"\" }";
            writeHostBytesAtomic(root/L"steamapps"/toWide("appmanifest_"+std::to_string(id)+".acf"),text);
        };
        make(primary,321,"MainGame");make(other,322,"AlternateGame");
        make(primary,323,"NotInstalled","2");make(primary,324,"Uninstalling","2052");
        make(primary,325,"WrongAppId","4",326);make(primary,327,"ExcludedGame");
        make(primary,328,"ManualNonGame");make(primary,329,"Conflicted");make(primary,330,"Conflicted");
        make(primary,331,"DuplicateId");make(other,331,"DuplicateId");
        make(primary,228980,"Steamworks Common Redistributables");make(primary,332,"MissingDirectory");
        fs::remove(primary/L"steamapps"/L"common"/L"MissingDirectory");
        const auto first=toUtf8((primary/L"steamapps"/L"common"/L"ExcludedGame").wstring());
        const auto second=toUtf8((primary/L"steamapps"/L"common"/L"ManualNonGame").wstring());
        writeJsonWithLocalBackup(f.root/L"config"/L"library-config.json",{{"excludedGameDirectories",json::array({first})},
            {"manualBuckets",{{second,{{"bucket","non-game"},{"gameDirectory",second}}}}}},false);
        auto otherPath=other.generic_string();
        writeHostBytesAtomic(primary/L"steamapps"/L"libraryfolders.vdf","\"libraryfolders\" { \"1\" { \"path\" \""+otherPath+"\" } }");
        f.start(10,5000);check(until([&]{return f.owner.finished();}),"Cold background attempt did not finish");
        check(!fs::exists(touch)&&!fs::exists(catalog),"Background startup entered the cold Steam route");
        writeHostBytesAtomic(scan,"{\"games\":{}}");
        check(f.owner.prepareForOpen(f.child,{0,5000})==22&&!fs::exists(touch),"Malformed scan incorrectly enabled cold Steam fallback");
        writeHostBytesAtomic(scan,"{\"games\":[]}");
        check(f.owner.prepareForOpen(f.child,{0,5000})==0&&!fs::exists(touch),"Known empty scan incorrectly enabled cold fallback");
        fs::remove(scan);
        const auto firstOpen=f.owner.prepareForOpen(f.child,{0,12000});
        std::cout<<"cold-native first open exit="<<firstOpen<<std::endl;
        check(firstOpen==23&&fs::exists(touch)&&!fs::exists(scan),"Cold Steam installed games did not supplement independently of a custom scan");
        const auto contents=readText(catalog);
        const auto games=json::parse(contents);
        std::set<std::string> ids;
        for(const auto& game:games){
            check(game["Platform"]==0&&!game.contains("ExecutablePath"),"Cold manifest import guessed an executable or platform");
            ids.insert(game["AppId"].get<std::string>());
        }
        check(games.size()==2&&ids==std::set<std::string>{"321","322"},"Cold import lost confirmed rows or accepted an excluded, uninstalled or ambiguous row");
        check(f.owner.prepareForOpen(f.child,{0,12000})==23&&readText(catalog)==contents&&
            !fs::exists(catalog.parent_path()/L"games.json.ymcc.bak"),"Cold re-open duplicated or rewrote the existing catalog");
    });
    test("explicit open resumes after the automatic unconfigured attempt through the same owner",[&]{
        Fixture f(base,"explicit-after-startup");
        fs::remove(f.root/L"OptiscalerClient"/L"games.json");fs::remove(f.root/L"OptiscalerClient");
        writeHostBytesAtomic(f.root/L"child-observed","yes");f.start(0,5000);
        check(until([&]{return f.owner.finished();}),"Automatic missing-client attempt did not finish");
        check(readJson(f.root/L"state"/L"last-opti-client-supplement.json",json::object())["reason"]=="client-not-configured","Automatic missing-client behavior changed");
        check(f.owner.prepareForOpen(f.child,{0,5000})==0,"Explicit bounded prepare did not finish");
        const auto receipt=readJson(f.root/L"state"/L"last-opti-client-supplement.json",json::object());
        check(receipt["trigger"]=="ymcc-client-open"&&receipt["added"]==1,"Explicit command lost first-run initialization or trigger");
        const auto child=readJson(f.root/L"child-started.json",json::object());
        check(child["cpuLimitKnown"]==true&&child["cpuRate"]==startup::workerCpuRate,"Explicit child escaped original CPU cap");
        check(!f.owner.start(f.child,GetTickCount64()),"Explicit prepare reset automatic budget");
        const auto bytes=readText(f.root/L"OptiscalerClient"/L"games.json");
        check(f.owner.prepareForOpen(f.child,{0,5000})==0&&readText(f.root/L"OptiscalerClient"/L"games.json")==bytes,"Later explicit open duplicated existing games");
    });
    test("explicit open cancels startup delay and returns running-client deferral without writes",[&]{
        Fixture f(base,"explicit-running","client-running");writeHostBytesAtomic(f.root/L"child-observed","yes");f.start(30000);
        check(f.owner.prepareForOpen(f.child,{0,5000})==20,"Explicit opening lost its running-client receipt");f.unchanged();
        const auto r=readJson(f.root/L"state"/L"last-opti-client-supplement.json",json::object());
        check(r["reason"]=="client-running"&&r["trigger"]=="ymcc-client-open"&&!fs::exists(f.root/L"OptiscalerClient"/L"games.json.ymcc.bak"),"Running client catalog was changed");
    });
    test("duplicate explicit request does not launch and owner exit cancels the admitted child",[&]{
        Fixture f(base,"explicit-cancel","wait-cancel");writeHostBytesAtomic(f.root/L"child-observed","yes");DWORD result=0;
        std::thread work([&]{result=f.owner.prepareForOpen(f.child,{0,5000});});
        const bool launched=until([&]{return f.owner.childPid()!=0;});
        startup::Handle child{launched?OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,f.owner.childPid()):nullptr};
        const auto duplicate=f.owner.prepareForOpen(f.child,{0,5000});f.owner.stop();work.join();
        check(launched&&child.value&&duplicate==ERROR_BUSY&&result==ERROR_CANCELLED,"Explicit request overlapped or ignored shutdown");
        check(WaitForSingleObject(child.value,0)==WAIT_OBJECT_0&&f.owner.prepareForOpen(f.child)==ERROR_CANCELLED,"Stopped explicit child survived or was resurrected");f.unchanged();
        Fixture replacing(base,"cancel-during-old-join","hang");replacing.start(0,5000);replacing.observe();
        check(until([&]{return fs::exists(replacing.root/L"hanging-entered");}),"Old child never reached its deliberate non-cooperative operation");
        DWORD replacementResult=0;std::atomic<bool> requested{false};
        std::thread replacement([&]{requested.store(true);replacementResult=replacing.owner.prepareForOpen(replacing.child,{0,5000});});
        until([&]{return requested.load();});Sleep(15);
        const auto cancelTick=GetTickCount64();replacing.owner.cancel();const auto cancelMs=GetTickCount64()-cancelTick;
        const bool oldStillRunning=WaitForSingleObject(replacing.observed.value,0)==WAIT_TIMEOUT;
        replacing.owner.stop();replacement.join();
        writeJsonWithLocalBackup(replacing.root/L"cancel-order.json",{{"cancelMs",cancelMs},{"oldChildStillRunningWhenCancelReturned",oldStillRunning},{"prepareResult",replacementResult},{"oldChildExitedAfterStop",WaitForSingleObject(replacing.observed.value,0)==WAIT_OBJECT_0}},false);
        check(oldStillRunning&&replacementResult==ERROR_CANCELLED,"Cancel waited for the old child join or admitted a replacement after exit");
        check(WaitForSingleObject(replacing.observed.value,0)==WAIT_OBJECT_0,"Old non-cooperative child survived stop");replacing.unchanged();

    });
    test("installed client first automatic startup creates its missing profile and fills games",[&]{
        Fixture f(base,"automatic-first","installed-first");
        fs::remove(f.root/L"OptiscalerClient"/L"games.json");fs::remove(f.root/L"OptiscalerClient");
        writeHostBytesAtomic(f.root/L"child-observed","yes");f.start(0,5000);f.finished();
        const auto r=readJson(f.root/L"state"/L"last-opti-client-supplement.json",json::object());
        check(r["status"]=="synced"&&r["trigger"]=="ymcc-startup-once"&&r["added"]==1,"First YMCC start still required prior client initialization");
        const auto games=json::parse(readText(f.root/L"OptiscalerClient"/L"games.json"));
        check(games.size()==1&&games[0]["Platform"]==0&&!games[0].contains("IsOptiscalerInstalled")&&!fs::exists(f.root/L"OptiscalerClient"/L"config.json"),"First startup changed settings/injection or omitted known game");
    });
    test("running installed client still prevents first-profile creation",[&]{
        Fixture f(base,"automatic-running");g_dataRoot=f.root;
        fs::remove(f.root/L"OptiscalerClient"/L"games.json");fs::remove(f.root/L"OptiscalerClient");
        const auto r=runOptiCatalogSupplement([]{return false;},[]{return true;},"ymcc-startup-once",[&]{return f.root/L"OptiscalerClient";},[]{return true;},[]{return true;});
        check(r["reason"]=="client-running"&&!fs::exists(f.root/L"OptiscalerClient"),"Running client did not retain first-run write protection");
    });
    test("YMCC startup schedules exactly once after real 30 seconds even with wallpaper OFF",[&]{
        Fixture f(base,"default-30s");const auto off=readText(f.root/L"config"/L"game-media-downloads.json");
        const auto tick=GetTickCount64();f.start(startup::delayMs,5000);
        check(!f.owner.start(f.child,tick),"Repeated startup scheduled twice");Sleep(150);check(f.owner.launches()==0,"Launched before delay");f.unchanged();
        f.observe(35000);check(GetTickCount64()-tick>=30000,"Production startup delay bypassed");f.finished(5000);
        const auto admitted=readJson(f.root/L"child-started.json",json::object());
        check(admitted["cpuLimitKnown"]==true&&admitted["cpuRate"]==startup::workerCpuRate&&
            (admitted["cpuFlags"].get<DWORD>()&(JOB_OBJECT_CPU_RATE_CONTROL_ENABLE|JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP))==(JOB_OBJECT_CPU_RATE_CONTROL_ENABLE|JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP),"Optional child did not inherit its CPU hard cap");
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
    test("YMCC exit interrupts a serial item wait before catalog commit",[&]{
        Fixture f(base,"cancel-paced");f.start();f.observe();Sleep(250);const auto tick=GetTickCount64();f.owner.stop();
        check(GetTickCount64()-tick<2500&&WaitForSingleObject(f.observed.value,0)==WAIT_OBJECT_0,"Paced child survived cancellation");f.unchanged();
        check(!fs::exists(f.root/L"OptiscalerClient"/L"games.json.ymcc.bak"),"Cancelled pacing reached catalog commit");
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
        const auto root=base/L"commit-cancel";g_dataRoot=root;optiOwnedRootRecord(root);writeHostBytesAtomic(root/L"games.json","[]");bool rejected=false;
        const std::vector<custom_steam_library::opti_catalog::Candidate> games{{"D:\\Games\\Native","","Native","",123}};
        try{supplementOptiCatalog(root,games,[]{return false;},[]{return false;},[]{return false;});}catch(...){rejected=true;}
        check(rejected&&readText(root/L"games.json")=="[]","Revoked commit wrote catalog");
    });
    if(const auto* selected=std::getenv("YMCC_OPTI_HEADLESS_CASES")){
        const auto names=json::parse(selected);std::set<std::string> wanted,actual;
        for(const auto& x:names)wanted.insert(x.get<std::string>());
        for(const auto& x:cases)actual.insert(x.at("name").get<std::string>());
        check(wanted.size()==names.size()&&actual==wanted,"Unknown, duplicate or empty OPT selection");
    }
    writeJsonWithLocalBackup(base/L"summary.json",{{"status","PASS"},{"caseCount",passed},{"cases",cases},{"actualDelayMs",30000},
        {"realStartupOwner",true},{"realInheritedParentIdentity",true},{"realJobKill",true},{"wallpaperEnabled",false},
        {"physicalAcceptance",false},{"installedApplicationChanged",false}},false);
    std::cout<<"PASS "<<passed<<" once-per-YMCC startup/lifecycle cases"<<std::endl;return 0;
}catch(const std::exception& error){std::cerr<<"FAIL after "<<passed<<" cases: "<<error.what()<<std::endl;return 1;}}
