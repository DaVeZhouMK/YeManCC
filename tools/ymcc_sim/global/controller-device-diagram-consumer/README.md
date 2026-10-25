# Controller device diagram consumer (candidate only)

Candidate ID: `controller.device-diagram-consumer`; registration belongs to the parent.

Run `node tools/ymcc_sim/global/controller-device-diagram-consumer/run.mjs --repo-root <owned source view> --evidence-dir <fresh absolute D evidence directory>`.
The explicit evidence directory is mandatory. The unchanged original OEM presentation producer receives that directory, never its G Build default.

The module compiles and renders the actual `ControllerDeviceDiagram.vue` and `ControllerVectorArtwork.ts` using the existing Vue SFC / esbuild APIs and selected Playwright/Edge toolchain. It repeats the same ordered 21 DOM/component cases at 1280x720 and 1280x800, plus all assertions of the unchanged original producer. It does not run the full original navigation suite or product App; directional navigation/physical input acceptance is not claimed.

Two isolated one-source/one-site mutations target the original m1 slot mapping and the component's held-state cleanup. The same original/component cases must fail named business assertions after successful compilation; setup failures never count as detection.

`lockBundledNodeTools`, `classifyBundledInput`, `verifyBundledToolScope` come from the pinned parent helper, unchanged. The original UI declaration verifies complete browser/compiler tool membership. Observed compiler inputs, actual source bytes and loaded Node modules are recorded separately from the complete declared tool closure. External node_modules tools never enter product sourceHashes. Original global sourcePath is unchanged.

Browser events are synthetic DOM events and component emit ports only. Pointer capture is an explicit DOM test port. The source reset clears local held/axis/timer state without emitting a new downstream neutral. Tests report those facts separately and do not claim native release. No network beyond owned loopback, installation, App, Host, Steam, device, OS sleep, CPU/TDP functionality or HC fixed24 execution is performed.


父端整合边界：正常场景直接消费声明的固定源码manifest；基线有限捕获SHA只作参考。两个故障视图逐字复制同一已选版本，仅改变指定一处产品源码，并复用原固定版本的工具authority；不把snapshot binding转发成另一个primary scope。编译和原producer读取只允许已声明的源/模块路径，未知产品import拒绝；失焦/卸载仍仅证明组件本地清理，不冒称下游中立归还。


2026-10-21 G3E direction regression: the user verified correct rear-button directions in Steam. The native model catalog already describes front-view M1 right / M2 left. SteamDeck presentation now takes left/right from that catalog and moves numbered rear keys to the lower row on the same side (G3E M1 -> R5, M2 -> L5). The golden catalog expectations and DOM cases cover labels, live/mapped fills, disabled sources and unchanged persisted IDs. The single-source display mutation restores the original wrong M1 -> L5 / M2 -> R5 rule and must fail both the golden producer and actual SFC cases. No firmware, native input or Host output mapping changes are part of this fix.
