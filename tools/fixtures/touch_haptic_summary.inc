// Explicit summary-only scope of screen_touchpads_haptics_selftest.
static Json touchHapticSummarySelfTest() {
    using namespace ymcc::touchhaptic;
    Json cases=Json::array();const auto check=[&](const char* name,bool ok){cases.push_back({{"name",name},{"ok",ok}});};
    Summary summary;Receipt r;r.ok=false;r.reason=Reason::SourcePending;r.attempted=false;
    summary.record(r);auto counts=summary.take();
    check("receipt-not-attempted-does-not-claim-output-API-failure",counts.failed==1&&counts.notAttemptedFailures==1&&counts.attemptedFailures==0&&counts.unknownAttemptFailures==0);
    r.reason=Reason::WriteFailed;r.attempted=true;r.error=5;summary.record(r);counts=summary.take();
    check("original-attempted-flag-partitions-unsuccessful-receipt",counts.failed==1&&counts.attemptedFailures==1&&counts.notAttemptedFailures==0&&counts.errors[(size_t)Reason::WriteFailed]==5);
    summary.failure(Reason::SourceChanged,false,FailureAttempt::NotAttempted);counts=summary.take();
    check("known-predispatch-refusal-is-not-an-attempt",counts.failed==1&&counts.notAttemptedFailures==1&&counts.reasons[(size_t)Reason::SourceChanged]==1);
    summary.failure(Reason::SourcePending);summary.record(false,false);counts=summary.take();
    check("legacy-callers-keep-unknown-attempt-instead-of-reason-inference",counts.failed==2&&counts.unknownAttemptFailures==2&&counts.notAttemptedFailures==0);
    summary.failure(Reason::QueueExpired,true,FailureAttempt::Attempted);counts=summary.take();
    check("dropped-is-separate-from-failed-attempt-partition",counts.dropped==1&&counts.failed==0&&counts.attemptedFailures==0&&counts.notAttemptedFailures==0&&counts.unknownAttemptFailures==0);
    r={};r.ok=true;r.attempted=true;r.backend=Backend::XInput;r.strength=40;r.outputBaselineAttempted=true;r.outputBaselineAccepted=true;
    summary.record(r);counts=summary.take();
    check("successful-pulse-and-baseline-counters-remain-original",counts.sent==1&&counts.xinput==1&&counts.medium==1&&counts.outputBaselineAttempts==1&&counts.outputBaselineAccepted==1&&counts.failed==0&&counts.attemptedFailures==0);
    summary.record(true,true,60);summary.record(false,false);summary.failure(Reason::OpenFailed,false,FailureAttempt::Attempted);
    summary.failure(Reason::SourcePending,false,FailureAttempt::NotAttempted);counts=summary.take();
    check("mixed-window-preserves-old-totals-and-three-way-partition",counts.sent==1&&counts.msi==1&&counts.strong==1&&counts.failed==3&&counts.attemptedFailures==1&&counts.notAttemptedFailures==1&&counts.unknownAttemptFailures==1);
    const auto empty=summary.take();check("take-clears-all-new-and-original-counters",summary.empty()&&empty.failed==0&&empty.sent==0&&empty.attemptedFailures==0&&empty.notAttemptedFailures==0&&empty.unknownAttemptFailures==0);
    r={};r.reason=Reason::WriteFailed;r.attempted=true;r.error=5;summary.record(r);r.error=6;summary.record(r);counts=summary.take();
    check("errors-remain-last-original-codes-not-counts",counts.failed==2&&counts.attemptedFailures==2&&counts.reasons[(size_t)Reason::WriteFailed]==2&&counts.errors[(size_t)Reason::WriteFailed]==6);
    unsigned failed=0;for(const auto& row:cases)if(!row["ok"].get<bool>())++failed;
    return {{"ok",failed==0},{"cases",cases},{"failedChecks",failed},{"scope","haptic-summary-only"},
        {"hardwareWrites",0},{"systemInputInjected",false},{"virtualTargetStarted",false}};
}
