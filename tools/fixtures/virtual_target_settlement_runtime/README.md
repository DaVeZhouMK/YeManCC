# Actual target settlement runtime (148)

Runs verbatim extraction from `native/virtual_target_return_runtime.h`: State/Target/Physical, currentOwner/sameFlow/contains/flushProven, PS quarantine settlement and accepted RECREATE settlement. Also extracts original BeforeCreate/open/newKey/contextCurrent and note so next-entry proven-debt flushing is executed rather than modeled. Main/full TU and other lifecycle/device algorithms are never included.

```powershell
node tools/virtual_target_settlement_runtime_selftest.mjs --out G:/<private>/fresh148 --toolchain G:/<existing>/production-link100
```

Fresh absolute private output outside repository/toolchain is required. The actual Session/store/record run with real private files. MoveFileExW instrumentation forwards the real OS call; a held private read handle denying delete causes actual atomic replacement failure. Current parent identity is real GetCurrentProcess capture. The only Host handle is an owned anonymous event mapped to explicit synthetic PID/birth; this is a controlled identity fact, not actual Host liveness. Runtime path/binding, log sink, persona/power/fence observations are explicit external leaves. No CLI/Host/device/installed AppData calls.

Cases cover PS false absence/live Host/missing owner/wrong tuple/parent/pin/mixed paths, exact local target-only versus historical duties, and actual commit failure plus retry at the original entry. RECREATE validates positive exact receipt, command/status/tuple/new reservation/sequence; negative/lost/unknown/malformed receipts and nonolder or historical duties remain. A failed old-origin commit keeps new reservation and proven debt, then actual next BeforeCreate flush retries it. A continuous old pin-A proven/failed commit -> new pin-B quarantine case must finish without requiring a new Start, without bypassing pin checks.

Three compiled semantic negatives mutate only extracted copies: bypass qualified PS absence, include accepted sequence when deleting older reservations, and ignore failed actual PS consume. Original assertions must catch named cases; compile errors, fixture exceptions and crashes never count. `--expect-continuity-red` is historical evidence mode only: positives must fail exclusively the explicit A-proven/B-quarantine continuity case and remain pass=false. Default requires green Release/Checked.

Compiler/sourceDependencies/link inputs/static imports, tool identities and all source/binary/result hashes are bound to production-link100. Prior evidence/failures are never overwritten. Scope is exact target historical settlement and private persistence, not new lifecycle policy, actual Host exit proof, pin admission, hardware or full144/145 validation.