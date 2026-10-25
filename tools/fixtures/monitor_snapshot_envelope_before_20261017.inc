static json monitorSnapshotServeFn() {
    // Keep the same lock order as nativeMonitorSetMode (native -> snapshot).
    // The diagnostic lane uses this order too, so a mode change cannot wait on
    // a reader that is waiting on the mode mutex.
    bool liveTop = false, liveFps = false;
    {
        std::lock_guard<std::mutex> lock(g_nativeMonitorMx);
        liveTop = g_nativeMonitorTop;
        liveFps = g_nativeMonitorFps;
    }
    MonitorSnapshotState copy;
    {
        std::lock_guard<std::mutex> lock(g_monitorSnapshotMx);
        copy = g_monitorSnapshot;
    }

    json out{{"schema", 1}, {"available", copy.valid},
             {"generation", copy.generation}, {"seq", copy.seq},
             {"session", nativeMonitorSessionId()},
             {"relayEpoch", g_monitorRelayEpoch.load(std::memory_order_acquire)},
             {"topActive", liveTop}, {"fpsActive", liveFps}};
    if (!copy.valid) {
        out["hwDown"] = true;
        g_monitorSnapshotServes.fetch_add(1, std::memory_order_relaxed);
        return out;   // 明确"不可用"，不返回陈旧值
    }
    out["collectedAt"] = copy.collectedAt;
    out["heartbeatAt"] = copy.heartbeatAt;
    out["hwinfoOkAt"] = copy.hwinfoOkAt;
    out["hwDown"] = copy.hwDown;
    const bool topFits = ymcc::monitorsnapshot::topFits(copy.topText);
    const bool fpsFits = ymcc::monitorsnapshot::fpsFits(copy.fpsText);
    const bool combinedFits = ymcc::monitorsnapshot::textFits(
        topFits ? std::string_view(copy.topText) : std::string_view{},
        fpsFits ? std::string_view(copy.fpsText) : std::string_view{});
    if (!topFits || !fpsFits || !combinedFits) {
        // The producer normally makes this branch unreachable. Keep the IPC
        // boundary defensive without counting every consumer read as a new
        // producer drop.
        out["snapshotOversize"] = true;
    }
    if (topFits && combinedFits && !copy.topText.empty()) out["top"] = std::move(copy.topText);
    // If top plus fps exceeds the combined cap, omit only the lower-rate top
    // string and keep the bounded high-frequency FPS payload available.
    if (fpsFits && !copy.fpsText.empty()) out["fps"] = std::move(copy.fpsText);
    g_monitorSnapshotServes.fetch_add(1, std::memory_order_relaxed);
    return out;
}

