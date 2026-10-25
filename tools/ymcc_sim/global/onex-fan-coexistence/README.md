# OneX fan / lighting coexistence

This existing `ymcc_sim` domain executes the exact pre-fix Host7f and helper e546, then a candidate Host, against a shared in-memory exclusive COM port. It preserves complete source-derived `IDevice.Open`, `IDevice.OpenLibSys`, `OneXAOKZOE.Open`, `OneXPlayerX1.Open`, both inherited fan methods, and `ECRamDirectWriteByte`. The probe checks all seven methods' opcode and call sequences against the exact HC PE before running the original failure cases.

Only X1AMD, X1Intel, X1Pro and the actual nested `HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex` are admitted by the new fan lifecycle. Mini/Fly/G1 are not silently included. Unknown derived classes, altered EC data and changed method owners are rejected.

The old software counterexamples cover fan-then-lighting COM contention, reverse-order busy fallback, and failed OLS initialization being accepted on a later Open. Candidate cases execute actual Host capability admission, `OpenHcDevice`, `OpenEventsBeginCore`, `OpenEventsFinalizeCore`, and `CloseHcDevice`, including Host profile delegate cleanup. Complete inherited fan/EC methods run against inert port terminals. Actual HC factory/constructors/managers, native OLS and device IO are not run.

```text
node tools/ymcc_sim/global/onex-fan-coexistence/run.mjs --evidence-dir <fresh directory>

node tools/ymcc_sim/global/onex-fan-coexistence/run.mjs --evidence-dir <fresh directory> --candidate-host-root <exact existing candidate payload> --hc-assembly <exact reviewed HC DLL> --cecil-path <local approved Cecil DLL> --legacy-root <exact old three-DLL directory>

node --test tools/ymcc_sim/global/onex-fan-coexistence/entry.test.mjs
```

All five CLI flags are strict: unknown, duplicate, empty and missing values fail before evidence creation. Existing evidence directories cannot be overwritten. Missing baseline, HC, Cecil, dotnet or explicit candidate inputs are `BLOCKED`, exit 3; incorrect identities or behavior are `FAIL`, exit 2. The pure static `getOneXFanCoexistenceAdapters` descriptor opts into blocked exit 3 and is available for parent registration.

The three pre-fix DLLs and provenance are hard-pinned in the repository. Cecil discovery reuses the approved `fan-open-signals` resolver and existing repository tool hint; no historical paths, installed directory or network downloads are required. The exact baseline DLLs/provenance are source inputs as well as external hash inputs. Explicit candidate files are captured before and after execution. A supplied candidate passing behavior is not proof of installation or of a build from current sources.

The lifecycle owns only its successfully initialized OLS transport and its DeviceOpen marker, rejects foreign resources, leaves LED COM/HID alone, and retains cleanup debt after failure. It checks the original EC write method's bool for automatic return because the inherited void `SetFanControl` drops false. Port-write exceptions, false returns, Exit errors and Dispose errors remain distinct. A software true is not physical acknowledgement or fan RPM proof.

A passing run produces a separate complete `host-payload` for later integration. It does not install, start Host Main, touch the running app, write a real device, or put the OS to sleep. The parent simulator owns global registration and overall receipt auditing.


The fifth repair iteration also pins prior source-built Host `a6788164…` and its actual Backend `6d2bcbd4…` in `fixtures/host-a678-write-failure/`. The installed Backend `f8…` is a different artifact and cannot replace that pair. Six original consumer failures must reproduce: cold/cached duty, Software/Hardware profile writes, temperature false target publication and missed same-temperature retry. Nine original duty-conversion goldens remain mandatory.

Candidate strict control/duty dispatch consumes the original bool-returning EC method before any legacy delegate cache. Tests cover actual Host profile and temperature consumers, wrong/stale/duplicate samples, source detachment, target/cache clearing, same-temperature recovery, return debt, and the existing engine's single recovery timer, 30-attempt cap, 60-second cap and retired-owner rejection. No new recovery system or physical acknowledgement is introduced. Recovery-engine tests use a synthetic existing window and isolated logs, without opening the production recovery ledger.


## Inherited HC EC write receipts (2026-10-20)

The same registered regression now also executes the actual Host duty/profile and temperature-to-engine consumers for the exact inherited IDevice SetFanControl/SetFanDuty pair. The original methods and bool-returning ECRamDirectWriteByte are extracted from frozen HC source; actual HC PE checks verify that the original void setters discard that bool. Source CEc fan capability metadata supplies the known model's admission bit. The original 69 OneX cases are preserved.

Current Host binds a private per-device EC writer only when both setters belong to IDevice. It consumes the existing original EC bool before a legacy cached delegate, preserves each device's current EC addresses, minimum/maximum and Convert.ToByte rounding, and refuses metadata/readiness/owner/suspend changes. Reflection and compiled delegate creation happen only when binding the device; no new poll, driver, factory, OpenEvents handler or physical route is added. Vendor overrides retain their existing consumers. Existing lifecycle/return ownership is unchanged. This does not complete AYANEO Flip DS secondary-screen capability integration or its mixed-settings lifecycle review.

