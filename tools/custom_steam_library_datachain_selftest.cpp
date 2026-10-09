#include "custom_steam_library_test_runtime.h"
#include <winioctl.h>
#include <thread>

static void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void textFile(const fs::path& path, const std::string& text) { writeAtomic(path, std::vector<unsigned char>(text.begin(), text.end())); }
static void mustThrow(const std::function<void()>& fn) { bool threw=false; try { fn(); } catch (...) { threw=true; } check(threw, "Expected operation to fail safely"); }
static std::string fileText(const fs::path& path) { const auto bytes=readBinaryFile(path); return std::string(bytes.begin(),bytes.end()); }
static bool hasStages(const fs::path& root) {
    for (const auto& entry: fs::recursive_directory_iterator(root)) {
        const auto name=entry.path().filename().wstring();
        if (name.find(L".yeman-stage-")!=std::wstring::npos || name.find(L".yeman-delete-stage-")!=std::wstring::npos) return true;
    }
    return false;
}
static std::vector<fs::path> pendingJournals(const fs::path& root) {
    std::vector<fs::path> result;
    for(const auto& entry:fs::recursive_directory_iterator(root)) if(entry.path().filename()==L"transaction.pending.json") result.push_back(entry.path());
    return result;
}
struct Fixture {
    fs::path root, data, local, planFile;
    SteamShortcutTarget target;
    json plan;
    Fixture(const fs::path& base, bool existing=false):root(base),data(base/L"data"),
        local(base/L"Steam"/L"userdata"/L"19627"/L"config"/L"localconfig.vdf"),planFile(data/L"state"/L"plan.json"),target{local.parent_path()/L"shortcuts.vdf","19627"} {
        fs::create_directories(local.parent_path());
        textFile(base/L"Steam"/L"steam.exe", "fixture only, not an executable");
        if(existing) writeAtomic(target.path,BinaryVdfDocument::emptySteamShortcuts().serialize());
        const uint32_t id=0xc0000001u; int32_t stored=0; std::memcpy(&stored,&id,sizeof(id));
        plan={{"realSteamTarget",true},{"shortcutsBeforeSha256",sha256(existing?readBinaryFile(target.path):std::vector<unsigned char>{})},
            {"summary",{{"readyToAdd",1},{"shortcutIdConflicts",0}}},{"unmatchedSelections",json::array()},
            {"items",json::array({{{"status","ready-to-add"},{"formalName","Data chain fixture"},{"storedAppId",stored},{"shortAppId",std::to_string(id)},
              {"longAppId",std::to_string((static_cast<uint64_t>(id)<<32)|0x02000000ull)},
              {"quotedExecutable",quotedSteamPath(root/L"Game"/L"Game.exe")},{"quotedStartDirectory",quotedSteamPath(root/L"Game")}}})}};
    }
    json commit() { return commitPreparedSteamAddPlan(data,target,planFile,true,plan); }
};
static bool createFixtureJunction(const fs::path& link, const fs::path& target) {
    fs::create_directories(link);
    const auto print = fs::absolute(target).wstring(), substitute = L"\\??\\" + print;
    struct MountPointData { DWORD tag; WORD length, reserved, substituteOffset, substituteLength, printOffset, printLength; WCHAR paths[1]; };
    const auto names = substitute + L'\0' + print + L'\0';
    std::vector<unsigned char> buffer(offsetof(MountPointData, paths) + names.size() * sizeof(wchar_t));
    auto* data = reinterpret_cast<MountPointData*>(buffer.data());
    data->tag = IO_REPARSE_TAG_MOUNT_POINT; data->length = static_cast<WORD>(buffer.size() - 8);
    data->substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    data->printOffset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t)); data->printLength = static_cast<WORD>(print.size() * sizeof(wchar_t));
    std::memcpy(data->paths, names.data(), names.size() * sizeof(wchar_t));
    HANDLE h = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD returned = 0; const bool ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr) != FALSE;
    CloseHandle(h); return ok;
}

static fs::path fixtureScanState(Fixture& f) {
    const auto state=f.data/L"state"/L"library-scan.json";
    textFile(f.root/L"Game"/L"Game.exe","fixture executable");
    writeJsonAtomic(state,{{"games",json::array({{{"gameDirectory",toUtf8((f.root/L"Game").wstring())},{"primaryExecutable",toUtf8((f.root/L"Game"/L"Game.exe").wstring())}}})}});
    return state;
}
static json fixtureDelete(Fixture& f) { return commitSteamLibraryDeletePlan(f.data,fixtureScanState(f),f.target,f.data/L"state"/L"delete.json",true,{canonicalPathKey(f.root/L"Game")}); }
static json fixtureRefresh(Fixture& f) {
    const auto cover=f.root/L"new-cover.png";textFile(cover,"new cover");
    writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(f.root/L"Game"/L"Game.exe"),{{"cover",{{"file",toUtf8(cover.wstring())}}}}}}}});
    return refreshExistingSteamArtwork(f.data,fixtureScanState(f),f.target,f.data/L"state"/L"refresh.json",true,{canonicalPathKey(f.root/L"Game")});
}
static fs::path fixtureAutomaticIcon(Fixture& f) {
    const auto executable = f.root/L"Game"/L"Game.exe";
    textFile(executable, "fixture executable");
    const auto icon = f.root/L"automatic-icon.png";
    textFile(icon, "automatic icon bytes");
    const auto manifest = f.data/L"artwork"/toWide(gameDataId(executable.parent_path()))/L"manifest.json";
    writeJsonAtomic(manifest, {{"exe",toUtf8(executable.wstring())},{"match",json::object()},
        {"artwork",json::array({{{"type","icon"},{"steamSuffix","_icon"},{"ok",true},
            {"provider","steamgriddb"},{"file",toUtf8(icon.wstring())}}})}});
    f.plan["items"][0]["artworkManifest"] = toUtf8(manifest.wstring());
    return icon;
}
static std::string fixtureShortcutIcon(const Fixture& f) {
    const auto root=BinaryVdfReader(readBinaryFile(f.target.path)).parse();
    return jsonStringCaseInsensitive(root["shortcuts"]["0"], "icon");
}
static const std::string validLocal="\"localconfig\" { \"KeepThisSetting\" \"original\" \"RecentLocalPlayedGameIDs\" \"\" }";

