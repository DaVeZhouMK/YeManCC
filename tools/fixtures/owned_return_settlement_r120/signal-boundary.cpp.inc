// Qualified-signal boundary: intentionally no ReadSignal/read-window/identity
// implementation here. The parent independently owns those qualifications.
static unsigned long long g_signalSequence=1;
static MsiOwnedReturnSignal msiOwnedReturnReadSignal(unsigned long long,unsigned,ULONGLONG) {
    ++g_signalCalls;
    if(g_signalHook) { auto hook=std::move(g_signalHook);g_signalHook={};hook(); }
    MsiOwnedReturnSignal result;result.matched=true;result.mask=1;result.sequence=g_signalSequence;
    result.readStartedTick=g_now-1;result.readCompletedTick=g_now;result.readIdentity=23;return result;
}
static bool fixtureCapturedContextCurrent() {
    return !inputHostExitFenceRaised() && currentPowerGeneration()==11 && g_msiFeedbackModeGeneration.load()==12 &&
        g_xinputTopologyEpoch.load()==13 && g_msiOwnedReturnSourceEvent.load()==14 && g_msiMainSourceRevision.load()==15 &&
        inputHostEpochToken()==16 && g_msiDinputReturnDebtEpisode.load()==17 &&
        g_inputHostVirtualIntentRevision.load()==18 && g_inputHostVirtualIntentWriters.load()==0;
}

