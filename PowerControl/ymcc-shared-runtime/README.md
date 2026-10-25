# YMCC shared third-party runtime

`bin/` contains only eight unchanged, pinned third-party DLLs. `manifest.json`
locks their original HC distribution hashes, assembly identities, upstream source
commits, licenses and consumers. HC is the previous distributor, not their owner.
HIDMaestro, USBIP and the HidHide driver are separate components; no SDK or driver
is reimplemented or included here. `Nefarius.Drivers.HidHide.dll` is the existing
managed client library, not a driver installer.

Source projects resolve `$(YmccSharedRuntimeRoot)/bin/`; the default is this
folder. Normal builds copy the required DLLs beside each host. InputHost's
existing thin fallback resolves only `PowerControl/ymcc-shared-runtime/<name>.dll`
with exact SHA256/identity checks. Packaging must flatten `bin/*` into that
runtime directory and retain `manifest.json`, `licenses/` and `provenance/`.
Manifest `path` is the source-relative location; `packagedPath` is the flattened
DLL location. Existing HidHide Vicius/logging/DI support files remain host-local.
The normal build, package and deploy scripts now consume this independent authority. This does not deploy the installed product.

LibreHardwareMonitor is the original 0.9.6+d91ec6e build, NOT substituted with the
cached NuGet 0.9.6 build (different bytes/commit). RAMSPDToolkit is retained as its
existing managed transitive dependency. The battery EC path still uses existing
PawnIO behavior. `Providers.cs` resolves the retained Resources/inpoutx64.dll from the reviewed
YMCC-DEVICECORE-20261018-v1 runtime; that EC transport is not part of this bundle.

Run `python tools/ymcc-shared-runtime/verify.py` for dependency validation.
Run the same tool with `--build --scratch <new-directory>` for isolated shadow
builds containing no HC runtime/source directory. It never launches a product
host or accesses a device. Global build/deploy/export integration is covered by the formal integration selftest.
