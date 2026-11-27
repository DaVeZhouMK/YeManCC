// Included by main.cpp after the existing XInput and read-only USB helpers.

static bool win5MenuBoard() noexcept {
    return g_machineIdentity.resolved && g_machineIdentity.family == YmccFamily::Gpd &&
        g_machineIdentity.deviceClass == "HandheldCompanion.Devices.GPDWin5" &&
        _stricmp(g_machineIdentity.product.c_str(), "G1618-05") == 0;
}

static bool win5MenuSourceCurrent(const ymcc::win5Menu::Source& source) noexcept {
    return source.valid() && g_powerLifecycle.load() == PowerLifecycle::Ready &&
        source.power == currentPowerGeneration() && source.topology == g_xinputTopologyEpoch.load() &&
        source.identity == g_xinputSlotIdentityEpoch.load() && source.intent == g_inputHostVirtualIntentRevision.load() &&
        !g_inputHostVirtualIntentWriters.load() && !g_externalPriorityVirtualSuspended.load() && !g_exitRequested.load();
}

struct Win5MenuUsbProof {
    ymcc::win5Menu::Source source;
    std::wstring path;
    int portKind = -1;
};
static std::mutex g_win5MenuProofMx;
static Win5MenuUsbProof g_win5MenuUsbProof;
static std::atomic<bool> g_win5MenuProofQueued{false};
static std::atomic<bool> g_win5MenuRuleDemand{false};
static std::atomic<uint64_t> g_win5MenuSourceRevision{1};

// Only the UI sample below writes this observation, under g_gamepadReadMx.
struct Win5MenuSample {
    ymcc::win5Menu::Source source;
    ymcc::win5Menu::Reading reading = ymcc::win5Menu::Reading::Unavailable;
};
static Win5MenuSample g_win5MenuSample;

static bool win5MenuActionSourceCurrent(const ymcc::win5Menu::Source& source, uint64_t revision,
        uint64_t observed, uintptr_t foreground) noexcept {
    return revision && revision == g_win5MenuSourceRevision.load(std::memory_order_acquire) &&
        observed &&
        foreground && foreground == reinterpret_cast<uintptr_t>(GetForegroundWindow()) && win5MenuSourceCurrent(source);
}

static void win5MenuConfirmUsbAsync(const ymcc::win5Menu::Source& source, std::wstring_view path) {
    if (g_win5MenuProofQueued.exchange(true, std::memory_order_acq_rel)) return;
    try {
        if (gamepadSerialSubmit([source, path = std::wstring(path)] {
            struct Done { ~Done() { g_win5MenuProofQueued.store(false, std::memory_order_release); } } done;
            if (!win5MenuSourceCurrent(source)) return;
            std::wstring instance;
            const bool rootKnown = deviceInterfaceUsbInstance(path.c_str(), instance);
            const auto root = rootKnown ? hidHideUpper(W2U(instance)) : std::string{};
            // The observed XUSB device class is necessary, not sufficient. An
            // external port anywhere in the parent chain defeats admission.
            const int kind = root.rfind("USB\\VID_045E&PID_028E\\", 0) == 0
                ? externalPriorityUsbPortKindWithEvidence(root, nullptr, 0) : -1;
            if (!win5MenuSourceCurrent(source)) return;
            {
                std::lock_guard<std::mutex> lock(g_win5MenuProofMx);
                g_win5MenuUsbProof = {source, path, kind};
            }
            appendNativeLifecycleLog("win5-menu-source", {{"slot", source.slot}, {"episode", source.episode},
                {"power", source.power}, {"topology", source.topology}, {"identity", source.identity},
                {"portKind", kind}, {"root", root}, {"admitted", kind == 0}, {"readOnly", true}});
        })) return;
    } catch (...) { /* No probe means no new source admission. */ }
    g_win5MenuProofQueued.store(false, std::memory_order_release);
}

static void win5MenuPublishSample(bool requested, const std::array<Win5GuideRead, 4>& reads,
        ULONGLONG power, unsigned topology, unsigned identity, ULONGLONG intent, DWORD admitted) noexcept {
    Win5MenuSample next;
    const auto slot = g_xinputSemanticSlot;
    if (requested && slot < 4 && (admitted & (1u << slot)) &&
            !((g_xinputVirtualEndpointMask | g_xinputSlotUnknownMask) & (1u << slot)) &&
            g_xinputSlotIdentityResolvedEpoch[slot] == identity) {
        const auto proof = g_xinputFeedbackIdentity[slot].capture(identity, power);
        const auto path = g_xinputFeedbackIdentity[slot].physicalPath(identity, power);
        const ymcc::win5Menu::Source source{power, topology, identity, intent, proof.episode, slot};
        const auto status = reads[slot].status;
        if (!path.empty() && win5MenuSourceCurrent(source) &&
            (status == Win5GuideProbe::Up || status == Win5GuideProbe::Down)) {
            bool known = false;
            int portKind = -1;
            {
                std::unique_lock<std::mutex> lock(g_win5MenuProofMx, std::try_to_lock);
                if (lock.owns_lock() && g_win5MenuUsbProof.source == source && path == g_win5MenuUsbProof.path) {
                    known = true;
                    portKind = g_win5MenuUsbProof.portKind;
                }
            }
            if (!known) win5MenuConfirmUsbAsync(source, path);
            if (ymcc::win5Menu::qualifies(true, known, win5MenuSourceCurrent(source), true, false, false, portKind))
                next = {source, status == Win5GuideProbe::Down ? ymcc::win5Menu::Reading::Down : ymcc::win5Menu::Reading::Up};
        }
    }
    if (g_win5MenuSample.source != next.source) g_win5MenuSourceRevision.fetch_add(1, std::memory_order_release);
    g_win5MenuSample = next;
}
