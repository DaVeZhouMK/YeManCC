> RETIRED 2026-10-19: historical v3.2.10 fixture only. Production consumes byte-identical official releases via tools/decky-loader-upgrade.ps1; these patches must not be applied to new releases.

# YMCC Windows Decky recovery policy (2026-10-16)

The pinned Windows Loader is a GPL-2.0 derivative of the existing YMCC Loader.
Its license stays at PowerControl/decky/LICENSE.decky-loader. Original source
identity and the reproducible patch are fixed by manifest.json.

## Reuse and reconnect

injection.js replaces the original unconditional RestartJSContext branch with
existing frontend reuse, fresh authentication and guarded recovery. No Steam UI,
focus, reload, restart or business operation is called by these policies.

The shared router-recovery.js is attached by the packaged lazy connect entry and
by injection into an already-mounted router. A non-enumerable canonical connect
reference prevents recursive capture when injection precedes first connect.
The original WSRouter class, FULL_SYNC, pending resolver map and real RPC error
rejections are preserved. Connect callers share eventual success through failed
handshakes; stale/duplicate closes are ignored. Repeated connect calls cannot
bypass a failure backoff. A borrowed legacy CONNECTING socket also gains the
same ten-second handshake deadline, never extended by repeated signals.

Failure retries back off at 5, 15, 30 and 60 seconds, then remain at 60 seconds
without giving up. A verified backend-ready signal bypasses that backoff while
leaving an open socket alone. Only one transport retry and one pending handshake
are owned at a time. Failed transport retries renew backend auth through the
shared bounded authentication request, allowing token rotation to recover while
the initial frontend boot is still awaiting connection. No healthy auth polling
is added. Transport diagnostics are bounded JSON debug facts without tokens or
URLs; same-code failures are limited to thirty seconds except code changes/open.
An old close callback bound before upgrade may still emit once until replaced.

## Authentication and initialization

Authentication has a ten-second deadline across headers and body, aborts on
timeout, and never applies a losing/late result. Injection and transport retries,
including new notifications, share one pending auth request. Failed injection
work retries with the same 5/15/30/60-second backoff, has at most one retry timer,
and cancels it when a new notification arrives. Successful work clears its timer.
There are no periodic recovery tasks while the interface is healthy.

The fixed static index.js exposes its real async boot Promise to injection,
uses the shared auth function, and retains an existing router on boot retry.
A failed module-fetch retry changes only the entry retry query; healthy modules
are not remounted. Waiting for SteamApp initialization is capped at four checks
per second rather than a zero-delay loop, without a hard startup cutoff. This
is a bounded check, not proof that a game or Steam Input is responding. JavaScript
cannot progress while its process is suspended; late timer service resumes one
attempt rather than replaying a catch-up burst.

## Hidden optional Python probe

python-path-probe.py keeps optional interpreter discovery. Windows probing uses
CREATE_NO_WINDOW/SW_HIDE, a five-second timeout and skips Microsoft WindowsApps
aliases. Missing Python remains optional; Linux/mac user/env behavior is retained.
No real third-party plugin is promoted to administrator. Original native process
identity, user/session verification and launch permissions are unchanged.

## Reproduction and evidence

Use isolated Python 3.11 with tools/patch-decky-loader.py, the exact original
binary in manifest.json, --policy injection.js, --python-policy python-path-probe.py
and --router-policy router-recovery.js. Repacking validates original base, module
and both static entry hashes. Only create_inject_script and get_system_pythonpaths
in decky_loader.helpers change inside PYZ. Only the pinned router chunk and
index.js change outside PYZ; all other resources and the original router class
remain unchanged. The archive is read back and the generated injection is
executed without importing real backend modules. A second repack must be identical.

Policy version 5 is pinned with native setup and build/export validation. Native
prepare/environment logging reuses the existing resource digest and records
expected/observed/matched/version without requiring sidebar demand. This receipt
identifies a checked file, not an atomic hash of a mapped running image. Runtime
resource acceptance rules are unchanged. Export remains strict about source,
staging and final archive identity.

Tests execute the actual old/new packaged routers, injection and async entry
with inert authentication, imports, WebSocket and virtual time. Coverage includes
long outages, late timers, failed/late auth, duplicate signals, initial boot token
rotation, queued RPC recovery and zero healthy recovery timers. They do not run
Steam/Loader, measure real-machine CPU percentages, or claim every external Steam
failure is recoverable. The no-console test separately starts only an inert Python
child and checks its console HWND.

## Hang recovery additions (policy 5)

A CLOSING socket has a ten-second close grace. If acknowledgement never arrives,
only its logical router reference is retired before replacement. No Steam process
is killed or restarted. New router message callbacks ignore stale socket events;
late replies cannot settle a current RPC. Diagnostics include policyVersion and
closingTimeouts, distinguishing running JS policy from the prepare-time file hash.

The separate YMCC mirror client has a demand-only fifteen-second snapshot deadline
when no initial connection deadline already covers the request. It catches an OPEN
transport whose business owner no longer responds. A valid snapshot, hide, close,
rebind or disposal cancels the deadline. Repeated demand does not postpone it.
Timeout reconnects/readbacks and rejects unconfirmed writes; mutations are never
replayed. Send/close exceptions cannot strand cleanup. Healthy HUD-only mode has
no snapshot demand, periodic liveness timer or extra business read.

Fixed local Steam binaries contain IPC-timeout/webhelper recovery and watchdog
diagnostics. These are static evidence, not a public recovery API or a universal
timeout specification. A fully suspended renderer cannot execute JS; native CDP
command deadlines release the watch and verified context replacement can reattach
without trying to repair Steam internally. Research and isolated fault tests are
under Build/Tasks/YMCC-Decky-HangRecovery-20261016.
