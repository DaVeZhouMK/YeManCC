#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "custom_steam_library_storage_isolation.h"
#define wWinMain unusedHostMain
#include "../native/custom-steam-library/workspace_host.cpp"
#undef wWinMain
#include <iostream>

namespace oc = custom_steam_library::opti_catalog;
static unsigned passed = 0;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> static void test(const char* name, F action) {
    action(); ++passed; std::cout << "PASS " << name << '\n';
}
template<class F> static void throws(F action) {
    bool rejected = false; try { action(); } catch (...) { rejected = true; }
    check(rejected, "Expected rejection");
}
struct Memory {
    std::optional<std::string> bytes = "[]";
    std::optional<std::string> saved;
    unsigned reads = 0, writes = 0, backups = 0, copies = 0, ids = 0, checks = 0, steps = 0;
    bool running = false, cancelled = false;
    std::function<void()> onRead, onBackup, onCheck, onPace;
    oc::Ports ports() {
        oc::Ports p;
        p.read = [&] { ++reads; if (onRead) onRead(); return bytes; };
        p.backup = [&](const std::string& b) { ++backups; saved = b; if (onBackup) onBackup(); };
        p.commit = [&](const std::string& b) { ++writes; bytes = b; };
        p.clientRunning = [&] { ++checks; if (onCheck) onCheck(); return running; };
        p.cancelled = [&] { return cancelled; };
        p.pace = [&] { ++steps; if (onPace) onPace(); };
        p.newId = [&] { return std::to_string(++ids); };
        p.copyCover = [&](const oc::Candidate&) { ++copies; return "C:\\Fixture\\Covers\\copy.png"; };
        return p;
    }
    json games() const { return oc::parseCatalog(bytes); }
    void set(const json& games) { bytes = games.dump(); }
};
static oc::Candidate manual() {
    return {"D:\\Games\\Example", "D:\\Games\\Example\\Binaries\\Game.exe", "Example", "D:\\Cache\\cover.png", 0};
}
static void planNativeOptiCatalog(const fs::path& fixture) {
    struct RestoreRoot { fs::path old = g_dataRoot; ~RestoreRoot() { g_dataRoot = std::move(old); } } restore;
    g_dataRoot=fixture/L"data";
    const auto utf=[](const fs::path& p){return toUtf8(p.wstring());};
    fs::create_directories(g_dataRoot);
    { std::ofstream out(g_dataRoot/L"data-root.json");out<<json{{"schemaVersion",2},{"dataLayoutVersion",2},{"dataRoot",utf(g_dataRoot)}}.dump();check(bool(out),"Root identity seed failed"); }
    const auto apps=fixture/L"SteamLibrary"/L"steamapps";
    auto make=[&](int id,const std::wstring& name,const std::string& vdfId,const std::string& vdfDirectory,bool executable=true){
        const auto dir=apps/L"common"/name, exe=dir/L"Game.exe", manifest=apps/(L"appmanifest_"+std::to_wstring(id)+L".acf");
        fs::create_directories(dir);
        if(executable)writeHostBytesAtomic(exe,"inert fixture executable");
        if(!vdfId.empty())writeHostBytesAtomic(manifest,"\"AppState\" { \"appid\" \""+vdfId+"\" \"installdir\" \""+vdfDirectory+"\" }");
        return json{{"gameDirectory",utf(dir)},{"directoryName",utf(name)},{"primaryExecutable",executable?json(utf(exe)):json(nullptr)},
            {"status","already-in-steam"},{"steamNative",true},{"steamOwnership","native-steam"},{"contentType","steam-native"},
            {"nativeSteamAppId",id},{"nativeSteamManifestPath",utf(manifest)}};
    };
    const auto confirmed=make(101,L"Exact","101","Exact");
    const auto folder=make(102,L"NoExecutable","102","NoExecutable",false);
    const auto scanned=make(103,L"Scanned","103","Scanned");
    auto scannedPlan=scanned;scannedPlan["formalName"]="Old plan title";
    auto alias=confirmed;
    auto wrongId=make(104,L"WrongId","999","WrongId");
    auto wrongDir=make(105,L"WrongDir","105","SomethingElse");
    auto missing=make(106,L"Missing","","Missing");
    auto unconfirmed=make(107,L"Unconfirmed","107","Unconfirmed");unconfirmed["nativeSteamScanUnconfirmed"]=true;
    auto wrongFile=make(108,L"WrongFile","108","WrongFile");wrongFile["nativeSteamManifestPath"]=confirmed["nativeSteamManifestPath"];
    auto staleExe=make(109,L"RetiredExe","109","RetiredExe",false);staleExe["primaryExecutable"]=utf(apps/L"common"/L"RetiredExe"/L"Missing.exe");
    auto malformed=make(110,L"Malformed","","Malformed");
    writeHostBytesAtomic(apps/L"appmanifest_110.acf","\"AppState\" { \"appid\" \"110\" \"appid\" \"110\" \"installdir\" \"Malformed\" }");
    auto tool=make(228980,L"Tool","228980","Tool");
    const auto missingScan=make(111,L"ScanMissing","111","ScanMissing");
    auto deferredScan=make(112,L"DeferredScan","112","DeferredScan");
    const auto deferredPlan=deferredScan;deferredScan["nativeSteamScanUnconfirmed"]=true;
    auto storeOnly=make(113,L"StoreOnly","113","StoreOnly");
    storeOnly.erase("nativeSteamAppId");storeOnly["steamStoreAppId"]=113;
    const auto libraryPath=g_dataRoot/L"state"/L"library-scan.json";
    auto library=json{{"games",json::array({scanned})},{"missingGames",json::array({missingScan})},
        {"unscannedGames",json::array({deferredScan})}};
    writeHostBytesAtomic(libraryPath,library.dump());
    writeHostBytesAtomic(workspacePlanPath(),json{{"libraryState",utf(libraryPath)},{"summary",json::object()},
        {"items",json::array({confirmed,alias,folder,scannedPlan,wrongId,wrongDir,missing,unconfirmed,wrongFile,staleExe,malformed,
            tool,missingScan,deferredPlan,storeOnly})}}.dump());
    const auto collect=[] {return optiCatalogCandidates([]{return false;},false);};
    const auto rows=collect();
    std::set<int> ids;for(const auto& row:rows)ids.insert(row.steamAppId);
    std::cout<<"plan-native candidates="<<rows.size()<<" ids="<<json(ids).dump()<<std::endl;
    check(rows.size()==3&&ids==std::set<int>{101,102,103},"Confirmed native plan rows missing or stale records included");
    check(rows.front().steamAppId==103&&rows.front().name=="Scanned","Cached plan took precedence over current scan");
    const auto client=fixture/L"FreshOpti";
    const auto receipt=supplementOptiCatalog(client,rows,[]{return false;});
    const auto bytes=readText(client/L"games.json");const auto games=json::parse(bytes);
    check(receipt["added"]==3&&games.size()==3,"First OPT pass did not create native game rows");
    const auto game=[&](const char* id)->const json&{for(const auto& row:games)if(row["AppId"]==id)return row;throw std::runtime_error("Expected native OPT identity missing");};
    check(game("101")["Platform"]==0&&game("101")["ExecutablePath"]==confirmed["primaryExecutable"],"Native game lost its platform, app ID or executable");
    check(game("102")["Platform"]==0&&!game("102").contains("ExecutablePath"),"Folder-only native game invented an executable");
    check(supplementOptiCatalog(client,collect(),[]{return false;})["status"]=="idle"&&readText(client/L"games.json")==bytes,"Second native supplement changed saved rows");
    library["nativeSteamInstallationUnavailable"]=true;writeHostBytesAtomic(libraryPath,library.dump());
    const auto incomplete=collect();
    check(incomplete.size()==1&&incomplete[0].steamAppId==103,"Unconfirmed native scan borrowed plan-only identities");

    auto legacy=[&](int id,const wchar_t* name,bool exeExists=true) {
        auto row=make(id,name,std::to_string(id),toUtf8(name),exeExists);
        if(!exeExists)row["primaryExecutable"]=utf(apps/L"common"/name/L"Game.exe");
        row["lifecycleStatus"]="primary-executable-missing-or-filtered";
        return row;
    };
    const auto recovered=legacy(114,L"OldMissing");
    const auto excluded=legacy(115,L"UserRemoved");
    const auto filtered=legacy(116,L"Filtered");
    const auto gone=legacy(117,L"Gone",false);
    const auto mismatch=legacy(118,L"WrongPrimary");
    auto differentPrimary=mismatch;
    const auto replacement=apps/L"common"/L"WrongPrimary"/L"Backup.exe";
    writeHostBytesAtomic(replacement,"inert alternate fixture executable");
    differentPrimary["primaryExecutable"]=utf(replacement);
    auto removed=legacy(119,L"Uninstalled");removed["lifecycleStatus"]="game-directory-missing";
    writeHostBytesAtomic(g_dataRoot/L"config"/L"library-config.json",json{{"schemaVersion",1},
        {"excludedGameDirectories",json::array({json{{"path",excluded["gameDirectory"]}}})}}.dump());
    library={{"games",json::array()},{"missingGames",json::array({recovered,excluded,filtered,gone,mismatch,removed})},
        {"skippedDirectories",json::array({json{{"path",utf(apps.parent_path())},{"reason","platform-library-excluded"}},
            json{{"path",filtered["gameDirectory"]},{"reason","non-game-directory-excluded"}}})}};
    writeHostBytesAtomic(libraryPath,library.dump());
    writeHostBytesAtomic(workspacePlanPath(),json{{"libraryState",utf(libraryPath)},{"summary",json::object()},
        {"items",json::array({recovered,excluded,filtered,gone,differentPrimary,removed})}}.dump());
    const auto collectFirst=[&] {
        const auto active=collect();std::vector<int> ids;
        for(const auto& row:active)ids.push_back(row.steamAppId);
        std::cout<<"legacy-native candidates="<<active.size()<<" ids="<<json(ids).dump()<<std::endl;
        return active;
    };
    const auto restored=collectFirst();
    check(restored.size()==1&&restored.front().steamAppId==114&&restored.front().executablePath==jsonStringOr(recovered,"primaryExecutable"),
        "Legacy scan false missing blocked the verified native installation");
    const auto restoredClient=fixture/L"RestoredOpti";
    const auto receipt2=runOptiCatalogSupplement([]{return false;},[]{return true;},"fixture-first-start",
        [&]{return restoredClient;},[]{return false;},[]{return true;});
    const auto restoredBytes=readText(restoredClient/L"games.json");
    check(receipt2["status"]=="synced"&&receipt2["added"]==1&&json::parse(restoredBytes)[0]["AppId"]=="114",
        "First actual OPT pass did not write the verified legacy installation");
    check(readText(libraryPath)==library.dump(),"OPT pass changed the library lifecycle records");
    writeHostBytesAtomic(g_dataRoot/L"config"/L"library-config.json","invalid config");
    check(collect().empty(),"Unreadable exclusion policy allowed legacy plan recovery");
}
int wmain(int argc, wchar_t** argv) {
    try {
        const bool planNativeOnly=argc==3&&std::wstring_view(argv[2])==L"--plan-native";
        check(argc == 2 || planNativeOnly, "Pass an isolated Build fixture directory");
        const auto fixture = fs::absolute(argv[1]).lexically_normal();
        const auto workspace = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path();
        const auto build = fs::absolute(workspace / L"Build").lexically_normal();
        check(normalizedPathKey(fixture).starts_with(normalizedPathKey(build) + L"\\") || normalizedPathKey(fixture).starts_with(normalizedPathKey(workspace / L"Testing" / L"YMCC" / L"Evidence" / L"Build") + L"\\"), "Fixture escaped workspace test Build");
        check(!fs::exists(fixture), "Fixture must be fresh");
        fs::create_directories(fixture);
        if(planNativeOnly){test("current native plans supplement empty OPT without reviving stale entries",[&]{planNativeOptiCatalog(fixture);});return 0;}
        test("native Steam folder-only entry retains real app ID", [] {
            Memory m; auto p = m.ports(); auto row = manual(); row.executablePath.clear(); row.steamAppId = 123;
            const auto r = oc::supplement({row}, p);
            check(r.added == 1 && r.status == "synced", "Native folder was not added");
            check(m.games()[0]["Platform"] == 0 && m.games()[0]["AppId"] == "123", "Native identity changed");
            check(!m.games()[0].contains("IsOptiscalerInstalled"), "Metadata inferred installation");
        });
        test("joined non-Steam entry uses Manual identity", [] {
            Memory m; auto p = m.ports(); const auto r = oc::supplement({manual()}, p);
            check(r.added == 1 && m.games()[0]["Platform"] == 8 && m.games()[0]["AppId"] == "Manual_1", "Manual identity wrong");
            check(m.games()[0]["InstallPath"] == manual().installPath, "Wrong installation root");
        });
        test("fill missing fields, retain every existing OPT field and unrelated game", [] {
            Memory m;
            const json original = json::array({{{"ExecutablePath", manual().executablePath}, {"Platform", 8}, {"AppId", "Manual_user"},
                {"Name", "User name"}, {"IsFavorite", true}, {"IsHidden", true}, {"IsOptiscalerInstalled", true},
                {"OptiscalerVersion", "user-version"}, {"FrameGenerationSettings", {{"Route", 9}}}, {"FutureField", {{"v", 7}}}},
                {{"Name", "OPT only"}, {"ExecutablePath", "E:\\Other.exe"}, {"Custom", nullptr}}});
            m.set(original); const auto before = m.bytes; auto p = m.ports();
            const auto r = oc::supplement({manual()}, p); auto games = m.games();
            check(r.supplemented == 1 && games.size() == 2 && games[1] == original[1], "Unrelated record changed");
            for (const auto& [key, value] : original[0].items()) check(games[0][key] == value, "Existing field overwritten");
            check(games[0]["InstallPath"] == manual().installPath && r.covers == 1 && m.saved == before, "Missing metadata/backup wrong");
        });
        test("existing cover and executable are preserved when Steam ID matches", [] {
            Memory m; m.set(json::array({{{"Platform", 0}, {"AppId", "123"}, {"InstallPath", "D:\\UserRoot"},
                {"ExecutablePath", "D:\\UserRoot\\Launcher.exe"}, {"CoverImageUrl", "user-cover.png"}, {"Name", "User"}}}));
            const auto before = m.bytes; auto p = m.ports(); auto c = manual(); c.steamAppId = 123;
            const auto r = oc::supplement({c}, p);
            check(r.status == "idle" && m.bytes == before && !m.writes && !m.backups && !m.copies, "Unchanged record was written");
        });
        test("formal Steam identity conflicts cannot borrow a shared executable or directory", [] {
            for (const std::string mode : {"exe", "directory", "correct-id", "complete"}) {
                Memory m; auto c = manual(); c.steamAppId = 123;
                json other = {{"Platform", 0}, {"AppId", "620"}, {"InstallPath", c.installPath},
                    {"Name", "Other game"}, {"ExecutablePath", mode == "directory" ? c.installPath + "\\Other.exe" : c.executablePath},
                    {"UserSettings", {{"keep", true}}}};
                if (mode == "complete") other["CoverImageUrl"] = "user-cover.png";
                auto rows = json::array({other});
                if (mode == "correct-id") rows.push_back({{"Platform", 0}, {"AppId", "123"},
                    {"Name", "Desired game"}, {"InstallPath", "E:\\OldRoot"}, {"ExecutablePath", "E:\\OldRoot\\Old.exe"}});
                m.set(rows); const auto before = m.bytes; auto p = m.ports(); const auto r = oc::supplement({c}, p);
                const auto games = m.games();
                check(r.status == "synced" && games.size() == 2 && games[0] == other, "Conflicting Steam game was changed or target was omitted");
                check(games[1]["AppId"] == "123" && games[1]["Platform"] == 0 && games[1]["CoverImageUrl"] == "C:\\Fixture\\Covers\\copy.png", "Target did not receive its own Steam identity/cover");
                check(m.copies == 1 && m.steps == 1 && m.saved == before, "Conflict handling changed serial pacing or backup");
                if (mode == "correct-id") check(r.supplemented == 1 && r.added == 0 && games[1]["ExecutablePath"] == "E:\\OldRoot\\Old.exe", "Correct AppID lost to conflicting EXE");
                else check(r.added == 1 && r.supplemented == 0 && games[1]["ExecutablePath"] == c.executablePath, "Distinct native game was not created");
                const auto committed = m.bytes; const auto copies = m.copies;
                check(oc::supplement({c}, p).status == "idle" && m.bytes == committed && m.copies == copies, "Repeated conflicting candidate duplicated or rewrote");
            }
        });
        test("repeat execution is idempotent and writes nothing", [] {
            Memory m; auto p = m.ports(); oc::supplement({manual(), manual()}, p);
            check(m.games().size() == 1 && m.copies == 1, "Duplicate candidate added");
            const auto before = m.bytes; const auto writes = m.writes, backups = m.backups;
            check(oc::supplement({manual()}, p).status == "idle" && m.bytes == before && m.writes == writes && m.backups == backups && m.copies == 1, "Repeat wrote data");
        });
        test("case/slash matching and old executable subdirectory deduplicate", [] {
            Memory m; m.set(json::array({{{"InstallPath", "d:/GAMES/Example/Binaries/"}, {"Name", "User"}}}));
            auto p = m.ports(); check(oc::supplement({manual()}, p).supplemented == 1 && m.games().size() == 1, "Nested match duplicated");
            check(m.games()[0]["InstallPath"] == "d:/GAMES/Example/Binaries/", "Existing root overwritten");
        });
        test("non-ASCII Windows path casing also deduplicates", [] {
            Memory m;
            m.set(json::array({{{"InstallPath", "D:/GAMES/\xc3\x84/\xe6\xb5\x8b\xe8\xaf\x95"}}}));
            auto c = manual(); c.installPath = "d:\\games\\\xc3\xa4\\\xe6\xb5\x8b\xe8\xaf\x95";
            c.executablePath = c.installPath + "\\Game.exe";
            auto p = m.ports();
            check(oc::supplement({c}, p).supplemented == 1 && m.games().size() == 1, "Unicode casing duplicated");
        });
        test("ambiguous executable or Steam identities skip instead of guessing", [] {
            Memory m; m.set(json::array({{{"ExecutablePath", manual().executablePath}}, {{"ExecutablePath", manual().executablePath}}}));
            auto p = m.ports(); const auto before = m.bytes;
            check(oc::supplement({manual()}, p).skipped == 1 && m.bytes == before && !m.writes && !m.copies, "Duplicate EXE was guessed");
            m.set(json::array({{{"ExecutablePath", manual().executablePath}}, {{"Platform", 0}, {"AppId", "123"}}}));
            auto c = manual(); c.steamAppId = 123;
            check(oc::supplement({c}, p).skipped == 1 && !m.writes, "Conflicting identity was guessed");
        });
        test("relative/traversal/UNC paths and non-Steam folder-only entries skip", [] {
            std::vector<oc::Candidate> invalid;
            for (const auto& root : {"Games\\Example", "D:\\Games\\..\\Example", "\\\\server\\games\\Example", "D:\\"}) {
                auto c = manual(); c.installPath = root; invalid.push_back(c);
            }
            auto c = manual(); c.executablePath.clear(); invalid.push_back(c);
            c = manual(); c.executablePath = "D:\\Other\\Game.exe"; invalid.push_back(c);
            c = manual(); c.executablePath += ".bat"; invalid.push_back(c);
            Memory m; auto p = m.ports(); const auto r = oc::supplement(invalid, p);
            check(r.skipped == invalid.size() && !m.writes && !m.copies, "Unsafe candidate accepted");
        });
        test("running client performs no catalog/cover reads or writes", [] {
            Memory m; m.running = true; auto p = m.ports();
            check(oc::supplement({manual()}, p).reason == "client-running" && !m.reads && !m.writes && !m.copies, "Live catalog accessed");
        });
        test("client launch during preparation defers catalog write", [] {
            Memory m; m.onCheck = [&] { if (m.checks >= 2) m.running = true; }; auto p = m.ports();
            check(oc::supplement({manual()}, p).status == "deferred" && !m.writes && !m.backups && m.bytes == "[]", "Live catalog replaced");
        });
        test("catalog change during scan is preserved", [] {
            Memory m; m.onRead = [&] { if (m.reads == 2) m.bytes = "[{\"Name\":\"Concurrent\"}]"; }; auto p = m.ports();
            check(oc::supplement({manual()}, p).reason == "catalog-changed" && !m.writes && !m.backups && m.games()[0]["Name"] == "Concurrent", "Concurrent data lost");
        });
        test("catalog change after backup is preserved", [] {
            Memory m; m.onBackup = [&] { m.bytes = "[{\"Name\":\"After backup\"}]"; }; auto p = m.ports();
            check(oc::supplement({manual()}, p).reason == "catalog-changed" && !m.writes && m.saved == "[]", "Post-backup race lost");
        });
        test("backup failure prevents commit", [] {
            Memory m; auto p = m.ports(); p.backup = [](const std::string&) { throw std::runtime_error("fixture failure"); };
            throws([&] { oc::supplement({manual()}, p); }); check(!m.writes && m.bytes == "[]", "Commit bypassed backup error");
        });
        test("write verification rejects an external replacement", [] {
            Memory m; auto p = m.ports(); p.commit = [&](const std::string&) { m.bytes = "[]"; };
            throws([&] { oc::supplement({manual()}, p); });
        });
        test("invalid and oversized catalogs are preserved", [] {
            for (const auto& bytes : {std::string{"{"}, std::string{"{}"}, std::string{"[null]"}, std::string(oc::maxCatalogBytes + 1, ' ')}) {
                Memory m; m.bytes = bytes; auto p = m.ports();
                throws([&] { oc::supplement({manual()}, p); }); check(m.bytes == bytes && !m.writes && !m.copies && !m.backups, "Corrupt catalog changed");
            }
        });
        test("UTF8 BOM catalog remains readable and backed up byte for byte", [] {
            Memory m; m.bytes = std::string("\xef\xbb\xbf") + "[]"; const auto before = m.bytes; auto p = m.ports();
            check(oc::supplement({manual()}, p).added == 1 && m.saved == before, "BOM catalog failed");
        });
        test("missing catalog can be created without fake installation state", [] {
            Memory m; m.bytes.reset(); auto p = m.ports();
            check(oc::supplement({manual()}, p).added == 1 && !m.backups && !m.games()[0].contains("OptiscalerVersion"), "New catalog failed");
        });
        test("cancellation during paced iteration prevents commit", [] {
            Memory m; m.onPace = [&] { m.cancelled = true; }; auto p = m.ports();
            throws([&] { oc::supplement({manual()}, p); }); check(!m.writes && !m.copies && m.steps == 1, "Cancellation wrote data");
        });
        test("missing cover does not block metadata", [] {
            Memory m; auto p = m.ports(); p.copyCover = [](const oc::Candidate&) { return std::string{}; };
            const auto r = oc::supplement({manual()}, p); check(r.added == 1 && !r.covers && !m.games()[0].contains("CoverImageUrl"), "Cover failure blocked directory");
        });
        test("unknown non-empty field types are preserved", [] {
            Memory m; m.set(json::array({{{"ExecutablePath", manual().executablePath}, {"Name", false}, {"CoverImageUrl", {{"future", 1}}}, {"InstallPath", 42}}}));
            auto p = m.ports(); const auto before = m.bytes; oc::supplement({manual()}, p);
            check(m.bytes == before && !m.writes && !m.copies, "Unknown field type overwritten");
        });
        test("catalog count limit cannot drop existing games", [] {
            Memory m; json games = json::array(); for (size_t i = 0; i < oc::maxGames; ++i) games.push_back({{"Name", std::to_string(i)}});
            m.set(games); auto p = m.ports(); const auto r = oc::supplement({manual()}, p);
            check(r.skipped == 1 && !m.writes && m.games().size() == oc::maxGames, "Limit dropped a game");
        });

        test("complete OPT record skips both lazy artwork and per-item sleeping", [] {
            Memory m;auto c=manual();c.coverPath.clear();
            m.set(json::array({{{"ExecutablePath",c.executablePath},{"InstallPath",c.installPath},{"Name","User"},{"CoverImageUrl","User cover"}}}));
            auto p=m.ports();unsigned lookups=0;p.resolveCover=[&](const oc::Candidate&){++lookups;throw std::runtime_error("Must not inspect artwork");return std::string{};};
            check(oc::supplement({c},p).status=="idle"&&!lookups&&!m.steps&&!m.writes,"Complete record performed optional work");
        });
        test("lazy cover is resolved only for the row missing one", [] {
            Memory m;auto c=manual();c.coverPath.clear();auto p=m.ports();unsigned lookups=0;
            p.resolveCover=[&](const oc::Candidate&){++lookups;return "D:\\Cache\\tall.png";};
            check(oc::supplement({c},p).covers==1&&lookups==1&&m.copies==1&&m.steps==1,"Missing cover was not resolved once");
        });
        test("serial pacing admits one row only after its predecessor completes", [] {
            Memory m;auto p=m.ports();std::vector<std::string> events;
            auto a=manual(),b=a;b.installPath="E:/Games/Second";b.executablePath=b.installPath+"/Game.exe";
            p.pace=[&]{events.push_back("pace");};
            p.copyCover=[&](const auto& item){events.push_back(item.installPath);return "cover.jpg";};
            const auto r=oc::supplement({a,b},p);
            check(r.added==2&&events==std::vector<std::string>{"pace",a.installPath,"pace",b.installPath},"Optional items overlapped or escaped pacing");
        });
        test("budget before admission creates neither ID nor partial row", [] {
            Memory m;auto p=m.ports();p.pace=[]{throw oc::WorkLimit{};};
            const auto r=oc::supplement({manual()},p);
            check(r.status=="idle"&&r.workLimited&&r.nextCandidate==oc::candidateKey(manual())&&!m.ids&&!m.writes&&!m.backups&&!m.copies&&m.games().empty(),"Budget created unfinished catalog work");
        });
        test("budget commits completed prefix and resumes at the next item", [] {
            Memory m;auto p=m.ports();auto a=manual(),b=a,c=a;
            b.installPath="E:/Games/Second";b.executablePath=b.installPath+"/Game.exe";
            c.installPath="F:/Games/Third";c.executablePath=c.installPath+"/Game.exe";
            m.onPace=[&]{if(m.steps==2)throw oc::WorkLimit{};};
            const auto first=oc::supplement({a,b,c},p);
            check(first.status=="synced"&&first.workLimited&&first.added==1&&m.games().size()==1&&m.ids==1&&m.backups==1,"Completed prefix lost or unfinished row committed");
            std::vector<oc::Candidate> rest{a,b,c};check(oc::resumeCandidates(rest,first.nextCandidate)&&oc::candidateKey(rest[0])==oc::candidateKey(b),"Wrong continuation item");
            m.onPace={};const auto second=oc::supplement(rest,p);
            check(second.added==2&&!second.workLimited&&m.games().size()==3&&m.steps==4&&m.games()[1]["InstallPath"]==b.installPath,"Next invocation repeated a complete item or missed the tail");
        });
        test("missing artwork cannot starve the later candidates", [] {
            Memory m;auto a=manual(),b=a;b.installPath="E:/Games/Second";b.executablePath=b.installPath+"/Game.exe";
            m.set(json::array({{{"InstallPath",a.installPath},{"ExecutablePath",a.executablePath},{"Name",a.name}}}));
            auto p=m.ports();p.copyCover=[](const auto&){return std::string{};};m.onPace=[&]{if(m.steps==2)throw oc::WorkLimit{};};
            const auto first=oc::supplement({a,b},p);std::vector<oc::Candidate> rest{a,b};
            check(first.workLimited&&oc::resumeCandidates(rest,first.nextCandidate),"Unresolved cover did not retain a continuation");
            m.onPace={};const auto second=oc::supplement(rest,p);
            check(second.added==1&&m.games().size()==2&&m.games()[1]["InstallPath"]==b.installPath,"Unresolved cover blocked the next game");
        });
        test("work deadline inside lazy artwork discards only the staged current row", [] {
            Memory m;auto p=m.ports();p.resolveCover=[](const auto&)->std::string{throw oc::WorkLimit{};};auto a=manual();a.coverPath.clear();
            const auto r=oc::supplement({a},p);check(r.workLimited&&m.games().empty()&&!m.writes&&!m.copies,"Interrupted lazy lookup leaked a partial row");
        });
        test("partial pass retains original concurrent-write and cancellation guards", [] {
            Memory m;auto p=m.ports();auto a=manual(),b=a;b.installPath="E:/Games/Second";b.executablePath=b.installPath+"/Game.exe";
            m.onPace=[&]{if(m.steps==2){m.bytes="[{\"External\":true}]";throw oc::WorkLimit{};}};
            const auto r=oc::supplement({a,b},p);check(r.status=="deferred"&&r.reason=="catalog-changed"&&!m.writes&&!m.backups&&m.games()[0]["External"]==true,"Partial pass overwrote a concurrent edit");
        });
        test("absent continuation key preserves candidate order", [] {
            auto a=manual(),b=a;b.steamAppId=99;std::vector<oc::Candidate> items{a,b};
            check(!oc::resumeCandidates(items,"removed")&&oc::candidateKey(items[0])==oc::candidateKey(a),"Stale cursor redirected queue");
        });
        g_dataRoot = fixture / L"library-data";
        fs::create_directories(g_dataRoot);
        { std::ofstream out(g_dataRoot/L"data-root.json");out<<json{{"schemaVersion",2},{"dataLayoutVersion",2},{"dataRoot",toUtf8(g_dataRoot.wstring())}}.dump();check(bool(out),"Root identity seed failed"); }
        const auto joinedRoot = fixture / L"Joined";
        const auto nativeRoot = fixture / L"Native";
        const auto unjoinedRoot = fixture / L"NotJoined";
        const auto joinedExe = joinedRoot / L"Binaries" / L"Game.exe";
        const auto unjoinedExe = unjoinedRoot / L"Game.exe";
        writeHostBytesAtomic(joinedExe, "fixture-executable");
        writeHostBytesAtomic(unjoinedExe, "fixture-executable");
        fs::create_directories(nativeRoot);
        const auto image = g_dataRoot / L"artwork" / L"cover.png";
        // Minimal valid 1x1 PNG; copying is a metadata operation, not image processing.
        const unsigned char png[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,11,73,68,65,84,120,156,99,0,1,0,0,5,0,1,13,10,45,180,0,0,0,0,73,69,78,68,174,66,96,130};
        writeHostBytesAtomic(image, std::string(reinterpret_cast<const char*>(png), sizeof(png)));
        const auto utf = [](const fs::path& path) { return toUtf8(path.wstring()); };
        writeJsonWithLocalBackup(g_dataRoot / L"state" / L"library-scan.json", {{"games", json::array({
            {{"gameDirectory", utf(joinedRoot)}, {"primaryExecutable", utf(joinedExe)}, {"status", "ready"}, {"directoryName", "Joined"}},
            {{"gameDirectory", utf(nativeRoot)}, {"primaryExecutable", ""}, {"steamNative", true}, {"nativeSteamAppId", 123}, {"directoryName", "Native"}},
            {{"gameDirectory", utf(unjoinedRoot)}, {"primaryExecutable", utf(unjoinedExe)}, {"status", "ready"}, {"directoryName", "Not joined"}}
        })}}, false);
        writeJsonWithLocalBackup(workspacePlanPath(), {{"libraryState", utf(g_dataRoot / L"state" / L"library-scan.json")},
            {"summary", json::object()}, {"items", json::array({
                {{"gameDirectory", utf(joinedRoot)}, {"primaryExecutable", utf(joinedExe)}, {"status", "already-in-steam"}, {"formalName", "User game"}, {"steamStoreAppId", 987}},
                {{"gameDirectory", utf(fixture / L"Stale")}, {"primaryExecutable", utf(fixture / L"Stale" / L"Gone.exe")}, {"status", "already-in-steam"}}
            })}}, false);
        writeJsonWithLocalBackup(g_dataRoot / L"config" / L"manual-overrides.json", {{"items", {{toUtf8(normalizedPathKey(joinedExe)),
            {{"cover", {{"portableFile", utf(image)}}}}}}}}, false);
        std::vector<oc::Candidate> candidates;
        test("actual host selects joined/native current games and cached local cover", [&] {
            candidates = optiCatalogCandidates(); check(candidates.size() == 2, "Host included stale/unjoined record");
            const auto joined = std::find_if(candidates.begin(), candidates.end(), [&](const auto& row) { return row.executablePath == utf(joinedExe); });
            const auto native = std::find_if(candidates.begin(), candidates.end(), [](const auto& row) { return row.steamAppId == 123; });
            check(joined != candidates.end() && joined->steamAppId == 0 && joined->coverPath == utf(image), "Joined identity or artwork wrong");
            check(native != candidates.end() && native->executablePath.empty(), "Native folder-only game missing");
        });
        test("indexed joined ownership accepts current recorded directory alias", [&] {
            const auto path=g_dataRoot/L"state"/L"library-scan.json";
            auto state=readJson(path,json::object());const auto before=state;
            state["games"][0]["gameDirectory"]=utf(joinedExe.parent_path());
            state["games"][0]["alternateGameDirectories"]=json::array({utf(joinedRoot)});
            writeJsonWithLocalBackup(path,state,false);
            const auto found=optiCatalogCandidates([]{return false;},false);
            check(found.size()==2&&std::any_of(found.begin(),found.end(),[&](const auto& row){return row.installPath==utf(joinedRoot)&&row.executablePath==utf(joinedExe);}),"Current alias lost joined ownership");
            writeJsonWithLocalBackup(path,before,false);
        });
        test("current native plans supplement empty OPT without reviving stale entries",[&]{
            planNativeOptiCatalog(fixture/L"PlanNative");
        });
        test("explicit cover deletion prevents manifest fallback in lazy lookup", [&] {
            const auto path=g_dataRoot/L"config"/L"manual-overrides.json";
            const auto before=readJson(path,json::object());auto manual=before;
            manual["items"][toUtf8(normalizedPathKey(joinedExe))]["artworkProtection"]={{"cover","deleted"}};
            writeJsonWithLocalBackup(path,manual,false);
            OptiCatalogArtworkIndex index([]{return false;});
            check(index.coverFor({utf(joinedRoot),utf(joinedExe),"Game","",0}).empty(),"Deliberately deleted cover was restored");
            writeJsonWithLocalBackup(path,before,false);
        });
        test("lazy artwork retains exact current Steam grid fallback", [&] {
            const auto path=workspacePlanPath();const auto before=readJson(path,json::object());auto plan=before;
            const auto grid=fixture/L"SteamGrid";const auto cover=grid/L"123456p.png";writeHostBytesAtomic(cover,"local cover");
            plan["items"][0]["gridDirectory"]=utf(grid);plan["items"][0]["shortAppId"]="123456";
            const auto manual=g_dataRoot/L"config"/L"manual-overrides.json";const auto overrides=readJson(manual,json::object());
            writeJsonWithLocalBackup(manual,json::object(),false);writeJsonWithLocalBackup(path,plan,false);
            OptiCatalogArtworkIndex index([]{return false;});
            check(index.coverFor({utf(joinedRoot),utf(joinedExe),"Game","",0})==utf(cover),"Known Steam grid cover fallback missing");
            writeJsonWithLocalBackup(manual,overrides,false);writeJsonWithLocalBackup(path,before,false);
        });
        test("actual host copies cover into isolated OPT storage and backs up catalog", [&] {
            const auto root = fixture / L"OptiscalerClient";
            const std::string original = "[{\"Name\":\"OPT only\",\"FutureField\":7}]";
            writeHostBytesAtomic(root / L"games.json", original);
            const auto receipt = supplementOptiCatalog(root, candidates, [] { return false; });
            check(receipt["status"] == "synced" && receipt["added"] == 2 && receipt["covers"] == 1, "Filesystem supplement failed");
            const auto games = json::parse(readText(root / L"games.json"));
            check(games.size() == 3 && games[0]["FutureField"] == 7 && readText(root / L"games.json.ymcc.bak") == original, "Filesystem preservation failed");
            const auto found = std::find_if(games.begin(), games.end(), [](const auto& row) { return row.value("Platform", -1) == 8; });
            check(found != games.end(), "Manual item missing");
            const auto copied = fs::path(toWide(found->at("CoverImageUrl").template get<std::string>()));
            check(samePath(copied.parent_path(), root / L"Covers") && readText(copied) == readText(image), "Cover bytes/path mismatch");
            const auto before = readText(root / L"games.json");
            const auto count = std::distance(fs::directory_iterator(root / L"Covers"), fs::directory_iterator());
            check(supplementOptiCatalog(root, candidates, [] { return false; })["status"] == "idle", "Second host pass rewrote catalog");
            check(readText(root / L"games.json") == before && count == std::distance(fs::directory_iterator(root / L"Covers"), fs::directory_iterator()), "Second host pass copied cover");
        });
        test("actual host running-client guard leaves all filesystem metadata untouched", [&] {
            const auto root = fixture / L"RunningClient";
            writeHostBytesAtomic(root / L"games.json", "[]");
            const auto r = supplementOptiCatalog(root, candidates, [] { return true; });
            check(r["status"] == "deferred" && readText(root / L"games.json") == "[]" && !fs::exists(root / L"Covers") && !fs::exists(root / L"games.json.ymcc.bak"), "Running client mutated");
        });
        test("unregistered old UI timer cannot start OPT work", [] {
            check(oc::startupDelayMs == 30000 && oc::itemDelayMs == 1500 && oc::workBudgetMs == 240000 && ymcc::optiStartup::workerBudgetMs == 300000, "Startup/pacing contract changed");
            g_updateHealthOnly = true;
            windowProcedure(nullptr, WM_TIMER, 0x54, 0);
            g_updateHealthOnly = false; g_shuttingDown = true;
            windowProcedure(nullptr, WM_TIMER, 0x54, 0);
            g_shuttingDown = false;
            check(g_hostThreads.empty(), "Guarded timer started background work");
        });
        test("first cover copy error aborts before the next item and catalog commit", [] {
            Memory m;auto p=m.ports();unsigned attempted=0;
            p.copyCover=[&](const oc::Candidate&)->std::string{++attempted;throw std::runtime_error("fixture cover error");};
            auto next=manual();next.installPath="E:\\Games\\Other";next.executablePath=next.installPath+"\\Other.exe";
            throws([&]{oc::supplement({manual(),next},p);});
            check(attempted==1&&!m.writes&&!m.backups&&m.bytes=="[]"&&m.steps==1,"Error continued/retried or wrote catalog");
        });
        test("real cooperative deadline commits prefix and receipt resumes the next normal pass", [&] {
            const auto root=fixture/L"BoundedClient";writeHostBytesAtomic(root/L"games.json","[]");
            std::vector<oc::Candidate> rows;json inventory=json::array();
            for(int i=0;i<3;++i){const auto directory=fixture/toWide("BoundedGame"+std::to_string(i));fs::create_directories(directory);
                rows.push_back({utf(directory),"","Bounded"+std::to_string(i),utf(image),200+i});
                inventory.push_back({{"gameDirectory",utf(directory)},{"primaryExecutable",""},{"steamNative",true},{"nativeSteamAppId",200+i},{"directoryName",rows.back().name}});
            }
            oc::WorkBudget budget([]{return false;},1900);
            auto receipt=supplementOptiCatalog(root,rows,[]{return false;},[]{return false;},[]{return true;},&budget);
            auto stored=json::parse(readText(root/L"games.json"));
            check(receipt["status"]=="synced"&&receipt["workLimited"]==true&&receipt["added"]==1&&stored.size()==1&&stored[0]["AppId"]=="200"&&receipt["nextCandidate"]==oc::candidateKey(rows[1]),"Real deadline failed completed-prefix commit");
            check(optiCatalogPreparationExitCode(receipt)==21,"Explicit open reported a bounded prefix as fully prepared");
            const auto oldInventory=readJson(g_dataRoot/L"state"/L"library-scan.json",json::object());
            writeJsonWithLocalBackup(g_dataRoot/L"state"/L"library-scan.json",{{"games",inventory}},false);
            receipt["clientRoot"]=toUtf8(normalizedPathKey(root));
            writeJsonWithLocalBackup(g_dataRoot/L"state"/L"last-opti-client-supplement.json",receipt,false);
            const auto skipped=runOptiCatalogSupplement([]{return false;},[]{return true;},"fixture-client-open",[&]{return root;},[]{return true;});
            check(skipped["status"]=="deferred"&&skipped["reason"]=="client-running"&&skipped["workLimited"]==true&&skipped["nextCandidate"]==receipt["nextCandidate"],"Skipped startup erased the continuation cursor");
            check(optiCatalogPreparationExitCode(skipped)==20,"Resumable cursor masked the live client result");
            const auto next=runOptiCatalogSupplement([]{return false;},[]{return true;},"fixture-next-normal-start",[&]{return root;},[]{return false;});
            stored=json::parse(readText(root/L"games.json"));
            check(next["status"]=="synced"&&next["resumed"]==true&&next["added"]==2&&next["workLimited"]==false&&stored.size()==3&&stored[1]["AppId"]=="201"&&stored[2]["AppId"]=="202","Actual receipt failed cursor continuation or duplicated prefix");
            check(optiCatalogPreparationExitCode(next)==0,"Completed supplement no longer reported prepared");
            const auto noWork=fixture/L"UnstartedClient";
            oc::WorkBudget noTime([]{return false;},oc::itemDelayMs-1);
            const auto unstarted=supplementOptiCatalog(noWork,rows,[]{return false;},[]{return false;},[]{return true;},&noTime);
            check(unstarted["status"]=="idle"&&unstarted["workLimited"]==true&&!fs::exists(noWork/L"games.json")&&
                optiCatalogPreparationExitCode(unstarted)==21,"Explicit open reported a timed-out empty pass as prepared");
            check(optiCatalogPreparationExitCode(json{{"status","deferred"},{"reason","library-scan-unavailable"}})==22&&
                optiCatalogPreparationExitCode(json{{"status","idle"}})==0,"Missing or empty inventory result changed");
            writeJsonWithLocalBackup(g_dataRoot/L"state"/L"library-scan.json",oldInventory,false);
        });
        writeJsonWithLocalBackup(fixture / L"summary.json", {{"status", "PASS"}, {"cases", passed},
            {"realHostConsumer", true}, {"isolatedStorageOnly", true}, {"physicalAcceptance", false}}, false);
        std::cout << "PASS " << passed << " cases; isolated production host/catalog consumers\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << passed << " cases: " << error.what() << '\n';
        return 1;
    }
}
