#include <functional>
#include "custom_steam_library_storage_isolation.h"
#define wWinMain hostMainUnusedInSelftest
#include "../native/custom-steam-library/workspace_host.cpp"
#undef wWinMain
#include <iostream>
static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void writeFixtureJson(const fs::path& p, const json& value){
    // Fixture-owned roots need the same existing readiness record as a real
    // initialized library. Keep the actual Host admission check intact.
    if(!g_dataRoot.empty()&&!fs::exists(g_dataRoot/L"data-root.json")){
        fs::create_directories(g_dataRoot);
        std::ofstream record(g_dataRoot/L"data-root.json",std::ios::binary);
        record<<json{{"schemaVersion",2},{"dataRoot",toUtf8(g_dataRoot.wstring())},{"createdAt",uint64_t{1}}}.dump();record.close();
        if(!record.good())throw std::runtime_error("Owned readiness fixture initialization failed");
    }
    writeJsonWithLocalBackup(p,value,false);
}
static void fixtureFile(const fs::path& p){fs::create_directories(p.parent_path());std::ofstream stream(p,std::ios::binary);stream<<"fixture; local only";}

static void verifyLaunchScriptSync(const fs::path& game,const fs::path& exe,const json& accounts,std::string_view variant) {
    namespace launch = custom_steam_library::launch_file;
    const auto script=launch::wrapperPath(game), bat=game/L"Start.bat";
    const auto vdf=fs::path(toWide(jsonStringOr(accounts.at("accounts").at(0),"shortcutsVdf")));
    const auto beforeVdf=readText(vdf);
    if(variant!="bad-bat")fixtureFile(bat);
    const auto args=[&](const json& configuration){return json{{"gameDirectory",toUtf8(exe.parent_path().wstring())},
        {"executable",toUtf8(exe.wstring())},{"configuration",configuration}};};
    const auto action=[&](const char* command,const json& cfg){return workspaceLaunchFileActionWithAccounts(command,args(cfg),accounts);};
    const json previous={{"mode","auto"},{"source",""},{"workingDirectory",""},{"command",""}};
    auto next=previous;next["source"]=toUtf8(bat.wstring());
    const auto path=g_dataRoot/L"config"/L"manual-overrides.json";
    if(variant=="legacy-auto-alias") {
        const auto alias=toUtf8((exe.parent_path()/L"."/exe.filename()).wstring());
        const auto cover=game/L"manual-cover.png";fixtureFile(cover);
        writeFixtureJson(path,{{"schemaVersion",1},{"items",{{alias,{{"launchConfiguration",previous},{"name","keep legacy metadata"},
            {"cover",{{"file",toUtf8(cover.wstring())}}},{"artworkProtection",{{"wallpaper","deleted"}}}}}}}});
        require(workspaceLaunchFileActionWithAccounts("launchFileState",args(previous),accounts).at("configuration")==previous,
            "Host did not read the legacy launch configuration before saving");
        require(workspaceSnapshot(false).at("launchConfigurations").at(toUtf8(exe.wstring()))==previous,
            "Workspace snapshot did not project the effective legacy launch mode");
    } else if(variant!="legacy-unknown")require(action("saveLaunchConfiguration",previous).at("scriptSync")=="missing","Cannot seed prior auto config");
    require(action("createLaunchFile",previous).at("created")==true,"Cannot seed original script");
    if(variant=="marked-hand-edit" || variant=="confirmed-update") {
        auto edited=readText(script);
        // Keep the generator header, append a handwritten UTF-16LE line.
        edited.append("'\0 \0e\0d\0i\0t\0\r\0\n\0",16);
        writeHostBytesAtomic(script,edited);
    } else if(variant=="handwritten") {
        writeHostBytesAtomic(script,"WScript.Quit 7\r\n");
    }
    const auto original=readText(script), beforeConfig=readText(path);
    if(variant=="bad-bat") {
        bool refused=false;
        try { action("saveLaunchConfiguration",next); } catch(const std::runtime_error&) { refused=true; }
        require(refused&&readText(script)==original&&readText(path)==beforeConfig,"Invalid BAT changed script or configuration");
    } else if(variant=="ordinary-keeps-vbs") {
        require(workspaceLaunchFileAction("saveLaunchConfiguration",args({{"mode","direct"}})).at("saved")==true,
            "Ordinary save failed without Steam account evidence");
        require(readText(script)==original,"Ordinary save changed advanced script");
    } else {
        const auto initial=action("saveLaunchConfiguration",next);
        require(initial.at("saved")==true,"Launch settings did not save");
        launch::Configuration expected;
        expected.gameRoot=game;expected.primaryExecutable=exe;expected.source=bat;
        const auto bytes=launch::scriptBytes(expected);
        const std::string expectedBat(bytes.begin(),bytes.end());
        if(variant=="auto-switch-bat" || variant=="legacy-auto-alias") {
            require(readText(script)==expectedBat && initial.at("scriptSync")=="updated","Untouched auto script was not synchronized to BAT");
            require(action("saveLaunchConfiguration",next).at("scriptSync")=="current","Equal generated script was not kept as current");
            if(variant=="legacy-auto-alias") {
                const auto alias=toUtf8((exe.parent_path()/L"."/exe.filename()).wstring());
                const auto document=readJson(path,json::object());
                require(document.at("items").at(alias).at("name")=="keep legacy metadata"&&
                    document.at("items").at(alias).at("launchConfiguration")==previous,
                    "Save overwrote the historical alias instead of superseding it");
                const auto& canonical=document.at("items").at(toUtf8(normalizedPathKey(exe)));
                require(canonical.at("name")==document.at("items").at(alias).at("name")&&
                    canonical.at("cover")==document.at("items").at(alias).at("cover")&&
                    canonical.at("artworkProtection")==document.at("items").at(alias).at("artworkProtection"),
                    "Canonical launch write hid the legacy title or artwork");
                require(workspaceSnapshot(false).at("launchConfigurations").at(toUtf8(exe.wstring()))==next,
                    "Workspace snapshot still projected the stale alias");
            }
        } else {
            require(readText(script)==original,"Automatic save overwrote an edited or unproven script");
            require(initial.at("scriptSync")=="preserved","Protected script was reported as applied");
            require(action("saveLaunchConfiguration",next).at("scriptSync")=="preserved","Repeated save overwrote protected script");
            if(variant=="confirmed-update") {
                const auto explicitWrite=action("updateLaunchFile",next);
                const auto backup=fs::path(toWide(jsonStringOr(explicitWrite,"backup")));
                require(explicitWrite.at("updated")==true&&readText(backup)==original&&readText(script)==expectedBat,
                    "Explicit update failed to back up and replace edited script");
                require(action("saveLaunchConfiguration",next).at("scriptSync")=="current","Explicit update did not reconcile auto settings");
            }
        }
    }
    require(readText(vdf)==beforeVdf&&!fs::exists(launch::wrapperPath(exe.parent_path())),"Save rewrote the card or created a second script");
}

