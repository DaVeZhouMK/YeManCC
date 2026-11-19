# Standalone core installer

This folder is the mainline source for the separate full installer. It is not a
runtime dependency of YeManCC and is not copied into the normal YeManCC.zip.
Build payloads, acquired installers, extracted trees and final EXEs must remain
outside the Git repository. Do not change version.json or the fixed GitHub
YeManCC.zip update asset when building this product.

Default delivery: Deliverables/YeManCC完整安装包exe/安装YeManCC.exe.
As of the user's latest 2026-11-12 instruction, this product automatically detects
fresh installation, local upgrade or same-version reinstallation. Show the installed
and bundled versions before confirmation; preserve user settings, reject downgrades
and recheck the confirmed version before elevation and file replacement.
Step 1 shows the bundled version and environment detection; step 2 shows
the fixed installation directory, desktop shortcut and default-enabled, visible
Defender exact-file exclusions option. Installation is locked to C:\SOFT\YeMan;
reject alternate roots at the native boundary. No browse/reset/online-source options
or extra installation-mode selector; reuse the two-step flow for local updates.
Desktop shortcuts use the native Shell Link interface, verify their stored target,
working directory and YMCC icon, and notify Explorer after publishing. Keep the
YMCC shortcut on the common desktop so alternate UAC credentials do not change
the destination. Per the user's 2026-11-13 request, remove desktop .lnk files that
are named HidHide and point to the official HidHide configuration client. Journal
this removal with the original file transaction and preserve unrelated shortcuts,
the installed client, driver and hiding rules. Do not depend on WScript.Shell.
Use only the official Defender provider, preserve existing exclusions, verify
effective exclusions and journal new entries. Never bypass policy/tamper protection
or disable antivirus. Preview/capture must never invoke the Defender client.
YMCC, HW/RTSS, .NET and VC++ use bundled local resources, without version queries.
The standalone installer owns fresh HidHide preparation. Use the pinned official
EXE already in program.zip; validate SHA256 and Nefarius Authenticode publisher.
Reuse an existing usable driver, preserve partial installations, never uninstall
or change hiding rules. Journal results and reboot requirements; do not run or
bypass YMCC input module deployment scripts or infer module readiness from this.
WebView2 is the sole network exception: bundle only Microsoft's signed small
Evergreen bootstrapper, run it only if WebView2 is missing or too old. Never bundle
the full WebView2 runtime. A small .NET Framework 4.8 bootstrap works without
WebView2; after explicit online preparation, the actual UI is ui/setup.html hosted
in WebView2. Preview and production must use these same embedded UI bytes.
--preview / --capture-ui must reject installation at the native boundary.

Never run runtime installers, replace installed files, launch YMCC or restart the
machine as a test on the development host. Use the explicit isolated verification
commands for file operations; report system actions separately as NOT_RUN.
