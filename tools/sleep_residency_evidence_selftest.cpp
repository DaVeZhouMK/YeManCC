#define NOMINMAX
#include "sleep_residency_evidence.h"
#include "sleep_export_diagnostics.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
using json = nlohmann::json;
static unsigned sleepEvidenceChecks = 0;
static void check(bool ok, const std::string& name) {
    ++sleepEvidenceChecks;
    if (!ok) throw std::runtime_error("CHECK_FAILED: " + name);
}
static std::vector<std::pair<std::string, json>> captured;
static unsigned long long testPowerGeneration=771;
static unsigned long long currentPowerGeneration() { return testPowerGeneration; }
static std::string sgFormatFactFileTime(unsigned long long) { return "fixture-original-event-time"; }
static void (*runtimeFactObserver)(const char*,const json&)=nullptr;
static void sgRecordFact(const char* event, const json& details) { captured.emplace_back(event, details);if(runtimeFactObserver)runtimeFactObserver(event,details); }
#ifdef YMCC_SLEEP_TIMEOUT_NATIVE_SHARED
static void sgSleepTimeoutObserveCompleted(const json&,unsigned long long) noexcept {}
#else
static void sgSleepTimeoutObserveCompleted(const json&,unsigned long long) noexcept;
#endif
static void productionCapture(bool leaving, const std::string& xml, unsigned long long eventFileTime=123456789) {
    const unsigned long long recordedFileTime = eventFileTime;
#include "production_capture_hook.inc"
}
#if !defined(YMCC_SLEEP_TIMEOUT_ONLY) && !defined(YMCC_SLEEP_LATE_WAKE_ONLY)
#include "abnormal_native_harness.inc"
#ifndef YMCC_SLEEP_ACTIVITY_NATIVE_SHARED
#include "sleep_activity_native_harness.inc"
#endif
#ifndef YMCC_SLEEP_EXPORT_DIAGNOSTICS_SHARED
#include "sleep_export_harness.inc"
#endif
#include "static_wake_harness.inc"
#include "static_wake_schedule_harness.inc"
#endif
#ifndef YMCC_SLEEP_TIMEOUT_NATIVE_SHARED
#include "sleep_timeout_native_harness.inc"
#endif
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
#include "latewake_cases.inc"
#endif
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("expected cases.json and result.json");
#ifdef YMCC_SLEEP_LATE_WAKE_ONLY
        std::ifstream payloadInput(argv[1]);json latePayload;payloadInput>>latePayload;
        const auto lateWakeNativeChecks=lateWakeNativeTests(latePayload);
        check(lateWakeNativeChecks>0,"late-wake exact group executes actual decisions and sender");
        json result={{"status","passed"},{"componentScope","late-wake"},{"checks",sleepEvidenceChecks},
            {"lateWakeNativeChecks",lateWakeNativeChecks},{"lateWakeNativeScenarios",sleep_timeout_fixture::lateScenarios},
            {"cases",sleep_timeout_fixture::lateScenarios.size()},{"caseResults",sleep_timeout_fixture::lateScenarios},
            {"sleepTimeoutNativeChecks",0},{"sleepTimeoutNativeScenarios",json::array()},
            {"sleepActivityNativeChecks",0},{"sleepExportDiagnosticChecks",0},{"runtimeChecks",0},
            {"conservativeProductionChecks",0},{"historicalStagedEngineChecks",0},{"staticWakeChecks",0},{"staticWakeScheduleChecks",0},
            {"actualSleepCalls",0},{"hardwareCalls",0},{"realSleepStudyQuery",false},
            {"scope","Exact late-wake native decision/S0 branch/manual callback/shared sender with inert Win32 boundaries; payload field replays are software evidence, not physical sleep or root-cause proof."}};
        std::ofstream output(argv[2]);output<<result.dump(2)<<'\n';std::cout<<json{{"status","passed"},{"componentScope","late-wake"},{"checks",sleepEvidenceChecks},{"cases",sleep_timeout_fixture::lateScenarios.size()}}.dump()<<'\n';
