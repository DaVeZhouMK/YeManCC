# T22-BUS-P129：HC XInput canonical read source fix（2026-09-06）

## 1. 范围与结论

本轮只处理一个已经由三方源码对照明确证明的 XInput source divergence：
当前 YMCC 曾把 `xinput1_4.dll` ordinal `#100` 的私有 `XInputStateSecret`
当作完整 `XINPUT_STATE` 读取；锁定 HC 与 Git `v0.0.28` 都没有这个语义。

本轮已修正为：

```text
canonical ButtonState / AxisState = xinput1_4.dll!XInputGetState
Xbox Special probe                  = xinput1_4.dll ordinal #100 only
```

这只闭合了 **canonical XInput source 读取的静态偏差**，不能宣称
Steam/game/XInput consumer 隔离、P-HID、P-OWNER、ROG motion 或恢复事务已经闭合。

## 2. 三方证据

### 2.1 HC source fact

锁定 HC：

`deps/handheldcompanion-runtime/source/Controllers/XInputController.cs`

- `UpdateXInputState()` 先调用 `XInputGetStateSecret14(UserIndex, out State)`；
- 完整实体按钮、扳机、左右摇杆来自 `Controller.GetState().Gamepad`；
- `State.wButtons` 只用于 `ButtonFlags.Special` 的 Xbox bit（`1024`）；
- `XInputStateSecret` 字段为 `eventCount + wButtons + triggers + four axes`，
  不是 HC 的 canonical `Gamepad` 对象来源。

### 2.2 Git `v0.0.28` source fact

`git show v0.0.28:native/main.cpp` 的旧 `gamepadReadState()`：

```cpp
XINPUT_STATE s{};
if (XInputGetState(i, &s) == ERROR_SUCCESS) {
    live = true;
    pad = s.Gamepad;
    break;
}
```

因此旧主线也使用标准 `XInputGetState()` 作为 canonical
`XINPUT_GAMEPAD` 来源，没有把 ordinal `#100` 作为完整状态读取。

### 2.3 Current source fact（修正前）

修正前 `native/main.cpp` 将 ordinal `#100` 声明为：

```cpp
DWORD (WINAPI *)(DWORD, XINPUT_STATE*)
```

并用 `s.Gamepad` 填充所有按钮、扳机和轴。这在字节布局上可能“看起来可用”，
但 provenance/contract 与 HC 不同，且把 private secret state 当成 canonical
state，属于明确的 YMCC-only 偏差。

## 3. 本轮源码改动

`native/main.cpp`：

1. 新增与 HC 对齐的 `YmccXInputStateSecret` 结构；
2. 从 `xinput1_4.dll` 按名字解析 `XInputGetState`，作为唯一 canonical read；
3. ordinal `#100` 改为独立 secret probe，仅读取 Xbox Special bit `1024`；
4. canonical `XINPUT_GAMEPAD` 不再由 secret probe 覆盖；
5. lifecycle/input receipt 增加 `canonicalResolved`、`secretResolved`、
   `secretRole=xbox-special-only` 和 `specialXbox` 字段，便于后续区分来源。

没有新增 HidHide、OEM Disable、XInputPlus、reWASD 或第三方隔离路线。

## 4. 验证证据

| 命令 | 结果 |
|---|---|
| `cmd /c native\\build_native.bat` | `BUILD_OK` |
| `pnpm run type-check` | PASS |
| `pnpm run test:input-host-supervisor` | PASS |
| `pnpm run test:input-runtime-admission` | PASS |
| `pnpm run test:button-mapping-runtime-readiness` | PASS |
| `pnpm run test:input-owner-runtime-integration` | PASS |
| `pnpm run test:input-route-evidence` | PASS，保留 3 个 `UNENCLOSED` migration candidate |
| `pnpm run test:physical-input-ownership` | PASS |
| `pnpm run test:hc-parity-drift-ledger` | PASS |
| `git diff --check -- native/main.cpp` | PASS |

曾误调用不存在的 `pnpm run test:hc-parity-ledger`，返回
`ERR_PNPM_NO_SCRIPT`；随后按 package scripts 修正为
`test:hc-parity-drift-ledger` 并通过。该工具命名错误不属于产品运行错误。

本轮构建产物为：

```text
G:\YeManCC-Work\Mainline\Build\App\Native\YeManCC.exe
```

## 5. 仍未闭合的边界（不是猜测）

以下项目在本轮没有被错误升级：

- P-HID、P-XINPUT、P-OWNER 仍是三条独立平面；HidHide 不能证明 XInput 阻断；
- Steam、实际游戏、GameInput 或其他外部 XInput consumer 未观测；
- current source 仍有多 slot 选择、ROG HID fallback、Raw Input + fallback timer、
  neutral admission 等 YMCC 外壳逻辑；它们不是 HC `Controller.GetState().Gamepad`
  的替代读取，但其跨设备/跨 owner 行为仍需单独证据；
- ROG gyro provider、same-provider pair、HC calibration、DS4 descriptor/raw report
  与恢复事务仍保持原有 `UNENCLOSED / RUNTIME-BLOCKED`。

因此本轮状态只能写为：

```text
P129 = HC-CANONICAL-XINPUT-READ-SOURCE-FIXED
      / BUILD-AND-STATIC-REGRESSION-PASS
      / CONSUMER-ISOLATION-UNENCLOSED
      / RF00-T20-T21-T14-REFRESHED
```

## 6. Post-fix provenance refresh

After the P129 report and TASK index were written, the required static refresh
was completed:

```text
manifestId    = T22-RF00-20260906-T10-I-HC-ROUTE-STATIC-SNAPSHOT
sourceDigest  = 41FF57702ECF777BD409B1D7FE5820A81F9891B1C34CB0E9CB1E2F6861B5B80C
keyFileCount  = 74
porcelainCount= 521
T20           = 4/13/4
T21 claims    = 9
T14 range     = DGF-01…DGF-15
runtimeUpgrade= false
```

The refresh remains a static provenance gate. It does not change the
`P-XINPUT=UNENCLOSED` or external consumer boundary above.

## 7. Installed smoke receipt

With the explicitly identified old `YeManCC.exe` process stopped, the current
build was deployed by `tools/deploy-installed.ps1` (`DEPLOY_OK`) and started
again from `C:\SOFT\YeMan\YeManCC`.

```text
installed PID                         = 37752
installed/build SHA-256               = B8E50DCCDF0A5BC562D065B2CAC2077795043ECB12E9303A40A84B4771C1904B
canonicalEntryPoint                  = XInputGetState
canonicalResolved                    = true
secretEntryPoint                     = #100
secretResolved                       = true
secretRole                           = xbox-special-only
connectedMask / primarySlot          = 1 / 0
source / buttonMask / triggers       = xinput / 0 / 0 / 0
neutral admission                    = admitted-neutral
virtual target at start              = disabled (publication suppressed)
ROG HID match                        = false
```

This is a local YMCC startup/source receipt only. It does not observe Steam,
the game, HidHide, P-HID, P-XINPUT or P-OWNER behavior.

## 8. Read-only physical XInput observation

The existing read-only probe `tools/xinput_physical_observation_probe.ps1`
was run for 5 seconds. It reported `connected=true`, `connectedSlots=[0]`,
`buttonObserved=false`, `axisObserved=false`, and `systemMutation=false`.
This confirms that the physical XInput API remains visible to a separate local
observer; it does not identify Steam/game consumer counts, and it does not
prove that the YMCC owner route is wrong after the canonical-read fix.
