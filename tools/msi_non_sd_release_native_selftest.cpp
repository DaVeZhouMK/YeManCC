// Standalone fake boundary test. The runner extracts production source into
// output-only includes; no Native TU, Windows, SDK, device or observer is used.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <json.hpp>
#include "actual-source-metadata.hpp"

namespace allocationFault {
inline bool enabled = false;
inline std::int64_t failAt = -1;
inline std::uint64_t attempts = 0;
inline std::uint64_t failures = 0;
void* allocate(std::size_t size) {
    if (enabled) {
        const auto ordinal = attempts++;
        if (failAt >= 0 && ordinal == static_cast<std::uint64_t>(failAt)) {
            ++failures;
            throw std::bad_alloc{};
        }
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc{};
}
}
// Fail allocations only in a scoped logger/handoff call. The actual production
// alias nlohmann::json and its normal allocator are retained.
void* operator new(std::size_t size) { return allocationFault::allocate(size); }
void* operator new[](std::size_t size) { return allocationFault::allocate(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

using json = nlohmann::json;
struct Calls {
    std::uint64_t logger = 0, authority = 0, powerGeneration = 0, persona = 0, profile = 0;
    std::uint64_t wrapper = 0, sinkBody = 0, sinkExceptionsRaised = 0, allocationAttempts = 0, allocationFailures = 0;
    json asJson() const {
        return {{"msiNonSdReleaseReceiptLog", logger}, {"msiClawKnownBoardAuthority", authority},
                {"inputHostProtocolPowerGenerationLocked", powerGeneration},
                {"inputHostBoundOrCurrentPersona", persona}, {"inputHostBoundOrCurrentProfileIdentity", profile},
                {"inputHostCommandResultLog", wrapper}, {"fakeLifecycleSinkBody", sinkBody}, {"fakeSinkExceptionsRaised", sinkExceptionsRaised},
                {"scopedAllocationAttempts", allocationAttempts}, {"injectedAllocationFailures", allocationFailures}};
    }
};
static Calls g_calls;
static bool g_inputHostMsiStageRequested = true;
static bool g_fakeBoardAuthority = true, g_fakeAuthorityThrows = false, g_fakeSinkThrows = false;
static bool msiClawKnownBoardAuthority() {
    ++g_calls.authority;
    if (g_fakeAuthorityThrows) throw std::runtime_error("fake cached-authority failure");
    return g_fakeBoardAuthority;
}
static const std::string g_inputHostRunId = "fake-run";
static constexpr unsigned long long g_inputHostEpoch = 7, sequenceValue = 13, g_inputHostConfigRevision = 3;
static const std::string requestIdValue = "fake-request";
static const std::string g_inputHostStartupNonce = "fake-nonce", g_inputHostHostInstanceId = "fake-host";
static constexpr const char* kInputHostTargetId = "fake-target";
static constexpr const char* kInputHostOwnerIdentity = "fake-owner";
static constexpr const char* kInputHostProtocolConfigHash = "fake-config";
static constexpr const char* kInputHostLockedCoreSha256 = "fake-core-sha";
static constexpr const char* kInputHostLockedCoreMvid = "fake-core-mvid";
static std::string g_fakePersona = "xbox360";
static const std::string g_fakeProfile = "fake-profile";
static unsigned long long inputHostProtocolPowerGenerationLocked() { ++g_calls.powerGeneration; return 11; }
static const std::string& inputHostBoundOrCurrentPersona() { ++g_calls.persona; return g_fakePersona; }
static const std::string& inputHostBoundOrCurrentProfileIdentity() { ++g_calls.profile; return g_fakeProfile; }
struct FakeDecision {
    bool tupleAccepted, businessAccepted;
    bool operator==(const FakeDecision&) const = default;
};
static void fakeLifecycleSinkBody(const char* event, const json& detail) {
    (void)event; (void)detail;
    if (g_fakeSinkThrows) { ++g_calls.sinkExceptionsRaised; throw std::runtime_error("fake log body exception"); }
}
#include "actual-msi-non-sd-release-receipt-log.inc"
#include "actual-receipt-control.inc"
#include "actual-lifecycle-handoff.inc"
static_assert(noexcept(msiNonSdReleaseReceiptLog(std::declval<json&>(), std::declval<const json&>(), nullptr, false)));

struct AllocationScope {
    explicit AllocationScope(std::int64_t fail = -1) {
        allocationFault::attempts = 0; allocationFault::failures = 0;
        allocationFault::failAt = fail; allocationFault::enabled = true;
    }
    ~AllocationScope() {
        allocationFault::enabled = false;
        g_calls.allocationAttempts += allocationFault::attempts;
        g_calls.allocationFailures += allocationFault::failures;
    }
    AllocationScope(const AllocationScope&) = delete;
    AllocationScope& operator=(const AllocationScope&) = delete;
};
static void invoke(json& row, const json& response, const char* command, bool tuple = true) {
    ++g_calls.logger;
    msiNonSdReleaseReceiptLog(row, response, command, tuple);
}
static json evidence() {
    return {{"version", 1}, {"observationState", "generic-count-only-absent"},
            {"exactOwnedIdsPinned", false}, {"classificationComplete", false}, {"exactOwnedAbsenceProven", false},
            {"cachedBefore", {{"primaryRootCount", 4}, {"detail", "cached-before"}}},
            {"freshObservations", json::array({{{"source", "existing-poll"}, {"detail", "汉🙂"}, {"count", 0}}})}};
}
static json responseFixture(const std::string& persona = "xbox360", const std::string& command = "RELEASE_TARGET") {
    return {{"protocolVersion", 1}, {"command", command}, {"requestId", requestIdValue}, {"sequence", sequenceValue},
            {"runId", g_inputHostRunId}, {"epoch", g_inputHostEpoch}, {"powerGeneration", 11},
            {"targetId", kInputHostTargetId}, {"ownerIdentity", kInputHostOwnerIdentity}, {"persona", persona},
            {"profileIdentity", g_fakeProfile}, {"startupNonce", g_inputHostStartupNonce},
            {"configRevision", g_inputHostConfigRevision}, {"configHash", kInputHostProtocolConfigHash},
            {"assetVerified", true}, {"lockedCoreSha256", kInputHostLockedCoreSha256}, {"lockedCoreMvid", kInputHostLockedCoreMvid},
            {"hostInstanceId", g_inputHostHostInstanceId}, {"ok", true}, {"status", "expected-ok"}, {"detail", "original"},
            {"targetPnpAdmitted", true}, {"msiNonSdReleaseEvidenceVersion", 1}, {"msiNonSdReleaseEvidence", evidence()}};
}
static json baseRow(bool accepted = true) {
    return {{"command", "RELEASE_TARGET"}, {"phase", accepted ? "accepted" : "rejected"}, {"ok", accepted},
            {"status", accepted ? "expected-ok" : "release-local-dispose-failed"},
            {"targetPnpAdmitted", false}, {"hostConsumed", true}, {"sequenceSynced", true}, {"sequence", 13}};
}
static json withoutLogFields(json row) {
    if (row.is_object()) {
        for (const char* key : {"msiNonSdReleaseEvidenceVersion", "msiNonSdReleaseEvidencePresent",
                               "msiNonSdReleaseEvidenceTupleAccepted", "legacyTargetPnpFieldsAreFreshReleaseProof",
                               "msiNonSdReleaseEvidence"}) row.erase(key);
    }
    return row;
}
struct Case {
    std::string name, category;
    json checks = json::array(), detail = json::object();
    int failures = 0;
    void check(const char* label, bool passed) {
        checks.push_back({{"check", label}, {"passed", passed}});
        if (!passed) ++failures;
    }
};
static json g_cases = json::array();
static std::uint64_t g_checks = 0, g_failures = 0;
template<class Test> static void run(const std::string& name, const char* category, Test&& test) {
    g_inputHostMsiStageRequested = true; g_fakeBoardAuthority = true;
    g_fakeAuthorityThrows = false; g_fakeSinkThrows = false; g_fakePersona = "xbox360";
    const json beforeCalls = g_calls.asJson();
    Case c{name, category};
    try { test(c); }
    catch (const std::exception& ex) { allocationFault::enabled = false; c.check("unexpected exception", false); c.detail["exception"] = ex.what(); }
    catch (...) { allocationFault::enabled = false; c.check("unexpected non-standard exception", false); }
    json calls = g_calls.asJson();
    for (auto it = calls.begin(); it != calls.end(); ++it) it.value() = it.value().get<std::uint64_t>() - beforeCalls.at(it.key()).get<std::uint64_t>();
    g_checks += c.checks.size(); g_failures += static_cast<std::uint64_t>(c.failures);
    g_cases.push_back({{"name", name}, {"category", category}, {"passed", c.failures == 0}, {"checks", c.checks}, {"apiCalls", calls}, {"evidence", c.detail}});
}
static void gateTests() {
    const std::vector<std::string> personas = {"xbox360", "elite", "dualshock4", "dualsense", "dualsense-edge", "steamdeck", "steam-deck", "XBOX360"};
    for (const bool requested : {false, true}) for (const bool authority : {false, true})
        for (const char* command : {"HELLO", "RELEASE_TARGET", "SHUTDOWN"}) for (const auto& persona : personas) {
            run("gate-" + std::to_string(requested) + "-" + std::to_string(authority) + "-" + command + "-" + persona, "gate", [&](Case& c) {
                g_inputHostMsiStageRequested = requested; g_fakeBoardAuthority = authority;
                const auto response = responseFixture(persona, command), original = response;
                auto row = baseRow(); const auto rowBefore = row;
                const auto authorityBefore = g_calls.authority;
                invoke(row, response, command);
                const bool eligible = requested && authority;
                const bool hello = std::strcmp(command, "HELLO") == 0;
                const bool allowedPersona = persona == "xbox360" || persona == "elite" || persona == "dualshock4" || persona == "dualsense" || persona == "dualsense-edge";
                c.check("cached board authority obeys flag short circuit", g_calls.authority - authorityBefore == (requested ? 1u : 0u));
                c.check("response untouched", response == original);
                c.check("existing row control fields untouched", withoutLogFields(row) == rowBefore);
                c.check("HELLO version obeys MSI/opt-in gate", row.contains("msiNonSdReleaseEvidenceVersion") == (eligible && hello));
                c.check("release evidence excludes non-MSI, flag-off, SD and unknown personas", row.contains("msiNonSdReleaseEvidence") == (eligible && !hello && allowedPersona));
                if (!eligible || (!hello && !allowedPersona)) c.check("excluded row exactly unchanged", row == rowBefore);
                if (eligible && hello) c.check("HELLO copies capability version only", row.size() == rowBefore.size() + 1 && row.at("msiNonSdReleaseEvidenceVersion") == 1);
            });
        }
    const std::vector<const char*> excluded = {nullptr, "", "PREPARE_TARGET", "RECREATE_TARGET", "SUBMIT_FRAME", "release_target", "HIDHIDE_QUERY"};
    for (const char* command : excluded) run(std::string("ignored-command-") + (command ? command : "null"), "gate", [&](Case& c) {
        auto row = baseRow(); const auto before = row; const auto response = responseFixture();
        invoke(row, response, command);
        c.check("null/unsupported command creates no log fields", row == before);
    });
}
static std::vector<json> payloads() {
    auto forged = evidence();
    forged["exactOwnedIdsPinned"] = true; forged["classificationComplete"] = true; forged["exactOwnedAbsenceProven"] = true;
    forged["ok"] = true; forged["status"] = "expected-ok"; forged["targetPnpAdmitted"] = true;
    return {nullptr, "untrusted diagnostic string", json::array({1, false}), evidence(), forged};
}
static void receiptTests() {
    const auto values = payloads();
    for (const bool accepted : {false, true}) for (const bool tuple : {false, true})
        for (const char* command : {"RELEASE_TARGET", "SHUTDOWN"}) for (std::size_t i = 0; i <= values.size(); ++i) {
            run("receipt-" + std::to_string(accepted) + "-" + std::to_string(tuple) + "-" + command + "-" + std::to_string(i), "receipt", [&](Case& c) {
                auto response = responseFixture("xbox360", command);
                if (!accepted) { response["ok"] = false; response["status"] = "release-local-dispose-failed"; }
                if (i == values.size()) response.erase("msiNonSdReleaseEvidence"); else response["msiNonSdReleaseEvidence"] = values[i];
                const auto original = response; const auto control = actualReceiptControl(response, command, "expected-ok");
                auto row = baseRow(accepted); const auto rowBefore = row;
                invoke(row, response, command, tuple);
                c.check("diagnostic presence records exact field presence including null", row.at("msiNonSdReleaseEvidencePresent") == (i != values.size()));
                c.check("tuple argument recorded independently of business result", row.at("msiNonSdReleaseEvidenceTupleAccepted") == tuple);
                c.check("legacy target PnP fields never promoted to fresh proof", row.at("legacyTargetPnpFieldsAreFreshReleaseProof") == false);
                c.check("evidence copied verbatim without control grant", i == values.size() ? !row.contains("msiNonSdReleaseEvidence") : row.at("msiNonSdReleaseEvidence") == values[i]);
                c.check("source response unchanged", response == original);
                c.check("accepted/rejected row control fields unchanged", withoutLogFields(row) == rowBefore);
                c.check("actual control predicates unchanged by logging", actualReceiptControl(response, command, "expected-ok") == control);
                c.check("business success/rejection remains as before logging", control.businessAccepted == accepted);
                c.detail["directFalseTupleScope"] = "Only direct helper contract is exercised for tuple=false; actual callers return before logger on tuple failure.";
            });
        }
}
static void controlTests() {
    const std::vector<std::pair<std::string, json>> mismatches = {
        {"protocolVersion", 0}, {"command", "SHUTDOWN"}, {"requestId", "wrong"}, {"sequence", 14},
        {"runId", "wrong"}, {"epoch", 8}, {"powerGeneration", 12}, {"targetId", "wrong"}, {"ownerIdentity", "wrong"},
        {"persona", "elite"}, {"profileIdentity", "wrong"}, {"startupNonce", "wrong"}, {"configRevision", 4},
        {"configHash", "wrong"}, {"assetVerified", false}, {"lockedCoreSha256", "wrong"}, {"lockedCoreMvid", "wrong"},
        {"hostInstanceId", "wrong"}, {"hostInstanceId", ""}};
    for (std::size_t i = 0; i < mismatches.size(); ++i) for (const bool forged : {false, true}) {
        run("tuple-reject-" + std::to_string(i) + "-" + std::to_string(forged), "control", [&](Case& c) {
            auto response = responseFixture(); response[mismatches[i].first] = mismatches[i].second;
            if (forged) response["msiNonSdReleaseEvidence"] = payloads().back(); else response.erase("msiNonSdReleaseEvidence");
            const auto decision = actualReceiptControl(response, "RELEASE_TARGET", "expected-ok");
            auto row = baseRow(); const auto before = row; const auto loggerBefore = g_calls.logger;
            // Mirror only the statically verified call gate; the real request is never run.
            if (decision.tupleAccepted) invoke(row, response, "RELEASE_TARGET");
            c.check("actual tuple/identity predicate rejects mismatched receipt", !decision.tupleAccepted && !decision.businessAccepted);
            c.check("tuple failure cannot reach logger", g_calls.logger == loggerBefore && row == before);
            c.detail["mismatchedField"] = mismatches[i].first;
        });
    }
    for (const char* command : {"RELEASE_TARGET", "SHUTDOWN", "PREPARE_TARGET", "RECREATE_TARGET"})
        for (int rejection = 0; rejection < 4; ++rejection) run(std::string("business-") + command + "-" + std::to_string(rejection), "control", [&](Case& c) {
            auto response = responseFixture("xbox360", command);
            if (rejection == 1) response["ok"] = false;
            if (rejection == 2) response["status"] = "wrong-status";
            if (rejection == 3) response["targetPnpAdmitted"] = false;
            const auto expected = actualReceiptControl(response, command, "expected-ok");
            response["msiNonSdReleaseEvidence"] = payloads().back();
            auto row = baseRow(expected.businessAccepted); const auto before = row;
            invoke(row, response, command);
            c.check("forged diagnostic proof cannot change actual control predicates", actualReceiptControl(response, command, "expected-ok") == expected);
            c.check("tuple-valid receipt remains tuple-valid", expected.tupleAccepted);
            const bool requiresPnp = std::strcmp(command, "PREPARE_TARGET") == 0 || std::strcmp(command, "RECREATE_TARGET") == 0;
            c.check("actual status/ok/PnP acceptance obeys pre-existing command rules", expected.businessAccepted == (rejection == 0 || (rejection == 3 && !requiresPnp)));
            c.check("logging cannot change row status/ok/sequence/PnP fields", withoutLogFields(row) == before);
        });
}
static void exceptionTests() {
    for (const char* command : {"HELLO", "RELEASE_TARGET", "SHUTDOWN"}) run(std::string("authority-throws-") + command, "exception", [&](Case& c) {
        auto row = baseRow(); const auto before = row; const auto response = responseFixture();
        g_fakeAuthorityThrows = true;
        invoke(row, response, command);
        c.check("authority hook exception swallowed inside actual noexcept helper", row == before);
    });
    run("flag-off-short-circuits-throwing-authority", "exception", [&](Case& c) {
        g_inputHostMsiStageRequested = false; g_fakeAuthorityThrows = true;
        auto row = baseRow(); const auto before = row; const auto calls = g_calls.authority;
        invoke(row, responseFixture(), "RELEASE_TARGET");
        c.check("flag-off never calls throwing authority", g_calls.authority == calls && row == before);
    });
    const std::vector<json> malformed = {nullptr, true, 123, "wrong-type", json::array({1}), json::object()};
    for (std::size_t i = 0; i < malformed.size(); ++i) {
        run("malformed-persona-" + std::to_string(i), "exception", [&](Case& c) {
            auto response = responseFixture(); response["persona"] = malformed[i]; const auto original = response;
            auto row = baseRow(); const auto before = row;
            invoke(row, response, "RELEASE_TARGET");
            c.check("malformed persona cannot escape or alter row", row == before);
            c.check("malformed response untouched", response == original);
        });
        run("malformed-version-" + std::to_string(i), "exception", [&](Case& c) {
            auto response = responseFixture(); response["msiNonSdReleaseEvidenceVersion"] = malformed[i];
            auto row = baseRow(); const auto before = row;
            invoke(row, response, "HELLO");
            if (malformed[i].is_number_integer() || malformed[i].is_boolean()) c.check("actual value<int> capability conversion recorded", row.at("msiNonSdReleaseEvidenceVersion").get<int>() == malformed[i].get<int>());
            else c.check("non-numeric capability version exception swallowed", row == before);
        });
        if (!malformed[i].is_object()) run("malformed-response-root-" + std::to_string(i), "exception", [&](Case& c) {
            auto row = baseRow(); const auto before = row; const auto original = malformed[i];
            invoke(row, malformed[i], "RELEASE_TARGET");
            c.check("invalid response root isolated", row == before && malformed[i] == original);
        });
        if (!malformed[i].is_null() && !malformed[i].is_object()) run("malformed-row-root-" + std::to_string(i), "exception", [&](Case& c) {
            auto row = malformed[i]; const auto before = row;
            invoke(row, responseFixture(), "RELEASE_TARGET");
            c.check("invalid logging row isolated", row == before);
        });
    }
    run("hello-version-missing", "receipt", [&](Case& c) {
        auto response = responseFixture(); response.erase("msiNonSdReleaseEvidenceVersion"); auto row = baseRow();
        invoke(row, response, "HELLO");
        c.check("absent capability version records default zero", row.at("msiNonSdReleaseEvidenceVersion") == 0);
        c.check("HELLO never copies release proof", !row.contains("msiNonSdReleaseEvidence"));
    });
    run("null-row-becomes-object", "receipt", [&](Case& c) {
        json row; invoke(row, responseFixture(), "SHUTDOWN");
        c.check("valid null row may be initialized as logging object", row.is_object() && row.at("msiNonSdReleaseEvidence") == evidence());
    });
}
static void allocationTests() {
    auto response = responseFixture();
    response["msiNonSdReleaseEvidence"]["longDetail"] = std::string(1024, 'x');
    std::uint64_t count = 0;
    run("logger-allocation-baseline", "allocation", [&](Case& c) {
        auto row = baseRow(); const auto before = row;
        { AllocationScope allocation; invoke(row, response, "RELEASE_TARGET"); }
        count = allocationFault::attempts;
        c.check("actual JSON path exercised bounded real allocations", count > 0 && count <= 512);
        c.check("baseline copied complete evidence", row.at("msiNonSdReleaseEvidence") == response.at("msiNonSdReleaseEvidence"));
        c.check("baseline did not modify control fields", withoutLogFields(row) == before);
        c.detail["loggerAllocationCount"] = count;
    });
    const auto bounded = count < 512 ? count : 512;
    for (std::uint64_t i = 0; i < bounded; ++i) run("logger-allocation-fails-" + std::to_string(i), "allocation", [&](Case& c) {
        auto row = baseRow(); const auto before = row; const auto original = response;
        const auto decision = actualReceiptControl(response, "RELEASE_TARGET", "expected-ok");
        { AllocationScope allocation(static_cast<std::int64_t>(i)); invoke(row, response, "RELEASE_TARGET"); }
        c.check("requested allocation failure actually occurred", allocationFault::failures == 1);
        c.check("helper survived bad_alloc with original response intact", response == original);
        c.check("partial optional log retains original control fields", withoutLogFields(row) == before);
        c.check("bad_alloc never changes actual acceptance predicates", actualReceiptControl(response, "RELEASE_TARGET", "expected-ok") == decision);
        c.detail["failAtAllocation"] = i;
        c.detail["optionalFieldsAfterFailure"] = row.size() - before.size();
    });
}
static void handoffTests() {
    for (const bool accepted : {false, true}) for (const bool requested : {false, true}) {
        for (const bool bodyThrows : {false, true}) run("wrapper-body-" + std::to_string(accepted) + "-" + std::to_string(requested) + "-" + std::to_string(bodyThrows), "handoff", [&](Case& c) {
            g_inputHostMsiStageRequested = requested;
            auto response = responseFixture(); response["ok"] = accepted;
            auto row = baseRow(accepted); invoke(row, response, "RELEASE_TARGET"); const auto before = row;
            const auto control = actualReceiptControl(response, "RELEASE_TARGET", "expected-ok");
            const auto entered = g_calls.sinkBody, raised = g_calls.sinkExceptionsRaised, authority = g_calls.authority;
            // The wrapper trusts the existing launch flag and must not query identity again.
            g_fakeAuthorityThrows = true; g_fakeSinkThrows = bodyThrows;
            bool escaped = false;
            try {
                if (accepted) actualacceptedLineHandoff(row); else actualrejectedLineHandoff(row);
            } catch (const std::runtime_error&) { escaped = true; }
            c.check("fake sink body entered once", g_calls.sinkBody == entered + 1);
            c.check("configured fake sink body exception actually raised", g_calls.sinkExceptionsRaised == raised + (bodyThrows ? 1u : 0u));
            c.check("opt-in contains body exceptions; flag-off preserves direct-call propagation", escaped == (bodyThrows && !requested));
            c.check("wrapper reuses launch authorization without new authority query", g_calls.authority == authority);
            c.check("source receipt and logging row unchanged by wrapper", row == before && response.at("ok") == accepted);
            c.check("actual control predicates unchanged by wrapper", actualReceiptControl(response, "RELEASE_TARGET", "expected-ok") == control);
            c.detail["optIn"] = requested; c.detail["bodyExceptionEscaped"] = escaped;
            c.detail["scope"] = "Actual wrapper and call statement; fake sink body may throw to exercise wrapper catch. Production sink body and complete Native handler are not executed.";
        });
        run("wrapper-argument-allocation-failure-" + std::to_string(accepted) + "-" + std::to_string(requested), "handoff", [&](Case& c) {
            g_inputHostMsiStageRequested = requested;
            auto response = responseFixture(); response["ok"] = accepted;
            auto row = baseRow(accepted); invoke(row, response, "RELEASE_TARGET"); const auto before = row;
            const auto control = actualReceiptControl(response, "RELEASE_TARGET", "expected-ok");
            const auto entered = g_calls.sinkBody, authority = g_calls.authority;
            g_fakeAuthorityThrows = true;
            bool escaped = false;
            try {
                AllocationScope allocation(0);
                if (accepted) actualacceptedLineHandoff(row); else actualrejectedLineHandoff(row);
            } catch (const std::bad_alloc&) { escaped = true; }
            c.check("parameter-copy allocation failure actually injected", allocationFault::failures == 1);
            c.check("copy fails before fake sink body entry", g_calls.sinkBody == entered);
            c.check("opt-in contains copy exception; flag-off preserves original propagation", escaped == !requested);
            c.check("wrapper reuses launch authorization without new authority query", g_calls.authority == authority);
            c.check("row and receipt intact after parameter-copy failure", row == before && response.at("ok") == accepted);
            c.check("actual control predicates unchanged after copy failure", actualReceiptControl(response, "RELEASE_TARGET", "expected-ok") == control);
            c.detail["optIn"] = requested; c.detail["argumentExceptionEscaped"] = escaped;
            c.detail["fakeSinkBodyEntered"] = g_calls.sinkBody != entered;
            c.detail["scope"] = "Verbatim actual wrapper, production sink signature and actual handoff statement; only sink body is fake. No complete Native handler execution.";
        });
    }
}
int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: msi-non-sd-release-native-selftest report.json\n"; return 2; }
    try {
        gateTests(); receiptTests(); controlTests(); exceptionTests(); allocationTests(); handoffTests();
        json categories = json::object();
        for (const auto& c : g_cases) {
            const auto category = c.at("category").get<std::string>();
            if (!categories.contains(category)) categories[category] = {{"caseCount", 0}, {"failedCases", 0}};
            categories[category]["caseCount"] = categories[category]["caseCount"].get<int>() + 1;
            if (!c.at("passed").get<bool>()) categories[category]["failedCases"] = categories[category]["failedCases"].get<int>() + 1;
        }
        const json report = {{"testKind", "verbatim-extracted-native-helper-all-fake"}, {"caseCount", g_cases.size()},
            {"checkCount", g_checks}, {"failureCount", g_failures}, {"passed", g_failures == 0},
            {"mainSha256", kMainSha256}, {"helperSha256", kHelperSha256}, {"wrapperSha256", kWrapperSha256}, {"apiCallTotals", g_calls.asJson()},
            {"categoryTotals", categories}, {"cases", g_cases},
            {"productExecuted", false}, {"wholeNativeTuCompiled", false}, {"hardwareSdkCalls", 0}, {"deviceApiCalls", 0}, {"registryApiCalls", 0}, {"serviceApiCalls", 0},
            {"scope", "Actual extracted helper and wrapper with actual control declarations in a fake evaluator. Fake cached identity, receipt inputs and sink body; no complete Native handler or production sink execution; only report-file IO."}};
        std::ofstream output(argv[1], std::ios::binary | std::ios::trunc);
        if (!output) { std::cerr << "cannot open report\n"; return 2; }
        output << report.dump(2) << '\n'; output.close();
        if (!output) { std::cerr << "cannot write report\n"; return 2; }
        std::cout << "MSI_NON_SD_RELEASE_NATIVE_SELFTEST cases=" << g_cases.size() << " checks=" << g_checks << " failures=" << g_failures << '\n';
        std::cout << "API_CALL_TOTALS " << g_calls.asJson().dump() << '\n';
        return g_failures == 0 ? 0 : 1;
    } catch (const std::exception& ex) { std::cerr << "fatal selftest error: " << ex.what() << '\n'; return 2; }
}