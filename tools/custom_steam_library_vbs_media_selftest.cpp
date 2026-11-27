#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "../native/shared_game_media_shortcut.h"
#include <iostream>

using namespace ymcc::media;

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

static void bindingRecovery(const fs::path& root) {
    using namespace custom_steam_library::launch_file;
    const auto report=read(root/L"summary.json");
    check(report.value("allPassed",false)&&!report.value("realSteamFilesModified",true),"requires isolated binding recovery results");
    for(const auto& index:{L"c1",L"c2"}) {
        const auto fixture=root/index, data=fixture/L"data", primary=fixture/L"Game"/L"Game.exe";
        const auto vdf=fixture/L"Steam"/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf";
        const auto backups=data/L"backups"/L"steam-shortcuts"/L"19627";
        size_t matches=0;
        for(const auto& entry:fs::directory_iterator(backups)) {
            const auto backup=entry.path()/L"shortcuts.before.vdf";
            if(!fs::is_regular_file(backup)) continue;
            for(const auto& row:readShortcutItems(backup)) {
                if(steamScriptOperand(wide(stringField(row,"exe")),wide(stringField(row,"launchoptions"))).empty()) continue;
                ++matches;
                const auto start=fs::path(unquoted(wide(stringField(row,"startdir"))));
                check(!fs::exists(start/L"binding.json")&&fs::is_regular_file(start/bindingRecoveryFileName),"isolated card must depend on its recovery copy");
                const auto decoded=describeShortcut(row,vdf,data,"19627");
                check(fs::path(wide(stringField(decoded,"executable")))==primary,"media reader substituted the shared script host");
                bool rejected=false;
                try { describeShortcut(row,vdf,data,"22202"); }
                catch(const std::exception&) { rejected=true; }
                check(rejected&&!fs::exists(start/L"binding.json"),"media reader wrote back or accepted another account");
            }
        }
        check(matches==1,"expected precisely one archived VBS row");
    }
}

int wmain(int argc, wchar_t** argv) try {
    if(argc==3&&std::wstring_view(argv[2])==L"--binding-recovery") {
        bindingRecovery(fs::absolute(argv[1]));
        std::cout << "VBS_MEDIA_BINDING_RECOVERY_OK\n";
        return 0;
    }
    check(argc == 2, "usage: existing isolated commit fixture root [--binding-recovery]");
    const auto root = fs::absolute(argv[1]);
    const auto data = root / L"data";
    const auto file = data / L"sandbox-steam" / L"userdata" / L"19627" / L"config" / L"shortcuts.vdf";
    const auto report = read(root / L"result.json");
    check(report.value("ok",false) && report.value("realSteamWrites",1) == 0, "requires a verified isolated VDF");
    const auto cardFile = readShortcutItems(file);
    check(cardFile.size() == 3, "three written cards");
    const auto scopedCards = shortcutInventoryForAccount(file, 19627, [] {return 19627u;}, data);
    // The fixture leaves the old card's Start In contaminated as a negative.
    check(scopedCards.size() == 2, "contaminated existing card must not publish a media identity");
    for (const auto& card : scopedCards) {
        const auto name = stringField(card,"name");
        check(name == "New A" || name == "New B", "exact expected card is selected");
        const auto expected = root / L"games" / wide(name) / L"bin" / L"Game.exe";
        check(fs::path(wide(stringField(card,"executable"))) == expected, "shared wscript host does not cross-bind game EXEs");
        const auto shortId = shortcutAppId(stringField(card,"shortcutAppId"));
        const auto gameId = std::to_string((uint64_t(shortId) << 32) | 0x02000000ull);
        check(shortcutIdentity(file,gameId,data,"19627")["executable"] == card["executable"], "full GameID resolves the same target");
        check(shortcutInventory(file,data,"19628").empty(), "wrong current account cannot reuse this Start In binding");
        for (const auto& original : cardFile) {
            if (original["appid"] != shortId) continue;
            for (int negative = 0; negative < 3; ++negative) {
                auto spoof = original;
                auto vdf = file;
                if (negative == 0) spoof["launchoptions"] = stringField(spoof,"launchoptions") + " --steam";
                if (negative == 1) spoof["appid"] = shortId ^ 1;
                if (negative == 2) vdf = file.parent_path() / L"other.vdf";
                bool rejected = false;
                try {describeShortcut(spoof,vdf,data,"19627");}
                catch (const std::exception& error) {rejected=std::string_view(error.what())=="media-shortcut-script-binding-unconfirmed";}
                check(rejected, "extra args, different ID or different VDF must not fall back to the host");
            }
        }
    }
    check(shortcutInventory(file).empty(), "no local binding source means no media lookup through script host");
    std::cout << "VBS_MEDIA_BINDING_SELFTEST_OK\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
