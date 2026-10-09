// Extract current production shared-settings functions for an isolated C++ test.
// Mutex/filesystem mocks never touch the installed app, its mutex or hardware.
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const root=process.cwd(),source=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
const out=process.env.YMCC_GLOBAL_CONFIG_ARTIFACT_ROOT?path.join(process.env.YMCC_GLOBAL_CONFIG_ARTIFACT_ROOT,'native-current'):path.resolve(root,'../../../_scratch/global-cache-config-audit-20261006/native-current');fs.mkdirSync(out,{recursive:true});
function fn(name){const a=source.search(new RegExp('^static [^\\n]+ '+name+'\\(', 'm'));if(a<0)throw Error('Missing '+name);const b=source.indexOf('\n}',a);if(b<0)throw Error('Missing function end '+name);return source.slice(a,b+2)}
const guard=source.slice(source.indexOf('class SettingsFileGuard {'),source.indexOf('\n};',source.indexOf('class SettingsFileGuard {'))+3);
const names=['ymSettingsApplyDelta','ymSettingsFillMissing','ymSettingsReadUnlocked','ymSettingsWriteDocumentUnlocked','ymSettingsWriteDocument','ymSettingsReadSnapshot','ymSettingsResetApplication','ymSettingsRead','ymSettingsWriteSection','ymSettingsSection','ymSettingsPatchSection'];
// ymSettingsSection has a forward declaration before the definition.
function body(name){if(name!=='ymSettingsSection')return fn(name);const a=source.indexOf('static json ymSettingsSection(const char* section) {');return source.slice(a,source.indexOf('\n}',a)+2)}
const rulesPrefix=source.slice(source.indexOf('static const std::wstring SG_GAME_RULE_JOURNAL'),source.indexOf('static bool sgRestoreGameRuleFile('));
const rulesFunctions=['sgRestoreGameRuleFile','sgRecoverGameRuleFilesUnlocked','sgReadGameRulesText','sgGameRuleFileContent','sgWriteGameRuleFiles'];
const raw=fn('ymSettingsReadRaw');
function ipcBody(name){const start=source.indexOf('    ipc_on("'+name+'",');if(start<0)throw Error('Missing native IPC '+name);const begin=source.indexOf('{',start),end=source.indexOf('\n    });',begin);if(end<0)throw Error('Missing IPC end');return source.slice(begin+1,end)}
const pathWire=fn('normalizeWindowsPathForCompare')+'\n'+fn('sameWindowsPath')+'\nstatic std::wstring U2W(const std::string& s){return std::wstring(s.begin(),s.end());}\nstatic const std::wstring POWER_CONTROL_DIR=L"fixture-native-root";\n'+['location','read','reset'].map(name=>'static json fixtureSettings'+name+'(const json& a){'+ipcBody('settings.'+name)+'\n}').join('\n');