Four original failures were reproduced against the source-built pre-repair Host: cold/cached duty and software/hardware profile dispatch accepted low-level false. The current regression adds 15 inherited-EC scenarios, including unchanged scaling, known-model capability admission, metadata and owner drift, vendor fallback, and real temperature failure delivery into the existing FaultLocked/recovery flow.

Every run also mutates only the bool-result branch of the exact tested Host DLL inside its private evidence directory. The original EC call and post-write owner check remain. Cecil verifies that exactly one method changed and no methods were added/removed. The same candidate consumer harness must then produce exactly five named business failures (four rejected writes and temperature error delivery), with zero native attempts. Build/start/other failures cannot count as detection. Mutated DLL, proof, raw result and logs are explicit hashed evidence; no source or shipping binary is patched by the counterexample.

The tested candidate payload, compiled consumer/fixture assemblies, private-fault payload, mutation proof, original positive/fault results and logs are also paired own-case hashes in sourceHashes (with explicit fixture-output meaning), so the existing formal receipt auditor can reject changed or missing proof bytes without a new audit policy.

The bound writer also retains only the original existing OLS reference, checks that identity before each write, and never opens or disposes it. Missing/replaced static transport with a stale DeviceOpen marker is rejected before the inherited EC method can silently skip nullable writes or use the wrong transport. Its disposal/return remains with the existing lifecycle owner.


## Complete original virtual Close consumers (2026-10-20)

The fixture now extracts complete IDevice, AYANEODevice, AYANEODeviceCEc and AYANEOFlipDS Close bodies. Full normalized PE opcodes, all method/field references, exception-handler kinds and actual inheritance are compared against the pinned real HC DLL. The original AYANEO power/session subscriptions are included, with explicit seeded event preconditions. The RGB operation, managers, events and OLS leaf remain inert fixture terminals; constructors, mixed-settings OpenEvents and secondary-screen actions are not executed.

Eight additional cases run original virtual Close through actual Host cleanup and existing engine/public state consumers. They cover source ordering, all seeded unsubscriptions, swallowed EC release false on suspend and process exit, honest no-write history, RGB/OLS disposal failures and a late Closed-event failure. The existing lifecycle thread is seeded as an explicit entry precondition; dispatcher bootstrap is not covered. Native attempts must remain zero. No production behavior change is needed when current consumers already retain unknown release responsibility.

Every run additionally mutates only the exact tested Host's CloseHcSessionForLifecycle release-safety verdict, preserving original virtual Close and cleanup. The same candidate consumer harness must fail exactly the two write-history unresolved-release cases with exit 2 and zero native attempts; no other failures, compile/start errors or timeouts count as detection. The compiled private payload, raw results, proof and logs are protected by the existing paired own-artifact hashes. The five inherited-write counterexamples remain mandatory as a separate fault copy.


## Original EC lifecycle identity (2026-10-20)

Twenty-two further cases execute the original CEc Open and the complete original Close through the actual Host. The existing inherited-EC route now retains the admitted device/transport observation across temperature teardown, rejects a foreign session before original Open acquires cleanup debt, and checks identity before and after original Close. The original virtual methods remain the sole open/dispose owners. A lifecycle-scoped writer is no longer rebound by monitor/profile cache teardown. The EC metadata guard also precedes the mixed original Close. Open/write/close boundaries use one root-type gate within this inherited route; no new timer, factory, driver, polling or runtime component is added. Vendor setter overrides and non-fan/unadmitted routes retain their previous dispatch. A changed route cannot abandon pending inherited-session debt; verified retirement can yield to its next original vendor lifecycle.

Cases include cold and primed foreign Close, replaced device, missing/replaced transport, same-owner retry, failed original initialization, real close/reopen, concurrent and reentrant Open, drifted release address, late Closed callback, and real manual OEM restore after monitor detach. Actual public engine snapshots retain ownership failures. Manual restore uses the complete extracted original PowerProfileManager.CloneProfile with the unchanged existing shared Newtonsoft.Json.dll; source and compiled fixture identity are hashed, original clone opcodes/calls match the pinned HC PE, and the actual loaded Newtonsoft bytes must match the locked 13.0.4 net6.0 NuGet original. Both runner and compiled-output/runtime checks reject any new trim or rewrite. RGB/system terminals remain inert.

A third mandatory private payload changes exactly two session identity guards plus the original temperature-detach identity fields. It preserves actual Open/Close, EC packets, disposal, results and the existing release-safety predicate. The same current consumer harness preserves the twelve original resource/admission/restore counterexamples and also refuses the new foreign-identity OpenEvents preflight (thirteen exact failures) with exit 2 and zero native attempts, in addition to the separate five-write and two-release compiled controls. Other failures, compile errors and timeout are not valid detection. All three private payloads/proofs/results/logs use the existing paired output hashes. An arbitrary external mutation inside a raw EC port sequence is not a proven atomic hardware-ownership contract; the existing lifecycle and physical integration obligations remain.


## OpenEvents immutable admission and rollback (2026-10-20)

