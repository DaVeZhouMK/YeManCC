// Extract and execute the production C++ background read/clear functions.
// Files and settings are isolated under Build/Validation; never touch an install.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';

const root = process.cwd();
const out = process.env.YMCC_DEFAULT_BACKGROUND_OUT || path.resolve(root, '../../Build/Validation/default-background-20261009/native-fixture');
fs.mkdirSync(out, { recursive: true });
const stormOnly=process.argv.includes('--config-storm');
const nativeOnly=process.argv.includes('--native-only');
const uiSourceOnly=process.argv.includes('--ui-source-guard');
const sourcePath=stormOnly&&process.env.YMCC_BACKGROUND_CONFIG_SOURCE?path.resolve(process.env.YMCC_BACKGROUND_CONFIG_SOURCE):path.join(root,'native/main.cpp');
const main = fs.readFileSync(sourcePath, 'utf8').replaceAll('\r\n', '\n');
if(stormOnly){generateConfigStorm();process.exit(0);}
const bridge = fs.readFileSync(path.join(root, 'src/bridge/background.ts'), 'utf8');
const settings = fs.readFileSync(uiSourceOnly&&process.env.YMCC_BACKGROUND_UI_SOURCE?process.env.YMCC_BACKGROUND_UI_SOURCE:path.join(root, 'src/views/SettingsView.vue'), 'utf8');
assert(main.includes('L"C:\\\\SOFT\\\\YeMan\\\\Demo\\\\MP4"'));
assert(bridge.includes("'C:\\\\SOFT\\\\YeMan\\\\Demo\\\\MP4\\\\SteamDeck.mp4'"));
if(!nativeOnly){
  // The current UI names the actual native selection, including the factory
  // MP4. Keep the source contract tied to the load/choose/clear data flow.
  assert(settings.includes(':title="backgroundFileLabel"'));
  assert(settings.includes("{{bgEnabled?backgroundFileLabel:'未选择壁纸'}}"));
  const uiFunction=name=>{const start=settings.indexOf('async function '+name+'(');assert(start>=0);const end=settings.indexOf('\n}',start);assert(end>start);return settings.slice(start,end+2);};
  const load=uiFunction('loadBackgroundState'),choose=uiFunction('chooseBackground'),clear=uiFunction('clearBackground');
  assert(load.includes('await backgroundGet()'));
  assert(load.includes(String.raw`backgroundFileLabel.value=state?.file?.split(/[\\/]/).pop()`));
  assert(choose.includes('const state = await backgroundInstall(picked);'));
  assert(choose.includes(String.raw`backgroundFileLabel.value=(state.file || picked).split(/[\\/]/).pop()`));
  assert(choose.includes('notifyBackgroundChanged(state);'));
  assert(clear.includes('const state = await backgroundClear();'));
  assert(clear.includes("backgroundFileLabel.value='未选择背景文件'"));
  assert(clear.includes('notifyBackgroundChanged(state);'));
  assert(bridge.includes("invoke<BackgroundState>('background.get')"));
  assert(bridge.includes("invoke<BackgroundState>('background.install', { source }"));
  assert(bridge.includes("invoke<BackgroundState>('background.clear')"));
}
assert(settings.includes('overflow-wrap: anywhere;'));
const mapping = main.slice(main.indexOf('static bool configureUserAssetsHost('), main.indexOf('static std::wstring webViewFallbackMimeType('));
assert(mapping.includes('DEFAULT_BACKGROUND_VIDEO_HOST, DEFAULT_BACKGROUND_VIDEO_DIR.c_str()'));
assert(mapping.includes('return SUCCEEDED(userAssetsView->SetVirtualHostNameToFolderMapping('));
assert(mapping.includes('L"user-assets.localhost"'));
if(uiSourceOnly){console.log('PASS original background source guards: default path, mapping, current label binding and load/choose/clear; native12 NOT_RUN');process.exit(0);}

function backgroundProductionFunction(name){const hits=[...main.matchAll(new RegExp('^static [^\\n]+ '+name+'\\(', 'gm'))].filter(m=>{const brace=main.indexOf('{',m.index),semi=main.indexOf(';',m.index);return brace>=0&&(semi<0||brace<semi);});if(hits.length!==1)throw Error('Missing/ambiguous background function '+name);const a=hits[0].index,b=main.indexOf('\n}',a);if(b<0)throw Error('Missing function end '+name);return main.slice(a,b+2);}

let production = main.slice(main.indexOf('static const std::wstring DEFAULT_BACKGROUND_VIDEO_DIR ='), main.indexOf('// Dynamic Steam media is a consumer'));
assert(production.includes('static json default_background_state()'));
assert(production.includes('static json background_state()'));
if(main.includes('static bool backgroundAssetValid('))production=['startupParseJson','startupReadJsonFile'].map(backgroundProductionFunction).join('\n\n')+'\n'+production;
production = production.replace(/static const std::wstring DEFAULT_BACKGROUND_VIDEO_DIR = [^;]+;/,
  'static const std::wstring DEFAULT_BACKGROUND_VIDEO_DIR = (fspath::current_path() / L"demo-mp4").wstring();');
