// Included only by the existing publisher test's explicit publication-revocation scope.
// Production callback/snapshot are exact extractions; source events/owner are real Win32.
test("publication-revocation-default-denies-actual-publisher", [&] {
    reset(targets.at(0), false);
    expect(!publicationRevocation.current(), "unbound publication capability accepted");
    expectPublicationRejection(3, 0);
});
test("publication-revocation-real-source-preserves-original-full-gameid", [&] {
    const DWORD groupColdEntry = publicationHandleCount();
    reset(targets.at(0));
    expect(publicationRevocation.current(), "live real source was not current");
    success();
    expect(WaitForSingleObject(publicationSources.power, 0) == WAIT_TIMEOUT &&
           WaitForSingleObject(publicationSources.identityEvent, 0) == WAIT_TIMEOUT,
           "successful publisher changed borrowed source events");

    // One original canonical case, with exact production callback/helpers and
    // real receiver/events. These Graph/PID/path values are explicit sparse-map
    // fixture data; no typed host owner or held process topology is asserted.
    const DWORD afterOriginalSingleControl = publicationHandleCount();
    reset(targets.at(0));
    {
        PublicationGroupRequest warm("group-source-cold-setup-no-callback"); warm.setup();
        expectGroupOtherSources(0, false);
        expect(warm.finish(), "cold group source setup failed to return real owned resources");
    }
    const DWORD stableSourceBaseline = publicationHandleCount();
    publicationLeaseOwnerEvidence.push_back({{"groupFixtureColdBaseline", true}, {"handlesAtOriginalCaseEntry", groupColdEntry},
        {"handlesAfterOriginalSingleSuccess", afterOriginalSingleControl}, {"handlesAfterFirstGroupSetupReturn", stableSourceBaseline},
        {"coldDeltaNotSilentlyDiscarded", static_cast<LONG64>(stableSourceBaseline) - groupColdEntry},
        {"containsOriginalPowerIdentitySources", true}, {"newProcessesStarted", 0}});
    const std::array<tr::QualificationRoute, 2> routes{tr::QualificationRoute::LegacyDynamic, tr::QualificationRoute::SemiStatic};
    const std::array<const char*, 3> sourceNames{"group", "process-request", "business-request"};
    const std::array<const char*, 3> stageNames{"before-callback", "inside-first-snapshot-read", "final-publisher-identity-port"};
    for (const auto route : routes) {
        const std::string routeName = tr::qualificationRouteName(route);
        reset(targets.at(0)); bound.qualificationRoute = route; fixedOriginalQualification = route == tr::QualificationRoute::LegacyDynamic && fixtureLegacyQualification == "fixed";
        expect(rawQualification() == (legacyQualification() && fixedOriginalQualification), "qualification route and module fact were conflated");
        publicationLeaseOwnerEvidence.push_back({{"qualificationInput", true}, {"route", tr::qualificationRouteName(route)}, {"fixedOriginalQualification", fixedOriginalQualification}, {"rawQualification", rawQualification()}});
        {
            PublicationGroupRequest positive(routeName + ":group-uncancelled-full64"); positive.setup();
            expectGroupPublicationSuccess();
            publicationLeaseOwnerEvidence.push_back({{"groupCallbackBoundary", routeName + ":group-uncancelled-full64"},
                {"fullGameId", std::to_string(capturedGameId)}, {"primary", originalTarget.primary}, {"foreground", originalTarget.foreground},
                {"actualPublisherStubCalls", stubCalls}, {"callbackResult", static_cast<LONG>(live.callbackResult)},
                {"productionSnapshotAndPublishExact", true}, {"actualNativePublisherCodeExecuted", false}});
            expect(positive.finish(), "group positive receiver/source resource return failed");
        }
        expect(publicationHandleCount() == stableSourceBaseline, "group positive changed stable actual handle baseline");
        for (unsigned cancelled = 0; cancelled < 3; ++cancelled) {
            for (unsigned stage = 0; stage < 3; ++stage) {
                reset(targets.at(0)); bound.qualificationRoute = route; fixedOriginalQualification = route == tr::QualificationRoute::LegacyDynamic && fixtureLegacyQualification == "fixed";
        expect(rawQualification() == (legacyQualification() && fixedOriginalQualification), "qualification route and module fact were conflated");
        publicationLeaseOwnerEvidence.push_back({{"qualificationInput", true}, {"route", tr::qualificationRouteName(route)}, {"fixedOriginalQualification", fixedOriginalQualification}, {"rawQualification", rawQualification()}});
                const std::string name = routeName + ":" + sourceNames[cancelled] + ":" + stageNames[stage];
                PublicationGroupRequest request(name); request.setup(); bool hookRan = false;
                const auto signal = [&] {
                    hookRan = true;
                    expect(SetEvent(publicationGroupSources.sources[cancelled]) != FALSE, "actual group/request source signal failed");
                    expectGroupOtherSources(cancelled, true);
                };
                if (stage == 0) signal();
                else if (stage == 1) snapshotReadHook = signal;
                else groupFinalIdentityHook = signal;
                const auto untouched = memory; const auto proof = bound.healthyProof;
                expectPublicationRejection(stage == 2 ? 4 : 1, stage == 2 ? 1 : 0);
                expect(hookRan && !snapshotReadHook && !groupFinalIdentityHook, "selected actual callback cancellation hook did not settle once");
                expect(memory == untouched && std::memcmp(&proof, &bound.healthyProof, sizeof(proof)) == 0,
                       "group cancellation changed original sparse memory or healthy pins");
                expect(!groupCancellationCurrent() && !groupPublicationRevocation.current(), "actual group receiver cancellation was ignored");
                expectGroupOtherSources(cancelled, true);
                publicationLeaseOwnerEvidence.push_back({{"groupCallbackBoundary", name}, {"route", routeName}, {"source", sourceNames[cancelled]},
                    {"stage", stageNames[stage]}, {"hookRan", hookRan}, {"rejectCode", live.rejectCode},
                    {"guardBefore", static_cast<LONG>(live.guardBefore)}, {"actualPublisherStubCalls", stubCalls},
                    {"productionPublisherCalls", static_cast<LONG>(live.publisherCalls)}, {"memoryUnchanged", memory == untouched},
                    {"otherTwoAndOriginalPowerIdentityUnsignalled", true}, {"rawGraphIsTypedOwner", false}});
                expect(request.finish(), "group cancellation receiver/source resource return failed");
                expect(publicationHandleCount() == stableSourceBaseline, "cancelled group changed stable actual handle baseline");
            }
        }
    }

    // Original-only v2 must survive the final publisher checkpoint without a
    // topology lease. Exactly owner + process-request + business-request copies.
    for (const auto route : routes) {
        for (unsigned cancelled = 0; cancelled < 3; ++cancelled) {
            reset(targets.at(0)); bound.qualificationRoute = route; fixedOriginalQualification = route == tr::QualificationRoute::LegacyDynamic && fixtureLegacyQualification == "fixed";
        expect(rawQualification() == (legacyQualification() && fixedOriginalQualification), "qualification route and module fact were conflated");
        publicationLeaseOwnerEvidence.push_back({{"qualificationInput", true}, {"route", tr::qualificationRouteName(route)}, {"fixedOriginalQualification", fixedOriginalQualification}, {"rawQualification", rawQualification()}});
            const DWORD originalBefore = publicationHandleCount();
            publicationGroupSources.create(2);
            bound.roleGroup.version = 2;
            bound.roleGroup.cancellation = publicationGroupSources.wire;
            expect(tr::validRoleGroupShape(bound) && tr::validRoleGroupWire(bound), "original v2 shape rejected");
            expect(!oldReaderValidRoleGroupShape(bound), "old reader silently accepted original v2");
            const DWORD beforeBind = publicationHandleCount();
            expect(groupPublicationRevocation.bindOriginal(publicationGroupSources.owner, bound.roleGroup.cancellation), "original two-event bind rejected");
            expect(publicationHandleCount() == beforeBind + 3, "original receiver did not own exactly three copies");
            bool hookRan = false;
            if (cancelled < 2) {
                groupFinalIdentityHook = [&] { hookRan = true; expect(SetEvent(publicationGroupSources.sources[cancelled]) != FALSE, "original request signal failed"); };
                const auto proof = bound.healthyProof;
                expectPublicationRejection(4, 1);
                expect(hookRan && !groupFinalIdentityHook && !groupCancellationCurrent(), "original final checkpoint ignored cancellation");
                expect(std::memcmp(&proof, &bound.healthyProof, sizeof proof) == 0, "original cancellation changed healthy pins");
                expect(WaitForSingleObject(publicationGroupSources.sources[1-cancelled], 0) == WAIT_TIMEOUT, "other original request was signalled");
                expect(ResetEvent(publicationGroupSources.sources[cancelled]) != FALSE && !groupCancellationCurrent(), "original reset revived revoked request");
            } else success();
            groupPublicationRevocation.retire();
            expect(publicationHandleCount() == beforeBind, "original receiver copies were not returned");
            expect(publicationGroupSources.close() && publicationHandleCount() == originalBefore, "original source slots were not returned");
            publicationLeaseOwnerEvidence.push_back({{"originalOnlyV2", true}, {"route", tr::qualificationRouteName(route)},
                {"cancelledSource", cancelled < 2 ? (cancelled == 0 ? "process-request" : "business-request") : "none"},
                {"finalHookRan", hookRan}, {"publisherCalls", stubCalls}, {"oldReaderRejected", true}, {"receiverCopies", 3}, {"resourcesReturned", true}});
        }
    }
    reset(targets.at(0));
    expect(tr::validRoleGroupWire(bound) && oldReaderValidRoleGroupShape(bound), "legacy exact empty wire no longer compatible");
    success();
    for (unsigned variant = 0; variant < 24; ++variant) {
        reset(targets.at(0)); publicationGroupSources.create(2);
        bound.roleGroup.version = 2; bound.roleGroup.cancellation = publicationGroupSources.wire;
        auto& wire = bound.roleGroup; auto& cancellation = wire.cancellation;
        switch (variant) {
        case 0: cancellation.sourceProcessRequestEvent = 0; break;
        case 1: cancellation.sourceBusinessRequestEvent = 0; break;
        case 2: cancellation.sourceProcessRequestEvent = UINT64_MAX; break;
        case 3: cancellation.sourceBusinessRequestEvent = UINT64_MAX; break;
        case 4: cancellation.sourceBusinessRequestEvent = cancellation.sourceProcessRequestEvent; break;
        case 5: cancellation.sourceGroupEvent = cancellation.sourceProcessRequestEvent; break;
        case 6: cancellation.size = 0; break;
        case 7: cancellation.version = 2; break;
        case 8: wire.enabled = 1; break;
        case 9: wire.sourceRevision = 1; break;
        case 10: wire.consoleRevision = 1; break;
        case 11: wire.fullGameId = bound.gameId; break;
        case 12: wire.expectedPrimary = bound.gamePid; break;
        case 13: wire.expectedForeground = bound.gamePid; break;
        case 14: wire.graph.count = 1; break;
        case 15: wire.graph.selectedPid = bound.gamePid; break;
        case 16: wire.graph.members.back().birth = 1; break;
        case 17: wire.identities.back().path[519] = L'x'; break;
        case 18: wire.identities.back().pid = 1; break;
        case 19: wire.reserved = 1; break;
        case 20: wire.version = 1; break; // v1 disabled must never smuggle two events.
        case 21: wire.version = 3; break;
        case 22: cancellation = {}; break; // v2 must never silently degrade to empty.
        default: wire.version = 1; cancellation = {}; cancellation.sourceBusinessRequestEvent = 1; break;
        }
        expect(!tr::validRoleGroupWire(bound), "partial/malformed/hidden original wire accepted");
        expect(!groupCancellationCurrent(), "malformed packet bypassed current checkpoint");
    }
    // Numeric shape cannot detect distinct handles for the same Kernel object.
    for (unsigned variant = 0; variant < 3; ++variant) {
        reset(targets.at(0)); publicationGroupSources.create(2);
        auto wire = publicationGroupSources.wire;
        HANDLE alias = nullptr;
        if (variant == 0) {
            expect(DuplicateHandle(GetCurrentProcess(), publicationGroupSources.sources[0], GetCurrentProcess(), &alias, SYNCHRONIZE, FALSE, 0) != FALSE, "original alias duplicate failed");
            wire.sourceBusinessRequestEvent = reinterpret_cast<U64>(alias);
        } else if (variant == 1) wire.sourceProcessRequestEvent = reinterpret_cast<U64>(publicationGroupSources.owner);
        else wire.sourceBusinessRequestEvent = UINT64_MAX - 2;
        const DWORD before = publicationHandleCount();
        expect(!groupPublicationRevocation.bindOriginal(publicationGroupSources.owner, wire) && !groupPublicationRevocation.current(), "original object alias/unknown slot accepted");
        expect(publicationHandleCount() == before, "partial original bind leaked copies");
        expect(!groupPublicationRevocation.bindOriginal(publicationGroupSources.owner, publicationGroupSources.wire), "failed original bind retried");
        if (alias) expect(CloseHandle(alias) != FALSE, "original alias close failed");
    }
    resetPublicationGroupRequest();
    expect(publicationGroupSources.opened == publicationGroupSources.closed && !publicationGroupSources.closeErrors,
           "original source/slot ledger debt remains");
    expect(!publicationGroupCleanupFailed && publicationGroupSources.opened == publicationGroupSources.closed &&
           publicationGroupSources.closeErrors == 0, "group source/slot resource debt remains");
    publicationLeaseOwnerEvidence.push_back({{"groupFixtureFinalResourceReadback", true}, {"handlesAtStableBaseline", stableSourceBaseline},
        {"handlesAfterAllIndependentRequests", publicationHandleCount()}, {"sourceAndSlotHandlesOpened", publicationGroupSources.opened},
        {"sourceAndSlotHandlesClosed", publicationGroupSources.closed}, {"sourceCloseErrors", publicationGroupSources.closeErrors},
        {"independentReceiverRequests", 21}, {"groupPositiveCallbackCalls", 2}, {"groupCancelledCallbackCalls", 18},
        {"originalSinglePositiveRetained", true}, {"wholeProcessColdGrowthWaived", false}});
});
test("publication-revocation-power-before-publish-denies-sdk", [&] {
    reset(targets.at(0));
    expect(SetEvent(publicationSources.power) != FALSE, "power signal failed");
    expectPublicationRejection(3, 0);
});
test("publication-revocation-identity-before-publish-denies-sdk", [&] {
    reset(targets.at(0));
    expect(SetEvent(publicationSources.identityEvent) != FALSE, "identity signal failed");
    expectPublicationRejection(3, 0);
});
test("publication-revocation-power-during-original-snapshot-denies-final-sdk", [&] {
    reset(targets.at(0));
    bool hookRan = false;
    snapshotReadHook = [&] { hookRan = true; expect(SetEvent(publicationSources.power) != FALSE, "snapshot power signal failed"); };
    expectPublicationRejection(4, 1);
    expect(hookRan && !snapshotReadHook, "did not signal between exact snapshot and final publisher boundary");
});
test("publication-revocation-identity-during-original-snapshot-denies-final-sdk", [&] {
    reset(targets.at(0));
    bool hookRan = false;
    snapshotReadHook = [&] { hookRan = true; expect(SetEvent(publicationSources.identityEvent) != FALSE, "snapshot identity signal failed"); };
    expectPublicationRejection(4, 1);
    expect(hookRan && !snapshotReadHook, "identity signal did not traverse actual snapshot");
});
test("publication-revocation-power-reset-and-ready-cannot-revive", [&] {
    reset(targets.at(0));
    expect(SetEvent(publicationSources.power) != FALSE, "power cancellation failed");
    expect(!publicationRevocation.current(), "power cancellation was ignored");
    expect(ResetEvent(publicationSources.power) != FALSE, "fixture source reset failed");
    ++publicationSources.wire.power.publicationRevision;
    expect(!publicationRevocation.current(), "later Ready/reset revived consumed source");
    expect(!publicationRevocation.bind(publicationSources.owner, publicationSources.wire), "Ready minted a second binding");
    expectPublicationRejection(3, 0);
});
test("publication-revocation-identity-reset-cannot-revive", [&] {
    reset(targets.at(0));
    expect(SetEvent(publicationSources.identityEvent) != FALSE, "identity cancellation failed");
    expect(!publicationRevocation.current(), "identity cancellation was ignored");
    expect(ResetEvent(publicationSources.identityEvent) != FALSE, "fixture identity reset failed");
    expect(!publicationRevocation.current(), "identity reset revived cancelled publication");
    expectPublicationRejection(3, 0);
});
test("publication-revocation-bind-attempt-is-single-use", [&] {
    reset(targets.at(0), false);
    auto missing = publicationSources.wire;
    missing.sourcePowerEvent = 0;
    expect(!publicationRevocation.bind(publicationSources.owner, missing), "missing event admitted");
    expect(!publicationRevocation.bind(publicationSources.owner, publicationSources.wire), "failed bind could be retried");
    expectPublicationRejection(3, 0);
});
test("publication-revocation-missing-unknown-wire-and-owner-denied", [&] {
    for (unsigned variant = 0; variant != 9; ++variant) {
        reset(targets.at(0), false);
        auto invalid = publicationSources.wire;
        HANDLE owner = publicationSources.owner;
        switch (variant) {
        case 0: invalid.sourcePowerEvent = 0; break;
        case 1: invalid.sourceIdentityEvent = 0; break;
        case 2: invalid.sourcePowerEvent = UINT64_MAX; break;
        case 3: invalid.sourceIdentityEvent = UINT64_MAX; break;
        case 4: invalid.size = 0; break;
        case 5: invalid.version = 99; break;
        case 6: owner = nullptr; break;
        case 7: owner = INVALID_HANDLE_VALUE; break;
        default: invalid.sourcePowerEvent = UINT64_MAX - 2; break;
        }
        const DWORD before = publicationHandleCount();
        expect(!publicationRevocation.bind(owner, invalid), "unknown/missing source admitted");
        expect(!publicationRevocation.current(), "failed bind became current");
        expect(publicationHandleCount() == before, "failed bind leaked partial duplicates");
        expectPublicationRejection(3, 0);
    }
});
test("publication-revocation-same-handle-alias-denied", [&] {
    reset(targets.at(0), false);
    auto alias = publicationSources.wire;
    alias.sourceIdentityEvent = alias.sourcePowerEvent;
    expect(!publicationRevocation.bind(publicationSources.owner, alias), "identical source handle pair admitted");
    expectPublicationRejection(3, 0);
});
test("publication-revocation-distinct-handle-object-alias-denied", [&] {
    reset(targets.at(0), false);
    HANDLE alias = nullptr;
    expect(DuplicateHandle(GetCurrentProcess(), publicationSources.power, GetCurrentProcess(), &alias,
                           SYNCHRONIZE, FALSE, 0) != FALSE, "fixture alias creation failed");
    const auto closeAlias = [](void* handle) { if (handle) CloseHandle(handle); };
    const std::unique_ptr<void, decltype(closeAlias)> ownedAlias(alias, closeAlias);
    auto wire = publicationSources.wire;
    wire.sourceIdentityEvent = reinterpret_cast<U64>(alias);
    expect(wire.sourcePowerEvent != wire.sourceIdentityEvent, "fixture did not create distinct handle values");
    const DWORD before = publicationHandleCount();
    expect(!publicationRevocation.bind(publicationSources.owner, wire), "same object under different handles admitted");
    expect(publicationHandleCount() == before, "object-alias rejection leaked target duplicates");
    expectPublicationRejection(3, 0);
});
test("publication-revocation-process-event-alias-denied", [&] {
    reset(targets.at(0), false);
    auto wire = publicationSources.wire;
    wire.sourcePowerEvent = reinterpret_cast<U64>(publicationSources.owner);
    const DWORD before = publicationHandleCount();
    expect(!publicationRevocation.bind(publicationSources.owner, wire), "source event aliased the owner process");
    expect(publicationHandleCount() == before, "process-alias rejection leaked target duplicates");
    expectPublicationRejection(3, 0);
});
test("publication-revocation-ready-and-known-native-stamp-required", [&] {
    for (unsigned variant = 0; variant != 10; ++variant) {
        reset(targets.at(0), false);
        auto wire = publicationSources.wire;
        switch (variant) {
        case 0: wire.power.phase = 1; break;
        case 1: wire.power.phase = 2; break;
        case 2: wire.power.phase = 3; break;
        case 3: wire.power.flags = 0; break;
        case 4: wire.power.flags = ymcc::sleeppower::kKnown; break;
        case 5: wire.power.flags |= ymcc::sleeppower::kNotificationPending; break;
        case 6: wire.power.generation = 0; break;
        case 7: wire.power.size = 0; break;
        case 8: wire.power.version = 99; break;
        default: wire.power.phase = 99; break;
        }
        expect(!publicationRevocation.bind(publicationSources.owner, wire), "non-Ready/UNKNOWN Native stamp admitted");
        expectPublicationRejection(3, 0);
    }
});
test("publication-revocation-retire-returns-only-three-local-copies", [&] {
    reset(targets.at(0), false);
    const DWORD before = publicationHandleCount();
    expect(publicationRevocation.bind(publicationSources.owner, publicationSources.wire), "real bind failed");
    expect(publicationHandleCount() == before + 3, "target must own exactly owner/power/identity copies");
    expect(SetEvent(publicationSources.power) != FALSE, "source signal failed");
    expect(!publicationRevocation.current(), "cancelled target remained current");
    expect(publicationHandleCount() == before + 3, "cancellation returned handles before retire");
    publicationRevocation.retire();
    expect(publicationHandleCount() == before, "retire did not return all local copies");
    expect(WaitForSingleObject(publicationSources.power, 0) == WAIT_OBJECT_0 &&
           WaitForSingleObject(publicationSources.identityEvent, 0) == WAIT_TIMEOUT &&
           WaitForSingleObject(publicationSources.owner, 0) == WAIT_TIMEOUT,
           "retire closed/reset borrowed source objects");
    publicationRevocation.retire();
    expect(!publicationRevocation.current() && publicationHandleCount() == before, "retire was not permanent/idempotent");
    expect(!publicationRevocation.bind(publicationSources.owner, publicationSources.wire), "retired publisher rebound");
    expectPublicationRejection(3, 0);
});
test("publication-revocation-source-number-close-does-not-close-held-copies", [&] {
    reset(targets.at(0));
    expect(CloseHandle(publicationSources.power) != FALSE, "fixture could not close its source number");
    publicationSources.power = nullptr;
    expect(CloseHandle(publicationSources.identityEvent) != FALSE, "fixture could not close its identity source number");
    publicationSources.identityEvent = nullptr;
    expect(publicationRevocation.current(), "closing borrowed source numbers invalidated held objects");
    success();
    publicationRevocation.retire();
    expect(!publicationRevocation.current(), "retired held copies remained usable");
});
test("publication-revocation-retire-during-original-snapshot-denies-sdk", [&] {
    reset(targets.at(0));
    bool hookRan = false;
    snapshotReadHook = [&] { hookRan = true; publicationRevocation.retire(); };
    expectPublicationRejection(4, 1);
    expect(hookRan, "retire did not traverse original snapshot read port");
});

