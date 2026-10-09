# T22 BUS-P120：控制器内页连接态晚订阅修正

日期：2026-09-06
状态：`SOURCE-FIXED / BUILD-DEPLOY-VERIFIED / CACHE-CLEARED / NATIVE-STATE-REPLAY-PROVEN / UI-PAGE-RECEIPT-PENDING`

## 1. 用户现象

native 日志已经有真实 XInput 连接：

```text
connectedMask=1
primarySlot=0
source=xinput
```

但进入 YMCC「控制器」内页后仍显示未连接。

## 2. 明确源码根因

`GamepadVisualizer.vue` 只在该控制器内页组件 `onMounted()` 时订阅：

```ts
stopState = onIpc('gamepad.state', ...)
```

组件没有查询 native 当前状态；而 native 的 `gamepad.state` 是一次性状态广播。用户先打开主界面、随后才进入控制器页时，组件错过了首帧/连接态广播，`connected` 默认值一直是 `false`。

组件内的 `poll()` 也没有读取设备：

```ts
const pads: Gamepad[] = [];
```

这是有意保持单一 native 输入源，不应恢复 `navigator.getGamepads()`。

## 3. 修正

文件：`src/bridge/ipc.ts`

增加 `gamepad.state` 最近快照缓存：

```ts
let lastGamepadState: unknown = null;
let hasLastGamepadState = false;
```

native 状态事件到达时缓存；后挂载的 `on('gamepad.state', handler)` 注册完成后，通过 microtask 补发最近快照。

这样控制器内页会显示同一份真实 native 状态，不会启动第二个物理读取器，也不会创建虚拟手柄。

## 4. 构建、部署、缓存证据

```text
pnpm run type-check → PASS
pnpm run build      → PASS
native BUILD_OK     → PASS
InputHost build     → 0 warnings / 0 errors
deploy-installed    → DEPLOY_OK
```

构建/安装文件：

```text
YeManCC.exe SHA-256 = 856ACF9DD9844BA53E710AF83F6EBF0CA13C9626B72DBA7F0E3792E537CBBF44
Web index SHA-256    = A0B3259EEE825DDF0B1C436EFFD9F70FA30A2402132DF9982DB245...
```

已停止旧实例，并将 WebView 缓存移出当前目录以便新建：

```text
C:\Users\DaVe\AppData\Local\YeManCC\EBWebView
→ C:\Users\DaVe\AppData\Local\YeManCC\.EBWebView-cache-backup-20260906-203908
```

当前新缓存已重新生成。

## 5. 新实例证据

实例 PID `27852`：

```text
gamepad-input-registration
  rawInputRegistered=true
  fallbackTimerArmed=true

gamepad-xinput-state
  connected=true
  connectedMask=1
  primarySlot=0
  source=xinput

gamepad-state-replay
  reason=webview-render-ready
```

当前安装目录与 workspace 构建产物的 Web bundle hash 一致，native 也一致。

## 6. 证据边界

### 已闭合

- native XInput 连接态；
- WebView 首帧连接态重放；
- 控制器页面的晚订阅补发；
- 不使用浏览器第二读取器；
- 不创建虚拟手柄；
- 不改变 HC owner/focus/release 隔离。

### 仍需运行态观察

当前日志能证明 native snapshot 与 bridge cache 已建立，但没有自动化 DOM 读取器直接读取控制器页面的文字状态。实体按钮输入仍不伪造；主线不把这项 UI 观察缺口冒充为按钮语义 runtime PASS。

