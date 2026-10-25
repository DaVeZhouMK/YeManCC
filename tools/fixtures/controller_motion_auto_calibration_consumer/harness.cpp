// Existing controlled globals/callbacks/JSON boundary are reused without changes.
#define main previous_consumer_main
#define scenarios previous_consumer_scenarios
#include "../legion_motion_consumer/harness.cpp"
#undef scenarios
#undef main
#include "scenarios.inl"
static const Scenario scenarios[]={
 {"four-models-and-sides",acFamilies},
 {"session-mode-api-cache",acSessionCache},
 {"steady-setting-cache-no-io",acSteady},
 {"lagging-request-flag-idempotent",acLaggingRequestFlag},
 {"manual-threshold-success-auto-restore",acSuccess},
 {"persist-failure-baseline-restore",acPersistFailure},
 {"manual-timeout-terminal-frame",acTimeout},
 {"cancel-withdraw-and-abort",acCancel},
 {"request-replacement-owns-mode",acReplacement},
 {"source-retirement-new-owner",acSourceRetirement},
 {"handle-and-callback-retirement",acCallbackRetirement},
 {"missing-callbacks-fail-closed",acMissingCallbacks},
 {"freshness-and-second-source-check",acFreshness},
 {"live-auto-baseline-rollback",acLiveBaseline},
 {"live-baseline-admission",acBaselineAdmission},
};
int main(int argc,char** argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);if(argc!=3)return 2;
 {std::ifstream input(argv[2]);input>>familyTopology;}
 calibrationPath=std::filesystem::path(argv[1])/"initial.json";std::filesystem::create_directories(calibrationPath.parent_path());
 unsigned index=0;int failedCases=0;json events=json::array();
 for(const auto& scenario:scenarios){
  const auto startChecks=checks,startFailures=failures;const std::string shortName="a"+std::to_string(++index);activeCase=shortName.c_str();
  acRestoreCallbacks();reset(index);activeCase=scenario.name;g_realStickTest.autoCalibrate=true;
  try{scenario.run();}catch(const std::exception& e){std::printf("FAIL %s fixture-exception=%s\n",activeCase,e.what());CHECK(false);}
  acRestoreCallbacks();inputCaptureLegionMotionSessionRevoke();CHECK(liveHandles()==0);CHECK(g_gm==windowsSentinel);CHECK(calibrationFiles.empty());
  events.push_back({{"case",activeCase},{"events",calibrationEvents},{"lifecycle",lifecycle}});
  const auto failed=failures-startFailures;if(failed)++failedCases;
  std::printf("CASE %s checks=%d failures=%d\n",activeCase,checks-startChecks,failed);
 }
 std::ofstream fx(calibrationPath.parent_path()/"effects.json");fx<<json(effects).dump(2);
 std::ofstream out(calibrationPath.parent_path()/"calibration-events.json");out<<events.dump(2);
 std::printf("SUMMARY cases=%zu checks=%d failures=%d failedCases=%d\n",std::size(scenarios),checks,failures,failedCases);return failures?1:0;
}
