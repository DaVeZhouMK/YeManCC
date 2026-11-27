# Standalone core installer

This folder is the mainline source for the separate full installer. It is not a
runtime dependency of YeManCC and is not copied into the normal YeManCC.zip.
Build payloads, acquired installers, extracted trees and final EXEs must remain
outside the Git repository. Do not change version.json or the fixed GitHub
YeManCC.zip update asset when building this product.

Default delivery: Deliverables/YeManCC完整安装包exe/安装YeManCC.exe.
Show the bundled version, never use installed versions for admission, and preserve
user settings while replacing package program files. Close only owned processes
from the installation trees; never stop Windows services or unrelated apps.
Step 1 shows the bundled version and environment detection; step 2 shows
the fixed installation directory, desktop shortcut and default-enabled, visible
Defender exact-file exclusions option. Installation is locked to C:\SOFT\YeMan;
reject alternate roots at the native boundary. No browse/reset/online-program options
or extra installation-mode selector; reuse the two-step flow for local updates.
Installation is machine-wide: always publish a Common Programs shortcut and use
the common desktop for the optional desktop shortcut. Use native Shell Links and
verify their target, working directory and icon. Preserve same-named links that
do not target this installation. Publish the uninstall entry in HKLM (64-bit)
after committing files. Missing/failed registration and inventory are not
installation prerequisites. Per the user's 2026-11-13 request, remove desktop .lnk files that
are named HidHide and point to the official HidHide configuration client after a
fresh HidHide install is attempted. Journal
this removal with the original file transaction and preserve unrelated shortcuts,
the installed client, driver and hiding rules. Do not depend on WScript.Shell.
Use only the official Defender provider, preserve existing exclusions, verify
effective exclusions and journal new entries. Never bypass policy/tamper protection
or disable antivirus. Preview/capture must never invoke the Defender client.
YMCC, HW/RTSS, .NET and VC++ use bundled local resources, without version queries.
The standalone installer owns fresh HidHide preparation. Use the pinned official
EXE already in program.zip; validate SHA256 and Nefarius Authenticode publisher.
After committing program files, use the bundled helper's uninstall-only mode for
verified HidHide 1.1.50.0. Keep newer, partial or unverified installations intact.
Cleanup failure, contention or a slow MSI must not block program completion.
During a YMCC-owned HidHide migration, reserve the missing-driver interval for
that owner, using its protected maintenance marker and shared driver mutex.
Never force cleanup or directly change hiding rules from Setup. Journal results and reboot requirements; do not run or
bypass YMCC input module deployment scripts or infer module readiness from this.
WebView2 is the sole network exception: bundle only Microsoft's signed small
Evergreen bootstrapper, run it only if WebView2 is unavailable and the user opts in.
Never bundle the full WebView2 runtime. Use the native .NET Framework 4.8 wizard
when WebView2 is absent or its UI cannot initialize. Missing components are warnings
after program files are installed. The WebView2 UI is ui/setup.html; preview and
production use the same embedded UI bytes. --preview, --preview-offline and
--capture-ui must reject installation at the native boundary.

Never run runtime installers, replace installed files, launch YMCC or restart the
machine as a test on the development host. Use the explicit isolated verification
commands for file operations; report system actions separately as NOT_RUN.