test("publication-revocation-actual-source-owner-exit-sticky-denies-sdk", [&] {
    reset(targets.at(0), false);
    const DWORD coldBefore = publicationHandleCount();
    {
        PublicationLeaseOwnerChild control;control.start();
        const auto controlPid=control.pid;const auto controlBirth=control.birth;
        expect(control.finish()&&control.returned&&control.exitCode==0,"no-publication child control did not return");
        expect(!publicationRevocation.current(),"cold process control created publication permission");
        publicationLeaseOwnerEvidence.push_back({{"controlOnlyNoPublication",true},{"pid",controlPid},{"birth",controlBirth},{"exitCode",control.exitCode},{"normalReturn",control.returned},{"coldParentHandlesBefore",coldBefore}});
    }
    const DWORD before = publicationHandleCount();
    publicationLeaseOwnerEvidence.back()["parentHandlesAfterControl"]=before;
    {
        PublicationLeaseOwnerChild owner;
        owner.start();
        HANDLE receiverPower = nullptr, receiverIdentity = nullptr;
        const auto closeReceiverTransport = [](void* value) {
            if (value && !CloseHandle(value)) publicationOwnerCleanupFailed = true;
        };
        expect(DuplicateHandle(GetCurrentProcess(), publicationSources.power, GetCurrentProcess(),
                               &receiverPower, SYNCHRONIZE, FALSE, 0) != FALSE,
               "power transport could not be copied into actual receiver");
        const std::unique_ptr<void, decltype(closeReceiverTransport)> ownedReceiverPower(receiverPower, closeReceiverTransport);
        expect(DuplicateHandle(GetCurrentProcess(), publicationSources.identityEvent, GetCurrentProcess(),
                               &receiverIdentity, SYNCHRONIZE, FALSE, 0) != FALSE,
               "identity transport could not be copied into actual receiver");
        const std::unique_ptr<void, decltype(closeReceiverTransport)> ownedReceiverIdentity(receiverIdentity, closeReceiverTransport);
        // Wire v2 borrows dedicated transport slots from this receiver's own
        // table. The real child's held process object remains the owner:
        // its normal exit revokes the publication independently of the events.
        auto wire = publicationSources.wire;
        expect(wire.version == 2, "owner-exit case requires receiver-local wire v2");
        wire.sourcePowerEvent = reinterpret_cast<U64>(ownedReceiverPower.get());
        wire.sourceIdentityEvent = reinterpret_cast<U64>(ownedReceiverIdentity.get());
        expect(publicationRevocation.bind(owner.process.hProcess, wire), "live remote actual owner was not admitted");
        expect(publicationRevocation.current(), "live remote lease unexpectedly unavailable");
        const U64 heldBirth = PublicationLeaseOwnerChild::birthKey(owner.process.hProcess);
        expect(heldBirth == owner.birth && GetProcessId(owner.process.hProcess) == owner.pid,
               "held child owner changed before release");
        expect(WaitForSingleObject(publicationSources.power, 0) == WAIT_TIMEOUT &&
               WaitForSingleObject(publicationSources.identityEvent, 0) == WAIT_TIMEOUT,
               "initial parent source events were not unsignaled");
        const DWORD actualPid = owner.pid;
        const U64 actualBirth = owner.birth;
        expect(owner.finish(), "actual lease owner did not normally exit and return resources");
        expect(owner.returned && owner.waitCode == WAIT_OBJECT_0 && owner.exitCode == 0,
               "owner exit was not a confirmed zero-code process return");
        expect(!publicationRevocation.current(), "actual source owner death failed to revoke publication");
        expectPublicationRejection(3, 0);
        for (unsigned retry = 0; retry != 3; ++retry) {
            expect(WaitForSingleObject(publicationSources.power, 0) == WAIT_TIMEOUT &&
                   WaitForSingleObject(publicationSources.identityEvent, 0) == WAIT_TIMEOUT,
                   "owner death closed or signaled the parent's source events");
            expect(ResetEvent(publicationSources.power) != FALSE && ResetEvent(publicationSources.identityEvent) != FALSE,
                   "parent source events were not retained after owner exit");
            expect(!publicationRevocation.current(), "reset source events revived dead owner's publication");
        }
        expect(!publicationRevocation.bind(publicationSources.owner, publicationSources.wire),
               "new live owner revived a consumed publication binding");
        expectPublicationRejection(3, 0);
        publicationLeaseOwnerEvidence.push_back({{"pid", actualPid}, {"birth", actualBirth},
            {"exitCode", owner.exitCode}, {"waitResult", owner.waitCode}, {"normalReturn", owner.returned},
            {"inheritedHandleCount", 1}, {"inheritedHandle", "release-event-only"},
            {"remoteSourceEvents", 0}, {"receiverTransportEvents", 2},
            {"parentSourcesUnsignaled", true}, {"publisherCalls", live.publisherCalls}});
        publicationRevocation.retire();
    }
    const DWORD after=publicationHandleCount();
    publicationLeaseOwnerEvidence.back()["parentHandlesBeforeActualLease"]=before;publicationLeaseOwnerEvidence.back()["parentHandlesAfterActualLease"]=after;
    expect(after == before, "owner-exit case leaked child/release/publication handles");
});
