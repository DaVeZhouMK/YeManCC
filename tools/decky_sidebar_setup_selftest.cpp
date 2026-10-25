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
 const auto base=std::filesystem::absolute(argv[1]).lexically_normal();if(base!=allowed && base.filename()!=L"setup-fixture"){std::wcerr<<L"actual="<<base.native()<<L" expected="<<allowed.native()<<std::endl;throw std::runtime_error("Fixture scope rejected");}
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
 check(result["loaderExpectedSha256"]==loaderSha&&result["loaderObservedSha256"]==loaderSha&&result["loaderHashMatched"]==true&&result["loaderOrigin"]=="official-release"&&result["loaderVersion"]==ymcc::deckyidentity::version&&result["loaderConsumerContract"]==1&&result["loaderModified"]==false,"prepare carries the observed/pinned Loader identity without sidebar demand");
 const auto identityLog=ymcc::deckydiag::record("prepare",result);
 check(identityLog["loaderObservedSha256"]==loaderSha&&identityLog["loaderHashMatched"]==true,"native diagnostic retains checked Loader identity");
 const auto wrong=prepare(steam,home,[](const std::wstring&){return std::string(64,'a');},false);
 check(wrong["reason"]=="resource-hash-mismatch"&&wrong["loaderHashMatched"]==false&&!wrong.contains("loaderConsumerContract"),"mismatch never claims the current policy is running");
 check(ymcc::deckydiag::record("prepare",wrong)["loaderObservedSha256"]==std::string(64,'a'),"valid mismatched digest is retained for deployment diagnosis");
 check(!ymcc::deckydiag::record("runtime",result).contains("loaderObservedSha256"),"identity only logs at its existing preparation boundary");
 check(!ymcc::deckydiag::record("prepare",{{"loaderObservedSha256","C:/PRIVATE/token"},{"loaderPolicyVersion",1001},{"loaderHashMatched","true"}}).contains("loaderObservedSha256"),"arbitrary diagnostic text is not a Loader digest");
 check(ymcc::deckydiag::record("prepare",{{"loaderPolicyVersion",-1},{"loaderHashMatched","true"}}).size()==1,"invalid policy or boolean types are omitted");
 check(std::filesystem::file_size(marker)==0,"empty marker");file(marker,"PRESERVE-USER-TEXT");check(prepare(steam,home,digest,false)["ready"]==true,"idempotent prepare");
 std::ifstream in(marker);std::string text;in>>text;in.close();check(text=="PRESERVE-USER-TEXT","existing contents preserved");
 // Exact resolved task path checked before each removal, never the real Steam install.
 if(std::filesystem::weakly_canonical(marker.parent_path())!=std::filesystem::weakly_canonical(root/L"Steam"))throw std::runtime_error("marker scope rejected");
 std::filesystem::remove(marker);std::filesystem::create_directory(marker);check(prepare(steam,home,digest,false)["reason"]=="steam-debug-unsafe-path","directory marker refused");
 std::filesystem::remove(marker);check(prepare(steam,home,digest,false)["ready"]==true,"stopped Steam no restart");
 // Native startup prewarm uses this same preparation consumer, including OFF/power cancellation.
 const auto canceledSteam=root/L"CanceledSteam";file(canceledSteam/L"steam.exe");
 const auto canceledMarker=canceledSteam/L".cef-enable-remote-debugging";
 bool prewarmAllowed=false;unsigned reads=0;
 auto canceled=prepare(canceledSteam,home,[&](const std::wstring&){++reads;return std::string(loaderSha);},false,[&]{return prewarmAllowed;});
 check(canceled["reason"]=="preparation-cancelled"&&!std::filesystem::exists(canceledMarker)&&reads==0,"OFF cancels prewarm before resource IO");
 prewarmAllowed=true;
 canceled=prepare(canceledSteam,home,[&](const std::wstring&){++reads;prewarmAllowed=false;return std::string(loaderSha);},false,[&]{return prewarmAllowed;});
 check(canceled["reason"]=="preparation-cancelled"&&!std::filesystem::exists(canceledMarker),"OFF during hash prevents late marker creation");
 prewarmAllowed=true;check(prepare(canceledSteam,home,digest,false,[&]{return prewarmAllowed;})["ready"]==true,"fresh admitted prewarm prepares known-stopped Steam");
 file(canceledMarker,"KEEP-CANCEL-CONTENT");prewarmAllowed=false;
 check(prepare(canceledSteam,home,digest,false,[&]{return prewarmAllowed;})["reason"]=="preparation-cancelled","canceled repeat cannot claim readiness");
 std::ifstream preserved(canceledMarker);std::string preservedText;preserved>>preservedText;preserved.close();check(preservedText=="KEEP-CANCEL-CONTENT","cancellation preserves existing debug marker content");
 const auto safe=ymcc::deckydiag::record("runtime",{{"reason","loader-start-failed"},{"error",5},{"loaderPath","fixture-only"},{"token","NEVER-LOG"},{"endpoint","ws://private"},{"gameIdentity","PRIVATE"},{"args",{{"password","PRIVATE"}}}});
 check(safe["reason"]=="loader-start-failed"&&safe["error"]==5,"reason and Windows error retained");
 check(!safe.contains("token")&&!safe.contains("endpoint")&&!safe.contains("args")&&!safe.contains("gameIdentity"),"secrets and mutation args excluded");
 check(ymcc::deckydiag::maxBytes==1024ull*1024ull,"bounded diagnostic size");
 // Managed receipt verification is the real startup consumer, not a separate model.
 const auto receipt=home/L".ymcc-managed.json";
 Json manifest={{"schemaVersion",1},{"owner","YMCC"},{"origin","official-release"},{"loaderVersion",ymcc::deckyidentity::version},{"loaderSha256",loaderSha},{"files",Json::array()}};
 for(const auto* name:managedFiles){file(home/name);manifest["files"].push_back({{"path",pathText(std::filesystem::path(name))},{"bytes",std::filesystem::file_size(home/name)},{"sha256",std::string(name==managedFiles[0]?loaderSha:std::string(64,'c'))}});}
 auto writeReceipt=[&](const Json& value){std::ofstream(receipt,std::ios::binary)<<value.dump();};
 const Digest managedDigest=[&](const std::wstring& path){return std::filesystem::path(path).filename()==L"PluginLoader_noconsole.exe"?std::string(loaderSha):std::string(64,'c');};
 writeReceipt(manifest);result=inspect(steam,home,managedDigest);
 check(result["ready"]==true&&result["managedPackageVerified"]==true,"exact paired package verified");
 check(ymcc::deckydiag::record("prepare",result)["managedPackageVerified"]==true,"package verification available in native diagnostics");
 auto broken=manifest;broken["loaderVersion"]="v99.0.0";writeReceipt(broken);check(inspect(steam,home,managedDigest)["reason"]=="resource-upgrade-required","native/package version mismatch cannot claim healthy");
 broken=manifest;broken["files"][0]["sha256"]=std::string(64,'f');writeReceipt(broken);check(inspect(steam,home,managedDigest)["reason"]=="resource-package-invalid","Loader entry cannot diverge from official identity");
 broken=manifest;broken["files"][1]["path"]="../outside";writeReceipt(broken);check(inspect(steam,home,managedDigest)["reason"]=="resource-package-invalid","receipt traversal rejected");
 broken=manifest;broken["files"][1]=broken["files"][0];writeReceipt(broken);check(inspect(steam,home,managedDigest)["reason"]=="resource-package-invalid","duplicate receipt path rejected");
 broken=manifest;broken["files"][1]["bytes"]=0;writeReceipt(broken);check(inspect(steam,home,managedDigest)["reason"]=="resource-package-invalid","invalid length rejected");
 writeReceipt(manifest);file(home/L"plugins/ymcc-sidebar/dist/index.js","changed size");check(inspect(steam,home,managedDigest)["reason"]=="resource-package-mismatch","corrupt plugin length detected before bootstrap");
 file(home/L"plugins/ymcc-sidebar/dist/index.js");check(inspect(steam,home,[&](const std::wstring& path){return std::filesystem::path(path).filename()==L"index.js"?std::string(64,'f'):managedDigest(path);})["reason"]=="resource-package-mismatch","equal-length corrupt plugin hash detected");
 std::filesystem::remove(home/L"LICENSE.decky-loader");check(inspect(steam,home,managedDigest)["reason"]=="resource-package-mismatch","partial package missing immutable asset detected");file(home/L"LICENSE.decky-loader");
 file(receipt,"not-json");check(inspect(steam,home,managedDigest)["reason"]=="resource-package-invalid","corrupt receipt fails closed");
 file(receipt,std::string(131073,'x').c_str());check(inspect(steam,home,managedDigest)["reason"]=="resource-package-invalid","oversized receipt cannot make unbounded read");
 std::filesystem::remove(receipt);result=inspect(steam,home,managedDigest);check(result["ready"]==true&&result["managedPackageVerified"]==false,"legacy install uses existing official identity until managed migration");
 // First upgrade through an OLD updater has package metadata but no receipt.
 const auto bundled=root/L"YeManCC"/L"decky-package.json";std::filesystem::create_directories(bundled.parent_path());
 std::ofstream(bundled,std::ios::binary)<<"\xef\xbb\xbf"<<manifest.dump();
 result=inspect(steam,home,managedDigest,bundled);check(result["ready"]==true&&result["managedPackageVerified"]==true&&result["managedPackageSource"]=="package","first old-updater upgrade verifies BOM package metadata without receipt");
 file(receipt,"broken receipt");check(inspect(steam,home,managedDigest,bundled)["reason"]=="resource-package-invalid","valid package never masks corrupt installed receipt");std::filesystem::remove(receipt);
 check(inspect(steam,home,managedDigest,root/L"missing-package.json")["reason"]=="resource-package-invalid","supplied missing package metadata cannot masquerade as verified legacy");
 check(inspect(steam,home,[&](const std::wstring& path){return std::filesystem::path(path).filename()==L"index.js"?std::string(64,'f'):managedDigest(path);},bundled)["reason"]=="resource-package-mismatch","first-upgrade fallback verifies plugin bytes too");
 broken=manifest;broken["loaderVersion"]="v99.0.0";std::ofstream(bundled,std::ios::binary)<<broken.dump();check(inspect(steam,home,managedDigest,bundled)["reason"]=="resource-upgrade-required","first upgrade still binds current native official version");
 std::ofstream(bundled,std::ios::binary)<<manifest.dump();std::filesystem::remove(marker);result=prepare(steam,home,managedDigest,false,{},bundled);
 check(result["ready"]==true&&result["managedPackageVerified"]==true&&result["managedPackageSource"]=="package","marker preparation retains first-upgrade verification on its final read");
 std::cout<<Json({{"cases",cases},{"status","passed"},{"fakeSteamDirectory",true},{"testDigestInjected",true},{"realSteamWrites",0},{"hardwareWrites",0},{"externalPython",false},{"externalNode",false}}).dump()<<std::endl;return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
