#include "json.hpp"
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
using json=nlohmann::json;namespace fs=std::filesystem;
static std::mutex g_steamOperationMutex;static bool failWrite=false;static unsigned closes=0,writes=0,restarts=0;
static void setSteamTarget(const json&){}
static bool jsonBoolOr(const json& value,const char* key,bool fallback=false){const auto found=value.find(key);return found!=value.end()&&found->is_boolean()?found->get<bool>():fallback;}
static bool hostProcessRunningByName(const wchar_t*){return true;}
static void closeSteamForShortcutCommit(){++closes;}
static void written(){++writes;if(failWrite)throw std::runtime_error("fixture transaction failed");}
static void commitToAllSteamAccounts(const json&){written();}
static void refreshArtworkForAllSteamAccounts(const json&){written();}
static std::vector<std::wstring> steamTargetArguments(const json&){return {};}
static fs::path workspacePlanPath(){return L"inert-no-file-plan.json";}
static int runWorker(const std::vector<std::wstring>&){written();return 0;}
static void requireSuccess(int){}
static json workspaceSnapshot(bool,const json&){return {{"ok",true}};}
static void restartSteamBigPicture(const json&){++restarts;}
#include "production-library-no-restart.h"
int main(int argc,char** argv){if(argc!=2)return 2;unsigned checks=0,failures=0;for(const auto& command:{"commit","refreshSteamArtwork"})for(bool failure:{false,true})for(int legacy:{-1,0,1})for(const auto& accountMode:{"all-steam-accounts","single"}){
 failWrite=failure;closes=writes=restarts=0;json target{{"selectionMode",accountMode},{"forceCloseSteam",true}};if(legacy>=0)target["restartSteamBigPicture"]=legacy==1;
 bool caught=false;try{productionLibraryCommand(command,target);}catch(const std::runtime_error&){caught=true;}
 const bool ok=restarts==0&&writes==1&&closes==1&&caught==failure;++checks;if(!ok)++failures;
 std::cout<<(ok?"PASS ":"FAIL ")<<command<<" failure="<<failure<<" legacyRestart="<<legacy<<" account="<<accountMode<<" restartCalls="<<restarts<<'\n';
 }
 fs::create_directories(argv[1]);const json receipt{{"ok",failures==0},{"checks",checks},{"failures",failures},{"realSteamOperations",0},{"source","Exact production commit/refresh command branches with memory-only file/process ports"}};
 std::ofstream(fs::path(argv[1])/"result.json")<<receipt.dump(2)<<'\n';std::cout<<receipt.dump()<<'\n';return failures?1:0;}