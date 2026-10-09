// Extract and execute the production C++ background read/clear functions.
// Files and settings are isolated under Build/Validation; never touch an install.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const root = process.cwd();
const out = process.env.YMCC_DEFAULT_BACKGROUND_OUT || path.resolve(root, '../../Build/Validation/default-background-20261009/native-fixture');
fs.mkdirSync(out, { recursive: true });
const main = fs.readFileSync(path.join(root, 'native/main.cpp'), 'utf8').replaceAll('\r\n', '\n');
const bridge = fs.readFileSync(path.join(root, 'src/bridge/background.ts'), 'utf8');
const settings = fs.readFileSync(path.join(root, 'src/views/SettingsView.vue'), 'utf8');
assert(main.includes('L"C:\\\\SOFT\\\\YeMan\\\\Demo\\\\MP4"'));
assert(bridge.includes("'C:\\\\SOFT\\\\YeMan\\\\Demo\\\\MP4\\\\SteamDeck.mp4'"));
assert(settings.includes('默认 MP4：{{ DEFAULT_BACKGROUND_VIDEO_PATH }}'));
assert(settings.includes('overflow-wrap: anywhere;'));
const mapping = main.slice(main.indexOf('static bool configureUserAssetsHost('), main.indexOf('static std::wstring webViewFallbackMimeType('));
assert(mapping.includes('DEFAULT_BACKGROUND_VIDEO_HOST, DEFAULT_BACKGROUND_VIDEO_DIR.c_str()'));
assert(mapping.includes('return SUCCEEDED(userAssetsView->SetVirtualHostNameToFolderMapping('));
assert(mapping.includes('L"user-assets.localhost"'));

let production = main.slice(main.indexOf('static const std::wstring DEFAULT_BACKGROUND_VIDEO_DIR ='), main.indexOf('// Dynamic Steam media is a consumer'));
assert(production.includes('static json default_background_state()'));
assert(production.includes('static json background_state()'));
production = production.replace(/static const std::wstring DEFAULT_BACKGROUND_VIDEO_DIR = [^;]+;/,
  'static const std::wstring DEFAULT_BACKGROUND_VIDEO_DIR = (fspath::current_path() / L"demo-mp4").wstring();');
