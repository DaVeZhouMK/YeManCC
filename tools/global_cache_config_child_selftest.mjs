// Extract actual child storage routines. All data stays in a unique scratch fixture;
// production process/Steam entry points and named mutexes are never executed.
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const root=process.cwd(),base=process.env.YMCC_GLOBAL_CONFIG_ARTIFACT_ROOT?path.join(process.env.YMCC_GLOBAL_CONFIG_ARTIFACT_ROOT,'child-storage'):path.resolve(root,'../../../_scratch/global-cache-config-audit-20261006/child-storage');
const sourceRoot=process.env.YMCC_CHILD_STORAGE_SOURCE_ROOT||process.env.YMCC_CHILD_STORAGE_BASELINE||path.join(root,'native/custom-steam-library');
const host=fs.readFileSync(path.join(sourceRoot,'workspace_host.cpp'),'utf8'),worker=fs.readFileSync(path.join(sourceRoot,'steam_artwork_lab.cpp'),'utf8');
function fn(s,n){const a=s.search(new RegExp('^static [^\\n]+ '+n+'\\(', 'm'));if(a<0)throw Error('Missing '+n);const b=s.indexOf('\n}',a);return s.slice(a,b+2)}
function optional(s,n){return s.includes(' '+n+'(')?fn(s,n):''}
fs.mkdirSync(base,{recursive:true});const out=path.join(base,process.env.YMCC_CHILD_STORAGE_BASELINE?'red':'current');fs.mkdirSync(out,{recursive:true});
const cpp=String.raw`#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <mutex>
#include <chrono>
#include <optional>
#include <vector>
#include <functional>
#include <algorithm>
#include <iostream>
#include "json.hpp"
namespace fs=std::filesystem;
using json=nlohmann::json;
namespace custom_steam_library {static fs::path ioPath(const fs::path& p){return p;}}
static fs::path g_dataRoot;
static std::mutex g_jsonWriteMutex;
static std::atomic<uint64_t> g_jsonBackupSequence{0};
static std::recursive_mutex fixtureMutex;
class HostDataTransaction {std::lock_guard<std::recursive_mutex> lock{fixtureMutex};};
class DataTransactionMutex {std::lock_guard<std::recursive_mutex> lock{fixtureMutex};};
static fs::path failReadPath;static bool failStage=false,failBackup=false,failArchive=false,failActivation=false,failWrite=false;
static std::string toUtf8(const std::wstring& s){return std::string(s.begin(),s.end());}
static std::wstring toWide(const std::string& s){return std::wstring(s.begin(),s.end());}
static std::string canonicalPathKey(const fs::path& p){return p.generic_string();}
static uint64_t unixTimeMs(){return GetTickCount64();}
static int64_t unixTimeMsHost(){return GetTickCount64();}
struct HostIoHandle {HANDLE value=INVALID_HANDLE_VALUE;~HostIoHandle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
`+optional(host,'hostRegularFile')+optional(host,'hostUserConfigPath')+optional(host,'hostConfigDocumentValid')+fn(host,'readText').replace('readText(', 'fixtureRealReadText(')+String.raw`
static std::string readText(const fs::path& p,size_t maximum=64u<<20){if(p==failReadPath)throw std::runtime_error("fixture unreadable");return fixtureRealReadText(p,maximum);}
static void writeHostBytes(const fs::path& p,std::string_view s){if(failStage)throw std::runtime_error("fixture stage failure");fs::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);out.write(s.data(),s.size());if(!out)throw std::runtime_error("write failure");}
struct HostStagedFile {fs::path path;~HostStagedFile(){std::error_code e;fs::remove(path,e);}};
static fs::path hostTemporaryPath(const fs::path& p){return p.parent_path()/(p.filename().wstring()+L".tmp-"+std::to_wstring(++g_jsonBackupSequence));}
static void activateHostStagedFile(const fs::path& p,const fs::path& t){if(failActivation||(failBackup&&t.extension()==L".bak")||(failArchive&&t.parent_path().filename()==L"corrupt"))throw std::runtime_error("fixture activation failure");fs::copy_file(p,t,fs::copy_options::overwrite_existing);fs::remove(p);}
static void copyHostFileAtomic(const fs::path& p,const fs::path& t){HostStagedFile stage{hostTemporaryPath(t)};writeHostBytes(stage.path,readText(p));activateHostStagedFile(stage.path,t);}
`+fn(host,'hostCentralBackupDirectory')+fn(host,'nextHostBackupPath')+optional(host,'writeHostBytesAtomic')+fn(host,'jsonRecoveryCandidates')+fn(host,'readJson')+fn(host,'writeJsonWithLocalBackup')+String.raw`
static std::vector<unsigned char> readBinaryFile(const fs::path& p,size_t maxBytes=64u<<20){auto s=readText(p,maxBytes);return {s.begin(),s.end()};}
static void writeAtomic(const fs::path& p,const std::vector<unsigned char>& s){if(failWrite||(failArchive&&p.parent_path().filename()==L"corrupt")||(failBackup&&p.extension()==L".bak"))throw std::runtime_error("fixture atomic failure");writeHostBytes(p,std::string_view(reinterpret_cast<const char*>(s.data()),s.size()));}
static void writeJsonAtomic(const fs::path& p,const json& j){const auto s=j.dump(2);writeAtomic(p,{s.begin(),s.end()});}
`+fn(worker,'sortRecoveryCandidates')+optional(worker,'configRegularFile')+optional(worker,'configRecoveryCandidates')+optional(worker,'nextConfigBackupPath')+optional(worker,'loadUserConfigDocument')+optional(worker,'applyConfigDelta')+fn(worker,'loadManualOverrides')+fn(worker,'defaultLibraryConfig')+fn(worker,'loadLibraryConfig')+optional(worker,'configDocumentValid')+fn(worker,'writeJsonWithBackup')+String.raw`
static void require(bool v){if(!v)throw std::runtime_error("assertion failed");}
static bool rejected(const std::function<void()>& fn){try{fn();return false;}catch(...){return true;}}
static std::vector<json> cases;
int wmain(int argc,wchar_t** argv){if(argc!=2)return 2;fs::path fixture=argv[1];fs::create_directories(fixture);size_t number=0;
 auto test=[&](const char* name,const std::function<void(const fs::path&)>& action){failReadPath.clear();failStage=failBackup=failArchive=failActivation=failWrite=false;g_dataRoot=fixture/std::to_wstring(++number);fs::create_directories(g_dataRoot/L"config");try{action(g_dataRoot/L"config"/L"library-config.json");cases.push_back({{"name",name},{"ok",true}});}catch(const std::exception& e){cases.push_back({{"name",name},{"ok",false},{"error",e.what()}});}};
 auto text=[](const fs::path& p,const std::string& s){fs::create_directories(p.parent_path());std::ofstream(p,std::ios::binary)<<s;};
 auto backup=[](const fs::path& p){return fs::path(p.wstring()+L".bak");};
 test("host corrupt main never replaces valid backup",[&](const auto& p){text(p,"{bad");text(backup(p),"{\"safe\":42}");writeJsonWithLocalBackup(p,{{"safe",43}});require(readText(backup(p))=="{\"safe\":42}");require(fs::is_directory(g_dataRoot/L"backups"/L"corrupt"));});
 test("host backup failure keeps original main",[&](const auto& p){text(p,"{\"safe\":42}");failBackup=true;require(rejected([&]{writeJsonWithLocalBackup(p,{{"safe",43}});}));require(readText(p)=="{\"safe\":42}");});
 test("host unreadable main cannot be replaced",[&](const auto& p){text(p,"{\"safe\":42}");failReadPath=p;require(rejected([&]{writeJsonWithLocalBackup(p,{{"safe",43}});}));failReadPath.clear();require(readText(p)=="{\"safe\":42}");});
 test("host unreadable main is not a fallback",[&](const auto& p){text(p,"{\"safe\":42}");text(backup(p),"{\"safe\":40}");failReadPath=p;require(rejected([&]{readJson(p,json::object());}));});
 test("host unreadable recovery backup is not defaults",[&](const auto& p){text(p,"{");text(backup(p),"{\"safe\":42}");failReadPath=backup(p);require(rejected([&]{readJson(p,json::object());}));});
 test("host all corrupt user config is not empty defaults",[&](const auto& p){text(p,"{");text(backup(p),"[");require(rejected([&]{readJson(p,json::object());}));});
 test("host missing first run config still returns fallback",[&](const auto& p){require(readJson(p,{{"default",true}})["default"]==true);});
 test("host nonregular main is not missing",[&](const auto& p){fs::create_directory(p);require(rejected([&]{readJson(p,json::object());}));});
 test("host parse corruption recovers valid backup",[&](const auto& p){text(p,"{");text(backup(p),"{\"safe\":42}");require(readJson(p,json::object())["safe"]==42);});
 test("host archive failure prevents overwrite",[&](const auto& p){text(p,"{");text(backup(p),"{\"safe\":42}");failArchive=true;require(rejected([&]{writeJsonWithLocalBackup(p,{{"safe",43}});}));require(readText(p)=="{"&&readText(backup(p))=="{\"safe\":42}");});
 test("host activation failure keeps main",[&](const auto& p){text(p,"{\"safe\":42}");failActivation=true;require(rejected([&]{writeJsonWithLocalBackup(p,{{"safe",43}});}));require(readText(p)=="{\"safe\":42}");});
 test("host valid main does not enumerate broken backup directory",[&](const auto& p){text(p,"{\"safe\":42}");text(g_dataRoot/L"backups","not a directory");require(readJson(p,json::object())["safe"]==42);});
 test("host corrupt cache remains disposable",[&](const auto&){auto p=g_dataRoot/L"cache"/L"manifest.json";text(p,"{");require(readJson(p,{{"cacheMiss",true}})["cacheMiss"]==true);});
 test("worker library read error never recovers defaults",[&](const auto& p){text(p,"{\"unknown\":42}");failReadPath=p;require(rejected([&]{loadLibraryConfig(p);}));failReadPath.clear();require(readText(p)=="{\"unknown\":42}");require(!fs::exists(g_dataRoot/L"backups"));});
 test("worker manual read error never recovers empty items",[&](const auto& p){text(p,"{\"items\":{\"keep\":42}}");failReadPath=p;require(rejected([&]{loadManualOverrides(p);}));failReadPath.clear();require(readText(p)=="{\"items\":{\"keep\":42}}");});
 test("worker unreadable backup never manufactures defaults",[&](const auto& p){text(p,"{");text(backup(p),"{\"unknown\":42}");failReadPath=backup(p);require(rejected([&]{loadLibraryConfig(p);}));failReadPath.clear();require(readText(p)=="{");});
 test("worker recovery commit failure is propagated",[&](const auto& p){text(p,"{");text(backup(p),"{\"unknown\":42}");failWrite=true;require(rejected([&]{loadLibraryConfig(p);}));require(readText(p)=="{");});
 test("worker archive failure is propagated",[&](const auto& p){text(p,"{");text(backup(p),"{\"unknown\":42}");failArchive=true;require(rejected([&]{loadLibraryConfig(p);}));require(readText(p)=="{");});
 test("worker corrupt main never replaces valid backup",[&](const auto& p){text(p,"{");text(backup(p),"{\"safe\":42}");writeJsonWithBackup(p,g_dataRoot/L"backups"/L"config","library-config",{{"safe",43}});require(readText(backup(p))=="{\"safe\":42}");});
 test("worker backup failure preserves main",[&](const auto& p){text(p,"{\"safe\":42}");failBackup=true;require(rejected([&]{writeJsonWithBackup(p,g_dataRoot/L"backups"/L"config","library-config",{{"safe",43}});}));require(readText(p)=="{\"safe\":42}");});
 test("worker library unknown fields survive normalization",[&](const auto& p){text(p,"{\"unknown\":{\"keep\":42}}");require(loadLibraryConfig(p)["unknown"]["keep"]==42);});
 test("worker manual unknown fields survive normalization",[&](const auto& p){text(p,"{\"items\":{\"game\":{\"unknown\":42}},\"extra\":true}");auto j=loadManualOverrides(p);require(j["items"]["game"]["unknown"]==42&&j["extra"]==true);});
 test("worker corrupt main recovers backup",[&](const auto& p){text(p,"{");text(backup(p),"{\"unknown\":42}");require(loadLibraryConfig(p)["unknown"]==42);});
 test("worker unchanged config does not churn backup",[&](const auto& p){text(p,"{\"safe\":42,\"updatedAt\":1}");writeJsonWithBackup(p,g_dataRoot/L"backups"/L"config","library-config",{{"safe",42},{"updatedAt",2}});require(!fs::exists(backup(p))&&readText(p)=="{\"safe\":42,\"updatedAt\":1}");});

 test("host actual sharing lock never returns defaults",[&](const auto& p){text(p,"{\"safe\":42}");HANDLE lock=CreateFileW(p.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);require(lock!=INVALID_HANDLE_VALUE);bool blocked=rejected([&]{readJson(p,json::object());});CloseHandle(lock);require(blocked);});
 test("host schema-invalid object is not a recovery backup",[&](const auto& p){text(p,"{\"roots\":null}");text(backup(p),"{\"roots\":[]}");writeJsonWithLocalBackup(p,{{"roots",json::array()}});require(readText(backup(p))=="{\"roots\":[]}");});
 test("worker schema-invalid object preserves last valid backup",[&](const auto& p){text(p,"{\"roots\":null}");text(backup(p),"{\"roots\":[]}");writeJsonWithBackup(p,g_dataRoot/L"backups"/L"config","library-config",{{"roots",json::array()}});require(readText(backup(p))=="{\"roots\":[]}");});
 test("worker damaged user config without recovery blocks defaults",[&](const auto& p){text(p,"{");require(rejected([&]{loadLibraryConfig(p);}));require(readText(p)=="{");});
 test("worker nonregular config blocks fallback",[&](const auto& p){fs::create_directory(p);require(rejected([&]{loadLibraryConfig(p);}));});
 test("worker missing initial config is allowed",[&](const auto& p){require(loadLibraryConfig(p)["enabled"]==false&&!fs::exists(p));});
`+(worker.includes('const json* baseline = nullptr')?String.raw`

 test("worker stale section edit preserves external and unknown fields",[&](const auto& p){json baseline={{"enabled",false},{"manualPrimary",json::object()},{"unknown",1}};json latest=baseline;latest["unknown"]=42;latest["manualPrimary"]["external"]={{"exe","other.exe"}};text(p,latest.dump());auto next=baseline;next["enabled"]=true;auto saved=writeJsonWithBackup(p,g_dataRoot/L"backups"/L"config","library-config",next,&baseline);require(saved["unknown"]==42&&saved["manualPrimary"]["external"]["exe"]=="other.exe"&&saved["enabled"]==true);});
 test("worker stale nested edit preserves other game and deletion",[&](const auto& p){json baseline={{"items",{{"game",{{"name","old"},{"removed",true}}},{"second",{{"old",true}}}}}};json latest=baseline;latest["items"]["game"]["external"]=42;latest["items"].erase("second");text(p,latest.dump());auto next=baseline;next["items"]["game"]["name"]="new";next["items"]["game"].erase("removed");auto saved=writeJsonWithBackup(p,g_dataRoot/L"backups"/L"config","manual-overrides",next,&baseline);require(saved["items"]["game"]["external"]==42&&saved["items"]["game"]["name"]=="new"&&!saved["items"]["game"].contains("removed")&&!saved["items"].contains("second"));});
 test("worker concurrent collection edit fails without losing entries",[&](const auto& p){json baseline={{"roots",json::array({"old"})}};text(p,json{{"roots",json::array({"old","external"})}}.dump());auto next=baseline;next["roots"].push_back("submitted");const auto old=readText(p);require(rejected([&]{writeJsonWithBackup(p,g_dataRoot/L"backups"/L"config","library-config",next,&baseline);}));require(readText(p)==old&&!fs::exists(backup(p)));});
`:String.raw``)+String.raw`
 int failed=0;for(auto& c:cases)if(!c["ok"].get<bool>())failed++;std::cout<<json{{"passed",cases.size()-failed},{"failed",failed},{"cases",cases},{"installedDataTouched",false},{"liveMutexUsed",false}}.dump(2)<<"\n";return failed?1:0;
}
`;
const file=path.join(out,'child-storage.cpp');fs.writeFileSync(file,cpp);
const vc='C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat';
const script=path.join(out,'compile.cmd');fs.writeFileSync(script,`@echo off\r\ncall "${vc}" >nul && cl /nologo /EHsc /std:c++20 /utf-8 /I"${path.join(root,'deps/json')}" "${file}" /Fo:"${path.join(out,'child-storage.obj')}" /Fe:"${path.join(out,'child-storage.exe')}"\r\nexit /b %errorlevel%\r\n`);
const build=spawnSync('cmd.exe',['/d','/c',script],{encoding:'utf8'});process.stdout.write(build.stdout||'');process.stderr.write(build.stderr||'');if(build.status!==0)process.exit(build.status||1);
const fixture=path.join(out,'fixture-'+Date.now());const run=spawnSync(path.join(out,'child-storage.exe'),[fixture],{encoding:'utf8'});process.stdout.write(run.stdout||'');process.stderr.write(run.stderr||'');fs.writeFileSync(path.join(out,'results.json'),run.stdout||'');fs.writeFileSync(path.join(out,'identity.json'),JSON.stringify({hostSha256:createHash('sha256').update(host).digest('hex'),workerSha256:createHash('sha256').update(worker).digest('hex'),fixture},null,2));process.exitCode=run.status||0;
