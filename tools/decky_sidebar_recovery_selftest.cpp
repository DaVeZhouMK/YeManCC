#include "../native/decky_sidebar_recovery.h"
#include "../native/decky_sidebar_manual_repair.h"
#include "../native/decky_sidebar_diagnostics.h"
#include <vector>
#include <cassert>
#include <iostream>
int main(){using ymcc::deckysidebar::ConnectionRetryWindow;
 assert(ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-async-error"));assert(ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-deadline"));
 assert(ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-ws-closed"));assert(ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-ws-receive-failed"));assert(!ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-ws-invalid"));
 assert(!ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-owner-mismatch"));assert(!ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-readback-failed"));assert(!ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-canceled"));
 ConnectionRetryWindow window;assert(window.timeout(100)==INFINITE);
 ULONGLONG now=100;constexpr DWORD delays[]{250,1000,3000,10000,30000,30000,30000,30000};
 for(auto delay:delays){window.begin(now);assert(window.timeout(now)==delay);window.begin(now+10);assert(window.timeout(now)==delay);assert(!window.take(now+delay-1));assert(window.take(now+delay));assert(window.timeout(now+delay)==INFINITE);now+=delay;}
 window.begin(now);window.cancel();assert(window.timeout(now)==INFINITE);window.begin(now);assert(window.timeout(now)==30000);window.reset();window.begin(now);assert(window.timeout(now)==250);window.reset();assert(window.timeout(now)==INFINITE);
 using Fallback=ymcc::deckysidebar::ConnectionRecoveryWindow;using Action=Fallback::Action;
 const ymcc::deckysidebar::ConnectionHealth missing{true,false,"unknown-future-error",false,"steam-a:account-a:ready1"};
 Fallback ladder;now=100;
 assert(ladder.observe(now,missing)==Action::reconnect);
 for(unsigned i=0;i<10000;++i)assert(ladder.observe(now+1,missing)==Action::none);
 assert(ladder.timeout(now+1)==4999&&ladder.remaining(now+1)==6999);
 assert(ladder.observe(now+6999,missing)==Action::none);
 assert(ladder.observe(now+7000,missing)==Action::recycle);
 ladder.started(Action::recycle,now+7100); // Actual child creation, not the earlier restart request.
 ladder.started(Action::recycle,now+9000); // Duplicate receipt cannot refund20s.
 assert(ladder.observe(now+27099,missing)==Action::none);
 assert(ladder.observe(now+27100,missing)==Action::rebuild);
 ladder.started(Action::rebuild,now+27300); // Actual positive dispatch receipt.
 assert(ladder.observe(now+47299,missing)==Action::none);
 assert(ladder.observe(now+47300,missing)==Action::reopen);
 ladder.failed(Action::reopen,now+47301);
 assert(ladder.observe(now+47301,missing)==Action::exhausted);
 assert(ladder.observe(now+999999,missing)==Action::none); // No whole-Steam or second visible restart.
 assert(ladder.observe(now+1000000,{true,true,"actual-three-channel-ready",false,missing.identity})==Action::recovered);
 assert(ladder.remaining(now+1000000)==0&&ladder.stageBudget()==0);
 assert(ladder.observe(now+1000001,missing)==Action::reconnect);
 Fallback failed;assert(failed.observe(now,missing)==Action::reconnect);
 failed.failed(Action::reconnect,now+1);assert(failed.observe(now+1,missing)==Action::recycle);
 failed.failed(Action::reconnect,now+2);assert(failed.observe(now+2,missing)==Action::none); // Late old-stage failure.
 failed.failed(Action::recycle,now+3);assert(failed.observe(now+3,missing)==Action::rebuild); // No owned service skips dead wait.
 failed.failed(Action::rebuild,now+4);assert(failed.observe(now+4,missing)==Action::reopen);
 assert(failed.attempts()==4);
 auto officialOnly=missing;officialOnly.transportConnected=true;
 Fallback backend;assert(backend.observe(now,officialOnly)==Action::recycle); // Rebinding a healthy mirror cannot fix dead official WS.
 Fallback pendingAfterIdle;assert(pendingAfterIdle.observe(now,{false,false,"session-initializing"})==Action::none);
 assert(pendingAfterIdle.timeout(now)==30000);auto initialPending=missing;initialPending.paused=true;
 assert(pendingAfterIdle.observe(now+1,initialPending)==Action::none&&pendingAfterIdle.timeout(now+1)==5000); // Idle30s cannot hide newly pending proof.
 Fallback paused;assert(paused.observe(now,missing)==Action::reconnect);
 auto pending=missing;pending.required=false;pending.paused=true;
 assert(paused.observe(now+20000,pending)==Action::none&&paused.timeout(now+20000)>0);
 assert(paused.observe(now+20001,missing)==Action::recycle&&paused.attempts()==2);
 auto nextReady=missing;nextReady.identity="steam-a:account-a:ready2";
 assert(paused.observe(now+20002,nextReady)==Action::reconnect&&paused.attempts()==1);
 assert(paused.observe(now+20003,{false,false,"off"})==Action::stopped);
 Fallback stall;assert(stall.observe(now,missing)==Action::reconnect);
 assert(stall.observe(now+86400000,missing)==Action::recycle&&stall.attempts()==2);
 assert(stall.observe(now+86400001,missing)==Action::none); // One next step, no catch-up burst.
 Fallback cold;auto firstBoot=missing;firstBoot.coldLoaderStarted=now;
 assert(cold.observe(now+1000,firstBoot)==Action::reconnect);
 assert(!cold.serviceStarted(now+10000));cold.started(Action::reconnect,now+11000);
 assert(cold.observe(now+36999,firstBoot)==Action::none&&cold.remaining(now+36999)==1);
 assert(cold.observe(now+37000,firstBoot)==Action::recycle); // Initial31.371s cold case survives, deadline is not refilled.
 // Recorded transport-only samples; backend/hooks are independent inert ports
 // assumed already healthy here, NOT inferred from historical mirror records.
 // Calibration episodes SHA256: 884252b4137d2ef10f7c251ed2cc60bb69672da80120d202902425ee3bbe1456
 constexpr DWORD recordedRebindMs[]{32,30,910,107,121,66,123,26,34,73,27,94,116,54,1064,351,1303,56,90,77,3645,749,650,95,828,4065087,4432,264,1286,121,167,125,78,132,140,141,148,98,108,77,187,109,3688,109,1127,3658,122,151,385,174,5106,2421,2009,1406,123,231,155,122,218,92,2323,203,2737,1707,95,1799,4987,3985,4369,3198,2832,85,2364,4083,2509,3743,2866,107,1862,1315,2446,4682,105,2994,4075,4665,4763,2128,2182,2465,1336,184,4682,2434,123,260,114,3270,96,2937,2932,4270,106,122,2974,2433,4981,1262,2321,4959,2530,3270,4113,4740,4748,3761,1582,2067,2843,2632,2574,4674,184,4343,92,172,121,388,4628,4399,4077,90,89,579,1964,525,152,513,3739,5043,1109,593,82,2518,4131,1042,592,959,4613,4613,91,2295,1131,2519,124,201,3447,4995,4932,4034,1193,1824,1596,5128,1470,116,5008,2635,770,4698,4256,1916,92,3345,1118,2469,2164,229,82,1100,609,327,73,483,1770,91,84,121,108,124};
 constexpr DWORD recordedWakeMs[]{172,19,20,27,25,34,26,1924,1066,1333,843,1247,698,68,143,119,208,1916,166,1941,194,1730,1255,1656,273,1942,160,1926,259,1610,130,87,940,171,1693,2804,1813,1303,719,1944,1988,1753,1913,1994,1603,1024};
 constexpr DWORD recordedColdMs[]{30946,16195,31371,1625,16498,27440,29780,1015949,4323,2330,2387,8515,438,29,3,2681,7911,5686,539,3515,2309,2448,8673,2129,2271,2193,2122,2343,2957,2369,3050,2911,2855,2913,2274,7660,7281,2764,2485,2338,2913,2182,2293,2367,2300,7799,2713,2473,3054,2292,2412,3950,3164,2565,2261,2213,2302,1573,3527,3451,3497,2429,2376,2263,2848,2121,2492,2393,2145,3589,3931,97,2037,16,2,2050,2802,1944,1873,2099,1952,8180,4546,7711,7164,5481,8242,4301,6070,5052,4650,3898,1964,14191,13765,12020,11671,2311,13398,2614,3623,4494,3609,2857,2568,2127,2316,3800,1950,2338,3125,7474,2458,2640,3705,2957,1960,2968,2770,6876,2196,5412,4591,4828,3319,4402,4017,3998,3433,3853,2838,5551,4915,2617,2169,4779,2986,4157,2968,5037,2980,3206,2659,2169,3054,2199,3276,3360,3051,1960,4435,2000,3860,3641,6331,5505,5919,2188,2847,3730,3282,2352,4250,2356,2857,4840,4259,3236,3067,3183,4318,3063,2169,4412,2854,3793,2730,2911,4949,2600,5216,5720,3279,3294,2745,2774,3418,1969,3495,2637,2507,2630,2838,2848,2938,3031,3145,2684,2916,2981,2017,2139,7148,3469,3732,4321,2661,2281,3545,2441,2638,2636,3345,2691,2052,2104,2250,4349,2366,1858,2101,2077,4120,4561,12619,13099,2425,2355,4723,12018};
 constexpr DWORD recordedContextMs[]{13777,13707};
 unsigned historicalTimely=0,historicalLate=0;
 for(const auto elapsed:recordedRebindMs){Fallback trial;assert(trial.observe(now,missing)==Action::reconnect);
   if(elapsed<=Fallback::transportGraceMs){assert(trial.observe(now+elapsed-1,missing)==Action::none);assert(trial.observe(now+elapsed,{true,true,"fixture-ui-healthy-and-recorded-mirror"})==Action::recovered);++historicalTimely;}
   else {assert(trial.observe(now+Fallback::transportGraceMs,missing)==Action::recycle);++historicalLate;}}
 assert(historicalTimely==189&&historicalLate==1);
 for(const auto elapsed:recordedWakeMs){Fallback trial;assert(trial.observe(now,missing)==Action::reconnect);
   assert(trial.observe(now+elapsed-1,missing)==Action::none);assert(trial.observe(now+elapsed,{true,true,"fixture-ui-healthy-and-recorded-mirror"})==Action::recovered);}
 for(const auto elapsed:recordedColdMs){Fallback trial;auto h=missing;h.coldLoaderStarted=now;assert(trial.observe(now,h)==Action::reconnect);
   if(elapsed<Fallback::coldStartGraceMs){assert(trial.observe(now+elapsed-1,h)==Action::none);assert(trial.observe(now+elapsed,{true,true,"fixture-ui-healthy-and-recorded-mirror"})==Action::recovered);}
   else assert(trial.observe(now+Fallback::coldStartGraceMs,h)==Action::recycle);}
 for(const auto elapsed:recordedContextMs){Fallback trial;assert(trial.observe(now,missing)==Action::reconnect);trial.failed(Action::reconnect,now+1);assert(trial.observe(now+1,missing)==Action::recycle);
   trial.failed(Action::recycle,now+2);assert(trial.observe(now+2,missing)==Action::rebuild);trial.started(Action::rebuild,now+3);
   assert(trial.observe(now+3+elapsed-1,missing)==Action::none);assert(trial.observe(now+3+elapsed,{true,true,"fixture-ui-healthy-and-recorded-mirror"})==Action::recovered);}
 std::cout<<"Historical timing samples: rebind190 (189 timely/1 late kept), wake46, token-verified cold230, context2; transport-only evidence, inert UI ports\n";
 using Json=nlohmann::json;ymcc::steamsession::Session session;session.pid=77;session.created=88;session.root=L"C:\\inert";session.account=99;
 unsigned cycles=0;
 for(const std::string scenario:{"success","close-no-transition","close-rejected","open-no-transition","open-rejected","off-after-close","account-after-close","identity-missing","throws"}){
   ++cycles;std::vector<std::string>sent;unsigned long long tick=100;bool current=true;unsigned reads=0;
   const auto read=[&]()->Json{
     ++reads;
     if(scenario=="throws"&&!sent.empty())throw std::runtime_error("inert");
     std::string mode=sent.empty()?"bigpicture":sent.back();
     if(scenario=="close-no-transition"&&!sent.empty())mode="bigpicture";
     if(scenario=="open-no-transition"&&sent.size()==2)mode="desktop";
     return {{"known",true},{"running",true},{"pid",session.pid},{"birth",std::to_string(session.created)},
       {"mode",mode},{"observedAccount",scenario=="account-after-close"&&!sent.empty()?100:99},
       {"deckyAccountWatchAvailable",scenario!="identity-missing"}};
   };
   const auto result=ymcc::deckyrepair::bigpicture::run(session,[&]{return current;},read,[&](const std::string& mode){
     sent.push_back(mode);if(scenario=="off-after-close")current=false;
     return !(scenario=="close-rejected"||scenario=="open-rejected"&&mode=="bigpicture");
   },[&]{return tick;},[&]{tick+=500;});
   assert(result.value("ok",false)==(scenario=="success"));
   const auto expectedStage=scenario=="success"?"complete":scenario=="identity-missing"?"initial-read":
     scenario=="close-rejected"?"close-submit":scenario=="open-rejected"?"open-submit":
     scenario=="open-no-transition"?"open-readback":"close-readback";
   const auto expectedMode=scenario=="success"?"bigpicture":
     scenario=="identity-missing"||scenario=="account-after-close"||scenario=="throws"?"unknown":
     scenario=="open-rejected"||scenario=="open-no-transition"?"desktop":"bigpicture";
   assert(result.value("launchStage",std::string{})==expectedStage);
   assert(result.value("launchMode",std::string{})==expectedMode);
   const unsigned expectedReads=scenario=="success"?3:scenario=="close-no-transition"?25:
     scenario=="open-no-transition"?50:scenario=="open-rejected"||scenario=="account-after-close"||scenario=="throws"?2:1;
   assert(reads==expectedReads); // Diagnostics add no mode or identity reads.
   auto detail=result;detail["elapsedMs"]=tick-100;detail["token"]="must-not-log";
   const auto logged=ymcc::deckydiag::record("connection-recovery",detail);
   assert(logged.at("launchStage")==result.at("launchStage"));
   assert(logged.at("launchMode")==result.at("launchMode"));
   assert(logged.at("elapsedMs")==tick-100&&!logged.contains("token"));
   std::cout<<"BigPicture diagnostic "<<scenario<<" "<<expectedStage<<" "<<expectedMode<<" reads="<<reads<<"\n";
   if(scenario=="identity-missing"){assert(sent.empty()&&!result.value("mutated",true));}
   else {assert(result.value("mutated",false));assert(result.value("uncertain",false)==(scenario!="success"));}
   assert(sent.size()<=2);if(!sent.empty())assert(sent[0]=="desktop");
   if(sent.size()==2)assert(sent[1]=="bigpicture");
   if(scenario=="off-after-close"||scenario=="account-after-close"||scenario=="close-no-transition")assert(sent.size()==1);
 }
 Json runtime={{"enabled",true},{"pid",0u},{"revision",1},{"phase","waiting-manual-steam"},{"reason","decky-context-already-loaded"}};
 Json args={{"reopenBigPicture",true},{"pid",session.pid},{"birth",std::to_string(session.created)},{"revision",1},{"modeEpoch","doc:1"}};
 assert(ymcc::deckyrepair::requestMatches(args,session,runtime,ymcc::deckyrepair::Origin::ReopenBigPicture));
 assert(!ymcc::deckyrepair::requestMatches(args,session,runtime,ymcc::deckyrepair::Origin::FinalCleanup));
 unsigned reservedCount=0,reopenCount=0,preflightCount=0;bool consent=true;
 Json mode={{"known",true},{"running",true},{"pid",session.pid},{"birth",std::to_string(session.created)},
   {"mode","bigpicture"},{"modeEpoch","doc:1"},{"observedAccount",session.account},{"deckyAccountWatchAvailable",true},
   {"deckyFrontendLoaded",true},{"deckyEmptyFrontend",{{"protocol",1},{"frontendBuildId",std::string(64,'a')},{"loaderVersion","v3.3.1"}}}};
 ymcc::deckyrepair::Dependencies deps;deps.session=[&]{return session;};deps.runtime=[&]{return runtime;};
 deps.mode=deps.freshMode=[&](const auto&){return mode;};deps.installed=[]{return std::string(64,'a');};deps.loader="v3.3.1";
 deps.now=[]{return 100ull;};deps.exiting=[] {return false;};deps.cleanupAllowed=[&](const auto&){return consent;};deps.consent=deps.cleanupAllowed;
 deps.preflight=[&]{++preflightCount;return std::string{};};deps.reserveCleanup=[&](const auto&){++reservedCount;return true;};
 deps.reopenBigPicture=[&](const auto& current){++reopenCount;assert(current());return Json{{"ok",true},{"mutated",true}};};
 ymcc::deckyrepair::ManualRepair repair;
 assert(repair.request(args,deps,ymcc::deckyrepair::Origin::ReopenBigPicture).value("ok",false));
 assert(reservedCount==1&&reopenCount==1&&preflightCount==2); // One admission recheck, no hashing in mode waits.
 assert(!repair.request(args,deps,ymcc::deckyrepair::Origin::ReopenBigPicture).value("ok",false));
 assert(reservedCount==1&&reopenCount==1);
 Json reserved={{"schemaVersion",1},{"steamSession",ymcc::deckyupdate::ticket(session)}};
 Json document={{"deckyBigPictureRecovery",reserved}};
 assert(!ymcc::deckyrepair::cleanupDocumentAvailable(document,session,{},"deckyBigPictureRecovery"));
 assert(ymcc::deckyrepair::cleanupDocumentAvailable(document,session)); // Independent lower-tier fuse.
 std::cout<<"Decky ladder + observed Big Picture cycle PASS: nine bounded scenarios; no Steam/OS mode commands\n";
 std::cout<<"Decky outage recovery PASS: capped retry, coalescing, cancel, health reset; no real process IO\n";
}
