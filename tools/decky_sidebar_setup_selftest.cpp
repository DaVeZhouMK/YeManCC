#define WIN32_LEAN_AND_MEAN
#include "../native/decky_sidebar_setup.h"
#include "../native/decky_sidebar_diagnostics.h"
#include <fstream>
#include <iostream>
using namespace ymcc::deckysetup;
static unsigned cases=0;
static void check(bool v,const char* s){if(!v)throw std::runtime_error(s);cases++;}
static void file(const std::filesystem::path& p,const char* s="fixture"){std::filesystem::create_directories(p.parent_path());std::ofstream(p)<<s;}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)throw std::runtime_error("Expected Setup19 task directory");
 const auto allowed=std::filesystem::path(LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Sidebar\Build\Setup19)").lexically_normal();
 const auto base=std::filesystem::absolute(argv[1]).lexically_normal();if(base!=allowed){std::wcerr<<L"actual="<<base.native()<<L" expected="<<allowed.native()<<std::endl;throw std::runtime_error("Fixture scope rejected");}
 const auto root=base/(L"run-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 const auto steam=root/L"Steam",home=root/L"PowerControl"/L"decky",marker=steam/L".cef-enable-remote-debugging";
 const Digest digest=[](const std::wstring&){return std::string(loaderSha);};
 check(inspect(steam,home,digest)["reason"]=="steam-not-installed","no Steam");file(steam/L"steam.exe");
 auto missing=prepare(steam,home,digest,false);
 check(missing["reason"]=="resource-missing","no Loader");
 check(missing["loaderPath"]==pathText(home/L"PluginLoader_noconsole.exe"),"actual loader path reported");
 check(missing["missingPath"]==missing["loaderPath"],"missing path not hidden by default install");
 check(missing["resourceDirectory"]==pathText(home),"actual resource directory reported");check(!std::filesystem::exists(marker),"no early marker");
 file(home/L"PluginLoader_noconsole.exe");check(prepare(steam,home,[](const std::wstring&){return "bad";},false)["reason"]=="resource-hash-mismatch","bad checksum");
 check(!std::filesystem::exists(marker),"bad Loader no marker");check(inspect(steam,home,digest)["reason"]=="plugin-missing","no plugin");
 file(home/L"plugins"/L"ymcc-sidebar"/L"plugin.json");file(home/L"plugins"/L"ymcc-sidebar"/L"dist"/L"index.js");
 check(inspect(steam,home,digest)["reason"]=="steam-debug-missing","inspect missing marker");check(!std::filesystem::exists(marker),"inspection read-only");
 auto result=prepare(steam,home,digest,true);check(result["reason"]=="steam-restart-required"&&result["createdSteamDebugMarker"]==true,"running Steam restart notice");
 check(std::filesystem::file_size(marker)==0,"empty marker");file(marker,"PRESERVE-USER-TEXT");check(prepare(steam,home,digest,false)["ready"]==true,"idempotent prepare");
 std::ifstream in(marker);std::string text;in>>text;in.close();check(text=="PRESERVE-USER-TEXT","existing contents preserved");
 // Exact resolved task path checked before each removal, never the real Steam install.
 if(std::filesystem::weakly_canonical(marker.parent_path())!=std::filesystem::weakly_canonical(root/L"Steam"))throw std::runtime_error("marker scope rejected");
 std::filesystem::remove(marker);std::filesystem::create_directory(marker);check(prepare(steam,home,digest,false)["reason"]=="steam-debug-unsafe-path","directory marker refused");
 std::filesystem::remove(marker);check(prepare(steam,home,digest,false)["ready"]==true,"stopped Steam no restart");
 const auto safe=ymcc::deckydiag::record("runtime",{{"reason","loader-start-failed"},{"error",5},{"loaderPath","fixture-only"},{"token","NEVER-LOG"},{"endpoint","ws://private"},{"gameIdentity","PRIVATE"},{"args",{{"password","PRIVATE"}}}});
 check(safe["reason"]=="loader-start-failed"&&safe["error"]==5,"reason and Windows error retained");
 check(!safe.contains("token")&&!safe.contains("endpoint")&&!safe.contains("args")&&!safe.contains("gameIdentity"),"secrets and mutation args excluded");
 check(ymcc::deckydiag::maxBytes==1024ull*1024ull,"bounded diagnostic size");
 std::cout<<Json({{"cases",cases},{"status","passed"},{"fakeSteamDirectory",true},{"testDigestInjected",true},{"realSteamWrites",0},{"hardwareWrites",0},{"externalPython",false},{"externalNode",false}}).dump()<<std::endl;return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
