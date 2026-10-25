# Actual route fallback TS/DOM consumer

Unregistered adapter candidate: getRouteFallbackConsumerAdapters() in adapter.mjs. No central registry/index changes. Fixed 30,000ms budget for this new leaf.

run.mjs bundles the actual src/robust/routeFallback.ts through the existing declared esbuild and runs it in an owned headless Edge via the existing UI tool declaration. Source bytes are pinned by spec.json. document, HTMLButtonElement, focus, disabled button click, event dispatch and subscription de-duplication are native DOM semantics. memory-ports.ts replaces only the dialog/reset imports and injects a window proxy whose event methods still invoke the native window. The proxy's location.reload is a memory counter. No real settings reset, navigation or installed app.

The original fifteen actual cases and two cleanup mutants are preserved byte-for-byte in the repair. Five additional cases cover a replaced same-node owner, owner loss before confirmation/reset settlement, and retired error/finally paths. Three additional single-change mutants delete the ownership predicate or one post-await owner check. Source failures stay FAIL; neither compiler/environment failure counts as mutation detection. All twenty cases, six actual TS bundles and five mutants share the same 30,000ms total leaf budget. The original actual-r1 evidence preserves two real source FAILs from the pre-repair product.

The fallback owns a retained DOM node, subscription and borrowed inert state: persistent. Ordinary clear revokes the overlay owner before removing DOM; a new show supersedes the previous rendered owner. Old buttons and async continuations must still own the current connected node before they act or update status. StartupFailure/busy retention is intentional and unchanged. A reset already requested before owner loss cannot be cancelled by this API; its completion may not trigger stale status/reload/reenabling. There is no complete exit/fallback-destruction or sleep/healthy-recovery API, so full persistent lifecycle remains INCOMPLETE even if leaf cases pass. Page/browser closing is test-resource cleanup, not evidence of product destruction.

Use an explicit fresh core evidence directory:

    node tools/ymcc_sim/global/route-fallback-consumer/run.mjs --repo-root G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC --evidence-dir G:/YeManCC-Work/Testing/YMCC/Evidence/RouteFallbackConsumer-20261023/actual-r1

Every build records exact product/mutated bytes, actual compiler inputs and emitted output hashes. The result includes declared complete tool artifacts, actual CJS tool modules, compiler identity, owned browser PID/executable/exit, source-before/after and raw assertions. Source coverage association is not parent acceptance. Main independently reviews/registers the adapter and keeps all original lifecycle/owner/tool gates.
