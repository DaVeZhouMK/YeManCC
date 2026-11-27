#define NOMINMAX
#include "../native/custom-steam-library/custom_steam_library_launch_file.h"
#include <fstream>
#include <iostream>

namespace launch = custom_steam_library::launch_file;
namespace fs = std::filesystem;

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

static DWORD run(const fs::path& program, std::wstring args, const fs::path& cwd) {
    args = L"\"" + program.native() + L"\" " + args;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    check(CreateProcessW(program.c_str(), args.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                         cwd.c_str(), &si, &pi) != FALSE, "create fixture process");
    CloseHandle(pi.hThread);
    const auto result = WaitForSingleObject(pi.hProcess, 10000);
    DWORD status = 0;
    const bool exited = result == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &status);
    if (result == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 254);
        WaitForSingleObject(pi.hProcess, 1000);
    }
    CloseHandle(pi.hProcess);
    check(exited, "fixture process must finish in bounded time");
    return status;
}

static void writeScript(const fs::path& file, const std::vector<unsigned char>& bytes) {
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    check(out.good(), "write fixture script");
}

static void writeShortcut(const fs::path& path, const fs::path& target, const fs::path& cwd, const wchar_t* args) {
    struct Apartment {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    } apartment;
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    Microsoft::WRL::ComPtr<IPersistFile> file;
    check(SUCCEEDED(apartment.result), "initialize fixture COM");
    check(SUCCEEDED(CoCreateInstance(__uuidof(ShellLink), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))),
          "create Shell Link fixture");
    check(SUCCEEDED(link->SetPath(target.c_str())), "set fixture target");
    if (!cwd.empty()) check(SUCCEEDED(link->SetWorkingDirectory(cwd.c_str())), "set fixture working directory");
    check(SUCCEEDED(link->SetArguments(args)), "set fixture arguments");
    check(SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Save(path.c_str(), TRUE)), "write Shell Link fixture");
}

