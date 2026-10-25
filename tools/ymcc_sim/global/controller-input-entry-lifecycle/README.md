# Controller caller / repository lifecycle replies

Run with the existing simulator:

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests 'controller.input-entry-lifecycle' -EvidenceDir 'G:\YeManCC-Work\Build\Validation\VC-ENTRY-20261021\global'
```

This executes the original mirror action, game input overlay, settings repository, API and IPC consumers. The inert WebView reply endpoint supplies explicit protection/conflict/write-failure replies. It does not copy or implement native admission: native single-cycle locking, timing, cleanup and return remain the responsibility of the existing actual full-TU fixture.

Thirteen scenarios cover protected OFF/persona saves, success remaining pending until Host evidence, cancellation before queued commit, overlay retry, native revision conflict, stop during queued read, mirror/overlay interleaving, legacy restore and failed writes. Three compiled source mutations falsely accept protection replies, publish rejected overlays or ignore mirror refusal; designated assertions must fail. Product sources are never edited by this harness.

Game detection/config reading and the existing scheduler are declared memory seams. All file state is memory only; unexpected native ports fail. Node, esbuild and observed Vue dependency bytes are checked before and after. Complete source inputs, bundle bytes, mutation identities and original failures are retained in a fresh evidence directory. This is not normal App, native lifecycle, real driver, game or sleep acceptance. A new passing regression does not by itself change an already frozen R4 package.