The existing public backend OpenEvents now carries one immutable preparation through its original begin/wait/finalize phases. Device, power/close epoch, profile generation, authorization and current readiness are revalidated before native event entry and before/after profile subscription. The original engine gate and async Device_Inserted completion mechanism remain. A power-superseded event call leaves device cleanup with the announced power owner and records the cause; a non-power generation failure still uses existing recovery. Both paths are executed, so cancelling one cannot silently disable all recovery. The final phase consumes the captured generation, so a late old phase cannot activate or publish a newer session. Each new registration retains its exact manager/delegates/generation; rollback removes only those handlers, invalidates only the matching generation and retains failed-removal debt for the existing Close owner.

Twenty-eight new cases run the actual preparation/finalization and public backend entry on the OneX and inherited EC routes, queue real profile callback work, apply power notifications during both add boundaries, preserve a newer generation across rollback, retry registration cleanup through Close, and route the actual error through the existing engine recovery/public SnapshotForRead. The actual public insertion/wait/finalize branch also runs on an explicit inert insertion terminal, with a preserved admission-generation record. It does not execute original ROG HID lifecycle bodies. No HTTP listener or Host Main is started. The generic original mixed OpenEvents body remains an explicit fixture leaf; this does not claim its settings/battery/controller subfunctions have all been executed. The existing lifecycle thread is a fixture precondition.

A fourth mandatory private payload changes exactly the tested RequireOpenEventsAdmissionCurrent method to return. Actual prepare/finalize, profile-generation activation, handler attachment/rollback, original virtual lifecycle and feedback still execute. The same 142-case harness must fail exactly twenty stale-admission business assertions with exit 2 and zero native attempts. Generation-mismatch cases that remain blocked by the independent activation check are preserved as honest controls. All prior five-write, two-release and thirteen-current-ownership counterexamples remain in separate exact payloads. Outputs use the existing paired hash audit.

The same domain run also invokes the existing fan-global wrapper's selected control-self-heal suite unchanged as a runner. Its real FanHostLifecycle cases now include both new Host error codes across five existing model labels. The bridge must perform bounded observe-only reconciliation, with no immediate event/curve replay, then retain a reachable explicit manual request. The ten original policy failures are preserved before repair; the expanded suite has 598 cases. This selected child suite is not the complete five-suite fan-global acceptance. Its exact loaded inputs and owned artifacts are merged into this case's current-source/paired-output proof, without changing the shared runner or central registration.

## Compiled initialization and current source contracts (2026-10-20)

Nine additional cases locate the actual device.initialize delegate wired by the candidate's compiled LoadAssemblyAndFactory, reconstruct only its compiler-generated closure's captured backend, and execute that original delegate through the actual RunHcInitializationStage. The callback may read only that backend field; unknown captured locals are refused. An observer proves one virtual Initialize(false,false) dispatch. Complete original IDevice, FlipDS and Flip1SDS initialization bodies are extracted and matched against the pinned HC PE (opcodes, calls, fields and exception regions); FirstStart=false performs no settings or transport action. Explicit FirstStart=true fixture controls prove the original branches remain observable through inert SettingsManager terminals. Original failure chains and throwing diagnostics are checked without replay. This executes the wired delegate and stage, not full factory validation, assembly loading, motherboard detection, sensors or device constructors.

A fifth independent compiled private payload changes exactly one FirstStart bool in that actual callback. The same 151-case harness must fail exactly the three argument/FlipDS/Flip1SDS assertions with exit 2 and zero native attempts. The existing five write, two release, thirteen session and twenty OpenEvents fault detections remain unchanged.

The same domain now runs the three existing read-only source audits, then eleven private source counterexamples. The audits follow the identity-bound virtual Close helper, exact result-aware inherited EC consumer, current typed timeout/temperature receipts and terminal resume read refusal. Missing virtual Close, post-Close identity, manager confirmation, initialization arguments, bool refusal or callback/refusal identity must fail at the named existing assertion. These source controls are recorded separately from compiled runtime proof and do not claim full ManagerFactory, physical acknowledgement or UI release acceptance. No shared runner or central registration is changed.

## Complete original mixed lifecycle and manager flags (2026-10-20)

The existing fixture now executes the complete original IDevice, AYANEO, CEc and FlipDS OpenEvents chain, query methods and settings/device/power/input callbacks: 22 full methods plus the original ManagerStatus enum and Flags attribute. Opcode, call, field and exception-region comparisons bind each method to the pinned HC PE. The original 151 cases remain; 41 added cases cover ready/deferred manager combinations, late initialization, PnP ID filtering, mapped/unchanged OEM input, settings-to-screen and battery threshold branches, late post-Close events, partial exceptions, reentry, retry and manager cleanup. Settings, PnP, power-event publishers, button state operations, LED/screen actions and transport operations are explicit inert terminals. Actual manager construction, hardware effects, OS events and lifecycle-thread bootstrap remain separate.

The real Host now retains raw OpenEvents entry until its original Close returns and candidate cleanup settles. A superseded Host publication cannot repeat original raw subscriptions; a partially throwing raw entry requires same-owner Close before retry. Exact raw device identity stays live, and only that closing device may retire the record. ManagerStatus flag names are consumed individually without token allocations, so Initialized/Busy and halting/halted combinations cannot become false clean-manager receipts. No new recovery owner, timer, driver, product API or manager Start/Stop is introduced.

Two additional independent compiled faults mutate exactly the raw-entry once guard and the manager flag consumer. Each runs the same two reentry or eight manager-flag backend/engine/public-snapshot consumers on the unchanged current harness, preserves actual raw methods/Close, and must fail the named business assertions with exit 2 and zero native attempts. The five older full-harness faults continue; the original twenty event-admission failures remain and the new mixed generation case adds one, so its exact count is now twenty-one. Source-contract counterexamples are separate from compiled consumer proof. Historical setup failures and the raw subscription residue are retained in round8-mixed-events evidence; they are not final acceptance.


### Factory initialization readiness and retry ownership

The same runner now separately executes 24 actual compiled factory consumers:
automatic/diagnostic construction, sensor/Initialize/identity failures, same-device
continuation, completed bootstrap-stage caching, authorization and write admission,
read-only sampling, disposal/reentry, exact object identity and owned ALC resolver
registration/removal. IsLoaded requires the exact completed factory owner; failed
stages cannot publish ready, and retry does not replay normally returned stages.
The existing 192-case candidate suite still executes the unmodified production
binary. No production validation gate is bypassed or weakened.

For only the new orchestration suite, a copied candidate has exactly five I/O leaf
bodies bound to explicit inert ports: ACL validation, deployment generation, native
lookup validation, runtime/log configuration and dependency lookup. All method
instructions, locals and exception-handler boundaries are compared; factory, stage
handling, readiness, handshake, write/sampling gates and Dispose remain unchanged.
Fixture motherboard/device constructors, sensor reads and Initialize terminals do
not prove the corresponding production algorithms or hardware initialization.

Two independent compiled counterexamples change only get_IsLoaded or the four
completed-stage guards in LoadAssemblyAndFactory. Each executes the same full24
case set after the identical five-leaf binding, and must produce exactly15 or7
named business failures with exit2 and zero native attempts. Earlier seven
compiled faults, source audits and bridge consumers continue unchanged. Factory
proof, copied binaries, logs and hashes are included in the existing receipt's
protected evidence; this is not whole-program or physical acceptance.


### Public STA bootstrap, disposal and abandoned queue ownership

The existing runner also executes16 actual public-handshake/STA consumers. They
start the real foreground STA and WPF Dispatcher, run original public handshake
and work admission, and verify retry/reentry, concurrent cold start, Start failure,
normal/self/early disposal, an ended pump, and an admitted but unexecuted request
when shutdown abandons its callback. Every owned test thread must naturally exit
or be explicitly shut down by fixture cleanup; original behavior failures retain
their exact terminal results. Five independent one-method faults must fail exactly
1/1/1/2/1 named consumers using the same16-case set. Process timeout, malformed IL
or compilation failure is never accepted as fault detection.

The copied image uses the previous five explicit I/O leaves plus a read-only OS
thread-ID port calling actual kernel32.GetCurrentThreadId. One three-instruction
scheduling probe precedes the retained original Thread.Start; removing it restores
all normalized startup instructions, locals and exception boundaries. Short/long
branch encodings are normalized and widened safely after IL insertion. Public
handshake, real thread/Dispatcher and all other lifecycle logic execute unchanged
relative to that candidate/fault. This suite does not prove production deployment
validation, original device/sensor algorithms, installed HostMain or hardware I/O.


### Actual deployment, manifest binding and private capability ACL (2026-10-20)

The existing runner separately executes 26 unmodified static validation cases,
53 manifest/cache/factory-refusal cases and four genuine current byte cases.
Only the AppContext.BaseDirectory getter operands in two actual consumers are
redirected to owned Build roots; restoring those operands must restore every
instruction, local and exception boundary. No validation gate is replaced.
The Host/runtime schema, exact runtime ID and resolved relative manifest
reference are checked before admitting a matching deployment record. The five
required Host declarations include YeManFanBackend.dll; missing both that file
and its manifest entry is refused by static, cold, cached and actual factory
consumers before assembly/native setup. Matching records retain the user's
explicit no-per-DLL-rehash policy.

Three one-method compiled faults must produce exactly 23 binding, 10 cached
admission and five ACL business failures in the same 53 cases. The original
pre-round11 image is also replayed with the final harness to retain its actual
failures. Private ACL checks use in-memory security descriptors; they do not
change ACLs or execute the formal AppData wrapper. Native refusal uses only
missing or inert non-loadable fixture bytes. Genuine current Host/runtime/shared
copies exercise full file validation and cache admission, then force GC to prove
the owned test load context survives lazy dependency resolution. No Host Main,
genuine device initialization, native hardware DLL or installed process runs.

Payload staging copies exactly the current manifest declarations, requires all
binary/config members from the current build, and uses template support scripts
only from the existing declared list. Missing declarations are errors; undeclared
SDK/PDB build outputs remain in their original build directory and are not
silently added to the test candidate. Staging receipts and genuine source pins
join the original evidence protections.


### Installer required-backend consumer (2026-10-20)

The same runner now executes nine cases against the actual unique installer
required-file foreach AST extent, with inert declaration maps. The preserved
legacy7f script (exact 5a6d68 SHA) and a private current-script copy omitting only
YeManFanBackend.dll must each fail exactly the missing-backend case. No full
installer, administrator/process probe, deployment, state write or ACL change
executes. The whole script/asset/manifest/native/verifier integrity chain is
separately checked by existing upgrade and actual PackageSource consumers.


The output gate also accepts the exact `installer-admission/positive`,
`original`, and `fault` children of a verified running original parent
evidence directory. It binds the owner and prepared-manifest hashes, repository
and evidence roots, the current fixture source pin, and the live Node owner
process. A terminal parent receipt, mismatched binding, or reparse path rejects
before output creation. Legacy workspace Build roots retain their original use.
The process read belongs to fixture output admission; the installer itself still
does not run.

Run `node --test tools/ymcc_sim/global/onex-fan-coexistence/installer-output-root.test.mjs`
for the 14 path-admission tests, including the unchanged three-role installer
consumer and its exact missing-backend failures. These standalone tests do not
replace the complete parent run against the updated source snapshot.


### Formal capability ACL wrapper and real Windows descriptors (2026-10-20)

The same public runner separately executes 35 actual compiled formal-wrapper
and factory-boundary cases (260 checks). Exactly one Environment.GetFolderPath
operand in ValidateFormalSessionCapabilityAcl is redirected to fresh Build
roots. Restoring that operand must restore every original instruction, local
and exception region. Real DirectoryInfo/FileInfo ACL reads and the original
private predicate execute; factory, stage diagnostics and refusal stay original.

Cases cover actual owner/System/Admin grants, directory and file untrusted
read/write/permission grants, inherited grants, missing/directory token paths,
actual denied READ_CONTROL exceptions, case/normalized formal paths and the
preserved blank/external diagnostic scope. Inert file bytes are not valid tokens.
No existing user/installed ACL or genuine AppData path is changed. Each run
creates its own descriptors and restores all four unreadable fixture DACLs with
the system DACL-only API, then reads the original DACLs back. Directory and file
managed setters can require the denied access; restoration never elevates,
changes owner/SACL, or touches another path. Full installed launcher/token parsing,
valid native/device initialization and physical acceptance remain separate.

Four one-method compiled faults execute the same 35 cases and must fail exactly
24/6/8/2 named business assertions. Compiler/setup/collection errors are not
counterexamples. All new files, original getter proof, private image identities,
actual Windows ACL outputs and preserved results join existing source/evidence
protections. Earlier deployment-only suites still truthfully report that they
did not execute this wrapper; this separate suite supplies that software proof.


## Original device/bootstrap algorithms (round14)

`bootstrap-proof.mjs` runs the same existing Host factory with the prior five
explicit IO leaves, but its motherboard, device selection, sensor discovery,
base Initialize(false,false) and identity data now come from original algorithms.
`BootstrapFixture` has a distinct assembly identity and is reflection-only: it
cannot replace the legacy fixture's HandheldCompanion type binding. Its complete
source methods and all constructor namespace mappings are recorded. Five known
source-compilation differences are retained in before evidence; the fixture then
binds complete pinned HC PE bodies without rewriting instructions. All42 methods,
used callbacks, fields, locals, branch targets and EH must compare exactly after
only delegate-site-bound display-class naming. No original HC assembly reference
may remain in the bound fixture.

WMI, gyro/accelerometer/serial discovery, USB metadata, logging/telemetry and every
model constructor are explicit inert terminals. Windows sensor equality operator
signatures remain exact. Constructors start with zero capabilities, and SuiPlay
refuses execution. No inactive capability or device hardware is enabled.

The53 consumers cover20 model selections, cached identity, actual original JSON
cache/load/query/write behavior, invalid/missing cache and WMI errors, sensor
names/normalization/precedence/flag changes/events, and actual Host factory cache,
identity, unsupported-sensor warning and retry/readiness consumers. Three compiled
single-method faults must fail exactly2/2/4 business cases in the identical set.
The42 admitted PE methods are structurally bound; this does not claim every
unused motherboard getter was exercised. Original model constructor bodies,
valid native initialization and installed launcher/token parsing remain separate
unproven obligations for the eight enabled capabilities.


## Original admitted constructors and native motion ownership (round15)

`originalConstructorProof` executes the pinned HC instance-constructor bodies for
all79 currently selected factory models except inactive SuiPlay, plus7 firmware
and CPU-identity variations. The same89-case set includes the actual compiled
Host ValidateGamepadMotion for valid/missing/invalid files. Every original device
instance constructor and the managed GamepadMotion/native ABI remain unchanged.
A whole-method comparison admits exactly seven declared IO/composition leaves:
MotherboardInfo static WMI construction and lookup; ManagerFactory composition
(only original SettingsManager/HotkeysManager constructors, never Start); isolated
LayoutMode read; OneX BIOS read that dispatches the original virtual firmware
predicate; SteamDeck Open refusal; and physical-memory metadata. No original
hardware Open body is executed. No user configuration is read or modified.

