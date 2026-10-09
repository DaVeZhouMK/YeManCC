# T22-P05 supplement - counter producer boundary

capturedUtc: 2026-09-04T23:14:54.0000000+08:00
cwd: G:\YeManCC-Work
runtimeOperation: false
buildOrTest: false

This supplement corrects the scope of the base P05 finding. It does not
replace `T22-BUS-P07-P05-P06-static-evidence-20260904.raw.md`; it separates
the mock's internal counter producer from a measurement of an external game
consumer.

## A. InputCoordinatorMock declares its non-product boundary

argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\inputCoordinatorMock.ts, lines 41-52
exitCode: 0
stdout:
`
41: /**
42:  * F1/F2 integration mock. It joins the lifecycle and physical-owner policies
43:  * around the pure assembler, while deliberately having no process, HID, PnP,
44:  * power, or filesystem side effects.
45:  */
46: export class InputCoordinatorMock {
47:   readonly lifecycle = new InputLifecycleMock();
48:   readonly ownership = new PhysicalInputOwnership();
49:   private readonly trace: InputCoordinatorTraceEvent[] = [];
50:   private gameInputCount = 0;
51:   private ymccActionCount = 0;
52:   private heldCleared = true;
`
stderr:

## D. Bounded product-root identifier search, excluding the mock

argv: rg -n 'gameInputCount|ymccActionCount|game-report|ymcc-semantic-only' src native InputHost | Where-Object { $_ -notmatch 'inputCoordinatorMock\\.ts' }
exitCode: 0
stdout:
`
src\bridge\inputOwnerRuntime.ts:11:  gameInputCount: number;
src\bridge\inputOwnerRuntime.ts:12:  ymccActionCount: number;
src\bridge\inputOwnerRuntime.ts:20:    schemaVersion: 1, owner: 'none', epoch: 0, gameInputCount: 0, ymccActionCount: 0,
src\bridge\inputOwnerRuntime.ts:29:    this.state.ymccActionCount += 1;
src\bridge\inputOwnerRuntime.ts:36:    this.state = { ...this.state, owner: 'none', epoch: this.state.epoch + 1, gameInputCount: 0, ymccActionCount: 0, heldCleared: true, chordCleared: true, shiftCleared: true };
src\bridge\inputOwnerRuntime.ts:41:    this.state = { ...this.state, owner, epoch: this.state.epoch + 1, gameInputCount: 0, ymccActionCount: 0, heldCleared: true, chordCleared: true, shiftCleared: true };
src\bridge\physicalInputOwnership.ts:16:export type FrameDisposition = 'game-report' | 'ymcc-semantic-only' | 'drop';
src\bridge\physicalInputOwnership.ts:100:    if (this.owner === 'ymcc-frontend') return 'ymcc-semantic-only';
src\bridge\physicalInputOwnership.ts:101:    if (this.owner === 'game-consumer') return 'game-report';
`
stderr:

## Normalized finding and boundary

- `inputCoordinatorMock.ts` contains the only located increment of
  `gameInputCount`; it is explicitly an F1/F2 integration mock with no
  process, HID, PnP, power, or filesystem side effects.
- The product renderer mirror contains no `gameInputCount` increment. Its
  `ymccActionCount` reflects receipt of an IPC UI semantic action only.
- `physicalInputOwnership.ts` is a pure owner/disposition model. Its
  `game-report` label is not a Steam or game observation point.
- Therefore `gameInputCount=0`, whether from the mock or the renderer mirror,
  is `not-a-consumer-measurement`. The bounded source search located no
  external Steam/game-consumer measurement producer in `src`, `native`, or
  `InputHost`; static absence is not proof that an external consumer cannot
  receive input.

Disposition: `P05-SOURCE-LOCATED / MOCK-PRODUCER-PRESENT /
EXTERNAL-CONSUMER-MEASUREMENT-ABSENT / RUNTIME-BLOCKED`.

No `T22-Cxx` source change follows from this finding: any change purporting to
measure or block the external consumer would depend on device identity and
runtime consumer behavior, which fails E2 condition (c).

## C. Renderer mirror and UI dispatch are not an external game-consumer probe

argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\inputOwnerRuntime.ts, lines 1-43; Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\gamepad\engine.ts, lines 151-157 and 232-239
exitCode: 0
stdout:
`
1: /**
2:  * Renderer-side mirror of the native owner transaction. It is observability
3:  * only: it cannot hide a device or revoke another process' XInput access.
4:  */
18: export class InputOwnerRuntime {
19:   private state: RuntimeOwnerSnapshot = {
20:     schemaVersion: 1, owner: 'none', epoch: 0, gameInputCount: 0, ymccActionCount: 0,
21:     heldCleared: true, chordCleared: true, shiftCleared: true,
22:   };
24:   start(): RuntimeOwnerSnapshot { return this.commit('game-consumer'); }
25:   summon(): RuntimeOwnerSnapshot { return this.commit('ymcc-frontend'); }
26:   dismiss(): RuntimeOwnerSnapshot { return this.commit('game-consumer'); }
27:   semanticAction(): RuntimeOwnerSnapshot {
28:     if (this.state.owner !== 'ymcc-frontend') return this.snapshot();
29:     this.state.ymccActionCount += 1;
35:   stop(_reason: RuntimeStopReason): RuntimeOwnerSnapshot {
36:     this.state = { ...this.state, owner: 'none', epoch: this.state.epoch + 1, gameInputCount: 0, ymccActionCount: 0, heldCleared: true, chordCleared: true, shiftCleared: true };
40:   private commit(owner: Exclude<RuntimeInputOwner, 'none'>): RuntimeOwnerSnapshot {
41:     this.state = { ...this.state, owner, epoch: this.state.epoch + 1, gameInputCount: 0, ymccActionCount: 0, heldCleared: true, chordCleared: true, shiftCleared: true };
151: // This is a native decision mirror, not a second gate. While true the
152: // renderer only advances its browser snapshot and never dispatches actions;
153: // YeManCC native has already forwarded the semantic action to the child.
155: const inputOwnerRuntime = new InputOwnerRuntime();
156: function publishOwnerTrace(): void {
157:   if (typeof window !== 'undefined') window.dispatchEvent(new CustomEvent('input:owner-trace', { detail: inputOwnerRuntime.snapshot() }));
232:   window.addEventListener('ipc:gamepad.ui-input', ((e: CustomEvent<{ action?: string }>) => {
235:     nativeUiInputActive = true;
236:     inputOwnerRuntime.semanticAction();
237:     publishOwnerTrace();
238:     enqueueGamepadTaskDetached(() => dispatchNativeUiAction(engineOpts!, action));
`
stderr:

## B. The two increment sites are real, but only inside the mock

argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\inputCoordinatorMock.ts, lines 106-129
exitCode: 0
stdout:
`
106:   submit(controller: ControllerFrame, motion?: MotionFrame): InputCoordinatorFrameResult {
107:     const lifecycle = this.lifecycle.snapshot();
108:     const disposition = ['admitted', 'active'].includes(lifecycle.phase) && lifecycle.epoch === controller.epoch
109:       ? this.ownership.routeFrame(controller.epoch)
110:       : 'drop';
111:     if (disposition === 'drop') {
112:       const trace = this.record('frame-rejected', disposition, controller.inputSequence, false);
113:       return { disposition, frame: null, trace };
114:     }
115:     const assembled = assembleCanonicalFrame(controller, motion);
116:     if (!assembled.ok) {
117:       const trace = this.record(`frame-${(assembled as { ok: false; reason: string }).reason}`, 'drop', controller.inputSequence, false);
118:       return { disposition: 'drop', frame: null, trace };
119:     }
120:     if (this.lifecycle.snapshot().phase === 'admitted') {
121:       this.lifecycle.transition('frame', { configRevision: controller.configRevision });
122:     }
123:     // Foreground-exclusive routing is semantic-only: a canonical frame is
124:     // available to the UI dispatcher, but never treated as a game report.
125:     if (disposition === 'game-report') this.gameInputCount += 1;
126:     if (disposition === 'ymcc-semantic-only') { this.ymccActionCount += 1; this.markInputStateHeld(); }
127:     const trace = this.record('frame', disposition, controller.inputSequence, disposition === 'ymcc-semantic-only' ? true : assembled.frame.neutral);
128:     return { disposition, frame: assembled.frame, trace };
129:   }
`
stderr:
