static MonitorDetailSnapshotCapture monitorSnapshotRecordForDetail(long long nowMs,unsigned pid,json counters){
    bool liveTop=false,liveFps=false;
    {
        std::lock_guard<std::mutex> lock(g_nativeMonitorMx);
        liveTop=g_nativeMonitorTop;liveFps=g_nativeMonitorFps;
    }
    std::lock_guard<std::mutex> lock(g_monitorSnapshotMx);
    ymcc::monitordetail::SnapshotView view;
    view.available=g_monitorSnapshot.valid;view.generation=g_monitorSnapshot.generation;
    view.seq=g_monitorSnapshot.seq;view.session=nativeMonitorSessionId();
    view.relayEpoch=g_monitorRelayEpoch.load(std::memory_order_acquire);
    view.topActive=liveTop;view.fpsActive=liveFps;
    view.hwDown=view.available?g_monitorSnapshot.hwDown:true;
    if(view.available){
        view.collectedAt=g_monitorSnapshot.collectedAt;view.heartbeatAt=g_monitorSnapshot.heartbeatAt;
        view.hwinfoOkAt=g_monitorSnapshot.hwinfoOkAt;
        const bool topFits=ymcc::monitorsnapshot::topFits(g_monitorSnapshot.topText);
        const bool fpsFits=ymcc::monitorsnapshot::fpsFits(g_monitorSnapshot.fpsText);
        const bool combinedFits=ymcc::monitorsnapshot::textFits(
            topFits?std::string_view(g_monitorSnapshot.topText):std::string_view{},
            fpsFits?std::string_view(g_monitorSnapshot.fpsText):std::string_view{});
        if(topFits&&combinedFits&&!g_monitorSnapshot.topText.empty())view.top=g_monitorSnapshot.topText;
        if(fpsFits&&!g_monitorSnapshot.fpsText.empty())view.fps=g_monitorSnapshot.fpsText;
    }
    ++g_monitorSnapshotServes;
    const bool same=view.available&&view.collectedAt==nowMs;
    return {ymcc::monitordetail::snapshotRecord(view,nowMs,pid,std::move(counters)),same};
}