static void verifyLaunchScriptPath(const fs::path& root, std::string_view variant, bool sync=false) {
    namespace launch = custom_steam_library::launch_file;
    g_dataRoot = root/L"data";
    const bool exactLegacyConflict = variant == "legacy-exact-owner-conflict";
    const auto game = root/L"Game", directory = exactLegacyConflict ? game : game/L"bin", primary = game/L"bin"/L"Game.exe";
    const auto original = launch::wrapperPath(game), fallback = launch::wrapperPath(directory);
    const auto steam = root/L"Steam";
    const bool recovering = variant == "empty-wrapper" || variant == "directory-wrapper" ||
        variant == "oversized-wrapper" || variant == "locked-empty-wrapper";
    fixtureFile(primary);
    if (!sync && !recovering && variant != "missing-original") fixtureFile(original);
    const auto pathText = [](const fs::path& path) { return toUtf8(path.wstring()); };
    json inventory{{"schemaVersion",1},{"games",json::array({{{"gameDirectory",pathText(directory)},
        {"primaryExecutable",pathText(primary)},{"status","ready"}}})}};
    const auto writeInventory = [&] { writeFixtureJson(g_dataRoot/L"state"/L"library-scan.json",inventory); };
    if (variant == "ordinary-save") {
        writeInventory();
        const auto result = workspaceLaunchFileAction("saveLaunchConfiguration", {{"gameDirectory",pathText(directory)},
            {"executable",pathText(primary)},{"configuration",{{"mode","direct"}}}});
        require(jsonBoolOr(result,"saved",false) && !fs::exists(fallback),"Ordinary save activated or created VBS");
        return;
    }
    json accounts{{"steamStatus", "ready"},{"accounts",json::array()}};
    const auto addCard = [&](const std::string& account, const fs::path& script, bool legacy, const std::string& boundAccount,
                             const fs::path& boundPrimary) {
        const auto vdf = steam/L"userdata"/toWide(account)/L"config"/L"shortcuts.vdf";
        fs::create_directories(vdf.parent_path());
        constexpr uint32_t id=3221225473u;
        auto start=script.parent_path();
        if (!legacy) {
            const json binding{{"schemaVersion",1},{"accountId",boundAccount},{"shortcutAppId",id},
                {"shortcutsVdf",pathText(vdf)},{"script",pathText(script)},{"primaryExecutable",pathText(boundPrimary)}};
            start=launch::bindingRoot(g_dataRoot)/toWide(custom_steam_library::plan_receipt::digest(binding));
            fs::create_directories(start);
            std::ofstream bindingFile(start/L"binding.json",std::ios::binary);
            bindingFile<<binding.dump();
        }
        std::ofstream file(vdf,std::ios::binary);
        const auto text = [&](const std::string& value) { file.write(value.data(),value.size());file.put('\0'); };
        const auto field = [&](const std::string& key,const std::string& value) { file.put('\1');text(key);text(value); };
        file.put('\0');text("shortcuts");file.put('\0');text("0");
        file.put('\2');text("appid");for (unsigned i=0;i<4;++i) file.put(static_cast<char>(id>>(8*i)));
        field("exe","\""+pathText(launch::systemFile(L"wscript.exe"))+"\"");
        field("startdir","\""+pathText(start)+"\"");field("LaunchOptions",toUtf8(launch::steamOptions(script)));
        for (int i=0;i<3;++i) file.put('\10');
        file.close();
        require(file.good(),"Fixture VDF could not be written");
        accounts["accounts"].push_back({{"accountId",account},{"steamRoot",pathText(steam)},{"shortcutsVdf",pathText(vdf)}});
    };
    const bool legacy=exactLegacyConflict || variant=="legacy-alias" || variant=="legacy-contested" || variant=="legacy-recovered";
    if ((legacy && !exactLegacyConflict) || variant=="wrong-account") inventory["games"][0]["alternateGameDirectories"]=json::array({pathText(game)});
    if (variant=="legacy-contested") inventory["games"].push_back({{"gameDirectory",pathText(game)},{"primaryExecutable",nullptr}});
    if (exactLegacyConflict) inventory["games"].push_back({{"gameDirectory",pathText(game)},{"primaryExecutable",nullptr}});
    if (variant=="legacy-recovered") inventory["libraryScanRecovery"]={{"schemaVersion",1},{"requiresFreshScan",true}};
    if (variant!="no-account") addCard("19627",original,legacy,variant=="wrong-account" ? "22202" : "19627",primary);
    else accounts["steamStatus"]="not-installed";
    if (variant=="multiple-paths") addCard("22202",fallback,false,"22202",primary);
    if (variant=="shared-with-other-game") addCard("22202",original,false,"22202",game/L"Other.exe");
    if (variant=="same-path-multiple-accounts") addCard("22202",original,false,"22202",primary);
    writeInventory();
    if (exactLegacyConflict) {
        const auto before = readText(original);
        const auto vdf = fs::path(toWide(jsonStringOr(accounts["accounts"][0], "shortcutsVdf")));
        const auto originalVdf = readText(vdf);
        const json args = {{"gameDirectory",pathText(game)},{"executable",pathText(primary)},
            {"configuration",{{"mode","auto"}}}};
        bool refused = false;
        try { workspaceLaunchFileActionWithAccounts("updateLaunchFile", args, accounts); }
        catch (const std::runtime_error&) { refused = true; }
        require(refused && readText(original) == before && readText(vdf) == originalVdf,
            "Editor rewrote a legacy script without a unique owner");
        inventory["games"].erase(1);
        writeInventory();
        require(workspaceLaunchFileActionWithAccounts("launchFileState",args,accounts)["script"] == pathText(original),
            "Resolving the inventory conflict did not recover the original script");
        return;
    }
    if(recovering) {
        const auto vdf = fs::path(toWide(jsonStringOr(accounts["accounts"][0],"shortcutsVdf")));
        const auto before = readText(vdf);
        const auto args = json{{"gameDirectory",pathText(directory)},{"executable",pathText(primary)},
            {"configuration",{{"mode","auto"}}}};
        const auto action = [&](const char* cmd) { return workspaceLaunchFileActionWithAccounts(cmd,args,accounts); };
        if(variant == "directory-wrapper") fs::create_directory(original);
        else writeHostBytesAtomic(original, {});
        if(variant == "oversized-wrapper") fs::resize_file(original, 4u*1024*1024+1);
        require(action("launchFileState").at("script") == pathText(original), "Recovery must target the original card's wrapper");
        if(variant == "empty-wrapper") {
            const auto state = action("launchFileState");
            require(state.at("fileState") == "empty", "Empty script was not offered for repair");
            require(action("saveLaunchConfiguration").at("scriptSync") == "preserved" && fs::file_size(original) == 0,
                "Saving auto must not replace an empty file without an explicit update");
            const auto repaired = action("updateLaunchFile");
            const auto backup = fs::path(toWide(jsonStringOr(repaired,"backup")));
            const auto expected = launch::scriptBytes({game,primary});
            require(repaired.at("updated") == true && repaired.at("fileState") == "present" &&
                fs::is_regular_file(backup) && fs::file_size(backup) == 0 &&
                readText(original) == std::string(expected.begin(),expected.end()), "Explicit repair did not preserve and replace the empty file");
            require(action("updateLaunchFile").at("unchanged") == true, "Repeated update should not rewrite the repaired file");
        } else {
            const bool locked = variant == "locked-empty-wrapper";
            HostIoHandle lock{locked ? CreateFileW(original.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr) : INVALID_HANDLE_VALUE};
            require(!locked || lock.value != INVALID_HANDLE_VALUE,"Could not hold the fixture lock");
            bool rejected=false;
            try { action("updateLaunchFile"); } catch(const std::runtime_error&) { rejected=true; }
            require(rejected,"Unverifiable wrapper was overwritten");
            if(variant == "directory-wrapper") require(fs::is_directory(original),"Directory replaced by script");
            if(variant == "oversized-wrapper") require(fs::file_size(original) == 4u*1024*1024+1,"Oversized file overwritten");
        }
        require(readText(vdf) == before && !fs::exists(fallback),"Repair rewrote the Steam card or generated a second wrapper");
        return;
    }
    if(sync){verifyLaunchScriptSync(game,primary,accounts,variant);return;}
    json scan;
    auto owner=workspaceLaunchOwner({{"gameDirectory",pathText(directory)},{"executable",pathText(primary)}},&scan);
    const bool mustRefuse = variant=="wrong-account" || variant=="legacy-contested" || variant=="legacy-recovered" ||
        variant=="multiple-paths" || variant=="shared-with-other-game";
    fs::path resolved;
    try { resolved=resolveWorkspaceLaunchScript(owner,scan,accounts); }
    catch (...) { if (mustRefuse) return; throw; }
    require(!mustRefuse,"Conflicting file ownership was accepted");
    require(launch::samePath(resolved, variant=="no-account" ? fallback : original),"Editor did not resolve the expected script path");
    require(!fs::exists(fallback),"Lookup itself generated a replacement script");
    if (variant=="missing-original") require(launch::mayCreateWrapper(resolved),"Explicit creation cannot restore the missing original");
    if (variant=="managed-reverse") {
        owner.gameRoot=resolved.parent_path();
        owner.manual=true;
        owner.command=L"@echo off\r\nrem fixture command, not executed";
        const auto bytes=launch::scriptBytes(owner);
        const auto written=writeWorkspaceLaunchScript(resolved,{reinterpret_cast<const char*>(bytes.data()),bytes.size()},true);
        require(written.changed && readText(written.backup,256)=="fixture; local only" && !fs::exists(fallback),
            "Updating the resolved script did not update and back up the original in place");
    }
}

static void verifyLaunchDeleteAdmission(const fs::path& root, std::string_view variant) {
    const auto script=root/L"Game"/L"StartGame.vbs";
    const auto steam=root/L"Steam", account=steam/L"userdata"/L"19627", vdf=account/L"config"/L"shortcuts.vdf";
    fixtureFile(script);
    json accounts={{"steamStatus","ready"},{"accounts",json::array({{{"accountId","19627"},
        {"steamRoot",toUtf8(steam.native())},{"shortcutsVdf",toUtf8(vdf.native())}}})}};
    if(variant=="no-installation"||variant=="no-account"||variant=="unknown") {
        accounts["steamStatus"]=variant=="no-installation"?"not-installed":variant=="no-account"?"installed-no-account":"unknown";
        accounts["steamInstalled"]=variant=="no-account";
        accounts["accounts"]=json::array();
        if(variant=="no-account") {
            fs::create_directories(steam);
            accounts["steamPath"]=toUtf8(steam.native());
        }
    } else if(variant!="missing-account-directory") {
        fs::create_directories(variant=="missing-config"?account:vdf.parent_path());
        if(variant=="unreadable-shortcuts")fs::create_directory(vdf);
        if(variant=="partial")accounts["partialAccounts"]=true;
        if(variant=="referenced") {
            std::string bytes;
            const auto field=[&](char type,std::string_view key){bytes.push_back(type);bytes.append(key);bytes.push_back(0);};
            field(0,"shortcuts");field(0,"0");field(1,"Exe");
            bytes+=toUtf8(workspaceLaunch::systemFile(L"wscript.exe").native()); bytes.push_back(0);
            field(1,"LaunchOptions");bytes+=toUtf8(workspaceLaunch::steamOptions(script)); bytes.push_back(0);
            bytes.append(3,8);
            writeHostBytes(vdf,bytes);
        }
    }
    const bool shouldPass=variant=="no-installation"||variant=="no-account"||variant=="missing-config"||variant=="missing-vdf";
    bool accepted=false;
    try {requireUnreferencedWorkspaceLaunchFile(accounts,script);accepted=true;} catch(const std::runtime_error&) {}
    require(accepted==shouldPass,shouldPass?"Confirmed empty Steam inventory still blocked deletion":"Unknown Steam state or referenced script was admitted");
    require(readText(script)=="fixture; local only","Delete admission check changed the script");
}

