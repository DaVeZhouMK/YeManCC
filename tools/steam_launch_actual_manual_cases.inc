#ifndef YMCC_NOIO_CASE_DIR
#error Actual-manual selection requires the reviewed NoIO adapter
#endif
int main(int argc,char** argv){
 if(argc!=2||(std::string(argv[1])!="--actual-manual"&&std::string(argv[1])!="--actual-manual-first"))return 2;
 const bool firstOnly=std::string(argv[1])=="--actual-manual-first";
 const pathfs::path scratch=ymcc_noio::scratch();pathfs::create_directories(scratch);
 const auto pc=(scratch/L"PowerControl").wstring();
 nlohmann::json rows=nlohmann::json::array();unsigned originalChecks=0,cleanupChecks=0,terminalChecks=0;std::string failure;
 auto& state=ymcc_noio::state();
 const auto need=[&](bool okay,const char* why){if(!okay)throw std::runtime_error(why);};
 const auto check=[&](const char* name,const auto& body){
  if(!failure.empty()||(firstOnly&&!rows.empty()))return;
  try{++originalChecks;need(body(),"original actual-manual assertion failed");
   ++cleanupChecks;need(state.mutexes.empty()&&state.acquiredMutexes.empty(),"memory mutex debt after original case");
   ++cleanupChecks;need(state.mutexCreates==state.mutexCloses&&state.mutexWaits==state.mutexReleases,"memory mutex lifetime counts");
   ++cleanupChecks;need(state.denied==0,"unexpected denied terminal");rows.push_back({{"name",name},{"passed",true}});
  }catch(const std::exception& e){failure=e.what();rows.push_back({{"name",name},{"passed",false},{"error",failure}});}
 };
const auto manualSteam=(scratch/L"steam.exe").wstring();std::ofstream(manualSteam).put('x');
const auto ownerFacts=memorySteamLaunchFacts();
check("actual manual creator refusal uses one fixed memory Shell dispatch",[&]{auto& ports=ymcc_noio::state();ports.manualSteamMode=true;ports.manualSteamPresent=false;ports.manualShellOkay=true;const auto primary=ports.manualPrimaryCalls,shell=ports.manualShellCalls;WindowsPorts io(pc,scratch.wstring(),{},{},ownerFacts);const bool ok=io.spawnManualSteam(manualSteam,startupArguments);const auto diagnostic=io.steamLaunchDiagnostics();ports.manualSteamMode=false;return ok&&ports.manualPrimaryCalls==primary+1&&ports.manualShellCalls==shell+1&&diagnostic["primaryStage"]=="create-process"&&diagnostic["primaryError"]==ERROR_ACCESS_DENIED&&diagnostic["fallbackStage"]=="shell-dispatched";});
check("actual manual Shell failure retains precise Win32 refusal",[&]{auto& ports=ymcc_noio::state();ports.manualSteamMode=true;ports.manualSteamPresent=false;ports.manualShellOkay=false;const auto shell=ports.manualShellCalls;WindowsPorts io(pc,scratch.wstring(),{},{},ownerFacts);const bool ok=io.spawnManualSteam(manualSteam,startupArguments);const auto detail=io.steamLaunchFailure();ports.manualSteamMode=false;ports.manualShellOkay=true;return !ok&&ports.manualShellCalls==shell+1&&detail.find("primary=create-process/5")!=detail.npos&&detail.find("fallback=shell-execute/5")!=detail.npos;});
check("actual manual fallback absence guard rejects a new Steam instance",[&]{auto& ports=ymcc_noio::state();ports.manualSteamMode=true;ports.manualSteamPresent=true;const auto shell=ports.manualShellCalls;WindowsPorts io(pc,scratch.wstring(),{},{},ownerFacts);const bool ok=io.spawnManualSteam(manualSteam,startupArguments);ports.manualSteamMode=false;ports.manualSteamPresent=false;return !ok&&ports.manualShellCalls==shell&&io.steamLaunchDiagnostics()["fallbackStage"]=="steam-absence-unverified";});

 if(failure.empty()&&!firstOnly){
  const auto test=[&](bool okay,const char* why){++terminalChecks;need(okay,why);};
  try{
   const auto deniedBefore=state.denied;
   test(ymcc_noio::createMutex(nullptr,FALSE,nullptr)==nullptr,"null mutex name accepted");
   test(ymcc_noio::createMutex(nullptr,FALSE,L"Local\\Foreign") == nullptr,"foreign mutex name accepted");
   test(ymcc_noio::createMutex(nullptr,TRUE,L"Local\\YeManSteamArtworkLabShortcutWriter")==nullptr,"initial ownership accepted");
   SECURITY_ATTRIBUTES attrs{};test(ymcc_noio::createMutex(&attrs,FALSE,L"Local\\YeManSteamArtworkLabShortcutWriter")==nullptr,"mutex attributes accepted");
   const auto h=ymcc_noio::createMutex(nullptr,FALSE,L"Local\\YeManSteamArtworkLabShortcutWriter");test(h!=nullptr,"valid memory mutex refused");
   test(ymcc_noio::wait(h,30001)==WAIT_FAILED,"unbounded memory wait accepted");
   test(!ymcc_noio::releaseMutex(h),"unowned mutex released");
   test(ymcc_noio::wait(h,30000)==WAIT_OBJECT_0,"bounded memory wait refused");
   test(!ymcc_noio::close(h),"held mutex closed");
   test(ymcc_noio::releaseMutex(h)!=FALSE,"owned mutex release failed");
   test(!ymcc_noio::releaseMutex(h),"duplicate mutex release accepted");
   test(ymcc_noio::close(h)!=FALSE,"memory close failed");
   test(ymcc_noio::wait(h,0)==WAIT_FAILED,"stale handle wait accepted");
   test(!ymcc_noio::releaseMutex(h),"stale handle release accepted");
   test(!ymcc_noio::close(h),"stale handle close accepted");
   test(state.denied==deniedBefore+11,"expected memory denials missing");
   test(state.mutexes.empty()&&state.acquiredMutexes.empty(),"terminal control leaked memory handle");
   test(state.mutexCreates==state.mutexCloses&&state.mutexWaits==state.mutexReleases,"terminal control lifetime debt");
  }catch(const std::exception& e){failure=e.what();}
 }
 std::cout<<nlohmann::json{{"selection","actual-manual-noio-successor"},{"cases",rows},{"originalChecks",originalChecks},{"cleanupChecks",cleanupChecks},{"terminalChecks",terminalChecks},{"failure",failure},{"expectedTerminalDenials",firstOnly?0:11},{"memoryMutexCreates",state.mutexCreates},{"memoryMutexCloses",state.mutexCloses},{"memoryMutexWaits",state.mutexWaits},{"memoryMutexReleases",state.mutexReleases},{"realSteamActions",0},{"actualChildCreated",state.createCalls},{"defaultGroupsExecuted",false}}.dump()<<"\n";
 return failure.empty()?0:23;
}
