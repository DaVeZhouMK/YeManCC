// Fixture-only reads after original maintenance/Stop has returned. Never call
// this from a product callback while holding BUS/persona/return/store locks.
// Separately sampled atomics/try-lock sections are diagnostics, not a coherent
// physical-return or durable-settlement proof. Missing observations stay null.
static json fixtureMsiReturnObservation() noexcept {
    try {
        const auto owedBefore=g_msiDinputReturnOwed.load(std::memory_order_acquire);
        const auto episodeBefore=g_msiDinputReturnDebtEpisode.load(std::memory_order_acquire);
        const auto modeBefore=g_msiFeedbackModeGeneration.load(std::memory_order_acquire);
        const bool seam=ymStartSeamActive();
        json row={{"schemaVersion",1},{"observationIsReturnProof",false},{"coherentSnapshot",false},
            {"modeReturnOwed",owedBefore},{"debtEpisode",episodeBefore},{"modeGeneration",modeBefore},
            {"routeArmed",g_msiDinputRouteDinputArmed.load(std::memory_order_acquire)},
            {"matchedEpisode",g_msiOwnedReturnMatchedEpisode.load(std::memory_order_acquire)},
            {"matchedPower",g_msiOwnedReturnMatchedPower.load(std::memory_order_acquire)},
            {"matchedTopology",g_msiOwnedReturnMatchedTopology.load(std::memory_order_acquire)},
            {"currentTopology",g_xinputTopologyEpoch.load(std::memory_order_acquire)},
            {"publishedPending",g_msiOwnedReturnPending.load(std::memory_order_acquire)},
            {"publishedWorking",g_msiOwnedReturnWorking.load(std::memory_order_acquire)},
            {"publishedFailed",g_msiOwnedReturnFailed.load(std::memory_order_acquire)},
            {"startSeamActive",seam},{"durableDischargeUnavailableInStartSeam",seam},
            {"durableDischargeProven",nullptr},{"logicalConfiguredPersona",nullptr},
            {"operationalTargetEnabled",nullptr},{"logicalOffEligibility",nullptr},
            {"personaObserved",false},{"ownerMetadataObserved",false},{"storeMetadataObserved",false},
            {"owner",nullptr},{"store",nullptr}};
        {
            std::unique_lock<std::mutex> bus(g_inputCaptureBusTickMx,std::try_to_lock);
            if(bus.owns_lock()) {
                std::unique_lock<std::mutex> persona(g_inputHostPersonaMx,std::try_to_lock);
                if(persona.owns_lock()) {
                    row["logicalConfiguredPersona"]=g_realStickTest.virtualTargetPersona;
                    row["operationalTargetEnabled"]=g_realStickTest.virtualTargetEnabled;
                    row["logicalOffEligibility"]=!g_realStickTest.virtualTargetEnabled &&
                        g_realStickTest.virtualTargetPersona=="disabled";
                    row["personaObserved"]=true;
                }
            }
        }
        {
            std::unique_lock<std::mutex> owner(g_msiOwnedReturnMx,std::try_to_lock);
            if(owner.owns_lock()) {
                row["owner"]={{"episode",g_msiOwnedReturnRecovery.episode()},
                    {"pending",g_msiOwnedReturnRecovery.pending()},
                    {"working",g_msiOwnedReturnRecovery.working()},
                    {"failed",g_msiOwnedReturnRecovery.failed()},
                    {"completed",g_msiOwnedReturnRecovery.completed()},
                    {"cancelled",g_msiOwnedReturnRecovery.cancelled()},
                    {"retryUsed",g_msiOwnedReturnRecovery.retryUsed()},
                    {"observationCount",g_msiOwnedReturnRecovery.observations()},
                    {"expectedRoot",g_msiOwnedReturnExpected.rootInstance},
                    {"expectedLocations",g_msiOwnedReturnExpected.locationPaths}};
                row["ownerMetadataObserved"]=true;
            }
        }
        {
            std::unique_lock<std::mutex> store(g_msiCrashMx,std::try_to_lock);
            if(store.owns_lock()) {
                row["store"]={{"loaded",g_msiCrashLoaded},{"invalid",g_msiCrashStoreInvalid},
                    {"modeReturnPending",g_msiCrashRecord.modeReturnPending},
                    {"rootInstance",g_msiCrashRecord.rootInstance},
                    {"locations",g_msiCrashRecord.locationPaths},
                    {"hiddenPathCount",g_msiCrashRecord.hiddenPaths.size()}};
                row["storeMetadataObserved"]=true;
            }
        }
        row["modeDebtReadStable"]=owedBefore==g_msiDinputReturnOwed.load(std::memory_order_acquire) &&
            episodeBefore==g_msiDinputReturnDebtEpisode.load(std::memory_order_acquire) &&
            modeBefore==g_msiFeedbackModeGeneration.load(std::memory_order_acquire);
        return row;
    } catch(...) { return nullptr; }
}
static void fixtureCaptureMsiReturnObservation(json& target,const char* name) noexcept {
    try { target[name]=fixtureMsiReturnObservation(); } catch(...) { /* Preserve the original outcome. */ }
}
