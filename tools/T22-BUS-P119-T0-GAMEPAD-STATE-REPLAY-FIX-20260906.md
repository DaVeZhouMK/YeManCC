# T22 BUS-P119：真实手柄连接态首帧重放修正

日期：2026-09-06
状态：`SOURCE-FIXED / BUILD-VERIFIED / DEPLOY-VERIFIED / RENDER-READY-REPLAY-PROVEN / BUTTON-RUNTIME-UNOBSERVED`

## 1. 明确根因

BUS 对照发现，XInput 连接态第一次广播早于前端安装监听器：

```text
gamepadReadState() → ipc_emit("gamepad.state")
                  → WebView navigation/render-ready
                  → App.vue startGamepad()/renderer listener
```

旧代码只在状态变化时广播；`g_gamepadStatePublished=true` 后，同一连接态不会再次广播。因此 renderer 的 `nativePadConnected` 会永久保持 `false`，`startGamepad()` 进入“无手柄，待连接”，这解释了前端控制器检测失效。

这不是虚拟手柄缺失，也不是 HidHide/XInput 阻断问题；不放宽 HC/owner 的前台与可见 gate。

## 2. 最小源码修正

文件：`native/main.cpp`

在 `finalizeWebViewRenderReady()` 中，`g_webviewReady=true` 后增加：

```cpp
g_gamepadStatePublished = false;
gamepadReadState();
appendNativeLifecycleLog("gamepad-state-replay", {
    {"reason", "webview-render-ready"}
});
```

语义：只重放当前真实 XInput snapshot；不创建设备、不增加物理读取器、不合并 slot、不改变 owner/release/focus gate。WebView recovery 的新 generation 也会重新经过同一 render-ready 边界。

## 3. 构建与部署证据

执行：

```text
pnpm run type-check
→ vue-tsc --noEmit PASS

pnpm run build
→ Web build PASS
→ native BUILD_OK
→ InputHost build 0 warnings / 0 errors

cmd /c native\build_native.bat
→ BUILD_OK

powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\deploy-installed.ps1 -WorkspaceRoot 'G:\YeManCC-Work\Mainline'
→ DEPLOY_OK
→ backup: C:\SOFT\YeMan\YeManCC\.deployment-backup-20260906-202224
```

构建与安装文件一致：

```text
bytes  = 2,262,528
SHA256 = 7762A104AB6A72AEB41F2A0D4C25494CEB74F9FAA48AC3654F1B655EEE5963A1
```

## 4. 新实例 runtime 证据

日志：`C:\Users\DaVe\AppData\Local\YeManCC\native-lifecycle.log`

当前实例 PID `20276`：

```text
20:22:32 gamepad-input-registration
         rawInputRegistered=true
         fallbackTimerArmed=true
         rawInputError=0

20:22:32 gamepad-xinput-state
         connected=true
         connectedMask=1
         primarySlot=0
         source=xinput

20:22:33 gamepad-state-replay
         reason=webview-render-ready
```

对应 WebView 日志确认 render-ready 在首帧完成后发生；实例保持存活、窗口可见且前台：

```text
PID=20276
Visible=true
Iconic=false
ForegroundWindow=YeManCC HWND
Responding=true
```

WebView2 后续发生一次 browser recovery 时，第二次 `gamepad-state-replay` 也出现，说明 recovery generation 的连接态重放路径同样生效。

## 5. 当前闭合与未闭合

### 已闭合

- native XInput slot 0 连接检测；
- WebView 首帧前后连接态广播竞态；
- renderer 初始 `nativePadConnected` 不再永久卡在 false；
- 不依赖 `navigator.getGamepads()`，不引入第二物理读取源；
- 不改 HC owner/focus/release 隔离；
- 不启用虚拟手柄、HidHide、InputHost。

### 尚未能由无按键自动观测闭合

当前本机没有软件 API 可以注入实体 Xbox 360 按钮状态；因此仍不能仅靠中性连接态证明：

```text
buttonMask != 0
→ gamepadEval edge
→ gamepad-ui-input-emitted 或 gamepad-ui-input-dropped
→ renderer action
```

这不是当前已知代码缺口，而是实体按键 runtime 证据缺口。禁止用虚拟手柄或伪造按钮 receipt 代替。

## 6. 交付边界

当前安装包已具备正确的真实手柄连接态初始化和恢复重放，源码/构建/部署一致；但“真实 A/B/LB/RB 按下后 UI 动作”仍未被本机自动观测。正式 Release/updater 未修改，默认虚拟手柄、HidHide 和断开自动恢复仍关闭。

