// ORDINARY_BEGIN
            const json fpsJson=frameOutput.packet(nowMs);
            monitorFpsText = fpsJson.is_null() ? std::string{} : fpsJson.dump();
// ORDINARY_END
// FAST_BEGIN
                const json framePacket=frameOutput.packet(frameNow);
                monitorSnapshotPublishFrames(iterGeneration,static_cast<long long>(frameNow),
                                             framePacket.is_null()?std::string{}:framePacket.dump());
// FAST_END
// PUBLISH_BEGIN
static void monitorSnapshotPublishFrames(unsigned long long expectedGeneration,
                                         long long collectedAt, std::string fpsText) {
    const bool accepted = ymcc::monitorsnapshot::retain(fpsText, ymcc::monitorsnapshot::fpsMaxBytes);
    monitorTestBarrierPause(1);
    std::lock_guard<std::mutex> lock(g_monitorSnapshotMx);
    if (g_monitorSnapshot.generation != expectedGeneration || !g_monitorSnapshot.valid) return;
    unsigned drops = accepted ? 0 : 1;
    // Enforce the combined budget on the actual retained cache, not only on
    // the later IPC response. Replacing FPS never copies the hardware string.
    if (!ymcc::monitorsnapshot::textFits(g_monitorSnapshot.topText, fpsText)) {
        ymcc::monitorsnapshot::discard(g_monitorSnapshot.topText); ++drops;
    }
    if (drops) g_monitorSnapshotOversizeDrops.fetch_add(drops, std::memory_order_relaxed);
    g_monitorSnapshot.seq += 1;
    g_monitorSnapshot.collectedAt = collectedAt;
    g_monitorSnapshot.heartbeatAt = collectedAt;
    g_monitorSnapshot.fpsText = std::move(fpsText);
}
// PUBLISH_END
