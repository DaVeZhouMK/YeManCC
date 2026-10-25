# Controller shortcut → monitor feedback contract v1

2026-10-17. User authorized delivery from the controller editor chat to the existing “监控” chat. This is a display-only interface. The action owner implements execution and emits the final result; the monitor owner implements fullscreen and left-monitor presentation. Neither consumer repeats or executes the action.

Source: `src/bridge/shortcutActionFeedback.ts`.
Native event: `input.shortcutFeedback`.
Public consumer: `onShortcutActionFeedback(listener)`, returning an unsubscribe function. The IPC event is broadcast by the existing host transport to the main renderer and live child renderers. The module has one underlying subscription, no polling/timer, no startup replay, a maximum 128-entry duplicate history, independent copies for each observer, and exception isolation.

Payload:
```ts
interface ShortcutActionFeedback {
  schemaVersion: 1;
  id: string; // unique result ID; never use title/text as the deduplication key
  actionId: string; // original action identity
  origin: 'controller-shortcut';
  state: 'applied' | 'saved' | 'pending' | 'failed';
  title: string;
  message: string;
  occurredAtUnixMs: number;
  expiresAtUnixMs: number; // display only until this deadline; maximum lifetime 10 s
  powerGeneration: number;
  value?: { text: string; number?: number; unit?: string };
  presentation: { fullscreen: true; leftMonitor: true };
}
```

Semantics:
- `applied`: the existing execution boundary returned the relevant success receipt. This is not proof the user saw an external app/menu.
- `saved`: original settings were saved, but hardware/output application was not confirmed, e.g. switching a fan preset while control is off.
- `pending`: the existing owner retained an intent and has not confirmed application. Merely placing work in a new queue must not emit “applied”.
- `failed`: readable failure without rule names, IDs, paths, raw stack traces or garbled headers.
- Expired/malformed/duplicate records are ignored. Do not restore old notices on renderer restart, resume or monitor activation.
- Successful visible transitions (summon/hide YMCC, open keyboard, return desktop, switch programs, frontend menu keys) and successful keyboard output are suppressed. Their failures remain eligible for notification because there was no visible result.
- Monitoring OFF does not revoke the result event. A monitor-owned transient notice may report OFF without re-enabling persistent monitoring. Do not change monitor preferences, enable another output, stop RTSS or open an alternative transport just to display a notice.
- Fullscreen presentation is a monitor owner capability. This contract does not claim all fullscreen carriers have already been verified.

Planned actions: `os.switchProgram`; existing `input.frontendButton` with `steam`/`steamQuickAccess`; `monitor.toggle`; `display.frameRateControl`; `fan.switchPreset`; `app.launchQuickApp`. Existing TDP/brightness/mouse/gyro/virtual-controller result paths also feed this interface when they can provide an actual result.

Important user routing: both Steam key and Steam-three-dot key use Ctrl+1 when no virtual controller exists, exactly as requested in the controller chat. With a live virtual Steam Deck, use existing screenButtons bit 1 for Steam and bit 2 for Quick Access, through the original InputHost writer. Do not change these semantics to Ctrl+2 without a new user correction.

Code ownership: controller chat owns this module, new shortcut execution modules and shortcut-local native changes; monitor chat owns renderer/transport/UI consumption. No monitor renderer or hardware sampling code is changed by the controller chat. Final execution and mainline-import evidence will be appended separately.

## 2026-10-17 producer handoff and reserved direction

The producer is now integrated into current mainline source. This task does not install or launch the generated executable. The official native build and Vite production build passed; source execution tests use memory ports, and UI tests use the actual source with a read-only settings snapshot. Fullscreen HUD transport, native game overlay rendering, physical key activation and installed WebView2 behavior still require their respective owners' validation.

Confirmed result paths: five new UI actions; Steam / Quick Access through the original virtual report writer or the requested Ctrl+1 fallback; original TDP, brightness and simulated-mouse receipts; gyro and virtual-controller intention as pending until confirmed by the original owner. The actual App TDP consumer is covered with revoked tickets, actual wattage, boundary/no-op, silent cooldown, and readable failure. Native result tickets reject stale power/revision/source epochs and replay.

The monitoring chat's later design direction is reserved, not implemented here: a secondary monitoring menu with two saved templates (usually simple/detailed), switching templates with a future user-defined shortcut, and a separate YMCC capability view for actual effective TDP, virtual controller and frame limiting. Final action IDs, interactions, storage and any protocol additions will be aligned later. Do not infer a new action or settings schema from this section.

For adjustment bars, v1 only carries confirmed value.number/unit where available. It carries no min/max/step. Preview ranges must be marked as examples; future real bars require trustworthy bounds and step metadata from the execution owner. Saved intent must never be presented as applied hardware state. Monitor-side placement/H5 changes remain with the monitoring owner.
