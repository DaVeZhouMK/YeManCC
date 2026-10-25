// FAN977: exact production thread/arm/disarm/ensure extracts, real Win32 event,
// fake tick/log/power gates. No Host, HC, hardware, sleep, CPU measurements.
int main() {
    std::vector<std::string> failures; int checks = 0;
    const auto expect = [&](bool pass, const std::string& name) { ++checks; if (!pass) failures.push_back(name); };
    const auto waitFor = [&](std::size_t count) {
        std::unique_lock<std::mutex> lock(scheduleMx);
        return scheduleCv.wait_for(lock, std::chrono::milliseconds(350), [&] { return schedules.size() >= count; });
    };
    const auto timeoutAt = [&](std::size_t index) { std::lock_guard<std::mutex> lock(scheduleMx); return schedules.size() > index ? schedules[index] : 1UL; };
    fanLeaseKeepaliveEnsureThread();
    expect(waitFor(1) && timeoutAt(0) == INFINITE, "unarmed worker parks without a periodic deadline");
    fanLeaseKeepaliveArm("fixture-lease-0001", "fixture-arm");
    expect(waitFor(2) && timeoutAt(1) == 5000, "arm wakes a parked owner and retains active 5s interval");
    {
        std::lock_guard<std::mutex> lock(scheduleMx);
        expect(!g_fanLeaseKeepaliveStop && schedules.size() <= 2, "one arm notification does not leave the event permanently signalled");
    }
    fanLeaseKeepaliveDisarm("fixture-off");
    expect(waitFor(3) && timeoutAt(2) == INFINITE, "OFF immediately moves the same worker back to an event-only wait");
    fanLeaseKeepaliveArm("fixture-new-0002", "fixture-rearm");
    expect(waitFor(4) && timeoutAt(3) == 5000, "next explicit control remains reachable after idle wait");
    fanLeaseKeepaliveDisarm("fixture-idle-exit");
    expect(waitFor(5) && timeoutAt(4) == INFINITE, "idle before exit is still event-only");
    fanLeaseKeepaliveShutdown();
    expect(g_fanLeaseKeepaliveStop && !g_fanLeaseKeepaliveThread.joinable() && g_fanLeaseKeepaliveWake == nullptr,
        "actual shutdown notification joins the parked worker and closes its event");
    // Real event semantics, including the arm-before-wait edge, without running any control code.
    HANDLE notification = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    expect(notification != nullptr && SetEvent(notification) && WaitForSingleObject(notification, 0) == WAIT_OBJECT_0,
        "an auto-reset notification sent before wait is retained");
    expect(WaitForSingleObject(notification, 0) == WAIT_TIMEOUT, "a notification is consumed once without polling spin");
    CloseHandle(notification);
    std::cout << json{{"ok", failures.empty()}, {"checks", checks}, {"failed", failures.size()}, {"failures", failures},
        {"scope", "exact-production-event-worker-real-win32-events-fake-control-tick-no-device-no-cpu-benchmark"}}.dump(2) << '\n';
    return failures.empty() ? 0 : 1;
}
