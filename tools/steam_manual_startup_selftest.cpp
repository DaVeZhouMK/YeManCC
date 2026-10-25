// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "steam_link_admission.h"
#include "steam_cssloader_guard.h"
#include "decky_sidebar_runtime.h"
#include <atomic>
#include <fstream>
#include <iostream>
using Session=ymcc::steamsession::Session;
static unsigned tests=0;
static void check(bool value,const char* name){if(!value)throw std::runtime_error(name);++tests;std::cout<<"PASS "<<name<<'\n';}
template<class F> bool until(F predicate){const auto end=GetTickCount64()+3000;while(!predicate()){if(GetTickCount64()>=end)return false;Sleep(5);}return true;}
static void write(const std::filesystem::path& path,const std::string& bytes){std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<bytes;}
int main(int argc,char** argv){try{
 check(argc==2,"explicit isolated artifact directory required");const auto root=std::filesystem::absolute(argv[1]);
 Session old{L"c:\\inert-steam",1,7,90},newer{L"c:\\inert-steam",2,7,110};
 ymcc::steamlink::Admission admission(100);
 check(!admission.allows(old),"saved checkbox plus an existing Steam session is observation-only on YMCC restart");
 check(!admission.allows({}),"no Steam session cannot authorize any linked loader");
 check(admission.allows(newer),"a genuinely newer Steam process retains missing-only linked recovery");
 check(!admission.manual({}),"invalid or unavailable Steam identity cannot grant manual admission");
 check(admission.manual(old)&&admission.allows(old),"an explicit manual Steam action admits only the verified existing session");
 auto account=old;account.account=42;admission.observe(account);check(admission.allows(account),"account drift alone does not recreate a Steam process or revoke its manual admission");
 auto reused=old;reused.created=91;check(!admission.allows(reused),"manual permission is bound to PID and birth; reused PID is not admitted");
 auto moved=old;moved.root=L"c:\\other-steam";check(!admission.allows(moved),"manual permission cannot transfer to another install root");
 admission.desktop(old);check(!admission.allows(old),"manual return to desktop revokes existing-session loader admission");admission.manual(old);
 admission.observe({});check(!admission.allows(old),"confirmed Steam exit clears the transient manual grant");
 admission.desktop(newer);check(!admission.allows(newer),"manual desktop veto applies even to a Steam process born after YMCC");admission.manual(newer);check(admission.allows(newer),"a subsequent explicit bigpicture action can reopen the same session");
 ymcc::steamlink::Admission restarted(120);check(!restarted.allows(old)&&!restarted.allows(newer),"YMCC restart forgets manual grants even when Steam stays alive");
 ymcc::steamlink::Admission unknown;check(!unknown.allows(newer),"unverified application creation time fails closed");
 check(ymcc::steamlink::currentProcessCreated()>0,"current application birth is obtained without a Steam action");
 ymcc::steamlink::Admission observed(100);observed.observeMode(old,"unknown");observed.observeMode(old,"bigpicture","page:1");
 check(!observed.allows(old),"first verified bigpicture snapshot is a baseline even after an unknown read");
 observed.observeMode(old,"bigpicture","new-cef-document:1");check(!observed.allows(old),"new CEF document does not open restored-session admission");
 observed.observeMode(old,"bigpicture","new-cef-document:2");check(observed.allows(old),"higher callback counter within the same document detects genuine quick reentry");
 observed.observeMode(old,"desktop","new-cef-document:2");check(!observed.allows(old),"actual desktop return revokes observed-entry grant");
 observed.observeMode(old,"unknown");observed.observeMode(old,"bigpicture","new-cef-document:3");check(observed.allows(old),"unknown read cannot erase a verified desktop-to-bigpicture transition");
 observed.observe({});observed.observeMode(old,"bigpicture","new-cef-document:4");check(!observed.allows(old),"Steam exit erases observation history even if the same fixture PID returns");
 const auto link=root/L"links"/L"loader.txt";write(link,"C:\\isolated\\CssLoader-Standalone-Headless.exe");
 std::atomic<unsigned> cssChecks{0},cssStarts{0};std::atomic<bool> manual{false};HANDLE lease=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(lease!=nullptr,"isolated backend wait lease created");
 ymcc::steamcss::Runtime css;ymcc::steamcss::Config config;config.linkFile=link.wstring();config.delays={40,55,70};config.ensure=[&](const auto&,const auto&,const auto&){++cssChecks;if(!manual)return ymcc::steamcss::Result{nullptr,0,"waiting-manual-steam","ymcc-restart-observation-only"};HANDLE copy=nullptr;DuplicateHandle(GetCurrentProcess(),lease,GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS);return ymcc::steamcss::Result{copy,33,"running",{},false};};
 check(css.initialize(config),"CSS runtime initializes with a remembered enabled link");css.observeSteam(old);
 check(until([&]{return css.snapshot().phase=="waiting-manual-steam";}),"CSS existing-session check remains waiting for manual Steam action, not an exhausted or running fiction");
 Sleep(250);check(cssChecks==1&&cssStarts==0&&!css.snapshot().started,"saved enabled state does not launch/reload CSS and paused wait has no retries");
 manual=true;css.requestManualRecovery();check(until([&]{return css.snapshot().phase=="running";}),"explicit manual recovery wakes the same session without needing a fake process change");
 check(cssChecks==2&&!css.snapshot().started,"manual recovery adopts an already-running backend without duplication");css.shutdown();CloseHandle(lease);
 const auto home=root/L"sidebar-home";std::filesystem::create_directories(home);std::atomic<unsigned> preflight{0},injections{0},pending{0};std::atomic<bool> permit{false};ymcc::deckysidebar::Runtime sidebar;ymcc::deckysidebar::Config sidebarConfig;sidebarConfig.executable=(root/L"NeverStarted.exe").wstring();sidebarConfig.home=home.wstring();sidebarConfig.preflight=[&]{++preflight;return std::string{"inert-preflight-stop"};};sidebarConfig.prepareBinding=[&]{++injections;return std::string{};};sidebarConfig.beginPending=[&]{++pending;};sidebarConfig.launchAdmission=[&]{return permit.load();};sidebar.initialize(sidebarConfig);sidebar.observeSteam(1,true);sidebar.setEnabled(true);
 check(until([&]{return sidebar.snapshot().phase=="waiting-manual-steam";}),"remembered sidebar enable plus live Steam defers original loader startup");
 check(sidebar.snapshot().enabled&&sidebar.snapshot().steamKnown&&sidebar.snapshot().steamRunning,"waiting for manual action retains truthful Steam running state and saved preference");Sleep(180);
 check(preflight==0&&injections==0&&pending==0&&sidebar.snapshot().pid==0,"YMCC boot does not prepare/inject/start a third-party sidebar frontend");
 sidebar.observeSteam(1,true);Sleep(50);check(preflight==0&&injections==0,"duplicate startup Steam events do not bypass the admission gate");
 permit=true;check(sidebar.requestLaunchAdmissionRefresh(),"late bigpicture UI event can recheck a deferred admitted session without a timer");check(until([&]{return sidebar.snapshot().phase=="unavailable";}),"manual session admission opens only the existing bounded preflight path");
 check(preflight==1&&pending==1&&injections==0,"preflight failure still stops before binding or process launch");check(!sidebar.requestLaunchAdmissionRefresh(),"unavailable or healthy non-deferred phase cannot open another startup through UI events");sidebar.shutdown();
 std::cout<<"STEAM_MANUAL_STARTUP_OK tests="<<tests<<" actualSteamActions=0 actualCssLaunches=0 actualSidebarLaunches=0\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
