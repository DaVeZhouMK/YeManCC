# InputHost CPU selftest

Use the exact pre-edit InputHost assembly and its recorded SHA-256. Never rebuild the candidate and pretend it is the baseline. The PowerShell wrapper builds the current InputHost and this runner in isolated output directories, then executes the actual private frame-hash function, the actual bound-parent-handle implementation, and six existing production selftest entries.

- 70,000 random IEEE-754 hash comparisons across seven cultures;
- 504 edge/culture cases, including signed zero, subnormals, NaN bit patterns and a non-ASCII minus sign;
- original-parent exit, fail-closed missing/disposed handle and non-revival checks;
- five alternating helper microbenchmark windows for allocation and elapsed cost.

This runner never invokes the Host's normal Main or a controller factory. Its two short-lived child processes are diagnostic-only. Microbenchmark gains are not whole InputHost, HID backend, or YMCC CPU gains. Actual normal-pipe/real-hardware performance still needs separate measurements.

The wrapper requires `-BaselineAssembly`, `-ExpectedBaselineSha256`; an existing nonempty output directory is rejected. It does not install, deploy, start YMCC, create SYSTEM privileges, or close unrelated processes.