The current independent device catalog bounds the observed constructor flags;
actual command/input objects, reserved hotkey flags, sensor matrices, motion
owner and original command subscription/disposal are checked. Firmware variants
retain the original predicate, including its existing major/minor semantics.
CPU values are metadata only and do not enable performance controls.

The pinned x64 GamepadMotion software math DLL is the only allowed unmanaged
module: its21 original managed declarations, exports and fixed CRT/kernel import
set are checked before execution. Actual original create/delete wrappers allocate
and release their software handles. All other unmanaged imports are refused.
This is valid native-math ownership and Host dependency admission, not hardware
native initialization or whole-program deployment acceptance.

Four single-method fault copies must fail exactly4/86/86/2 business consumers:
model capabilities, reserved hotkey flags, motion Dispose, and Host native-file
validation. The disposal fault deliberately keeps owned math handles; the test
releases all86 through the verified delete export after its original assertion
fails, then zeroes only those fixture-owned fields. Positive cleanup count is0.
Compile/ALC/setup failures are retained and never count as detected business faults.


## Joined actual factory and original constructors (round16)

`joinedFactoryProof` adds123 cases to the same existing global consumer. A separate
probe references only `ConstructorIoPorts`, never the legacy HC fixture/Harness.
It starts with no HC assembly loaded, invokes the actual Host factory and retains
Default ALC loading, Configure/logger, native-file admission, dependency resolver,
original GetCurrent/constructors/Initialize, identity, readiness, capability policy
and refused write admission. Exact pinned loaded HC path and bytes are checked.

All79 admitted models reach the Host:27 automatic motherboard identities and79
diagnostic admissions include the10 original nested-namespace refusals. Those
models are constructed through their original automatic route; no diagnostic gate
is widened. Unknown/default, unsupported ASUS/SteamDeck and original G1AMD refusal
remain truthful. X1/Apex keep the original missing FanControl bit and reach the
existing exact inherited-EC policy. No capabilities are forged by the fixture.

The prior seven constructor IO leaves remain explicit. The joined HC adds only
sensor discovery and always-refused settings-write leaves; all constructor,
GetCurrent and Initialize bodies remain byte-structure identical. Actual Host
replaces only its separately proven ACL/deployment leaves and one GetTempPath
operand. Restoring that operand must reproduce every original Configure
instruction/local/branch/exception region. Original MVID is retained and recorded.

Sixteen failure/lifecycle cases cover missing/invalid native math before assembly
load, unsupported sensors, five failed-stage retries, reentry, disposal and device
or assembly owner replacement. Every actual read-only handshake keeps hardware
writes refused and the device unopened. Five exact single-method faults fail the
same123-case suite at exactly3/6/2/4/1 business consumers: FirstStart, old readiness,
replayed completed stages, skipped native admission and the former unprefixed native path. Setup/build failures are
not accepted as fault detection. No Host Main, original hardware Open, manager
Start, SuiPlay or inactive display/CPU/GPU capability is activated.

The formal long evidence path reproduced a product admission defect: identical
pinned GamepadMotion bytes loaded at a short path but failed at259 characters
with HRESULT0x800700CE; the extended absolute Windows path succeeded. The Host
now normalizes the exact selected DLL path for NativeLibrary.TryLoad, including
already-prefixed and UNC syntax. The new actual-consumer cases explicitly use
the exact259-character failure boundary and paths over300 characters, validate the already-prefixed form, and retain missing
and invalid-file refusals. The one-operand private fault restores the former
unprefixed loader argument and must fail only joined-native:long-valid. No actual
UNC share or full long-path hardware bootstrap is claimed.


## Public factory joined with original constructors (round17)

`publicFactoryProof` runs the same123 original-constructor cases through the actual
public LoadAndHandshake, real OnStaBounded, thread Start, Dispatcher message pump,
factory stages and Dispose. All79 admitted original models are exercised. Actual
stage frames must name the same live foreground STA, differ from the caller and
carry the real DispatcherSynchronizationContext; Host thread identity must match.
The positive run must complete125 owned STA lifecycles,125 actual OS identity reads,
and zero fixture shutdowns or denied device-native calls.

The public Host retains the three prior validation/configuration leaves and adds
only one exact endpoint: original ReadCurrentOsThreadId forwards to the same
kernel32 GetCurrentThreadId signature in ConstructorIoPorts. The port assembly
must declare exactly this one native endpoint; no broad kernel32 resolver grant is
made. Dispatcher creation/publication/admission/execution/retirement and original
factory/constructor/Initialize bodies remain unchanged. Build-only settings/logs,
sensor/settings refusal and the prior seven constructor leaves retain their scope.

Six exact single-method faults use the same123 public cases and must fail exactly
3/6/2/4/1/122 consumers: FirstStart, old readiness, replayed stages, skipped native
admission, old path operand and public-dispatch bypass. The bypass constructs the
same original factory objects on the caller;122 actual invocation cases must reject
it while the independent all-model coverage check still runs. All owned threads
must exit through original Dispose, including fault runs; forced cleanup is a
failure, not evidence of successful product retirement. No Host Main, original
hardware Open, manager Start or deployment is executed.

