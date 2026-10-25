# Actual process exit-retirement helper

Compile/execute the current native input_host_exit_retirement.h and its checked
process-retirement dependency with private kernel child/creation-thread handles.
The suite does not mirror the algorithm or supply a fixed successful exit.

```powershell
python tools/input_host_exit_retirement_selftest.py --out G:/YeManCC-Work/Testing/YMCC/Evidence/VC-NativeExitOwned23/retirement-helper/next
```

Release/Checked retain 13 cases. Real force uses the same HANDLE and real
pid+creation identity, then checks process/thread signals and process exit code,
checked closes all fixture owners, and verifies no natural event-release backstop.
Four independent authority omissions, PID/creation/unknown identity and real
WAIT_FAILED must never force/close. Explicit accepted-without-exit and force
failure ports retain live owners; forceSpent permits no repeat force or renewed
wait. A real OS protected process Close failure retains the same exited HANDLE;
unprotect/retry closes it without force. These failure-port case cleanups use
fixture-only natural event release and are labeled, not product acceptance.

This runner accepts only the process helper contract, not a Host/SDK, pipe,
endpoint, physical or complete parent lifecycle. Permanent OS blocking remains
UNKNOWN. Parent composition and complete worker/Ensure execution are separate
NativeExitOwned23 parent evidence; UI/Main callsite execution is separate again.