static void verifyJoinedArtworkPreview(const fs::path& root, std::string_view scenario) {
    g_dataRoot = root / L"data";
    const auto overrides = g_dataRoot / L"config" / L"manual-overrides.json";
    writeFixtureJson(overrides, {{"items", json::object()}});
    const auto grid = root / L"Steam" / L"userdata" / L"22202" / L"config" / L"grid";
    const auto source = grid / L"4000000000p.png";
    const auto exe = toUtf8((root / L"Game" / L"Game.exe").wstring());
    fixtureFile(source);
    json item = {{"status", "already-in-steam"}, {"primaryExecutable", exe}, {"accountId", "22202"},
        {"shortAppId", "4000000000"}, {"gridDirectory", toUtf8(grid.wstring())}};
    if (scenario == "readonly-original")
        require(SetFileAttributesW(source.c_str(), FILE_ATTRIBUTE_READONLY), "Could not set read-only source");
    const auto project = [&] {
        json plan = {{"items", json::array({item})}};
        attachWorkspaceArtwork(plan, json::object());
        return jsonObjectOr(plan["items"][0], "artworkPreview");
    };
    const auto initial = project();
    require(initial.contains("tall"), "Initial grid cover unavailable");
    const auto before = initial["tall"];
    const auto cached = fs::path(toWide(jsonStringOr(before, "portableFile")));
    if (scenario == "unchanged-and-locked-source") {
        const auto stamp = fs::last_write_time(cached);
        require(project()["tall"] == before, "Unchanged artwork acquired a different preview identity");
        const auto handle = CreateFileW(source.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        require(handle != INVALID_HANDLE_VALUE, "Could not lock test cover");
        json during;
        try { during = project(); } catch (...) { CloseHandle(handle); throw; }
        CloseHandle(handle);
        require(during.contains("tall") && during["tall"] == before && fs::last_write_time(cached) == stamp,
            "A read-locked but unchanged source did not retain its own projection");
    } else if (scenario == "same-path-replacement" || scenario == "readonly-original") {
        const auto stamp = fs::last_write_time(source);
        const auto stage = source.parent_path() / L"replacement.png";
        auto bytes = readText(source);
        bytes[0] = 'F';
        { std::ofstream out(stage, std::ios::binary); out << bytes; }
        fs::last_write_time(stage, stamp);
        if (scenario == "readonly-original")
            require(SetFileAttributesW(source.c_str(), FILE_ATTRIBUTE_NORMAL), "Could not replace read-only original");
        require(MoveFileExW(stage.c_str(), source.c_str(), MOVEFILE_REPLACE_EXISTING), "Could not replace test cover");
        const auto after = jsonObjectOr(project(), "tall");
        require(!jsonStringOr(after, "previewRevision").empty() &&
            jsonStringOr(after, "previewRevision") != jsonStringOr(before, "previewRevision"),
            "Same path, size and mtime replacement still has the old browser identity");
        require(readText(fs::path(toWide(jsonStringOr(after, "portableFile")))) == bytes, "Projection kept old bytes");
    } else if (scenario == "removed-source") {
        fs::remove(source);
        require(!project().contains("tall"), "Removed cover resurfaced from a disposable cache");
    } else if (scenario == "different-grid-root") {
        item["gridDirectory"] = toUtf8((root / L"OtherSteam" / L"userdata" / L"22202" / L"config" / L"grid").wstring());
        require(!project().contains("tall"), "Another installation inherited stale cover bytes");
    } else if (scenario == "account-switch") {
        item["accountId"] = "55555";
        item["gridDirectory"] = toUtf8((root / L"Steam" / L"userdata" / L"55555" / L"config" / L"grid").wstring());
        require(!project().contains("tall"), "Another account inherited stale cover bytes");
    } else if (scenario == "full-id-horizontal") {
        const auto fullId = std::to_wstring((uint64_t{4000000000} << 32) | 0x02000000ull);
        const auto full = grid / (fullId + L".jpg");
        fixtureFile(full);
        auto after = jsonObjectOr(project(), "long");
        require(jsonStringOr(after, "file") == toUtf8(full.wstring()), "Existing desktop horizontal image was not visible");
        const auto shortPath = grid / L"4000000000.png";
        fixtureFile(shortPath);
        after = jsonObjectOr(project(), "long");
        require(jsonStringOr(after, "file") == toUtf8(shortPath.wstring()), "Full-ID preview displaced the short-ID image");
    } else if (scenario == "deleted-slot") {
        writeFixtureJson(overrides, {{"items", {{exe, {{"artworkProtection", {{"cover", "deleted"}}}}}}}});
        require(!project().contains("tall"), "Deleted cover resurfaced from grid/cache");
    } else if (scenario == "manual-slot") {
        const auto manual = g_dataRoot / L"artwork" / L"Override.png";
        fixtureFile(manual);
        writeFixtureJson(overrides, {{"items", {{exe, {{"cover", {{"file", toUtf8(manual.wstring())}}}}}}}});
        const auto selected = jsonObjectOr(project(), "tall");
        require(jsonStringOr(selected, "source") == "manual", "Grid displaced a valid manual cover");
    } else if (scenario == "canonical-manual-slot") {
        const auto executable=fs::path(toWide(exe));
        const auto key=toUtf8(normalizedPathKey(executable));
        require(exe<key,"Alias must sort before the canonical record");
        const auto stale=g_dataRoot/L"artwork"/L"Stale.png", saved=g_dataRoot/L"artwork"/L"Saved.png";
        fixtureFile(stale);fixtureFile(saved);
        json items={{exe,{{"name","legacy name"},{"steamId",1091500},{"cover",{{"file",toUtf8(stale.wstring())}}},
            {"artworkProtection",{{"wallpaper","deleted"}}},{"launchConfiguration",{{"mode","direct"}}}}}};
        writeFixtureJson(overrides,{{"items",items}});
        require(jsonStringOr(jsonObjectOr(project(),"tall"),"portableFile")==toUtf8(stale.wstring()),"Legacy-only artwork was not projected");
        fixtureFile(executable);
        const auto receipt=executeWorkspaceCommand("saveArtworkOverride",{{"executable",exe},{"clearCover",true},{"deferSnapshot",true}});
        require(receipt.value("saved",false),"Explicit save did not accept the legacy-only record");
        auto persisted=readJson(overrides,json::object()).at("items");
        const auto& edit=persisted.at(key);
        auto launches=custom_steam_library::launch_file::indexSavedLaunchConfigurations(json{{"items",persisted}},toWide);
        require(persisted.at(exe)==items.at(exe)&&edit.at("name")==items.at(exe).at("name")&&
            !edit.contains("launchConfiguration")&&launches.at(toWide(key)).value==items.at(exe).at("launchConfiguration")&&edit.at("artworkProtection").at("wallpaper")=="deleted"&&
            edit.at("artworkProtection").at("cover")=="deleted"&&!project().contains("tall"),"First artwork save lost legacy siblings or resurfaced the deleted cover");
        items[key]={{"cover",{{"file",toUtf8(saved.wstring())}}}};
        writeFixtureJson(overrides,{{"items",items}});
        auto selected=jsonObjectOr(project(),"tall");
        require(jsonStringOr(selected,"portableFile")==toUtf8(saved.wstring()),"Stale alias displaced the saved cover in the preview");
        require(manualArtworkOverrideForExecutable({{"items",items}},exe)==items[key],"Single-record lookup still preferred the legacy alias");
        const auto optiCover=[&]{
            OptiCatalogArtworkIndex index([]{return false;});
            return index.coverFor({toUtf8(executable.parent_path().wstring()),exe,"Game","",0});
        };
        require(optiCover()==toUtf8(saved.wstring()),"OPT projection ignored the saved cover");
        items[key]={{"artworkProtection",{{"cover","deleted"}}}};
        writeFixtureJson(overrides,{{"items",items}});
        require(!project().contains("tall"),"Deleted cover resurfaced from the legacy alias or Steam grid");
        require(optiCover().empty(),"OPT projection resurrected deleted cover from the legacy alias");
        items[key]={{"name","Current name"}};
        writeFixtureJson(overrides,{{"items",items}});
        require(jsonStringOr(jsonObjectOr(project(),"tall"),"portableFile")==toUtf8(stale.wstring())&&optiCover()==toUtf8(stale.wstring()),
            "Metadata-only canonical record hid the historical cover");
        const auto split=readArtworkOverrides().at("items").at(key);
        require(split.at("name")=="Current name"&&split.at("artworkProtection").at("wallpaper")=="deleted"&&
            !split.contains("steamId")&&!split.contains("launchConfiguration")&&readJson(overrides,json::object()).at("items")==items,
            "Static projection changed stored data, identity, or launch settings");
    } else if (scenario == "opti-uses-current-grid-only") {
        item["gameDirectory"] = toUtf8((root / L"Game").wstring());
        item["artworkPreview"] = initial;
        writeFixtureJson(workspacePlanPath(), {{"libraryState", toUtf8((g_dataRoot / L"state" / L"library-scan.json").wstring())},
            {"summary", json::object()}, {"items", json::array({item})}});
        const auto cover = [&] {
            OptiCatalogArtworkIndex index([] { return false; });
            return index.coverFor({jsonStringOr(item, "gameDirectory"), exe, "Game", "", 0});
        };
        require(cover() == toUtf8(source.wstring()), "OPT chose an old preview over the current original");
        fs::remove(source);
        require(cover().empty(), "OPT used a saved preview after the source was removed");
    }
}

static void verifyProspectiveLaunchOwner(const fs::path& root, bool unresolved) {
    namespace launch = custom_steam_library::launch_file;
    g_dataRoot = root/L"data";
    const auto game = root/L"Game", exe = game/L"Game.exe", other = game/L"Other.exe";
    fixtureFile(exe);
    fixtureFile(other);
    const auto text = [](const fs::path& p){return toUtf8(p.wstring());};
    json inventory{{"games",json::array({{{"gameDirectory",text(game)},{"primaryExecutable",text(exe)},{"status","ready"}},
        {{"gameDirectory",text(game)},{"primaryExecutable",unresolved ? "" : text(other)},{"status",unresolved ? "needs-primary" : "ready"}}})}};
    const auto writeInventory = [&]{writeFixtureJson(g_dataRoot/L"state"/L"library-scan.json",inventory);};
    writeInventory();
    const auto steam = root/L"Steam", vdf = steam/L"userdata"/L"22202"/L"config"/L"shortcuts.vdf";
    if (unresolved) fs::create_directories(vdf.parent_path());
    const json accounts = unresolved ? json{{"steamStatus","ready"},{"accounts",json::array({
        {{"accountId","22202"},{"steamRoot",text(steam)},{"shortcutsVdf",text(vdf)}}})}} :
        json{{"steamStatus","not-installed"},{"steamInstalled",false},{"accounts",json::array()}};
    json args{{"gameDirectory",text(game)},{"executable",text(exe)},{"configuration",{{"mode","direct"}}}};
    const auto action = [&](const char* cmd){return workspaceLaunchFileActionWithAccounts(cmd,args,accounts);};
    require(action("saveLaunchConfiguration").at("saved")==true,"Ordinary launch was blocked by a VBS owner conflict");
    args["configuration"]["mode"]="auto";
    const auto script = launch::wrapperPath(game);
    bool rejected = false;
    try {action("createLaunchFile");} catch(const std::runtime_error&){rejected=true;}
    require(rejected&&!fs::exists(script)&&!fs::exists(vdf),"Host created an unowned wrapper before any Steam card existed");
    fixtureFile(script);
    const auto original = readText(script);
    rejected = false;
    try {action("updateLaunchFile");} catch(const std::runtime_error&){rejected=true;}
    require(rejected&&readText(script)==original,"Host updated the pre-existing ambiguous wrapper");
    fs::remove(script);
    inventory["games"].erase(1);
    writeInventory();
    require(action("createLaunchFile").at("created")==true&&fs::is_regular_file(script)&&!fs::exists(vdf),
        "Unique owner could not create the original wrapper on retry");
}

static void verifyPreviewReadiness(const fs::path& root, std::string_view mode) {
    g_dataRoot=root/L"data";
    const bool longOnly=mode=="long-cached"||mode=="long-identity";
    const auto* previewType=longOnly?"long":"tall";
    const auto* manualSlot=longOnly?"long":"cover";
    const auto exe=root/L"Game"/L"Game.exe", original=g_dataRoot/L"artwork"/L"chosen.png";
    const auto artDir=g_dataRoot/L"artwork"/L"fixture";
    fixtureFile(exe);
    json artwork=json::array();
    for(const auto* type:{"tall","long","hero"}) {
        const auto path=artDir/toWide(std::string(type)+".png");fixtureFile(path);
        json row={{"type",type},{"ok",true},{"portableFile",toUtf8(path.wstring())},{"provider","steam"}};
        if(std::string_view(type)==previewType)row["previewOnlyManualFallback"]=true;
        artwork.push_back(row);
    }
    const auto exeText=toUtf8(exe.wstring()), key=toUtf8(normalizedPathKey(exe));
    const json manual={{manualSlot,{{"file",toUtf8(original.wstring())}}},{"artworkProtection",{{manualSlot,"manual"}}}};
    const auto overrides=mode=="cleared"?json{{"items",json::object()}}:json{{"items",{{key,manual}}}};
    const json identity={{"appId",4012810},{"steamVerificationStatus","steam-verified"},{"identityStatus","verified"},
        {"primaryProvider","steam"},{"resolverVersion","steam-official-fuzzy-global-v4"}};
    const json game={{"primaryExecutable",exeText},{"gameDirectory",toUtf8(exe.parent_path().wstring())},
        {"directoryName","Fixture Game"},{"status","ready"}};
    writeFixtureJson(g_dataRoot/L"config"/L"manual-overrides.json",overrides);
    writeFixtureJson(artDir/L"manifest.json",{{"exe",exeText},{"match",identity},{"artwork",artwork}});
    json snapshot={{"library",{{"games",json::array({game})}}},{"manualOverrides",overrides},{"resolvedIdentities",{{key,identity}}}};
    if(mode=="restored")fixtureFile(original);
    snapshot["steamPlan"]=buildLocalSteamPlanSnapshot(snapshot);
    auto& plan=snapshot["steamPlan"];
    auto& row=plan["items"][0];
    if(mode=="identity"||mode=="long-identity") {
        row["status"]="needs-steam-verification";row.erase("steamStoreAppId");row.erase("storefrontAppId");
        plan["summary"]={{"needsIdentity",1},{"needsSteamVerification",1}};
    } else if(mode=="waiting") {
        row["status"]="needs-minimum-artwork";plan["summary"]={{"needsMinimumArtwork",1}};
    } else if(mode=="cached-ready") {
        row["status"]="ready-to-add";plan["summary"]={{"readyToAdd",1}};
    } else if(mode=="long-cached") {
        row["status"]="ready-not-selected";plan["summary"]={{"readyNotSelected",1}};
    }
    if(longOnly)row.erase("artwork");
    attachArtworkPreviews(plan);
    projectResolvedIdentityIntoPendingPlan(snapshot);
    projectCompletedArtworkIntoPendingPlan(snapshot);
    const auto preview=jsonObjectOr(jsonObjectOr(row,"artworkPreview"),previewType);
    const auto readback=jsonObjectOr(row,"artwork");
    require(jsonIntegerOr(row,"steamStoreAppId",0)==4012810,"Readiness lost verified SteamID");
    if(longOnly) {
        const bool cached=mode=="long-cached";
        require(jsonStringOr(row,"status")== (cached?"ready-not-selected":"ready-to-add")&&
            jsonIntegerOr(plan["summary"],cached?"readyNotSelected":"readyToAdd",0)==1&&
            jsonBoolOr(readback,"minimumComplete",false)&&!jsonBoolOr(readback,"long",false),"Optional landscape changed import selection or became publishable");
        require(jsonBoolOr(preview,"previewOnlyManualFallback",false)&&jsonBoolOr(readback,"automaticSlotsComplete",false),
            "Optional temporary landscape left the cached row eligible for repeat scraping");
    } else if(mode=="restored") {
        require(jsonStringOr(row,"status")=="ready-to-add"&&jsonStringOr(preview,"provider")=="manual-override"&&
            jsonBoolOr(readback,"minimumComplete",false),"Original did not restore readiness");
    } else {
        require(jsonStringOr(row,"status")=="needs-minimum-artwork"&& !jsonBoolOr(readback,"minimumComplete",false)&&
            !jsonBoolOr(readback,"tall",false)&&jsonIntegerOr(plan["summary"],"readyToAdd",0)==0&&
            jsonIntegerOr(plan["summary"],"needsMinimumArtwork",0)==1,"Temporary portrait promoted plan or counters");
        if(mode=="cleared")require(preview.empty()&&!jsonBoolOr(readback,"automaticSlotsComplete",false),"Cleared choice reused stale preview or blocked retry");
        else require(jsonBoolOr(preview,"previewOnlyManualFallback",false)&&!jsonStringOr(preview,"url").empty()&&
            jsonBoolOr(readback,"automaticSlotsComplete",false),"Preview disappeared or left missing manual slot eligible for retry");
    }
}

static void verifyManualOriginalPreview(const fs::path& root,std::string_view mode) {
    g_dataRoot=root/L"data";
    const auto exe=root/L"Game"/L"Game.exe", original=root/L"chosen.png", portable=g_dataRoot/L"bad.png";
    const auto artDir=g_dataRoot/L"artwork"/L"fixture", recovered=artDir/L"tall.png";
    for(const auto& path:{exe,original,portable,recovered})fixtureFile(path);
    const auto text=[](const auto& path){return toUtf8(path.wstring());};
    const json choice={{"file",text(original)},{"portableFile",text(portable)},{"updatedAt",42}};
    json current=choice;
    if(mode=="new-choice")current["updatedAt"]=43;
    const auto selected=mode=="deleted"?"deleted":"manual";
    const auto overrides=g_dataRoot/L"config"/L"manual-overrides.json";
    json overrideDocument;
    auto& selection=overrideDocument["items"][toUtf8(normalizedPathKey(exe))];
    selection["cover"]=current;
    selection["artworkProtection"]["cover"]=selected;
    writeFixtureJson(overrides,overrideDocument);
    const auto manifest=artDir/L"manifest.json";
    const auto manifestExe=mode=="foreign-game"?root/L"Other.exe":exe;
    json image={{"type","tall"},{"ok",true},{"file",text(original)},{"portableFile",text(recovered)},
        {"provider","manual-override"},{"sha256",std::string(64,'a')},{"manualRecovery",choice}};
    json document={{"exe",text(manifestExe)},{"artwork",json::array()}};
    document["artwork"].push_back(image);
    writeFixtureJson(manifest,document);
    const auto saved=readText(overrides);
    json plan={{"items",json::array({{{"primaryExecutable",text(exe)},{"artworkManifest",text(manifest)}}})}};
    attachArtworkPreviews(plan);
    const auto result=jsonObjectOr(jsonObjectOr(plan["items"][0],"artworkPreview"),"tall");
    if(mode=="recovered")require(jsonStringOr(result,"portableFile")==text(recovered)&&result.value("manualRecovery",json::object())==choice,
        "Host preferred the broken portable file over the verified original");
    else if(mode=="deleted")require(result.empty(),"Recovery revived a deleted choice");
    else require(!result.contains("manualRecovery")&&jsonStringOr(result,"portableFile")==text(portable),"Recovery followed stale ownership or a new choice");
    require(readText(overrides)==saved,"Reading a preview rewrote the original choice");
}

static void verifyManualRecoveryConsumers(const fs::path& root, std::string_view mode) {
    g_dataRoot = root/L"data";
    const bool cachedPlan = mode.starts_with("cached-"), priorOnly = mode == "cached-plan-only" || mode == "cached-long-only";
    const bool longOnly = mode == "cached-long-only";
    const auto* type = longOnly ? "long" : "tall";
    const auto exe = root/L"Game"/L"Game.exe", artwork = g_dataRoot/L"artwork"/L"fixture";
    const auto manifest = artwork/L"manifest.json", cached = artwork/toWide(std::string(type)+".png");
    fixtureFile(exe);
    fixtureFile(cached);
    const auto text = [](const fs::path& path) { return toUtf8(path.wstring()); };
    const auto key = toUtf8(normalizedPathKey(exe));
    const auto original = text(root/L"original.png");
    const json choice = {{"file", original}, {"portableFile", text(g_dataRoot/L"old-copy.png")}, {"updatedAt", 42}};
    json manual = {{"cover", choice}, {"artworkProtection", {{"cover", "manual"}}}};
    if (mode == "local-clear" || cachedPlan || mode == "opti-plan-clear") manual = json::object();
    if (mode == "local-reselect" || mode == "opti-manifest-reselect") manual["cover"]["updatedAt"] = 43;
    json cover = {{"type", type}, {"ok", true}, {"file", original}, {"portableFile", text(cached)},
        {"provider", "manual-override"}, {"manualRecovery", choice}};
    if (mode == "opti-preview-only") {
        cover.erase("manualRecovery");
        cover["previewOnlyManualFallback"] = true;
        cover["provider"] = "steam";
    }
    const json overrides = {{"items", {{key, manual}}}};
    writeFixtureJson(g_dataRoot/L"config"/L"manual-overrides.json", overrides);
    const json game = {{"primaryExecutable", text(exe)}, {"gameDirectory", text(exe.parent_path())},
        {"directoryName", "Fixture Game"}, {"status", "ready"}};
    if (mode.starts_with("opti-")) {
        if (mode != "opti-plan-clear") writeFixtureJson(manifest, {{"exe", text(exe)}, {"artwork", json::array({cover})}});
        if (mode == "opti-plan-clear" || mode == "opti-preview-only") {
            auto planned = game, preview = cover;
            preview.erase("file");
            planned["artworkPreview"]["tall"] = std::move(preview);
            writeFixtureJson(workspacePlanPath(), {{"libraryState", text(g_dataRoot/L"state"/L"library-scan.json")},
                {"summary", json::object()}, {"items", json::array({planned})}});
        }
        OptiCatalogArtworkIndex index([]{return false;});
        const auto source = index.coverFor({text(exe.parent_path()), text(exe), "Game", "", 0});
        require(source == (mode == "opti-current" ? text(cached) : std::string{}), "OPT exported an invalidated or preview-only image, or lost its current recovery");
        return;
    }
    const json identity = {{"appId", 4012810}, {"steamVerificationStatus", "steam-verified"}, {"identityStatus", "verified"},
        {"primaryProvider", "steam"}, {"resolverVersion", "steam-official-fuzzy-global-v4"}};
    json rows = json::array();
    if (!priorOnly) rows.push_back(cover);
    if (!longOnly) for (const auto* other : {"long", "hero"}) {
        const auto path = artwork/toWide(std::string(other)+".png");
        fixtureFile(path);
        rows.push_back({{"type", other}, {"ok", true}, {"portableFile", text(path)}, {"provider", "steam"}});
    }
    writeFixtureJson(manifest, {{"exe", text(exe)}, {"match", identity}, {"artwork", rows}});
    json snapshot = {{"library", {{"games", json::array({game})}}}, {"manualOverrides", overrides}};
    snapshot["steamPlan"] = buildLocalSteamPlanSnapshot(snapshot);
    auto& plan = snapshot["steamPlan"];
    auto& item = plan["items"][0];
    const bool ready = mode == "local-current" || longOnly;
    if (cachedPlan) {
        item["status"] = "ready-to-add";
        item["artwork"] = {{"minimumComplete", true}, {"automaticSlotsComplete", true}, {"tall", true}, {"long", true}, {"hero", true}};
        plan["summary"] = {{"readyToAdd", 1}, {"needsMinimumArtwork", 0}};
        if (priorOnly) {
            cover.erase("file");
            item["artworkPreview"][type] = cover;
        }
    }
    attachArtworkPreviews(plan);
    projectCompletedArtworkIntoPendingPlan(snapshot);
    const auto state = jsonObjectOr(item, "artwork");
    require(jsonIntegerOr(item, "steamStoreAppId", 0) == 4012810, "Rechecking artwork lost verified Steam identity");
    require(jsonStringOr(item, "status") == (ready ? "ready-to-add" : "needs-minimum-artwork") &&
        jsonBoolOr(state, "tall", !ready) == ready && jsonBoolOr(state, "automaticSlotsComplete", mode != "local-current") == (mode == "local-current") &&
        jsonIntegerOr(plan["summary"], "readyToAdd", -1) == (ready ? 1 : 0), "Host retained stale artwork readiness or lost its current recovery");
}

static void verifyNativeCoverTarget(const fs::path& root, std::string_view mode) {
    g_dataRoot = root / L"data";
    const auto steamApps = root / L"steamapps", directory = steamApps / L"common" / L"A1";
    const auto manifest = steamApps / L"appmanifest_4209920.acf";
    fs::create_directories(directory);
    {
        std::ofstream output(manifest, std::ios::binary);
        output << "\"AppState\" { \"appid\" \"" << (mode == "manifest-changed" ? "108600" : "4209920")
               << "\" \"installdir\" \"A1\" }";
    }
    const auto dir = toUtf8(directory.wstring());
    json row = {{"steamNative", true}, {"nativeSteamAppId", 4209920}, {"gameDirectory", dir},
        {"nativeSteamManifestPath", toUtf8(manifest.wstring())}, {"primaryExecutable", nullptr}};
    if (mode == "unconfirmed") row["nativeSteamScanUnconfirmed"] = true;
    auto rows = json::array({row});
    if (mode == "duplicate") rows.push_back(row);
    writeFixtureJson(g_dataRoot / L"state" / L"library-scan.json", {{"games", rows}});
    json request = {{"nativeSteamAppId", mode == "wrong-appid" ? "108600" : "4209920"},
        {"gameDirectory", mode == "wrong-directory" ? toUtf8(steamApps.wstring()) : dir}};
    if (mode == "current") {
        require(requireNativeCoverRecord(request) == row, "Manifest-backed app without EXE did not resolve");
        return;
    }
    std::string rejected;
    try {
        if (mode == "wallpaper-rejected") {
            request["clearWallpaper"] = true;
            executeWorkspaceCommand("saveArtworkOverride", request);
        } else {
            requireNativeCoverRecord(request);
        }
    } catch (const std::runtime_error& e) { rejected = e.what(); }
    require(!rejected.empty(), "Invalid cover request was admitted");
    if (mode == "wallpaper-rejected")
        require(rejected == "此原生 Steam 入口只适用于封面", "Wallpaper reached the Steam write path");
    require(!fs::exists(g_dataRoot / L"config" / L"manual-overrides.json"), "Rejected request wrote artwork overrides");
}

int wmain(int argc,wchar_t** argv){
    try{
        const bool launchPaths=argc==3&&std::wstring_view(argv[2])==L"--launch-script-path";
        const bool launchSync=argc==3&&std::wstring_view(argv[2])==L"--launch-script-sync";
        const bool launchRecovery=argc==3&&std::wstring_view(argv[2])==L"--launch-script-recovery";
        const bool launchDelete=argc==3&&std::wstring_view(argv[2])==L"--launch-script-delete";
        const bool launchLegacyOwner=argc==4&&std::wstring_view(argv[2])==L"--launch-script-path"&&std::wstring_view(argv[3])==L"legacy-owner";
        const bool launchProspectiveOwner=argc==4&&std::wstring_view(argv[2])==L"--launch-script-path"&&std::wstring_view(argv[3])==L"prospective-owner";
        const bool joinedArtwork=argc==3&&std::wstring_view(argv[2])==L"--joined-artwork-preview";
        const bool previewReadiness=argc==3&&std::wstring_view(argv[2])==L"--preview-readiness";
        const bool previewLandscape=argc==4&&std::wstring_view(argv[2])==L"--preview-readiness"&&std::wstring_view(argv[3])==L"landscape";
        const bool manualOriginal=argc==3&&std::wstring_view(argv[2])==L"--manual-original";
        const bool manualRecoveryConsumers=argc==3&&std::wstring_view(argv[2])==L"--manual-recovery-consumers";
        const bool nativeCoverTargets=argc==3&&std::wstring_view(argv[2])==L"--native-cover-targets";
        require(argc==2||launchPaths||launchSync||launchRecovery||launchDelete||launchLegacyOwner||launchProspectiveOwner||joinedArtwork||previewReadiness||previewLandscape||manualOriginal||manualRecoveryConsumers||nativeCoverTargets,"Pass a new Build fixture root");const auto root=fs::absolute(argv[1]);
        const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
        auto rootText=normalizedPathKey(root),buildText=normalizedPathKey(build);
        if(buildText.back()!=L'\\')buildText+=L"\\";
        require(rootText.starts_with(buildText)&&!fs::exists(root),"Unsafe/reused fixture root");fs::create_directories(root);
        json cases=json::array();
        auto test=[&](const char* name,const std::function<void()>& action){try{action();cases.push_back({{"name",name},{"passed",true}});}catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"error",e.what()}});}};
        if (launchPaths || launchSync || launchRecovery || launchDelete || launchLegacyOwner || launchProspectiveOwner || joinedArtwork || previewReadiness || previewLandscape || manualOriginal || manualRecoveryConsumers || nativeCoverTargets) {
            if(nativeCoverTargets)for(const auto* name:{"current","wrong-appid","wrong-directory","manifest-changed","unconfirmed","duplicate","wallpaper-rejected"})
                test(name,[&]{verifyNativeCoverTarget(root/toWide(name),name);});
            if(manualRecoveryConsumers)for(const auto* name:{"local-current","local-clear","local-reselect","cached-clear","cached-plan-only","cached-long-only",
                    "opti-current","opti-plan-clear","opti-manifest-reselect","opti-preview-only"})
                test(name,[&]{verifyManualRecoveryConsumers(root/toWide(name),name);});
            if(manualOriginal)for(const auto* name:{"recovered","new-choice","foreign-game","deleted"})
                test(name,[&]{verifyManualOriginalPreview(root/toWide(name),name);});
            if(previewReadiness)for(const auto* name:{"local-plan","identity","waiting","cached-ready","cleared","restored"})
                test(name,[&]{verifyPreviewReadiness(root/toWide(name),name);});
            if(previewReadiness||previewLandscape)for(const auto* name:{"long-cached","long-identity"})
                test(name,[&]{verifyPreviewReadiness(root/toWide(name),name);});
            if(launchLegacyOwner)test("legacy-exact-owner-conflict",[&]{verifyLaunchScriptPath(root/L"owner","legacy-exact-owner-conflict");});
            if(launchPaths||launchProspectiveOwner) {
                test("prospective-owner-no-steam",[&]{verifyProspectiveLaunchOwner(root/L"no-steam",false);});
                test("prospective-owner-unresolved-no-vdf",[&]{verifyProspectiveLaunchOwner(root/L"no-vdf",true);});
            }
            if(joinedArtwork)for(const auto* name : {"unchanged-and-locked-source", "same-path-replacement", "removed-source",
                    "different-grid-root", "account-switch", "full-id-horizontal", "deleted-slot", "manual-slot", "canonical-manual-slot",
                    "readonly-original", "opti-uses-current-grid-only"})
                test(name,[&]{verifyJoinedArtworkPreview(root/toWide(name),name);});
            if(launchPaths)for (const auto* name : {"managed-reverse","legacy-alias","legacy-contested","legacy-recovered","missing-original",
                    "wrong-account","multiple-paths","shared-with-other-game","same-path-multiple-accounts","no-account","ordinary-save","legacy-exact-owner-conflict"})
                test(name,[&]{verifyLaunchScriptPath(root/toWide(name),name);});
            if(launchSync)for(const auto* name : {"auto-switch-bat","handwritten","marked-hand-edit","legacy-unknown",
                    "confirmed-update","bad-bat","ordinary-keeps-vbs","legacy-auto-alias"})
                test(name,[&]{verifyLaunchScriptPath(root/toWide(name),name,true);});
            if(launchRecovery)for(const auto* name : {"empty-wrapper","directory-wrapper","oversized-wrapper","locked-empty-wrapper"})
                test(name,[&]{verifyLaunchScriptPath(root/toWide(name),name);});
            if(launchDelete)for(const auto* name : {"no-installation","no-account","missing-config","missing-vdf",
                    "missing-account-directory","unreadable-shortcuts","unknown","partial","referenced"})
                test(name,[&]{verifyLaunchDeleteAdmission(root/toWide(name),name);});
            const auto failures=std::count_if(cases.begin(),cases.end(),[](const auto& row){return !jsonBoolOr(row,"passed",false);});
            const json report={{"allPassed",failures==0},{"caseCount",cases.size()},{"failedCount",failures},{"cases",cases},{"realSteamFilesModified",false}};
            g_dataRoot=root/L"data";
            writeFixtureJson(root/L"summary.json",report);std::cout<<report.dump(2)<<"\n";return failures?1:0;
        }
        g_dataRoot=root/L"data";const auto exe=root/L"Game"/L"Game.exe";fixtureFile(exe);
        const auto tall=g_dataRoot/L"artwork"/L"game"/L"tall.png",longCover=g_dataRoot/L"artwork"/L"game"/L"long.png",hero=g_dataRoot/L"artwork"/L"game"/L"hero.png";
        for(const auto& p:{tall,longCover,hero})fixtureFile(p);
        const auto manifestPath=tall.parent_path()/L"manifest.json";
        json manifest={{"exe",toUtf8(exe.wstring())},{"ok",false},{"successCount",0},{"match",{{"appId",nullptr},{"igdbId",7},{"primaryProvider","playnite-igdb"},{"identityStatus","metadata-verified"},{"contentRelation",nullptr}}},{"artwork",json::array({{{"type","tall"},{"ok",true},{"portableFile",toUtf8(tall.wstring())},{"provider","playnite-igdb"}},{{"type","long"},{"ok",true},{"portableFile",toUtf8(longCover.wstring())},{"provider","baidu-image"}},{{"type","hero"},{"ok",true},{"portableFile",toUtf8(hero.wstring())},{"provider","playnite-igdb"}}})}};
        writeFixtureJson(manifestPath,manifest);
        json plan={{"items",json::array({{{"primaryExecutable",toUtf8(exe.wstring())},{"status","needs-steam-verification"}}})}};
        json snapshot={{"library",{{"games",json::array({{{"primaryExecutable",toUtf8(exe.wstring())},{"gameDirectory",toUtf8(exe.parent_path().wstring())},{"directoryName","Fixture Game"},{"status","ready"}}})}}},{"manualOverrides",{{"items",json::object()}}}};
        test("missing-manifest-link-is-recovered-without-steamid",[&]{auto p=plan;attachArtworkPreviews(p);require(p["items"][0]["artworkPreview"].size()==3,"Portable artwork not attached");require(p["items"][0]["artworkManifest"]==toUtf8(manifestPath.wstring()),"Manifest link not repaired");});
        test("igdb-baidu-preview-is-independent-of-top-level-count",[&]{auto p=plan;attachArtworkPreviews(p);require(p["items"][0]["artworkPreview"]["long"]["provider"]=="baidu-image","Baidu preview hidden");});
        test("visual-complete-is-not-steam-identity",[&]{auto p=buildLocalSteamPlanSnapshot(snapshot);attachArtworkPreviews(p);require(p["items"][0]["status"]=="waiting-steam-verification","Visual result granted import-ready identity");require(p["summary"]["readyToAdd"]==0,"No-ID automatically importable");require(p["items"][0]["artworkPreview"].size()==3,"Waiting item has no images");});
        const auto manualCover=g_dataRoot/L"artwork"/L"overrides"/L"cover.png";fixtureFile(manualCover);
        const json overrides={{"items",{{toUtf8(exe.wstring()),{{"cover",{{"file",toUtf8(manualCover.wstring())}}},{"artworkProtection",{{"cover","manual"}}}}}}}};
        test("one-manual-slot-does-not-skip-game-queue",[&]{require(!manualArtworkProtected(overrides,toUtf8(exe.wstring())),"Partial manual edit blocks automatic queue");});
        test("missing-manual-file-does-not-lock-queue",[&]{auto o=overrides;o["items"][toUtf8(exe.wstring())]["cover"]["file"]=toUtf8((root/L"missing.png").wstring());require(!manualArtworkProtected(o,toUtf8(exe.wstring())),"Missing manual file blocks automatic queue");});
        test("manual-preview-wins-only-own-slot",[&]{writeFixtureJson(g_dataRoot/L"config"/L"manual-overrides.json",overrides);auto p=plan;attachArtworkPreviews(p);const auto a=p["items"][0]["artworkPreview"];require(a["tall"]["source"]=="manual"&&a["long"]["source"]=="automatic"&&a["hero"]["source"]=="automatic","Slot priority is not independent");});
        test("deleted-slot-does-not-show-stale-auto-image",[&]{auto o=overrides;o["items"][toUtf8(exe.wstring())]["artworkProtection"]["cover"]="deleted";writeFixtureJson(g_dataRoot/L"config"/L"manual-overrides.json",o);auto p=plan;attachArtworkPreviews(p);require(!p["items"][0]["artworkPreview"].contains("tall")&&p["items"][0]["artworkPreview"].contains("hero"),"Deleted slot reappeared");});
        test("deleted-slots-stop-auto-retry-but-remain-empty",[&]{auto a=hostArtworkReadiness(manifest,{{"artworkProtection",{{"cover","deleted"}}}});require(!a["tall"].get<bool>()&&a["automaticSlotsComplete"].get<bool>(),"Deleted slot repeatedly scheduled");});
        test("all-explicit-deletions-protect-all-slots",[&]{json o={{"items",{{toUtf8(exe.wstring()),{{"artworkProtection",{{"cover","deleted"},{"long","deleted"},{"wallpaper","deleted"}}}}}}}};require(manualArtworkProtected(o,toUtf8(exe.wstring())),"Full manual opt-out not protected");});
        test("unverified-steam-number-is-not-identity-authority",[&]{auto m=manifest;m["match"]={{"appId",77},{"primaryProvider","steam"},{"steamVerificationStatus",nullptr}};writeFixtureJson(manifestPath,m);auto p=buildLocalSteamPlanSnapshot(snapshot);require(p["summary"]["readyToAdd"]==0,"Unverified AppID granted readiness");writeFixtureJson(manifestPath,manifest);});
        test("bad-manifest-link-cannot-attach-another-game",[&]{auto p=plan;const auto other=g_dataRoot/L"artwork"/L"other"/L"manifest.json";auto m=manifest;m["exe"]=toUtf8((root/L"Other"/L"Game.exe").wstring());writeFixtureJson(other,m);p["items"][0]["artworkManifest"]=toUtf8(other.wstring());attachArtworkPreviews(p);require(p["items"][0]["artworkManifest"]==toUtf8(manifestPath.wstring()),"Wrong game's manifest accepted");});
        test("manual-only-readiness-works-without-manifest",[&]{const auto a=hostArtworkReadiness(json::object(),{{"cover",{{"file",toUtf8(tall.wstring())}}},{"wallpaper",{{"file",toUtf8(hero.wstring())}}}});require(a["minimumComplete"].get<bool>(),"Manual-only images need manifest");});
        const json joined={{"gameDirectory",toUtf8(exe.parent_path().wstring())},{"primaryExecutable",toUtf8(exe.wstring())},{"status","already-in-steam"},{"shortAppId","2388972698"},{"suggestedName","My Custom Shortcut"},{"existingSteamArtwork",{{"hero",toUtf8(hero.wstring())}}}};
        const json ownership={{"libraryState",toUtf8((g_dataRoot/L"state"/L"library-scan.json").wstring())},{"items",json::array({joined})}};
        test("local-artwork-refresh-retains-exact-card-conflict",[&]{
            for(const auto* status:{"shortcut-id-conflict","shortcut-id-collision"}){
                auto observed=ownership;
                auto& prior=observed["items"][0];
                prior["status"]=status;prior["reason"]="stored-shortcut-id-not-unique";
                const auto local=buildLocalSteamPlanSnapshot(snapshot,observed);
                const auto& row=local["items"][0];
                require(row["status"]==status&&row["reason"]==prior["reason"]&&row["selectedForAdd"]==false&&
                    local["summary"]["shortcutIdConflicts"]==1&&!steamPlanItemAlreadyOwned(row)&&
                    !row.contains("shortAppId")&&!row.contains("gridDirectory"),"Local refresh lost a conflict or borrowed a card identity");
                auto different=snapshot;
                different["library"]["games"][0]["primaryExecutable"]=toUtf8((root/L"Game"/L"New.exe").wstring());
                fixtureFile(root/L"Game"/L"New.exe");
                require(buildLocalSteamPlanSnapshot(different,observed)["summary"]["shortcutIdConflicts"]==0,"Conflict leaked onto a replacement EXE");
                observed["items"][0]=joined;
                const auto fresh=buildLocalSteamPlanSnapshot(snapshot,observed);
                require(fresh["items"][0]==joined&&fresh["summary"]["shortcutIdConflicts"]==0,"Authoritative repair did not clear the conflict");
            }
        });
        test("artwork-refresh-retains-joined-shortcut-entire-row",[&]{auto p=buildLocalSteamPlanSnapshot(snapshot,ownership);require(p["items"][0]==joined,"Joined shortcut was reverified or lost grid/identity");require(p["summary"]["alreadyInSteam"]==1&&p["summary"]["needsSteamVerification"]==0,"Joined summary moved to needs-processing");require(steamPlanItemAlreadyOwned(p["items"][0]),"Automatic queue would scrape joined game");});
        test("advisory-scan-classification-cannot-demote-owned-shortcut",[&]{auto s=snapshot;s["library"]["games"][0]["status"]="unrecognized-tool";s["library"]["games"][0]["contentType"]="non-game";const auto p=buildLocalSteamPlanSnapshot(s,ownership);require(p["items"][0]==joined&&p["summary"]["alreadyInSteam"]==1,"Advisory PE classification demoted an owned shortcut");});
        test("added-shortcut-without-store-id-stays-owned",[&]{auto o=ownership;o["items"][0]["status"]="added-to-steam";const auto p=buildLocalSteamPlanSnapshot(snapshot,o);require(p["items"][0]["status"]=="added-to-steam","No-ID added shortcut lost ownership");});
        test("mixed-refresh-keeps-joined-and-applies-new-artwork",[&]{auto s=snapshot;auto other=s["library"]["games"][0];other["gameDirectory"]=toUtf8((root/L"Other").wstring());other["primaryExecutable"]=toUtf8((root/L"Other"/L"Other.exe").wstring());fixtureFile(root/L"Other"/L"Other.exe");s["library"]["games"].push_back(other);auto p=buildLocalSteamPlanSnapshot(s,ownership);attachArtworkPreviews(p);require(p["items"][0]["status"]=="already-in-steam"&&p["items"][0]["artworkPreview"].contains("hero"),"Mixed refresh moved joined game or hid artwork");require(!steamPlanItemAlreadyOwned(p["items"][1]),"Ownership leaked to another game");});
        test("authoritative-shortcut-removal-is-not-sticky",[&]{auto o=ownership;o["items"]=json::array();const auto p=buildLocalSteamPlanSnapshot(snapshot,o);require(!steamPlanItemAlreadyOwned(p["items"][0]),"Removed shortcut remained joined forever");});
        test("changed-primary-exe-does-not-inherit-shortcut-ownership",[&]{auto s=snapshot;s["library"]["games"][0]["primaryExecutable"]=toUtf8((exe.parent_path()/L"New.exe").wstring());fixtureFile(exe.parent_path()/L"New.exe");const auto p=buildLocalSteamPlanSnapshot(s,ownership);require(!steamPlanItemAlreadyOwned(p["items"][0]),"Different EXE inherited ownership");});
        test("different-directory-or-library-does-not-inherit-ownership",[&]{auto o=ownership;o["items"][0]["gameDirectory"]=toUtf8(root.wstring());require(!steamPlanItemAlreadyOwned(buildLocalSteamPlanSnapshot(snapshot,o)["items"][0]),"Directory ownership leaked");o=ownership;o["libraryState"]=toUtf8((root/L"foreign"/L"library-scan.json").wstring());require(!steamPlanItemAlreadyOwned(buildLocalSteamPlanSnapshot(snapshot,o)["items"][0]),"Foreign library ownership leaked");});
        test("native-steam-games-remain-excluded-in-artwork-refresh",[&]{auto s=snapshot;s["library"]["games"][0]["steamNative"]=true;s["library"]["games"][0]["nativeSteamAppId"]=620;auto p=buildLocalSteamPlanSnapshot(s,ownership);require(steamPlanItemAlreadyOwned(p["items"][0])&&p["items"][0]["steamNative"].get<bool>(),"Native exclusion lost");});
        test("artwork-slot-wait-does-not-exhaust-provider-budget",[&]{using Clock=ScrapeGameTimeBudget::Clock;const auto now=Clock::time_point{};ScrapeGameTimeBudget b(now);b.suspend(now+std::chrono::seconds(90));b.suspend(now+std::chrono::seconds(100));b.resume(now+std::chrono::seconds(690));require(b.remaining(now+std::chrono::seconds(690))==210000,"Queue wait charged to provider budget");require(b.remaining(now+std::chrono::seconds(800))==100000,"Work budget was reset");b.suspend(now+std::chrono::seconds(900));b.resume(now+std::chrono::seconds(1900));require(b.remaining(now+std::chrono::seconds(1900))==0,"Idle wait revived exhausted budget");});
        test("cancelled-artwork-slot-waiters-exit-without-another-worker-notification",[&]{std::mutex mutex;std::condition_variable cv;int active=4;std::atomic<bool> cancelled{false},jobCancelled{false};std::atomic<int> returned{0};std::vector<std::thread> waiters;for(int i=0;i<12;++i)waiters.emplace_back([&]{if(!acquireScrapeArtworkSlot(mutex,cv,active,4,cancelled,jobCancelled))++returned;});Sleep(40);jobCancelled=true;for(auto& waiter:waiters)waiter.join();require(returned==12&&active==4,"Cancelled waiters acquired slots or remained asleep");});

        const auto grid=root/L"Steam"/L"userdata"/L"fixture"/L"config"/L"grid";
        for(const auto* stem:{L"4000000000p",L"4000000000",L"4000000000_hero"})fixtureFile(grid/(std::wstring(stem)+L".png"));
        auto gridGame=joined;gridGame["primaryExecutable"]=toUtf8((root/L"GridGame"/L"Game.exe").wstring());gridGame["gameDirectory"]=toUtf8((root/L"GridGame").wstring());gridGame["shortAppId"]="4000000000";gridGame["gridDirectory"]=toUtf8(grid.wstring());gridGame["accountId"]="fixture";gridGame.erase("artworkManifest");gridGame.erase("artworkPreview");
        test("all-advisory-refreshes-retain-steam-grid-images",[&]{json p={{"items",json::array({gridGame})}};for(int i=0;i<8;++i){attachWorkspaceArtwork(p,json::object());require(p["items"][0]["artworkPreview"].size()==3,"Advisory refresh blanked Steam artwork");for(const auto* type:{"tall","long","hero"})require(p["items"][0]["artworkPreview"][type]["provider"]=="steam-grid-existing","Wrong preview provider");}});
        test("temporarily-unreadable-steam-grid-retains-local-cache",[&]{verifyJoinedArtworkPreview(root/L"joined-locked", "unchanged-and-locked-source");});
        g_dataRoot=root/L"data";
        test("manual-deletion-blocks-grid-and-cached-artwork",[&]{const auto overrideFile=g_dataRoot/L"config"/L"manual-overrides.json";const auto before=readJson(overrideFile,json::object());writeFixtureJson(overrideFile,{{"items",{{jsonStringOr(gridGame,"primaryExecutable"),{{"artworkProtection",{{"cover","deleted"},{"long","deleted"},{"wallpaper","deleted"}}}}}}}});json p={{"items",json::array({gridGame})}};attachWorkspaceArtwork(p,json::object());require(!p["items"][0].contains("artworkPreview"),"Deleted slot resurrected from Steam grid");writeFixtureJson(overrideFile,before);});
        test("duplicate-exe-plan-has-one-row-and-retains-nested-ownership",[&]{auto s=snapshot;auto nested=s["library"]["games"][0];nested["gameDirectory"]=toUtf8((exe.parent_path()/L"bin").wstring());s["library"]["games"].push_back(nested);auto o=ownership;o["items"][0]["gameDirectory"]=nested["gameDirectory"];const auto p=buildLocalSteamPlanSnapshot(s,o);require(p["items"].size()==1&&steamPlanItemAlreadyOwned(p["items"][0]),"Deduplication lost existing nested shortcut ownership");});


        writeFixtureJson(g_dataRoot/L"config"/L"manual-overrides.json",overrides);
        const json verified={{"appId",4012810},{"storefrontAppId",4012810},{"formalName","STEINS;GATE RE:BOOT"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"primaryProvider","steam"},{"resolverVersion","steam-official-fuzzy-global-v4"}};
        const auto identityResult=root/L"identity-output"/L"manifest.json";writeFixtureJson(identityResult,{{"exe",toUtf8(exe.wstring())},{"match",verified},{"metadata",{{"releaseDate","2026-08-19"}}}});
        test("metadata-only-verified-identity-survives-persistence-and-old-visual-manifest",[&]{persistResolvedIdentityCache(identityResult);auto s=snapshot;attachResolvedIdentityCache(s);const auto key=toUtf8(normalizedPathKey(exe));require(s["resolvedIdentities"][key]["steamVerificationStatus"]=="steam-verified","Verified status lost from persisted identity index");const auto p=buildLocalSteamPlanSnapshot(s);require(p["items"][0]["status"]=="ready-to-add"&&p["items"][0]["steamStoreAppId"]==4012810,"Old visual/IGDB manifest masked verified Steam identity");attachWorkspaceArtwork(s["steamPlan"]=p,json::object());require(s["steamPlan"]["items"][0]["artworkPreview"].size()==3,"Applying identity erased existing images");});
        test("visual-only-followup-cannot-demote-verified-identity-cache",[&]{persistResolvedIdentityCache(manifestPath);auto s=snapshot;attachResolvedIdentityCache(s);require(buildLocalSteamPlanSnapshot(s)["items"][0]["status"]=="ready-to-add","Weaker visual result overwrote verified cache");});
        test("manual-id-clear-wins-over-cached-verified-identity",[&]{auto s=snapshot;attachResolvedIdentityCache(s);s["manualOverrides"]={{"items",{{toUtf8(exe.wstring()),{{"idCleared",true}}}}}};const auto p=buildLocalSteamPlanSnapshot(s);require(p["items"][0]["status"]=="waiting-steam-verification"&&!p["items"][0].contains("steamStoreAppId"),"Manual ID deletion was undone by identity cache");});
        test("unverified-numeric-candidate-never-promotes-ready",[&]{auto s=snapshot;auto candidate=verified;candidate["identityStatus"]="unverified";candidate["steamVerificationStatus"]="network-pending";s["resolvedIdentities"]={{toUtf8(normalizedPathKey(exe)),candidate}};require(buildLocalSteamPlanSnapshot(s)["items"][0]["status"]=="waiting-steam-verification","Numeric candidate was granted verified identity");});

        test("legacy-local-title-cache-cannot-promote-unverified-import",[&]{auto s=snapshot;auto candidate=verified;candidate["source"]="local-install-metadata-offline";s["resolvedIdentities"]={{toUtf8(normalizedPathKey(exe)),candidate}};require(buildLocalSteamPlanSnapshot(s)["items"][0]["status"]=="waiting-steam-verification","Legacy local-only cache granted official identity");});


        wchar_t corpusBuffer[32768]{};
        if(GetEnvironmentVariableW(L"YMCC_LOCAL_IDENTITY_CORPUS",corpusBuffer,32768)>0) {
            test("actual-local-worker-manifests-survive-host-cache-and-plan",[&]{
                const auto corpus=fs::absolute(fs::path(corpusBuffer));require(normalizedPathKey(corpus).starts_with(buildText),"Actual corpus must stay in Build");
                const auto summary=readJson(corpus/L"cold-summary.json",json::object());require(summary.value("count",0)>=51,"Full local corpus is incomplete");
                g_dataRoot=root/L"actual-local-host-data";
                const auto scan=readJson(corpus/L"full-scan-data"/L"state"/L"library-scan.json",json::object());
                require(jsonArrayOr(scan,"games").size()>=40,"Full game-address scan is missing");
                for(const auto& row:jsonArrayOr(summary,"cases")){
                    const auto caseRoot=corpus/L"cold"/toWide(jsonStringOr(row,"key"));
                    if(fs::is_directory(caseRoot/L"jobs"))persistResolvedIdentityFromOutput(caseRoot/L"jobs",jsonStringOr(row,"executable"));
                }
                json state={{"library",scan},{"manualOverrides",{{"items",json::object()}}}};
                attachResolvedIdentityCache(state);const auto plan=buildLocalSteamPlanSnapshot(state);
                size_t checked=0;
                for(const auto& row:jsonArrayOr(summary,"cases")){
                    if(!jsonBoolOr(row,"steamAccepted",false))continue;
                    const auto exeText=jsonStringOr(row,"executable");const auto wanted=normalizedPathKey(fs::path(toWide(exeText)));
                    for(const auto& item:jsonArrayOr(plan,"items")) {
                        if(normalizedPathKey(fs::path(toWide(jsonStringOr(item,"primaryExecutable"))))!=wanted)continue;
                        const auto match=jsonObjectOr(row,"match");
                        require(jsonIntegerOr(item,"steamStoreAppId",0)==jsonIntegerOr(match,"appId",0),"Verified worker identity disappeared at native Host boundary");
                        if(!jsonBoolOr(item,"steamNative",false))require(jsonStringOr(item,"steamVerificationStatus")=="steam-verified","Native plan demoted formally verified identity");
                        ++checked;
                    }
                }
                require(checked>=25,"Too few complete local Worker-to-Host identity cases");
                state["steamPlan"]=plan;
                state["config"]={{"roots",json::array()},{"automaticScrapingEnabled",false}};
                state["steamAccounts"]={{"accounts",json::array()}};
                writeFixtureJson(root/L"actual-local-host-plan.json",plan);
                writeFixtureJson(root/L"actual-local-host-snapshot.json",state);
            });
        }
        size_t failures=0;for(const auto& t:cases)if(!t["passed"].get<bool>())++failures;
        json report={{"allPassed",failures==0},{"caseCount",cases.size()},{"failedCount",failures},{"cases",cases},{"realSteamFilesModified",false}};writeFixtureJson(root/L"summary.json",report);std::cout<<report.dump(2)<<"\n";return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
