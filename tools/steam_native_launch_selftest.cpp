#include "steam_native_launch_windows.h"
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
    bool spawnSteamShell(const std::wstring& file,const std::wstring& args){++fallbackCalls;arguments=args;return file==executable&&args==startupArguments&&shellOk;}
    std::string steamLaunchFailure(){return "memory-primary/5;fallback/5";}
    bool spawnManualSteam(const std::wstring& file,const std::wstring& args){return dispatchManualSteam(*this,file,args);}
};
int main(int argc,char** argv){
const auto self=pathfs::path(executablePath());
if(self.filename()==L"inert-addon.exe"){std::ofstream marker(self.wstring()+L".started");marker<<(IsWindowVisible(GetConsoleWindow())?"visible":"hidden");marker.close();Sleep(400);return 0;}
unsigned passed=0;auto check=[&](const char* name,std::function<bool()> test){if(!test()){std::cerr<<"FAIL "<<name<<"\n";exit(23);}++passed;};
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
check("JSON launch refuses implicit intent before system IO",[]{for(const auto& input:{nlohmann::json::object(),nlohmann::json{{"userInitiated",false}},nlohmann::json{{"userInitiated",1}},nlohmann::json{{"userInitiated",true},{"path","unsafe"}}}){try{launch(L"unused",input);return false;}catch(const std::runtime_error&){}}return true;});
check("malformed logon command cannot fall into normal app",[]{int code=0;return tryLogonCommand(L"unused",L"\"x:\\ymcc.exe\" --steam-logon-start --dev ignored",code)&&code==2;});
check("ordinary startup never invokes logon transaction",[]{int code=0;return !tryLogonCommand(L"unused",L"\"x:\\ymcc.exe\" --minimized",code);});
if(argc<2)return 2;const pathfs::path scratch=pathfs::temp_directory_path()/(L"ymcc-steam-native-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));pathfs::create_directories(scratch);std::ofstream(pathfs::path(argv[1])/L"owned-scratch-path.txt")<<scratch.string();
const auto image=(scratch/L"YeManCC.exe").wstring(),pc=(scratch/L"PowerControl").wstring();Startup cfg(pc,image,scratch.wstring());
check("disabled boot entry remains disabled",[&]{return cfg.migrate()&&!cfg.authorized()&&!pathfs::exists(cfg.file);});
check("enable stores quoted advanced-start BAT",[&]{std::wstring text;return cfg.set(true)&&cfg.read(text)&&trim(text)==L"\""+(pathfs::path(pc)/L"YeManSteam.bat").wstring()+L"\""&&cfg.authorized();});
check("disable is empty and cannot authorize helper",[&]{std::wstring text;return cfg.set(false)&&cfg.read(text)&&text.empty()&&!cfg.authorized();});
for(const auto* name:{L"YeManSteam.bat",L"YeManSteam.vbs"})check("recognized legacy boot command migrates without launching",[&]{return writeText(cfg.file,L"\""+(pathfs::path(pc)/name).wstring()+L"\"\r\n")&&cfg.migrate()&&cfg.authorized();});
check("previous native logon helper migrates back to BAT without execution",[&]{std::wstring text;return writeText(cfg.file,L"\""+image+L"\" --steam-logon-start\r\n")&&cfg.migrate()&&cfg.read(text)&&trim(text)==cfg.command&&cfg.authorized();});
check("standard installed command matches the user advanced-start contract",[&]{Startup installed(LR"(C:\SOFT\YeMan\PowerControl)",image,scratch.wstring());return installed.command==LR"("C:\SOFT\YeMan\PowerControl\YeManSteam.bat")";});
check("unrelated custom command is never overwritten",[&]{std::wstring text;const std::wstring custom=L"\"x:\\User App.exe\" --custom";return writeText(cfg.file,custom)&&cfg.migrate()&&!cfg.set(true)&&cfg.read(text)&&text==custom;});
check("malformed UTF8 is rejected without replacing",[&]{std::ofstream out(cfg.file,std::ios::binary);out<<"\xff\xff";out.close();std::wstring text;return !cfg.read(text)&&!cfg.migrate()&&!cfg.set(false);});
check("UTF16 legacy config migrates",[&]{std::wstring text=L"\""+(pathfs::path(pc)/L"YeManSteam.vbs").wstring()+L"\"";std::ofstream out(cfg.file,std::ios::binary);out.put('\xff');out.put('\xfe');out.write(reinterpret_cast<const char*>(text.data()),text.size()*2);out.close();return cfg.migrate()&&cfg.authorized();});
check("actual receipt survives native owner destruction",[&]{Ledger stored{true,true,12345};{WindowsPorts first(pc,scratch.wstring());if(!first.lock()||!first.saveLedger(stored))return false;first.unlock();}WindowsPorts second(pc,scratch.wstring());Ledger actual;if(!second.lock())return false;const bool ok=second.readLedger(actual);second.unlock();return ok&&actual.bootConsumed&&actual.dispatched&&actual.dispatchedAt==12345;});
check("actual cross-process receipt lock rejects simultaneous owner",[&]{WindowsPorts first(pc,scratch.wstring()),second(pc,scratch.wstring());if(!first.lock())return false;const bool duplicate=second.lock();second.unlock();first.unlock();return !duplicate;});
check("actual addon parser only accepts enabled paths and excludes CSS aliases",[&]{const auto links=pathfs::path(pc)/L"YeManSteam";pathfs::create_directories(links);const auto app=scratch/L"Scaling App.exe",css=scratch/L"CssLoader-Standalone-Headless.exe";std::ofstream(app).put('x');std::ofstream(css).put('x');if(!writeText(links/L"one.txt",app.wstring())||!writeText(links/L"duplicate.txt",app.wstring())||!writeText(links/L"hidden-css.txt",css.wstring())||!writeText(links/L"disabled.txt.disabled",(scratch/L"Other.exe").wstring())||!writeText(links/L"bad.txt",L"relative.exe"))return false;WindowsPorts io(pc,scratch.wstring());const auto addons=io.addons();return addons.size()==1&&addons.front()==lower(app.wstring());});
#ifdef YMCC_NOIO_CASE_DIR
// The reviewed adapter consumes this exact owned non-executable marker in
// memory; neither the creator failure nor Shell path starts any real Steam.
const auto manualSteam=(scratch/L"steam.exe").wstring();std::ofstream(manualSteam).put('x');
check("actual manual creator refusal uses one fixed memory Shell dispatch",[&]{auto& ports=ymcc_noio::state();ports.manualSteamMode=true;ports.manualSteamPresent=false;ports.manualShellOkay=true;const auto primary=ports.manualPrimaryCalls,shell=ports.manualShellCalls;WindowsPorts io(pc,scratch.wstring());const bool ok=io.spawnManualSteam(manualSteam,startupArguments);const auto diagnostic=io.steamLaunchDiagnostics();ports.manualSteamMode=false;return ok&&ports.manualPrimaryCalls==primary+1&&ports.manualShellCalls==shell+1&&diagnostic["primaryStage"]=="create-process"&&diagnostic["primaryError"]==ERROR_ACCESS_DENIED&&diagnostic["fallbackStage"]=="shell-dispatched";});
check("actual manual Shell failure retains precise Win32 refusal",[&]{auto& ports=ymcc_noio::state();ports.manualSteamMode=true;ports.manualSteamPresent=false;ports.manualShellOkay=false;const auto shell=ports.manualShellCalls;WindowsPorts io(pc,scratch.wstring());const bool ok=io.spawnManualSteam(manualSteam,startupArguments);const auto detail=io.steamLaunchFailure();ports.manualSteamMode=false;ports.manualShellOkay=true;return !ok&&ports.manualShellCalls==shell+1&&detail.find("primary=create-process/5")!=detail.npos&&detail.find("fallback=shell-execute/5")!=detail.npos;});
check("actual manual fallback absence guard rejects a new Steam instance",[&]{auto& ports=ymcc_noio::state();ports.manualSteamMode=true;ports.manualSteamPresent=true;const auto shell=ports.manualShellCalls;WindowsPorts io(pc,scratch.wstring());const bool ok=io.spawnManualSteam(manualSteam,startupArguments);ports.manualSteamMode=false;ports.manualSteamPresent=false;return !ok&&ports.manualShellCalls==shell&&io.steamLaunchDiagnostics()["fallbackStage"]=="steam-absence-unverified";});
check("actual logon spawn port cannot enter the manual Shell terminal",[&]{auto& ports=ymcc_noio::state();ports.manualSteamMode=true;const auto shell=ports.manualShellCalls;WindowsPorts io(pc,scratch.wstring());const bool ok=io.spawn(manualSteam,startupArguments,true);ports.manualSteamMode=false;return !ok&&ports.manualShellCalls==shell;});
check("actual manual entry rejects changed startup parameters without a terminal",[&]{auto& ports=ymcc_noio::state();ports.manualSteamMode=true;const auto primary=ports.manualPrimaryCalls,shell=ports.manualShellCalls;WindowsPorts io(pc,scratch.wstring());const bool ok=io.spawnManualSteam(manualSteam,L" -silent");ports.manualSteamMode=false;return !ok&&ports.manualPrimaryCalls==primary&&ports.manualShellCalls==shell;});
#endif
check("actual native creator starts detached hidden inert addon without shell",[&]{const auto child=scratch/L"inert-addon.exe",marker=pathfs::path(child.wstring()+L".started");std::error_code ec;pathfs::remove(marker,ec);pathfs::copy_file(self,child,pathfs::copy_options::overwrite_existing);WindowsPorts io(pc,scratch.wstring());if(!io.spawn(child.wstring(),L"",false))return false;const auto until=GetTickCount64()+3000;while(!pathfs::exists(marker)&&GetTickCount64()<until)Sleep(10);std::ifstream input(marker);std::string value;input>>value;return value=="hidden";});
std::cout<<"{\"ok\":true,\"passed\":"<<passed<<",\"realSteamActions\":0,\"installedSettingsWrites\":0}"<<std::endl;return 0;}
