# Settings diagnostics UI consumer (finite sidecar)

`settings.diagnostics-consumer` compiles and renders the actual captured `src/views/SettingsView.vue`, actual child SFCs and actual bridge state logic at 1280x720 and 1280x800. It is a separate D-only sidecar and does not gate parent B43 work. The source is selected by the complete B43 manifest; tools are separately declared/pinned. `sourceApproval=false`.

## Accepted UI scope and remaining requested gap

B43 actually renders the unified high-frequency switch, whole-log export and `DiagnosticLogPauseClearButton` transaction UI. Twelve named cases per viewport cover confirmed enable/disable, rejected/unknown acknowledgements, inline export success/warnings, empty/rejected export, busy/reentry/recovery, original category-B and delete-dialog-B assertions, delete confirmation/busy, and unconfirmed restoration/transport failure. The real `diagnosticLogMaintenance.ts` parses the injected protocol receipts and formats feedback.

The old independent fan/input toggle/export/clear functions and status refs remain in the SettingsView script but are not bound/rendered in its template. They are `DORMANT_NOT_RENDERED`, not UI executions. `requestedFullScopeAccepted=false` is always explicit. The sidecar never adds controls or calls/extracts those dormant handlers to manufacture coverage. `fanFeature`, `fanDiagnostics`, `inputDiagnosticsUi`, `settingsRepository` and their dependencies are real source; their mount paths do not establish acceptance for absent UI actions.

## Memory terminal boundary

The existing `ui-navigation/templates/memoryIpc.ts` is reused without edits for in-memory settings and initialization endpoints. `memory-ports.ts` only adds controlled replies/holds/rejections for `logs.domainGetEnabled`, `logs.domainSetEnabled`, `logs.exportAll`, `logs.deleteCurrent`, and returns false for `fs.exists` to retain optional feature/capture absence. Every command/reply is recorded. No real file, settings, export, delete, installed application, Host, Steam, device, network or OS sleep operation runs. Initial unrelated read endpoints keep the existing navigation memory facts; these are not native implementation acceptance.

The route terminal is fixed to `/settings`; no product router or installed-window action runs. The original `gamepad/engine.ts` is appended with the existing `engineAudit.ts` reference adapter, exactly as the original navigation fixture does. Engine functions are not copied or replaced, and `startGamepad`/hardware polling is never started. B dispatch and focus are the actual engine/SettingsSections/delete-dialog consumers. No entire navigation suite is rerun.

## Tools, source and output identities

The existing `bundled-node-tool-authority.mjs` and complete `ui-navigation/toolchain-inputs.json` are composed; full Node26/749 and UI2302 members are deduplicated to 2595 inputs (limit3072). The existing reviewed diagram `resolveUiToolContext` is reused only for its explicit verified original binding root mapping. Snapshot raw `.pnpm` paths are never treated as a tool root: snapshot source stays captured, UI package resolution uses the verified original bound source root and captured UI manifest. Finite D validation owns exact, separately recorded tool copies with matching primary paths.

The browser explicitly loads the declared Playwright entry and Edge executable. Full scope is verified before/after compile and browser; actual CJS loads, actual esbuild source/tool inputs, compiler/browser identity, command/PID/exit and owned HTML/JS/map/CSS/screenshots are separate records. External tools never enter product source hashes. Product keys are repository-relative; absolute keys are only case-owned sources/outputs, preserving the existing source reader fence. Missing descriptor inputs return BLOCKED metadata.

Two single-literal SettingsView fault copies live inside their owned case: export sets busy=false, and export failure text is replaced with success text. Both must compile and mount, then fail the same named UI assertion used by the normal cases at both sizes. Raw FAIL records and screenshots remain; compilation/environment failures never count as detections.

`run.mjs --repo-root <source-root> --evidence-dir <fresh-owned-case>` is the leaf entry. The single extra-adapters draft registers that producer through the unchanged exported global runAdapters API, with 240000ms budget. Only this module and the exact one-entry registry patch are merge candidates; no G/B43/shared runner/audit/UI tool implementation changes are included. Read READY/PATCH manifests for original receipts and SHA identities.
