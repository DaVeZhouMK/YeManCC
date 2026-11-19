static int msiPostHideCycleStepPartialBefore(uint64_t ticket) {
    std::lock_guard leaseLock(g_msiNativeForegroundMx);
    auto& s=g_msiNativeForegroundLease;
    if(!s.postHideCycle && nativeForegroundProfile()!=ymcc::nativeForeground::Profile::Gpd)return 0;
    if(s.postHideCycle && !msiPostHideContextCurrentLocked())return 2;
    if(!s.postHideCycle){
        if(s.active || g_msiNativeForegroundCyclePending || s.cycleProcess || s.nativeRefreshWriter)return 2;
        const auto originalNow=inputHostStartProtectionNow();
        const auto view=g_virtualStartProtection.view(originalNow);
        auto& j=g_hidHideVisibilityJournal;
        if(!ticket || view.requestId!=ticket || !view.locked || !view.remainingMs ||
            view.persona!=inputHostConfiguredPersona() || !j.active || !j.before.valid ||
            g_inputHostPrepared || g_inputHostPrepareMayHaveReachedHost || g_inputHostActive)return 2;
        // Missing optional refresh recognition is not a new prerequisite for
        // standard virtual output. Only an already-started owned action may
        // require cancellation/return instead of this pre-mutation no-op.
        if(j.physicalSources.size()!=1 || !j.physical.resolved ||
            !nativeForegroundRootCandidate(j.physical.baseContainerDeviceInstanceId) ||
            !nativeForegroundGameInputAdmitted(true,hidHideIsVirtualIdentity(j.physical.deviceInstanceId,j.physical.baseContainerDeviceInstanceId),
                j.physical.vendorId,j.physical.productId,j.physical.usagePage,j.physical.usage))
            return msiPostHideSkip(ticket,"selected-refresh-class-not-proven");
        // HC skips re-cycling an already hidden selected source. A carried
        // continuous-persona journal does not reopen consumers; foreign hiding
        // grants no restart ownership to this candidate.
        // A newly transferred native hide has not completed this virtual
        // execution's post-hide cycle. Only its exact authenticated journal
        // stamp removes this skip; all original mutation proofs below remain.
        const bool nativeHandoffCycle=j.nativeHandoffCycleRequest==ticket &&
            j.nativeHandoffCyclePower==currentPowerGeneration() && j.nativeHandoffCycleEpoch==g_inputHostEpoch &&
            j.transactionId==g_inputHostRunId;
        if(!nativeHandoffCycle && (hidHideContainsPath(j.before.hidden,j.physical.baseContainerDeviceInstanceId) ||
            hidHideContainsPath(j.before.hidden,j.physical.deviceInstanceId)))return msiPostHideSkip(ticket,"selected-source-already-hidden");
        if(!hidHideContainsPath(j.hiddenByUs,j.physical.baseContainerDeviceInstanceId) ||
            !hidHideContainsPath(j.hiddenByUs,j.physical.deviceInstanceId))return 2;
        if(!j.physicalSources.front().resolved ||
            ascii_lower(j.physicalSources.front().deviceInstanceId)!=ascii_lower(j.physical.deviceInstanceId) ||
            ascii_lower(j.physicalSources.front().baseContainerDeviceInstanceId)!=ascii_lower(j.physical.baseContainerDeviceInstanceId))return 2;
        const auto expectedPower=currentPowerGeneration();const auto expectedEpoch=g_inputHostEpoch;
        const auto expectedRun=g_inputHostRunId;const auto expectedProcess=g_inputHostProcess;
        const auto root=j.physical.baseContainerDeviceInstanceId;
        const auto positions=msiForegroundIsolationLocations(root);
        const auto xusb=msiNativeForegroundXusbInventory(j.physical);
        if(positions.empty() || !xusb.complete)return msiPostHideSkip(ticket,"selected-xusb-location-or-inventory-unproven");
        unsigned exact=0;
        for(const auto& item:xusb.interfaces)if(nativeForegroundCurrentXusbTargetAdmitted(root,item) &&
            ascii_lower(item.instanceId)==ascii_lower(root))++exact;
        if(exact!=1)return msiPostHideSkip(ticket,"exact-selected-xusb-node-not-proven");
        if(!msiPostHideRootStartedFor(root,positions))return msiPostHideSkip(ticket,"selected-node-readiness-unproven-no-cycle");
        HidHideVisibilitySnapshot after;
        if(!msiNativeForegroundSnapshot(after) || !after.valid || !after.cloaking ||
            hidHideInverseApplicationListState(after.inverseRaw)!="off" ||
            !hidHideContainsPath(after.applications,W2U(hidHideCurrentModulePath())) ||
            !hidHideContainsPath(after.hidden,root) || !hidHideContainsPath(after.hidden,j.physical.deviceInstanceId))return 2;
        if(currentPowerGeneration()!=expectedPower || g_inputHostEpoch!=expectedEpoch || g_inputHostRunId!=expectedRun ||
            g_inputHostProcess!=expectedProcess || g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId!=ticket)return 2;
        // The ordinary journal already owns durable hide/return responsibility.
        // This lease owns only the one exact helper, never a second hide writer.
        s=MsiNativeForegroundLease{};s.profile=ymcc::nativeForeground::Profile::Gpd;
        s.active=true;s.configurationReturned=true;s.postHideCycle=true;
        g_postHideCycleOwned.store(true,std::memory_order_release);
        s.root=root;s.locations=positions;s.power=expectedPower;s.postHideHostProcess=expectedProcess;
        s.postHideTicket=ticket;s.postHideEpoch=g_inputHostEpoch;s.postHideRunId=g_inputHostRunId;s.postHidePersona=view.persona;
        s.postHideDeadline=originalNow+view.remainingMs;
        if(g_inputHostSwitchRecoveryStartDeadline)s.postHideDeadline=(std::min)(s.postHideDeadline,g_inputHostSwitchRecoveryStartDeadline);
        if(!msiPostHideContextCurrentLocked())return 2;
        // Consume once when the original bounded helper owner is established,
        // before dispatch. Failure/late work cannot grant another activation.
        if(nativeHandoffCycle){
            j.nativeHandoffCycleRequest=0;j.nativeHandoffCyclePower=0;j.nativeHandoffCycleEpoch=0;
        }
        auto target=j.physical;target.deviceInstanceId=root;
        const int begin=msiNativeForegroundCycleLocked(false,&target,true);
        msiNativeForegroundReceipt("post-hide-cycle-started",nativeHandoffCycle?
            "owned-native-handoff;original-GPD-cycle;external-input-unverified":
            "HC-selected-root-cycle;SDK-only-no-external-proof");
        if(begin==2)return 2;
    }
    if(s.postHideTicket!=ticket || !msiPostHideContextCurrentLocked())return 2;
    if(g_msiNativeForegroundCyclePending)(void)msiNativeForegroundPollCycleLocked();
    if(g_msiNativeForegroundCyclePending || s.cycleProcess || s.nativeRefreshWriter)return 1;
    if(!s.cycleAccepted || s.helperExitCode!=0)return 2;
    if(!s.postHideTerminalTick){
        s.postHideTerminalTick=GetTickCount64();s.postHideBaselineSequence=msiPostHideLatestSourceLocked().sequence;
        xinputSlotsMarkDiscoveryDirty();xinputSlotsRequestIdentityReprobe(0xFu,"gpd-post-hide-cycle-returned");
        g_xinputPreTargetBoundaryScanned.store(false,std::memory_order_relaxed);
        g_xinputSlotFullScanEpochStart.store(g_xinputTopologyEpoch.load(std::memory_order_acquire),std::memory_order_release);
        g_xinputSlotFullScanOnce.store(true,std::memory_order_relaxed);
        msiNativeForegroundReceipt("post-hide-source-wait","same-R3;newer-source-read-required");return 1;
    }
    if(!s.postHideReadmitted)return 1;
    const auto topology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
    if(s.postHideValidatedTopology!=topology){
        if(!msiPostHideRootStartedLocked())return 1;
        s.postHideValidatedTopology=topology;
    }
    const auto source=msiPostHideLatestSourceLocked();
    if(source.route!=ymcc::input::recoveryHealth::Route::XInput || !source.live || !source.stable ||
        !source.incarnation || !source.continuity || source.continuity==(std::numeric_limits<uint64_t>::max)() ||
        source.sequence<=s.postHideBaselineSequence || source.started<s.postHideSourceAfterTick ||
        source.identity!=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire) ||
        !ymcc::input::recoveryHealth::fresh(source,GetTickCount64(),s.power,topology))return 1;
    if(!msiPostHideContextCurrentLocked())return 2;
    msiNativeForegroundReceipt("post-hide-source-returned","new-current-source-read;external-visibility-unverified");
    msiNativeForegroundClearLeaseLocked();return s.active?2:0;
}
