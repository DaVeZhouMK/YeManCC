# R119 canonical reader identity and recovery health

This fixture compiles the actual canonical snapshot ABI, the complete production
`inputHostRecoveryHealthSource`, the actual producer snapshot/continuity commit,
and the unmodified production recovery-health header. Source eligibility leaves,
cached atomics, pad reads and the clock are inert process-local seams. It does
not run the reader, Host, pipe, scheduler, product, Steam or any device.

From the repository root:

```powershell
node tools/recovery_source_identity_r119_selftest.mjs --out G:/YeManCC-Work/Build/Validation/VirtualInput-RecoveryClosure-R119-20261016/source-after-final
node tools/recovery_source_identity_r119_selftest.mjs --source G:/YeManCC-Work/Build/Validation/VirtualInput-RecoveryClosure-R119-20261016/before/native/main.cpp --header G:/YeManCC-Work/Build/Validation/VirtualInput-RecoveryClosure-R119-20261016/before/native/input_host_recovery_health.h --out G:/YeManCC-Work/Build/Validation/VirtualInput-RecoveryClosure-R119-20261016/source-before-final
```

The original source has 7 failing scenarios / 10 failing assertions among
12 scenarios / 28 assertions, in both Release and Checked. It treats retired
or zero MSI revisions as live, joins reopened readers, route transitions and
selected XInput slots into one health window, and accepts old receipts when the
continuity number alone collides. The fixed source passes all 12 / 28. A new
reader can subsequently build its own healthy window. Stable zero input and
the same XInput slot's `xinput` / `xinput-any-connected` aliases remain valid;
unrelated MSI revision changes do not retire an XInput reader.

The runner saves source/header/fixture/runner/dependency hashes, verbatim code
ranges, generated C++, executable hashes and raw JSON results. Exit 0 is green,
1 is a behavioral failure (the old baseline remains red), and 2 is an
infrastructure/compile/binding failure. It verifies the source, health header and fixture
remain stable during the run. Dependency hashes bind the copied inputs. Release and Checked must produce identical
complete results. None of this proves a field race occurred or a consumer can
use the controller.

An additional actual adapter/request/accepted/maintenance test in the existing
`input_host_recovery_health_selftest.mjs` demonstrates the downstream effect:
a new MSI incarnation must not clear the old recovery episode budget; its own
fresh accepted window may later settle it. The healthy receipt records route,
incarnation and continuity in ordinary exported logs. See the round evidence
for the paired original and candidate results; no old red result is converted
to a passing expected-failure result.