// Runs exact failure-log block with in-memory facts and log sink only.
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <cstring>
#include "json.hpp"
#include "msi_input_host_transport_evidence.h"
using json = nlohmann::json;
enum class YmccFamily { MsiClaw, AsusRogAlly, Gpd, Unknown };
struct Machine { YmccFamily family = YmccFamily::MsiClaw; } g_machineIdentity;
static std::string g_inputHostRunId = "run-fixture", g_inputHostHostInstanceId = "host-fixture";
static std::uint64_t g_inputHostEpoch = 3;
static json events = json::array();
static bool sinkThrows = false;
static void appendNativeLifecycleLog(const char* event, const json& data) {
    if (sinkThrows) throw std::runtime_error("synthetic-sink-failure");
    events.push_back({{"event", event}, {"data", data}});
}
static void exactFaultBlock(bool pipe, bool connected, bool written, bool read,
                           bool hostAlive, std::uint32_t lastError = 258,
                           std::uint32_t responseBytes = 0,
                           std::uint64_t writeElapsedUs = 8,
                           std::uint64_t readElapsedUs = 1000000) {
    const char* command = "SUBMIT_FRAME";
    const std::string requestIdValue = "request-fixture";
    const std::uint64_t sequenceValue = 150824;
    const std::uint32_t timeoutMs = 1000;
    /* PRODUCTION_MSI_TRANSPORT_FAULT_BLOCK */
}
/* PRODUCTION_BASE_EVENT_CLASSIFIER */
static unsigned assertions = 0, cases = 0;
static void check(bool pass, const char* message) {
    ++assertions;
    if (!pass) throw std::runtime_error(message);
}
static void reset(YmccFamily family = YmccFamily::MsiClaw) {
    ++cases; events = json::array(); sinkThrows = false; g_machineIdentity.family = family;
}
static json row() { check(events.size() == 1, "Exactly one bounded MSI failure event"); return events.at(0).at("data"); }
static void identity(const json& r) {
    check(r.at("runId") == "run-fixture" && r.at("epoch") == 3 && r.at("sequence") == 150824, "Exact request tuple");
    check(r.at("requestId") == "request-fixture" && r.at("hostInstanceId") == "host-fixture", "Request and authenticated Host identity");
    check(r.at("evidenceScope") == "native-pipe-boundary-only" && r.at("hostExecutionStage") == "unobserved" && r.at("steamConsumption") == "unobserved", "No invented backend or Steam proof");
    check(!r.contains("payload") && !r.contains("buttons") && !r.contains("mode"), "No input payload or mode action");
}
int main() {
    try {
        reset(); exactFaultBlock(false, false, false, false, true, 0); auto r=row(); identity(r);
        check(r.at("failureBoundary") == "pipe-unavailable", "Missing pipe distinguished");
        check(r.at("errorKnown") == false && r.at("responseReadAttempted") == false, "Unset error not invented");
        reset(); exactFaultBlock(true, false, false, false, true); r=row(); identity(r);
        check(r.at("failureBoundary") == "client-connect" && r.at("requestWritten") == false, "Connect timeout");
        check(r.at("responseReadAttempted") == false, "No response read on failed connection");
        reset(); exactFaultBlock(true, true, false, false, true); r=row(); identity(r);
        check(r.at("failureBoundary") == "request-write" && r.at("responseReadAttempted") == false, "Write failure not backend rejection");
        reset(); exactFaultBlock(true, true, true, false, true); r=row(); identity(r);
        check(r.at("failureBoundary") == "response-read", "Response failure boundary");
        check(r.at("hostAlive") == true && r.at("error") == 258 && r.at("timeoutMsPerOperation") == 1000, "Preserved error, per-operation timeout and liveness");
        check(r.at("writeElapsedUs") == 8 && r.at("readElapsedUs") == 1000000, "Already measured elapsed facts retained");
        reset(); exactFaultBlock(true, true, true, false, false, 109, 23, 7, 55); r=row(); identity(r);
        check(r.at("hostAlive") == false && r.at("responseBytes") == 23 && r.at("error") == 109, "Broken pipe and partial byte fact retained");
        reset(); exactFaultBlock(false, true, true, false, true); r=row();
        check(r.at("failureBoundary") == "inconsistent-transport-facts", "Impossible fact tuple never promoted to proof");
        reset(); exactFaultBlock(true, true, true, true, true, 0); check(events.empty(), "No success-path event");
        for (auto family : {YmccFamily::AsusRogAlly, YmccFamily::Gpd, YmccFamily::Unknown}) {
            reset(family); exactFaultBlock(true, true, true, false, true); check(events.empty(), "No other-family diagnostic path");
        }
        reset(); check(nativeLifecycleEventIsBase("input-host-msi-transport-boundary"), "Failure evidence survives default logging gate");
        check(!nativeLifecycleEventIsBase("synthetic-unregistered-event"), "No widening default logging policy");
        reset(); sinkThrows = true; exactFaultBlock(true, true, true, false, true);
        check(events.empty(), "Failure evidence exception cannot escape transport path");
        check(g_inputHostRunId == "run-fixture" && g_inputHostEpoch == 3 && g_inputHostHostInstanceId == "host-fixture", "No session mutation");
        std::cout << json{{"passed",true},{"cases",cases},{"assertions",assertions},
            {"exactRuntimeBlock",true},{"realDeviceCalls",0},{"scope","MSI fault log block plus pure classifier; not whole pipe I/O or Host execution"}}.dump() << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