Current frontend source closure includes startupConfiguration.ts, deployment.ts
and capabilityAdmission.ts as well as the stable fanHost.ts public entry. Refactor
imports do not by themselves prove unchanged behavior; current original audits and
compiled bridge consumers still execute and keep their private counterexamples.

The public probe reports actual message-pump execution only after observing the
real DispatcherSynchronizationContext in stage frames. Dispatcher-bypass faults
must explicitly report false with zero threads/OS identity reads; requesting the
public mode is not accepted as execution evidence.

The package-source adapter also declares all three extracted frontend modules.
The original21-case producer and strict read-only-input verifier remain intact;
removing any module declaration must refuse the otherwise matching bytes. Both
successful copies and the original unrecorded-input failure remain reviewable.


## Original OneX public device lifecycle (round18)

`originalLifecycleProof` joins five already admitted original OneX models to the
actual public Host Open/OpenEvents/Close and the real ApplyPowerProfile /
WriteFanProfileDuty consumers. All original constructor bodies and the complete
original IDevice.ECRamDirectWriteByte body remain unchanged. Its actual 12
command/data port writes execute with only OpenLibSys transport endpoints bound
to isolated ports. The original public factory, owned STA, message pump and
Dispose are retained. No Host Main or physical device operation runs.

The production fix adds the already supported X1 Mini to the narrow fan family,
and skips unrelated general IsReady HID probing for the exact narrow OneX family.
Mini otherwise entered general Open and its LED HID readiness override. Two
original-code failures remain in the round18 Build evidence. The narrow lifecycle
still validates the actual hierarchy and EC metadata, initializes its own
transport, checks false EC results and returns its own resources. Skipped general
readiness is reported as skipped, never as physical readiness.

The 29 cases / 1075 checks require 29 actual STA lifecycles and real read-only OS
thread-ID calls, zero forced cleanup or denied native-device calls, exact control
044a=1, duty 044b=92 at50%, and return044a=0 packets. Cases include all five
Open/Close/reopen paths, original profile subscription/cloning, authority refusal,
initialization/disposal/write/return faults, genuine public cleanup retries,
foreign transport ownership, changed metadata, supersession, invalid duty and
rounding, foreign open markers and busy-manager close debt. Every initialized
transport must be returned, including unpublished and foreign fixture owners.

HC adds exactly ten isolated leaves to the previously proven joined image: five
OpenLibSys IO endpoints, four traps for general mixed Open/Close/events or Mini
LED readiness, and the existing manager composition. PowerProfileManager uses
its real original constructor and events. DeviceManager is explicitly an
uninitialized original status-only instance; its constructor and Start are not
claimed. Original status/list readers execute. No physical OEM acknowledgement
is inferred from software events.

Six exact single-method private faults rerun the same29 cases and must fail
20/19/2/1/1/11 cases: Mini family, unrelated readiness, false EC result, failed
automatic return, duplicate release on disposal retry, and general event
admission. Only intended business failures count; all owned threads must retire
naturally even in fault runs. Earlier constructor, joined/public factory and
bootstrap suites remain required. All lifecycle evidence is protected by the
existing global source/evidence hash closure.


## Original MSI public lifecycle and structured hand-back (round19)

`originalMsiLifecycleProof` joins four already enabled original MSI models to
the public Host factory/STA, narrow Open/OpenEvents/Close, original HID readiness
algorithm, WMI readiness result, original ReleaseMsiFanControl/SetControlBit and
the actual Host current-operation settlement. No production behavior is changed
by this proof. The narrow workflow must retain firmware metadata binding; the
OneX skip is not copied into MSI.

Twenty-three cases cover all four models and reopen, disconnected HID rebind,
unknown firmware, transport/short/flag readiness refusal, superseded preparation,
failed release reads/writes/readback, already-automatic no-write, foreign boot or
operation receipts and foreign open markers. Each run requires23 actual STA
lifecycles and read-only OS identity calls with no forced cleanup or denied
device-native calls. Real original release packets retain32-byte padding and
152-before212 order, clear only bit7 and require original readback. A proven
software result is never physical RPM or OEM acknowledgement.

The prior original OneX HC image adds exactly ten MSI IO/trap methods: HID
enumeration, two connection-call operands in original IsReady, structured WMI
read/write and legacy shift read, plus forbidden general MSI Open/Close, registry
fix, PnP restart and legacy WMI write. Restoring only the two connection operands
must exactly restore every original readiness instruction/local/branch/EH. All
original device constructors, base Open, structured result/bit writer and release
remain unchanged. The original HidLibrary bytes remain pinned and unchanged;
only fixture metadata instances are uninitialized, with finalization suppressed.

The public Host adds only three prerequisite terminals after the established four
public factory ports: its system-folder operand points inside Build, its WMI
metadata endpoint is isolated, and PnP enumeration is forbidden. The complete
resource preparer and registration orchestrator remain real; restoring only the
folder operand restores the full preparer body. No system DLL, registry or PnP
change is allowed. PowerProfileManager remains actually constructed; DeviceManager
continues the explicitly uninitialized status-only fixture, with no Start claim.

