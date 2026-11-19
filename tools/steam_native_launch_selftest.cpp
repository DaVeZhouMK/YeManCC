#if defined(YMCC_LAUNCH_ACTUAL_MANUAL_ONLY) && (defined(YMCC_LAUNCH_BOOT_SELECTED_ONLY) || defined(YMCC_LAUNCH_INVENTORY_ONLY) || defined(YMCC_LAUNCH_SELECTED_ONLY))
#error Actual-manual selection cannot be combined with another directed mode
#endif
#if defined(YMCC_LAUNCH_BOOT_SELECTED_ONLY) && (defined(YMCC_LAUNCH_INVENTORY_ONLY) || defined(YMCC_LAUNCH_SELECTED_ONLY))
#error Boot-selected selection cannot be combined with an existing directed mode
#endif
#ifdef YMCC_LAUNCH_INVENTORY_ONLY
#include "steam_launch_inventory_cases.inc"
#else
#include "steam_native_launch_windows.h"
#ifdef YMCC_NOIO_CASE_DIR
#include "steam_launch_noio_facts.h"
#endif
#include <iostream>
#include <functional>
using namespace ymcc::steamlaunch;
struct Memory {
    Ledger ledger;uint64_t clock=100000;
    bool authorized=true,available=true,readable=true,writable=true,spawnOk=true,locked=false,appearance=false,shellOk=true,fallbackAbsent=true,addonReadFails=false;
    int presence=0,finalPresence=0,linkedPresence=0,snapshots=0,spawns=0,steamStarts=0,lockCalls=0,saves=0,fallbackCalls=0,primaryCalls=0,addonReads=0;
    SteamSpawnOutcome primaryOutcome=SteamSpawnOutcome::Dispatched;
    std::vector<std::wstring> links{L"x:\\lossless.exe",L"x:\\cheats.exe"};std::vector<std::string> order;
    std::set<std::wstring> active,uncertain;std::wstring executable=L"x:\\steam.exe",arguments;
    bool bootAuthorized(){return authorized;}bool lock(){++lockCalls;return available&&(locked=true);}void unlock(){locked=false;}bool readLedger(Ledger& s){s=ledger;return readable;}bool saveLedger(const Ledger& s){++saves;if(!writable)return false;ledger=s;return true;}uint64_t now(){return clock;}
    auto addons(){++addonReads;order.push_back("read-addons");if(addonReadFails)throw std::runtime_error("linked read failed");return links;}auto steamExecutable(){return executable;}
    Presence snapshot(const std::vector<std::wstring>& addons){++snapshots;return {snapshots==1?presence:addons.empty()?finalPresence:linkedPresence,active,uncertain};}
    bool spawn(const std::wstring&,const std::wstring& args,bool steam){++spawns;order.push_back(steam?"steam":"addon");if(steam){++steamStarts;arguments=args;}return spawnOk;}
    SteamSpawnOutcome spawnSteamPrimary(const std::wstring& file,const std::wstring& args){++primaryCalls;const bool okay=spawn(file,args,true);return okay?primaryOutcome:SteamSpawnOutcome::NotCreated;}
    bool manualSteamFallbackAllowed(const std::wstring& file){return fallbackAbsent&&file==executable;}
    bool spawnSteamShell(const std::wstring& file,const std::wstring& args){++fallbackCalls;arguments=args;return file==executable&&manualSteamArgumentsAllowed(args)&&shellOk;}
    std::string steamLaunchFailure(){return "memory-primary/5;fallback/5";}
    bool spawnManualSteam(const std::wstring& file,const std::wstring& args){return dispatchManualSteam(*this,file,args);}
};

