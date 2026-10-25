# Fan signal consumer

This module runs saved, reviewed fan signals through the current product `FanHostLifecycle` and `HttpFanApiAdapter` TypeScript. It reuses the existing `createFanLogFixture` and its VM-local `VirtualClock`. The server is the inert `NoIoHost`; all client operations, including Open, lease, curve, restore, release and Close, pass through the real HTTP serializer and receipt validator. No Host process, live network, settings, OS sleep, EC/OEM/HID/driver access or installation is performed.

## Public entrypoints

```js
import { replayFanSignals, runFanSignalRegressions } from './fan-signal-consumer.mjs';
const report = await replayFanSignals({
  repoRoot, events: plan.events, sourceArchive: plan.sourceArchive,
  runtimeContract: {
    protocolVersion: expectedProtocol,
    hcVersion: candidateVersion,
    runtimeRoot, sharedRuntimeRoot, hostPayloadRoot, payloadSha256,
  },
});
const regressions = await runFanSignalRegressions({ repoRoot });
```

The repository root defaults to this module's containing repository. HC version and runtime locations are parameters and never used to select a hardcoded HC release. Runtime paths and payload identity are declared inputs here; the separate runtime connectivity/ABI gate must load and validate candidate assemblies. `COMPATIBLE_PROTOCOL_ONLY` never asserts assembly or hardware compatibility. A declared protocol mismatch produces `FAN_PROTOCOL_INCOMPATIBLE`, an explicit compatibility difference and FAIL. Missing protocol remains UNKNOWN.

## Reviewed event contract

`startup.handshake` accepts flat literal `ok`, `supported`, `protocolVersion`, `hostMode`, `hardwareCapable`, `hardwareWritesEnabled`, `hardwareWritesObserved`, `fanRoute`, `fanRouteWriteReady`, `mockZeroHardwareEvidence` and optional `deviceClass`. Only the named fields survive. Event identity uses `id`, `memberId`, `line`, `atMs`, `domain`; no log free text, token, lease identity, settings or raw reason is returned.

A log handshake is a reply observation. Its reconstructed startup entry is always marked `source-derived-fixture`. The known unavailable shape can supply synthetic `ok=true/supported=false` solely to reproduce the documented refusal; the supplied fields are listed separately. If extracted events lack the fields required for reconstruction, the consumer returns `SOURCE_FIELDS_NOT_EXTRACTED` at input, with `actualProductRejection=false`, and does not start. This does not claim the fields were absent in the source log.

Startup observations do not contain enough identity to prove one persistent Host session. Each later startup observation uses a separate memory context; retiring a context is explicitly `sandbox-disposal;not-oem-release-proof`. This avoids an old simulated Ready result swallowing a later unavailable receipt. No cross-member causal ordering is inferred. Timing advances by the reviewed member's observed deltas and never waits on the wall clock.

Source-derived fixtures require `provenance="source-derived-fixture"`:

| Event | Fields | Production consumption |
| --- | --- | --- |
| `fan.intent` | `action=start/apply/disable/close/state`, `nodes` for apply, optional generation | Lifecycle public entrypoint; all HTTP operations use real adapter |
| `fan.power` | `phase`, positive `generation` | `setPowerGeneration` then `observePowerBoundary`; suspend only affects the inert lifecycle |
| `fan.clock` | `advanceMs` | VM-local virtual time |
| `api.transport-failure` | `winHttpCode=12029`, optional `/api/state` path | Real read transport decoding; preserves earlier declared refusal |

Fixture-only `simulation` fault injection requires the same provenance and supports `disconnectConsumer`, `missingFeedback`, `invalidFeedback`, endpoint `delays`, and `handshakeOverride`. Corruption is injected at the memory HTTP boundary so the production parser, rather than the event normalizer, must reject it. `settle=false` on a fixture intent permits a subsequent sleep/new-generation signal to supersede a pending request. Request and transition evidence retain exact event ownership.

Fault-locked re-probe replies may legally have `ok=false/supported=false/hardwareCapable=false` while keeping `hardwareWritesEnabled/Observed=true`. This reflects existing write debt. Neither the parser nor the simulator clears those fields. `sourceHardwareWriteDebt.clearedBySimulation=false` remains explicit; software cleanup is never physical OEM restoration proof.

## Result and coverage

The result includes `sourceChanges`, SHA-256 `sourceHashes`, `executedProductModules`, `inputConsumerStateFeedback` (also available as `evidence`), `compatibility`, `remainingGaps`, diagnostics and boundary flags. Every consumed signal has separate input, consumer, state and feedback. PASS is scoped to software signal consumption and scenario expectations. Expected product rejection is a successfully observed outcome, rather than successful hardware control.

- Missing reconstructed entry: input stage.
- Unextracted source fields: input stage, no dispatch.
- No reviewed consumer mapping: consumer stage. This means the simulator has no approved executable mapping; it does not assert the product lacks a subscriber.
- Missing/invalid reply: feedback stage, never a hardware acknowledgement.
- Illegal fields or stale generation: input stage.
- Declared protocol mismatch: consumer stage and explicit compatibility failure.

The event budget is 20,000, matching the bounded corpus reader. Timer advancement is bounded by the existing virtual clock's runaway protection. Fixture disposal clears only VM timers and local maintenance.

Run local verification from the repository:

```text
node --test tools/ymcc_sim/global/consumer/fan-signal-consumer.test.mjs tools/ymcc_sim/global/fan-log-regression.test.mjs
```

The fixed source-derived suite has sixteen scenarios. Additional node tests directly corrupt the production HTTP reply, verify legal legacy/mock/fault-locked contracts, mutate input curves, test candidate protocol parameters, and check source provenance and exact concurrent ownership. The UI rendering, current HC assemblies, firmware fan speed and physical restoration remain distinct gates.