Six exact original-consumer faults rerun the same23 cases: general Open fallback,
TransportOk used as IsUsable, missing firmware observation, omitted narrow release,
forced successful settlement and bypassed boot/token binding. Required failed sets
are23/2/5/17/5/2, only at named business assertions. Null/compilation/load failures
are not accepted as detection. Existing constructor/public/OneX proofs all remain
required. Manual curve/duty operation, real manager Start, other vendors and
cross-process ownership still require their independent acceptance.


## Original MSI public manual curve/control (round20)

`originalMsiManualProof` calls actual public ApplyCurve on the existing owned STA
after original factory/narrow Open/events. It consumes real ApplyCurveCore,
ApplyPowerProfile, ApplyMsiFamilyCurve, ReflectionMsiFanTransport, original private
WriteMsiFanTableBlock, SetFanControl, SetFanFullSpeed and structured readback.
Forty-four cases /1674 checks require44 actual STA lifecycles and OS identity
reads,52 public invocations with21 successful returns, and no native-device
attempts or forced cleanup. Original31-byte preservation,32-byte requests and
independent CPU/GPU temperature-axis goldens execute in the actual consumers.

A real missing owner guard was reproduced through this public entry: replacing
the selected device or loaded assembly after an isolated temperature/table call
let the old captured transport continue while the sleep epoch stayed unchanged.
The product now binds every existing manual stage guard to the same captured
device and assembly as well as the existing live epoch/open/close/timeout gate.
The same44-case set fails at four original owner cases before the repair. This
is a simulated replacement defect, not an observed physical device incident.

Only one additional Host IO operand is ported: the original infinite Timer is
created normally, while its one-shot Change call records an inert arm rather
than scheduling a background replay. Restoring that operand must exactly restore
every original watchdog instruction/local/branch/EH. All manual public/reflective
consumers and original HC table/bit methods remain unchanged. The inherited
resource target/metadata/PnP isolation stays intact. Strict terminal input guards
check independent original WMI scope/path, method, block, length and minimum.
The actual raw System.Management decoder is not executed or claimed.

Cases cover all four models, full speed and return to normal curves, repeated
current receipts,0/7/8/30-byte short tables, per-block read/write failures and
uncertainty, readback loss/mismatch, both required bits, invalid public curves,
explicit compatibility axes and a32-byte response's excluded final byte. Epoch
and owner changes are injected before writes and after each relevant stage; an
already-issued synchronous original call may finish uncertain, but no later
stage or successful commit may follow a rejected owner. Instant cancellation
inside original WMI/OS calls is not claimed.

Six exact single-method faults rerun all44 cases and must detect4/9/15/4/8/11
consumer failures: removed owner comparisons, removed stage boundaries, forged
table aggregate success, lost original payload tail, wrong100% intent and wrong
temperature endpoint. The tail fault changes one HC fixture method only; other
faults change one Host consumer only. Compile/load/null-reference/cleanup
failures never count. All inherited original factory and lifecycle suites remain
required. Replay callbacks, raw WMI decoding, managers Start, other vendors and
cross-process device ownership remain independent outstanding obligations.

## Original MSI timer/profile/firmware/power replay (round21)

`originalMsiReplayProof` extends the exact proven public manual chain. The
original BCL Timer is constructed unarmed, captures its original callback and is
disposed by the original owner. Timer scheduling and the physical power read
are the only three additional Host endpoint methods instrumented; restoring the
constructor/Change operands must reproduce the original full method bodies,
locals, branches and exception handlers. Explicit firing executes that captured
callback on an actual ThreadPool MTA. Real Host STA admission/message pumping,
Applied/Discarded delegates, pending snapshots, mode revisions, retries and
original manual/HC writers execute unchanged.

Twenty-six cases /1062 checks require26 actual public STA lifecycles and OS
thread identity reads,31 successful public manual calls and63 original timer
callbacks on ThreadPool MTA, with zero denied device-native attempts or forced
cleanup. Cases cover all four MSI models and firmware/profile/power edges,
retired timers after command replacement and reopening, actual bounded STA wait
while owner/sleep changes, stale event delegates, sleep/Close, duplicate and
coalesced revisions, one-shot mode attempts, three bounded profile attempts,
Applied during a write, stable zero-write state and explicit100% full-speed
intent. Actual Open reporting also requires observed original Open cycles.

The original implementation produced eight MTA write failures for external
profile/power edges and allowed two retired timer cases to adopt current state.
The Host now routes all MSI replay edges through the existing STA admission,
carries the pre-admission owner into the core and binds timer production to the
original captured owner. Existing sleep, revision, queue and bounded retry rules
remain authoritative. Six single-method private faults must reproduce exact
20/2/1/6/5/2 business failure sets. Missing methods, invalid IL, null references,
thread cleanup or compilation failures cannot count as detection.

Scheduling arms are deterministic ports; wall-clock timer delivery and busy
budget expiry are not proved by these cases. Raw System.Management decoding,
Manager Start/composition, cross-process ownership and physical hardware still
require their own acceptance. The production Host Main is never started.