#elif defined(YMCC_SLEEP_TIMEOUT_ONLY)
        const auto sleepTimeoutNativeChecks=sleepTimeoutNativeTests();
        check(sleepTimeoutNativeChecks>0,"timeout exact group executed actual header/native/sender consumers");
        json result={{"status","passed"},{"componentScope","timeout"},{"checks",sleepEvidenceChecks},
            {"sleepTimeoutNativeChecks",sleepTimeoutNativeChecks},{"sleepTimeoutNativeScenarios",sleep_timeout_fixture::scenarios},
            {"cases",sleep_timeout_fixture::scenarios.size()},{"caseResults",sleep_timeout_fixture::scenarios},
            {"sleepActivityNativeChecks",0},{"sleepExportDiagnosticChecks",0},{"runtimeChecks",0},
            {"conservativeProductionChecks",0},{"historicalStagedEngineChecks",0},{"staticWakeChecks",0},{"staticWakeScheduleChecks",0},
            {"actualSleepCalls",0},{"hardwareCalls",0},{"realSleepStudyQuery",false},
            {"scope","Exact timeout group: actual policy/native include, original507 hook, current-plan action reader, shared sender, scheduler, Start/Advance/dispatch; inert Windows power/timer/privilege ports. No physical sleep acceptance; other residency groups NOT_RUN."}};
        std::ofstream output(argv[2]);output<<result.dump(2)<<'\n';std::cout<<json{{"status","passed"},{"componentScope","timeout"},{"checks",sleepEvidenceChecks},{"cases",sleep_timeout_fixture::scenarios.size()}}.dump()<<'\n';
#else
        std::ifstream input(argv[1]); json fixture; input >> fixture;
        json outcomes = json::array();
        // Actual507 capture -> ordinary fact -> export selector. This producer
        // join runs in every original mutation, independently of IO sharing.
        productionCapture(true,"<Event><EventData><Data Name=\"DurationInUs\">120000000</Data><Data Name=\"DripsResidencyInUs\">0</Data><Data Name=\"HwDripsResidencyInUs\">0</Data><Data Name=\"DripsTransitions\">0</Data><Data Name=\"SleepEntered\">true</Data></EventData></Event>");
        check(captured.size()==1,"actual507 capture produces one source fact for export selection");
        const json joined={{"event",captured.front().first},{"writerInstanceId","production-capture-join"},{"details",captured.front().second}};
        check(ymcc::sleepexport::selectAnomaly({joined.dump()})==joined,"actual507 ordinary evidence reaches export selector without a separate schema model");
        captured.clear();
        for (const auto& c : fixture.at("cases")) {
            const auto name = c.at("name").get<std::string>();
            const auto evidence = ymcc::sleep_residency::makeEvidence(c.at("xml").get<std::string>());
            check(evidence.at("assessment") == c.at("expectedAssessment"), name + ": assessment");
            check(evidence.at("automaticRepairAuthorized") == false, name + ": diagnostics never authorize sleep");
            check(evidence.at("hardwareZeroIsConclusive") == false, name + ": hardware validity remains explicit");
            const auto& expected = c.at("expected");
            if (expected.contains("expectedHardwarePercent"))
                check(std::abs(evidence.at("hardwareDripsPercent").get<double>() - expected.at("expectedHardwarePercent").get<double>()) < 0.000001, name + ": hardware percent");
            if (expected.value("hardwarePercentAbsent", false)) check(!evidence.contains("hardwareDripsPercent"), name + ": no fabricated hardware ratio");
            if (expected.contains("expectedDurationUs")) check(evidence.at("values").at("DurationInUs") == expected.at("expectedDurationUs"), name + ": full64bit duration");
            if (expected.contains("expectedWindowsSleepStage")) check(evidence.at("windowsSleepStage") == expected.at("expectedWindowsSleepStage"), name + ": independent Windows phase");
            if (expected.contains("invalidField")) {
                const auto& invalid = evidence.at("invalidFields");
                check(std::find(invalid.begin(), invalid.end(), expected.at("invalidField")) != invalid.end(), name + ": invalid field retained");
            }
            if (expected.value("expectedFlagFields", false)) {
                check(evidence.at("values").at("PowerStateAc") == true, name + ": AC boolean");
                check(evidence.at("values").at("IsCsSessionInProgressOnExit") == false, name + ": session boolean");
            }
            productionCapture(false, c.at("xml").get<std::string>());
            check(captured.empty(), name + ": non507 emits no completed-session assessment");
            productionCapture(true, c.at("xml").get<std::string>());
            check(captured.size() == 1 && captured.front().first == "s0-low-power-residency", name + ": actual callback snippet emits one fact");
            check(captured.front().second.at("generation") == 771 && captured.front().second.at("fileTime") == "fixture-original-event-time", name + ": native context preserved");
            auto emitted = captured.front().second; emitted.erase("generation"); emitted.erase("fileTime");
            check(emitted == evidence, name + ": emitted assessment is the production header result");
            captured.clear(); outcomes.push_back({{"name", name}, {"evidence", evidence}});
        }
#ifdef YMCC_SLEEP_EXPORT_DIAGNOSTICS_SHARED
        const unsigned sleepExportDiagnosticChecks=0; // Original none child owns this independent companion.