const cpp=String.raw`#define NOMINMAX
#include <windows.h>
#include <string>
#include <map>
#include <mutex>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <system_error>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
`+raw.replace('ymSettingsReadRaw(', 'fixtureRealReadRaw(')+String.raw`
static const std::wstring YM_SETTINGS_FILE=L"memory-only-main";
static const std::wstring YM_SETTINGS_BACKUP_FILE=L"memory-only-backup";
static const std::wstring SG_DIR=L"memory-only-rules";
static const std::wstring SG_GAME_PLAYER_BLACKLIST=L"memory-blacklist";
static const std::wstring SG_GAME_WHITELIST=L"memory-whitelist";
static std::map<std::wstring,std::string> files;
static bool failWhiteNew=false,failBlackOld=false,failJournalPrepare=false,failJournalCommit=false,failJournalDelete=false,failRulesRead=false;
static int journalWrites=0;
namespace fspath {
static void create_directories(const std::wstring&,std::error_code& ec){ec.clear();}
static bool exists(const std::wstring& p,std::error_code& ec){ec.clear();return files.count(p)>0;}
static bool exists(const std::wstring& p){return files.count(p)>0;}
}
static std::string W2U(const std::wstring& v){return std::string(v.begin(),v.end());}
static bool failBackup=false,failMain=false,failCorrupt=false,failRead=false,failReadBackup=false;
static bool failMutexCreate=false;static DWORD mutexWait=WAIT_OBJECT_0;
static unsigned long long stamp=1;static int rawReads=0;
static std::mutex g_settingsFileMtx;
static std::string ymSettingsReadRaw(const std::wstring& p){rawReads++;if(p==SG_GAME_PLAYER_BLACKLIST&&failRulesRead)throw std::runtime_error("rules unreadable");if((p==YM_SETTINGS_FILE&&failRead)||(p==YM_SETTINGS_BACKUP_FILE&&failReadBackup))throw std::runtime_error("fixture unreadable main/backup");return files.count(p)?files[p]:std::string{};}
static bool sgWriteFileAtomic(const std::wstring& p,const std::string& s){
 if(p==SG_DIR+L"\\game-rules.transaction.json") {
  journalWrites++;
  if((failJournalPrepare&&journalWrites==1)||(failJournalCommit&&journalWrites==2))return false;
 }
 if(p==SG_GAME_WHITELIST&&s.starts_with("new-white")&&failWhiteNew){failWhiteNew=false;return false;}
 if(p==SG_GAME_PLAYER_BLACKLIST&&s.starts_with("old-black")&&failBlackOld)return false;
 if((p==YM_SETTINGS_BACKUP_FILE&&failBackup)||(p==YM_SETTINGS_FILE&&failMain)||(p!=YM_SETTINGS_FILE&&p!=YM_SETTINGS_BACKUP_FILE&&failCorrupt))return false;
 files[p]=s;if(p==YM_SETTINGS_FILE)stamp++;return true;
}
static BOOL fixtureDeleteFileW(LPCWSTR p){if(std::wstring(p)==SG_DIR+L"\\game-rules.transaction.json"&&failJournalDelete){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}if(files.erase(p))return TRUE;SetLastError(ERROR_FILE_NOT_FOUND);return FALSE;}
static HANDLE fixtureCreateMutexW(LPSECURITY_ATTRIBUTES,BOOL,LPCWSTR){return failMutexCreate?nullptr:reinterpret_cast<HANDLE>(1);}
static DWORD fixtureWaitForSingleObject(HANDLE,DWORD){return mutexWait;}
static BOOL fixtureReleaseMutex(HANDLE){return TRUE;}
static BOOL fixtureCloseHandle(HANDLE){return TRUE;}
static BOOL fixtureGetAttributes(LPCWSTR p,GET_FILEEX_INFO_LEVELS,LPVOID target){
 if(!files.count(p)){SetLastError(ERROR_FILE_NOT_FOUND);return FALSE;}
 auto* a=static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(target);*a={};a->nFileSizeLow=static_cast<DWORD>(files[p].size());a->ftLastWriteTime.dwLowDateTime=static_cast<DWORD>(stamp);a->ftCreationTime.dwLowDateTime=1;return TRUE;
}
#define DeleteFileW fixtureDeleteFileW
#define CreateMutexW fixtureCreateMutexW
#define WaitForSingleObject fixtureWaitForSingleObject
#define ReleaseMutex fixtureReleaseMutex
#define CloseHandle fixtureCloseHandle
#define GetFileAttributesExW fixtureGetAttributes
`+guard+'\nstatic json ymSettingsSection(const char* section);\n'+names.map(body).join('\n\n')+'\n'+pathWire+'\n'+rulesPrefix+rulesFunctions.map(fn).join('\n\n')+String.raw`
#undef CloseHandle
#undef DeleteFileW
static json base;
static void reset(){files.clear();g_gameRuleRecoveryNeeded=true;failWhiteNew=failBlackOld=failJournalPrepare=failJournalCommit=failJournalDelete=failRulesRead=false;journalWrites=0;failBackup=failMain=failCorrupt=failRead=failReadBackup=failMutexCreate=false;mutexWait=WAIT_OBJECT_0;rawReads=0;stamp++;
 base={{"schemaVersion",1},{"ui",{{"theme","blue-black"},{"opacity",0.5}}},{"background",{{"asset","old"}}},{"tdp",{{"max",37},{"float",{{"target",120}}}}},{"input",{{"revision",7},{"opaque","input-sentinel"}}},{"fan",{{"opaque","fan-sentinel"}}},{"domainLogs",{{"enabled",true}}},{"steamDeckMouse",{{"receipt","native"}}},{"future",{{"keep",1}}}};files[YM_SETTINGS_FILE]=base.dump();}
static json disk(){return json::parse(files.at(YM_SETTINGS_FILE));}
static bool commit(const json& next){return ymSettingsWriteDocument(next.dump(),nullptr,&base);}
static void require(bool v){if(!v)throw std::runtime_error("invariant failed");}
int main(){json cases=json::array();auto check=[&](const char* name,std::function<void()> f){reset();try{f();cases.push_back({{"name",name},{"ok",true}});}catch(const std::exception& e){cases.push_back({{"name",name},{"ok",false},{"error",e.what()}});}};

 check("native location response exposes exact effective settings paths",[]{auto r=fixtureSettingslocation({});require(r["directory"]==W2U(POWER_CONTROL_DIR)&&r["file"]==W2U(YM_SETTINGS_FILE)&&r["backup"]==W2U(YM_SETTINGS_BACKUP_FILE));});
 check("actual native read handler accepts its canonical path",[]{auto r=fixtureSettingsread({{"path",W2U(YM_SETTINGS_FILE)}});require(json::parse(r["content"].get<std::string>())==base);});
 check("actual native read handler rejects fixed-root alias when backend differs",[]{bool rejected=false;try{fixtureSettingsread({{"path","C:/SOFT/YeMan/PowerControl/yeman-settings.json"}});}catch(...){rejected=true;}require(rejected);});
 check("actual reset handler refuses missing confirmation and wrong path",[]{auto before=files;bool rejected=false;try{fixtureSettingsreset({{"path",W2U(YM_SETTINGS_FILE)}});}catch(...){rejected=true;}require(rejected&&files==before);rejected=false;try{fixtureSettingsreset({{"path","C:/wrong/yeman-settings.json"},{"confirm","reset-application-settings"}});}catch(...){rejected=true;}require(rejected&&files==before);});
 check("incomplete recovery defaults never archive or activate",[]{auto before=files;bool rejected=false;try{ymSettingsResetApplication({{"schemaVersion",1}});}catch(...){rejected=true;}require(rejected&&files==before);});
 check("explicit reset archives exact bytes and preserves all protected and unknown fields",[]{auto d=json{{"schemaVersion",1}};for(auto k:{"ui","background","music","performanceSchedule","gameCustom","tdp","cpu","sleep","quickApps","power","tray","autoclose"})d[k]=json::object();auto old=base;old["schemaVersion"]=99;old["gamepad"]={{"keep",true}};old["startupDesired"]={{"fanControl",true},{"gyroPreset","keep"}};files[YM_SETTINGS_FILE]=old.dump();files[YM_SETTINGS_BACKUP_FILE]="backup-original-bytes";auto r=fixtureSettingsreset({{"path",W2U(YM_SETTINGS_FILE)},{"confirm","reset-application-settings"},{"defaults",d}});require(r["ok"]==true&&r["backups"].size()==2);require(files[U2W(r["backups"][0].get<std::string>())]==old.dump()&&files[U2W(r["backups"][1].get<std::string>())]=="backup-original-bytes");auto now=disk();for(auto k:{"fan","input","gamepad","startupDesired","domainLogs","steamDeckMouse","future"})require(now[k]==old[k]);require(now["schemaVersion"]==1&&!now["tdp"]["autoApply"]["boot"].get<bool>()&&!now["performanceSchedule"]["enabled"].get<bool>());});
 check("recovery archive failure leaves configuration unchanged",[]{auto d=json{{"schemaVersion",1}};for(auto k:{"ui","background","music","performanceSchedule","gameCustom","tdp","cpu","sleep","quickApps","power","tray","autoclose"})d[k]=json::object();auto before=files;failCorrupt=true;bool rejected=false;try{ymSettingsResetApplication(d);}catch(...){rejected=true;}require(rejected&&files==before);});
 check("recovery activation failure preserves old main and original archive",[]{auto d=json{{"schemaVersion",1}};for(auto k:{"ui","background","music","performanceSchedule","gameCustom","tdp","cpu","sleep","quickApps","power","tray","autoclose"})d[k]=json::object();auto before=files[YM_SETTINGS_FILE];failMain=true;bool rejected=false;try{ymSettingsResetApplication(d);}catch(...){rejected=true;}require(rejected&&files[YM_SETTINGS_FILE]==before);bool archived=false;for(auto&[p,v]:files)if(p.find(L".before-user-reset-")!=std::wstring::npos&&v==before)archived=true;require(archived);});
 check("corrupt main recovery retains exact raw bytes and valid backup protected data",[]{auto d=json{{"schemaVersion",1}};for(auto k:{"ui","background","music","performanceSchedule","gameCustom","tdp","cpu","sleep","quickApps","power","tray","autoclose"})d[k]=json::object();files[YM_SETTINGS_FILE]="{broken";files[YM_SETTINGS_BACKUP_FILE]=base.dump();auto r=ymSettingsResetApplication(d);require(r["protectedDataRecovered"]==true&&disk()["fan"]==base["fan"]&&disk()["input"]==base["input"]&&files[U2W(r["backups"][0].get<std::string>())]=="{broken");});
 check("recovery read I/O failure never archives or defaults settings",[]{auto d=json{{"schemaVersion",1}};for(auto k:{"ui","background","music","performanceSchedule","gameCustom","tdp","cpu","sleep","quickApps","power","tray","autoclose"})d[k]=json::object();auto before=files;failRead=true;bool rejected=false;try{ymSettingsResetApplication(d);}catch(...){rejected=true;}require(rejected&&files==before);});
 check("unparseable main and backup need confirmed reset and retain exact originals",[]{auto d=json{{"schemaVersion",1}};for(auto k:{"ui","background","music","performanceSchedule","gameCustom","tdp","cpu","sleep","quickApps","power","tray","autoclose"})d[k]=json::object();files[YM_SETTINGS_FILE]="{broken-main";files[YM_SETTINGS_BACKUP_FILE]="{broken-backup";auto r=fixtureSettingsreset({{"path",W2U(YM_SETTINGS_FILE)},{"confirm","reset-application-settings"},{"defaults",d}});require(r["ok"]==true&&r["protectedDataRecovered"]==false&&r["backups"].size()==2);require(files[U2W(r["backups"][0].get<std::string>())]=="{broken-main"&&files[U2W(r["backups"][1].get<std::string>())]=="{broken-backup");require(disk()["schemaVersion"]==1&&!disk().contains("fan")&&!disk().contains("input"));});
 check("explicit empty existing main is archived rather than treated as absent",[]{auto d=json{{"schemaVersion",1}};for(auto k:{"ui","background","music","performanceSchedule","gameCustom","tdp","cpu","sleep","quickApps","power","tray","autoclose"})d[k]=json::object();files[YM_SETTINGS_FILE]="";files.erase(YM_SETTINGS_BACKUP_FILE);auto r=ymSettingsResetApplication(d);require(r["backups"].size()==1&&files[U2W(r["backups"][0].get<std::string>())].empty()&&disk()["schemaVersion"]==1);});
 check("transaction retains unrelated native changes",[]{auto newer=base;newer["background"]["asset"]="new";files[YM_SETTINGS_FILE]=newer.dump();auto next=base;next["ui"]["theme"]="red-black";require(commit(next));require(disk()["background"]["asset"]=="new");});
 check("transaction merges different leaves in same section",[]{auto newer=base;newer["tdp"]["float"]["target"]=75;files[YM_SETTINGS_FILE]=newer.dump();auto next=base;next["tdp"]["max"]=44;require(commit(next));require(disk()["tdp"]["float"]["target"]==75);});
 check("unknown fields and excluded opaque sections are preserved",[]{auto newer=base;newer["future"]["new"]=2;newer["fan"]["native-extra"]="opaque";newer["input"]["revision"]=8;newer["input"]["opaque"]="new-sentinel";files[YM_SETTINGS_FILE]=newer.dump();auto next=base;next["ui"]["theme"]="red-black";require(commit(next));auto d=disk();require(d["fan"]==newer["fan"]&&d["input"]==newer["input"]&&d["future"]==newer["future"]);});
 check("legacy stale input and native-owned receipts remain protected",[]{auto next=base;next["input"]["revision"]=1;next.erase("domainLogs");next.erase("steamDeckMouse");require(ymSettingsWriteDocument(next.dump()));auto d=disk();require(d["input"]==base["input"]&&d["domainLogs"]==base["domainLogs"]&&d["steamDeckMouse"]==base["steamDeckMouse"]);});
 check("backup failure prevents main commit",[]{auto before=files;failBackup=true;auto next=base;next["ui"]["theme"]="new";require(!commit(next));require(files==before);});
 check("main failure retains main and valid backup",[]{auto before=files[YM_SETTINGS_FILE];failMain=true;auto next=base;next["ui"]["theme"]="new";require(!commit(next));require(files[YM_SETTINGS_FILE]==before&&files[YM_SETTINGS_BACKUP_FILE]==before);});
 check("mutex creation failure prevents commit",[]{failMutexCreate=true;auto before=files;bool rejected=false;try{commit(base);}catch(...){rejected=true;}require(rejected&&files==before);});
 check("mutex wait failure prevents commit",[]{mutexWait=WAIT_FAILED;auto before=files;bool rejected=false;try{commit(base);}catch(...){rejected=true;}require(rejected&&files==before);});
 check("mutex timeout prevents commit",[]{mutexWait=WAIT_TIMEOUT;auto before=files;bool rejected=false;try{commit(base);}catch(...){rejected=true;}require(rejected&&files==before);});
 check("abandoned mutex is acquired and valid document can commit",[]{mutexWait=WAIT_ABANDONED;require(commit(base));});
 check("unreadable main never enters recovery or writes",[]{failRead=true;auto before=files;bool rejected=false;try{commit(base);}catch(...){rejected=true;}require(rejected&&files==before);});
 check("unreadable backup never becomes defaults on missing main",[]{files.erase(YM_SETTINGS_FILE);failReadBackup=true;auto before=files;bool rejected=false;try{commit(base);}catch(...){rejected=true;}require(rejected&&files==before);});
 check("corrupt bytes do not replace valid backup",[]{files[YM_SETTINGS_BACKUP_FILE]=base.dump();files[YM_SETTINGS_FILE]="{broken";require(commit(base));require(json::parse(files[YM_SETTINGS_BACKUP_FILE])==base);});
 check("corrupt archive failure prevents main commit",[]{files[YM_SETTINGS_FILE]="{broken";failCorrupt=true;auto before=files;require(!commit(base));require(files==before);});
 check("committed reply contains native changes",[]{auto newer=base;newer["background"]["asset"]="new";files[YM_SETTINGS_FILE]=newer.dump();auto next=base;next["ui"]["theme"]="new";json result;require(ymSettingsWriteDocument(next.dump(),nullptr,&base,&result));require(result==disk()&&result["background"]["asset"]=="new");});
 check("deleted keys and array replacement are respected",[]{base["ui"]["obsolete"]=true;base["ui"]["array"]={1,2};files[YM_SETTINGS_FILE]=base.dump();auto next=base;next["ui"].erase("obsolete");next["ui"]["array"]={3};require(commit(next));require(!disk()["ui"].contains("obsolete")&&disk()["ui"]["array"]==json::array({3}));});
 check("missing nested object accepts delta without losing disk siblings",[]{auto newer=base;newer["ui"]=json::object();newer["ui"]["external"]=42;files[YM_SETTINGS_FILE]=newer.dump();auto next=base;next["ui"]["theme"]="new";require(commit(next));require(disk()["ui"]["external"]==42&&disk()["ui"]["theme"]=="new");});
 check("native section save rejects backup failure",[]{failBackup=true;auto before=files;require(!ymSettingsWriteSection("music",{{"volume",0.7}}));require(files==before);});
 check("native patch save rejects backup failure",[]{failBackup=true;auto before=files;require(!ymSettingsPatchSection("music",{{"volume",0.7}}));require(files==before);});
 check("metadata unchanged avoids JSON read",[]{auto first=ymSettingsReadSnapshot("");int reads=rawReads;auto next=ymSettingsReadSnapshot(first["stamp"]);require(next["unchanged"]==true&&rawReads==reads&&!next.contains("content"));});
 check("same-length rapid external change is observable",[]{auto first=ymSettingsReadSnapshot("");auto changed=base;changed["background"]["asset"]="NEW";files[YM_SETTINGS_FILE]=changed.dump();stamp++;auto next=ymSettingsReadSnapshot(first["stamp"]);require(next["unchanged"]==false&&json::parse(next["content"].get<std::string>())["background"]["asset"]=="NEW");});
 check("initialization backfills without overwriting newer native fields",[]{json old={{"tdp",{{"max",55}}},{"future",{{"keep",9}}}};files[YM_SETTINGS_FILE]=old.dump();json empty=json::object();require(ymSettingsWriteDocument(base.dump(),nullptr,&empty,nullptr,true));require(disk()["tdp"]["max"]==55&&disk()["future"]["keep"]==9&&disk()["ui"]==base["ui"]);});
 check("strict real file read distinguishes a sharing violation",[]{wchar_t dir[MAX_PATH],name[MAX_PATH];require(GetTempPathW(MAX_PATH,dir)>0);require(GetTempFileNameW(dir,L"YMC",0,name)>0);HANDLE lock=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);require(lock!=INVALID_HANDLE_VALUE);bool rejected=false;try{fixtureRealReadRaw(name);}catch(...){rejected=true;}CloseHandle(lock);DeleteFileW(name);require(rejected);});

 check("game rules journal preparation failure changes neither text file",[]{files[SG_GAME_PLAYER_BLACKLIST]="old-black\n";files[SG_GAME_WHITELIST]="old-white\n";auto before=files;failJournalPrepare=true;require(!sgWriteGameRuleFiles({L"new-black"},{L"new-white"}));require(files==before);});
 check("unreadable game rules are not replaced by empty lists",[]{files[SG_GAME_PLAYER_BLACKLIST]="old-black\n";auto before=files;failRulesRead=true;bool rejected=false;try{sgWriteGameRuleFiles({L"new-black"},{L"new-white"});}catch(...){rejected=true;}require(rejected&&files==before);});
 check("second game-rule file failure rolls back both files",[]{files[SG_GAME_PLAYER_BLACKLIST]="old-black\n";files[SG_GAME_WHITELIST]="old-white\n";auto before=files;failWhiteNew=true;require(!sgWriteGameRuleFiles({L"new-black"},{L"new-white"}));require(files==before);});
 check("failed game-rule rollback retains recoverable journal and next read restores it",[]{files[SG_GAME_PLAYER_BLACKLIST]="old-black\n";files[SG_GAME_WHITELIST]="old-white\n";failWhiteNew=failBlackOld=true;require(!sgWriteGameRuleFiles({L"new-black"},{L"new-white"}));require(files.count(SG_GAME_RULE_JOURNAL)&&g_gameRuleRecoveryNeeded);bool blocked=false;try{sgReadGameRulesText(SG_GAME_PLAYER_BLACKLIST);}catch(...){blocked=true;}require(blocked);failBlackOld=false;require(sgReadGameRulesText(SG_GAME_PLAYER_BLACKLIST)=="old-black\n");require(files[SG_GAME_WHITELIST]=="old-white\n"&&!files.count(SG_GAME_RULE_JOURNAL));});
 check("game-rule commit-receipt failure rolls back completed replacements",[]{files[SG_GAME_PLAYER_BLACKLIST]="old-black\n";files[SG_GAME_WHITELIST]="old-white\n";auto before=files;failJournalCommit=true;require(!sgWriteGameRuleFiles({L"new-black"},{L"new-white"}));require(files==before);});
 check("retained committed game-rule receipt never rolls back a later edit",[]{files[SG_GAME_PLAYER_BLACKLIST]="old-black\n";files[SG_GAME_WHITELIST]="old-white\n";failJournalDelete=true;require(sgWriteGameRuleFiles({L"new-black"},{L"new-white"}));require(files.count(SG_GAME_RULE_JOURNAL));files[SG_GAME_PLAYER_BLACKLIST]="external-after-commit";g_gameRuleRecoveryNeeded=true;require(sgReadGameRulesText(SG_GAME_PLAYER_BLACKLIST)=="external-after-commit");});
 check("game-rule transaction preserves missing-file status during rollback",[]{files[SG_GAME_WHITELIST]="old-white\n";failWhiteNew=true;require(!sgWriteGameRuleFiles({L"new-black"},{L"new-white"}));require(!files.count(SG_GAME_PLAYER_BLACKLIST)&&files[SG_GAME_WHITELIST]=="old-white\n");});
 check("empty or corrupt game-rule journal blocks automatic overwrite",[]{files[SG_GAME_RULE_JOURNAL]="";files[SG_GAME_PLAYER_BLACKLIST]="old-black";auto before=files;require(!sgWriteGameRuleFiles({L"new-black"},{L"new-white"}));require(files==before);files[SG_GAME_RULE_JOURNAL]="{broken";require(!sgWriteGameRuleFiles({L"new-black"},{L"new-white"}));});
 check("ordinary game-rule reads never poll or parse a completed journal",[]{files[SG_GAME_PLAYER_BLACKLIST]="old-black";sgReadGameRulesText(SG_GAME_PLAYER_BLACKLIST);int previous=rawReads;for(int i=0;i<20;i++)sgReadGameRulesText(SG_GAME_PLAYER_BLACKLIST);require(rawReads-previous==20);});
 int failed=0;for(auto& c:cases)if(!c["ok"].get<bool>())failed++;std::cout<<json{{"passed",cases.size()-failed},{"failed",failed},{"cases",cases}}.dump(2)<<"\n";return failed?1:0;
}
`;
const file=path.join(out,'native-settings-current.cpp');fs.writeFileSync(file,cpp);
fs.writeFileSync(path.join(out,'source-identity.json'),JSON.stringify({sourceSha256:createHash('sha256').update(source).digest('hex'),functions:names,guardSha256:createHash('sha256').update(guard).digest('hex')},null,2));
const vc='C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat';
const command=`call "${vc}" >nul && cl /nologo /EHsc /std:c++20 /utf-8 /I"${path.join(root,'deps/json')}" "${file}" /Fo:"${path.join(out,'native-settings-current.obj')}" /Fe:"${path.join(out,'native-settings-current.exe')}"`;
const buildScript=path.join(out,'compile.cmd');fs.writeFileSync(buildScript,'@echo off\r\n'+command+'\r\nexit /b %errorlevel%\r\n');
const build=spawnSync('cmd.exe',['/d','/c',buildScript],{encoding:'utf8'});process.stdout.write(build.stdout||'');process.stderr.write(build.stderr||'');if(build.status!==0)process.exit(build.status||1);
const run=spawnSync(path.join(out,'native-settings-current.exe'),[],{encoding:'utf8'});process.stdout.write(run.stdout||'');process.stderr.write(run.stderr||'');if(run.stdout)fs.writeFileSync(path.join(out,'native-results.json'),run.stdout);process.exitCode=run.status||0;
