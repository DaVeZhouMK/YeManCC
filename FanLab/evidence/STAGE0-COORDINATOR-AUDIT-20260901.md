# Stage 0 Baseline and Stage 1 Fan-Gate Audit

Date: 2026-09-01

## Scope

Stage 0 established a pure coordinator contract. Stage 1 connects only the
fan admission gate to the established FanHost lifecycle. It does not load HC,
create a power listener, create a process, or perform a hardware write from
the coordinator.

The sole active HC source baseline is:

- Root: `deps/handheldcompanion-runtime/source`
- ID: `HC-BINARY-BATCH12-20260831`
- Source-files index SHA-256:
  `D1CC40D8587117C2D4C8FA703D3228D5AE1CCF2034C5C065F5CD822A628421E4`

All C-drive, migration, and R4 references are historical evidence only and are
not valid forward build or comparison inputs.

## Result

`FanCoordinatorGate` in `src/bridge/hardwareCoordinator.ts` is now the
fan-only outer admission contract. It tracks the authoritative native power
generation, the current FanHost session, HC-ready admission, and idempotent
fan-write requests. It has no HC, Windows power, process, or hardware
dependency.

The current mainline FanHost route remains the sole fan lifecycle owner:
`Open -> OpenEvents -> lease -> HC-ready -> write admission -> Enable`.
The new gate observes the existing native power generation; it does not issue
HC `Open`, `Close`, OEM restore, EC/ACPI/HID calls, or retries.

The coordinator must not be promoted to an HC lifecycle replacement. Original
HC coordinates its single `CurrentDevice` and manager graph inside one process;
the Stage 0 model's resources, epochs, release proof, and command admission
are YMCC coordination concepts rather than HC mechanisms.

## Explicit Constraints

1. Do not instantiate or route `HardwareCoordinatorPlan` in the current fan
   path. In its default owner mode it deliberately rejects platform-routed
   commands.
2. Do not enable the `input-host-placeholder` participant. Controller, motion,
   virtual gamepad, and HidHide remain reserved names only; no input runtime is
   authorized.
3. `OwnerReleaseProof` is not proof of a physical OEM handoff or complete HC
   manager disposal. It is insufficient for production owner switching until a
   future authoritative native lifecycle and measured cleanup evidence exist.
4. The only approved behavioral difference from HC remains the HWiNFO shared
   memory temperature source. No coordinator feature expands that exception.
5. The coordinator consumes the existing authoritative native power generation
   through the root power event path. It must never create a second
   suspend/resume listener or retry loop. The existing resident 10-second
   FanHost guard remains the only automatic recovery mechanism.

## Evidence

- `test:hardware-coordinator`: passed using the controlled Node runtime.
- `test:fan-host-lifecycle`: passed, including current-session write admission,
  suspend gate closure, and post-resume session re-admission.
- `test:fan-sleep-extreme`: passed, 18 source/model scenarios; hardware writes
  disabled.
- `fan_exit_cleanup_selftest.ps1`: passed.
- `fan_host_parent_watchdog_selftest.ps1`: passed using the verified current
  `PowerControl\\fan-host` payload in safe no-hardware mode.
- `fan_payload_selftest.ps1`: passed. The frontend now verifies that
  `YeManFanHost.payload.json` is the sole runtime dependency authority rather
  than retaining a second DLL list.
- `test:hc-source-baseline`: passed, 1048 files, HC `0.32.3.2`.
- `test:fanhost-source-baseline`: passed, 22 source files.
- `fan_hc_lifecycle_deep_selftest.ps1`: source-order pass with hardware writes
  disabled; output in `G:\YeManCC-Work\Mainline\Build\Validation\HC-Parity\T1-Lifecycle-Coordinator-Audit-20260901`.
- `fan_hc_lifecycle_order_audit.ps1`: source order confirmed. The known limits
  are full HC ManagerFactory ownership and universal physical OEM acknowledgement;
  neither is claimed as complete.
- `fan_t4_hc_parity_audit.ps1`: 20 checks passed. Its only remaining entries
  are explicit architectural boundaries, not unreviewed runtime differences:
  full HC manager graph is intentionally not initialized by FanHost; universal
  physical OEM acknowledgement requires device evidence; and the YeMan lease
  is a safety boundary with no direct HC counterpart.
- `fan_hc_profile_events_full_audit.ps1`: 17 checks passed, 0 failed. Its six
  remaining entries are explicitly documented fan-only/HC-manager boundaries;
  none is silently treated as a completed HC equivalence claim.
- Standalone test package: `Build\\TestPackages\\FanCoordinator-20260901-203802\\YeManCC-FanCoordinator-Test.zip`.
  It includes the complete verified `PowerControl\\fan-host` payload and was
  revalidated from the ZIP against its manifest. ZIP SHA-256:
  `337A8FC2103A7D0BB5FF753BCC4D8B1AC8E1568F6AB4C529E16519E3E32A9023`.

## Future Entry Criteria

Any input/gyro participant requires a separate approved contract, a controlled
source baseline, and hardware evidence. It must not share HC managers, HidHide,
virtual-controller state, device sessions, or lifecycle ownership with FanHost.