#else
        const auto sleepExportDiagnosticChecks=sleepExportDiagnosticTests();
        check(sleepExportDiagnosticChecks>0,"export-only helper and actual native adapter execute nonzero checks");
#endif
#ifdef YMCC_SLEEP_ACTIVITY_NATIVE_SHARED
        const unsigned sleepActivityNativeChecks=0; // Only the original none child compiles and executes this group.
#else
        const auto sleepActivityNativeChecks=sleepActivityNativeTests();
        check(sleepActivityNativeChecks>0,"formal activity header and native observer execute nonzero checks");
#endif
#ifdef YMCC_SLEEP_TIMEOUT_NATIVE_SHARED
        const unsigned sleepTimeoutNativeChecks=0;
        const json sleepTimeoutNativeScenarios=json::array();
#else
        const auto sleepTimeoutNativeChecks=sleepTimeoutNativeTests();
        const auto sleepTimeoutNativeScenarios=sleep_timeout_fixture::scenarios;
        check(sleepTimeoutNativeChecks>0,"formal timeout policy/native include and shared sender execute nonzero checks");
#endif
#ifdef YMCC_SLEEP_LATE_WAKE_ENABLE
        std::ifstream latePayloadInput("amd_rog_late_spontaneous_ac.json");json latePayload;latePayloadInput>>latePayload;
        const auto lateWakeNativeChecks=lateWakeNativeTests(latePayload);
        const auto lateWakeNativeScenarios=sleep_timeout_fixture::lateScenarios;
#else
        const unsigned lateWakeNativeChecks=0;const json lateWakeNativeScenarios=json::array();
#endif
        // Historical engine is explicitly isolated from the real production hooks.
        const auto runtimeChecks=abnormalRuntimeTests(fixture);
        const auto conservativeProductionChecks=abnormalConservativeProductionTests();
        check(conservativeProductionChecks>0,"real conservative production entry consumer executes nonzero checks");
        const auto staticWakeChecks=staticWakePolicyTests();
        check(staticWakeChecks>0,"static wake harness executes nonzero checks");
        const auto staticWakeScheduleChecks=staticWakeScheduleTests();
        check(staticWakeScheduleChecks>0,"static wake schedule harness executes nonzero checks");
        json result = {{"lateWakeNativeChecks",lateWakeNativeChecks},{"lateWakeNativeScenarios",lateWakeNativeScenarios},{"sleepTimeoutNativeChecks",sleepTimeoutNativeChecks},{"sleepTimeoutNativeScenarios",sleepTimeoutNativeScenarios},{"sleepActivityNativeChecks",sleepActivityNativeChecks},{"sleepExportDiagnosticChecks",sleepExportDiagnosticChecks},{"realSleepStudyQuery",false},{"conservativeProductionChecks",conservativeProductionChecks},{"historicalStagedEngineChecks",runtimeChecks},{"staticWakeScheduleChecks",staticWakeScheduleChecks},{"staticWakeChecks",staticWakeChecks},{"runtimeChecks",runtimeChecks},{"status", "passed"}, {"checks", sleepEvidenceChecks}, {"cases", fixture.at("cases").size()},
            {"caseResults", outcomes}, {"actualSleepCalls", 0}, {"hardwareCalls", 0},
            {"scope", "actual completed507 accounting and conservative production hooks; UI event-driven activity header/native observer and fact dispatcher; staged engine is historical-only explicit StartPhase regression; static companions separately scoped; Windows boundaries mocked; export-only WindowsSleepStudy adapter uses inert commands and owned local files"}};
        std::ofstream output(argv[2]); output << result.dump(2) << '\n';
        std::cout << json{{"status", "passed"}, {"checks", sleepEvidenceChecks}, {"cases", fixture.at("cases").size()}}.dump() << '\n';
        #endif
        return 0;
    } catch (const std::exception& error) {
#ifdef YMCC_SLEEP_LATE_WAKE_ONLY
        if(argc==3){const json failure={{"status","failed"},{"componentScope","late-wake"},{"checks",sleepEvidenceChecks},
            {"lateWakeNativeScenarios",sleep_timeout_fixture::lateScenarios},{"cases",sleep_timeout_fixture::lateScenarios.size()},
            {"caseResults",sleep_timeout_fixture::lateScenarios},{"failure",error.what()},{"actualSleepCalls",0},{"hardwareCalls",0}};
            std::ofstream output(argv[2]);output<<failure.dump(2)<<'\n';}
#endif
        std::cerr<<error.what()<<'\n';return 1;
    }
}
