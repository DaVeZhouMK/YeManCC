# T22-P05/P06 raw static evidence

capturedUtc: 2026-09-04T15:05:54.9210733Z
cwd: G:\YeManCC-Work
runtimeOperation: false

## P05 — owner counter and XInput slot provenance
argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\inputOwnerRuntime.ts
exitCode: 0
stdout:
`
/**
 * Renderer-side mirror of the native owner transaction. It is observability
 * only: it cannot hide a device or revoke another process' XInput access.
 */
export type RuntimeInputOwner = 'none' | 'game-consumer' | 'ymcc-frontend';
export type RuntimeStopReason = 'close' | 'blur' | 'suspend' | 'disconnect' | 'crash';
export interface RuntimeOwnerSnapshot {
  schemaVersion: 1;
  owner: RuntimeInputOwner;
  epoch: number;
  gameInputCount: number;
  ymccActionCount: number;
  heldCleared: boolean;
  chordCleared: boolean;
  shiftCleared: boolean;
}

export class InputOwnerRuntime {
  private state: RuntimeOwnerSnapshot = {
    schemaVersion: 1, owner: 'none', epoch: 0, gameInputCount: 0, ymccActionCount: 0,
    heldCleared: true, chordCleared: true, shiftCleared: true,
  };

  start(): RuntimeOwnerSnapshot { return this.commit('game-consumer'); }
  summon(): RuntimeOwnerSnapshot { return this.commit('ymcc-frontend'); }
  dismiss(): RuntimeOwnerSnapshot { return this.commit('game-consumer'); }
  semanticAction(): RuntimeOwnerSnapshot {
    if (this.state.owner !== 'ymcc-frontend') return this.snapshot();
    this.state.ymccActionCount += 1;
    this.state.heldCleared = false;
    this.state.chordCleared = false;
    this.state.shiftCleared = false;
    return this.snapshot();
  }
  stop(_reason: RuntimeStopReason): RuntimeOwnerSnapshot {
    this.state = { ...this.state, owner: 'none', epoch: this.state.epoch + 1, gameInputCount: 0, ymccActionCount: 0, heldCleared: true, chordCleared: true, shiftCleared: true };
    return this.snapshot();
  }
  snapshot(): RuntimeOwnerSnapshot { return { ...this.state }; }
  private commit(owner: Exclude<RuntimeInputOwner, 'none'>): RuntimeOwnerSnapshot {
    this.state = { ...this.state, owner, epoch: this.state.epoch + 1, gameInputCount: 0, ymccActionCount: 0, heldCleared: true, chordCleared: true, shiftCleared: true };
    return this.snapshot();
  }
}

`
stderr:

argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\main.cpp, lines 5535-5575
exitCode: 0
stdout:
`
5535:static void gamepadReadState() {
5536:    bool live = false;
5537:    XINPUT_GAMEPAD pad{};
5538:    g_xinputConnectedMask = 0;
5539:    g_xinputPads.fill(XINPUT_GAMEPAD{});
5540:    for (DWORD i = 0; i < 4; i++) {
5541:        XINPUT_STATE s; ZeroMemory(&s, sizeof(s));
5542:        if (XInputGetState(i, &s) == ERROR_SUCCESS) {
5543:            g_xinputConnectedMask |= (1u << i);
5544:            g_xinputPads[i] = s.Gamepad;
5545:            live = true;
5546:        }
5547:    }
5548:    if (g_xinputConnectedMask != 0) {
5549:        // Prefer the slot that actually carries the summon combo. ROG may
5550:        // expose two XInput faces; choosing slot 0 unconditionally can miss
5551:        // LB+RB when the active face is slot 1. Once selected, keep the slot
5552:        // stable until it disconnects or all controls become neutral.
5553:        const WORD summonBits = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER;
5554:        DWORD comboSlot = 0;
5555:        for (DWORD i = 0; i < 4; ++i) {
5556:            if ((g_xinputConnectedMask & (1u << i)) &&
5557:                (g_xinputPads[i].wButtons & summonBits) == summonBits) { comboSlot = i; break; }
5558:        }
5559:        if (comboSlot != 0 || (g_xinputConnectedMask & 1u) == 0 ||
5560:            (g_xinputConnectedMask & (1u << g_xinputSemanticSlot)) == 0) {
5561:            g_xinputSemanticSlot = comboSlot;
5562:            if ((g_xinputConnectedMask & (1u << g_xinputSemanticSlot)) == 0) {
5563:                g_xinputSemanticSlot = 0;
5564:                while (g_xinputSemanticSlot < 4 &&
5565:                       (g_xinputConnectedMask & (1u << g_xinputSemanticSlot)) == 0) ++g_xinputSemanticSlot;
5566:            }
5567:        }
5568:        g_xinputPrimarySlot = g_xinputSemanticSlot;
5569:        pad = g_xinputPads[g_xinputPrimarySlot];
5570:    }
5571:    if (g_xinputConnectedMask != g_xinputLoggedMask) {
5572:        g_xinputLoggedMask = g_xinputConnectedMask;
5573:        json slots = json::array();
5574:        for (DWORD i = 0; i < 4; ++i) {
5575:            if (g_xinputConnectedMask & (1u << i)) slots.push_back(i);
`
stderr:

## P06 — lifecycle after T22-C01
argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\InputHost\Program.cs, lines 30-59 and 139-154
exitCode: 0
stdout:
`
30:    static void RunFile(string statePath)
31:    {
32:        EnsureController();
33:        var last = "";
34:        while (true)
35:        {
36:            try
37:            {
38:                if (!File.Exists(statePath)) { Thread.Sleep(20); continue; }
39:                var text = File.ReadAllText(statePath);
40:                if (text == last) { Thread.Sleep(4); continue; }
41:                last = text;
42:                using var doc = JsonDocument.Parse(text);
43:                var root = doc.RootElement;
44:                // A stop record carries both neutral=true and alive=false. Neutral
45:                // must be submitted before leaving this loop; otherwise native can
46:                // restore physical visibility while the previous virtual report is
47:                // still latched. This is best-effort only: the file protocol has no
48:                // Coordinator ACK or release proof.
49:                var stopRequested = root.TryGetProperty("alive", out var alive) && alive.ValueKind == JsonValueKind.False;
50:                if (root.TryGetProperty("neutral", out var neu) && neu.ValueKind == JsonValueKind.True)
51:                    SubmitNeutral();
52:                else if (!stopRequested)
53:                    Submit(root);
54:                if (stopRequested)
55:                    return;
56:            }
57:            catch
58:            {
59:                Thread.Sleep(20);
139:    static void SubmitNeutral()
140:    {
141:        if (controller is null) return;
142:        var profile = ctx!.GetProfile(persona)!;
143:        var state = new HMGamepadState
144:        {
145:            Buttons = HMButton.None,
146:            Hat = HMHat.None,
147:            Axes = HMGamepadStateHelpers.StandardAxes(profile, 0, 0, 0, 0, 0, 0),
148:        };
149:        controller.SubmitState(in state);
150:    }
151:
152:    static void Release()
153:    {
154:        try { SubmitNeutral(); } catch { }
`
stderr:

argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\main.cpp, lines 5210-5236
exitCode: 0
stdout:
`
5210:static void inputHostWrite(const json& rec) {
5211:    if (g_inputHostStatePath.empty()) return;
5212:    const std::wstring tmp = g_inputHostStatePath + L".tmp";
5213:    {
5214:        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
5215:        if (!out) return;
5216:        const auto text = rec.dump();
5217:        out.write(text.data(), (std::streamsize)text.size());
5218:    }
5219:    MoveFileExW(tmp.c_str(), g_inputHostStatePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
5220:}
5221:static void inputHostStop() {
5222:    if (!g_inputHostStatePath.empty()) {
5223:        // InputHost consumes neutral before alive=false and exits through its
5224:        // finally/Dispose path. The legacy state-file protocol has no ACK, so
5225:        // this is not a release proof; it only preserves the required local
5226:        // ordering before any physical visibility restore.
5227:        inputHostWrite({{"alive", false}, {"neutral", true}});
5228:    }
5229:    if (g_inputHostProcess) {
5230:        if (WaitForSingleObject(g_inputHostProcess, 1500) == WAIT_TIMEOUT)
5231:            TerminateProcess(g_inputHostProcess, 0);
5232:        CloseHandle(g_inputHostProcess);
5233:        g_inputHostProcess = nullptr;
5234:    }
5235:    hidHideRestorePhysical();
5236:}
`
stderr:
