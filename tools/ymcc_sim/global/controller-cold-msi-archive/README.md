# G3E MSI archived cold-start replay

Global adapter: `controller.cold-msi-archive`, profile `full`, scope `behavioral-mock`.

The entry calls the existing `tools/input_coordinator_lifecycle_selftest.mjs` twice, serially, with the fixed SDK-free managed fixture, the byte-exact archived field record and `--suite cold-msi-archive`. The first run must pass **14 cases / 545 checks**. The second adds only `--mutation rollback-cold-observed-id`; its harness RESULT must PASS while the fixture fails **14 cases / 86 checks / 14 exact behavioral assertions**, including three occurrences of `MSI opaque OS ID permits durable two-duty cleanup before saved ON creation`. A compiler failure, absent report, timeout or input drift always fails this wrapper.

```powershell
node tools/ymcc_sim/global/controller-cold-msi-archive/run.mjs --help
node tools/ymcc_sim/global/controller-cold-msi-archive/run.mjs --out G:\Evidence\cold-msi --compile-out G:\Evidence\cold-tu
```

Both paths must be fresh, absolute and distinct. Use a short compile path: the complete native translation unit produces long generated include paths. The global registration supplies a short sibling compile root. Each invocation has a 420-second bound; the global adapter retains its 900-second bound. No concurrent compiler is started by this entry.

The output contains INPUTS-BEFORE.json, RESULT.json and separate current/rollback command.json, stdout.log and stderr.log. Original runner RESULT.json, source-manifest.json, build.cmd/build.log, executable, snapshots, fixture-result.json and full-pipe-child receipts remain intact in the two compile directories; wrapper results reference their paths and hashes.

`field-record.json` is a byte-for-byte copy of the selected 1,287-byte target-return.json: SHA256 `e2390d9db29ec41ad5da1e3ff1ebd39edec0bdd9a106d3e76aef42042e36275a`. `provenance.json` records the ZIP SHA256 `3efa8b18687158f6d4dcb9611197a2835f2cc050b608043fc47bb4e0ab5c5480`, exact selected path and ZIP entry. Archived machine/PID/device identities are historical evidence; they do not describe current OS state. Original ZIP, selected record and extraction receipt are individually bound.

The fixed managed prerequisite is `G:\YeManCC-Work\Build\Validation\VirtualController-Lifecycle-20261019\integration\managed\env-return200-r4\bin\Release\net10.0\Fixture.dll` with its sibling fixture-manifest.json. The adapter declares every manifest source original and snapshot, generated input, runtime artifact, evaluated compiler input and tool allowlist entry as an individual artifactInput. It reuses the existing late-hide adapter's native source/tool inventory and explicitly adds this module and literal imported modules. Tool SHA mismatch, missing prerequisite or invalid binding gives a concrete blockedReason. Production source SHA is collected for each run; no old production-source admission baseline is imposed. Source/artifact bytes and membership must remain unchanged before and after both variants, including unrelated shared native changes.

Managed cleanup is mandatory. Positive runs currently produce 12 children. Native cases[].managedReceipts[] must prove naturalExit, controllerReleased, contextReleased and productionHostSession, and match each raw child PID/path/tuple. Raw children have status OBSERVED and exitObservationOwner native-parent-wait; their own naturalExitObserved is null. The wrapper verifies pipeLoopReturned, finalCleanupOk, released controller/context, production pipe/session, input bindings, hardwareIo false and realSdkLoaded false. The directed negative creates zero children; all 14 native case-boundary no-residual checks, the final no-spawn assertion, zero raw children and zero target-create observations are required. It cannot obtain cleanup credit merely from an empty receipt list.

The complete source TU, cold owner/session/queue and private native-to-managed pipe execute. Census, writer identity, ancestry, visibility and SDK/device endpoints remain controlled fixture leaves. Normal product Main, actual devices/drivers/services are not run. The wrapper never edits production sources. The original 13/503 PASS is historical. The later parent 14/545 positive and 14/86 semantic negative had top-level FAIL due to concurrent FxSound main.cpp drift; those do not constitute accepted mainline evidence.

This integration was validated with node syntax checks, --help and adapter import/build only. The parent performs the frozen-source formal two-compile run and reviews its reports.