static void ipc_post_json(int childId, const json& message) {
    auto target = ipc_view_for_child(childId);
    if (!target) return;
    target->PostWebMessageAsJson(U2W(message.dump()).c_str());
}

static void ipc_emit(const std::string& ev, const json& data = {}) {
    // R3（二批需求单）：按事件名 1Hz 耗时（门控；gyro.telemetry 大包直接可见）。
    UiCostIpcScope ipcCost(ev.c_str());
    if (!g_view && g_children.empty()) return;
    json enriched = data.is_object() ? data : json::object();
    if (ev.rfind("power.", 0) == 0) {
        if (!enriched.contains("generation"))
            enriched["generation"] = currentPowerGeneration();
        if (!enriched.contains("source"))
            enriched["source"] = std::string("native.") + ev.substr(6);
    }
    json m = {{"event", ev}, {"data", enriched}};
    // Broadcast native events to the main renderer and every live child
    // renderer. In particular, shortcut.recording.* must reach the
    // standalone editor window that owns the recording session.
    ipc_post_json(0, m);
    for (const auto& [id, cw] : g_children) {
        if (cw && cw->view) ipc_post_json(id, m);
    }
    if (ev == "power.suspending" || ev == "power.resuming") {
        enriched["eventType"] = "PowerPending";
        json pending = {{"event", "power.pending"}, {"data", enriched}};
        ipc_post_json(0, pending);
        for (const auto& [id, cw] : g_children) {
            if (cw && cw->view) ipc_post_json(id, pending);
        }
    } else if (ev == "power.resumed") {
        enriched["eventType"] = "PowerReady";
        json ready = {{"event", "power.ready"}, {"data", enriched}};
        ipc_post_json(0, ready);
        for (const auto& [id, cw] : g_children) {
            if (cw && cw->view) ipc_post_json(id, ready);
        }
    } else if (ev == "power.sourceChanged" || ev == "power.acChanged" || ev == "power.schemeChanged") {
        enriched["eventType"] = "AcDcChanged";
        json acdc = {{"event", "power.acDcChanged"}, {"data", enriched}};
        ipc_post_json(0, acdc);
        for (const auto& [id, cw] : g_children) {
            if (cw && cw->view) ipc_post_json(id, acdc);
        }
    }
}

