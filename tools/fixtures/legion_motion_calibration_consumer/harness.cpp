#include "windows_motion_submission.h"
// Reuse read-only existing topology/native effect boundaries; new scenarios only.
#define main previous_consumer_main
#define scenarios previous_consumer_scenarios
#include "../legion_motion_consumer/harness.cpp"
#undef scenarios
#undef main
#include "scenarios.inl"
static const Scenario scenarios[] = {
    {"saved-trust-restored-new-epoch",savedTrustRestoredAcrossEpoch},
    {"legacy-offset-not-trusted",legacyAndMismatchedTrust},
    {"four-model-calibration",fourModelsCalibrate},
    {"request-missing-stale",missingAndStale},
    {"request-route-busy",routeAndBusy},
    {"request-before-first-handle",requestBeforeHandle},
    {"pre-request-receipt-no-training",preRequestSamples},
    {"repeat-receipt-timeout",repeatedReceiptAndTimeout},
    {"left-right-persistence-preserves-windows",persistenceRecords},
    {"atomic-boundary-failures",atomicFailures},
    {"invalid-persistence-refused",invalidPersistence},
    {"save-before-apply-and-ui-result",successOrdering},
    {"write-failure-rollback-not-saved",failedSaveRollback},
    {"confidence-threshold-invalid",lowAndInvalidConfidence},
    {"invalid-offset-no-store",invalidOffset},
    {"pending-source-retirement",pendingSourceRetirement},
    {"active-source-retirement",activeSourceRetirement},
    {"request-replaced-ui-id-fence",requestReplacement},
    {"log-exception-still-destroys-owner",loggingFailureStillDestroys},
    {"create-failure-cancels-request",createFailureCancels},
    {"missing-read-cancels-request",missingReadCancels},
    {"missing-calibration-callback",missingCalibrationCallback},
    {"source-retired-after-persist",retirementDuringReplace},
    {"stop-aborts-before-destroy",abortAndStop},
};
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    std::set_terminate([](){try{auto e=std::current_exception();if(e)std::rethrow_exception(e);}catch(const std::exception& e){std::fprintf(stderr,"UNEXPECTED_FIXTURE_EXCEPTION: %s\n",e.what());}catch(...){std::fprintf(stderr,"UNEXPECTED_FIXTURE_EXCEPTION: unknown\n");}std::abort();});
    if(argc!=3)return 2;{std::ifstream f(argv[2]);f>>familyTopology;}
    calibrationPath=std::filesystem::path(argv[1])/"initial.json";std::filesystem::create_directories(calibrationPath.parent_path());
    unsigned index=0;int failedCases=0;json caseEvents=json::array();
    for(const auto& scenario:scenarios){activeCase=scenario.name;const auto beforeChecks=checks,beforeFailures=failures;
        const auto* scenarioName=activeCase;const std::string shortName="c"+std::to_string(++index);
        activeCase=shortName.c_str();reset(index);activeCase=scenarioName;scenario.run();inputCaptureLegionMotionSessionRevoke();CHECK(liveHandles()==0);CHECK(g_gm==windowsSentinel);CHECK(calibrationFiles.empty());
        caseEvents.push_back({{"case",activeCase},{"events",calibrationEvents},{"lifecycle",lifecycle}});
        const auto failed=failures-beforeFailures;if(failed)++failedCases;
        std::printf("CASE %s checks=%d failures=%d\n",activeCase,checks-beforeChecks,failed);}
    std::ofstream out(calibrationPath.parent_path()/"effects.json");out<<json(effects).dump(2);
    std::ofstream events(calibrationPath.parent_path()/"calibration-events.json");events<<caseEvents.dump(2);
    std::printf("SUMMARY cases=%zu checks=%d failures=%d failedCases=%d\n",sizeof(scenarios)/sizeof(scenarios[0]),checks,failures,failedCases);
    return failures==0?0:1;
}