#if defined(YMCC_LAUNCH_ACTUAL_MANUAL_ONLY)
#include "steam_launch_actual_manual_cases.inc"
#elif defined(YMCC_LAUNCH_BOOT_SELECTED_ONLY)
#include "steam_boot_selected_cases.inc"
#elif defined(YMCC_LAUNCH_SELECTED_ONLY)
int main(int argc,char** argv){
 if(argc!=2||std::string(argv[1])!="--selected-executable")return 2;
 nlohmann::json cases=nlohmann::json::array();unsigned assertions=0;
 const auto need=[&](bool value,const char* why){++assertions;if(!value)throw std::runtime_error(why);};
 const auto check=[&](const char* name,const auto& fn){try{fn();cases.push_back({{"name",name},{"passed",true}});}catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"error",e.what()}});}};
 check("selected exact path is plain and has no addon cascade",[&]{Memory f;const auto r=run(f,Origin::Manual,{},f.executable);need(r.dispatched,"not dispatched");need(f.arguments.empty(),"plain launch gained flags");need(f.steamStarts==1&&f.primaryCalls==1&&!f.fallbackCalls,"one primary");need(!f.addonReads&&f.spawns==1,"linked cascade");need(!f.locked,"lock not returned");});
 check("selected slash and case use existing installation",[&]{Memory f;need(run(f,Origin::Manual,{},L"X:/StEaM.ExE").dispatched,"equivalent spelling refused");need(f.arguments.empty()&&f.spawns==1,"plain terminal changed");});
 check("changed selected installation refuses rather than substituting",[&]{Memory f;const auto r=run(f,Origin::Manual,{},L"y:\\steam.exe");need(!r.ok&&r.reason=="steam-selected-executable-changed","wrong path accepted");need(!f.spawns&&!f.saves&&!f.addonReads,"mismatch caused effects");});
 check("selected path cannot become a logon grant",[&]{Memory f;need(!run(f,Origin::Logon,{},f.executable).ok,"logon accepted selection");need(!f.lockCalls&&!f.spawns&&!f.saves,"logon effect");});
 check("selected running Steam has no dispatch or mode change",[&]{Memory f;f.presence=1;need(run(f,Origin::Manual,{},f.executable).alreadyRunning,"running lost");need(!f.spawns&&!f.saves&&!f.addonReads,"running reopened");});
 check("selected unknown cannot authorize start",[&]{Memory f;f.presence=-1;need(!run(f,Origin::Manual,{},f.executable).ok,"unknown accepted");need(!f.spawns&&!f.saves,"unknown effects");});
 check("selected final presence and unknown stop dispatch",[&]{for(int final:{1,-1}){Memory f;f.finalPresence=final;auto r=run(f,Origin::Manual,{},f.executable);need(!r.dispatched,"final observation ignored");need(!f.spawns&&!f.addonReads,"late dispatch");}});
 check("selected primary definite failure has one plain fallback",[&]{Memory f;f.spawnOk=false;need(run(f,Origin::Manual,{},f.executable).dispatched,"fallback absent");need(f.primaryCalls==1&&f.fallbackCalls==1&&f.arguments.empty(),"fallback changed semantics");need(!f.addonReads,"fallback cascade");});
 check("selected uncertain primary never replays",[&]{Memory f;f.primaryOutcome=SteamSpawnOutcome::Uncertain;need(!run(f,Origin::Manual,{},f.executable).ok,"uncertain accepted");need(!f.fallbackCalls&&!f.addonReads,"uncertain replay");});
 check("selected fallback keeps final absence guard",[&]{Memory f;f.spawnOk=false;f.fallbackAbsent=false;need(!run(f,Origin::Manual,{},f.executable).ok,"fallback absence ignored");need(!f.fallbackCalls,"fallback issued");});
 check("ordinary manual default keeps flags and addons",[&]{Memory f;need(run(f,Origin::Manual).dispatched,"default rejected");need(f.arguments==startupArguments&&f.spawns==3&&f.addonReads==1,"default semantics changed");});
 check("manual terminal still rejects arbitrary arguments",[&]{Memory f;need(!dispatchManualSteam(f,f.executable,L" -silent"),"arbitrary arguments accepted");need(!f.primaryCalls&&!f.fallbackCalls,"arbitrary terminal");});
 check("typed selected request validates before any environment IO",[&]{
  need(selectedManualExecutable({{"userInitiated",true}}).empty(),"old request invalid");
  need(selectedManualExecutable({{"userInitiated",true},{"selectedExecutable","X:/Steam/steam.exe"}})==L"X:/Steam/steam.exe","selected path changed");
  for(const auto& j:{nlohmann::json::object(),nlohmann::json{{"userInitiated",false}},nlohmann::json{{"userInitiated",1}},nlohmann::json{{"userInitiated",true},{"selectedExecutable",nullptr}},nlohmann::json{{"userInitiated",true},{"selectedExecutable",1}},nlohmann::json{{"userInitiated",true},{"selectedExecutable",""}},nlohmann::json{{"userInitiated",true},{"selectedExecutable","steam.exe"}},nlohmann::json{{"userInitiated",true},{"selectedExecutable","X:/helper.exe"}},nlohmann::json{{"userInitiated",true},{"selectedExecutable","X:/steam.exe\n"}},nlohmann::json{{"userInitiated",true},{"selectedExecutable",std::string("X:/steam.exe\0",13)}},nlohmann::json{{"userInitiated",true},{"selectedExecutable","X:/steam.exe"},{"args","-silent"}}}){bool rejected=false;try{(void)selectedManualExecutable(j);}catch(const std::runtime_error&){rejected=true;}need(rejected,"invalid request accepted");}
 });
 unsigned failed=0;for(const auto& row:cases)if(!row["passed"].get<bool>())++failed;
 std::cout<<nlohmann::json{{"selection","selected-executable"},{"cases",cases},{"assertions",assertions},{"failed",failed},{"actualSteamActions",0},{"defaultGroupExecuted",false}}.dump()<<'\n';return failed?1:0;
}
#else

