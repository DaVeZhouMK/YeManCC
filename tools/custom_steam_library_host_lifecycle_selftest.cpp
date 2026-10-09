#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <functional>
#include <string>
static std::function<DWORD(const std::wstring&, const std::wstring&)> hostMoveFault;
static BOOL WINAPI fixtureMove(LPCWSTR from, LPCWSTR to, DWORD flags) {
    if (hostMoveFault) { const auto error = hostMoveFault(from, to); if (error) { SetLastError(error); return FALSE; } }
    return MoveFileExW(from, to, flags);
}
#define MoveFileExW fixtureMove
#include "custom_steam_library_storage_isolation.h"
#define wWinMain unusedHostMain
#include "../native/custom-steam-library/workspace_host.cpp"
#undef wWinMain
#undef MoveFileExW
#include <iostream>
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void mustFail(const std::function<void()>& action) { bool failed=false; try { action(); } catch (const std::exception&) { failed=true; } check(failed,"Expected a controlled error"); }
static std::string bytes(const fs::path& path) { std::ifstream in(custom_steam_library::ioPath(path),std::ios::binary); return {std::istreambuf_iterator<char>(in),{}}; }
static void text(const fs::path& path, const std::string& value) { fs::create_directories(path.parent_path()); std::ofstream(path,std::ios::binary)<<value; }
static size_t temporaries(const fs::path& root) { size_t count=0; for(const auto& e:fs::recursive_directory_iterator(root)) if(e.path().filename().wstring().find(L".tmp-")!=std::wstring::npos) ++count; return count; }
static fs::path selfExecutable() { std::wstring p(32768,L'\0'); p.resize(GetModuleFileNameW(nullptr,p.data(),static_cast<DWORD>(p.size()))); return p; }
static int helper(const std::wstring& mode) {
    if(mode==L"empty") return 0;
    if(mode==L"nonzero") { std::cout<<"fixture error"; return 23; }
    if(mode==L"invalid-output") { const char bad[]={char(0xff),char(0xfe),0,1}; std::cout.write(bad,sizeof(bad)); return 0; }
    if(mode==L"sleep" || mode==L"pipe-child") { Sleep(mode==L"sleep"?30000:2000); return 0; }
    if(mode==L"flood" || mode==L"continuous-flood") {
        const std::string block(8192,'X');DWORD wrote=0;
        for(int i=0;mode==L"continuous-flood"||i<800;++i)
            if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),block.data(),static_cast<DWORD>(block.size()),&wrote,nullptr))return 9;
        return 0;
    }
    if(mode==L"hold-pipe") {
        const auto exe=selfExecutable(); const auto cmd=quoteArgument(exe.wstring())+L" --fixture-worker pipe-child";
        std::vector<wchar_t> c(cmd.begin(),cmd.end());c.push_back(0);STARTUPINFOW si{sizeof(si)};
        si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);si.hStdError=GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION pi{};check(CreateProcessW(exe.c_str(),c.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi),"Could not start fixture descendant");
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);std::cout<<"parent done";return 0;
    }
    return 99;
}
// End-to-end queue worker: no network, Steam discovery or user-data access.
// Every subprocess is this fixture EXE and every path must remain in Build.
static int queueFixtureWorker(int argc, wchar_t** argv) {
    wchar_t value[32768]{};
    const DWORD length=GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_DATA_ROOT",value,32768);
    if(!length || length>=32768) return 98;
    const auto data=fs::absolute(value);
    const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
    check(normalizedPathKey(data).starts_with(normalizedPathKey(build)+L"\\"),"Queue fixture escaped Build");
    g_dataRoot=data;
    auto has=[&](const wchar_t* flag){for(int i=1;i<argc;++i)if(std::wstring(argv[i])==flag)return true;return false;};
    auto argument=[&](const wchar_t* flag){for(int i=1;i+1<argc;++i)if(std::wstring(argv[i])==flag)return fs::path(argv[i+1]);return fs::path{};};
    auto output=argument(L"--output");
    if(has(L"--list-steam-accounts")) {
        check(normalizedPathKey(output).starts_with(normalizedPathKey(data)+L"\\"),"Accounts output escaped fixture");
        writeJsonWithLocalBackup(output,{{"accounts",json::array()},{"steamStatus","fixture"}},false);return 0;
    }
    if(has(L"--plan-add-library-to-steam")) {
        check(normalizedPathKey(output).starts_with(normalizedPathKey(data)+L"\\"),"Plan output escaped fixture");
        json snapshot={{"library",readJson(data/L"state"/L"library-scan.json",json::object())},
            {"manualOverrides",readJson(data/L"config"/L"manual-overrides.json",json::object())}};
        auto plan=buildLocalSteamPlanSnapshot(snapshot,readJson(data/L"state"/L"fixture-plan.json",json::object()));
        writeJsonWithLocalBackup(output,plan,false);return 0;
    }
    check(argc>=3,"Missing fixture EXE/output");const auto exe=fs::absolute(argv[1]);const auto out=fs::absolute(argv[2]);
    check(normalizedPathKey(exe).starts_with(normalizedPathKey(data.parent_path())+L"\\")&&
        normalizedPathKey(out).starts_with(normalizedPathKey(data)+L"\\"),"Worker path escaped fixture");
    if(has(L"--metadata-only")) {
        writeJsonWithLocalBackup(out/L"rounds-failed.json",{{"executable",toUtf8(exe.wstring())},
            {"failure",{{"kind","unrecognized"},{"retryWhenNetworkReturns",false}}}},false);
        std::cout<<"FAILURE_KIND: unrecognized\n";return 1;
    }
    check(has(L"--igdb-artwork-only"),"No-ID queue used a Steam artwork/identity worker");
    const auto artwork=data/L"artwork"/L"old-game";json slots=json::array();
    for(const auto* type:{"tall","long","hero"}) {
        const auto image=artwork/(toWide(type)+L".png");text(image,"isolated picture fixture");
        slots.push_back({{"type",type},{"ok",true},{"portableFile",toUtf8(image.wstring())},{"provider","baidu-image"}});
    }
    const json manifest={{"exe",toUtf8(exe.wstring())},{"gameDirectory",toUtf8(exe.parent_path().wstring())},
        {"match",{{"appId",0},{"igdbId",0},{"primaryProvider","artwork-fallback"},{"identityStatus","unverified"}}},
        {"artwork",slots},{"successCount",3},{"ok",true},{"required",{{"tall",true},{"hero",true}}}};
    writeJsonWithLocalBackup(artwork/L"manifest.json",manifest,false);
    writeJsonWithLocalBackup(out/L"result"/L"manifest.json",manifest,false);return 0;
}
static void runQueueFixture(const fs::path& root,bool manual,bool cooldown) {
    const auto savedData=g_dataRoot,savedLab=g_labRoot,savedWorker=g_worker;
    const auto savedTarget=steamTargetSnapshot();
    wchar_t prior[32768]{};const auto priorLength=GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_DATA_ROOT",prior,32768);
    auto restore=[&]{
        g_shuttingDown=true;requestTaskCancellation();joinHostBackgroundTasks();closeWorkerProcessJob();
        g_dataRoot=savedData;g_labRoot=savedLab;g_worker=savedWorker;g_steamTarget=savedTarget;
        SetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_DATA_ROOT",priorLength&&priorLength<32768?prior:nullptr);
        g_shuttingDown=false;g_cancelRequested=false;
    };
    try {
        g_dataRoot=root/L"data";g_labRoot=root;g_worker=selfExecutable();g_steamTarget=json::object();
        SetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_DATA_ROOT",g_dataRoot.c_str());
        const auto joined=root/L"Games"/L"Joined"/L"Joined.exe",old=root/L"Games"/L"Ishin2"/L"ISHIN2.EXE";
        text(joined,"already-added fixture");text(old,"old no-ID fixture");json games=json::array();
        for(const auto& exe:{joined,old}) games.push_back({{"primaryExecutable",toUtf8(exe.wstring())},
            {"gameDirectory",toUtf8(exe.parent_path().wstring())},{"directoryName",toUtf8(exe.parent_path().filename().wstring())},
            {"status","ready"},{"contentType","game"}});
        writeJsonWithLocalBackup(g_dataRoot/L"state"/L"library-scan.json",{{"games",games}},false);
        writeJsonWithLocalBackup(g_dataRoot/L"config"/L"library-config.json",{{"roots",json::array({toUtf8((root/L"Games").wstring())})}},false);
        json overrides={{"name","維新の嵐 幕末志士伝"}};
        if(cooldown) overrides["automaticIdentityRetryAfterAt"]=unixTimeMsHost()+600000;
        writeJsonWithLocalBackup(g_dataRoot/L"config"/L"manual-overrides.json",{{"items",{{toUtf8(old.wstring()),overrides}}}},false);
        const json plan={{"libraryState",toUtf8((g_dataRoot/L"state"/L"library-scan.json").wstring())},
            {"summary",{{"alreadyInSteam",1}}},{"items",json::array({
                {{"primaryExecutable",toUtf8(joined.wstring())},{"gameDirectory",toUtf8(joined.parent_path().wstring())},
                 {"status","already-in-steam"},{"shortAppId","2388972698"}},
                {{"primaryExecutable",toUtf8(old.wstring())},{"gameDirectory",toUtf8(old.parent_path().wstring())},
                 {"status","needs-identity-and-artwork"},{"artwork",{{"minimumComplete",false},{"long",false},{"automaticSlotsComplete",false}}}}
            })}};
        writeJsonWithLocalBackup(workspacePlanPath(),plan,false);writeJsonWithLocalBackup(g_dataRoot/L"state"/L"fixture-plan.json",plan,false);
        check(startScrapeQueue(manual),"Queue fixture did not start");
        const auto deadline=GetTickCount64()+15000;
        while(g_scrapeQueueRunning.load()&&GetTickCount64()<deadline)Sleep(10);
        check(!g_scrapeQueueRunning.load(),"Queue waited for identity retries/batch barrier");joinHostBackgroundTasks();
        json history;
        for(const auto& entry:fs::directory_iterator(scrapeHistoryRoot())) {
            if(entry.path().extension()==L".json"&&entry.path().filename().wstring().starts_with(L"round-"))history=readJson(entry.path(),json::object());
        }
        check(history["status"]=="completed"&&history["summary"]["total"]==1&&history["summary"]["completed"]==1,"No-ID queue did not complete or scraped joined game");
        const auto runs=jsonArrayOr(history,"workerRuns");
        check(runs.size()==(cooldown&&!manual?1:2),"Identity failure blocked fallback or caused redundant identity retries");
        for(const auto& run:runs)check(jsonStringOr(run,"executable")==toUtf8(old.wstring()),"Joined shortcut underwent automatic ID verification");
        check(jsonStringOr(runs.back(),"stage")=="artwork-fallback","No-ID game did not directly enter visual fallback");
        const auto snapshot=workspaceSnapshot(false);const auto items=snapshot["steamPlan"]["items"];
        check(items[0]["status"]=="already-in-steam","Artwork refresh moved joined game");
        check(items[1]["status"]=="waiting-steam-verification"&&items[1]["artworkPreview"].size()==3,"No-ID pictures not applied or invented import identity");
        restore();
    } catch(...) {restore();throw;}
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3 && std::wstring(argv[1])==L"--fixture-worker") return helper(argv[2]);
    if(argc>2) { try{return queueFixtureWorker(argc,argv);}catch(const std::exception& e){std::cerr<<e.what();return 97;} }
    try {
        check(argc==2,"Pass a fresh Build root");const auto root=fs::absolute(argv[1]);
        const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
        check(normalizedPathKey(root).starts_with(normalizedPathKey(build)+L"\\")&&!fs::exists(root),"Unsafe/reused fixture root");
        fs::create_directories(root);g_dataRoot=root/L"data";g_labRoot=root;json cases=json::array();
        auto test=[&](const char* name,const std::function<void(const fs::path&)>& action){hostMoveFault={};g_shuttingDown=false;g_cancelRequested=false;const auto p=root/std::to_wstring(cases.size());fs::create_directories(p);
            try { action(p);cases.push_back({{"name",name},{"passed",true}}); } catch(const std::exception& e) { cases.push_back({{"name",name},{"passed",false},{"error",toUtf8(toWide(e.what()))}}); }
            hostMoveFault={};g_shuttingDown=true;requestTaskCancellation();closeWorkerProcessJob();joinHostBackgroundTasks();g_shuttingDown=false;g_cancelRequested=false;g_window=nullptr;};
        test("json-first-write-and-complete-backup",[](const auto& p){auto f=p/L"config.json";writeJsonWithLocalBackup(f,{{"v",1}},false);const auto old=bytes(f);writeJsonWithLocalBackup(f,{{"v",2}},false);check(readJson(f,json::object())["v"]==2&&bytes(f.wstring()+L".bak")==old,"Backup or activation incomplete");check(!temporaries(p),"Staging residue");});
        test("json-transient-sharing-retries",[](const auto& p){auto f=p/L"config.json";text(f,"old");int n=0;hostMoveFault=[&](const auto&,const auto& to){return to==f.wstring()&&++n<=3?ERROR_SHARING_VIOLATION:0;};writeJsonWithLocalBackup(f,{{"v",2}},false);check(n==4&&readJson(f,json::object())["v"]==2,"Did not retry sharing contention");});
        test("json-transient-access-denied-retries",[](const auto& p){auto f=p/L"config.json";text(f,"old");int n=0;hostMoveFault=[&](const auto&,const auto& to){return to==f.wstring()&&++n==1?ERROR_ACCESS_DENIED:0;};writeJsonWithLocalBackup(f,{{"v",2}},false);check(n==2,"Did not retry transient access denied");});
        test("json-disk-fault-preserves-original-and-cleans-temp",[](const auto& p){auto f=p/L"config.json";text(f,"old");hostMoveFault=[&](const auto&,const auto& to){return to==f.wstring()?ERROR_WRITE_FAULT:0;};mustFail([&]{writeJsonWithLocalBackup(f,{{"v",2}},false);});check(bytes(f)=="old"&&!temporaries(p),"Failed replacement damaged original");});
        test("json-readonly-does-not-force-permissions",[](const auto& p){auto f=p/L"config.json";text(f,"old");SetFileAttributesW(f.c_str(),FILE_ATTRIBUTE_READONLY);mustFail([&]{writeJsonWithLocalBackup(f,{{"v",2}},false);});check(bytes(f)=="old"&&!temporaries(p),"Readonly original changed");SetFileAttributesW(f.c_str(),FILE_ATTRIBUTE_NORMAL);});
        test("json-target-directory-fails-closed",[](const auto& p){auto f=p/L"config.json";fs::create_directory(f);mustFail([&]{writeJsonWithLocalBackup(f,{{"v",2}},false);});check(fs::is_directory(f)&&!temporaries(p),"Directory removed or residue");});
        test("json-backup-fault-preserves-original-and-backup",[](const auto& p){auto f=p/L"config.json";auto b=fs::path(f.wstring()+L".bak");text(f,R"({"v":1})");text(b,R"({"v":0})");hostMoveFault=[&](const auto&,const auto& to){return to==b.wstring()?ERROR_WRITE_FAULT:0;};mustFail([&]{writeJsonWithLocalBackup(f,{{"v",2}},false);});check(bytes(f)==R"({"v":1})"&&bytes(b)==R"({"v":0})"&&!temporaries(p),"Failed backup damaged files");});
        test("json-real-reader-lock-is-transient",[](const auto& p){auto f=p/L"config.json";text(f,"old");HANDLE h=CreateFileW(f.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);check(h!=INVALID_HANDLE_VALUE,"Fixture lock failed");std::thread unlock([=]{Sleep(200);CloseHandle(h);});try{writeJsonWithLocalBackup(f,{{"v",2}},false);}catch(...){unlock.join();throw;}unlock.join();check(readJson(f,json::object())["v"]==2&&!temporaries(p),"Real lock not retried");});
        test("json-invalid-utf8-has-no-filesystem-side-effect",[](const auto& p){auto f=p/L"config.json";text(f,"old");text(f.wstring()+L".bak","older");mustFail([&]{writeJsonWithLocalBackup(f,{{"bad",std::string(1,char(0xff))}},false);});check(bytes(f)=="old"&&bytes(f.wstring()+L".bak")=="older"&&!temporaries(p),"Serialization fault touched backup");});
        test("json-missing-parent-can-be-created",[](const auto& p){writeJsonWithLocalBackup(p/L"a"/L"b"/L"c.json",{{"ok",true}},false);check(fs::is_regular_file(p/L"a"/L"b"/L"c.json"),"Missing directories not created");});
        test("json-parent-is-file-fails-closed",[](const auto& p){text(p/L"a","not a directory");mustFail([&]{writeJsonWithLocalBackup(p/L"a"/L"b.json",{{"ok",true}},false);});check(bytes(p/L"a")=="not a directory","Parent file lost");});
        test("manual-image-activation-fault-keeps-old-image",[](const auto& p){auto src=p/L"source.png";text(src,"new");const auto dest=g_dataRoot/L"artwork"/L"overrides"/L"image-fault"/L"cover.png";text(dest,"old");hostMoveFault=[&](const auto&,const auto& to){return to==dest.wstring()?ERROR_WRITE_FAULT:0;};mustFail([&]{copyArtworkToIsolation(src,"image-fault","cover");});check(bytes(dest)=="old"&&!temporaries(g_dataRoot),"Old manual artwork truncated");});
        test("manual-image-transient-replace-retries",[](const auto& p){auto src=p/L"source.png";text(src,"new");int count=0;hostMoveFault=[&](const auto&,const auto& to){return to.ends_with(L"cover.png")&&++count==1?ERROR_LOCK_VIOLATION:0;};const auto dest=copyArtworkToIsolation(src,"image-retry","cover");check(bytes(dest)=="new"&&count==2,"Manual retry failed");});
        test("copy-source-missing-keeps-destination",[](const auto& p){auto target=p/L"target";text(target,"old");mustFail([&]{copyHostFileAtomic(p/L"missing",target);});check(bytes(target)=="old"&&!temporaries(p),"Missing source destroyed destination");});
        test("copy-to-self-is-safe",[](const auto& p){auto f=p/L"image.png";text(f,"old");copyHostFileAtomic(f,f);check(bytes(f)=="old"&&!temporaries(p),"Same-file copy damaged bytes");});
        test("exit-diagnostic-failure-never-throws",[&](const auto& p){g_labRoot=p/L"not-directory";text(g_labRoot,"x");g_hostExitRecordWritten=false;writeHostExitRecord();writeHostExitRecord();check(g_hostExitRecordWritten.load(),"Exit diagnostic not bounded");g_labRoot=root;});
        auto run=[&](const wchar_t* mode,DWORD timeout=3000){return runProcess(selfExecutable(),{L"--fixture-worker",mode},timeout);};
        test("worker-empty-output",[&](const auto&){auto r=run(L"empty");check(r.exitCode==0&&r.output.empty(),"Empty-output worker failed");});
        test("worker-nonzero-output",[&](const auto&){auto r=run(L"nonzero");check(r.exitCode==23&&r.output.find("fixture error")!=std::string::npos,"Exit code or stderr lost");});
        test("worker-invalid-utf8-output-does-not-crash-host",[&](const auto&){auto r=run(L"invalid-output");check(r.exitCode==0&&r.output.size()>=4&&r.output.find("\xef\xbf\xbd")!=std::string::npos,"Binary output not handled");});
        test("worker-timeout-terminates-and-clears-slot",[&](const auto&){auto t=GetTickCount64();auto r=run(L"sleep",120);check(r.exitCode==ERROR_TIMEOUT&&GetTickCount64()-t<2000&&g_activeProcess==nullptr,"Timeout leaked worker/slot");});
        test("worker-cancel-before-start",[&](const auto&){g_cancelRequested=true;auto r=run(L"sleep");check(r.exitCode==ERROR_CANCELLED&&g_activeProcess==nullptr,"Cancelled worker started");});
        test("worker-shutdown-before-start",[&](const auto&){g_shuttingDown=true;auto r=run(L"sleep");check(r.exitCode==ERROR_CANCELLED,"Shutdown started new worker");});
        test("worker-output-retention-is-bounded",[&](const auto&){auto r=run(L"flood",15000);if(!((r.exitCode==0||r.exitCode==ERROR_BUFFER_OVERFLOW)&&r.output.size()<=(4u<<20)+100&&r.output.find("WORKER_OUTPUT_TRUNCATED")!=std::string::npos))throw std::runtime_error("Worker output: exit="+std::to_string(r.exitCode)+" bytes="+std::to_string(r.output.size())+" tail="+r.output.substr(r.output.size()>50?r.output.size()-50:0));});
        test("worker-output-flood-cannot-starve-timeout",[&](const auto&){auto t=GetTickCount64();auto r=run(L"continuous-flood",150);check((r.exitCode==ERROR_TIMEOUT||r.exitCode==ERROR_BUFFER_OVERFLOW)&&GetTickCount64()-t<2000,"Flood blocked timeout");});
        test("worker-descendant-holding-pipe-does-not-block",[&](const auto&){auto t=GetTickCount64();auto r=run(L"hold-pipe");check(r.exitCode==0&&GetTickCount64()-t<1500,"Final pipe read waited for descendant");});
        test("worker-missing-executable-does-not-leak-handles",[&](const auto& p){DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);for(int i=0;i<20;++i)mustFail([&]{runProcess(p/L"missing.exe",{},100);});GetProcessHandleCount(GetCurrentProcess(),&after);if(after>before+2)throw std::runtime_error("Handle count before="+std::to_string(before)+" after="+std::to_string(after));});
        test("shutdown-joins-multiple-running-workers",[&](const auto&){initializeWorkerProcessJob();std::atomic<int> finished=0;for(int i=0;i<3;++i)launchHostBackgroundTask([&]{auto r=run(L"sleep",10000);if(r.exitCode==ERROR_CANCELLED||r.exitCode!=0)++finished;});Sleep(150);g_shuttingDown=true;requestTaskCancellation();closeWorkerProcessJob();joinHostBackgroundTasks();if(!(finished==3&&g_hostThreads.empty()&&g_activeProcess==nullptr))throw std::runtime_error("Shutdown: finished="+std::to_string(finished)+" threads="+std::to_string(g_hostThreads.size())+" slot="+std::to_string(reinterpret_cast<uintptr_t>(g_activeProcess)));});
        test("worker-job-close-kills-suspended-child",[&](const auto&){initializeWorkerProcessJob();const auto exe=selfExecutable();auto cmd=quoteArgument(exe.wstring())+L" --fixture-worker sleep";std::vector<wchar_t> c(cmd.begin(),cmd.end());c.push_back(0);STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};check(CreateProcessW(exe.c_str(),c.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi),"Fixture child failed");assignProcessToHostWorkerJob(pi.hProcess);BOOL assigned=FALSE;IsProcessInJob(pi.hProcess,g_workerProcessJob,&assigned);closeWorkerProcessJob();const auto wait=WaitForSingleObject(pi.hProcess,1000);if(wait!=WAIT_OBJECT_0)TerminateProcess(pi.hProcess,99);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);check(assigned&&wait==WAIT_OBJECT_0,"Host job did not own/terminate child");});
        test("background-late-post-is-rejected-after-close",[](const auto&){HWND w=CreateWindowExW(0,L"STATIC",L"fixture",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);check(w!=nullptr,"Fixture window failed");g_window=w;auto a=std::make_unique<AsyncResult>();a->message={{"id",1}};check(enqueueWorkspaceResult(std::move(a)),"Initial result was not queued");stopWorkspaceResultPosting(w);auto b=std::make_unique<AsyncResult>();check(!enqueueWorkspaceResult(std::move(b)),"Result queued after shutdown");MSG m{};check(!PeekMessageW(&m,w,WM_WORKSPACE_RESULT,WM_WORKSPACE_RESULT,PM_REMOVE),"Close did not drain queued payload");DestroyWindow(w);g_window=nullptr;});
        test("background-unhandled-task-error-is-contained",[](const auto&){launchHostBackgroundTask([]{throw std::runtime_error("fixture background failure");});joinHostBackgroundTasks();check(g_hostThreads.empty(),"Failed background task not joined");});
        test("shutdown-forbids-new-background-thread",[](const auto&){g_shuttingDown=true;mustFail([]{launchHostBackgroundTask([]{});});check(g_hostThreads.empty(),"Shutdown started thread");});
        test("new-job-generation-invalidates-late-result",[](const auto&){std::vector<BackgroundJob> jobs;std::unordered_map<std::string,uint64_t> generations;auto a=beginBackgroundJob("fixture",jobs,generations,"game");auto b=beginBackgroundJob("fixture",jobs,generations,"game");check(!jobIsCurrent(generations,a)&&jobIsCurrent(generations,b)&&a->cancelRequested,"Old generation accepted");finishBackgroundJob(jobs,a);finishBackgroundJob(jobs,b);});
        test("different-game-generations-remain-independent",[](const auto&){std::vector<BackgroundJob> jobs;std::unordered_map<std::string,uint64_t> generations;auto a=beginBackgroundJob("fixture",jobs,generations,"a");auto b=beginBackgroundJob("fixture",jobs,generations,"b");check(jobIsCurrent(generations,a)&&jobIsCurrent(generations,b),"Other game cancelled");finishBackgroundJob(jobs,a);finishBackgroundJob(jobs,b);});
        test("native-grid-activation-failure-keeps-all-old-extensions",[](const auto& p){const auto grid=p/L"SteamFixture"/L"grid";text(grid/L"620p.jpg","old jpg");text(grid/L"620p.png","old png");text(p/L"new.png","new");NativeSteamArtworkTarget target;target.appId=620;target.grids.push_back({"fixture",grid});hostMoveFault=[&](const auto&,const auto& to){return to==(grid/L"620p.png").wstring()?ERROR_WRITE_FAULT:0;};mustFail([&]{writeNativeSteamArtwork(target,"cover",toUtf8((p/L"new.png").wstring()),false);});check(bytes(grid/L"620p.jpg")=="old jpg"&&bytes(grid/L"620p.png")=="old png","Old extensions deleted before activation");check(!temporaries(p),"Native staging leaked");});
        test("native-grid-success-replaces-only-own-slot",[](const auto& p){const auto grid=p/L"SteamFixture"/L"grid";text(grid/L"620p.jpg","old cover");text(grid/L"620_hero.png","hero stays");text(p/L"new.png","new cover");NativeSteamArtworkTarget target;target.appId=620;target.grids.push_back({"fixture",grid});writeNativeSteamArtwork(target,"cover",toUtf8((p/L"new.png").wstring()),false);check(bytes(grid/L"620p.png")=="new cover"&&!fs::exists(grid/L"620p.jpg")&&bytes(grid/L"620_hero.png")=="hero stays","Slot/extension replacement damaged other files");});
        test("unicode-artwork-and-config-paths",[](const auto& p){const auto source=p/L"测试游戏 Ω"/L"封面.png";text(source,"local artwork");const auto dest=p/L"数据"/L"图片.png";copyHostFileAtomic(source,dest);writeJsonWithLocalBackup(p/L"数据"/L"配置.json",{{"game","测试游戏"}},false);check(bytes(dest)=="local artwork"&&!temporaries(p),"Unicode paths failed");});
        test("manifest-wrong-root-and-invalid-json-are-skipped",[](const auto& p){const auto saved=g_dataRoot;g_dataRoot=p/L"data";text(g_dataRoot/L"artwork"/L"bad"/L"manifest.json","{");writeJsonWithLocalBackup(g_dataRoot/L"artwork"/L"array"/L"manifest.json",json::array(),false);const auto exe=p/L"Game.exe";writeJsonWithLocalBackup(g_dataRoot/L"artwork"/L"valid"/L"manifest.json",{{"exe",toUtf8(exe.wstring())}},false);auto result=findPortableArtworkManifestForHost(toUtf8(exe.wstring()));g_dataRoot=saved;check(result&&result->second.parent_path().filename()==L"valid","Bad manifest hid valid game");});
        test("empty-artwork-manifest-exe-does-not-match",[](const auto& p){const auto saved=g_dataRoot;g_dataRoot=p/L"data";writeJsonWithLocalBackup(g_dataRoot/L"artwork"/L"manifest.json",{{"exe",""}},false);auto result=findPortableArtworkManifestForHost("");g_dataRoot=saved;check(!result,"Empty path accepted as identity");});
        test("multiple-host-fixture-mutex-denies-second-owner",[](const auto&){const auto name=L"Local\\YeManSteamLibraryFixture-"+std::to_wstring(GetCurrentProcessId());HANDLE first=CreateMutexW(nullptr,TRUE,name.c_str());check(first!=nullptr,"Fixture mutex failed");HANDLE second=CreateMutexW(nullptr,TRUE,name.c_str());const auto error=GetLastError();CloseHandle(second);ReleaseMutex(first);CloseHandle(first);check(error==ERROR_ALREADY_EXISTS,"Second instance not detected");});
        test("host-json-and-image-copy-beyond-max-path",[](const auto& p){auto dir=p;for(int i=0;i<8;++i)dir/=L"long-config-artwork-fixture-012345";const auto config=dir/L"config.json";check(config.wstring().size()>300,"Long-path fixture too short");writeJsonWithLocalBackup(config,{{"id",1}},false);writeJsonWithLocalBackup(config,{{"id",2}},false);check(readJson(config,json::object())["id"]==2,"Long-path Host JSON failed");const auto copy=dir/L"local-artwork.png";copyHostFileAtomic(config,copy);check(bytes(copy)==bytes(config),"Long-path artwork copy failed");});
        test("automatic-no-id-queue-applies-pictures-without-retrying-identity",[](const auto& p){runQueueFixture(p,false,false);});
        test("identity-cooldown-does-not-block-no-id-artwork",[](const auto& p){runQueueFixture(p,false,true);});
        test("manual-refresh-still-excludes-already-added-shortcuts",[](const auto& p){runQueueFixture(p,true,false);});
        test("gpu-utility-frame-failures-do-not-reload-or-close-editor",[](const auto&){for(const auto kind:{COREWEBVIEW2_PROCESS_FAILED_KIND_GPU_PROCESS_EXITED,COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED,COREWEBVIEW2_PROCESS_FAILED_KIND_UTILITY_PROCESS_EXITED})check(!workspaceWebViewFailureNeedsRecovery(kind),"Resource subprocess warning escalated to whole-window recovery");});
        test("actual-browser-renderer-failures-still-recover",[](const auto&){for(const auto kind:{COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED,COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED,COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE})check(workspaceWebViewFailureNeedsRecovery(kind),"Fatal WebView process failure ignored");});

        test("builtin-steam-tool-direct-id-identify-blocked-before-worker",[](const auto&){
            const json args={{"executable","G:/Fixture/Game.exe"},{"steamId","228980"}};
            mustFail([&]{executeWorkspaceCommand("identifyOne",args);});
            mustFail([&]{startManualIdentifyTask(args);});
        });
        test("builtin-steam-tool-name-path-search-blocked-before-job",[](const auto&){
            mustFail([&]{startIdentitySearchRequest({{"executable","G:/Fixture/Game.exe"},{"query","Steamworks Common Redistributables"}});});
            mustFail([&]{startIdentitySearchTask({{"executable","G:/Steam/steamapps/common/Steamworks Shared/vcredist.exe"},{"query","Portal 2"}});});
        });
        test("builtin-steam-tool-artwork-search-does-not-launch-worker",[](const auto&){
            mustFail([&]{executeWorkspaceCommand("searchArtwork",{{"query","Renamed Tool"},{"steamAppId","228980"},{"type","cover"}});});
        });
        test("empty-root-config-always-enters-first-run-retry-path",[](const auto& p){
            g_dataRoot=p/L"data";
            for(const auto& status:{"pending","running","completed","skipped-existing-roots"}) {
                writeJsonWithLocalBackup(g_dataRoot/L"config"/L"library-config.json",{{"roots",json::array()},{"firstRunDefaultScan",{{"status",status}}}},false);
                check(firstRunDefaultScanPending(),"Zero-root config incorrectly treated as permanently complete");
            }
        });
        test("explicitly-cleared-roots-do-not-enter-first-run-discovery",[](const auto& p){
            g_dataRoot=p/L"data";
            writeJsonWithLocalBackup(g_dataRoot/L"config"/L"library-config.json",{{"roots",json::array()},{"scanRootsExplicitlyCleared",true}},false);
            check(!firstRunDefaultScanPending(),"User-cleared roots triggered discovery");
            const auto arguments=libraryScanWorkerArguments();
            check(std::find(arguments.begin(),arguments.end(),L"--first-run-defaults")==arguments.end(),"User-cleared roots added discovery flag");
        });
        test("builtin-steam-tool-cache-is-not-exposed",[](const auto& p){
            g_dataRoot=p/L"data";g_labRoot=p;const auto cache=g_dataRoot/L"cache"/L"identity";
            writeJsonWithLocalBackup(cache/L"tool.json",{{"executable","G:/Fixture/Tool.exe"},{"match",{{"appId",228980},{"formalName","Renamed Tool"}}}},false);
            writeJsonWithLocalBackup(cache/L"game.json",{{"executable","G:/Fixture/Portal.exe"},{"match",{{"appId",620},{"formalName","Portal 2"}}}},false);
            json snapshot=json::object();attachResolvedIdentityCache(snapshot);
            check(snapshot["resolvedIdentities"].size()==1,"Tool cache leaked or valid game cache disappeared");
        });
        size_t failures=0;for(const auto& t:cases)if(!t["passed"].get<bool>())++failures;
        json report={{"allPassed",failures==0},{"caseCount",cases.size()},{"failedCount",failures},{"cases",cases},{"realSteamFilesModified",false},{"steamStoppedOrLaunched",false}};
        g_labRoot=root;hostMoveFault={};writeJsonWithLocalBackup(root/L"summary.json",report,false);std::cout<<report.dump(2)<<"\n";return failures?1:0;
    } catch(const std::exception& e){g_shuttingDown=true;requestTaskCancellation();closeWorkerProcessJob();joinHostBackgroundTasks();std::cerr<<e.what()<<"\n";return 1;}
}