const clearStart = main.indexOf('    ipc_on("background.clear", [](const json&) -> json {');
assert(clearStart >= 0);
const clearEnd = main.indexOf('\n    });', clearStart);
const clearBody = main.slice(main.indexOf('\n', clearStart) + 1, clearEnd);
assert(clearBody.includes('json{{"disabled", true}}'));
const fixture = String.raw`
#define NOMINMAX
#include <windows.h>
#include <vector>
#include <set>
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
static bool fixtureDeleteFileW(const wchar_t* path) { std::error_code ec; return fspath::remove(path, ec); }
#define DeleteFileW fixtureDeleteFileW
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
const environment=process.env.YMCC_DEFAULT_BACKGROUND_ENV_CMD||'C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat';
assert(fs.existsSync(environment)&&!/["\r\n]/.test(environment));
const compile = '@echo off\r\ncall "'+environment+'" >nul\r\nif errorlevel 1 exit /b 1\r\n' +
  'cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /sourceDependencies "'+path.join(out,'legacy-dependencies.json')+'" /I"' + path.join(root, 'deps/json') + '" "' + path.join(out, 'fixture.cpp') + '" /Fo"' + path.join(out, 'fixture.obj') + '" /Fe"' + path.join(out, 'fixture.exe') + '"\r\nexit /b %errorlevel%\r\n';
fs.writeFileSync(path.join(out, 'compile.cmd'), compile);
const built = spawnSync('cmd.exe', ['/d', '/c', path.join(out, 'compile.cmd')], { cwd: out, encoding: 'utf8' });
fs.writeFileSync(path.join(out, 'build.log'), (built.stdout || '') + (built.stderr || ''));
assert.equal(built.status, 0, built.stdout + built.stderr);
const runDir = fs.mkdtempSync(path.join(out, 'run-'));
fs.writeFileSync(path.join(out,'legacy-execution-identity.json'),JSON.stringify({sourcePath,sourceSha256:createHash('sha256').update(fs.readFileSync(sourcePath)).digest('hex'),generatedCppSha256:createHash('sha256').update(fs.readFileSync(path.join(out,'fixture.cpp'))).digest('hex'),executableSha256:createHash('sha256').update(fs.readFileSync(path.join(out,'fixture.exe'))).digest('hex'),defaultAssertionsUnchanged:true},null,2));
const ran = spawnSync(path.join(out, 'fixture.exe'), [], { cwd: runDir, encoding: 'utf8' });
fs.writeFileSync(path.join(out, 'results.log'), (ran.stdout || '') + (ran.stderr || ''));
console.log(ran.stdout);
assert.equal(ran.status, 0, ran.stderr);
console.log(nativeOnly?'PASS original12 native checks; legacy UI label assertion explicitly NOT_RUN':'PASS native/UI default paths agree, scoped optional media host wired, long paths wrap');


// Explicit generation for this original consumer. The default12 behavior above
// and its setup remain unchanged; an owned runner compiles only this selection.
function generateConfigStorm(){

 const names=['startupParseJson','startupReadJsonFile','backgroundAssetValid','default_background_state','background_state'].filter(n=>new RegExp('^static [^\\n]+ '+n+'\\(','m').test(main));
 const txStart=main.indexOf('// BEGIN YMCC_STATIC_BACKGROUND_TRANSACTION'),txEnd=main.indexOf('// END YMCC_STATIC_BACKGROUND_TRANSACTION',txStart);
 const transaction=txStart>=0&&txEnd>txStart?main.slice(txStart,txEnd+'// END YMCC_STATIC_BACKGROUND_TRANSACTION'.length):'';
 const ipcBody=name=>{const a=main.indexOf('    ipc_on("'+name+'",');if(a<0)throw Error('Missing background IPC '+name);const b=main.indexOf('\n    });',a);if(b<0)throw Error('Missing background IPC end');return main.slice(main.indexOf('{',a)+1,b);};
 const installBody=ipcBody('background.install'),clearBody=ipcBody('background.clear');
 const cpp=String.raw`#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <cwctype>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <string>