static bool refusesLegacyLogon(){
    int exitCode=0;
    return refuseLegacyLogonCommand(L"\"x:\\ymcc.exe\" --steam-logon-start",exitCode)&&exitCode==3&&
        refuseLegacyLogonCommand(L"\"x:\\ymcc.exe\" --steam-logon-start --dev ignored",exitCode)&&exitCode==2;
}
static bool keepsOrdinaryStartup(){
    int exitCode=53;
    return !refuseLegacyLogonCommand(L"\"x:\\ymcc.exe\" --minimized",exitCode)&&exitCode==53;
}
static bool migratesLegacyLogon(const Startup& cfg,const std::wstring& image){
    std::wstring text;
    return writeText(cfg.file,L"\""+image+L"\" --steam-logon-start\r\n")&&cfg.recognized(L"\""+image+L"\" --steam-logon-start")&&
        !cfg.authorized()&&cfg.migrate()&&cfg.read(text)&&trim(text)==cfg.command&&cfg.authorized();
}
int main(int argc,char** argv){
if(argc>1&&std::string_view(argv[1])=="--legacy-logon-guard"){
    if(argc!=3||!*argv[2])return 2;
    const auto directory=pathfs::absolute(argv[2]);
    if(!pathfs::create_directory(directory))return 2;
    const auto image=(directory/L"YeManCC.exe").wstring();
    const Startup cfg((directory/L"PowerControl").wstring(),image,directory.wstring());
    const nlohmann::json cases={{"retired-command-refuses-exact-and-malformed",refusesLegacyLogon()},
        {"ordinary-minimized-untouched",keepsOrdinaryStartup()},
        {"legacy-config-migrates-without-dispatch",migratesLegacyLogon(cfg,image)}};
    std::cout<<nlohmann::json{{"selection","legacy-logon-guard"},{"cases",cases},{"defaultGroupExecuted",false},{"realSteamActions",0}}.dump()<<'\n';
    return std::all_of(cases.begin(),cases.end(),[](const auto& value){return value==true;})?0:23;
}
const auto self=pathfs::path(executablePath());
if(self.filename()==L"inert-addon.exe"){std::ofstream marker(self.wstring()+L".started");marker<<(IsWindowVisible(GetConsoleWindow())?"visible":"hidden");marker.close();Sleep(400);return 0;}
unsigned passed=0;auto check=[&](const char* name,std::function<bool()> test){if(!test()){std::cerr<<"FAIL "<<name<<"\n";exit(23);}++passed;};
check("authorized logon prepares overlay before Steam and before linked apps",[]{Memory f;unsigned calls=0;const auto result=run(f,Origin::Logon,[&]{++calls;f.order.push_back("overlay");});return result.dispatched&&calls==1&&f.order==std::vector<std::string>{"overlay","steam","read-addons","addon","addon"};});
check("manual launch does not consume automatic overlay preference",[]{Memory f;unsigned calls=0;return run(f,Origin::Manual,[&]{++calls;}).dispatched&&calls==0;});
check("consumed logon never repeats overlay preparation",[]{Memory f;f.ledger.bootConsumed=true;unsigned calls=0;return run(f,Origin::Logon,[&]{++calls;}).ok&&calls==0&&!f.spawns;});
check("unauthorized logon never applies overlay preparation",[]{Memory f;f.authorized=false;unsigned calls=0;return !run(f,Origin::Logon,[&]{++calls;}).ok&&calls==0&&!f.spawns;});
check("overlay preparation failure cannot block Steam launch",[]{Memory f;return run(f,Origin::Logon,[]{throw std::runtime_error("controlled overlay failure");}).dispatched;});
check("Steam appearing during overlay preparation is observed before dispatch",[]{Memory f;const auto result=run(f,Origin::Logon,[&]{f.finalPresence=1;});return result.alreadyRunning&&!f.steamStarts;});
check("cold manual once fixed offline flags",[]{Memory f;auto r=run(f,Origin::Manual);return r.ok&&r.dispatched&&f.steamStarts==1&&f.spawns==3&&f.arguments==startupArguments&&!f.locked;});
check("running Steam has no startup args/addons/mode URI",[]{Memory f;f.presence=1;auto r=run(f,Origin::Manual);return r.ok&&r.alreadyRunning&&!f.spawns;});
check("unknown state fails closed",[]{Memory f;f.presence=-1;return !run(f,Origin::Manual).ok&&!f.spawns;});
check("Steam appeared during launch validation cancels second dispatch",[]{Memory f;f.finalPresence=1;auto r=run(f,Origin::Manual);return r.ok&&r.alreadyRunning&&!f.steamStarts;});
check("final unknown never dispatches Steam",[]{Memory f;f.finalPresence=-1;return !run(f,Origin::Manual).ok&&!f.steamStarts;});
check("missing executable starts no addons",[]{Memory f;f.executable.clear();return !run(f,Origin::Manual).ok&&!f.spawns;});
check("logon second request before 40s stays fenced across fresh consumer",[]{Memory f;run(f,Origin::Manual);Memory next;next.ledger=f.ledger;next.clock+=39999;return !run(next,Origin::Logon).ok&&!next.spawns;});
check("logon 40s boundary permits authorized dispatch",[]{Memory f;run(f,Origin::Manual);Memory next;next.ledger=f.ledger;next.clock+=40000;return run(next,Origin::Logon).dispatched;});
check("failed manual primary and fallback do not impose a timed retry ban",[]{Memory f;f.spawnOk=false;f.shellOk=false;auto failed=run(f,Origin::Manual);Memory next;next.ledger=f.ledger;return !failed.ok&&f.fallbackCalls==1&&run(next,Origin::Manual).dispatched;});
check("clock regression does not open logon fence",[]{Memory f;f.ledger={false,true,200000};return !run(f,Origin::Logon).ok&&!f.spawns;});
check("manual ignores recent receipt while preserving consumed boot authority",[]{Memory f;f.ledger={true,true,99999};return run(f,Origin::Manual).dispatched&&f.ledger.bootConsumed;});
check("manual ignores receipt clock rollback without changing logon fence",[]{Memory f;f.ledger={false,true,200000};return run(f,Origin::Manual).dispatched;});
check("Steam dispatch precedes linked app discovery and starts",[]{Memory f;auto r=run(f,Origin::Manual);return r.dispatched&&f.order==std::vector<std::string>{"steam","read-addons","addon","addon"};});
check("primary definite failure gets one fixed manual fallback",[]{Memory f;f.spawnOk=false;auto r=run(f,Origin::Manual);return r.dispatched&&f.primaryCalls==1&&f.fallbackCalls==1&&f.arguments==startupArguments;});
check("destroyed owned child permits one fixed manual fallback",[]{Memory f;f.primaryOutcome=SteamSpawnOutcome::OwnedChildDestroyed;return run(f,Origin::Manual).dispatched&&f.fallbackCalls==1;});
check("ambiguous primary never gets a fallback",[]{Memory f;f.primaryOutcome=SteamSpawnOutcome::Uncertain;return !run(f,Origin::Manual).dispatched&&!f.fallbackCalls&&!f.addonReads;});
check("Steam reappearing before fallback prevents replay",[]{Memory f;f.spawnOk=false;f.fallbackAbsent=false;return !run(f,Origin::Manual).dispatched&&!f.fallbackCalls;});
check("successful primary is never replayed through Shell",[]{Memory f;return run(f,Origin::Manual).dispatched&&f.primaryCalls==1&&!f.fallbackCalls;});
check("failed Shell acceptance is not retried by the transaction",[]{Memory f;f.spawnOk=false;f.shellOk=false;return !run(f,Origin::Manual).ok&&f.primaryCalls==1&&f.fallbackCalls==1&&!f.addonReads;});
check("logon never receives manual Shell fallback",[]{Memory f;f.spawnOk=false;auto r=run(f,Origin::Logon);return !r.ok&&!f.primaryCalls&&!f.fallbackCalls&&f.ledger.bootConsumed;});
check("manual fallback rejects changed arguments before any terminal",[]{Memory f;return !dispatchManualSteam(f,f.executable,L" -silent")&&!f.primaryCalls&&!f.fallbackCalls;});
check("Steam refusal preserves primary and fallback diagnostics",[]{Memory f;f.spawnOk=false;f.shellOk=false;const auto r=run(f,Origin::Manual);return !r.ok&&r.reason=="steam-launch-refused: memory-primary/5;fallback/5";});
check("linked read failure preserves actual Steam dispatch receipt",[]{Memory f;f.addonReadFails=true;auto r=run(f,Origin::Manual);return r.ok&&r.dispatched&&f.spawns==1&&r.addonsFailed==1;});
check("unknown linked app census cannot invalidate Steam dispatch or start addons",[]{Memory f;f.linkedPresence=-1;auto r=run(f,Origin::Manual);return r.ok&&r.dispatched&&f.spawns==1&&r.addonsFailed==2;});
check("locked by another caller fails with no IO dispatch",[]{Memory f;f.available=false;return !run(f,Origin::Manual).ok&&!f.spawns&&!f.snapshots;});
check("unreadable receipt fails closed",[]{Memory f;f.readable=false;return !run(f,Origin::Manual).ok&&!f.spawns&&!f.locked;});
check("unwritable receipt fails before any child",[]{Memory f;f.writable=false;return !run(f,Origin::Manual).ok&&!f.spawns;});
check("existing and uncertain addon instances both skipped",[]{Memory f;f.active.insert(f.links[0]);f.uncertain.insert(f.links[1]);return run(f,Origin::Manual).dispatched&&f.spawns==1;});
check("no boot authorization is not an intent",[]{Memory f;f.authorized=false;return !run(f,Origin::Logon).ok&&!f.lockCalls&&!f.spawns;});
check("one boot start is consumed across helper and YMCC restart",[]{Memory f;run(f,Origin::Logon);Memory next;next.ledger=f.ledger;next.clock+=90000;auto r=run(next,Origin::Logon);return r.ok&&!r.dispatched&&!next.spawns&&!next.snapshots;});
check("boot with existing desktop consumes grant without switching",[]{Memory f;f.presence=1;run(f,Origin::Logon);Memory next;next.ledger=f.ledger;return run(next,Origin::Logon).ok&&!next.spawns&&f.ledger.bootConsumed;});
check("boot unknown cannot be replayed later",[]{Memory f;f.presence=-1;run(f,Origin::Logon);Memory next;next.ledger=f.ledger;return run(next,Origin::Logon).ok&&!next.spawns;});
check("manual action remains available after boot consumed",[]{Memory f;f.ledger.bootConsumed=true;return run(f,Origin::Manual).dispatched;});
check("JSON launch refuses implicit intent before system IO",[]{for(const auto& input:{nlohmann::json::object(),nlohmann::json{{"userInitiated",false}},nlohmann::json{{"userInitiated",1}},nlohmann::json{{"userInitiated",true},{"path","unsafe"}}}){try{(void)selectedManualExecutable(input);return false;}catch(const std::runtime_error&){}}return true;});
check("exact and malformed retired logon command cannot dispatch or fall into normal app",refusesLegacyLogon);
check("ordinary startup never invokes logon transaction",keepsOrdinaryStartup);
if(argc<2)return 2;const pathfs::path scratch=pathfs::temp_directory_path()/(L"ymcc-steam-native-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));pathfs::create_directories(scratch);std::ofstream(pathfs::path(argv[1])/L"owned-scratch-path.txt")<<scratch.string();
const auto image=(scratch/L"YeManCC.exe").wstring(),pc=(scratch/L"PowerControl").wstring();Startup cfg(pc,image,scratch.wstring());
check("disabled boot entry remains disabled",[&]{return cfg.migrate()&&!cfg.authorized()&&!pathfs::exists(cfg.file);});
check("enable stores quoted advanced-start BAT",[&]{std::wstring text;return cfg.set(true)&&cfg.read(text)&&trim(text)==L"\""+(pathfs::path(pc)/L"YeManSteam.bat").wstring()+L"\""&&cfg.authorized();});
check("disable removes earlystart and cannot authorize helper",[&]{std::wstring text;return cfg.set(false)&&!pathfs::exists(cfg.file)&&cfg.read(text)&&text.empty()&&!cfg.authorized();});
check("repeated disable of absent earlystart never recreates a file",[&]{return cfg.set(false)&&cfg.set(false,true)&&!pathfs::exists(cfg.file);});
check("old empty earlystart is deleted",[&]{return writeText(cfg.file,L"")&&cfg.set(false,true)&&!pathfs::exists(cfg.file);});
check("disable then enable restores original quoted BAT",[&]{return cfg.set(true,true)&&cfg.authorized()&&cfg.set(false,true)&&!pathfs::exists(cfg.file)&&cfg.set(true,true)&&cfg.authorized();});
check("custom earlystart requires explicit repair and is backed up before deletion",[&]{const std::wstring custom=L"user advanced command";if(!writeText(cfg.file,custom)||cfg.set(false))return false;std::wstring current;if(!cfg.read(current)||current!=custom||!cfg.set(false,true)||pathfs::exists(cfg.file))return false;for(const auto& entry:pathfs::directory_iterator(scratch)){if(entry.path().filename().wstring().find(L".earlystart.ymcc-before-")==0){std::wstring saved;if(readText(entry.path(),saved)&&saved==custom)return true;}}return false;});
check("delete failure preserves entry and retry can recover",[&]{if(!cfg.set(true))return false;HANDLE held=CreateFileW(cfg.file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(held==INVALID_HANDLE_VALUE)return false;const bool refused=!cfg.set(false,true)&&pathfs::exists(cfg.file);CloseHandle(held);return refused&&cfg.set(false,true)&&!pathfs::exists(cfg.file);});
check("directory at earlystart is rejected without removing it",[&]{if(!pathfs::create_directory(cfg.file))return false;const bool refused=!cfg.set(false,true)&&pathfs::is_directory(cfg.file);pathfs::remove(cfg.file);return refused;});
for(const auto* name:{L"YeManSteam.bat",L"YeManSteam.vbs"})check("recognized legacy boot command migrates without launching",[&]{return writeText(cfg.file,L"\""+(pathfs::path(pc)/name).wstring()+L"\"\r\n")&&cfg.migrate()&&cfg.authorized();});
check("previous native logon helper migrates back to BAT without execution",[&]{return migratesLegacyLogon(cfg,image);});
check("standard installed command matches the user advanced-start contract",[&]{Startup installed(LR"(C:\SOFT\YeMan\PowerControl)",image,scratch.wstring());return installed.command==LR"("C:\SOFT\YeMan\PowerControl\YeManSteam.bat")";});
check("unrelated custom command is never overwritten",[&]{std::wstring text;const std::wstring custom=L"\"x:\\User App.exe\" --custom";return writeText(cfg.file,custom)&&cfg.migrate()&&!cfg.set(true)&&cfg.read(text)&&text==custom;});
check("malformed UTF8 is rejected without replacing",[&]{std::ofstream out(cfg.file,std::ios::binary);out<<"\xff\xff";out.close();std::wstring text;return !cfg.read(text)&&!cfg.migrate()&&!cfg.set(false);});
check("explicit repair archives malformed raw bytes before forcing OFF or ON",[&]{
const auto malformed=[&]{std::ofstream out(cfg.file,std::ios::binary);out<<"\xff\xff";};
const auto preserved=[&]{for(const auto& entry:pathfs::directory_iterator(scratch)){if(entry.path().filename().wstring().find(L".earlystart.ymcc-before-")==0){std::ifstream input(entry.path(),std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});if(bytes==std::string("\xff\xff",2))return true;}}return false;};
malformed();if(!cfg.set(false,true)||pathfs::exists(cfg.file)||!preserved())return false;malformed();return cfg.set(true,true)&&cfg.authorized()&&preserved();});
check("UTF16 legacy config migrates",[&]{std::wstring text=L"\""+(pathfs::path(pc)/L"YeManSteam.vbs").wstring()+L"\"";std::ofstream out(cfg.file,std::ios::binary);out.put('\xff');out.put('\xfe');out.write(reinterpret_cast<const char*>(text.data()),text.size()*2);out.close();return cfg.migrate()&&cfg.authorized();});
check("actual receipt survives native owner destruction",[&]{Ledger stored{true,true,12345};{WindowsPorts first(pc,scratch.wstring());if(!first.lock()||!first.saveLedger(stored))return false;first.unlock();}WindowsPorts second(pc,scratch.wstring());Ledger actual;if(!second.lock())return false;const bool ok=second.readLedger(actual);second.unlock();return ok&&actual.bootConsumed&&actual.dispatched&&actual.dispatchedAt==12345;});
check("actual cross-process receipt lock rejects simultaneous owner",[&]{WindowsPorts first(pc,scratch.wstring()),second(pc,scratch.wstring());if(!first.lock())return false;const bool duplicate=second.lock();second.unlock();first.unlock();return !duplicate;});
check("actual addon parser only accepts enabled paths and excludes CSS aliases",[&]{const auto links=pathfs::path(pc)/L"YeManSteam";pathfs::create_directories(links);const auto app=scratch/L"Scaling App.exe",css=scratch/L"CssLoader-Standalone-Headless.exe";std::ofstream(app).put('x');std::ofstream(css).put('x');if(!writeText(links/L"one.txt",app.wstring())||!writeText(links/L"duplicate.txt",app.wstring())||!writeText(links/L"hidden-css.txt",css.wstring())||!writeText(links/L"disabled.txt.disabled",(scratch/L"Other.exe").wstring())||!writeText(links/L"bad.txt",L"relative.exe"))return false;WindowsPorts io(pc,scratch.wstring());const auto addons=io.addons();return addons.size()==1&&addons.front()==lower(app.wstring());});
#ifdef YMCC_NOIO_CASE_DIR
// The reviewed adapter consumes this exact owned non-executable marker in
// memory; neither the creator failure nor Shell path starts any real Steam.
const auto manualSteam=(scratch/L"steam.exe").wstring();std::ofstream(manualSteam).put('x');
const auto ownerFacts=memorySteamLaunchFacts();
check("actual manual creator refusal uses one fixed memory Shell dispatch",[&]{auto& ports=ymcc_noio::state();ports.manualSteamMode=true;ports.manualSteamPresent=false;ports.manualShellOkay=true;const auto primary=ports.manualPrimaryCalls,shell=ports.manualShellCalls;WindowsPorts io(pc,scratch.wstring(),{},{},ownerFacts);const bool ok=io.spawnManualSteam(manualSteam,startupArguments);const auto diagnostic=io.steamLaunchDiagnostics();ports.manualSteamMode=false;return ok&&ports.manualPrimaryCalls==primary+1&&ports.manualShellCalls==shell+1&&diagnostic["primaryStage"]=="create-process"&&diagnostic["primaryError"]==ERROR_ACCESS_DENIED&&diagnostic["fallbackStage"]=="shell-dispatched";});
check("actual manual Shell failure retains precise Win32 refusal",[&]{auto& ports=ymcc_noio::state();ports.manualSteamMode=true;ports.manualSteamPresent=false;ports.manualShellOkay=false;const auto shell=ports.manualShellCalls;WindowsPorts io(pc,scratch.wstring(),{},{},ownerFacts);const bool ok=io.spawnManualSteam(manualSteam,startupArguments);const auto detail=io.steamLaunchFailure();ports.manualSteamMode=false;ports.manualShellOkay=true;return !ok&&ports.manualShellCalls==shell+1&&detail.find("primary=create-process/5")!=detail.npos&&detail.find("fallback=shell-execute/5")!=detail.npos;});
check("actual manual fallback absence guard rejects a new Steam instance",[&]{auto& ports=ymcc_noio::state();ports.manualSteamMode=true;ports.manualSteamPresent=true;const auto shell=ports.manualShellCalls;WindowsPorts io(pc,scratch.wstring(),{},{},ownerFacts);const bool ok=io.spawnManualSteam(manualSteam,startupArguments);ports.manualSteamMode=false;ports.manualSteamPresent=false;return !ok&&ports.manualShellCalls==shell&&io.steamLaunchDiagnostics()["fallbackStage"]=="steam-absence-unverified";});
check("actual logon spawn port cannot enter the manual Shell terminal",[&]{auto& ports=ymcc_noio::state();ports.manualSteamMode=true;const auto shell=ports.manualShellCalls;WindowsPorts io(pc,scratch.wstring(),{},{},ownerFacts);const bool ok=io.spawn(manualSteam,startupArguments,true);ports.manualSteamMode=false;return !ok&&ports.manualShellCalls==shell;});
check("actual manual entry rejects changed startup parameters without a terminal",[&]{auto& ports=ymcc_noio::state();ports.manualSteamMode=true;const auto primary=ports.manualPrimaryCalls,shell=ports.manualShellCalls;WindowsPorts io(pc,scratch.wstring(),{},{},ownerFacts);const bool ok=io.spawnManualSteam(manualSteam,L" -silent");ports.manualSteamMode=false;return !ok&&ports.manualPrimaryCalls==primary&&ports.manualShellCalls==shell;});
#endif
check("actual native creator starts detached hidden inert addon without shell",[&]{const auto child=scratch/L"inert-addon.exe",marker=pathfs::path(child.wstring()+L".started");std::error_code ec;pathfs::remove(marker,ec);pathfs::copy_file(self,child,pathfs::copy_options::overwrite_existing);WindowsPorts io(pc,scratch.wstring());if(!io.spawn(child.wstring(),L"",false))return false;const auto until=GetTickCount64()+3000;while(!pathfs::exists(marker)&&GetTickCount64()<until)Sleep(10);std::ifstream input(marker);std::string value;input>>value;return value=="hidden";});
std::cout<<"{\"ok\":true,\"passed\":"<<passed<<",\"realSteamActions\":0,\"installedSettingsWrites\":0}"<<std::endl;return 0;}

#endif // YMCC_LAUNCH_SELECTED_ONLY
#endif
