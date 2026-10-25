// FAN976: linked after exact production Fan observer/Tick extracts and fake OS/HTTP/storage boundaries.
// No native app, Host, filesystem state, HC, WMI, EC, or device is executed.
int checks = 0;
std::vector<std::string> failures;
void expect(bool value, const std::string& name) { ++checks; if (!value) failures.push_back(name); }
void resetFixture() {
    g_fanActivity = {}; persisted = armedCalls = disarmedCalls = authRejected = 0;
    g_fanLeaseId.clear(); g_fanLeaseKeepaliveArmed = false; g_fanLeaseKeepaliveLastRenewTick = 0;
    g_fanLeaseKeepaliveInFlight = false; g_fanKeepaliveRendererEpochSnapshot = 1;
    g_fanKeepaliveRendererEpoch = 1; g_fanKeepaliveRendererGraceUntil = 0;
    heartbeatEdge = {}; heartbeatOk = true; heartbeatStatus = 200; heartbeatCode.clear(); heartbeatRenewed = "fixture-lease-0001";
}
void observe(const std::string& path, const json& response, bool header = true, DWORD status = 200, bool suppliedTree = true, ULONGLONG receiptEpoch = 0) {
    const auto request = json{{"leaseId", "fixture-lease-0001"}, {"nodes", json::array({json{{"tempC", 0}, {"dutyPercent", 100}}})}}.dump();
    fanLeaseKeepaliveObserveHttp("POST", L"127.0.0.1", 8765, std::wstring(path.begin(), path.end()), request,
        status, response.dump(), header, "fixture-not-a-production-token", "fixture-request", suppliedTree ? &response : nullptr, receiptEpoch ? receiptEpoch : g_monitorFanDisplay.epoch());
}
int main() {
    const json lease{{"leaseId", "fixture-lease-0001"}, {"generation", 1}, {"expiresAtMonoMs", 15000}, {"owner", "fixture"}};
    const json state{{"state", "Ready"}, {"hardwareWritesEnabled", true}, {"lease", lease}, {"factoryType", "HandheldCompanion.Devices.MSI.ClawCG3EM"}};
    const json success{{"ok", true}, {"state", state}};
    for (bool suppliedTree : {false, true}) {
        for (const std::string path : {"/api/enable", "/api/preset"}) {
            resetFixture(); observe(path, success, true, 200, suppliedTree);
            expect(g_fanActivity.active && persisted == 1 && g_fanLeaseId == "fixture-lease-0001", path + " confirmed receipt records and arms");
            std::vector<json> bad = {json::object(), json{{"ok", true}}, json{{"ok", false}, {"state", state}}, json{{"ok", "true"}, {"state", state}},
                json::array(), nullptr, json{{"ok", true}, {"state", "Ready"}}, json{{"ok", true}, {"state", json{{"state", ""}}}},
                json{{"ok", true}, {"state", json{{"state", "Ready"}, {"hardwareWritesEnabled", "true"}}}}};
            for (const auto& reply : bad) {
                resetFixture(); observe(path, reply, true, 200, suppliedTree);
                expect(!g_fanActivity.active && persisted == 0 && armedCalls == 0, path + " malformed/false receipt has no passive success side effects");
            }
            resetFixture(); observe(path, success, false, 200, suppliedTree);
            expect(!g_fanActivity.active && armedCalls == 0, path + " missing session header cannot arm/activity");
            for (DWORD status : {401UL, 403UL, 409UL}) {
                resetFixture(); observe(path, success, true, status, suppliedTree);
                expect(!g_fanActivity.active && armedCalls == 0, path + " non-success HTTP cannot arm/activity");
                if (status != 409) expect(authRejected == 1, path + " auth refusal evidence retained");
            }
        }
        for (const std::string path : {"/api/acquire-control", "/api/heartbeat"}) {
            resetFixture(); observe(path, json{{"ok", true}, {"lease", lease}}, true, 200, suppliedTree);
            expect(armedCalls == 1 && g_fanLeaseId == "fixture-lease-0001", path + " real lease receipt arms");
            resetFixture(); observe(path, json{{"ok", true}, {"result", json{{"lease", lease}}}}, true, 200, suppliedTree);
            expect(armedCalls == 1, path + " legacy result lease remains usable");
            for (const json& reply : std::vector<json>{json{{"ok", true}}, json{{"ok", false}, {"lease", lease}}, json{{"ok", true}, {"lease", nullptr}},
                json{{"ok", true}, {"lease", json{{"leaseId", 5}}}}, json{{"ok", true}, {"lease", json{{"leaseId", " spaces "}}}}}) {
                resetFixture(); observe(path, reply, true, 200, suppliedTree);
                expect(armedCalls == 0, path + " never invents receipt from request lease");
            }
        }
        resetFixture(); observe("/api/heartbeat", json{{"ok", true}, {"lease", json{{"leaseId", "fixture-new-0002"}}}}, true, 200, suppliedTree);
        expect(armedCalls == 0, "heartbeat cannot rotate lease identity");
        for (const std::string path : {"/api/disable", "/api/release-control", "/api/restore"}) {
            resetFixture(); g_fanLeaseId = "fixture-lease-0001"; g_fanLeaseKeepaliveArmed = true; g_fanActivity.active = true;
            observe(path, json{{"ok", false}, {"state", state}}, true, 200, suppliedTree);
            expect(g_fanLeaseKeepaliveArmed && g_fanActivity.active && persisted == 0, path + " failed 200 cannot revoke confirmed activity");
            observe(path, success, true, 200, suppliedTree);
            expect(!g_fanLeaseKeepaliveArmed && !g_fanActivity.active, path + " actual success still revokes");
        }
        resetFixture(); g_fanLeaseKeepaliveArmed = true; g_fanLeaseId = "fixture-lease-0001";
        observe("/api/cancel-control-intent", json{{"ok", true}, {"state", state}}, true, 200, suppliedTree);
        expect(g_fanLeaseKeepaliveArmed, "missing cancellation proof does not report OFF");
        observe("/api/cancel-control-intent", json{{"ok", true}, {"state", state}, {"controlIntentCancelled", true}}, true, 200, suppliedTree);
        expect(!g_fanLeaseKeepaliveArmed, "confirmed cancellation disarms");
        resetFixture(); g_fanLeaseKeepaliveArmed = true; g_fanLeaseId = "fixture-lease-0001";
        observe("/api/suspend", json::object(), false, 409, suppliedTree);
        expect(!g_fanLeaseKeepaliveArmed, "existing teardown intent disarm retained independently of terminal success");
    }
    resetFixture();
    const auto retiredEpoch = g_monitorFanDisplay.epoch();
    g_monitorFanDisplay.invalidate(); // A newer OFF request was dispatched before the old enable reply returned.
    observe("/api/disable", success);
    observe("/api/enable", success, true, 200, true, retiredEpoch);
    expect(!g_fanActivity.active && !g_fanLeaseKeepaliveArmed, "late valid renderer enable cannot recreate intent/renewal after OFF");
    observe("/api/heartbeat", json{{"ok", true}, {"lease", lease}}, true, 200, false, retiredEpoch);
    expect(!g_fanLeaseKeepaliveArmed, "retired renderer heartbeat cannot rearm after OFF");
    const auto stalePersisted = persisted;
    observe("/api/disable", success, true, 200, false, retiredEpoch);
    expect(persisted == stalePersisted, "retired stop reply cannot update current activity revision");
    observe("/api/enable", success);
    expect(g_fanActivity.active && g_fanLeaseKeepaliveArmed, "a current explicit enable remains reachable after discarded old receipts");
    for (const json& bad : std::vector<json>{nullptr, json::array(), json{{"ok", true}}, json{{"ok", false}, {"lease", lease}}, json{{"ok", "true"}, {"lease", lease}}}) {
        expect(!ymcc::fan_response::accepted("/api/heartbeat", 200, bad, "fixture-lease-0001"), "native heartbeat rejects unusable receipt");
    }
    for (const json& badLease : std::vector<json>{json{{"leaseId", "fixture-lease-0001"}, {"generation", "1"}},
        json{{"leaseId", "fixture-lease-0001"}, {"generation", -1}}, json{{"leaseId", "fixture-lease-0001"}, {"expiresAtMonoMs", "15000"}},
        json{{"leaseId", "fixture-lease-0001"}, {"owner", nullptr}}}) expect(!ymcc::fan_response::leaseValid(badLease), "supplied lease metadata retains wire type");
    expect(ymcc::fan_response::accepted("/api/state", 200, json{{"state", json{{"state", "FutureFamilyState"}, {"lease", nullptr}}}}), "future optional state fields remain usable");
    expect(ymcc::fan_response::accepted("/api/open", 200, state), "legacy root lifecycle state remains usable");
    resetFixture(); g_fanLeaseKeepaliveArmed = true; g_fanLeaseId = "fixture-lease-0001";
    fanLeaseKeepaliveTick();
    expect(g_fanLeaseKeepaliveArmed && g_fanLeaseId == "fixture-lease-0001" && g_fanLeaseKeepaliveLastRenewTick == 10000, "current actual heartbeat advances renew clock");
    resetFixture(); g_fanLeaseKeepaliveArmed = true; g_fanLeaseId = "fixture-lease-0001";
    heartbeatEdge = [] { g_fanLeaseId = "fixture-new-0002"; };
    fanLeaseKeepaliveTick();
    expect(g_fanLeaseId == "fixture-new-0002" && g_fanLeaseKeepaliveLastRenewTick == 0, "late successful heartbeat cannot overwrite or refresh newer lease");
    resetFixture(); g_fanLeaseKeepaliveArmed = true; g_fanLeaseId = "fixture-lease-0001";
    heartbeatEdge = [] { g_fanLeaseId.clear(); g_fanLeaseKeepaliveArmed = false; };
    fanLeaseKeepaliveTick();
    expect(!g_fanLeaseKeepaliveArmed && g_fanLeaseKeepaliveLastRenewTick == 0, "late success after OFF cannot refresh renew clock");
    resetFixture(); g_fanLeaseKeepaliveArmed = true; g_fanLeaseId = "fixture-lease-0001";
    heartbeatOk = false; heartbeatStatus = 409; heartbeatCode = "LEASE_MISMATCH";
    heartbeatEdge = [] { g_fanLeaseId = "fixture-new-0002"; };
    fanLeaseKeepaliveTick();
    expect(g_fanLeaseKeepaliveArmed && g_fanLeaseId == "fixture-new-0002", "old rejected heartbeat preserves newer lease");
    const auto epochBefore = g_monitorFanDisplay.invalidations;
    const std::string oldId = "fixture-lease-0001";
    fanLeaseKeepaliveDisarm("stale-rejection", &oldId);
    expect(g_fanLeaseKeepaliveArmed && g_fanLeaseId == "fixture-new-0002" && g_monitorFanDisplay.invalidations == epochBefore,
        "atomic expected-lease retirement cannot clear a replacement lease or its display epoch");
    std::cout << json{{"ok", failures.empty()}, {"checks", checks}, {"failed", failures.size()}, {"failures", failures},
        {"scope", "exact-production-observers-and-tick-with-fake-os-http-storage-no-device"}}.dump(2) << '\n';
    return failures.empty() ? 0 : 1;
}
