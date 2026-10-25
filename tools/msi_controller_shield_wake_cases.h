// Included inside the production vendor-stack fixture's main(). No live hardware.
{
 auto prepareWake = [] {
     reset(); g_machineIdentity.family = YmccFamily::MsiClaw; seed();
     tasks[L"MSI_Center_M_Server"] = {}; tasks[L"MSI_Center_M_Updater"] = {};
     services[L"MSI Foundation Service"] = {};
     oemVendorStackShieldRequest(true); drain();
     g_powerLifecycle = PowerLifecycle::Resuming;
 };
 auto active = [] { return oemVendorStackShieldRuntimeSnapshot()["wakeGuardActive"].get<bool>(); };
 auto countState = [](const char* name) {
     return std::count_if(logs.begin(), logs.end(), [&](const auto& log) { return log.value("state", "") == name; });
 };

 reset(); g_machineIdentity.family = YmccFamily::MsiClaw; seed();
 oemVendorStackShieldRequest(false); drain();
 const auto disabledEpoch = g_oemVendorStackRequested;
 sgHandheldResumeInputOnWake("fixture-disabled");
 check(!active() && jobs.empty() && g_oemVendorStackRequested == disabledEpoch,
       "disabled shield never starts wake guard or queues extra work");
 check(wakeInputResumes == 1, "disabled shield leaves actual input-resume hook running");
 reset(); seed(); oemVendorStackRequest(true); drain();
 sgHandheldResumeInputOnWake("fixture-rog");
 check(!active() && jobs.empty() && wakeInputResumes == 1, "other OEM takeover and input remain unchanged on wake");

 prepareWake();
 const auto beforeWakeEpoch = g_oemVendorStackRequested;
 const auto wakeAt = tick;
 const auto taskWrites = taskMutations;
 const auto queriesBeforeWake = taskQueries;
 const auto originalJournal = journal;
 const auto inventoryReads = countState("msi-service-inventory");
 sgHandheldResumeInputOnWake("fixture-wake");
 check(active() && jobs.size() == 1 && g_oemVendorStackRequested > beforeWakeEpoch,
       "accepted wake forces a fresh pass even when shield was previously applied");
 check(wakeInputResumes == 1 && processCalls == 1, "wake resumes input immediately without draining queued shield work");
 const auto firstWakeEpoch = g_oemVendorStackRequested;
 const auto until = g_oemVendorShieldWake.until;
 tick += 300; sgHandheldResumeInputOnWake("fixture-duplicate");
 check(g_oemVendorStackRequested == firstWakeEpoch && g_oemVendorShieldWake.until == until && jobs.size() == 1,
       "duplicate wake cannot extend twenty seconds or duplicate queued pass");
 for (unsigned i = 0; i < 10; ++i) oemVendorStackSchedule();
 check(jobs.size() == 1, "busy serial queue never accumulates periodic shield jobs");
 drain();
 check(taskQueries - queriesBeforeWake == 2, "healthy wake pass needs exactly one read per task");
 check(active() && oemVendorStackShieldRuntimeSnapshot()["applied"] == true,
       "successful wake pass keeps guard active for relaunch protection");
 check(countState("msi-service-inventory") == inventoryReads && taskMutations == taskWrites && journal == originalJournal,
       "healthy periodic pass skips broad service inventory and avoids repeated task or journal writes");
 check(services[L"MSI Foundation Service"].state == SERVICE_RUNNING && stops == 0,
       "wake guard preserves MSI Foundation service");
 g_powerLifecycle = PowerLifecycle::Ready;
 check(active(), "resume-ready commit does not stop ongoing relaunch protection");
 const auto firstFinish = tick;
 tick = firstFinish + 1999; oemVendorStackSchedule();
 check(jobs.empty(), "no periodic execution before two seconds after completion");
 tick = firstFinish + 2000; oemVendorStackSchedule();
 check(jobs.size() == 1, "next periodic pass is due at two seconds");
 tasks[L"MSI_Center_M_Server"].enabled = true;
 tasks[L"MSI_Center_M_Server"].running = true;
 processes = {L"MSI_Center_M_Server", L"MSI_Center_M_Launcher", L"MCMOSDInfo"};
 drain();
 check(!tasks[L"MSI_Center_M_Server"].enabled && !tasks[L"MSI_Center_M_Server"].running && taskMutations == taskWrites + 1,
       "periodic pass disables a task re-enabled after wake");
 check(std::find(stoppedProcesses.begin(), stoppedProcesses.end(), L"MSI_Center_M_Launcher") != stoppedProcesses.end() &&
       std::find(stoppedProcesses.begin(), stoppedProcesses.end(), L"MCMOSDInfo") == stoppedProcesses.end(),
       "periodic pass stops relaunch of exact allowlisted processes and preserves telemetry");
 check(g_oemVendorTaskLeases.size() == 2 && journal == originalJournal,
       "repeated guard passes preserve original restore leases");
 tick = until - 1; oemVendorStackSchedule();
 check(active() && jobs.size() == 1, "last in-window poll can queue a pass");
 const auto processesBeforeExpiry = processCalls;
 tick = until; drain();
 check(!active() && jobs.empty() && processCalls == processesBeforeExpiry,
       "a queued pass expiring at twenty seconds performs no late task/process work");
 check(oemVendorStackShieldRuntimeSnapshot()["pending"] == false,
       "expired queue pass cannot leave status permanently pending");
 for (unsigned i = 0; i < 20; ++i) { tick += 400; oemVendorStackSchedule(); }
 check(jobs.empty() && countState("wake-guard-ended") == 1 && countState("wake-guard-started") == 1,
       "expired guard stays stopped without repeat logs or jobs");
 oemVendorStackShieldWakeBegin(currentPowerGeneration(), "late-duplicate");
 check(!active() && jobs.empty(), "late duplicate of same wake cannot reopen expired guard");
 oemVendorStackShieldRequest(false); drain();
 check(tasks[L"MSI_Center_M_Server"].enabled && tasks[L"MSI_Center_M_Server"].running,
       "disabling after guard expiry still restores original task state");

 prepareWake(); sgHandheldResumeInputOnWake("fixture-cadence");
 const auto cadenceUntil = g_oemVendorShieldWake.until;
 const auto callsAtStart = processCalls;
 drain();
 while (tick < cadenceUntil) { tick += 400; oemVendorStackSchedule(); if (!jobs.empty()) drain(); }
 check(processCalls - callsAtStart == 10 && !active() && jobs.empty(),
       "fast healthy guard performs ten serial passes over twenty seconds then stops");
 check(oemVendorStackShieldRuntimeSnapshot()["applied"] == true,
       "natural expiry preserves the last confirmed successful shield result");

 prepareWake(); processFail = true; sgHandheldResumeInputOnWake("fixture-failure"); drain();
 const auto failureUntil = g_oemVendorShieldWake.until;
 tick += 2000; oemVendorStackSchedule(); drain();
 tick += 2000; oemVendorStackSchedule(); drain();
 check(state("retry-exhausted") && active() && jobs.empty(),
       "failed attempt budget does not end twenty-second monitoring or spin the worker");
 const auto exhaustedEpoch = g_oemVendorStackRequested;
 processFail = false; tick += 2000; oemVendorStackSchedule(); drain();
 check(g_oemVendorStackRequested > exhaustedEpoch && oemVendorStackShieldRuntimeSnapshot()["applied"] == true,
       "guard retries after transient failure exhaustion and confirms recovery");
 check(g_oemVendorShieldWake.until == failureUntil, "failure recovery never extends the original wake window");

 prepareWake(); queueFail = true; sgHandheldResumeInputOnWake("fixture-queue-full");
 const auto rejectedEpoch = g_oemVendorStackRequested;
 for (unsigned i = 0; i < 5; ++i) { tick += 400; oemVendorStackSchedule(); }
 check(active() && jobs.empty() && g_oemVendorStackRequested == rejectedEpoch && !g_oemVendorStackOpQueued,
       "rejected queue publication retries same request without creating extra work");
 queueFail = false; oemVendorStackSchedule(); drain();
 check(oemVendorStackShieldRuntimeSnapshot()["applied"] == true,
       "guard recovers after queue capacity returns within its window");
 prepareWake(); queueFail = true; sgHandheldResumeInputOnWake("fixture-queue-expired");
 tick = g_oemVendorShieldWake.until; oemVendorStackSchedule();
 queueFail = false; oemVendorStackSchedule();
 check(!active() && jobs.empty(), "queue rejection beyond deadline cannot revive expired guard");

 prepareWake(); sgHandheldResumeInputOnWake("fixture-disable-queued");
 const auto callsBeforeOff = processCalls;
 oemVendorStackShieldRequest(false);
 check(!active() && jobs.size() == 1, "turning shield off immediately cancels active guard without queue overlap");
 drain();
 check(processCalls == callsBeforeOff && tasks[L"MSI_Center_M_Server"].enabled,
       "queued wake work consumes newest off intent and restores rather than reapplying shield");
 tick += 22000; oemVendorStackSchedule();
 check(jobs.empty(), "off intent cannot be undone by delayed guard poll");

 prepareWake(); sgHandheldResumeInputOnWake("fixture-suspend");
 const auto suspendCalls = processCalls;
 g_powerLifecycle = PowerLifecycle::Suspending;
 check(!active(), "suspend immediately invalidates guard authority");
 drain();
 check(processCalls == suspendCalls && jobs.empty(), "queued wake pass cannot run during a new suspend");
 ++g_powerGeneration; g_powerLifecycle = PowerLifecycle::Resuming;
 sgHandheldResumeInputOnWake("fixture-next-wake");
 check(active() && jobs.size() == 1 && g_oemVendorShieldWake.generation == currentPowerGeneration(),
       "next power generation starts a fresh twenty-second guard");
 drain();
 const auto newUntil = g_oemVendorShieldWake.until;
 oemVendorStackShieldWakeBegin(currentPowerGeneration() - 1, "stale-generation");
 check(jobs.empty() && g_oemVendorShieldWake.until == newUntil, "stale wake cannot replace current generation");

 prepareWake(); sgHandheldResumeInputOnWake("fixture-generation-changed");
 const auto generationCalls = processCalls;
 ++g_powerGeneration;
 drain();
 check(!active() && processCalls == generationCalls, "generation change invalidates queued work even without a suspend phase");
 prepareWake(); sgHandheldResumeInputOnWake("fixture-exit");
 g_exitRequested = true; const auto exitCalls = processCalls; drain();
 check(!active() && jobs.empty() && processCalls == exitCalls,
       "shutdown prevents pending guard work from applying or retrying");
 oemVendorStackRestore("fixture-exit");
 check(tasks[L"MSI_Center_M_Server"].enabled && g_oemVendorTaskLeases.empty(),
       "normal shutdown still restores the original lease after guard cancellation");

 prepareWake();
 tasks[L"MSI_Center_M_Server"].enabled = true;
 tasks[L"MSI_Center_M_Server"].running = true;
 tasks[L"MSI_Center_M_Server"].stuck = true;
 sgHandheldResumeInputOnWake("fixture-slow-task");
 const auto slowUntil = g_oemVendorShieldWake.until;
 drain();
 check(tick >= 2001 && g_oemVendorShieldWake.next == tick + 2000 && jobs.empty(),
       "slow task readback adds a full idle interval after completion without catch-up bursts");
 tick = slowUntil - 20; oemVendorStackSchedule(); drain();
 check(!active() && jobs.empty() && tick <= slowUntil + 50,
       "stuck task poll notices window expiry and stops instead of exhausting repeated waits");

 prepareWake();
 // Start a pass just as a settings change happens inside persistence.
 tasks[L"MSI_Center_M_Server"] = {}; g_oemVendorTaskLeases.clear();
 onPersist = [] { oemVendorStackShieldRequest(false); };
 sgHandheldResumeInputOnWake("fixture-off-during-pass"); drain();
 check(!active() && !oemVendorStackShieldIsWanted() && jobs.empty() && tasks[L"MSI_Center_M_Server"].enabled,
       "off during active pass invalidates mutation authority and stale success cannot override restore");
 prepareWake();
 tasks[L"MSI_Center_M_Server"] = {}; g_oemVendorTaskLeases.clear();
 onPersist = [] {
     ++g_powerGeneration;
     sgHandheldResumeInputOnWake("fixture-new-wake-mid-pass");
 };
 sgHandheldResumeInputOnWake("fixture-old-wake-mid-pass"); drain();
 check(active() && g_oemVendorShieldWake.generation == currentPowerGeneration() &&
       oemVendorStackShieldRuntimeSnapshot()["applied"] == true && jobs.empty(),
       "new wake during old work wins and gets exactly one fresh successful pass");
 check(countState("wake-guard-started") == 2, "two actual generations produce two start receipts");
}