#include <functional>
#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <climits>
#include "json.hpp"
using json=nlohmann::json;namespace fspath=std::filesystem;
static json settings=json::object();
static int settingsWriteMode=0;
static unsigned settingsWriteCalls=0;
static std::mutex g_backgroundMtx;
static std::wstring assetRoot,DEFAULT_BACKGROUND_VIDEO_DIR,YM_SETTINGS_FILE,YM_SETTINGS_BACKUP_FILE;
static json settingsBackup=json();
static bool selectorMainOverride=false,selectorBackupOverride=false,selectorMainFault=false,selectorBackupFault=false;
static std::string selectorMainRaw,selectorBackupRaw;
static std::mutex settingsPortMutex;
static thread_local bool settingsPortHeld=false;
struct SettingsFileGuard { std::unique_lock<std::mutex> lock;SettingsFileGuard():lock(settingsPortMutex,std::defer_lock){if(settingsPortHeld)throw std::runtime_error("recursive settings port lock");lock.lock();settingsPortHeld=true;}~SettingsFileGuard(){settingsPortHeld=false;} };
static json fixtureRootDocument(){return json{{"schemaVersion",1},{"background",settings},{"unknown",{{"keep",17}}}};}
static json ymSettingsReadSnapshot(const std::string&,const std::wstring& path){
 if(!settingsPortHeld)throw std::runtime_error("selector proof did not hold settings lock");
 const bool backup=path==YM_SETTINGS_BACKUP_FILE;
 if((backup&&selectorBackupFault)||(!backup&&selectorMainFault))return {{"stamp","fault"},{"readFault","owned-read-fault"},{"content",""}};
 if(backup&&selectorBackupOverride)return {{"stamp","owned-backup"},{"content",selectorBackupRaw}};
 if(!backup&&selectorMainOverride)return {{"stamp","owned-main"},{"content",selectorMainRaw}};
 if(backup&&settingsBackup.is_null())return {{"stamp","missing"},{"content",""}};
 return {{"stamp",backup?"owned-backup":"owned-main"},{"content",(backup?settingsBackup:fixtureRootDocument()).dump()}};
}
static bool ymSettingsPatchSection(const char*,const json& patch){SettingsFileGuard lock;++settingsWriteCalls;if(settingsWriteMode==1)return false;if(settingsWriteMode==3)throw std::runtime_error("owned settings IO failure");if(settingsWriteMode==4)return true;settingsBackup=fixtureRootDocument();for(const auto& [key,value]:patch.items())settings[key]=value;return settingsWriteMode!=2;}
static std::wstring mediaConflictPath;
static bool mediaMoveConflict=false,corruptMediaWrite=false,corruptSlotRecord=false;
static BOOL fixtureBackgroundMove(LPCWSTR source,LPCWSTR target,DWORD flags){
 if(mediaMoveConflict&&std::wstring(source).ends_with(L".pending")){HANDLE foreign=CreateFileW(target,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(foreign==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot create real collision port");const char bytes[]="unknown foreign target";DWORD written=0;::WriteFile(foreign,bytes,sizeof(bytes)-1,&written,nullptr);CloseHandle(foreign);mediaConflictPath=target;}
 const auto result=::MoveFileExW(source,target,flags);if(result&&corruptSlotRecord&&std::wstring(source).ends_with(L".json.new")){std::ofstream out(fspath::path(target),std::ios::binary|std::ios::trunc);out<<"{\"schemaVersion\":2}";out.flush();}return result;
}
static BOOL fixtureBackgroundWrite(HANDLE file,LPCVOID bytes,DWORD count,LPDWORD written,LPOVERLAPPED overlap){
 const auto ok=::WriteFile(file,bytes,count,written,overlap);
 if(ok&&corruptMediaWrite){wchar_t path[4096]{};const auto length=GetFinalPathNameByHandleW(file,path,4096,FILE_NAME_NORMALIZED);if(length&&length<4096&&std::wstring(path).ends_with(L".pending")){LARGE_INTEGER zero{},position{};SetFilePointerEx(file,zero,&position,FILE_CURRENT);SetFilePointerEx(file,zero,nullptr,FILE_BEGIN);const char altered='x';DWORD changed=0;::WriteFile(file,&altered,1,&changed,nullptr);SetFilePointerEx(file,position,nullptr,FILE_BEGIN);}}
 return ok;
}

static const wchar_t* DEFAULT_BACKGROUND_VIDEO_HOST=L"default-background.localhost";
static std::wstring background_assets_dir(){return assetRoot;}
static std::wstring background_config_path(){return assetRoot+L"\\background.json";}
static json ymSettingsSection(const char*){return settings;}
static std::string W2U(const std::wstring& value){if(value.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);std::string out(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr);return out;}
static std::wstring U2W(const std::string& value){if(value.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);std::wstring out(n,L'\0');if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n);return out;}
`+'\n'+names.map(backgroundProductionFunction).join('\n\n')+'\n#define MoveFileExW fixtureBackgroundMove\n#define WriteFile fixtureBackgroundWrite\n'+transaction+'\n#undef WriteFile\n#undef MoveFileExW\nstatic json fixtureInstallBackground(const json& a){'+installBody+'\n}\nstatic json fixtureClearBackground(const json& a){'+clearBody+'\n}\n'+String.raw`
static void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static void put(const fspath::path& path,const std::string& text){fspath::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(text.data(),static_cast<std::streamsize>(text.size()));out.flush();need(bool(out),"fixture write failed");}
static std::string bytes(const fspath::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
static bool off(const json& result){return result.is_object()&&result.value("enabled",true)==false&&result.value("url",std::string{})=="";}
int wmain(int argc,wchar_t** argv){try{
 if(argc<2||argc>3)return 2;const fspath::path root=argv[1];fspath::create_directories(root);const std::string selected=argc==3?W2U(argv[2]):"";json cases=json::array();
 auto check=[&](const std::string& name,const std::function<void(const fspath::path&)>& run){if(!selected.empty()&&selected!=name&&!(selected=="@install"&&(name.rfind("background install ",0)==0||name.rfind("background clear ",0)==0)))return;const auto dir=root/std::to_wstring(cases.size());assetRoot=(dir/L"assets").wstring();DEFAULT_BACKGROUND_VIDEO_DIR=(dir/L"demo").wstring();settings=json::object();settingsWriteMode=0;settingsWriteCalls=0;settingsBackup=json();selectorMainOverride=selectorBackupOverride=selectorMainFault=selectorBackupFault=false;selectorMainRaw.clear();selectorBackupRaw.clear();mediaMoveConflict=corruptMediaWrite=corruptSlotRecord=false;mediaConflictPath.clear();YM_SETTINGS_FILE=(dir/L"settings.json").wstring();YM_SETTINGS_BACKUP_FILE=YM_SETTINGS_FILE+L".bak";fspath::create_directories(assetRoot);put(fspath::path(assetRoot)/L"background.png","owned-png");try{run(dir);cases.push_back({{"name",name},{"ok",true}});}catch(const std::exception& error){cases.push_back({{"name",name},{"ok",false},{"error",error.what()}});}};
 const json good={{"file","background.png"},{"kind","image"},{"stamp",42}};
 check("background healthy unified asset and legacy source preserve preferences",[&](const auto&){settings["asset"]=good;need(background_state()["url"]=="https://user-assets.localhost/background.png?v=42","healthy asset changed");settings=json::object();put(background_config_path(),good.dump());need(background_state()["url"]=="https://user-assets.localhost/background.png?v=42"&&bytes(background_config_path())==good.dump(),"healthy legacy changed");});
 check("background missing configuration retains optional factory MP4",[&](const auto&){need(off(background_state()),"missing factory should disable");put(fspath::path(DEFAULT_BACKGROUND_VIDEO_DIR)/L"SteamDeck.mp4",std::string("\0\0\0\x10" "ftypisom\0\0\0\0",16));const auto state=background_state();need(state["enabled"]==true&&state["kind"]=="video"&&state["url"].get<std::string>().rfind("https://default-background.localhost/SteamDeck.mp4",0)==0,"factory fallback lost");});
 check("background explicit disabled always wins over legacy and factory",[&](const auto&){put(fspath::path(DEFAULT_BACKGROUND_VIDEO_DIR)/L"SteamDeck.mp4",std::string("\0\0\0\x10" "ftypisom\0\0\0\0",16));put(background_config_path(),good.dump());settings["asset"]={{"disabled",true}};need(off(background_state()),"clear reenabled old media");});
 for(const auto& value:std::vector<json>{nullptr,true,12,"bad",json::array()})check("background bad unified asset container "+value.dump(),[&,value](const auto&){put(background_config_path(),good.dump());settings["asset"]=value;need(off(background_state())&&bytes(background_config_path())==good.dump(),"bad unified asset resurrected old file or threw");});
 for(const auto& value:std::vector<json>{nullptr,true,12,"bad",json::array()})check("background bad legacy root "+value.dump(),[&,value](const auto&){put(background_config_path(),value.dump());need(off(background_state())&&bytes(background_config_path())==value.dump(),"bad legacy root accepted or modified");});
 for(const auto& key:std::vector<std::string>{"file","kind","stamp","disabled"})for(const auto& value:std::vector<json>{nullptr,json::array()})check("background bad known field "+key+" "+value.dump(),[&,key,value](const auto&){auto bad=good;bad[key]=value;settings["asset"]=bad;need(off(background_state()),"bad typed known field accepted");settings=json::object();put(background_config_path(),bad.dump());need(off(background_state())&&bytes(background_config_path())==bad.dump(),"bad legacy known field accepted or changed");});
 for(const auto& value:std::vector<json>{-1,0.5})check("background invalid stamp "+value.dump(),[&,value](const auto&){auto bad=good;bad["stamp"]=value;settings["asset"]=bad;need(off(background_state()),"invalid stamp adopted");});
 for(const auto* file:{"..","C:background.png","background.png:stream","nested/background.png","nested\\background.png"})check(std::string("background unrepresentable asset name ")+file,[&,file](const auto&){auto bad=good;bad["file"]=file;settings["asset"]=bad;need(off(background_state()),"path escape accepted");});
 check("background duplicate decoded legacy key rejected",[&](const auto&){const std::string raw="{\"file\":\"background.png\",\"fi\\u006ce\":\"background.png\",\"stamp\":1}";put(background_config_path(),raw);need(off(background_state())&&bytes(background_config_path())==raw,"duplicate key accepted");});
 check("background future legacy source remains inert",[&](const auto&){auto future=good;future["schemaVersion"]=2;put(background_config_path(),future.dump());need(off(background_state())&&bytes(background_config_path())==future.dump(),"future source consumed");});
 check("background oversized source defaults without rewriting",[&](const auto&){auto value=good;value["extension"]=std::string((1u<<20)+1,'x');put(background_config_path(),value.dump());need(off(background_state())&&bytes(background_config_path())==value.dump(),"oversized config consumed or changed");});
 check("background excessive nesting defaults without rewriting",[&](const auto&){json value=json::object();for(int i=0;i<70;++i)value={{"n",value}};auto cfg=good;cfg["extension"]=value;put(background_config_path(),cfg.dump());need(off(background_state())&&bytes(background_config_path())==cfg.dump(),"overdeep source consumed");});
 check("background corrupt source does not revive factory",[&](const auto&){put(fspath::path(DEFAULT_BACKGROUND_VIDEO_DIR)/L"SteamDeck.mp4",std::string("\0\0\0\x10" "ftypisom\0\0\0\0",16));for(const auto& raw:std::vector<std::string>{"", "{bad", "{} tail", std::string("{\"file\":\"")+char(0xff)+"\"}"}){put(background_config_path(),raw);need(off(background_state())&&bytes(background_config_path())==raw,"bad source fell through to factory or changed");}});
 check("background locked legacy source defaults then explicit reload heals",[&](const auto&){put(fspath::path(DEFAULT_BACKGROUND_VIDEO_DIR)/L"SteamDeck.mp4",std::string("\0\0\0\x10" "ftypisom\0\0\0\0",16));put(background_config_path(),good.dump());HANDLE held=CreateFileW(background_config_path().c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);need(held!=INVALID_HANDLE_VALUE,"fixture lock failed");json state;try{state=background_state();}catch(...){CloseHandle(held);throw;}CloseHandle(held);need(off(state)&&background_state()["enabled"]==true,"locked source was treated as absent or retry stuck");});
 check("background corrupt and repaired read storm stays available",[&](const auto&){for(int i=0;i<16;++i){put(background_config_path(),"{partial");need(off(background_state())&&bytes(background_config_path())=="{partial","corrupt storm changed source");put(background_config_path(),good.dump());need(background_state()["enabled"]==true,"read recovery stuck");}});
 check("background BOM unknown and Unicode values stay read-only",[&](const auto&){auto cfg=good;cfg["extension"]={{"note","中文"}};const auto raw=std::string("\xef\xbb\xbf")+cfg.dump();put(background_config_path(),raw);need(background_state()["enabled"]==true&&bytes(background_config_path())==raw,"valid BOM/extension rejected or changed");});
 check("background empty asset remains legacy compatibility marker",[&](const auto&){settings["asset"]=json::object();put(background_config_path(),good.dump());need(background_state()["enabled"]==true,"empty legacy compatibility changed");});

 check("background embedded NUL filename cannot reuse a truncated file path",[&](const auto&){auto cfg=good;cfg["file"]=std::string("background.png\0tail",19);settings["asset"]=cfg;need(off(background_state()),"embedded NUL filename accepted");});
 check("background valid byte boundary and unsigned stamp remain readable",[&](const auto&){auto cfg=good;cfg["stamp"]=18446744073709551615ull;cfg["extension"]="";const auto size=cfg.dump().size();cfg["extension"]=std::string((1u<<20)-size,'x');const auto raw=cfg.dump();need(raw.size()==(1u<<20),"fixture byte count wrong");put(background_config_path(),raw);const auto state=background_state();if(!(state["enabled"]==true&&state["url"]=="https://user-assets.localhost/background.png?v=18446744073709551615"&&bytes(background_config_path())==raw))throw std::runtime_error(json{{"reason","legal boundary preference lost"},{"state",state},{"stampType",cfg["stamp"].type_name()},{"integer",cfg["stamp"].is_number_integer()},{"lessThanZero",cfg["stamp"]<0},{"unchanged",bytes(background_config_path())==raw}}.dump());});
 check("background legal Unicode asset filename preserved",[&](const auto&){const auto file=std::string("自定义.png");put(fspath::path(assetRoot)/U2W(file),"owned Unicode asset");auto cfg=good;cfg["file"]=file;put(background_config_path(),cfg.dump());const auto result=background_state();need(result["enabled"]==true&&result["file"]==file&&bytes(background_config_path())==cfg.dump(),"Unicode preference changed");});

 const std::string oldPng=std::string("\x89PNG\r\n\x1a\n",8)+"old-owned-pixels",newPng=std::string("\x89PNG\r\n\x1a\n",8)+"new-owned-pixels";
 check("background install config refusal preserves previous selected media",[&](const auto& dir){settings["asset"]=good;put(fspath::path(assetRoot)/L"background.png",oldPng);const auto source=dir/L"selected-new.png";put(source,newPng);settingsWriteMode=1;bool refused=false;try{fixtureInstallBackground({{"source",W2U(source.wstring())}});}catch(...){refused=true;}need(refused&&settings["asset"]==good&&bytes(fspath::path(assetRoot)/L"background.png")==oldPng,"config refusal changed old selected media");need(background_state()["enabled"]==true,"failed install lost usable old background");});
 check("background install config refusal never deletes other selected type",[&](const auto& dir){auto old=good;old["file"]="background.jpg";settings["asset"]=old;put(fspath::path(assetRoot)/L"background.jpg","owned-old-jpg");const auto source=dir/L"selected-new.png";put(source,newPng);settingsWriteMode=1;bool refused=false;try{fixtureInstallBackground({{"source",W2U(source.wstring())}});}catch(...){refused=true;}need(refused&&settings["asset"]==old&&bytes(fspath::path(assetRoot)/L"background.jpg")=="owned-old-jpg"&&background_state()["enabled"]==true,"config refusal deleted old selected type");});
 check("background install commit then lost ACK keeps candidate available",[&](const auto& dir){settings["asset"]=good;put(fspath::path(assetRoot)/L"background.png",oldPng);const auto source=dir/L"selected-new.png";put(source,newPng);settingsWriteMode=2;try{fixtureInstallBackground({{"source",W2U(source.wstring())}});}catch(...){}const auto state=background_state();need(state["enabled"]==true&&bytes(fspath::path(assetRoot)/U2W(settings["asset"].value("file",std::string{})))==newPng,"uncertain ACK rolled back an actually committed candidate");});
 check("background install successful original route remains usable",[&](const auto& dir){settings["asset"]=good;put(fspath::path(assetRoot)/L"background.png",oldPng);const auto source=dir/L"selected-new.png";put(source,newPng);const auto state=fixtureInstallBackground({{"source",W2U(source.wstring())}});need(state["enabled"]==true&&state["kind"]=="image"&&bytes(fspath::path(assetRoot)/U2W(state["file"].get<std::string>()))==newPng&&settingsWriteCalls==1,"successful install route changed");});
 check("background clear config refusal preserves files and preferences",[&](const auto&){settings["asset"]=good;put(fspath::path(assetRoot)/L"background.png",oldPng);settingsWriteMode=1;bool refused=false;try{fixtureClearBackground(json::object());}catch(...){refused=true;}need(refused&&settings["asset"]==good&&bytes(fspath::path(assetRoot)/L"background.png")==oldPng&&background_state()["enabled"]==true,"failed clear changed old files or preferences");});
`+(transaction?String.raw`
 auto managedFiles=[&](){unsigned count=0;for(const auto& e:fspath::directory_iterator(assetRoot))if(e.is_regular_file()&&e.path().filename().wstring().rfind(L"background-static-",0)==0)++count;return count;};
 auto installSource=[&](const fspath::path& dir,const std::string& payload){const auto source=dir/L"selection.png";put(source,payload);return source;};
 auto installFile=[&](const fspath::path& source){return fixtureInstallBackground({{"source",W2U(source.wstring())}});};
 auto expectRefusal=[&](const fspath::path& source){bool refused=false;try{installFile(source);}catch(...){refused=true;}need(refused,"operation unexpectedly reported success");};
 auto assetBytes=[&](const json& state){return bytes(fspath::path(assetRoot)/U2W(state.at("file").template get<std::string>()));};
 auto tree=[&](){std::map<std::wstring,std::string> files;for(const auto& e:fspath::directory_iterator(assetRoot))if(e.is_regular_file())files[e.path().filename().wstring()]=bytes(e.path());return files;};
 check("background install throwing settings port preserves old selection and retries",[&](const auto& dir){settings["asset"]=good;put(fspath::path(assetRoot)/L"background.png",oldPng);const auto source=installSource(dir,newPng);settingsWriteMode=3;expectRefusal(source);need(settings["asset"]==good&&bytes(fspath::path(assetRoot)/L"background.png")==oldPng,"throwing commit changed old selection");settingsWriteMode=0;need(assetBytes(installFile(source))==newPng,"explicit retry after thrown port failed");});
 check("background install repeated refused commits keep a bounded owned candidate set",[&](const auto& dir){settings["asset"]=good;put(fspath::path(assetRoot)/L"background.png",oldPng);const auto source=installSource(dir,newPng);settingsWriteMode=1;for(int i=0;i<12;i++){expectRefusal(source);need(settings["asset"]==good&&bytes(fspath::path(assetRoot)/L"background.png")==oldPng,"repeat refusal damaged old selection");need(managedFiles()<=3,"refused installs accumulated unbounded candidates");}settingsWriteMode=0;need(assetBytes(installFile(source))==newPng,"bounded refusal set never recovered");});
 check("background install repeated lost ACK commits retain selected and backup candidates",[&](const auto& dir){settings["asset"]=good;put(fspath::path(assetRoot)/L"background.png",oldPng);const auto source=installSource(dir,newPng);std::set<std::string> names;settingsWriteMode=2;for(int i=0;i<10;i++){expectRefusal(source);const auto current=background_state();need(current["enabled"]==true&&assetBytes(current)==newPng,"lost ACK removed committed candidate");names.insert(current["file"].template get<std::string>());if(settingsBackup.contains("background")&&settingsBackup["background"].contains("asset")&&settingsBackup["background"]["asset"].contains("file"))need(fspath::is_regular_file(fspath::path(assetRoot)/U2W(settingsBackup["background"]["asset"]["file"].template get<std::string>())) ,"lost ACK deleted valid backup reference");need(managedFiles()<=3,"lost ACK commits accumulated unbounded candidates");}need(names.size()==10,"candidate generation pathname was reused");});
 check("background install backup selector retains managed media",[&](const auto& dir){const auto source=installSource(dir,newPng);const auto first=installFile(source);const auto second=installFile(source);need(first["file"]!=second["file"]&&assetBytes(first)==newPng,"backup media changed during next install");settingsWriteMode=1;for(int i=0;i<5;i++)expectRefusal(source);need(assetBytes(first)==newPng&&assetBytes(second)==newPng,"proven main or backup media was reclaimed");});
 check("background install three protected slots refuse safely then explicit clear frees capacity",[&](const auto& dir){const auto source=installSource(dir,newPng);const auto a=installFile(source),b=installFile(source),c=installFile(source);put(background_config_path(),json{{"file",a["file"]},{"kind","image"}}.dump());expectRefusal(source);need(assetBytes(a)==newPng&&assetBytes(b)==newPng&&assetBytes(c)==newPng&&managedFiles()==3,"capacity refusal changed protected media");fixtureClearBackground(json::object());need(assetBytes(installFile(source))==newPng,"explicit selector release could not recover capacity");});
 check("background install unknown root selectors never authorize reclamation",[&](const auto& dir){const auto source=installSource(dir,newPng);installFile(source);settings["asset"]=good;settingsBackup=json();const auto unchanged=tree();const auto calls=settingsWriteCalls;selectorMainOverride=true;for(const auto& raw:std::vector<std::string>{"","{broken","[]","null","{\"schemaVersion\":2}","{\"schemaVersion\":1,\"configurationBaseline\":{\"id\":\"ymcc-settings\",\"revision\":2,\"legacyImported\":true}}","{\"schemaVersion\":1,\"background\":[]}","{\"schemaVersion\":1,\"background\":{\"asset\":false}}","{\"schemaVersion\":1,\"background\":{\"asset\":{\"schemaVersion\":2,\"file\":\"background.png\"}}}","{\"schemaVersion\":1,\"schemaVersi\\u006fn\":1}"}){selectorMainRaw=raw;expectRefusal(source);need(tree()==unchanged,"unknown selector authorized deletion or allocation");}selectorMainOverride=false;selectorMainFault=true;expectRefusal(source);selectorMainFault=false;selectorBackupFault=true;expectRefusal(source);selectorBackupFault=false;selectorBackupOverride=true;selectorBackupRaw="{\"schemaVersion\":2}";expectRefusal(source);selectorBackupOverride=false;need(settingsWriteCalls==calls&&tree()==unchanged,"unproved selectors reached commit");need(assetBytes(installFile(source))==newPng,"selector read recovery failed");});
 check("background install legacy future selector preserves files until explicit repair",[&](const auto& dir){const auto source=installSource(dir,newPng);installFile(source);const auto legacy=std::string("{\"schemaVersion\":2,\"file\":\"future.png\"}");put(background_config_path(),legacy);const auto unchanged=tree();expectRefusal(source);need(tree()==unchanged&&bytes(background_config_path())==legacy,"future legacy selector authorized deletion");put(background_config_path(),good.dump());need(assetBytes(installFile(source))==newPng,"legacy explicit repair failed");});
 check("background install unknown files and foreign slot metadata are retained",[&](const auto& dir){const auto source=installSource(dir,newPng);const auto foreign=fspath::path(assetRoot)/L"background-static-2-foreign.png";put(foreign,"unknown user file");const auto metadata=fspath::path(assetRoot)/L".ymcc-static-background-slot0.json";const auto future=std::string("{\"schemaVersion\":2,\"owner\":\"YMCC.static-background.v1\"}");put(metadata,future);need(assetBytes(installFile(source))==newPng&&bytes(foreign)=="unknown user file"&&bytes(metadata)==future,"unknown ownership was overwritten or scanned for deletion");});
 check("background install file identity mismatch refuses foreign replacement cleanup",[&](const auto& dir){const auto source=installSource(dir,newPng);const auto first=installFile(source);const auto path=fspath::path(assetRoot)/U2W(first["file"].template get<std::string>());fspath::rename(path,dir/L"retained-owner.png");put(path,"foreign replacement bytes");settings["asset"]=good;settingsBackup=json();need(assetBytes(installFile(source))==newPng&&bytes(path)=="foreign replacement bytes"&&bytes(dir/L"retained-owner.png")==newPng,"file identity guard deleted unrelated bytes");});
 check("background install copy staging collisions preserve unknown pending files",[&](const auto& dir){const auto source=installSource(dir,newPng);for(int i=0;i<3;i++)put(fspath::path(assetRoot)/(L".ymcc-static-background-slot"+std::to_wstring(i)+L".pending"),"unknown staging");expectRefusal(source);for(int i=0;i<3;i++){const auto p=fspath::path(assetRoot)/(L".ymcc-static-background-slot"+std::to_wstring(i)+L".pending");need(bytes(p)=="unknown staging","staging conflict was overwritten");fspath::remove(p);}need(assetBytes(installFile(source))==newPng,"explicit retry after fixture releases staging failed");});
 check("background install source equals old or managed selected file never clobbers source",[&](const auto&){const auto old=fspath::path(assetRoot)/L"background.png";put(old,oldPng);settings["asset"]=good;const auto first=installFile(old);need(bytes(old)==oldPng&&assetBytes(first)==oldPng,"source equals legacy target was clobbered");const auto managed=fspath::path(assetRoot)/U2W(first["file"].template get<std::string>());const auto second=installFile(managed);need(bytes(managed)==oldPng&&assetBytes(second)==oldPng&&first["file"]!=second["file"],"managed source was reclaimed during own copy");});
 check("background install transaction lease lock refuses then explicitly recovers",[&](const auto& dir){const auto source=installSource(dir,newPng);const auto lock=fspath::path(assetRoot)/L".ymcc-static-background.lock";HANDLE held=CreateFileW(lock.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);need(held!=INVALID_HANDLE_VALUE,"fixture could not hold lease");try{expectRefusal(source);need(settingsWriteCalls==0&&managedFiles()==0,"busy transaction reached copy or selector commit");}catch(...){CloseHandle(held);throw;}CloseHandle(held);need(assetBytes(installFile(source))==newPng,"lease release retry failed");});
 check("background install locked slot ownership is retained and another slot remains usable",[&](const auto& dir){const auto source=installSource(dir,newPng);const auto first=installFile(source);settings["asset"]=good;settingsBackup=json();const auto metadata=fspath::path(assetRoot)/L".ymcc-static-background-slot0.json";HANDLE held=CreateFileW(metadata.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);need(held!=INVALID_HANDLE_VALUE,"fixture could not hold metadata");json second;try{second=installFile(source);need(assetBytes(first)==newPng&&assetBytes(second)==newPng,"locked metadata authorized collection");}catch(...){CloseHandle(held);throw;}CloseHandle(held);need(assetBytes(installFile(source))==newPng,"metadata sharing release retry failed");});
 check("background install real publish collision never replaces foreign target",[&](const auto& dir){const auto source=installSource(dir,newPng);settings["asset"]=good;mediaMoveConflict=true;expectRefusal(source);mediaMoveConflict=false;need(!mediaConflictPath.empty()&&bytes(mediaConflictPath)=="unknown foreign target"&&settings["asset"]==good,"publish collision replaced foreign target or selected it");need(assetBytes(installFile(source))==newPng,"publish collision release retry failed");});
 check("background install readback refuses altered staging before settings commit",[&](const auto& dir){const auto source=installSource(dir,newPng);settings["asset"]=good;corruptMediaWrite=true;expectRefusal(source);corruptMediaWrite=false;need(settingsWriteCalls==0&&settings["asset"]==good,"unverified copy reached settings commit");need(assetBytes(installFile(source))==newPng,"copy readback failure did not recover");});
 check("background install source locked or cancellation does not allocate or commit",[&](const auto& dir){const auto source=installSource(dir,newPng);HANDLE held=CreateFileW(source.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);need(held!=INVALID_HANDLE_VALUE,"fixture could not hold source");try{expectRefusal(source);}catch(...){CloseHandle(held);throw;}CloseHandle(held);expectRefusal(fspath::path{});need(settingsWriteCalls==0&&managedFiles()==0,"source refusal or cancellation performed install");need(assetBytes(installFile(source))==newPng,"source lock release retry failed");});
 check("background install acknowledged selector must actually read back candidate",[&](const auto& dir){const auto source=installSource(dir,newPng);settings["asset"]=good;settingsWriteMode=4;expectRefusal(source);need(settings["asset"]==good,"false success selector replaced old state");settingsWriteMode=0;need(assetBytes(installFile(source))==newPng,"selector confirmation retry failed");});
 check("background clear success or lost ACK never deletes backup or legacy media",[&](const auto& dir){const auto source=installSource(dir,newPng);const auto first=installFile(source),second=installFile(source);put(background_config_path(),good.dump());const auto legacy=bytes(fspath::path(assetRoot)/L"background.png");settingsWriteMode=2;bool refused=false;try{fixtureClearBackground(json::object());}catch(...){refused=true;}need(refused&&off(background_state())&&assetBytes(first)==newPng&&assetBytes(second)==newPng&&bytes(background_config_path())==good.dump()&&bytes(fspath::path(assetRoot)/L"background.png")==legacy,"uncertain clear deleted media");settingsWriteMode=0;need(off(fixtureClearBackground(json::object()))&&assetBytes(second)==newPng,"successful clear deleted referenced data");});
 check("background install slot ownership commit requires exact readback",[&](const auto& dir){const auto source=installSource(dir,newPng);settings["asset"]=good;corruptSlotRecord=true;expectRefusal(source);corruptSlotRecord=false;need(settingsWriteCalls==0&&managedFiles()==0,"unconfirmed slot ownership reached copy or selector commit");need(assetBytes(installFile(source))==newPng,"unknown corrupted slot did not leave another usable slot");});
 check("background install fixed unknown record staging remains untouched and bounded",[&](const auto& dir){const auto source=installSource(dir,newPng);for(int i=0;i<3;i++)put(fspath::path(assetRoot)/(L".ymcc-static-background-slot"+std::to_wstring(i)+L".json.new"),"unknown metadata staging");expectRefusal(source);need(settingsWriteCalls==0&&managedFiles()==0,"unknown metadata staging reached a large copy");for(int i=0;i<3;i++){const auto file=fspath::path(assetRoot)/(L".ymcc-static-background-slot"+std::to_wstring(i)+L".json.new");need(bytes(file)=="unknown metadata staging","unknown metadata staging overwritten");fspath::remove(file);}need(assetBytes(installFile(source))==newPng,"explicit metadata staging release did not recover");});
 check("background install wrong slot types never authorize owned file reclamation",[&](const auto& dir){const auto source=installSource(dir,newPng);const auto first=installFile(source);const auto metadata=fspath::path(assetRoot)/L".ymcc-static-background-slot0.json";const auto original=json::parse(bytes(metadata));settings["asset"]=good;settingsBackup=json();for(const auto& bad:std::vector<json>{json(4294967296ULL),json(0.0),json("0"),json(false)}){auto record=original;record["slot"]=bad;put(metadata,record.dump());installFile(source);need(assetBytes(first)==newPng&&bytes(metadata)==record.dump(),"wrong slot type authorized media or metadata replacement");settings["asset"]=good;settingsBackup=json();}});
 check("background install missing ownership records do not accumulate orphan generations",[&](const auto& dir){const auto source=installSource(dir,newPng);std::vector<json> retained;for(int i=0;i<3;i++){const auto state=installFile(source);retained.push_back(state);bool removed=false;for(int slot=0;slot<3;slot++){const auto metadata=fspath::path(assetRoot)/(L".ymcc-static-background-slot"+std::to_wstring(slot)+L".json");if(fspath::exists(metadata)){const auto record=json::parse(bytes(metadata));if(record["file"]==state["file"]){fspath::remove(metadata);removed=true;}}}need(removed,"fixture failed to remove exact owned metadata");settings["asset"]=good;settingsBackup=json();}expectRefusal(source);need(managedFiles()==3,"lost metadata allowed unbounded orphan generations");for(const auto& state:retained)need(assetBytes(state)==newPng,"lost ownership caused deletion of an unknown generation");});
 check("background install recorded interrupted pending is reclaimed only by file identity",[&](const auto& dir){const auto source=installSource(dir,newPng);const auto first=installFile(source);const auto file=fspath::path(assetRoot)/U2W(first["file"].template get<std::string>());const auto pending=fspath::path(assetRoot)/L".ymcc-static-background-slot0.pending";fspath::rename(file,pending);fspath::resize_file(pending,8);settings["asset"]=good;settingsBackup=json();need(assetBytes(installFile(source))==newPng&&!fspath::exists(pending)&&managedFiles()==1,"recorded interrupted pending was not safely reclaimed");});
 check("background install Windows selector aliases retain selected owned media",[&](const auto& dir){const auto source=installSource(dir,newPng);const auto first=installFile(source);const auto original=settings["asset"];auto upper=first["file"].template get<std::string>();std::transform(upper.begin(),upper.end(),upper.begin(),::toupper);const auto file=first["file"].template get<std::string>();settingsBackup=json();settingsWriteMode=1;for(const auto& alias:std::vector<std::string>{upper,file+".",file+" "}){settings["asset"]=original;settings["asset"]["file"]=alias;need(background_state()["enabled"]==true&&assetBytes(background_state())==newPng,"fixture alias did not select the actual Windows media");for(int i=0;i<4;i++)expectRefusal(source);need(assetBytes(first)==newPng&&background_state()["enabled"]==true,"Windows selector alias authorized deletion of selected media");}settings["asset"]=original;settingsWriteMode=0;need(assetBytes(installFile(source))==newPng,"explicit canonical selector retry did not recover");});
 check("background install slot record extensions survive known owner reuse",[&](const auto& dir){const auto source=installSource(dir,newPng);installFile(source);const auto file=fspath::path(assetRoot)/L".ymcc-static-background-slot0.json";auto record=json::parse(bytes(file));record["extension"]={{"keep",17}};put(file,record.dump());settings["asset"]=good;settingsBackup=json();installFile(source);need(json::parse(bytes(file))["extension"]==record["extension"],"slot reuse discarded unknown record extension");});
 check("background clear throwing port preserves media and permits explicit retry",[&](const auto&){settings["asset"]=good;put(fspath::path(assetRoot)/L"background.png",oldPng);settingsWriteMode=3;bool refused=false;try{fixtureClearBackground(json::object());}catch(...){refused=true;}need(refused&&settings["asset"]==good&&bytes(fspath::path(assetRoot)/L"background.png")==oldPng,"throwing clear deleted selected media");settingsWriteMode=0;need(off(fixtureClearBackground(json::object())),"explicit clear retry failed");});

`:'')+String.raw`
 if(cases.empty()||(!selected.empty()&&selected!="@install"&&cases.size()!=1))return 3;unsigned failed=0;for(const auto& c:cases)if(!c["ok"].get<bool>())++failed;std::cout<<json{{"cases",cases},{"passed",cases.size()-failed},{"failed",failed}}.dump(2);return failed?1:0;
}catch(const std::exception& error){std::cerr<<error.what();return 2;}}
`;
 const scopedCpp=cpp.replace('#include "json.hpp"','#include "json.hpp"\n#include "'+path.join(root,'native/settings_baseline.h').replaceAll('\\','/')+'"');
 fs.writeFileSync(path.join(out,'config-fixture.cpp'),scopedCpp);
 fs.writeFileSync(path.join(out,'config-source-identity.json'),JSON.stringify({mode:'generated-not-run',sourcePath,sourceSha256:createHash('sha256').update(fs.readFileSync(sourcePath)).digest('hex'),functions:names,functionHashes:Object.fromEntries(names.map(n=>[n,createHash('sha256').update(backgroundProductionFunction(n)).digest('hex')])),ipcHashes:{install:createHash('sha256').update(installBody).digest('hex'),clear:createHash('sha256').update(clearBody).digest('hex')},transactionSha256:transaction?createHash('sha256').update(transaction).digest('hex'):null,additionalInputs:[{path:path.join(root,'native/settings_baseline.h'),sha256:createHash('sha256').update(fs.readFileSync(path.join(root,'native/settings_baseline.h'))).digest('hex')}],ports:['declared main/backup settings snapshots under verified lock; patch false/throw/commit-then-false', 'owned assetRoot/default video directory','raw unified section JSON','actual Windows file API only under owned roots'],newDefaultOrParent:false},null,2));
}
