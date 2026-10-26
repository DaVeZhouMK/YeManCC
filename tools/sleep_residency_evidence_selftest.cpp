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
static void productionCapture(bool leaving, const std::string& xml) {
    const unsigned long long recordedFileTime = 123456789;
#include "production_capture_hook.inc"
}
#include "abnormal_native_harness.inc"
#ifndef YMCC_SLEEP_EXPORT_DIAGNOSTICS_SHARED
#include "sleep_export_harness.inc"
#endif
#include "static_wake_harness.inc"
#include "static_wake_schedule_harness.inc"
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("expected cases.json and result.json");
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
        // Historical engine is explicitly isolated from the real production hooks.
        const auto runtimeChecks=abnormalRuntimeTests(fixture);
        const auto conservativeProductionChecks=abnormalConservativeProductionTests();
        check(conservativeProductionChecks>0,"real conservative production entry consumer executes nonzero checks");
        const auto staticWakeChecks=staticWakePolicyTests();
        check(staticWakeChecks>0,"static wake harness executes nonzero checks");
        const auto staticWakeScheduleChecks=staticWakeScheduleTests();
        check(staticWakeScheduleChecks>0,"static wake schedule harness executes nonzero checks");
        json result = {{"sleepExportDiagnosticChecks",sleepExportDiagnosticChecks},{"realSleepStudyQuery",false},{"conservativeProductionChecks",conservativeProductionChecks},{"historicalStagedEngineChecks",runtimeChecks},{"staticWakeScheduleChecks",staticWakeScheduleChecks},{"staticWakeChecks",staticWakeChecks},{"runtimeChecks",runtimeChecks},{"status", "passed"}, {"checks", sleepEvidenceChecks}, {"cases", fixture.at("cases").size()},
            {"caseResults", outcomes}, {"actualSleepCalls", 0}, {"hardwareCalls", 0},
            {"scope", "actual completed507 accounting and conservative production hooks; staged engine is historical-only explicit StartPhase regression; static companions separately scoped; Windows boundaries mocked; export-only WindowsSleepStudy adapter uses inert commands and owned local files"}};
        std::ofstream output(argv[2]); output << result.dump(2) << '\n';
        std::cout << json{{"status", "passed"}, {"checks", sleepEvidenceChecks}, {"cases", fixture.at("cases").size()}}.dump() << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