int wmain(int argc, wchar_t** argv) try {
    // The fixture child proves working-directory and blocking/exit-code behavior.
    if (argc == 1 || (argc == 2 && std::wstring_view(argv[1]) == L"moshortcut://:F4SE")) {
        const auto expected = argc == 1 ? L"nested & exe" : L"MO2";
        if (!launch::equal(fs::current_path().filename().native(), expected)) return 44;
        std::ofstream out("fixture-receipt.txt");
        out << (argc == 1 ? "exe" : "mo2");
        return out.good() ? 37 : 45;
    }
    if (argc == 3 && std::wstring_view(argv[1]) == L"moshortcut://:F4SE" && std::wstring_view(argv[2]) == L"%TEMP% & two words") {
        std::ofstream out("shortcut-receipt.txt");
        out << "mo2 shortcut";
        return out.good() ? 39 : 45;
    }
    const bool selectedVbs = argc == 3 && std::wstring_view(argv[1]) == L"--vbs-entry";
    const bool selectedShortcut = argc == 3 && std::wstring_view(argv[1]) == L"--shortcut-entry";
    const bool selectedBatchShortcut = argc == 3 && std::wstring_view(argv[1]) == L"--batch-shortcut-entry";
    const bool sourceAlias = argc == 3 && std::wstring_view(argv[1]) == L"--source-alias";
    check(selectedVbs || selectedShortcut || selectedBatchShortcut || sourceAlias || (argc == 3 && std::wstring_view(argv[1]) == L"--fixture"),
          "usage: --fixture|--vbs-entry|--shortcut-entry|--batch-shortcut-entry|--source-alias fresh-directory");
    const auto scope = fs::absolute(argv[2]);
    check(!fs::exists(scope), "fresh fixture directory required");
    const auto game = scope / ((selectedShortcut || selectedBatchShortcut) ? L"Game & \u6e38\u620f" : L"Game %TEMP% & \u6e38\u620f");
    const auto nested = game / L"nested & exe";
    const auto primary = nested / L"Real Game.exe";
    const auto mo2 = game / L"MO2";
    fs::create_directories(nested);
    fs::create_directories(mo2);
    fs::copy_file(fs::absolute(argv[0]), primary);
    fs::copy_file(fs::absolute(argv[0]), mo2 / L"ModOrganizer.exe");
    const auto script = launch::wrapperPath(game);
    const auto wscript = launch::systemFile(L"wscript.exe");
    const auto options = launch::steamOptions(script);
    check(launch::samePath(launch::steamScriptOperand(L"\"" + wscript.native() + L"\"", options), script), "exact Steam operand round trip");
    check(launch::steamScriptOperand(wscript.native(), options + L" --steam").empty(), "refuse extra Steam arguments");
    check(launch::steamScriptOperand(primary.native(), options).empty(), "script name alone cannot claim another target");

    launch::Configuration config{game, primary};
    if (sourceAlias) {
        writeScript(script, {'t', 'e', 's', 't'});
        const auto alias = nested / L"Alternate launcher.vbs";
        const auto distinct = nested / L"Separate launcher.vbs";
        fs::create_hard_link(script, alias);
        fs::copy_file(script, distinct);
        check(fs::equivalent(script, alias) && !fs::equivalent(script, distinct), "fixture alias identity");
        const auto rejects = [&](const fs::path& source) {
            config.source = source;
            try { launch::scriptBytes(config); }
            catch (const std::exception& error) { return std::string_view(error.what()) == "launch-source-recursive"; }
            return false;
        };
        const bool identicalPath = rejects(script);
        const bool identicalFile = rejects(alias);
        config.source = distinct;
        const bool separateFile = !launch::scriptBytes(config).empty();
        std::cout << "samePath=" << identicalPath << " sameFile=" << identicalFile << " distinctFile=" << separateFile << '\n';
        check(identicalPath && identicalFile && separateFile, "wrapper file identity must reject a recursive launch without rejecting a distinct copy");
        return 0;
    }
    if (selectedShortcut) {
        const auto link = scope / L"F4SE.LNK";
        const auto target = mo2 / L"ModOrganizer.exe";
        const auto savedCwd = game / L"Different working directory";
        fs::create_directory(savedCwd);
        const auto args = L"\"moshortcut://:F4SE\"  \"%TEMP% & two words\"";
        writeShortcut(link, target, savedCwd, args);
        const auto parsed = launch::readShortcut(link);
        check(launch::samePath(parsed.target, target) && launch::samePath(parsed.workingDirectory, savedCwd) &&
              parsed.arguments == args, "Shell Link fields must be read without flattening argument text");
        config.source = link;
        writeScript(script, launch::scriptBytes(config));
        check(run(wscript, options, scope) == 39 && fs::exists(savedCwd / L"shortcut-receipt.txt") &&
              !fs::exists(mo2 / L"shortcut-receipt.txt"), "Shell Link uses its own CWD, exact arguments, and waits for result");
        std::cout << "PASS automatic-shortcut-arguments-and-working-directory\n";

        config.workingDirectory = nested;
        writeScript(script, launch::scriptBytes(config));
        check(run(wscript, options, scope) == 39 && fs::exists(nested / L"shortcut-receipt.txt"),
              "explicit working directory overrides the Shell Link");
        config.workingDirectory.clear();
        const auto noCwd = scope / L"F4SE no working directory.lnk";
        writeShortcut(noCwd, target, {}, args);
        config.source = noCwd;
        writeScript(script, launch::scriptBytes(config));
        check(run(wscript, options, scope) == 39 && fs::exists(mo2 / L"shortcut-receipt.txt"),
              "Shell Link without CWD defaults to the target directory");
        std::cout << "PASS automatic-shortcut-directory-precedence\n";

        const auto rejected = [&](const fs::path& source, std::string_view message) {
            config.source = source;
            try { launch::scriptBytes(config); }
            catch (const std::exception& ex) { return std::string_view(ex.what()) == message; }
            return false;
        };
        const auto malformed = scope / L"Malformed.lnk";
        writeScript(malformed, {'n', 'o', 't', ' ', 'a', ' ', 'l', 'i', 'n', 'k'});
        check(rejected(malformed, "launch-shortcut-unreadable"), "malformed link cannot become a fallback launch");
        const auto recursive = scope / L"Recursive.lnk";
        writeShortcut(recursive, script, savedCwd, L"");
        check(rejected(recursive, "launch-source-recursive"), "link cannot point back at StartGame.vbs");
        fs::remove(target);
        check(rejected(link, "launch-shortcut-target-unavailable"), "missing target cannot become a fallback launch");
        std::cout << "PASS automatic-shortcut-rejects-invalid-targets\n";
        return 0;
    }
    if (selectedBatchShortcut) {
        for (const auto* extension : {L".bat", L".cmd"}) {
            const auto base = std::wstring(extension + 1);
            const auto entry = mo2 / (L"Launch & Game" + std::wstring(extension));
            const auto savedCwd = game / (L"Selected start folder " + base);
            const auto link = scope / (L"Shortcut " + base + L".lnk");
            fs::create_directory(savedCwd);
            std::ofstream batch(entry, std::ios::binary);
            batch << "@echo off\r\n\"%~dp0ModOrganizer.exe\" \"%~1\" \"%~2\"\r\nexit /b %errorlevel%\r\n";
            batch.close();
            check(batch.good(), "write BAT/CMD shortcut target");
            const auto args = L"\"moshortcut://:F4SE\"  \"%TEMP% & two words\"";
            writeShortcut(link, entry, savedCwd, args);
            const auto parsed = launch::readShortcut(link);
            check(launch::samePath(parsed.target, entry) && launch::samePath(parsed.workingDirectory, savedCwd) &&
                parsed.arguments == args, "batch shortcut fields must be available without executing the shortcut");
            config.source = link;
            writeScript(script, launch::scriptBytes(config));
            check(run(wscript, options, scope) == 39 && fs::exists(savedCwd / L"shortcut-receipt.txt") &&
                !fs::exists(mo2 / L"shortcut-receipt.txt"), "batch shortcut did not preserve arguments, start folder, or exit status");
            std::wcout << L"PASS automatic-shortcut-" << base << L'\n';
        }
        return 0;
    }
    if (selectedVbs) {
        const auto external = nested / L"Launch %TEMP% & Game.VbS";
        std::ofstream nestedScript(external, std::ios::binary);
        nestedScript << "Option Explicit\r\nDim runner, status\r\n"
            "Set runner = CreateObject(\"WScript.Shell\")\r\n"
            "status = runner.Run(Chr(34) & \".\\Real Game.exe\" & Chr(34), 1, True)\r\n"
            "WScript.Quit status\r\n";
        nestedScript.close();
        check(nestedScript.good(), "write nested launcher");
        config.source = external;
        writeScript(script, launch::scriptBytes(config));
        check(run(wscript, options, scope) == 37 && fs::exists(nested / L"fixture-receipt.txt"),
              "automatic VBS uses the selected launch script's directory, waits and propagates its result");
        std::cout << "PASS automatic-vbs-entry\n";
        return 0;
    }
    writeScript(script, launch::scriptBytes(config));
    check(run(wscript, options, scope) == 37 && fs::exists(nested / L"fixture-receipt.txt"), "nested EXE waits, uses its directory and preserves literal percent path");

    const auto bat = mo2 / L"Launch %TEMP% & Game.bat";
    {
        std::ofstream batch(bat, std::ios::binary);
        batch << "@echo off\r\nstart \"\" /wait \"ModOrganizer.exe\" \"moshortcut://:F4SE\"\r\nexit /b %errorlevel%\r\n";
        check(batch.good(), "write BAT fixture");
    }
    config.source = bat;
    writeScript(script, launch::scriptBytes(config));
    check(run(wscript, options, scope) == 37 && fs::exists(mo2 / L"fixture-receipt.txt"), "BAT preserves path, MO2 argument, working directory and result");

    fs::remove(mo2 / L"fixture-receipt.txt");
    config.manual = true;
    config.command = L"@echo off\r\ncd /d \"MO2\"\nstart \"\" /wait \"ModOrganizer.exe\" \"moshortcut://:F4SE\"\nexit /b %errorlevel%";
    writeScript(script, launch::scriptBytes(config));
    check(run(wscript, options, scope) == 37 && fs::exists(mo2 / L"fixture-receipt.txt"), "manual multi-line batch command runs without translation");
    for (const auto& item : fs::directory_iterator(game))
        check(item.path().extension() != L".cmd", "temporary command file was released");

    config.manual = false;
    config.source = script;
    bool recursive = false;
    try { launch::scriptBytes(config); }
    catch (const std::exception& ex) { recursive = std::string_view(ex.what()) == "launch-source-recursive"; }
    check(recursive, "wrapper cannot execute itself");
    std::cout << "LAUNCH_FILE_SELFTEST_OK\n";
    return 0;
} catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
}