int wmain(int argc,wchar_t** argv) {
    try {
        check(argc==2,"Pass new workspace Build fixture root"); const auto root=fs::absolute(argv[1]);
        const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
        check(pathWithin(root,build)&&!fs::exists(root),"Unsafe or reused fixture root"); fs::create_directories(root);
        json reports=json::array(); size_t failures=0;
        auto run=[&](const char* name,const std::function<void(const fs::path&)>& fn) {
            const auto caseRoot=root/(L"c"+std::to_wstring(reports.size())); fs::create_directories(caseRoot); selftestMoveFault={}; selftestSteamRunning=false;
            try {fn(caseRoot);reports.push_back({{"name",name},{"passed",true}});}
            catch(const std::exception& e){++failures;reports.push_back({{"name",name},{"passed",false},{"error",e.what()}});}
            selftestMoveFault={}; selftestSteamRunning=false;
        };
        run("rollback-first-shortcut-restores-absence",[](const fs::path& p){Fixture f(p);textFile(f.local,validLocal);
            selftestMoveFault=[](const auto& from,const auto& to){return to.ends_with(L"localconfig.vdf")&&from.find(L".yeman-stage-")!=std::wstring::npos?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{f.commit();}); check(!fs::exists(f.target.path),"Failed first import left an empty shortcuts.vdf");check(fileText(f.local)==validLocal,"Existing config changed");check(!hasStages(p),"Staged files leaked");});
        run("rollback-existing-shortcuts-restores-bytes",[](const fs::path& p){Fixture f(p,true);const auto original=readBinaryFile(f.target.path);textFile(f.local,validLocal);
            selftestMoveFault=[](const auto& from,const auto& to){return to.ends_with(L"localconfig.vdf")&&from.find(L".yeman-stage-")!=std::wstring::npos?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{f.commit();});check(readBinaryFile(f.target.path)==original,"Original shortcut bytes not restored");check(pendingJournals(p).empty(),"Successful rollback left pending journal");});
        run("failed-rollback-retains-recovery-journal",[](const fs::path& p){Fixture f(p,true);const auto original=readBinaryFile(f.target.path);textFile(f.local,validLocal);
            selftestMoveFault=[](const auto& from,const auto& to){return (to.ends_with(L"localconfig.vdf")&&from.find(L".yeman-stage-")!=std::wstring::npos)||(to.ends_with(L"shortcuts.vdf")&&from.find(L".yeman-stage-")==std::wstring::npos)?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{f.commit();});check(!pendingJournals(p).empty(),"Rollback failure discarded the only recovery journal");selftestMoveFault={};cleanupInterruptedSteamTransactions(f.data);check(readBinaryFile(f.target.path)==original,"Next-open recovery failed");check(pendingJournals(p).empty(),"Recovered journal not consumed");});
        run("failure-before-journal-cleans-localconfig-stage",[](const fs::path& p){Fixture f(p);textFile(f.local,validLocal);const auto manifest=p/L"bad-artwork.json";textFile(manifest,"invalid JSON");f.plan["items"][0]["artworkManifest"]=toUtf8(manifest.wstring());
            mustThrow([&]{f.commit();});check(!hasStages(p),"Preparation failure leaked localconfig stage");check(!fs::exists(f.target.path),"Preparation wrote shortcuts");});
        run("partial-artwork-staging-cleans-earlier-files",[](const fs::path& p){Fixture f(p);const auto cover=p/L"cover.png", manifest=p/L"bad-artwork.json";textFile(cover,"fixture image");textFile(manifest,"bad JSON");f.plan["items"][0]["manualCoverPath"]=toUtf8(cover.wstring());f.plan["items"][0]["artworkManifest"]=toUtf8(manifest.wstring());
            mustThrow([&]{f.commit();});check(!hasStages(p),"Earlier artwork stage leaked after later source failure");});
        run("completion-marker-failure-rolls-back",[](const fs::path& p){Fixture f(p);selftestMoveFault=[](const auto&,const auto& to){return to.ends_with(L"transaction.complete.json")?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{f.commit();});check(!fs::exists(f.target.path),"Completion marker failure left an empty shortcut file");});
        run("recovery-keeps-journal-if-backup-missing",[](const fs::path& p){Fixture f(p,true);const auto journal=f.data/L"backups"/L"steam-shortcuts"/L"19627"/L"1"/L"transaction.pending.json";
            writeJsonAtomic(journal,{{"schemaVersion",1},{"targetPath",toUtf8(f.target.path.wstring())},{"targetExisted",true},{"targetBackup",toUtf8((journal.parent_path()/L"missing.before.vdf").wstring())},{"localConfigPath",nullptr},{"artworkTargets",json::array()},{"stagedFiles",json::array()}});
            cleanupInterruptedSteamTransactions(f.data);check(fs::exists(journal),"Missing backup was treated as successful recovery");});
        run("recovery-rejects-unrelated-target",[](const fs::path& p){Fixture f(p);const auto outside=p/L"unrelated-user-file.txt";textFile(outside,"do not modify");const auto journal=f.data/L"backups"/L"steam-shortcuts"/L"19627"/L"1"/L"transaction.pending.json";const auto backup=journal.parent_path()/L"shortcuts.before.vdf";textFile(backup,"overwritten");
            writeJsonAtomic(journal,{{"schemaVersion",1},{"targetPath",toUtf8(outside.wstring())},{"targetExisted",true},{"targetBackup",toUtf8(backup.wstring())},{"localConfigPath",nullptr},{"artworkTargets",json::array()},{"stagedFiles",json::array()}});
            cleanupInterruptedSteamTransactions(f.data);check(fileText(outside)=="do not modify","Untrusted journal overwrote unrelated data");check(fs::exists(journal),"Unsafe journal was discarded");});
        run("cleanup-preserves-unowned-temp-and-snapshots",[](const fs::path& p){textFile(p/L"config"/L"user-notes.tmp","owned by user");textFile(p/L"backups"/L"snapshots"/L"1"/L"config"/L"library-config.json.tmp","immutable snapshot");cleanupInterruptedTemporaryFiles(p);
            check(fs::exists(p/L"config"/L"user-notes.tmp"),"Cleanup deleted arbitrary user temp");check(fs::exists(p/L"backups"/L"snapshots"/L"1"/L"config"/L"library-config.json.tmp"),"Cleanup edited immutable backup");});
        run("same-account-in-two-installations-not-collapsed",[](const fs::path& p){const auto a=p/L"SteamA",b=p/L"SteamB";fs::create_directories(a/L"userdata"/L"19627");fs::create_directories(b/L"userdata"/L"19627");check(steamAccountSources({a,b}).size()==2,"Same account ID collapsed across Steam roots");});
        run("config-null-status-preserves-roots",[](const fs::path& p){auto config=defaultLibraryConfig();config["roots"]=json::array({"Z:\\Games"});config["firstRunDefaultScan"]["status"]=nullptr;const auto path=p/L"config"/L"library-config.json";writeJsonAtomic(path,config);
            check(loadLibraryConfig(path)["roots"]==config["roots"],"Null optional status reset unrelated roots");});
        run("manual-overrides-invalid-items-recovers-backup",[](const fs::path& p){const auto path=p/L"config"/L"manual-overrides.json";writeJsonAtomic(path,{{"items",nullptr}});writeJsonAtomic(fs::path(path.wstring()+L".bak"),{{"items",{{"game.exe",{{"name","retained"}}}}}});
            check(loadManualOverrides(path)["items"].contains("game.exe"),"Invalid items silently erased usable manual edits");});
        run("config-recovery-prefers-newest-valid-candidate",[](const fs::path& p){const auto path=p/L"config"/L"library-config.json";textFile(path,"bad");const auto adjacent=fs::path(path.wstring()+L".bak"),latest=p/L"backups"/L"config"/L"library-config-9999999999999-10.json";
            auto old=defaultLibraryConfig(),fresh=old;old["roots"]=json::array({"Old"});fresh["roots"]=json::array({"New"});writeJsonAtomic(adjacent,old);writeJsonAtomic(latest,fresh);fs::last_write_time(adjacent,fs::file_time_type::clock::now()-std::chrono::hours(1));
            check(loadLibraryConfig(path)["roots"]==fresh["roots"],"Lexical filename sorting selected stale configuration");});
        run("scan-corrupt-primary-falls-back-to-bak",[](const fs::path& p){const auto path=p/L"state"/L"library-scan.json";textFile(path,"bad JSON");writeJsonAtomic(fs::path(path.wstring()+L".bak"),{{"games",json::array()}});check(loadLibraryScanState(path)["games"].is_array(),"Scan state was not recovered");});
        run("scan-invalid-rows-do-not-crash-all-games",[](const fs::path& p){const auto path=p/L"state"/L"library-scan.json";writeJsonAtomic(path,{{"games",json::array({nullptr,7,{{"gameDirectory","Z:\\Games\\Good"},{"primaryExecutable",nullptr},{"status",nullptr}}})}});const auto state=loadLibraryScanState(path);
            check(state["games"].size()==1&&state["games"][0]["primaryExecutable"].is_string(),"Malformed game records reached destructive planner");});
        run("retry-invalid-jobs-recovers-backup",[](const fs::path& p){const auto path=networkRetryQueuePath(p);writeJsonAtomic(path,{{"jobs",nullptr}});writeJsonAtomic(fs::path(path.wstring()+L".bak"),{{"jobs",{{"retained",{{"status","pending"}}}}}});
            check(loadNetworkRetryQueue(p)["jobs"].contains("retained"),"Invalid retry jobs silently discarded backup queue");});
        run("retry-null-entry-is-quarantined",[](const fs::path& p){writeJsonAtomic(networkRetryQueuePath(p),{{"jobs",{{"bad",nullptr},{"good",{{"status","pending"}}}}}});const auto q=loadNetworkRetryQueue(p);check(!q["jobs"].contains("bad")&&q["jobs"].contains("good"),"Malformed retry job breaks later wake processing");});
        run("snapshot-roundtrip-retains-config-and-assets",[](const fs::path& p){textFile(p/L"config"/L"a.json","old");textFile(p/L"artwork"/L"image.png","asset");const auto snap=createPortableBackupSnapshot(p,L"snapshots","test");check(verifyPortableBackupSnapshot(snap)["verifiedFiles"]==2,"Incorrect backup inventory");textFile(p/L"config"/L"a.json","new");restorePortableBackupSnapshot(p,snap);check(fileText(p/L"config"/L"a.json")=="old","Restore did not recover original config");});
        run("snapshot-rejects-tampered-content",[](const fs::path& p){textFile(p/L"config"/L"a.json","original");const auto snap=createPortableBackupSnapshot(p,L"snapshots","test");textFile(snap/L"config"/L"a.json","tampered");mustThrow([&]{verifyPortableBackupSnapshot(snap);});});
        run("snapshot-rejects-inconsistent-count",[](const fs::path& p){textFile(p/L"config"/L"a.json","original");const auto snap=createPortableBackupSnapshot(p,L"snapshots","test");auto m=loadJsonDocument(snap/L"snapshot-manifest.json");m["fileCount"]=99;writeJsonAtomic(snap/L"snapshot-manifest.json",m);mustThrow([&]{verifyPortableBackupSnapshot(snap);});});
        run("snapshot-rejects-duplicate-path",[](const fs::path& p){textFile(p/L"config"/L"a.json","original");const auto snap=createPortableBackupSnapshot(p,L"snapshots","test");auto m=loadJsonDocument(snap/L"snapshot-manifest.json");m["files"].push_back(m["files"][0]);m["fileCount"]=2;m["totalBytes"]=16;writeJsonAtomic(snap/L"snapshot-manifest.json",m);mustThrow([&]{verifyPortableBackupSnapshot(snap);});});
        run("snapshot-rejects-extra-unlisted-file",[](const fs::path& p){textFile(p/L"config"/L"a.json","original");const auto snap=createPortableBackupSnapshot(p,L"snapshots","test");textFile(snap/L"config"/L"extra.json","unlisted");mustThrow([&]{verifyPortableBackupSnapshot(snap);});});
        for(const auto& unsafe:{L"..\\escape.json",L"\\root-relative.json",L"C:drive-relative.json",L"config\\file.json:ads",L"config\\.\\alias.json"}) {
            const auto name="unsafe-relative-path-"+std::to_string(reports.size());run(name.c_str(),[&](const fs::path&){check(!safeSnapshotRelativePath(fs::path(unsafe)),"Unsafe Windows path accepted");});
        }
        run("snapshot-junction-source-is-not-followed",[](const fs::path& p){const auto data=p/L"data",outside=p/L"outside";textFile(outside/L"secret.json","not library data");fs::create_directories(data/L"config");
            const auto link=data/L"config"/L"external"; const DWORD flags=SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
            check(createFixtureJunction(link,outside),"Unable to create junction fixture");mustThrow([&]{createPortableBackupSnapshot(data,L"snapshots","symlink-test");});});

        run("delete-completion-failure-rolls-back",[](const fs::path& p){Fixture f(p);f.commit();const auto original=readBinaryFile(f.target.path);
            selftestMoveFault=[](const auto&,const auto& to){return to.ends_with(L"transaction.complete.json")?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{fixtureDelete(f);});check(readBinaryFile(f.target.path)==original,"Deletion completion failure lost original shortcuts");check(!hasStages(p),"Delete stages leaked");});
        run("delete-failed-rollback-keeps-journal",[](const fs::path& p){Fixture f(p);f.commit();const auto original=readBinaryFile(f.target.path);
            selftestMoveFault=[](const auto& from,const auto& to){return to.ends_with(L"transaction.complete.json")||(to.ends_with(L"shortcuts.vdf")&&from.find(L".yeman-delete-stage-")==std::wstring::npos)?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{fixtureDelete(f);});check(!pendingJournals(f.data).empty(),"Deletion lost its recovery journal");selftestMoveFault={};cleanupInterruptedSteamTransactions(f.data);check(readBinaryFile(f.target.path)==original,"Deletion did not recover on reopen");});
        run("import-result-publication-failure-is-not-false-failure",[](const fs::path& p){Fixture f(p);selftestMoveFault=[](const auto&,const auto& to){return to.ends_with(L"last-steam-add.json")?ERROR_WRITE_FAULT:0;};
            const auto result=f.commit();check(result.value("committed",false),"Durable commit was reported as failure");check(readShortcuts(f.target.path,f.target.accountId).size()==1,"Committed shortcut missing");});
        run("refresh-completion-failure-restores-old-image",[](const fs::path& p){Fixture f(p);f.commit();const auto old=f.target.path.parent_path()/L"grid"/L"3221225473p.jpg";textFile(old,"old cover");
            selftestMoveFault=[](const auto&,const auto& to){return to.ends_with(L"transaction.complete.json")?ERROR_WRITE_FAULT:0;};mustThrow([&]{fixtureRefresh(f);});
            check(fs::exists(old)&&fileText(old)=="old cover","Image replacement did not roll back");check(!fs::exists(old.parent_path()/L"3221225473p.png"),"Failed replacement left new image");});
        run("refresh-failed-rollback-keeps-journal",[](const fs::path& p){Fixture f(p);f.commit();const auto old=f.target.path.parent_path()/L"grid"/L"3221225473p.jpg";textFile(old,"old cover");
            selftestMoveFault=[](const auto&,const auto& to){return to.ends_with(L"transaction.complete.json")||(to.ends_with(L"3221225473p.jpg")&&to.find(L"\\config\\grid\\")!=std::wstring::npos)?ERROR_WRITE_FAULT:0;};mustThrow([&]{fixtureRefresh(f);});
            check(!pendingJournals(f.data).empty(),"Image refresh lost recovery journal");selftestMoveFault={};cleanupInterruptedSteamTransactions(f.data);check(fileText(old)=="old cover","Interrupted image refresh did not restore old image");});

        run("scan-missing-primary-recovers-valid-backup",[](const fs::path& p){const auto state=p/L"state"/L"library-scan.json";writeJsonAtomic(fs::path(state.wstring()+L".bak"),{{"games",json::array()}});check(loadLibraryScanState(state)["games"].is_array(),"Missing primary did not recover backup");});
        run("scan-duplicate-and-malformed-rows-sanitized",[](const fs::path& p){const auto dir=toUtf8((p/L"Game").wstring());const auto clean=validateLibraryScanState({{"games",json::array({{{"gameDirectory",dir},{"status",17},{"contentType",nullptr},{"steamNative","false"}},{{"gameDirectory",dir},{"status","ready"}}})}});check(clean["games"].size()==1,"Duplicate game directory survived");check(clean["games"][0]["status"]=="data-invalid-review-required","Malformed game is importable");});
        run("scan-invalid-lifecycle-inventory-rejected",[](const fs::path&){mustThrow([&]{validateLibraryScanState({{"games",json::array()},{"missingGames",7}});});});
        run("scan-offline-root-is-not-complete",[](const fs::path& p){const auto root=p/L"offline";const auto config=defaultLibraryConfig();const auto report=scanLibraryRoots({root},{},2,config);check(!report["roots"][0].value("scanComplete",true),"Offline root reported complete");check(!libraryScanCoversGame(report,config,root/L"Game"),"Offline games would be marked missing");});
        run("scan-empty-online-root-is-complete",[](const fs::path& p){const auto root=p/L"Games";fs::create_directories(root);const auto config=defaultLibraryConfig();const auto report=scanLibraryRoots({root},{},2,config);check(report["roots"][0].value("scanComplete",false),"Empty healthy root is incomplete");check(libraryScanCoversGame(report,config,root/L"RemovedGame"),"Genuinely deleted game cannot be detected");});
        run("scan-partial-overlapping-root-retains-old-game",[](const fs::path& p){const auto root=p/L"Games";const json report{{"roots",json::array({{{"path",toUtf8(root.wstring())},{"scanComplete",true},{"status","scanned"}},{{"path",toUtf8((root/L"Nested").wstring())},{"scanComplete",false},{"status","scan-incomplete"}}})}};check(!libraryScanCoversGame(report,defaultLibraryConfig(),root/L"Nested"/L"Game"),"Partial overlapping root incorrectly marks game missing");});
        run("scan-manual-unavailable-is-not-missing",[](const fs::path& p){const auto dir=p/L"Game";auto config=defaultLibraryConfig();config["manualGameDirectories"]=json::array({{{"path",toUtf8(dir.wstring())}}});const json report{{"roots",json::array()},{"skippedDirectories",json::array({{{"path",toUtf8(dir.wstring())},{"reason","manual-game-scan-unavailable"}}})}};check(!libraryScanCoversGame(report,config,dir),"Unavailable manual directory would be marked missing");});
        run("pending-recovery-blocks-second-import",[](const fs::path& p){Fixture f(p,true);textFile(f.local,validLocal);selftestMoveFault=[](const auto& from,const auto& to){return (to.ends_with(L"localconfig.vdf")&&from.find(L".yeman-stage-")!=std::wstring::npos)||(to.ends_with(L"shortcuts.vdf")&&from.find(L".yeman-stage-")==std::wstring::npos)?ERROR_WRITE_FAULT:0;};mustThrow([&]{f.commit();});selftestMoveFault={};const auto before=readBinaryFile(f.target.path);mustThrow([&]{f.commit();});check(readBinaryFile(f.target.path)==before,"New import overwrote unresolved recovery");});
        run("rapid-transactions-use-distinct-directories",[](const fs::path& p){std::set<std::string> seen;for(int n=0;n<100;n++)check(seen.insert(toUtf8(steamTransactionDirectory(p,"19627").wstring())).second,"Same-millisecond backup collision");});
        run("refresh-success-replaces-extension",[](const fs::path& p){Fixture f(p);f.commit();const auto old=f.target.path.parent_path()/L"grid"/L"3221225473p.jpg";textFile(old,"old cover");const auto shortcuts=readBinaryFile(f.target.path);const auto result=fixtureRefresh(f);check(result.value("committed",false),"Refresh not committed");check(!fs::exists(old)&&fileText(old.parent_path()/L"3221225473p.png")=="new cover","Old extension was not replaced");check(readBinaryFile(f.target.path)==shortcuts,"Artwork refresh rewrote shortcut bytes");});
        run("refresh-manual-horizontal-cover-is-independent",[](const fs::path& p){Fixture f(p);f.commit();const auto state=fixtureScanState(f),image=p/L"long.png";textFile(image,"manual long");writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(p/L"Game"/L"Game.exe"),{{"long",{{"file",toUtf8(image.wstring())}}}}}}}});const auto result=refreshExistingSteamArtwork(f.data,state,f.target,f.data/L"state"/L"refresh.json",true,{canonicalPathKey(p/L"Game")});check(result.value("artworkFilesActivated",0)==2,"Horizontal cover did not populate both Steam IDs");check(fileText(f.target.path.parent_path()/L"grid"/L"3221225473.png")=="manual long","Manual horizontal cover ignored");});
        run("delete-success-keeps-backup-receipt",[](const fs::path& p){Fixture f(p);f.commit();const auto original=readBinaryFile(f.target.path);const auto result=fixtureDelete(f);check(result.value("committed",false)&&readShortcuts(f.target.path,f.target.accountId).empty(),"Deletion did not commit");const auto backup=fs::path(toWide(result.value("backup",std::string{})));check(readBinaryFile(backup)==original&&fs::is_regular_file(backup.parent_path()/L"transaction.complete.json"),"Deletion receipt missing");});
        run("recovery-does-not-touch-steam-while-running",[](const fs::path& p){Fixture f(p,true);textFile(f.local,validLocal);selftestMoveFault=[](const auto& from,const auto& to){return (to.ends_with(L"localconfig.vdf")&&from.find(L".yeman-stage-")!=std::wstring::npos)||(to.ends_with(L"shortcuts.vdf")&&from.find(L".yeman-stage-")==std::wstring::npos)?ERROR_WRITE_FAULT:0;};mustThrow([&]{f.commit();});selftestMoveFault={};selftestSteamRunning=true;const auto before=readBinaryFile(f.target.path);cleanupInterruptedSteamTransactions(f.data);check(readBinaryFile(f.target.path)==before&&!pendingJournals(f.data).empty(),"Recovery bypassed Steam guard");});
        run("sequential-account-failure-preserves-first-commit",[](const fs::path& p){Fixture first(p/L"a"),second(p/L"b");first.commit();const auto before=readBinaryFile(first.target.path);selftestMoveFault=[&](const auto& from,const auto& to){return to==second.target.path.wstring()&&from.find(L".yeman-stage-")!=std::wstring::npos?ERROR_WRITE_FAULT:0;};mustThrow([&]{second.commit();});check(readBinaryFile(first.target.path)==before,"Failed account rewrote successful account");check(!fs::exists(second.target.path),"Failed account left shortcut file");});

        run("three-manual-artwork-slots-stage-independently",[](const fs::path& p){Fixture f(p);textFile(p/L"cover.png","cover");textFile(p/L"long.png","long");textFile(p/L"hero.png","hero");f.plan["items"][0]["manualCoverPath"]=toUtf8((p/L"cover.png").wstring());f.plan["items"][0]["manualLongPath"]=toUtf8((p/L"long.png").wstring());f.plan["items"][0]["manualWallpaperPath"]=toUtf8((p/L"hero.png").wstring());f.commit();const auto grid=f.target.path.parent_path()/L"grid";check(fileText(grid/L"3221225473p.png")=="cover","Vertical cover ignored");check(fileText(grid/L"3221225473.png")=="long","Wallpaper overwrote horizontal cover");check(fileText(grid/L"3221225473_hero.png")=="hero","Wallpaper did not populate hero slot");});
        run("manual-wallpaper-counts-as-hero-not-long",[](const fs::path& p){textFile(p/L"cover.png","cover");textFile(p/L"hero.png","hero");const json manual{{"cover",{{"file",toUtf8((p/L"cover.png").wstring())}}},{"wallpaper",{{"file",toUtf8((p/L"hero.png").wstring())}}}};const auto readiness=artworkReadiness(json::object(),manual);check(readiness["minimumComplete"]==true&&readiness["hero"]==true&&readiness["long"]==false,"Artwork readiness confused wallpaper with horizontal cover");});
        run("refresh-manual-wallpaper-keeps-horizontal-cover",[](const fs::path& p){Fixture f(p);f.commit();const auto state=fixtureScanState(f),hero=p/L"hero.png",grid=f.target.path.parent_path()/L"grid";textFile(hero,"hero");textFile(grid/L"3221225473.png","long before");writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(p/L"Game"/L"Game.exe"),{{"wallpaper",{{"file",toUtf8(hero.wstring())}}}}}}}});refreshExistingSteamArtwork(f.data,state,f.target,f.data/L"state"/L"refresh.json",true,{canonicalPathKey(p/L"Game")});check(fileText(grid/L"3221225473.png")=="long before"&&fileText(grid/L"3221225473_hero.png")=="hero","Wallpaper refresh clobbered horizontal cover");});
        run("retry-malformed-fields-are-quarantined",[](const fs::path& p){writeJsonAtomic(networkRetryQueuePath(p),{{"jobs",{{"bad-time",{{"nextAttemptAt",nullptr}}},{"bad-args",{{"commandArgs",json::array({7})}}},{"bad-count",{{"consecutiveFailures",-1}}},{"good",{{"status","pending"},{"consecutiveFailures",1}}}}}});const auto queue=loadNetworkRetryQueue(p);check(queue["jobs"].size()==1&&queue["jobs"].contains("good"),"Invalid retry fields survived normalization");check(queue["quarantinedJobs"].size()==3,"Rejected retry jobs lost audit evidence");});
        run("initialization-restores-missing-config-before-defaults",[](const fs::path& p){const auto data=p/L"data",configPath=data/L"config"/L"library-config.json";auto config=defaultLibraryConfig();config["roots"]=json::array({toUtf8((p/L"MyGames").wstring())});writeJsonAtomic(fs::path(configPath.wstring()+L".bak"),config);const auto previous=custom_steam_library::configuredDataRoot();SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",data.c_str());initializeSteamBigPictureDataRoot(data,p);SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",previous.empty()?nullptr:previous.c_str());check(loadLibraryConfig(configPath)["roots"]==config["roots"],"Startup overwrote recoverable missing config with defaults");});
        run("cleanup-does-not-race-active-data-transaction",[](const fs::path& p){const auto temp=p/L"active.json.yeman-tmp-1-1";textFile(temp,"active transaction");DataTransactionMutex held;std::thread worker([&]{cleanupInterruptedTemporaryFiles(p);});worker.join();check(fs::exists(temp),"Cleanup deleted a live data transaction temp file");});
        run("writer-mutex-blocks-concurrent-steam-operation",[](const fs::path&){SteamShortcutWriterMutex held;std::atomic<bool> refused{false};std::thread worker([&]{try{SteamShortcutWriterMutex competing(0);}catch(...){refused=true;}});worker.join();check(refused,"Concurrent Steam writer was not serialized");});
        run("malformed-artwork-manifest-cannot-crash-plan",[](const fs::path& p){Fixture f(p);const auto state=fixtureScanState(f);auto game=loadLibraryScanState(state)["games"][0];const auto manifest=f.data/L"artwork"/toWide(gameDataId(p/L"Game"))/L"manifest.json";writeJsonAtomic(manifest,{{"exe",toUtf8((p/L"Game"/L"Game.exe").wstring())},{"ok",true},{"match",{{"identityStatus",nullptr}}}});check(!portableArtworkManifest(f.data,game,p/L"Game"/L"Game.exe"),"Malformed identity reached the planner");});
        run("unsafe-artwork-suffix-is-rejected",[](const fs::path& p){check(!validSteamArtworkSuffix("hero","../escaped"),"Unsafe artwork suffix accepted");check(validSteamArtworkSuffix("long",""),"Long artwork intentionally empty suffix rejected");Fixture f(p);textFile(p/L"cover.png","image");const auto manifest=p/L"manifest.json";writeJsonAtomic(manifest,{{"artwork",json::array({{{"ok",true},{"file",toUtf8((p/L"cover.png").wstring())},{"type","hero"},{"steamSuffix","../escaped"}}})}});f.plan["items"][0]["artworkManifest"]=toUtf8(manifest.wstring());mustThrow([&]{f.commit();});check(!fs::exists(f.target.path)&&!hasStages(p),"Bad suffix changed Steam or leaked stages");});

        run("scan-executable-outside-game-cannot-be-destructively-matched",[](const fs::path& p){const auto state=validateLibraryScanState({{"games",json::array({{{"gameDirectory",toUtf8((p/L"Game").wstring())},{"primaryExecutable",toUtf8((p/L"Other"/L"Game.exe").wstring())},{"status","ready"}}})}});check(state["games"][0]["primaryExecutable"]==""&&state["games"][0]["status"]=="data-invalid-review-required","Unrelated executable accepted");});
        run("scan-incomplete-auto-primary-is-review-only",[](const fs::path& p){const auto state=validateLibraryScanState({{"games",json::array({{{"gameDirectory",toUtf8((p/L"Game").wstring())},{"primaryExecutable",toUtf8((p/L"Game"/L"Game.exe").wstring())},{"status","ready"},{"scanComplete",false},{"selectionSource","automatic"}}})}});check(state["games"][0]["status"]=="scan-incomplete-needs-review","Partial scan is auto-importable");});
        run("real-target-guard-does-not-trust-plan-flag",[](const fs::path& p){Fixture f(p);f.plan["realSteamTarget"]=false;selftestSteamRunning=true;mustThrow([&]{f.commit();});check(!fs::exists(f.target.path),"Forged plan bypassed Steam running guard");});
        run("config-unchanged-save-does-not-activate-while-steam-is-open",[](const fs::path& p){
            const auto target=p/L"data"/L"config"/L"library-config.json"; fs::create_directories(target.parent_path());
            auto config=defaultLibraryConfig(); config["roots"]=json::array({toUtf8((p/L"Games").wstring())}); writeJsonAtomic(target,config);
            selftestSteamRunning=true; size_t moveAttempts=0; selftestMoveFault=[&](const std::wstring&,const std::wstring&){++moveAttempts;return ERROR_ACCESS_DENIED;};
            auto unchanged=config; unchanged["updatedAt"]=unixTimeMs(); writeJsonWithBackup(target,p/L"data"/L"backups"/L"config","library-config",unchanged);
            check(moveAttempts==0,"Unchanged library config still attempted an atomic activation while another process could hold it");
        });
        run("config-save-retries-transient-access-denied",[](const fs::path& p){
            const auto target=p/L"data"/L"config"/L"library-config.json"; fs::create_directories(target.parent_path());
            auto config=defaultLibraryConfig(); writeJsonAtomic(target,config); size_t moveAttempts=0;
            selftestMoveFault=[&](const std::wstring&,const std::wstring& destination) -> DWORD {if(destination!=target.wstring())return DWORD{ERROR_SUCCESS};return ++moveAttempts < 4 ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;};
            auto changed=config; changed["enabled"]=true; writeJsonWithBackup(target,p/L"data"/L"backups"/L"config","library-config",changed);
            check(moveAttempts==4,"Transient config lock was not retried until activation succeeded");
            check(loadLibraryConfig(target)["enabled"]==true,"Config changed after transient lock recovery");
        });
        run("config-save-persistent-access-denied-retains-original",[](const fs::path& p){
            const auto target=p/L"data"/L"config"/L"library-config.json"; fs::create_directories(target.parent_path());
            auto config=defaultLibraryConfig(); writeJsonAtomic(target,config); const auto original=readBinaryFile(target);
            selftestMoveFault=[&](const std::wstring&,const std::wstring& destination){return destination==target.wstring()?DWORD{ERROR_ACCESS_DENIED}:DWORD{ERROR_SUCCESS};};
            auto changed=config; changed["enabled"]=true; mustThrow([&]{writeJsonWithBackup(target,p/L"data"/L"backups"/L"config","library-config",changed);});
            check(readBinaryFile(target)==original,"Persistent config lock changed the original bytes");
            for(const auto& entry:fs::directory_iterator(target.parent_path())) check(entry.path().filename().wstring().find(L".yeman-tmp-")==std::wstring::npos,"Config lock leaked an activation temp file");
        });

        run("same-exe-root-and-bin-duplicates-repaired",[](const fs::path& p){const auto exe=toUtf8((p/L"Game"/L"bin"/L"Game.exe").wstring());json a={{"gameDirectory",toUtf8((p/L"Game").wstring())},{"primaryExecutable",exe},{"status","ready"}};auto b=a;b["gameDirectory"]=toUtf8((p/L"Game"/L"bin").wstring());for(const auto& rows:{json::array({a,b}),json::array({b,a})}){const auto clean=validateLibraryScanState({{"games",rows},{"missingGames",json::array({b})},{"unscannedGames",json::array({b})}});check(clean["games"].size()==1&&clean["games"][0]["gameDirectory"]==a["gameDirectory"],"Same EXE survived or winner depends on order");check(clean["games"][0]["alternateGameDirectories"].size()==1,"Nested selection alias lost");check(clean["missingGames"].empty()&&clean["unscannedGames"].empty(),"Active duplicate survived as lifecycle ghost");}});
        run("same-exe-path-case-and-slashes-are-equivalent",[](const fs::path& p){auto exe=toUtf8((p/L"Game"/L"Game.exe").wstring());json a={{"gameDirectory",toUtf8((p/L"Game").wstring())},{"primaryExecutable",exe},{"status","ready"}};auto b=a;auto altered=asciiLower(exe);std::replace(altered.begin(),altered.end(),'\\','/');b["primaryExecutable"]=altered;b["gameDirectory"]=toUtf8((p/L"Game"/L"Sub").wstring());const auto rows=custom_steam_library::uniqueExecutableInventory(json::array({a,b}),[](const auto& value){return canonicalPathKey(fs::path(toWide(value)));});check(rows.size()==1,"Case or slash duplicate survived");});
        run("same-filename-different-full-paths-are-not-duplicates",[](const fs::path& p){json a={{"gameDirectory",toUtf8((p/L"GameA").wstring())},{"primaryExecutable",toUtf8((p/L"GameA"/L"Game.exe").wstring())},{"status","ready"}};auto b=a;b["gameDirectory"]=toUtf8((p/L"GameB").wstring());b["primaryExecutable"]=toUtf8((p/L"GameB"/L"Game.exe").wstring());const auto clean=validateLibraryScanState({{"games",json::array({a,b})}});check(clean["games"].size()==2,"Unrelated EXEs collapsed by filename");});
        run("empty-executables-do-not-collapse",[](const fs::path& p){const auto clean=validateLibraryScanState({{"games",json::array({{{"gameDirectory",toUtf8((p/L"A").wstring())},{"primaryExecutable",""}},{{"gameDirectory",toUtf8((p/L"B").wstring())},{"primaryExecutable",""}}})}});check(clean["games"].size()==2,"Different unresolved games collapsed");});
        run("dedup-preserves-manual-selection-and-native-priority",[](const fs::path& p){json a={{"gameDirectory",toUtf8((p/L"Game").wstring())},{"primaryExecutable",toUtf8((p/L"Game"/L"bin"/L"Game.exe").wstring())},{"status","ready"}};auto b=a;b["gameDirectory"]=toUtf8((p/L"Game"/L"bin").wstring());b["manualGameDirectoryLocked"]=true;auto rows=custom_steam_library::uniqueExecutableInventory(json::array({a,b}),[](const auto& v){return canonicalPathKey(fs::path(toWide(v)));});check(rows[0]["gameDirectory"]==b["gameDirectory"],"Manual directory lock lost");a["steamNative"]=true;rows=custom_steam_library::uniqueExecutableInventory(json::array({b,a}),[](const auto& v){return canonicalPathKey(fs::path(toWide(v)));});check(rows[0]["steamNative"].get<bool>(),"Native exclusion lost");});
        run("explicit-nested-alias-selection-matches-single-plan-row",[](const fs::path& p){Fixture f(p);const auto scan=fixtureScanState(f);auto state=loadJsonDocument(scan);auto game=state["games"][0];game["primaryExecutable"]=toUtf8((p/L"Game"/L"bin"/L"Game.exe").wstring());textFile(p/L"Game"/L"bin"/L"Game.exe","fixture");state["games"][0]=game;game["gameDirectory"]=toUtf8((p/L"Game"/L"bin").wstring());state["games"].push_back(game);writeJsonAtomic(scan,state);const auto plan=buildSteamLibraryAddPlan(f.data,scan,f.target,f.planFile,{canonicalPathKey(p/L"Game"/L"bin")},true);check(plan["items"].size()==1&&plan["unmatchedSelections"].empty(),"Nested alias selected nothing or selected duplicate EXEs");});


        run("builtin-steam-tool-id-is-strict",[](const auto&){
            for(const auto& id:std::vector<json>{228980,228980u,"228980"," 00228980 "})
                check(custom_steam_library::builtinExcludedSteamAppId(id),"Tool ID not excluded");
            for(const auto& id:std::vector<json>{nullptr,228980.5,-228980,228981,"228980bad","2289800","+228980","228980.0",18446744073709551615ull})
                check(!custom_steam_library::builtinExcludedSteamAppId(id),"Unrelated or malformed ID incorrectly excluded");
        });
        run("builtin-steam-tool-names-paths-are-exact",[](const auto&){
            for(const auto& name:{"Steamworks Common Redistributables","sTeAmWoRkS Shared","Steamworks_Common_Redistributables"})
                check(custom_steam_library::builtinExcludedSteamToolName(name),"Exact tool name not excluded");
            for(const auto& name:{"Steamworks Simulator","My Steamworks Shared Adventure","Steamworks Shared 2","Redistributables","\xe6\xb8\xb8\xe6\x88\x8f Steamworks Shared"})
                check(!custom_steam_library::builtinExcludedSteamToolName(name),"Unrelated title incorrectly excluded");
            check(custom_steam_library::builtinExcludedSteamToolPath("D:/Steam/steamapps/common/Steamworks Shared/vcredist/installer.exe"),"Descendant tool EXE not excluded");
            check(!custom_steam_library::builtinExcludedSteamToolPath("D:/Games/Steamworks Shared Adventure/Game.exe"),"Similar game directory excluded");
            check(custom_steam_library::builtinExcludedSteamToolPath("C:/Program Files/Epic Games/UE_5.4/Engine/Binaries/Win64/UnrealEditor.exe"),"Versioned Unreal Engine directory not excluded");
            check(!custom_steam_library::builtinExcludedSteamToolPath("D:/Games/UE Game/Game.exe"),"UE-like game directory excluded");
        });
        run("builtin-steam-tool-nested-record-and-artwork-id",[](const auto&){
            check(custom_steam_library::builtinExcludedSteamTool({{"nativeSteam",{{"appId","228980"}}}}),"Nested native ID leaked");
            check(!custom_steam_library::builtinExcludedSteamTool({{"artwork",{{"appId",228980}}},{"igdbId",228980}}),"Non-Steam artwork/IGDB ID excluded");
        });
        run("builtin-steam-tool-clears-all-lifecycle-inventories",[](const auto& p){
            json game={{"gameDirectory",toUtf8((p/L"Game").wstring())},{"primaryExecutable",""},{"status","already-in-steam"}};
            auto tool=game;tool["nativeSteamAppId"]=228980;
            const auto rows=json::array({tool,game});
            const auto state=validateLibraryScanState({{"games",rows},{"unscannedGames",rows},{"missingGames",rows}});
            check(state["games"].size()==1&&state["games"][0]==game,"Excluded row shadowed valid same-directory game");
            for(const auto* section:{"unscannedGames","missingGames"})check(state[section].size()==1&&state[section][0]==game,"Excluded tool survived lifecycle filtering");
        });
        run("builtin-steam-tool-legacy-name-and-native-no-exe",[](const auto& p){
            json good={{"gameDirectory",toUtf8((p/L"Portal 2").wstring())},{"primaryExecutable",""},{"steamNativeAppId",620}};
            auto byName=good;byName["gameDirectory"]=toUtf8((p/L"OldTool").wstring());byName["formalName"]="Steamworks Common Redistributables";
            auto byNative=good;byNative["gameDirectory"]=toUtf8((p/L"RenamedTool").wstring());byNative["nativeSteam"]={{"appId",228980}};
            const auto state=validateLibraryScanState({{"games",json::array({good,byName,byNative})}});
            check(state["games"].size()==1,"Legacy tool survived or real native game vanished");
        });
        run("builtin-steam-tool-manifests-even-renamed-directory",[](const auto& p){
            const auto steam=p/L"Steam",common=steam/L"steamapps"/L"common";
            for(const auto& name:{L"Steamworks Shared",L"RenamedRuntime",L"Portal 2"})fs::create_directories(common/name);
            textFile(steam/L"steamapps"/L"appmanifest_228980.acf",R"("AppState" {"appid" "228980" "name" "Steamworks Common Redistributables" "installdir" "RenamedRuntime"})");
            textFile(steam/L"steamapps"/L"appmanifest_620.acf",R"("AppState" {"appid" "620" "name" "Portal 2" "installdir" "Portal 2"})");
            auto config=defaultLibraryConfig();config["manualGameDirectories"]=json::array({{{"path",toUtf8((common/L"Steamworks Shared").wstring())}},{{"path",toUtf8((common/L"RenamedRuntime").wstring())}}});
            const auto report=scanLibraryRoots({common},{},2,config);
            check(report["games"].size()==1&&report["games"][0].value("nativeSteamAppId",0)==620,"Shared runtime detected as game or Portal removed");
            bool rejected=false;for(const auto& row:report["skippedDirectories"])if(row.value("reason",std::string{})=="builtin-steam-tool-excluded")rejected=true;
            check(rejected,"Builtin exclusion not recorded in scan audit");
        });
        run("builtin-steam-tool-manual-primary-cannot-override",[](const auto& p){
            const auto tool=p/L"Steamworks Shared",exe=tool/L"vcredist.exe";textFile(exe,"fixture only");
            auto config=defaultLibraryConfig();config["manualGameDirectories"]=json::array({{{"path",toUtf8(tool.wstring())}}});
            config["manualPrimary"][canonicalPathKey(tool)]=toUtf8(exe.wstring());
            const auto report=scanLibraryRoots({p},{},2,config);check(report["games"].empty(),"Manual primary bypassed builtin blacklist");
            const auto direct=scanLibraryRoots({tool},{},2,config);check(direct["games"].empty(),"Direct tool root bypassed blacklist");
        });
        run("builtin-steam-tool-old-plan-is-never-import-ready",[](const auto& p){
            Fixture f(p);const auto scan=fixtureScanState(f);auto state=loadJsonDocument(scan);state["games"][0]["nativeSteamAppId"]=228980;writeJsonAtomic(scan,state);
            const auto plan=buildSteamLibraryAddPlan(f.data,scan,f.target,f.planFile,{},true);
            check(plan["items"].empty(),"Old tool cache entered add plan");
        });
        run("automatic-icon-add-binds-vdf-and-grid-together",[](const auto& p){
            Fixture f(p);fixtureAutomaticIcon(f);const auto result=f.commit();
            const auto icon=f.target.path.parent_path()/L"grid"/L"3221225473_icon.png";
            check(fixtureShortcutIcon(f)==toUtf8(icon.wstring()),"Downloaded icon not bound to shortcut");
            check(fileText(icon)=="automatic icon bytes","Bound icon file missing");
            check(result["items"][0]["automaticIconPath"]==toUtf8(icon.wstring()),"Icon binding absent from receipt");
        });
        run("automatic-icon-add-completion-failure-rolls-back-vdf-and-grid",[](const auto& p){
            Fixture f(p);fixtureAutomaticIcon(f);
            selftestMoveFault=[](const auto&,const auto& to){return to.find(L"transaction.complete.json")!=std::wstring::npos?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{f.commit();});selftestMoveFault={};
            check(!fs::exists(f.target.path),"Failed first icon import left shortcut");
            check(!fs::exists(f.target.path.parent_path()/L"grid"/L"3221225473_icon.png"),"Failed import left icon");
            check(!hasStages(p)&&pendingJournals(p).empty(),"Failed icon import left stages or unresolved journal");
        });
        run("automatic-icon-refresh-binds-empty-field-without-other-vdf-changes",[](const auto& p){
            Fixture f(p);f.commit();fixtureAutomaticIcon(f);
            auto expected=BinaryVdfDocument::parse(readBinaryFile(f.target.path));
            const auto icon=f.target.path.parent_path()/L"grid"/L"3221225473_icon.png";
            check(bindAutomaticSteamShortcutIcon(expected,p/L"Game"/L"Game.exe",icon),"Expected empty icon");
            const auto result=fixtureRefresh(f);
            check(readBinaryFile(f.target.path)==expected.serialize(),"Refresh changed unrelated shortcut fields");
            check(fileText(icon)=="automatic icon bytes"&&result["automaticIconsApplied"]==1,"Icon not committed");
        });
        run("automatic-icon-refresh-preserves-manually-chosen-grid-icon",[](const auto& p){
            Fixture f(p);f.commit();fixtureAutomaticIcon(f);
            const auto icon=f.target.path.parent_path()/L"grid"/L"3221225473_icon.png";textFile(icon,"manual choice");
            auto doc=BinaryVdfDocument::parse(readBinaryFile(f.target.path));
            bindAutomaticSteamShortcutIcon(doc,p/L"Game"/L"Game.exe",icon);writeAtomic(f.target.path,doc.serialize());
            const auto before=readBinaryFile(f.target.path);const auto result=fixtureRefresh(f);
            check(readBinaryFile(f.target.path)==before&&fileText(icon)=="manual choice","Automatic icon overwrote manual choice");
            check(result["automaticIconsApplied"]==0&&!result["shortcutsVdfModified"].template get<bool>(),"Manual icon caused VDF rewrite");
        });
        run("automatic-icon-refresh-vdf-activation-failure-rolls-back-images",[](const auto& p){
            Fixture f(p);f.commit();fixtureAutomaticIcon(f);const auto before=readBinaryFile(f.target.path);
            selftestMoveFault=[&](const auto& from,const auto& to){return to==f.target.path.wstring()&&from.find(L".yeman-stage-")!=std::wstring::npos?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{fixtureRefresh(f);});selftestMoveFault={};
            check(readBinaryFile(f.target.path)==before,"Failed icon binding changed VDF");
            check(!fs::exists(f.target.path.parent_path()/L"grid"/L"3221225473_icon.png"),"Failed icon binding left new icon");
            check(!hasStages(p)&&pendingJournals(p).empty(),"Failed icon refresh leaked journal/stages");
        });
        run("automatic-icon-refresh-completion-failure-restores-both-resources",[](const auto& p){
            Fixture f(p);f.commit();fixtureAutomaticIcon(f);const auto before=readBinaryFile(f.target.path);
            selftestMoveFault=[](const auto&,const auto& to){return to.find(L"transaction.complete.json")!=std::wstring::npos?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{fixtureRefresh(f);});selftestMoveFault={};
            check(readBinaryFile(f.target.path)==before,"Completion failure left automatic icon reference");
            check(!fs::exists(f.target.path.parent_path()/L"grid"/L"3221225473_icon.png"),"Completion failure left icon file");
            check(pendingJournals(p).empty(),"Successful icon rollback kept journal");
        });
        run("automatic-icon-refresh-failed-rollback-is-recoverable",[](const auto& p){
            Fixture f(p);f.commit();fixtureAutomaticIcon(f);const auto before=readBinaryFile(f.target.path);
            selftestMoveFault=[&](const auto& from,const auto& to){
                return to.find(L"transaction.complete.json")!=std::wstring::npos ||
                    (to==f.target.path.wstring()&&from.find(L".yeman-stage-")==std::wstring::npos)?ERROR_WRITE_FAULT:0;
            };
            mustThrow([&]{fixtureRefresh(f);});selftestMoveFault={};
            check(!pendingJournals(p).empty(),"Failed icon rollback lost recovery journal");
            cleanupInterruptedSteamTransactions(f.data);
            check(readBinaryFile(f.target.path)==before&&pendingJournals(p).empty(),"Icon transaction recovery failed");
        });
        run("empty-root-first-run-scan-is-retryable",[](const auto& p){
            const auto path=p/L"config"/L"library-config.json";
            for(const auto& status:{"pending","running","completed","skipped-existing-roots"}) {
                auto config=defaultLibraryConfig();config["roots"]=json::array();config["firstRunDefaultScan"]["status"]=status;writeJsonAtomic(path,config);
                const auto loaded=loadLibraryConfig(path);
                check(loaded["roots"].empty(),"Empty-root config was populated unexpectedly");
                check(loaded["firstRunDefaultScan"].value("status",std::string{})==status || status==std::string("skipped-existing-roots"),"First-run status was corrupted");
            }
        });
        const json summary{{"allPassed",failures==0},{"caseCount",reports.size()},{"failedCount",failures},{"cases",reports},{"fixtures",toUtf8(root.wstring())},{"realSteamFilesModified",false}};
        writeJsonAtomic(root/L"summary.json",summary);std::cout<<summary.dump(2)<<"\n";return failures?1:0;
    }catch(const std::exception& e){std::cerr<<"DATA_CHAIN_TEST_FAILED: "<<e.what()<<"\n";return 1;}
}
