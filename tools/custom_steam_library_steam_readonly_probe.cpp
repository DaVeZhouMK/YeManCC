// Opt-in live verification. Never called by deterministic -RunSelfTests.
#define wmain unusedShippingWorkerMain
#include "../native/custom-steam-library/steam_artwork_lab.cpp"
#undef wmain
int wmain(int argc,wchar_t** argv){
    try{
        if(argc!=2)throw std::runtime_error("Pass a fresh Build probe root");
        const auto root=fs::absolute(argv[1]);const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
        if(!canonicalPathKey(root).starts_with(canonicalPathKey(build)+"\\")||fs::exists(root))throw std::runtime_error("Unsafe/reused live probe root");
        fs::create_directories(root);g_workerDataRoot=root/L"data";json cases=json::array();
        for(const auto id:{620,570}){
            json attempts=json::array();
            try{const auto details=fetchSteamDetails(id,attempts);const bool valid=details.canonicalAppId==id&&details.type=="game";cases.push_back({{"name","production-live-appdetails"},{"appId",id},{"passed",valid},{"nameReturned",details.name},{"canonicalAppId",details.canonicalAppId},{"networkAttempts",attempts}});}
            catch(const std::exception& e){cases.push_back({{"name","production-live-appdetails"},{"appId",id},{"passed",false},{"error",e.what()},{"networkAttempts",attempts}});}
        }
        size_t failed=0;for(const auto& c:cases)if(!c["passed"].get<bool>())++failed;
        json report={{"allPassed",failed==0},{"caseCount",cases.size()},{"failedCount",failed},{"cases",cases},{"network","live-official-steam-read-only"},{"realSteamFilesModified",false},{"steamStoppedOrLaunched",false}};
        writeJsonAtomic(root/L"summary.json",report);std::cout<<report.dump(2)<<"\n";return failed?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
