# Process identity leaf (task 107, common census fallback)

The fixture compiles the actual production header `native/virtual_target_return_process.h` with the existing record `ProcessIdentity` (PID and canonical nonzero uint64 decimal creation FILETIME). It tests the common helper independently of device family. Recovery, record/session ownership and the later cold target-absence check remain the consumer's responsibility.

## Interface

Namespace: `ymcc::targetReturn::process`.

- `capture(HANDLE borrowedProcess, DWORD* error = nullptr)` returns the exact PID/birth or no identity. The caller retains its handle. Real process pseudo handles work; non-process, restricted, failed and zero-birth queries are rejected.
- `probeRecordedOwnerHandle(const ProcessIdentity&)` preserves the original direct probe: one noninheritable `PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE` open for the exact PID, PID/birth verification and a zero-time wait, with every owned handle closed.
- `probeRecordedOwner(const ProcessIdentity&)` returns the direct Alive/Dead result unchanged. Invalid input remains Unknown without API calls. Only other direct Unknown results may invoke the census fallback.
- `Probe` exposes `state`, `reason`, `win32Error`, `directWin32Error`, `censusAttempts`, `censusBytes`, `censusRows` and `censusNtStatus`. The last raw NTSTATUS is separate from the Win32 error; failed NT queries report `ERROR_GEN_FAILURE`. Stable `stateName`/`reasonName` helpers describe the result.

The fallback resolves `NtQuerySystemInformation` from the already loaded `ntdll.dll`, captures this fixture's own process birth, and queries SystemProcessInformation (class 5). It starts at 64KiB, permits at most three calls and at most 8MiB per allocation, and retries only STATUS_INFO_LENGTH_MISMATCH or STATUS_BUFFER_TOO_SMALL. It consumes the returned length, validates the complete NextEntryOffset chain and duplicate PIDs, and requires one self row with the exact captured birth before giving the snapshot authority. CreationTime is at byte 32 and UniqueProcessId at byte 80 on x64 (68 on x86).

| Observation | State / reason |
| --- | --- |
| Direct matching live / exited identity | Alive / matching-running; Dead / matching-exited; no census |
| Direct PID reused / error 87 | Dead / pid-reused or pid-not-found; no census |
| Census exact PID and birth still present | Alive / census-matching-running |
| Complete self-validated census lacks target PID | Dead / census-pid-not-found |
| Complete self-validated census has target PID with different nonzero birth | Dead / census-pid-reused |
| Missing API, failed query, size or attempt budget exhausted | Unknown / distinct census reason |
| Truncated, out-of-range, unaligned or duplicate rows; zero/negative target birth | Unknown / census-malformed |
| Missing, mismatching, ABI-shifted or uncapturable self identity | Unknown / census-self-unproven |

Non-target zero birth (including idle) is allowed. Alive may become stale immediately. Dead applies only to the recorded PID/birth and grants no process termination, target cleanup, descendant, CLI, service, driver or unhide authority. Cold recovery still requires its existing independent target-absence and ownership checks. The leaf neither authenticates persisted records nor schedules background work.

## Minimal isolated regression

Run the existing leaf runner with explicit absolute paths and a fresh output directory outside source/toolchain directories:

```powershell
node G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/virtual_target_return_process_selftest.mjs --out G:/YeManCC-Work/Testing/YMCC/Evidence/VC-ROG26-113705-ColdHost-R1/process-helper-UNIQUE --toolchain G:/YeManCC-Work/Testing/YMCC/Evidence/VC-Composition95Current23/toolchain-authority
```

The runner verifies actual tools against the captured manifest; freezes the current production process/record headers, JSON dependency, fixture, runner and this README; binds actual compiler includes and verbose linker inputs; inspects static PE imports; binds the executed binaries; and checks source/toolchain freshness afterward. It does not discover a replacement compiler or overwrite earlier FAIL evidence. Each variant retains its command, build/run logs, compile closure and terminal case/assertion result. The aggregate result includes the real census observation and per-variant coverage.

Release is `/O2 /MT /W4 /WX`; Checked is `/Od /RTC1 /MT /W4 /WX`. Checked supplies MSVC runtime checks, not arithmetic-overflow sanitization. Original real private child/parent lifetime, borrowed/restricted/event handles, terminal identity and repeated OS handle-count checks remain. Only private copies of this fixture are launched; children use private events and bounded safety timeouts. Cleanup can terminate only a fixture-owned child.

Synthetic seams intercept only the compiled production leaf's APIs. They independently encode snapshot bytes and control module/export availability, direct/self query faults, statuses, returned sizes and allocation pressure; the production helper makes every liveness decision. A separate required case forces only direct OpenProcess denial while delegating to the real ntdll export for a live private child, with actual calls, bytes, rows and self validation recorded. This is real OS snapshot integration with a synthetic denial, not an observed protected/reused OS PID.

Census boundaries cover reused/missing/exact-birth owners; either row ordering; recorded self; idle/unrelated zero births; missing or wrong self/layout; duplicate target/self/unrelated/idle; zero/negative target births; oversized PIDs; malformed offsets and terminal chains; truncation after an already-seen target and self; returned-length poison; missing APIs; failed self capture; negative query status; each retry status; growth through the third call; exhaustion; over-budget sizes; the exact 8MiB allocation boundary; and failure after growth. Conclusive direct results and invalid input assert zero census calls. Original direct error assertions remain against the actual unchanged handle helper, and the wrapper separately verifies preserved direct errors.

The five original separately compiled semantic faults remain: denied open as Dead, reused PID as Alive, exited process as Alive, failed wait as Dead, and leaked probe handles. Five additional mutations must fail their intended original census cases: live census owner as Dead, absent self proof accepted, scan stopped after target/self, a fourth query permitted, and failed query payload parsed. Mutations affect only frozen evidence copies of the production header. They are negative controls, not alternate implementations.

Scope is this common process-identity helper. No product/InputHost/Steam/device/service/driver execution, installation, physical hardware, actual sleep, complete cold consumer or family acceptance is claimed.