const clearStart = main.indexOf('    ipc_on("background.clear", [](const json&) -> json {');
assert(clearStart >= 0);
const clearEnd = main.indexOf('\n    });', clearStart);
const clearBody = main.slice(main.indexOf('\n', clearStart) + 1, clearEnd);
assert(clearBody.includes('json{{"disabled", true}}'));
const fixture = String.raw`
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <stdexcept>
#include "json.hpp"
namespace fspath = std::filesystem;
using json = nlohmann::json;
static json settings = json::object();
static int writes = 0;
static std::mutex g_backgroundMtx;
static std::string W2U(const std::wstring& value) { return std::string(value.begin(), value.end()); }
static std::wstring U2W(const std::string& value) { return std::wstring(value.begin(), value.end()); }
static json ymSettingsSection(const char*) { return settings; }
static bool ymSettingsPatchSection(const char*, const json& patch) {
    ++writes;
    for (auto it = patch.begin(); it != patch.end(); ++it) settings[it.key()] = it.value();
    return true;
}
static std::wstring background_assets_dir() { return (fspath::current_path() / L"custom-assets").wstring(); }
static std::wstring background_config_path() { return background_assets_dir() + L"\\background.json"; }
static bool DeleteFileW(const wchar_t* path) { std::error_code ec; return fspath::remove(path, ec); }
//__PRODUCTION__
static json clear_background() {
//__CLEAR_BODY__
}
static int passed = 0;
static void check(bool ok, const char* name) { if (!ok) throw std::runtime_error(name); ++passed; std::cout << "PASS " << name << '\n'; }
static bool disabled(const json& state) { return state.at("enabled") == false && state.at("url") == ""; }
static void put(const fspath::path& file, const std::string& contents) { std::ofstream out(file, std::ios::binary); out << contents; }
int main() {
    try {
        // The runner supplies a unique cwd; all fixture files stay there.
        const auto demo = fspath::path(DEFAULT_BACKGROUND_VIDEO_DIR);
        const auto file = demo / "SteamDeck.mp4";
        // The runner gives this executable a fresh cwd, so demo is absent on every invocation.
        check(disabled(background_state()) && !fspath::exists(demo) && writes == 0, "missing demo directory silently disables background without writes");
        fspath::create_directories(demo);
        check(disabled(background_state()) && writes == 0, "missing MP4 is optional and never errors");
        fspath::create_directory(file);
        check(disabled(background_state()), "directory named MP4 is not accepted as media");
        fspath::remove(file);
        put(file, "");
        check(disabled(background_state()), "empty MP4 quietly falls back to normal UI");
        put(file, "this is not a valid mp4");
        check(disabled(background_state()), "invalid MP4 quietly falls back to normal UI");
        put(file, std::string("\0\0\0\x10" "ftypisom\0\0\0\0", 16));
        const auto factory = background_state();
        check(factory.at("enabled") == true && factory.at("kind") == "video" && factory.at("file") == W2U(file.wstring()) && factory.at("url").get<std::string>().find("https://default-background.localhost/SteamDeck.mp4") == 0 && writes == 0,
              "valid default MP4 is served read-only from the requested path");
        fspath::create_directories(fspath::path(background_assets_dir()));
        put(fspath::path(background_assets_dir()) / "background.png", "custom-image");
        settings["asset"] = {{"file", "background.png"}, {"kind", "image"}, {"stamp", 42}};
        check(background_state().at("url") == "https://user-assets.localhost/background.png?v=42", "custom background takes precedence over default MP4");
        check(disabled(clear_background()) && disabled(background_state()) && disabled(background_state()) && fspath::exists(file), "explicit clear survives reload and never deletes the factory video");
        settings["asset"] = {{"file", "missing.png"}, {"kind", "image"}};
        check(disabled(background_state()), "missing configured custom background does not silently switch to default");
        put(fspath::path(background_assets_dir()) / "background.png", "custom-image");
        settings["asset"] = {{"file", "background.png"}, {"kind", "image"}, {"stamp", 43}};
        check(background_state().at("enabled") == true && background_state().at("kind") == "image", "choosing a custom file after clear re-enables background normally");
        settings["asset"] = json::object();
        put(fspath::path(background_config_path()), R"({"file":"background.png","kind":"image","stamp":44})");
        check(background_state().at("url") == "https://user-assets.localhost/background.png?v=44", "legacy custom configuration remains preferred");
        put(fspath::path(background_config_path()), "broken-json");
        check(disabled(background_state()), "broken legacy configuration remains non-fatal");
        std::cout << passed << " production native checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
`.replace('//__PRODUCTION__', production).replace('//__CLEAR_BODY__', clearBody);
fs.writeFileSync(path.join(out, 'fixture.cpp'), fixture);
const compile = '@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\n' +
  'cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"' + path.join(root, 'deps/json') + '" "' + path.join(out, 'fixture.cpp') + '" /Fo"' + path.join(out, 'fixture.obj') + '" /Fe"' + path.join(out, 'fixture.exe') + '"\r\nexit /b %errorlevel%\r\n';
fs.writeFileSync(path.join(out, 'compile.cmd'), compile);
const built = spawnSync('cmd.exe', ['/d', '/c', path.join(out, 'compile.cmd')], { cwd: out, encoding: 'utf8' });
fs.writeFileSync(path.join(out, 'build.log'), (built.stdout || '') + (built.stderr || ''));
assert.equal(built.status, 0, built.stdout + built.stderr);
const runDir = fs.mkdtempSync(path.join(out, 'run-'));
const ran = spawnSync(path.join(out, 'fixture.exe'), [], { cwd: runDir, encoding: 'utf8' });
fs.writeFileSync(path.join(out, 'results.log'), (ran.stdout || '') + (ran.stderr || ''));
console.log(ran.stdout);
assert.equal(ran.status, 0, ran.stderr);
console.log('PASS native/UI default paths agree, scoped optional media host wired, long paths wrap');
